#pragma once

// Helpers for the lists, list groups and navigation scenarios: a seed with
// groups, an inbox, a project and a Smart List; readers for what the sidebar
// shows; and the clicks a user makes on list rows, menus and dialogs.

#include "harness.h"

namespace e2e {

template <class F>
bool waitUntil(F ready, int ms = 5000, const char *file = __builtin_FILE(), int line = __builtin_LINE()) {
    QElapsedTimer timer;
    timer.start();
    while (!ready()) {
        if (timer.elapsed() >= ms) {
            reportSlowWait(timer.elapsed(), false, file, line);
            return false;
        }
        QTest::qWait(10);
    }
    reportSlowWait(timer.elapsed(), true, file, line);
    return true;
}

// Waits for a visible item to exist (rows, popups and menu entries appear
// after an event-loop turn or two).
#define LIST_ITEM(var, expr) \
    QQuickItem *var = nullptr; \
    QTRY_VERIFY2_WITH_TIMEOUT((var = (expr)) != nullptr, "item never appeared: " #expr, 5000)

// ------------------------------------------------------------------- seed

// Ids of what seedLists() adds to the default world. The default world has a
// "Personal" group holding Home and Work.
struct ListWorld {
    qint64 personal = 0, projects = 0;      // List groups.
    qint64 errands = 0, misc = 0, inbox = 0;  // Lists: Errands in Projects, Misc ungrouped.
    qint64 urgent = 0;                      // A Smart List.
    qint64 stamps = 0, parcel = 0;          // Errands: one overdue, one undated.
    qint64 cables = 0, trip = 0, triage = 0;  // Misc: due today with an alert; scheduled; Inbox task.
    qint64 launch = 0, announce = 0;        // A project in Work and its sub-task.
    qint64 alarm = 0;                       // The alert on "Sort cables".
};

inline ListWorld &world() {
    static ListWorld w;
    return w;
}

// The default seed plus: group Projects (order 2) with list Errands, an
// ungrouped list Misc, an Inbox, a Smart List, a project in Work, and an alert.
// The calendar is hidden so the whole sidebar is on screen.
inline Seed seedLists(Store &store) {
    const Seed s = seedWorld(store);
    ListWorld w;
    const QDate today = QDate::currentDate();
    w.personal = store.list(s.home).groupId;
    store.setSetting("appearance.showCalendar", false);
    store.begin("Seed lists", false);

    ListGroup group;
    group.id = store.nextId("list_groups");
    group.name = "Projects";
    group.order = 2;
    store.putGroup(group);
    w.projects = group.id;

    auto list = [&](const QString &name, int color, double order, qint64 groupId, int kind = ListKind::Normal) {
        TaskList l;
        l.id = store.nextId("lists");
        l.name = name;
        l.groupId = groupId;
        l.color = color;
        l.order = order;
        l.kind = kind;
        store.putList(l);
        return l.id;
    };
    w.errands = list("Errands", 4, 1, w.projects);
    w.misc = list("Misc", 5, 3, 0);
    w.inbox = list("Inbox", 6, 0, 0, ListKind::Inbox);
    w.urgent = list("Urgent", 1, 9, 0, ListKind::Smart);
    TaskList urgent = store.list(w.urgent);
    urgent.smartQuery = "#garden";
    store.putList(urgent);

    auto task = [&](qint64 listId, const QString &title, int dueDays, int type = TaskType::Task, qint64 parent = 0) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = listId;
        t.title = title;
        if (dueDays != INT_MIN)
            t.dueDate = today.addDays(dueDays);
        t.type = type;
        t.parentId = parent;
        t.order = t.id;
        store.putTask(t);
        return t.id;
    };
    w.stamps = task(w.errands, "Buy stamps", -1);
    w.parcel = task(w.errands, "Post parcel", INT_MIN);
    w.cables = task(w.misc, "Sort cables", 0);
    w.trip = task(w.misc, "Plan trip", INT_MIN);
    w.triage = task(w.inbox, "Triage me", INT_MIN);
    w.launch = task(s.work, "Launch", INT_MIN, TaskType::Project);
    w.announce = task(s.work, "Draft announcement", INT_MIN, TaskType::Task, w.launch);

    Task cables = store.task(w.cables);
    cables.dueTime = 17 * 60;
    store.putTask(cables);
    Task trip = store.task(w.trip);
    trip.startDate = today.addDays(10);
    store.putTask(trip);

    Alarm alarm;
    alarm.id = store.nextId("alarms");
    alarm.taskId = w.cables;
    alarm.kind = Alarm::Relative;
    alarm.offset = 15;
    store.putAlarm(alarm);
    w.alarm = alarm.id;
    store.commit();
    world() = w;
    return s;
}

// -------------------------------------------------------------- store reads

inline QString lk(qint64 id) { return "list:" + QString::number(id); }

inline TaskList liveList(App &app, qint64 id) {
    const TaskList l = app.store().list(id);
    return l.deleted ? TaskList() : l;
}

inline bool listExists(App &app, qint64 id) { return liveList(app, id).id != 0; }

inline int liveListsNamed(App &app, const QString &name) {
    int n = 0;
    for (const TaskList &l : app.store().lists())
        n += !l.deleted && l.name == name;
    return n;
}

inline qint64 listNamed(App &app, const QString &name) {
    for (const TaskList &l : app.store().lists())
        if (!l.deleted && l.name == name)
            return l.id;
    return 0;
}

inline int liveGroupsNamed(App &app, const QString &name) {
    int n = 0;
    for (const ListGroup &g : app.store().groups())
        n += !g.deleted && g.name == name;
    return n;
}

inline qint64 groupNamed(App &app, const QString &name) {
    for (const ListGroup &g : app.store().groups())
        if (!g.deleted && g.name == name)
            return g.id;
    return 0;
}

inline ListGroup liveGroup(App &app, qint64 id) {
    const ListGroup g = app.store().groups().value(id);
    return g.deleted ? ListGroup() : g;
}

// Live lists of a group (0 for the top level), in sidebar order.
inline QList<qint64> listsIn(App &app, qint64 groupId) {
    QList<TaskList> lists;
    for (const TaskList &l : app.store().lists())
        if (!l.deleted && !l.archived && l.kind == ListKind::Normal && l.groupId == groupId)
            lists << l;
    std::sort(lists.begin(), lists.end(), [](const TaskList &a, const TaskList &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    QList<qint64> ids;
    for (const TaskList &l : lists)
        ids << l.id;
    return ids;
}

inline QList<qint64> groupOrder(App &app) {
    QList<ListGroup> groups;
    for (const ListGroup &g : app.store().groups())
        if (!g.deleted)
            groups << g;
    std::sort(groups.begin(), groups.end(), [](const ListGroup &a, const ListGroup &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    QList<qint64> ids;
    for (const ListGroup &g : groups)
        ids << g.id;
    return ids;
}

// ------------------------------------------------------- what the sidebar shows

// Every sidebar row on screen, top to bottom: a source key for lists, focus
// lists and projects, "g:<id>" for a group header ("g:-1" is Smart Lists).
inline QStringList sidebarOrder() {
    QList<QPair<qreal, QString>> found;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            const QString name = i->objectName();
            const qreal y = i->mapToScene(QPointF(0, 0)).y();
            if (name.startsWith("sidebarRow:"))
                found.append({y, name.mid(11)});
            else if (name.startsWith("sidebarGroup:") && name.mid(13) != "0" && !name.endsWith("Rename") && !name.endsWith("Title") && !name.endsWith("Dot"))
                found.append({y, "g:" + name.mid(13)});
            return false;
        });
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList out;
    for (const auto &f : found)
        out << f.second;
    return out;
}

inline QQuickItem *childNamed(QQuickItem *root, const QString &name) {
    return root ? findIn(root, [&](QQuickItem *i) { return i->objectName() == name; }) : nullptr;
}

// The name a sidebar row or group header displays.
inline QString shownName(QQuickItem *item) {
    if (!item)
        return {};
    QQuickItem *text = childNamed(item, item->objectName().startsWith("sidebarGroup:") ? "sidebarGroupTitle" : "sidebarLabel");
    return text ? text->property("text").toString() : QString();
}

inline QQuickItem *groupHeader(qint64 id) { return findItem("sidebarGroup:" + QString::number(id)); }

inline QStringList shownNames() {
    QStringList out;
    for (const QString &key : sidebarOrder()) {
        QQuickItem *item = key.startsWith("g:") ? findItem("sidebarGroup:" + key.mid(2)) : findItem("sidebarRow:" + key);
        out << shownName(item);
    }
    return out;
}

// The list rows of a sidebar group in the order shown (stops at the next
// header), as source keys.
inline QStringList sidebarListKeys() {
    QStringList out;
    for (const QString &key : sidebarOrder())
        if (key.startsWith("list:"))
            out << key;
    return out;
}

inline void parkMouse(App &app) { QTest::mouseMove(app.window, QPoint(app.window->width() - 4, 4)); }

struct Badge {
    int overdue = 0, undone = 0;
    bool overdueShown = false, undoneShown = false;
};

// The overdue|total numbers on a sidebar row.
inline Badge badgeOf(App &app, const QString &key) {
    parkMouse(app);
    Badge b;
    QQuickItem *row = app.sidebarRow(key);
    if (!row)
        return b;
    if (QQuickItem *o = findIn(row, [](QQuickItem *i) { return i->objectName() == "sidebarOverdue"; }, false)) {
        b.overdue = o->property("value").toInt();
        b.overdueShown = o->isVisible();
    }
    if (QQuickItem *u = findIn(row, [](QQuickItem *i) { return i->objectName() == "sidebarUndone"; }, false)) {
        b.undone = u->property("value").toInt();
        b.undoneShown = u->isVisible();
    }
    return b;
}

inline QString paneTitle() {
    QQuickItem *t = findItem("paneTitle");
    return t ? t->property("text").toString() : QString();
}

inline QColor bandColour(App &app, qint64 listId) {
    QQuickItem *band = childNamed(app.sidebarRow(lk(listId)), "sidebarBand");
    return band ? band->property("color").value<QColor>() : QColor();
}

// ------------------------------------------------------------ menus, dialogs

inline QQuickItem *menuEntry(const QString &text) { return findText(text, "QQuickMenuItem"); }

// True once no menu, dialog or command bar is on screen (they fade out, and
// a click while one still is would land on its overlay).
inline bool popupsClosed() {
    for (QQuickWindow *w : quickWindows()) {
        bool open = false;
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if (i->width() > 0 && (i->inherits("QQuickMenuItem")
                                   || (i->inherits("QQuickAbstractButton")
                                       && QStringList{"OK", "Cancel", "Close"}.contains(i->property("text").toString()))))
                open = true;
            return open;
        });
        if (open)
            return false;
    }
    QObject *bar = findObject("commandPopup");
    return !bar || !bar->property("visible").toBool();
}

// Escapes out of whatever menus are open.
inline void closeMenus(App &app) {
    for (int i = 0; i < 4 && !popupsClosed(); ++i) {
        app.press(Qt::Key_Escape);
        waitUntil([] { return popupsClosed(); }, 1000);
    }
}

inline bool settle() { return waitUntil([] { return popupsClosed(); }); }

// Scrolls the nearest scrolling view above `item` until the item is on screen.
inline void scrollIntoView(QQuickItem *item) {
    for (QQuickItem *up = item->parentItem(); up; up = up->parentItem()) {
        if (!up->inherits("QQuickFlickable"))
            continue;
        const QRectF r = item->mapRectToItem(up->property("contentItem").value<QQuickItem *>(), QRectF(0, 0, item->width(), item->height()));
        const qreal view = up->height();
        qreal y = up->property("contentY").toReal();
        if (r.bottom() > y + view)
            y = r.bottom() - view + 8;
        if (r.top() < y)
            y = r.top() - 8;
        up->setProperty("contentY", y);
        QTest::qWait(30);
        return;
    }
}

// Clicks an item once it has stopped moving (menus and sheets animate in, and
// lists of checkboxes are rebuilt after each change).
inline void clickSteady(QQuickItem *item, Qt::MouseButton button = Qt::LeftButton) {
    QVERIFY2(item, "clickSteady: no such item");
    // The item can be recreated while this waits (list delegates, rebuilt menus).
    QPointer<QQuickItem> guard(item);
    settleLayout(item);
    if (!guard)
        return;
    QPoint last(-1, -1);
    int stable = 0;
    waitUntil([&] {
        if (!guard)
            return true;
        const QPoint now = centreOf(guard);
        stable = now == last ? stable + 1 : 0;
        last = now;
        return stable >= 5;
    }, 2000);
    if (!guard)
        return;
    click(guard, button);
}

// Clicks a menu entry once it has stopped moving: menus scale and fade in,
// and a click aimed at where an entry was a frame ago can miss it.
inline bool clickMenuEntry(const QString &text, int ms = 5000) {
    QQuickItem *entry = nullptr;
    if (!waitUntil([&] { return (entry = menuEntry(text)) != nullptr; }, ms))
        return false;
    settleLayout(entry);
    QPoint last(-1, -1);
    int stable = 0;
    waitUntil([&] {
        entry = menuEntry(text);
        if (!entry)
            return true;
        const QPoint now = centreOf(entry);
        stable = now == last ? stable + 1 : 0;
        last = now;
        return stable >= 5;
    }, 1000);
    if (!entry)
        return false;
    click(entry);
    return true;
}

inline QStringList menuEntries() {
    QStringList seen;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if (i->inherits("QQuickMenuItem") && i->width() > 0)
                seen << i->property("text").toString();
            return false;
        });
    return seen;
}

// Opens a submenu (hovering, then clicking, as it sometimes needs) and
// clicks one of its entries.
inline bool clickSubmenuEntry(const QString &submenu, const QString &entry) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (QQuickItem *e = menuEntry(entry); e && e != menuEntry(submenu)) {
            click(e);
            return true;
        }
        if (QQuickItem *parent = menuEntry(submenu); parent && parent->window()) {
            if (attempt == 0)
                QTest::mouseMove(parent->window(), centreOf(parent));
            else
                click(parent);
        }
        // Hovering opens nothing offscreen; give it a moment, and the click a proper wait.
        waitUntil([&] { return menuEntry(entry) != nullptr; }, attempt == 0 ? 250 : 2000);
    }
    return false;
}

inline bool clickDialogButton(const QString &text, int ms = 5000) {
    QQuickItem *button = nullptr;
    if (!waitUntil([&] { return (button = findText(text, "QQuickAbstractButton")) != nullptr; }, ms))
        return false;
    click(button);
    return true;
}

// Clicks a button of the dialog with this title, which matters when two
// dialogs are open at once (the colour picker over the Edit List sheet).
inline bool clickDialogButtonIn(const QString &title, const QString &text, int ms = 5000) {
    QQuickItem *button = nullptr;
    waitUntil([&] {
        QQuickItem *label = findText(title);
        for (QQuickItem *up = label; up && !button; up = up->parentItem())
            button = findIn(up, [&](QQuickItem *i) {
                return i->inherits("QQuickAbstractButton") && i->width() > 0 && i->property("text").toString() == text;
            });
        return button != nullptr;
    }, ms);
    if (!button)
        return false;
    click(button);
    return true;
}

// Right-clicks a list row and picks a menu entry.
inline bool listMenu(App &app, qint64 listId, const QString &entry) {
    QQuickItem *row = nullptr;
    if (!settle() || !waitUntil([&] { return (row = app.sidebarRow(lk(listId))) != nullptr; }))
        return false;
    click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
    return clickMenuEntry(entry);
}

inline bool listSubmenu(App &app, qint64 listId, const QString &submenu, const QString &entry) {
    QQuickItem *row = nullptr;
    if (!settle() || !waitUntil([&] { return (row = app.sidebarRow(lk(listId))) != nullptr; }))
        return false;
    click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
    return clickSubmenuEntry(submenu, entry);
}

inline bool groupMenu(qint64 groupId, const QString &entry) {
    QQuickItem *header = nullptr;
    if (!settle() || !waitUntil([&] { return (header = groupHeader(groupId)) != nullptr; }))
        return false;
    click(header, Qt::RightButton, pointIn(header, 0.4, 0.5));
    return clickMenuEntry(entry);
}

inline bool groupSubmenu(qint64 groupId, const QString &submenu, const QString &entry) {
    QQuickItem *header = nullptr;
    if (!settle() || !waitUntil([&] { return (header = groupHeader(groupId)) != nullptr; }))
        return false;
    click(header, Qt::RightButton, pointIn(header, 0.4, 0.5));
    return clickSubmenuEntry(submenu, entry);
}

// The + button's menu.
inline bool plusMenu(const QString &entry) {
    QQuickItem *plus = findItem("sidebarAdd");
    if (!settle() || !plus)
        return false;
    click(plus);
    return clickMenuEntry(entry);
}

// Picks an entry of one of the Edit List sheet's drop-downs.
inline bool pickCombo(const QString &comboName, const QString &entry) {
    QQuickItem *combo = findItem(comboName);
    if (!combo)
        return false;
    click(combo);
    QQuickItem *delegate = nullptr;
    if (!waitUntil([&] { return (delegate = findText(entry, "QQuickAbstractButton")) != nullptr; }))
        return false;
    click(delegate);
    return waitUntil([&] { return findText(entry, "QQuickAbstractButton") == nullptr || !findText(entry, "QQuickAbstractButton")->isVisible(); }, 1500);
}

inline bool checked(const QString &name) {
    QQuickItem *box = findItem(name);
    return box && box->property("checked").toBool();
}

// Types into whatever has focus, replacing its contents.
inline void replaceText(App &app, const QString &text) {
    app.press(Qt::Key_A, Qt::ControlModifier);
    app.type(text);
}

// ------------------------------------------------------------ task pane reads

inline bool shows(App &app, qint64 taskId) { return app.row(taskId) != nullptr; }

// Selects a source by its shortcut or sidebar click, and waits for the pane.
inline bool pane(App &app, const QString &key) {
    return settle() && waitUntil([&] { return app.openSource(key); });
}

// Clicks the list's name above the tasks, which opens the list jump bar.
inline bool clickPaneTitle() {
    QQuickItem *title = findItem("paneTitle");
    if (!settle() || !title)
        return false;
    click(title);
    return true;
}

// Drags one sidebar item onto another with the pointer.
inline void dragOnto(QQuickItem *from, QQuickItem *to, qreal toFy = 0.5, int dy = 0) {
    QVERIFY2(from && to, "dragOnto: missing item");
    QQuickWindow *w = from->window();
    const QPoint a = pointIn(from, 0.4, 0.5), b = pointIn(to, 0.4, toFy) + QPoint(0, dy);
    QTest::mouseMove(w, a);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, a);
    const int steps = 12;
    for (int i = 1; i <= steps; ++i)
        QTest::mouseMove(w, a + (b - a) * i / steps);
    QTest::mouseMove(w, b);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, b);
}

}  // namespace e2e
