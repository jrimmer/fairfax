#pragma once

// Helpers for the keyboard, sort, tags, import/export and window-feature
// end-to-end tests (test_misc_*.cpp): a seed with a list built to tell every
// sort apart, readers for the order of the rows on screen, a database that
// outlives its App for restarts, and the clicks a user makes on the sort
// menu, the Tags panel and the Export dialog.

#include <QClipboard>
#include <QFile>
#include <QQmlEngine>
#include <QRegularExpression>

#include "listkit.h"
#include "settingskit.h"
#include "taskkit.h"

namespace e2e {
namespace mk {

using tk::clickMenu;
using tk::clickNamed;
using tk::clickSub;
using tk::contextMenuOn;
using tk::listLayout;
using tk::openEditor;
using tk::closeEditor;
using tk::selectRow;
using tk::settled;
using tk::typeIn;
using tk::waitFor;
using tk::waitItem;

inline QString readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// A database that outlives its App: start() boots on it, as a user relaunching does.
struct Keep {
    QTemporaryDir dir;
    std::unique_ptr<App> app;
    bool started = false;
    Seed ids;
    std::function<Seed(Store &)> seed;

    explicit Keep(std::function<Seed(Store &)> s = seedWorld) : seed(std::move(s)) {}
    bool start() {
        app.reset();
        app = std::make_unique<App>(started ? std::function<Seed(Store &)>() : seed, dir.filePath("e2e.db"));
        if (!app->ok)
            return false;
        if (!started)
            ids = app->ids;
        app->ids = ids;
        started = true;
        return true;
    }
    App &operator*() { return *app; }
    App *operator->() { return app.get(); }
};

// ------------------------------------------------------------------ the seed

// What seedMisc() adds to the default world: a list "Sorting" whose five tasks
// differ in every field a sort looks at, so each sort has exactly one order.
//   task     due  start prio dur created modified notes     tag    location order
//   Alpha    +4   -3    1    90  -5d     -1d      (none)    zebra  Zoo      3
//   Bravo    +1   -     3    30  -3d     -4d      apple     mango  -        5
//   Charlie  -    -1    0    0   -4d     -2d      mango     -      Attic    1
//   Delta    +2   0     2    60  -2d     -3d      banana    apple  Barn     4
//   Echo     -1   -5    1    15  -1d     -5d      cherry    mango  Cabin    2
struct MiscWorld {
    qint64 sorting = 0;
    qint64 alpha = 0, bravo = 0, charlie = 0, delta = 0, echo = 0;
    qint64 zebra = 0, mango = 0, apple = 0;       // Tags.
    qint64 zoo = 0, attic = 0, barn = 0, cabin = 0;  // Locations.
    qint64 gardenTag = 0;                          // The default world's tag.
    qint64 trip = 0, flights = 0, bags = 0, passport = 0;  // A project in Work and its children.
};

inline MiscWorld &mw() {
    static MiscWorld w;
    return w;
}

inline Seed seedMisc(Store &store) {
    Seed s = seedWorld(store);
    MiscWorld &w = mw();
    const QDate today = QDate::currentDate();
    const QDateTime now = QDateTime::currentDateTime();
    store.begin("Seed misc", false, true);
    qint64 groupId = 0;
    for (const ListGroup &g : store.groups())
        groupId = g.id;
    TaskList list;
    list.id = store.nextId("lists");
    list.name = "Sorting";
    list.groupId = groupId;
    list.color = 4;
    list.order = 3;
    store.putList(list);
    w.sorting = list.id;
    auto tag = [&](const QString &name, double order) {
        Tag t;
        t.id = store.nextId("tags");
        t.name = name;
        t.order = order;
        store.putTag(t);
        return t.id;
    };
    w.zebra = tag("zebra", 2);
    w.mango = tag("mango", 3);
    w.apple = tag("apple", 4);
    for (const Tag &t : store.tags())
        if (t.name == "garden")
            w.gardenTag = t.id;
    auto place = [&](const QString &name) {
        Location l;
        l.id = store.nextId("locations");
        l.name = name;
        store.putLocation(l);
        return l.id;
    };
    w.zoo = place("Zoo");
    w.attic = place("Attic");
    w.barn = place("Barn");
    w.cabin = place("Cabin");
    auto task = [&](const QString &title, int due, int start, int prio, int dur, int createdDays, int modifiedDays,
                    const QString &notes, qint64 tagId, qint64 loc, double order) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = list.id;
        t.title = title;
        if (due != INT_MIN) t.dueDate = today.addDays(due);
        if (start != INT_MIN) t.startDate = today.addDays(start);
        t.priority = prio;
        t.duration = dur;
        t.created = now.addDays(createdDays);
        t.modified = now.addDays(modifiedDays);
        t.notes = notes;
        if (tagId) t.tagIds << tagId;
        if (loc) t.locationIds << loc;
        t.order = order;
        store.putTask(t);
        return t.id;
    };
    w.alpha = task("Alpha", 4, -3, 1, 90, -5, -1, "", w.zebra, w.zoo, 3);
    w.bravo = task("Bravo", 1, INT_MIN, 3, 30, -3, -4, "apple", w.mango, 0, 5);
    w.charlie = task("Charlie", INT_MIN, -1, 0, 0, -4, -2, "mango", 0, w.attic, 1);
    w.delta = task("Delta", 2, 0, 2, 60, -2, -3, "banana", w.apple, w.barn, 4);
    w.echo = task("Echo", -1, -5, 1, 15, -1, -5, "cherry", w.mango, w.cabin, 2);
    // A project in Work, its children in manual order Flights, Bags, Passport.
    auto child = [&](qint64 parent, const QString &title, int prio, double order, int type = TaskType::Task) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = s.work;
        t.parentId = parent;
        t.type = type;
        t.title = title;
        t.priority = prio;
        t.order = order;
        store.putTask(t);
        return t.id;
    };
    w.trip = child(0, "Trip plan", 0, 20, TaskType::Project);
    w.flights = child(w.trip, "Book flights", 1, 1);
    w.bags = child(w.trip, "Pack bags", 3, 2);
    w.passport = child(w.trip, "Check passport", 2, 3);
    store.commit();
    return s;
}

// -------------------------------------------------------------- reading rows

// The ids of the task rows on screen, top to bottom.
inline QList<qint64> rowIds() {
    QList<qint64> out;
    for (const tk::Placed &p : listLayout())
        if (p.name.startsWith("taskRow:"))
            out << p.name.mid(8).toLongLong();
    return out;
}

// The rows as the letters A-E of the Sorting tasks, for compact expectations.
inline QString letterOf(qint64 id) {
    const MiscWorld &w = mw();
    if (id == w.alpha) return "A";
    if (id == w.bravo) return "B";
    if (id == w.charlie) return "C";
    if (id == w.delta) return "D";
    if (id == w.echo) return "E";
    return "?";
}

inline QString rowLetters() {
    QString out;
    for (qint64 id : rowIds())
        out += letterOf(id);
    return out;
}

inline QString orderNow() { return rowLetters(); }

// The titles of the section headings on screen, top to bottom.
inline QStringList headingTitles() {
    QList<QPair<qreal, QString>> found;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if (i->objectName() == "sectionTitle")
                found.append({i->mapToScene(QPointF(0, 0)).y(), i->property("text").toString()});
            return false;
        });
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList out;
    for (const auto &f : found)
        out << f.second;
    return out;
}

// The titles of the task rows on screen, top to bottom; `except` leaves out
// one task (a project's own row at the head of its view).
inline QStringList rowTitles(App &app, qint64 except = 0) {
    QStringList out;
    for (qint64 id : rowIds())
        if (id != except)
            out << app.task(id).title;
    return out;
}

// ----------------------------------------------------------------- the sort

// The texts of the sort menu's entries, top to bottom (other menus fading
// out at the same time are not counted).
inline QStringList sortMenuTexts() {
    QList<QPair<qreal, QString>> found;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if ((i->objectName().startsWith("sortChoice:") || i->objectName() == "sortSeparated") && i->width() > 0)
                found.append({i->mapToScene(QPointF(0, 0)).y(), i->property("text").toString()});
            return false;
        });
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList out;
    for (const auto &f : found)
        out << f.second;
    return out;
}

inline QQuickItem *sortMenuItem(int field) { return findItem(QString("sortChoice:%1").arg(field)); }

inline bool openSortMenu() {
    if (!tk::waitItem("sortButton") || !tk::settled("sortButton"))
        return false;
    click(findItem("sortButton"));
    return waitUntil([] { return findItem("sortSeparated") != nullptr; });
}

// Picks a sort from the menu next to the list name, as a user does.
inline bool chooseSort(App &app, int field) {
    if (!openSortMenu())
        return false;
    QQuickItem *item = nullptr;
    if (!waitUntil([&] { return (item = sortMenuItem(field)) != nullptr; }))
        return false;
    clickSteady(item);
    return waitUntil([&] { return app.backend().sortField() == field && !findItem("sortSeparated"); });
}

inline bool setSeparated(App &app, bool on) {
    if (app.backend().separated() == on)
        return true;
    if (!openSortMenu())
        return false;
    clickSteady(findItem("sortSeparated"));
    return waitUntil([&] { return app.backend().separated() == on && !findItem("sortSeparated"); });
}

inline bool setAscending(App &app, bool on) {
    if (app.backend().sortAscending() == on)
        return true;
    if (!tk::clickNamed("sortReverse"))
        return false;
    return waitUntil([&] { return app.backend().sortAscending() == on; });
}

// Whether the sort menu's entry is ticked (it is checkable).
inline bool sortTicked(int field) {
    if (!openSortMenu())
        return false;
    QQuickItem *item = sortMenuItem(field);
    const bool on = item && item->property("checked").toBool();
    QTest::keyClick(item ? item->window() : quickWindows().first(), Qt::Key_Escape);
    waitUntil([] { return !findItem("sortSeparated"); });
    return on;
}


// Drags slowly from one item to another and reports whether the pill that
// follows the pointer (the named drag proxy) was on screen on the way.
inline bool dragShowingPill(QQuickItem *from, QQuickItem *to, const QString &pill) {
    QQuickWindow *w = from->window();
    const QPoint a = centreOf(from), b = centreOf(to);
    bool seen = false;
    QTest::mouseMove(w, a);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, a);
    for (int i = 1; i <= 40; ++i) {
        QTest::mouseMove(w, a + (b - a) * i / 40);
        QTest::qWait(5);
        seen = seen || findItem(pill) != nullptr;
    }
    for (int i = 0; i < 3; ++i) {
        QTest::mouseMove(w, b);
        QTest::qWait(20);
    }
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, b);
    return seen;
}

// ----------------------------------------------------------------- shortcuts

// A date relative to today.
inline QDate day(int offset) { return QDate::currentDate().addDays(offset); }

// Changes a task straight in the store: state a test is not about.
inline void prep(App &app, qint64 id, const std::function<void(Task &)> &change) {
    Task t = app.store().task(id);
    change(t);
    app.store().begin("Prepare", false, true);
    app.store().putTask(t);
    app.store().commit();
}

inline void pauseEveryTag(App &app) {
    for (const Tag &t : app.store().tags()) {
        Tag copy = t;
        copy.paused = true;
        app.store().begin("Prepare", false, true);
        app.store().putTag(copy);
        app.store().commit();
    }
}

inline bool commandBar(const QString &kind) {
    QObject *bar = findObject("commandPopup");
    return bar && bar->property("visible").toBool() && bar->property("kind").toString() == kind;
}

inline bool commandBarClosed() {
    QObject *bar = findObject("commandPopup");
    return !bar || !bar->property("visible").toBool();
}

// Opens Home with a task clicked: the list holds the keyboard, as the bare
// keys need.
inline bool homeWith(App &app, qint64 id) {
    return app.openSource(lk(app.ids.home)) && tk::selectRow(app, id);
}

// True when the item with the keyboard sits inside the named item.
inline bool focusWithin(App &app, const QString &name) {
    for (QQuickItem *i = app.window->activeFocusItem(); i; i = i->parentItem())
        if (i->objectName() == name)
            return true;
    return false;
}

inline bool popupOpen(const QString &name) {
    QObject *p = findObject(name);
    return p && p->property("visible").toBool();
}

// What the shortcut rows count: URLs a task opens, windows asked for, file
// choosers asked for, and quit requests (nothing leaves the process).
struct Counters {
    QStringList urls;
    int windows = 0, pickers = 0, quits = 0;
};
inline Counters &counters() {
    static Counters c;
    return c;
}

// A row of the shortcut table. `effect` is what a user sees or what is saved;
// `undo` says how it is taken back, and `reverted` that it is.
struct KeyRow {
    enum Undo { None, CtrlZ, Again, Escape, Opposite };
    QString name;       // As Main.qml's sequence or the README's table spells it.
    QString context;    // What must be true for the key to act.
    std::function<bool(App &)> arrange;  // Empty: Home open, "Water the plants" selected.
    Qt::Key key = Qt::Key_unknown;
    Qt::KeyboardModifiers mods;
    QString typed;      // Typed into the command bar or dialog the key opens, then Return.
    std::function<bool(App &)> effect;
    Undo undo = CtrlZ;
    std::function<bool(App &)> reverted;
    Qt::Key backKey = Qt::Key_unknown;   // For Opposite: the key that takes it back.
    Qt::KeyboardModifiers backMods;
    int undoSteps = 1;   // Ctrl+Z presses it takes (a new checklist and the move into it are two).
};

inline QList<KeyRow> &keyRows() {
    static QList<KeyRow> rows;
    if (!rows.isEmpty())
        return rows;
    const Qt::KeyboardModifiers none, ctrl = Qt::ControlModifier, alt = Qt::AltModifier, shift = Qt::ShiftModifier,
        ctrlAlt = Qt::ControlModifier | Qt::AltModifier, ctrlShift = Qt::ControlModifier | Qt::ShiftModifier,
        ctrlShiftAlt = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;
    auto add = [&](KeyRow r) { rows.append(std::move(r)); };
    auto dueIs = [](int offset) { return [offset](App &a) { return a.task(a.ids.plants).dueDate == day(offset); }; };
    auto startIs = [](int offset) { return [offset](App &a) { return a.task(a.ids.plants).startDate == day(offset); }; };
    // Plants start three days ago, so the start keys have something to move.
    auto started = [](App &a) {
        prep(a, a.ids.plants, [](Task &t) { t.startDate = day(-3); });
        return homeWith(a, a.ids.plants);
    };
    auto prio = [](int level, bool electricity = false) {
        return [level, electricity](App &a) { return a.task(electricity ? a.ids.electricity : a.ids.plants).priority == level; };
    };
    auto listIs = [](bool work) { return [work](App &a) { return a.task(a.ids.plants).listId == (work ? a.ids.work : a.ids.home); }; };
    auto tagsAre = [](QStringList names) {
        return [names](App &a) { return tk::tagNames(a, a.task(a.ids.plants)) == names; };
    };
    auto deleted = [](bool gone) { return [gone](App &a) { return a.task(a.ids.plants).deleted == gone; }; };

    // ---- bare keys, task selected, list focused
    add({"]", "task selected", {}, Qt::Key_BracketRight, none, "", dueIs(2), KeyRow::CtrlZ, dueIs(1)});
    add({"[", "task selected", {}, Qt::Key_BracketLeft, none, "", dueIs(0), KeyRow::CtrlZ, dueIs(1)});
    add({"Shift+]", "task selected", {}, Qt::Key_BraceRight, shift, "", dueIs(8), KeyRow::CtrlZ, dueIs(1)});
    add({"Shift+[", "task selected", {}, Qt::Key_BraceLeft, shift, "", dueIs(-6), KeyRow::CtrlZ, dueIs(1)});
    add({".", "task with a start date", started, Qt::Key_Period, none, "", startIs(-2), KeyRow::CtrlZ, startIs(-3)});
    add({",", "task with a start date", started, Qt::Key_Comma, none, "", startIs(-4), KeyRow::CtrlZ, startIs(-3)});
    add({"Shift+.", "task with a start date", started, Qt::Key_Greater, shift, "", startIs(4), KeyRow::CtrlZ, startIs(-3)});
    add({"Shift+,", "task with a start date", started, Qt::Key_Less, shift, "", startIs(-10), KeyRow::CtrlZ, startIs(-3)});
    add({"Alt+,", "task with a start date", started, Qt::Key_Comma, alt, "",
         [](App &a) { return !a.task(a.ids.plants).startDate.isValid(); }, KeyRow::CtrlZ, startIs(-3)});
    add({"Ctrl+T", "task selected", {}, Qt::Key_T, ctrl, "", startIs(0), KeyRow::CtrlZ,
         [](App &a) { return !a.task(a.ids.plants).startDate.isValid(); }});
    add({"Ctrl+Shift+[", "task with a due time",
         [](App &a) { prep(a, a.ids.plants, [](Task &t) { t.dueTime = 600; }); return homeWith(a, a.ids.plants); },
         Qt::Key_BracketLeft, ctrlShift, "",
         [](App &a) { return a.task(a.ids.plants).dueTime == -1 && a.task(a.ids.plants).dueDate == day(1); }, KeyRow::CtrlZ,
         [](App &a) { return a.task(a.ids.plants).dueTime == 600; }});
    add({"=", "task selected", {}, Qt::Key_Equal, none, "", prio(1), KeyRow::CtrlZ, prio(0)});
    add({"-", "task with priority", [](App &a) { return homeWith(a, a.ids.electricity); }, Qt::Key_Minus, none, "", prio(2, true),
         KeyRow::CtrlZ, prio(3, true)});

    // ---- bare keys that open the command bar
    add({"d", "task selected", {}, Qt::Key_D, none, "+3d", dueIs(4), KeyRow::CtrlZ, dueIs(1)});
    // Schedule sets the start date (Defer moves the due date).
    add({"k", "task selected", [](App &a) { return homeWith(a, a.ids.buyMilk); }, Qt::Key_K, none, "tom",
         [](App &a) { return a.task(a.ids.buyMilk).startDate == day(1) && a.task(a.ids.buyMilk).dueDate == day(0); }, KeyRow::CtrlZ,
         [](App &a) { return !a.task(a.ids.buyMilk).startDate.isValid(); }});
    add({"e", "task selected", {}, Qt::Key_E, none, "3pm", [](App &a) { return a.task(a.ids.plants).dueTime == 15 * 60; }, KeyRow::CtrlZ,
         [](App &a) { return a.task(a.ids.plants).dueTime == -1; }});
    add({"x", "task selected", {}, Qt::Key_X, none, "45", [](App &a) { return a.task(a.ids.plants).duration == 45; }, KeyRow::CtrlZ,
         [](App &a) { return a.task(a.ids.plants).duration == 0; }});
    add({"l", "task selected", {}, Qt::Key_L, none, "errand", tagsAre({"errand", "garden"}), KeyRow::CtrlZ, tagsAre({"garden"})});
    add({"Alt+L", "task with a tag", {}, Qt::Key_L, alt, "\n", tagsAre({}), KeyRow::CtrlZ, tagsAre({"garden"})});
    add({"m", "task selected", {}, Qt::Key_M, none, "Work", listIs(true), KeyRow::CtrlZ, listIs(false)});
    add({"Ctrl+L", "task selected", {}, Qt::Key_L, ctrl, "Work", listIs(true), KeyRow::CtrlZ, listIs(false)});
    add({"Ctrl+R", "task selected", {}, Qt::Key_R, ctrl, "chores", tagsAre({"chores", "garden"}), KeyRow::CtrlZ, tagsAre({"garden"})});
    add({"Ctrl+Alt+R", "task with a tag", {}, Qt::Key_R, ctrlAlt, "", tagsAre({}), KeyRow::CtrlZ, tagsAre({"garden"})});

    // ---- actions on the task
    add({"p", "task with an action",
         [](App &a) {
             prep(a, a.ids.plants, [](Task &t) { t.actionType = ActionType::Url; t.actionValue = "example.org/plants"; });
             counters().urls.clear();
             return homeWith(a, a.ids.plants);
         },
         Qt::Key_P, none, "", [](App &) { return counters().urls == QStringList{"http://example.org/plants"}; }, KeyRow::None, {}});
    add({"u", "task with a link in its notes",
         [](App &a) {
             prep(a, a.ids.plants, [](Task &t) { t.notes = "see https://example.com/a and https://example.com/b"; });
             counters().urls.clear();
             return homeWith(a, a.ids.plants);
         },
         Qt::Key_U, none, "", [](App &) { return counters().urls == QStringList({"https://example.com/a", "https://example.com/b"}); },
         KeyRow::None, {}});
    add({"Ctrl+A", "list focused", [](App &a) { return homeWith(a, a.ids.buyMilk); }, Qt::Key_A, ctrl, "",
         [](App &a) { return a.backend().selectionCount() == 4; }, KeyRow::Escape,
         [](App &a) { return a.backend().selectionCount() == 0; }});
    add({"Ctrl+C", "task selected", {}, Qt::Key_C, ctrl, "",
         [](App &) { return QGuiApplication::clipboard()->text().contains("Water the plants"); }, KeyRow::None, {}});
    add({"Ctrl+X", "task selected", {}, Qt::Key_X, ctrl, "",
         [](App &a) { return a.task(a.ids.plants).deleted && QGuiApplication::clipboard()->text().contains("Water the plants"); },
         KeyRow::CtrlZ, deleted(false)});
    add({"Ctrl+V", "text on the clipboard",
         [](App &a) { QGuiApplication::clipboard()->setText("Pasted by shortcut"); return homeWith(a, a.ids.plants); },
         Qt::Key_V, ctrl, "", [](App &a) { return a.taskTitled("Pasted by shortcut").id != 0; }, KeyRow::CtrlZ,
         [](App &a) { return a.taskTitled("Pasted by shortcut").id == 0; }});
    add({"Backspace", "task selected", {}, Qt::Key_Backspace, none, "", deleted(true), KeyRow::CtrlZ, deleted(false)});
    add({"Ctrl+Backspace", "task selected", {}, Qt::Key_Backspace, ctrl, "", deleted(true), KeyRow::CtrlZ, deleted(false)});
    add({"Ctrl+Shift+Up", "task selected", {}, Qt::Key_Up, ctrlShift, "",
         [](App &a) { return a.task(a.ids.plants).order < a.task(a.ids.electricity).order; }, KeyRow::CtrlZ,
         [](App &a) { return a.task(a.ids.plants).order > a.task(a.ids.electricity).order; }});
    add({"Ctrl+Shift+Down", "task selected", [](App &a) { return homeWith(a, a.ids.electricity); }, Qt::Key_Down, ctrlShift, "",
         [](App &a) { return a.task(a.ids.electricity).order > a.task(a.ids.plants).order; }, KeyRow::CtrlZ,
         [](App &a) { return a.task(a.ids.electricity).order < a.task(a.ids.plants).order; }});

    // ---- view toggles
    // Plants start in five days, so the list hides it until scheduled tasks show.
    auto hidden = [](App &a) { return a.openSource(lk(a.ids.home)) && waitUntil([&] { return a.row(a.ids.plants) == nullptr; }); };
    auto scheduled = [=](App &a) {
        prep(a, a.ids.plants, [](Task &t) { t.startDate = day(5); });
        return hidden(a);
    };
    auto shown = [](App &a) { return a.row(a.ids.plants) != nullptr; };
    add({"Ctrl+K", "a scheduled task hidden", scheduled, Qt::Key_K, ctrl, "",
         [=](App &a) { return a.backend().showScheduled() && shown(a); }, KeyRow::Again,
         [](App &a) { return !a.backend().showScheduled() && a.row(a.ids.plants) == nullptr; }});
    add({"Ctrl+U", "a task with a paused tag hidden", [=](App &a) { pauseEveryTag(a); return hidden(a); }, Qt::Key_U, ctrl, "",
         [=](App &a) { return a.backend().showPaused() && shown(a); }, KeyRow::Again,
         [](App &a) { return !a.backend().showPaused() && a.row(a.ids.plants) == nullptr; }});
    add({"Ctrl+Shift+K", "scheduled and paused tasks hidden",
         [=](App &a) { prep(a, a.ids.plants, [](Task &t) { t.startDate = day(5); }); pauseEveryTag(a); return hidden(a); },
         Qt::Key_K, ctrlShift, "", [=](App &a) { return a.backend().showScheduled() && a.backend().showPaused() && shown(a); },
         KeyRow::Again, [](App &a) { return !a.backend().showScheduled() && !a.backend().showPaused() && a.row(a.ids.plants) == nullptr; }});
    add({"Ctrl+-", "list focused", [](App &a) { return homeWith(a, a.ids.plants); }, Qt::Key_Minus, ctrl, "",
         [](App &a) { return a.backend().textSize() == 1; }, KeyRow::Opposite,
         [](App &a) { return a.backend().textSize() == 2; }, Qt::Key_Equal, ctrl});
    add({"Ctrl+Alt+Left", "list focused", {}, Qt::Key_Left, ctrlAlt, "",
         [](App &) { return findItem("sidebarAdd") == nullptr; }, KeyRow::Again, [](App &) { return findItem("sidebarAdd") != nullptr; }});
    add({"Ctrl+Alt+Right", "list focused", {}, Qt::Key_Right, ctrlAlt, "",
         [](App &) { return findItem("tagsManualToggle") == nullptr; }, KeyRow::Again, [](App &) { return findItem("tagsManualToggle") != nullptr; }});
    add({"Ctrl+J", "list focused", {}, Qt::Key_J, ctrl, "", [](App &a) { return a.backend().focusOn(); }, KeyRow::Again,
         [](App &a) { return !a.backend().focusOn(); }});

    // ---- the editor's quick jumps: from the list (the shortcut creates the
    // editor) and with the editor already open.
    auto editorOpen = [](App &a) { return homeWith(a, a.ids.plants) && openEditor(a, a.ids.plants); };
    auto jump = [&](const QString &name, Qt::Key key, Qt::KeyboardModifiers mods, const std::function<bool(App &)> &seen) {
        add({name, "task selected, editor closed", {}, key, mods, "", seen, KeyRow::Escape, [](App &) { return popupsClosed(); }});
        add({name, "editor open", editorOpen, key, mods, "", seen, KeyRow::Escape, [](App &) { return popupsClosed(); }});
    };
    jump("Ctrl+E", Qt::Key_E, ctrl, [](App &a) { return focusWithin(a, "editorNotes"); });
    jump("Ctrl+B", Qt::Key_B, ctrl, [](App &) { return menuEntry("Mail") != nullptr; });
    jump("Ctrl+G", Qt::Key_G, ctrl, [](App &) { return menuEntry("New Location…") != nullptr; });
    jump("Ctrl+'", Qt::Key_Apostrophe, ctrl, [](App &) { return menuEntry("At due time") != nullptr; });
    jump("Ctrl+/", Qt::Key_Slash, ctrl, [](App &a) { return focusWithin(a, "editorDue"); });
    add({"Ctrl+Return", "editor open", editorOpen, Qt::Key_Return, ctrl, "", [](App &) { return findItem("editorCard") == nullptr; }, KeyRow::None, {}});

    // ---- things the keys create
    add({"Alt+N", "task selected", {}, Qt::Key_N, alt, "",
         [](App &a) {
             const qint64 id = a.backend().editingTaskId();
             return id != 0 && a.task(id).parentId == a.ids.plants;
         },
         KeyRow::Escape, [](App &a) { return a.backend().editingTaskId() == 0; }});
    add({"Ctrl+Alt+T", "any", {}, Qt::Key_T, ctrlAlt, "walking",
         [](App &a) {
             for (const Tag &t : a.store().tags())
                 if (!t.deleted && t.name == "walking") return true;
             return false;
         },
         KeyRow::CtrlZ,
         [](App &a) {
             for (const Tag &t : a.store().tags())
                 if (!t.deleted && t.name == "walking") return false;
             return true;
         }});
    add({"Ctrl+Shift+Alt+T", "any", {}, Qt::Key_T, ctrlShiftAlt, "Outdoors",
         [](App &a) {
             for (const TagGroup &g : a.store().tagGroups())
                 if (!g.deleted && g.name == "Outdoors") return true;
             return false;
         },
         KeyRow::CtrlZ,
         [](App &a) {
             for (const TagGroup &g : a.store().tagGroups())
                 if (!g.deleted && g.name == "Outdoors") return false;
             return true;
         }});
    // The new checklist opens for its name; Return accepts "New Checklist".
    add({"Ctrl+Alt+C", "task selected", {}, Qt::Key_C, ctrlAlt, "\n",
         [](App &a) {
             const Task t = a.task(a.ids.plants);
             return t.parentId != 0 && a.task(t.parentId).type == TaskType::Checklist;
         },
         KeyRow::CtrlZ, [](App &a) { return a.task(a.ids.plants).parentId == 0; }});
    add({"Shift+F5", "two tasks visited",
         [](App &a) { return homeWith(a, a.ids.plants) && tk::selectRow(a, a.ids.electricity); }, Qt::Key_F5, shift, "",
         [](App &a) { return a.backend().currentTaskId() == a.ids.plants; }, KeyRow::None, {}});
    add({"Ctrl+Alt+W", "any",
         [](App &a) {
             counters().windows = 0;
             QObject::connect(&a.backend(), &Backend::newWindowRequested, &a.backend(), [] { ++counters().windows; });
             return homeWith(a, a.ids.plants);
         },
         Qt::Key_W, ctrlAlt, "", [](App &) { return counters().windows == 1; }, KeyRow::None, {}});
    add({"Ctrl+P", "list shown",
         [](App &a) {
             counters().pickers = 0;
             QObject::connect(&a.services().filePicker, &FilePicker::failed, &a.backend(), [] { ++counters().pickers; });
             return homeWith(a, a.ids.plants);
         },
         Qt::Key_P, ctrl, "", [](App &) { return counters().pickers == 1; }, KeyRow::None, {}});
    add({"Ctrl+Shift+E", "any", {}, Qt::Key_E, ctrlShift, "", [](App &) { return findText("Export Tasks") != nullptr; }, KeyRow::Escape,
         [](App &) { return findText("Export Tasks") == nullptr; }});
    add({"F11", "any", {}, Qt::Key_F11, none, "", [](App &a) { return a.window->visibility() == QWindow::FullScreen; }, KeyRow::Again,
         [](App &a) { return a.window->visibility() != QWindow::FullScreen; }});
    add({"?", "list focused", {}, Qt::Key_Question, shift, "", [](App &) { return popupOpen("shortcutOverlay"); }, KeyRow::Escape,
         [](App &) { return !popupOpen("shortcutOverlay"); }});
    add({"/", "list focused", {}, Qt::Key_Slash, none, "",
         [](App &) { return findItem("tagSearch") && findItem("tagSearch")->property("activeFocus").toBool(); }, KeyRow::Escape,
         [](App &) { return findItem("tagSearch") == nullptr; }});
    auto quitSpy = [](App &a) {
        counters().quits = 0;
        // Qt.quit() would end the test run; the engine's own quit signal is what is counted.
        QObject::disconnect(&a.engine(), &QQmlEngine::quit, nullptr, nullptr);
        QObject::connect(&a.engine(), &QQmlEngine::quit, &a.backend(), [] { ++counters().quits; });
        return homeWith(a, a.ids.plants);
    };
    add({"q", "list focused", quitSpy, Qt::Key_Q, none, "", [](App &) { return counters().quits == 1; }, KeyRow::None, {}});
    add({"Ctrl+Q", "any", quitSpy, Qt::Key_Q, ctrl, "", [](App &) { return counters().quits == 1; }, KeyRow::None, {}});
    return rows;
}

}  // namespace mk
}  // namespace e2e
