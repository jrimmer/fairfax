#pragma once

// End-to-end harness: boots the real QML window exactly as main() does (both
// go through AppServices) on a temporary database, and drives it with
// synthetic mouse and keyboard events at the on-screen position of the item a
// user would aim at. Nothing here touches the user's own database.

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>
#include <QtTest>
#include <functional>
#include <memory>

#include "appsetup.h"

namespace e2e {

// Ids of the records the default seed creates.
struct Seed {
    qint64 home = 0, work = 0;
    qint64 buyMilk = 0, electricity = 0, plants = 0, shelf = 0;
    qint64 report = 0, studio = 0, callAlex = 0, oldThing = 0;
};

// A small, fixed world: two lists, a handful of tasks on known days, one
// starred, one completed. Dates are relative to today so it never goes stale.
inline Seed seedWorld(Store &store) {
    Seed s;
    const QDate today = QDate::currentDate();
    store.begin("Seed", false);
    ListGroup group;
    group.id = store.nextId("list_groups");
    group.name = "Personal";
    group.order = 1;
    store.putGroup(group);
    auto list = [&](const QString &name, int color, double order) {
        TaskList l;
        l.id = store.nextId("lists");
        l.name = name;
        l.groupId = group.id;
        l.color = color;
        l.order = order;
        store.putList(l);
        return l.id;
    };
    s.home = list("Home", 0, 1);
    s.work = list("Work", 2, 2);
    Tag garden;
    garden.id = store.nextId("tags");
    garden.name = "garden";
    store.putTag(garden);
    auto task = [&](qint64 listId, const QString &title, int dueDays, int priority = 0) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = listId;
        t.title = title;
        if (dueDays != INT_MIN)
            t.dueDate = today.addDays(dueDays);
        t.priority = priority;
        t.order = t.id;
        store.putTask(t);
        return t.id;
    };
    s.buyMilk = task(s.home, "Buy milk", 0);
    s.electricity = task(s.home, "Pay electricity bill", -2, 3);
    s.plants = task(s.home, "Water the plants", 1);
    s.shelf = task(s.home, "Fix the shelf", INT_MIN);
    s.report = task(s.work, "Write status report", 2, 2);
    s.studio = task(s.work, "Book the studio", 3);
    s.callAlex = task(s.work, "Call Alex", 1);
    s.oldThing = task(s.work, "Old finished thing", -5);
    Task plants = store.task(s.plants);
    plants.tagIds.append(garden.id);
    store.putTask(plants);
    Task shelf = store.task(s.shelf);
    shelf.starred = true;
    store.putTask(shelf);
    Task old = store.task(s.oldThing);
    old.completed = true;
    old.completedAt = QDateTime::currentDateTime();
    store.putTask(old);
    store.commit();
    return s;
}

// ---------------------------------------------------------------- finding

inline QList<QQuickWindow *> quickWindows() {
    QList<QQuickWindow *> out;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *q = qobject_cast<QQuickWindow *>(w); q && q->isVisible())
            out << q;
    return out;
}

inline QQuickItem *findIn(QQuickItem *root, const std::function<bool(QQuickItem *)> &match, bool visibleOnly = true) {
    if (visibleOnly && !root->isVisible())
        return nullptr;
    if (match(root))
        return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = findIn(child, match, visibleOnly))
            return found;
    return nullptr;
}

// An item by objectName in any open window (popup windows included).
inline QQuickItem *findItem(const QString &name, bool visibleOnly = true) {
    for (QQuickWindow *w : quickWindows())
        if (QQuickItem *item = findIn(w->contentItem(), [&](QQuickItem *i) { return i->objectName() == name; }, visibleOnly))
            return item;
    return nullptr;
}

// A visible item whose `text` property is exactly `text`: menu entries, labels.
// A menu entry can carry its shortcut as one text property ("Label\tCtrl+Shift+E"),
// so when nothing matches exactly, a menu item is also matched by the label
// before its tab. `type` narrows by base class (e.g. "QQuickMenuItem").
inline QQuickItem *findText(const QString &text, const char *type = nullptr) {
    auto visible = [&](QQuickItem *i) {
        return i->width() > 0 && (!type || i->inherits(type)) && i->metaObject()->indexOfProperty("text") >= 0;
    };
    for (QQuickWindow *w : quickWindows())
        if (QQuickItem *item = findIn(w->contentItem(), [&](QQuickItem *i) {
                return visible(i) && i->property("text").toString() == text;
            }))
            return item;
    for (QQuickWindow *w : quickWindows())
        if (QQuickItem *item = findIn(w->contentItem(), [&](QQuickItem *i) {
                return visible(i) && i->inherits("QQuickMenuItem")
                       && i->property("text").toString().section('\t', 0, 0) == text;
            }))
            return item;
    return nullptr;
}

// A non-visual object (a Popup or Dialog) by objectName: they hang off the
// QObject tree of whichever item declares them, which a Loader may have
// reparented, so look through every visible item's own children.
inline QObject *findObject(const QString &name) {
    for (QQuickWindow *w : quickWindows()) {
        if (QObject *o = w->findChild<QObject *>(name))
            return o;
        QObject *found = nullptr;
        findIn(w->contentItem(), [&](QQuickItem *i) {
            found = i->findChild<QObject *>(name, Qt::FindDirectChildrenOnly);
            return found != nullptr;
        }, false);
        if (found)
            return found;
    }
    return nullptr;
}

inline QQuickWindow *windowTitled(const QString &part) {
    for (QQuickWindow *w : quickWindows())
        if (w->title().contains(part))
            return w;
    return nullptr;
}

// ---------------------------------------------------------------- driving

// Types text into a window one key at a time (QTest::keyClicks has no
// QWindow overload).
inline void typeInto(QWindow *w, const QString &text) {
    for (const QChar c : text) {
        const char ascii = c.toLatin1();
        QTest::keyClick(w, ascii);
    }
}

// FAIRFAX_E2E_SLOW=500 reports every wait that took that many milliseconds or
// more (and whether it succeeded): a wait that only ever ends by timing out is
// a wrong condition, and shows up here.
inline void reportSlowWait(qint64 took, bool ok, const char *file, int line) {
    static const int threshold = qEnvironmentVariableIntValue("FAIRFAX_E2E_SLOW");
    if (threshold > 0 && took >= threshold)
        qWarning("SLOWWAIT %lld ms %s %s:%d", static_cast<long long>(took), ok ? "ok" : "TIMEOUT", file, line);
}

// Lets pending layout in an item's window finish. A row that has just
// appeared is laid out on the next polish, and until then its children sit at
// stale positions, so a click would land on the wrong item. Call it before
// looking up the items to click; it runs the event loop, so a pointer held
// across it can go stale.
inline void settleLayout(QQuickItem *item) {
    if (item && item->window() && QQuickTest::qIsPolishScheduled(item->window()))
        QQuickTest::qWaitForPolish(item->window(), 3000);
}

// The same for a window, for a caller that will not touch an item afterwards (the
// one it clicked may be gone by the time the layout has run).
inline void settleWindow(QQuickWindow *w) {
    if (w && QQuickTest::qIsPolishScheduled(w))
        QQuickTest::qWaitForPolish(w, 3000);
}

// Leaves the pointer in a corner of the window that no list or popup reaches. A
// pointer resting over a list that is rebuilt under it gets "hover" events, and
// a menu that highlights what is under the pointer jumps there on its own.
inline void parkPointer(QQuickWindow *w) { QTest::mouseMove(w, QPoint(w->width() - 2, w->height() - 2)); }

inline QPoint centreOf(QQuickItem *item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

inline QPoint pointIn(QQuickItem *item, qreal fx, qreal fy) {
    return item->mapToScene(QPointF(item->width() * fx, item->height() * fy)).toPoint();
}

inline void click(QQuickItem *item, Qt::MouseButton button = Qt::LeftButton, QPoint at = QPoint(-1, -1)) {
    QVERIFY2(item, "click: no such item");
    QQuickWindow *w = item->window();
    const QPoint p = at.x() < 0 ? centreOf(item) : at;
    QTest::mouseMove(w, p);
    QTest::mouseClick(w, button, Qt::NoModifier, p);
}

inline void doubleClick(QQuickItem *item, QPoint at = QPoint(-1, -1)) {
    QVERIFY2(item, "doubleClick: no such item");
    QQuickWindow *w = item->window();
    const QPoint p = at.x() < 0 ? centreOf(item) : at;
    QTest::mouseMove(w, p);
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, p);
}

inline void drag(QQuickItem *from, QQuickItem *to) {
    QQuickWindow *w = from->window();
    const QPoint a = centreOf(from), b = centreOf(to);
    QTest::mouseMove(w, a);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, a);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i)
        QTest::mouseMove(w, a + (b - a) * i / steps);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, b);
}

inline QString className(const QObject *o) {
    QString name = o->metaObject()->className();
    const int q = name.indexOf("_QMLTYPE");
    return q > 0 ? name.left(q) : name;
}

// ------------------------------------------------------------ layout check

// Walks the visible items under `scan` and reports every one whose rectangle,
// mapped into `bounds`, pokes out of it by more than `tolerance`. Content
// inside a Flickable is scrollable, so only the Flickable itself is judged.
inline void collectOverflows(QQuickItem *bounds, QQuickItem *item, qreal tolerance, QStringList &out) {
    if (!item->isVisible() || item->opacity() <= 0.01)
        return;
    if (item->width() > 0 && item->height() > 0 && item != bounds) {
        const QRectF r = item->mapRectToItem(bounds, QRectF(0, 0, item->width(), item->height()));
        const QRectF b(0, 0, bounds->width(), bounds->height());
        if (r.left() < b.left() - tolerance || r.top() < b.top() - tolerance
            || r.right() > b.right() + tolerance || r.bottom() > b.bottom() + tolerance) {
            out << QString("%1 \"%2\" at (%3,%4 %5x%6) outside (0,0 %7x%8)")
                       .arg(className(item), item->objectName())
                       .arg(r.x(), 0, 'f', 1).arg(r.y(), 0, 'f', 1).arg(r.width(), 0, 'f', 1).arg(r.height(), 0, 'f', 1)
                       .arg(b.width(), 0, 'f', 1).arg(b.height(), 0, 'f', 1);
        }
    }
    if (item->inherits("QQuickFlickable"))
        return;
    for (QQuickItem *child : item->childItems())
        collectOverflows(bounds, child, tolerance, out);
}

inline QStringList overflows(QQuickItem *bounds, QQuickItem *scan, qreal tolerance = 1.5) {
    QStringList out;
    collectOverflows(bounds, scan, tolerance, out);
    return out;
}

// The root item of a Popup (the surface its background, header, content and
// footer are laid out in), found from the popup's content item.
inline QQuickItem *popupSurface(QObject *popup) {
    auto *content = popup->property("contentItem").value<QQuickItem *>();
    return content ? content->parentItem() : nullptr;
}

// ----------------------------------------------------------------- the app

// The offscreen platform activates every window it shows, so a Popup.Window
// (the date suggestions, Quick Entry's menus and tag matches) would take the
// keyboard from the field being typed in and make it lose focus after one
// letter. On a real desktop a popup never takes the keyboard from its parent;
// popup windows are marked as not accepting focus to behave the same here.
class PopupFocusGuard : public QObject {
public:
    static bool isPopupWindow(const QObject *o) {
        return qobject_cast<const QWindow *>(o) && QString(o->metaObject()->className()).contains("PopupWindow");
    }
    bool eventFilter(QObject *o, QEvent *e) override {
        // The window the popup belongs to is told it lost the keyboard; leaving
        // that unsaid keeps the field that was being typed in focused.
        if (e->type() == QEvent::FocusOut && qobject_cast<QQuickWindow *>(o) && !isPopupWindow(o)
            && isPopupWindow(QGuiApplication::focusWindow()))
            return true;
        return false;
    }
    static void install() {
        static PopupFocusGuard guard;
        static bool done = false;
        if (!done) {
            qApp->installEventFilter(&guard);
            done = true;
        }
    }
};

class App {
public:
    // `reopen` boots on an existing database (a restart) instead of a new one;
    // `withSync` starts the sync providers, which offscreen runs otherwise leave out;
    // `lastView` (the default) seeds ui.defaultView = "last" so every test that
    // predates My Day lands on the view it always did. A test that proves the
    // real fresh-database default passes lastView = false.
    explicit App(const std::function<Seed(Store &)> &seed = seedWorld, const QString &reopen = QString(), bool withSync = false,
                 bool lastView = true) {
        PopupFocusGuard::install();
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY2(m_dir.isValid(), "temporary directory");
        QString error;
        m_dbPath = reopen.isEmpty() ? m_dir.filePath("e2e.db") : reopen;
        QVERIFY2(m_store.open(m_dbPath, &error), qPrintable(error));
        if (seed) {
            ids = seed(m_store);
            // My Day is on by default, so a fresh store would open on it and
            // every existing test would land somewhere new. Keep them on the
            // view they always had unless a test asks otherwise.
            if (lastView && !m_store.setting("ui.defaultView").isValid())
                m_store.setSetting("ui.defaultView", "last");
        }
        m_services = std::make_unique<AppServices>(m_store, QString());
        m_services->backend.ensureDefaults();
        m_services->startSync(true, !withSync);
        m_services->install(m_engine, QString(), false);
        m_engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY2(!m_engine.rootObjects().isEmpty(), "Main.qml failed to load");
        window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1180, 760);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        settleLayout(window->contentItem());
        QTest::qWait(20);
        // No frame is ever presented offscreen, so QML animations never advance; a
        // row transition stuck half way would leave the rows below an opening
        // editor where they were, under it. The list is told to move rows at once.
        if (QQuickItem *list = findItem("taskList")) {
            QQmlProperty(list, "displaced").write(QVariant::fromValue<QObject *>(nullptr));
            QQmlProperty(list, "add").write(QVariant::fromValue<QObject *>(nullptr));
        }
        ok = true;
    }

    ~App() {
        // Closing every window first keeps teardown quiet.
        for (QQuickWindow *w : quickWindows())
            w->hide();
    }

    Backend &backend() { return m_services->backend; }
    AppServices &services() { return *m_services; }
    QString dir() const { return QFileInfo(m_store.path()).absolutePath(); }
    Store &store() { return m_store; }
    QQmlApplicationEngine &engine() { return m_engine; }
    QString dbPath() const { return m_dbPath; }

    // The task a user would see by this title, straight from the store.
    Task task(qint64 id) const { return m_store.task(id); }
    Task taskTitled(const QString &title) const {
        for (const Task &t : m_store.tasks())
            if (!t.deleted && t.title == title)
                return t;
        return {};
    }
    // What has keyboard focus, for failure messages.
    QString focusDescription() const {
        QQuickItem *f = window->activeFocusItem();
        return f ? className(f) + " \"" + f->objectName() + "\"" : QString("nothing");
    }
    // Every live title, for failure messages.
    QString titles() const {
        QStringList out;
        for (const Task &t : m_store.tasks())
            if (!t.deleted)
                out << t.title;
        return out.join(" | ");
    }
    QQuickItem *row(qint64 id) const { return findItem(QString("taskRow:%1").arg(id)); }
    QQuickItem *sidebarRow(const QString &sourceKey) const { return findItem("sidebarRow:" + sourceKey); }

    // Clicks a list or focus list in the sidebar, as a user would.
    bool openSource(const QString &key) {
        QQuickItem *r = sidebarRow(key);
        if (!r)
            return false;
        click(r);
        return backend().sourceKey() == key;
    }

    // Presses a key chord on the main window.
    void press(Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier) { QTest::keyClick(window, key, mods); }
    void type(const QString &text) { typeInto(window, text); }

    QQuickWindow *window = nullptr;
    Seed ids;
    bool ok = false;

private:
    QTemporaryDir m_dir;
    QString m_dbPath;
    Store m_store;
    std::unique_ptr<AppServices> m_services;
    QQmlApplicationEngine m_engine;
};

}  // namespace e2e
