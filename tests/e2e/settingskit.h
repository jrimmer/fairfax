#pragma once

// Helpers for the Settings end-to-end tests: a restartable session on one
// database, a driver for the Settings window's own controls (switches, combos,
// sliders, fields), and the table of every setting with the control that sets
// it and the visible effect that proves it works.

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include "harness.h"
#include "model.h"
#include "schedule.h"
#include "views.h"

namespace e2e {

// ---------------------------------------------------------------- session

// One database that outlives its App: start() boots the app on it, restart()
// closes the app and boots it again, as a user quitting and relaunching does.
struct Session {
    QTemporaryDir dir;
    std::function<Seed(Store &)> seed = seedWorld;
    std::unique_ptr<App> app;
    Seed ids;
    bool withSync = false;
    bool started = false;
    qint64 taskBaseline = 0, alarmBaseline = 0;   // The highest ids of the seeded world.

    // Removes what probes created (tasks, alerts) so the next row sees the
    // seeded world again.
    void dropProbeData() {
        Store &st = app->store();
        QVector<Task> tasks;
        QVector<Alarm> alarms;
        for (const Task &t : st.tasks())
            if (t.id > taskBaseline && !t.deleted)
                tasks.append(t);
        for (const Alarm &a : st.alarms())
            if (a.id > alarmBaseline && !a.deleted)
                alarms.append(a);
        st.begin("Clean up", false);
        for (Task &t : tasks) {
            t.deleted = true;
            st.putTask(t);
        }
        for (Alarm &a : alarms) {
            a.deleted = true;
            st.putAlarm(a);
        }
        st.commit();
    }

    bool start() {
        app.reset();
        app = std::make_unique<App>(started ? std::function<Seed(Store &)>() : seed, dir.filePath("e2e.db"), withSync);
        if (!app->ok)
            return false;
        if (!started) {
            ids = app->ids;
            for (const Task &t : app->store().tasks())
                taskBaseline = qMax(taskBaseline, t.id);
            for (const Alarm &a : app->store().alarms())
                alarmBaseline = qMax(alarmBaseline, a.id);
        }
        app->ids = ids;
        started = true;
        return true;
    }
    bool restart() { return start(); }
    App &operator*() { return *app; }
    App *operator->() { return app.get(); }
};

// ---------------------------------------------------------------- windows

inline void activate(QQuickWindow *w) {
    w->requestActivate();
    QTRY_VERIFY_WITH_TIMEOUT(w->isActive(), 5000);
}

inline QQuickWindow *settingsWindow() { return windowTitled("Precrime Settings"); }

// Opens Settings with its shortcut, as a user does, and makes it tall enough
// that no control hides below the fold.
inline void openSettings(App &app, QQuickWindow *&out) {
    out = nullptr;
    activate(app.window);
    QTest::keyClick(app.window, Qt::Key_Comma, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT((out = settingsWindow()) != nullptr && out->isExposed(), 4000);
    out->resize(720, 1100);
    activate(out);
}

// Closes Settings with its corner button. The offscreen platform reports both
// windows as active, and Settings answers Escape and Ctrl+W whenever it is
// active, so a test that goes back to the main window closes it first.
inline void closeSettings(App &app) {
    if (QQuickWindow *sw = settingsWindow()) {
        activate(sw);
        QTRY_VERIFY2_WITH_TIMEOUT([&] {
            if (!settingsWindow())
                return true;
            if (QQuickItem *button = findItem("settingsClose"))
                click(button);
            return false;
        }(), "Settings did not close", 5000);
    }
    activate(app.window);
}

inline const QStringList &paneNames() {
    static const QStringList names{"General", "Appearance", "Tasks", "Today & Lists", "Alerts", "Sync",
                                   "Backups", "Import & Export", "Calendar", "Automation", "Diagnostics"};
    return names;
}

inline void selectPane(QQuickWindow *sw, const QString &pane) {
    QQuickItem *tab = nullptr;
    QTRY_VERIFY2_WITH_TIMEOUT((tab = findItem("settingsPane:" + pane)) != nullptr, qPrintable("no pane " + pane), 5000);
    click(tab);
    QTRY_COMPARE_WITH_TIMEOUT(sw->property("pane").toInt(), int(paneNames().indexOf(pane)), 5000);
}

// A visible control by objectName, scrolled into view if the pane is taller
// than the window.
inline QString windowsDescription() {
    QStringList out;
    for (QQuickWindow *w : quickWindows())
        out << QString("\"%1\" %2x%3 pane=%4").arg(w->title()).arg(w->width()).arg(w->height()).arg(w->property("pane").toString());
    return out.join("; ");
}

inline void control(const QString &name, QQuickItem *&out) {
    out = nullptr;
    QTRY_VERIFY2_WITH_TIMEOUT((out = findItem(name)) != nullptr, qPrintable("no control " + name + ". Windows: " + windowsDescription()), 5000);
    // A pane that has just been opened lays itself out over a few frames (its
    // fields start far too wide); wait until the control has stopped moving.
    QPoint last(-1, -1);
    bool needInside = false;   // Only once scrolled into view.
    auto settled = [&] {
        const QPoint now = centreOf(out);
        const bool still = now == last;
        const bool inside = now.x() >= 0 && now.y() >= 0 && now.x() < out->window()->width() && now.y() < out->window()->height();
        last = now;
        return still && (inside || !needInside);
    };
    QTRY_VERIFY2_WITH_TIMEOUT(settled(), qPrintable(name + " never stopped moving"), 5000);
    for (QQuickItem *p = out->parentItem(); p; p = p->parentItem()) {
        if (!p->inherits("QQuickFlickable"))
            continue;
        const QRectF r = out->mapRectToItem(p->property("contentItem").value<QQuickItem *>(), QRectF(0, 0, out->width(), out->height()));
        const qreal top = p->property("contentY").toReal();
        if (r.top() < top)
            p->setProperty("contentY", r.top() - 8);
        else if (r.bottom() > top + p->height())
            p->setProperty("contentY", r.bottom() - p->height() + 8);
        break;
    }
    needInside = true;
    QTRY_VERIFY2_WITH_TIMEOUT(settled(), qPrintable(name + " never settled inside its window"), 5000);
}

// ---------------------------------------------------------------- driving

inline bool isChecked(QQuickItem *item) { return item->property("checked").toBool(); }

inline void setCheck(const QString &name, bool on) {
    QQuickItem *box = nullptr;
    control(name, box);
    if (!box)
        return;
    // A click that lands while the pane is still settling can be lost; it is
    // repeated if the box has not changed, after a wait long enough that a slow
    // answer is not mistaken for a lost click.
    for (int attempt = 0; attempt < 3; ++attempt) {
        QQuickItem *now = findItem(name);
        if (!now)
            break;
        if (isChecked(now) == on)
            break;
        click(now);
        const QString n = name;
        QTest::qWaitFor([&] { QQuickItem *i = findItem(n); return i && isChecked(i) == on; }, 2000);
    }
    // Panes rebuild their controls on a change, so look the box up afresh
    // rather than trust a pointer that may now be dangling.
    QTRY_VERIFY_WITH_TIMEOUT(findItem(name) && isChecked(findItem(name)) == on, 5000);
}

// Picks a combo entry the way a user does: open the drop-down, scroll to the
// entry if the list is long, click it.
inline void chooseCombo(const QString &name, int index) {
    QQuickItem *combo = nullptr;
    control(name, combo);
    if (!combo)
        return;
    QVERIFY2(index >= 0 && index < combo->property("count").toInt(), qPrintable(name + ": no such entry"));
    QString label;
    QMetaObject::invokeMethod(combo, "textAt", Q_RETURN_ARG(QString, label), Q_ARG(int, index));
    // A click on an entry of a drop-down that is still sliding open or scrolling
    // can land on its neighbour or be lost; the whole pick is made again then.
    for (int attempt = 0; attempt < 3; ++attempt) {
        QQuickItem *c = findItem(name);
        if (!c)
            break;
        click(c);
        QObject *popup = c->property("popup").value<QObject *>();
        QVERIFY(popup);
        // A click that closed a drop-down which was still shutting does not open this one.
        if (!QTest::qWaitFor([&] { return popup->property("visible").toBool(); }, 2500))
            continue;
        auto *view = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(view);
        settleLayout(view);
        QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, 2));
        auto entryOf = [&]() -> QQuickItem * {
            return findIn(view, [&](QQuickItem *i) {
                return i->width() > 0 && i->inherits("QQuickAbstractButton") && i->property("text").toString() == label;
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(entryOf() != nullptr, 5000);
        // The drop-down slides open and the list scrolls; click once it is still.
        QPoint last(-1, -1);
        int still = 0;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QQuickItem *e = entryOf();
            if (!e)
                return false;
            const QPoint now = centreOf(e);
            still = now == last ? still + 1 : 0;
            last = now;
            return still >= 3;
        }(), 5000);
        click(entryOf());
        QTRY_VERIFY_WITH_TIMEOUT(!popup->property("visible").toBool(), 5000);
        if (QTest::qWaitFor([&] { QQuickItem *x = findItem(name); return x && x->property("currentIndex").toInt() == index; }, 2000))
            break;
    }
    QQuickItem *done = findItem(name);
    QVERIFY(done);
    QTRY_COMPARE_WITH_TIMEOUT(done->property("currentIndex").toInt(), index, 5000);
}

// Moves a slider to `target` with the arrow keys after clicking it for focus.
inline void setSlider(QQuickWindow *sw, const QString &name, double target) {
    QQuickItem *slider = nullptr;
    control(name, slider);
    if (!slider)
        return;
    click(slider);
    Qt::Key dir = Qt::Key_Right;
    for (int i = 0; i < 80; ++i) {
        const double v = slider->property("value").toDouble();
        if (qAbs(v - target) < 1e-6)
            return;
        QTest::keyClick(sw, dir);
        if (qAbs(slider->property("value").toDouble() - target) > qAbs(v - target))
            dir = dir == Qt::Key_Right ? Qt::Key_Left : Qt::Key_Right;
    }
    QCOMPARE(slider->property("value").toDouble(), target);
}

inline void setSpin(QQuickWindow *sw, const QString &name, int target) {
    QQuickItem *spin = nullptr;
    control(name, spin);
    if (!spin)
        return;
    click(spin);
    for (int i = 0; i < 120 && spin->property("value").toInt() != target; ++i)
        QTest::keyClick(sw, spin->property("value").toInt() < target ? Qt::Key_Up : Qt::Key_Down);
    QCOMPARE(spin->property("value").toInt(), target);
}

// Types into a text field and leaves it with Return, which commits it.
inline void setText(QQuickWindow *sw, const QString &name, const QString &text) {
    QQuickItem *field = nullptr;
    control(name, field);
    if (!field)
        return;
    click(field);
    QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
    if (text.isEmpty())
        QTest::keyClick(sw, Qt::Key_Delete);
    else
        typeInto(sw, text);
    QTest::keyClick(sw, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(field->property("text").toString(), text, 5000);
}

// ------------------------------------------------------------ main window

inline void focusMain(App &app) {
    closeSettings(app);
    activate(app.window);
    QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
}

// A Text anywhere under `root` with exactly this text.
inline QQuickItem *textUnder(QQuickItem *root, const QString &text) {
    if (!root)
        return nullptr;
    return findIn(root, [&](QQuickItem *i) {
        return i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0 && i->property("text").toString() == text;
    });
}

// A Text under `root` whose text matches the expression.
inline QQuickItem *textMatching(QQuickItem *root, const QRegularExpression &re) {
    if (!root)
        return nullptr;
    return findIn(root, [&](QQuickItem *i) {
        return i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0 && re.match(i->property("text").toString()).hasMatch();
    });
}

inline int countNamed(const QString &name) {
    int n = 0;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) { n += i->objectName() == name; return false; });
    return n;
}

// A task's title as drawn in its row, or null when the row is not on screen.
inline QQuickItem *rowTitle(App &app, qint64 id) {
    return textUnder(app.row(id), app.task(id).title);
}

inline QColor colourOf(QQuickItem *text) { return text->property("color").value<QColor>(); }

inline int sidebarNumber(const QString &kind, const QString &source) {
    QQuickItem *row = findItem("sidebarRow:" + source);
    QQuickItem *n = row ? findIn(row, [&](QQuickItem *i) { return i->objectName() == kind; }) : nullptr;
    return n ? n->property("value").toInt() : -1;
}

inline QString isoOf(const QDate &d) { return d.toString(Qt::ISODate); }

// Opens a source and waits for it to be the one shown.
inline void showSource(App &app, const QString &key) {
    focusMain(app);
    QTRY_VERIFY_WITH_TIMEOUT(app.openSource(key), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), key, 5000);
}

inline QString listKey(qint64 id) { return "list:" + QString::number(id); }

// The newest live task whose title starts with `prefix`.
inline Task newestTitled(App &app, const QString &prefix) {
    Task found;
    for (const Task &t : app.store().tasks())
        if (!t.deleted && t.title.startsWith(prefix) && t.id > found.id)
            found = t;
    return found;
}

// New Task with Ctrl+N and a title typed into the editor, as a user does;
// leaves with Return. The task comes back from the store.
inline void newTaskByKeyboard(App &app, const QString &title, Task &out, QString prefix = QString()) {
    if (prefix.isEmpty())
        prefix = title;
    focusMain(app);
    app.press(Qt::Key_N, Qt::ControlModifier);
    QQuickItem *field = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((field = findItem("editorTitle")) != nullptr && field->property("activeFocus").toBool(), 5000);
    app.type(title);
    app.press(Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(newestTitled(app, prefix).id != 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
    out = newestTitled(app, prefix);
}

// The list a New Task (Ctrl+N) lands in, which may not be the list on screen
// (so there may be no editor to type into); the empty task is dropped again.
inline void newTaskListId(App &app, qint64 &listId) {
    focusMain(app);
    qint64 before = 0;
    for (const Task &t : app.store().tasks())
        before = qMax(before, t.id);
    app.press(Qt::Key_N, Qt::ControlModifier);
    Task made;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const Task &t : app.store().tasks())
            if (!t.deleted && t.id > before)
                made = t;
        return made.id != 0;
    }(), 5000);
    listId = made.listId;
    app.backend().endEdit();
    QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
}

// Closes Quick Entry with Ctrl+W (main() adds an Escape filter that the
// harness does not run).
inline void closeQuickEntry(QQuickWindow *quick) {
    QQuickItem *field = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((field = findItem("quickTitle")) != nullptr, 5000);
    click(field);
    QTest::keyClick(quick, Qt::Key_W, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!quick->isVisible(), 5000);
}

// Leaves the open editor with Escape, once its title field has the keys.
inline void closeEditorWithEscape(App &app) {
    QQuickItem *title = nullptr;
    QTRY_VERIFY2_WITH_TIMEOUT((title = findItem("editorTitle")) != nullptr && title->property("activeFocus").toBool(),
                              qPrintable("focus is on " + app.focusDescription()), 5000);
    app.press(Qt::Key_Escape);
    QTRY_VERIFY2_WITH_TIMEOUT(!findItem("editorCard"), qPrintable("focus is on " + app.focusDescription()), 5000);
}

// Space or Escape closes Quick Look once its content has the keys.
inline void closeQuickLook(App &app, QObject *look) {
    auto *content = look->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QTRY_VERIFY2_WITH_TIMEOUT(content->property("activeFocus").toBool(), qPrintable("focus is on " + app.focusDescription()), 5000);
    app.press(Qt::Key_Escape);
    QTRY_VERIFY2_WITH_TIMEOUT(!look->property("visible").toBool(), qPrintable("focus is on " + app.focusDescription()), 5000);
}

inline void openQuickEntry(App &app, QQuickWindow *&quick) {
    quick = nullptr;
    focusMain(app);
    app.press(Qt::Key_2, Qt::ShiftModifier | Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT((quick = windowTitled("Quick Entry")) != nullptr && quick->isExposed(), 4000);
    activate(quick);
}

// Quick Entry with a line typed into its title field and saved with Return.
// `prefix` is how the saved title starts, to find it again.
inline void quickAdd(App &app, const QString &line, const QString &prefix, Task &out) {
    QQuickWindow *quick = nullptr;
    openQuickEntry(app, quick);
    QQuickItem *field = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((field = findItem("quickTitle")) != nullptr, 5000);
    click(field);
    typeInto(quick, line);
    QTest::keyClick(quick, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(newestTitled(app, prefix).id != 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!windowTitled("Quick Entry") || !windowTitled("Quick Entry")->isVisible(), 5000);
    out = newestTitled(app, prefix);
}

// A fresh title each call, so probes can run twice on one database.
inline QString unique(const QString &stem) {
    static int n = 0;
    return QString("%1%2").arg(stem).arg(++n);
}

// How many columns from the left edge of the mini calendar a date's cell sits.
inline int calendarColumn(const QDate &d) {
    QQuickItem *cal = findItem("miniCalendar");
    QQuickItem *cell = findItem("calDay:" + isoOf(d));
    if (!cal || !cell)
        return -1;
    qreal minX = 1e9;
    // The first row always holds seven consecutive days, so the leftmost of
    // all 42 cells is column 0.
    for (int i = -40; i <= 70; ++i)
        if (QQuickItem *other = findItem("calDay:" + isoOf(QDate::currentDate().addDays(i))))
            minX = qMin(minX, other->mapToItem(cal, QPointF(0, 0)).x());
    return qRound((cell->mapToItem(cal, QPointF(0, 0)).x() - minX) / cell->width());
}

// The next date this month (or later) that falls on this weekday, 1 = Monday.
inline QDate dateOnWeekday(int dayOfWeek) {
    QDate d = QDate::currentDate().addDays(-QDate::currentDate().day() + 1);
    while (d.dayOfWeek() != dayOfWeek)
        d = d.addDays(1);
    return d;
}


// Tag names and the content width only show on a wide task pane; at the test
// window's size the tag panel takes the room, so hide it (Ctrl+Alt+Right) as
// a user would.
inline void widenPane(App &app) {
    focusMain(app);
    QQuickItem *list = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((list = findItem("taskList")) != nullptr, 5000);
    if (list->width() < 900)
        app.press(Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
    QTRY_VERIFY2_WITH_TIMEOUT(list->width() >= 900, qPrintable(QString("task list is %1 wide").arg(list->width())), 5000);
}

// The titles of the section headings on screen, top to bottom.
inline QStringList sectionTitles() {
    QList<QPair<qreal, QString>> found;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if (i->objectName() == "sectionTitle")
                found.append({i->mapToScene(QPointF(0, 0)).y(), i->property("text").toString()});
            return false;
        });
    std::sort(found.begin(), found.end());
    QStringList out;
    for (const auto &f : found)
        out << f.second;
    return out;
}

// Gives a new task a due date the way Alt-click on a mini-calendar day does
// (which goes through the same path as the date picker) and reports the due
// time it ended up with: -1 for none.
inline void dueTimeFromCalendar(App &app, int &time) {
    focusMain(app);
    QQuickItem *cell = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((cell = findItem("calDay:" + isoOf(QDate::currentDate().addDays(1)))) != nullptr, 5000);
    qint64 before = 0;
    for (const Task &t : app.store().tasks())
        before = qMax(before, t.id);
    const QPoint at = centreOf(cell);
    QTest::mouseMove(app.window, at);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::AltModifier, at);
    QTRY_VERIFY_WITH_TIMEOUT(findItem("editorCard") != nullptr, 5000);
    Task made;
    for (const Task &t : app.store().tasks())
        if (!t.deleted && t.id > before)
            made = t;
    QVERIFY(made.id);
    QCOMPARE(made.dueDate, QDate::currentDate().addDays(1));
    time = made.dueTime;
    closeEditorWithEscape(app);
}

// ------------------------------------------------------------------ table

// One user-visible setting: where it lives, how a user changes it, and what
// that visibly does. Run by the table-driven test in test_settings.cpp, which
// changes it through the Settings window, checks the effect, restarts the app
// on the same database, checks the control and the effect again, and changes
// it back.
struct SettingCase {
    enum Kind { Check, Combo, SliderK, Spin, Text, Choice };

    QString key;
    QString pane;
    QString control;  // objectName of the control (Choice: the button that changes it).
    Kind kind = Check;
    bool defaultOn = false;     // Check: the state a fresh database shows.
    int toIndex = -1, fromIndex = -1;           // Combo: the entry changed to, and the default one.
    double toValue = 0, fromValue = 0;          // Slider and Spin.
    QString toText;                             // Text.
    QString revert;                             // Choice: the button that puts it back.
    QString stored;                             // What the store holds after the change, as text.
    std::function<void(Store &, Seed &)> prepare;   // Data the effect needs, before the app starts.
    std::function<void(Session &, bool changed)> effect;   // Asserts the effect is (not) there.
    QString outside;   // Why the effect cannot be seen in the window; the stored value is checked instead.
    QString slot;      // A dedicated test proves this one instead of the table runner.
    // The same key can have several rows (a combo with several entries).
    QString name() const {
        return key + ":" + pane + (kind == Combo ? QString(":%1").arg(toIndex) : kind == Choice ? ":" + control.section(':', -1) : QString());
    }
};

inline void editTask(Store &st, qint64 id, const std::function<void(Task &)> &fn) {
    Task t = st.task(id);
    fn(t);
    st.begin("Prepare", false);
    st.putTask(t);
    st.commit();
}

inline qint64 addTask(Store &st, qint64 listId, const QString &title, const std::function<void(Task &)> &fn = {}) {
    Task t;
    t.id = st.nextId("tasks");
    t.listId = listId;
    t.title = title;
    t.order = t.id;
    if (fn)
        fn(t);
    st.begin("Prepare", false);
    st.putTask(t);
    st.commit();
    return t.id;
}

// Rows are read inside QTRY, so a probe states what must hold and the macro
// retries until it does.
#define SK_EFFECT [=](Session &s, bool changed)
#define SK_APP App &app = *s.app; Q_UNUSED(app)

}  // namespace e2e

#include "settingscases_general.h"
#include "settingscases_lists.h"
#include "settingscases_tasks.h"

namespace e2e {

inline const QVector<SettingCase> &settingCases() {
    static const QVector<SettingCase> cases = [] {
        QVector<SettingCase> v;
        addGeneralAndAppearanceCases(v);
        addTaskCases(v);
        addListAndAlertCases(v);
        return v;
    }();
    return cases;
}


// Settings proven by a dedicated test rather than the table runner: they have
// no single control, or their effect needs a longer story. Key (or prefix
// ending in '.') -> "test class::slot".
inline const QMap<QString, QString> &dedicatedKeys() {
    static const QMap<QString, QString> m{
        {"inbox", "E2ESettingsLists::inboxSwitchAddsAndRemovesTheInbox"},
        {"lists.hidden", "E2ESettingsLists::hiddenListsLeaveTheSidebarAndAll"},
        {"appearance.showCalendar", "E2ESettingsLists::sidebarCalendarMenu"},
        {"appearance.calendarWeekends", "E2ESettingsLists::sidebarCalendarMenu"},
        {"sort.", "E2ESettingsLists::sortMenuPersists"},
        {"bar.exact.", "E2ESettingsLists::quickOpenExactWordsIsRemembered"},
        {"search.soundex", "E2ESettingsLists::soundsLikeSearchIsRemembered"},
        {"tags.manual", "E2ESettingsLists::tagSortingSwitchIsRemembered"},
        {"tasks.autoDeleteDays", "E2ESettingsSystem::backupFrequencyAndAutoDelete"},
        {"tasks.autoDeleteDelaySubtasks", "E2ESettingsSystem::backupFrequencyAndAutoDelete"},
        {"backup.intervalHours", "E2ESettingsSystem::backupFrequencyAndAutoDelete"},
        {"backup.keep", "E2ESettingsSystem::keepAtMostPrunesOldBackups"},
        {"sync.auto", "E2ESettingsSystem::folderSyncWritesFilesAndDisconnects"},
        {"folder.path", "E2ESettingsSystem::folderSyncWritesFilesAndDisconnects"},
        {"git.remote", "E2ESettingsSystem::gitSyncPushesToTheRemote"},
        {"webdav.url", "E2ESettingsSystem::webDavAndTodoistConfiguration"},
        {"webdav.user", "E2ESettingsSystem::webDavAndTodoistConfiguration"},
        {"calendar.sources", "E2ESettingsSystem::calendarSourcesFeedTheDayBar"},
        {"calendar.inLists", "E2ESettingsSystem::calendarSourcesFeedTheDayBar"},
        {"calendar.disabled", "E2ESettingsSystem::calendarSourcesFeedTheDayBar"},
        {"calendar.colors", "E2ESettingsSystem::calendarSourcesFeedTheDayBar"},
        {"calendar.emails", "E2ESettingsSystem::calendarSourcesFeedTheDayBar"},
        {"calendar.expanded", "E2ESettingsSystem::calendarSourcesFeedTheDayBar"},
    };
    return m;
}

// Keys the code reads or writes that are not settings a user chooses: window
// state, bookkeeping and the like. Key (or prefix ending in '.') -> why.
inline const QMap<QString, QString> &internalKeys() {
    static const QMap<QString, QString> m{
        {"backup.last", "bookkeeping: when the last automatic backup ran"},
        {"git.lastCommit", "bookkeeping of the sync"},
        {"git.lastSync", "bookkeeping of the sync"},
        {"todoist.lastSync", "bookkeeping of the Todoist sync"},
        {"ui.collapsedSections", "remembered view state, not a choice"},
        {"ui.expandedLists", "remembered view state, not a choice"},
        {"ui.source", "remembered view state, not a choice"},
        {"window.", "remembered view state: whether the Lists and Tags panels are shown (E2EMiscWindow::panelTogglesHideShowAndPersist)"},
        {"tasks.lastList", "bookkeeping behind the \"Last used\" default list"},
        {"tasks.quickEntryList", "retired setting, migrated into tasks.defaultList on launch"},
        {"lists.doneWarned", "one-off warning flag"},
        {"lists.hiddenRevealed", "per-session reveal of hidden lists"},
        {"windows.extra", "which extra windows to reopen"},
        {"alerts.nag", "retired on/off switch, migrated into alerts.nagMinutes on launch"},
        {"print.notes", "read when saving a PDF but nothing in the UI sets it (a dead setting; see the report)"},
    };
    return m;
}

// Keys with a user control elsewhere that no test reaches yet, and why.
inline const QMap<QString, QString> &untestedKeys() {
    static const QMap<QString, QString> m;
    return m;
}

}  // namespace e2e
