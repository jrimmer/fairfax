#pragma once

// Helpers for the Smart List and search scenarios: a rich fixture that
// exercises every task property a query can name, readers for what the task
// pane shows, and the clicks and keystrokes a user makes in the search bar,
// the rule builder and the sidebar's Smart Lists section.
//
// The fixture is described once, as a table of specs (`specs()`). The seed
// builds the store from it, and the few expectations that depend on the day
// of the week (this week, next week) are derived from the same table, so a
// test never hard-codes a weekday.

#include <climits>

#include "listkit.h"

namespace e2e {

constexpr int NoDay = INT_MIN;

// One task of the fixture. Days are offsets from today; hours are ages.
struct Spec {
    QString title, list;
    int due = NoDay, start = NoDay;       // Days from today.
    int dueTime = -1;                     // Minutes after midnight.
    int duration = 0;                     // Minutes.
    int prio = 0;                         // 0 none, 1 low, 2 medium, 3 high.
    bool star = false;
    int type = TaskType::Task;
    QString parent;                       // Title of the project, checklist or task above.
    int doneDays = NoDay;                 // Completed that many days ago (at noon)…
    bool doneNow = false;                 // …or just now.
    QStringList tags, locs;
    int action = 0;                       // ActionType.
    QString notes;
    int attach = -1;                      // Attachment::Kind, or -1.
    int repeat = 0;                       // Repeat::Frequency.
    bool alarm = false;
    int createdHours = 90 * 24, modifiedHours = 90 * 24;

    Spec(const char *t, const char *l) : title(t), list(l) {}
    Spec &dueIn(int d) { due = d; return *this; }
    Spec &startIn(int d) { start = d; return *this; }
    Spec &at(int minutes) { dueTime = minutes; return *this; }
    Spec &lasts(int minutes) { duration = minutes; return *this; }
    Spec &priority(int p) { prio = p; return *this; }
    Spec &starred() { star = true; return *this; }
    Spec &kind(int k) { type = k; return *this; }
    Spec &under(const char *p) { parent = p; return *this; }
    Spec &doneAgo(int d) { doneDays = d; return *this; }
    Spec &doneToday() { doneNow = true; return *this; }
    Spec &tagged(std::initializer_list<const char *> names) { for (auto n : names) tags << n; return *this; }
    Spec &located(std::initializer_list<const char *> names) { for (auto n : names) locs << n; return *this; }
    Spec &act(int a) { action = a; return *this; }
    Spec &note(const char *n) { notes = n; return *this; }
    Spec &attached(int k) { attach = k; return *this; }
    Spec &repeats(int f) { repeat = f; return *this; }
    Spec &alert() { alarm = true; return *this; }
    Spec &made(int hours) { createdHours = hours; return *this; }
    Spec &edited(int hours) { modifiedHours = hours; return *this; }

    bool completed() const { return doneNow || doneDays != NoDay; }
    bool scheduled() const { return start != NoDay && start > 0; }
    bool paused() const { return tags.contains("onhold"); }
};

// The fixture: a hundred-odd properties across seventy tasks. Hidden from
// All by default: the completed ones, the scheduled ones (future start), the
// paused one, and children of checklists and plain tasks (they show nested).
inline const QList<Spec> &specs() {
    static const QList<Spec> all = [] {
        QList<Spec> v;
        auto add = [&v](const char *t, const char *l) -> Spec & { v.append(Spec(t, l)); return v.last(); };
        // Dates.
        add("Plain chore", "Home");
        add("Rent overdue", "Home").dueIn(-3).priority(3).tagged({"urgent"});
        add("Invoice overdue", "Work").dueIn(-1).tagged({"waiting"});
        add("Milk today", "Home").dueIn(0).priority(2).tagged({"home"}).located({"Home"});
        add("Dentist today", "Work").dueIn(0).at(17 * 60).priority(1).alert();
        add("Parcel tomorrow", "Home").dueIn(1).tagged({"errands"}).located({"Drycleaners"});
        add("Report in 3 days", "Work").dueIn(3);
        add("Fence in 6 days", "Garden").dueIn(6).priority(3);
        add("Taxes in 9 days", "Garden").dueIn(9);
        add("Passport in 14 days", "Garden").dueIn(14).priority(2);
        add("Starts today", "Work").startIn(0);
        add("Starts tomorrow", "Work").startIn(1);
        add("Starts in 3 due in 5", "Website").startIn(3).dueIn(5);
        add("Began yesterday due in 2", "Website").startIn(-1).dueIn(2);
        add("Began 5 days ago", "Home").startIn(-5);
        add("Two day window", "Work").dueIn(0).lasts(2 * 1440);
        add("Quick win", "Home").lasts(30);
        add("Fresh window", "Home").dueIn(0).lasts(1440).made(2).edited(2);
        add("Repeating window", "Work").dueIn(1).lasts(2 * 1440).repeats(Repeat::Weekly);
        // Status.
        add("Starred plain", "Inbox").starred();
        add("Starred urgent", "Work").starred().priority(3).dueIn(2);
        add("Weekly repeat", "Home").dueIn(4).repeats(Repeat::Weekly).priority(1);
        add("Daily repeat", "Home").dueIn(0).repeats(Repeat::Daily);
        add("Paused tag task", "Home").tagged({"onhold"});
        // Actions.
        add("Call mum", "Home").act(ActionType::Call).tagged({"phone"});
        add("Text Alex", "Work").act(ActionType::Message);
        add("Email boss", "Work").act(ActionType::Mail).tagged({"work", "mail"});
        add("Browse docs", "Website").act(ActionType::Url);
        add("Visit shop", "Home").act(ActionType::Visit).located({"Drycleaners"});
        add("Google it", "Home").act(ActionType::Google);
        // Notes and attachments.
        add("Noted chore", "Home").note("remember the receipt");
        add("Picture job", "Garden").attached(Attachment::Image);
        add("Voice memo job", "Garden").attached(Attachment::Audio);
        // Structure.
        add("Launch project", "Website").kind(TaskType::Project).priority(3);
        add("Launch step one", "Website").under("Launch project").dueIn(1);
        add("Launch step later", "Website").under("Launch project").dueIn(20);
        add("Launch step done", "Website").under("Launch project").doneToday();
        add("Empty project", "Garden").kind(TaskType::Project);
        add("Finished project", "Garden").kind(TaskType::Project);
        add("Finished step", "Garden").under("Finished project").doneAgo(1);
        add("Shopping checklist", "Home").kind(TaskType::Checklist).priority(2);
        add("Eggs", "Home").under("Shopping checklist");
        add("Bread", "Home").under("Shopping checklist");
        add("Empty checklist", "Work").kind(TaskType::Checklist);
        add("Parent task", "Home");
        add("Sub step", "Home").under("Parent task");
        // Completed.
        add("Done just now", "Work").doneToday();
        add("Done yesterday", "Home").doneAgo(1);
        add("Done 3 days ago", "Home").doneAgo(3);
        add("Done 40 days ago", "Archive").doneAgo(40);
        // Other lists.
        add("Archive item", "Archive").dueIn(1);
        add("Inbox note", "Inbox");
        // Free text mixed with keywords.
        add("Fix plumber leak", "Home").starred().tagged({"builders"});
        add("Plumber quote", "Home").starred();
        add("Plumber for garden", "Garden").starred().tagged({"builders"});
        add("Plumber for office", "Work").starred().tagged({"builders"});
        // Tags and locations.
        add("Blocked work", "Work").tagged({"work", "blocked"});
        add("Play tagged work", "Work").tagged({"work", "play"});
        add("Call work", "Work").tagged({"call", "work"});
        add("Someday idea", "Home").tagged({"someday"});
        add("Deep focus", "Work").tagged({"deep work"});
        add("Office desk", "Work").located({"Office"});
        add("Office and playground", "Work").located({"Office", "Playground"});
        // Created and modified.
        add("Fresh note", "Home").made(1).edited(1);
        add("Edited lately", "Home").made(30 * 24).edited(5);
        add("Made 3 days ago", "Home").made(3 * 24).edited(3 * 24);
        // Alerts, priorities.
        add("Alarm without date", "Home").alert();
        add("Low plain", "Garden").priority(1);
        add("Medium plain", "Garden").priority(2);
        return v;
    }();
    return all;
}

// Titles of the fixture's tasks matching a predicate on the spec.
template <class F>
QStringList specTitles(F pred) {
    QStringList out;
    for (const Spec &s : specs())
        if (pred(s))
            out << s.title;
    out.sort();
    return out;
}

// Whether the day `offset` days from today lies in the current week, which
// the fixture starts on Monday.
inline bool inThisWeek(int offset) {
    const QDate today = QDate::currentDate();
    const QDate first = today.addDays(-(today.dayOfWeek() - 1));
    const QDate d = today.addDays(offset);
    return d >= first && d <= first.addDays(6);
}

inline bool inNextWeek(int offset) {
    const QDate today = QDate::currentDate();
    const QDate first = today.addDays(-(today.dayOfWeek() - 1)).addDays(7);
    const QDate d = today.addDays(offset);
    return d >= first && d <= first.addDays(6);
}

// A task's parent is either absent or a project: only then does the child
// surface as a row of its own (checklist items and sub-tasks nest instead).
inline bool parentOk(const Spec &s) {
    if (s.parent.isEmpty())
        return true;
    for (const Spec &p : specs())
        if (p.title == s.parent)
            return p.type == TaskType::Project;
    return false;
}

// What All shows when nothing is typed: open top-level tasks, project
// children, no scheduled (future start) or paused ones unless asked for.
inline bool shownInAll(const Spec &s, bool includeScheduled = false) {
    if (s.completed() || s.paused() || (s.scheduled() && !includeScheduled))
        return false;
    return parentOk(s);
}

// Days from today a completed task was completed on.
inline int doneOffset(const Spec &s) { return s.doneNow ? 0 : -s.doneDays; }

inline QStringList allShown() { return specTitles([](const Spec &s) { return shownInAll(s); }); }

inline QStringList without(QStringList base, const QStringList &drop) {
    for (const QString &d : drop)
        base.removeAll(d);
    return base;
}

inline QStringList sorted(QStringList l) {
    l.sort();
    return l;
}

// ------------------------------------------------------------------- seed

struct SmartWorld {
    QHash<QString, qint64> task, list, group;
    qint64 clutter = 0;
};

inline SmartWorld &smartWorld() {
    static SmartWorld w;
    return w;
}

// The fixture world. The calendar is hidden so the whole sidebar is on
// screen, and the week starts on Monday so "this week" is predictable.
inline Seed seedSmart(Store &store) {
    SmartWorld w;
    const QDate today = QDate::currentDate();
    const QDateTime now = QDateTime::currentDateTime();
    store.setSetting("appearance.showCalendar", false);
    store.setSetting("general.weekStart", 1);
    store.begin("Seed smart", false, true);

    auto group = [&](const char *name, double order) {
        ListGroup g;
        g.id = store.nextId("list_groups");
        g.name = name;
        g.order = order;
        g.modified = now;
        store.putGroup(g);
        w.group[name] = g.id;
    };
    group("Home", 1);
    group("Projects", 2);
    auto list = [&](const char *name, int color, double order, const char *grp, int kind = ListKind::Normal) {
        TaskList l;
        l.id = store.nextId("lists");
        l.name = name;
        l.color = color;
        l.order = order;
        l.kind = kind;
        l.groupId = grp ? w.group.value(grp) : 0;
        l.modified = now;
        store.putList(l);
        w.list[name] = l.id;
    };
    list("Inbox", 6, 0, nullptr, ListKind::Inbox);
    list("Home", 0, 1, "Home");
    list("Garden", 3, 2, "Home");
    list("Work", 2, 1, "Projects");
    list("Website", 4, 2, "Projects");
    list("Archive", 5, 5, nullptr);

    QHash<QString, qint64> tagGroups, tags, places;
    auto tagGroup = [&](const char *name, double order) {
        TagGroup g;
        g.id = store.nextId("tag_groups");
        g.name = name;
        g.order = order;
        g.modified = now;
        store.putTagGroup(g);
        tagGroups[name] = g.id;
    };
    tagGroup("Contexts", 1);
    tagGroup("Someday", 2);
    auto tag = [&](const char *name, const char *grp, bool paused = false) {
        Tag t;
        t.id = store.nextId("tags");
        t.name = name;
        t.groupId = grp ? tagGroups.value(grp) : 0;
        t.paused = paused;
        t.modified = now;
        store.putTag(t);
        tags[name] = t.id;
    };
    for (const char *n : {"phone", "errands", "home", "work"})
        tag(n, "Contexts");
    tag("someday", "Someday");
    for (const char *n : {"urgent", "waiting", "blocked", "play", "mail", "call", "builders", "deep work"})
        tag(n, nullptr);
    tag("onhold", nullptr, true);
    for (const char *n : {"Home", "Drycleaners", "Playground", "Office"}) {
        Location l;
        l.id = store.nextId("locations");
        l.name = n;
        l.modified = now;
        store.putLocation(l);
        places[n] = l.id;
    }

    double order = 0;
    // Parents first, so a child can name its parent's id.
    for (int pass = 0; pass < 2; ++pass) {
        for (const Spec &s : specs()) {
            if (s.parent.isEmpty() != (pass == 0))
                continue;
            Task t;
            t.id = store.nextId("tasks");
            t.listId = w.list.value(s.list);
            t.parentId = s.parent.isEmpty() ? 0 : w.task.value(s.parent);
            t.type = s.type;
            t.title = s.title;
            t.notes = s.notes;
            t.priority = s.prio;
            t.starred = s.star;
            if (s.due != NoDay)
                t.dueDate = today.addDays(s.due);
            if (s.start != NoDay)
                t.startDate = today.addDays(s.start);
            t.dueTime = s.dueTime;
            t.duration = s.duration;
            t.actionType = s.action;
            if (s.action)
                t.actionValue = "value";
            if (s.repeat) {
                t.recurrence.frequency = s.repeat;
                t.recurrence.interval = 1;
            }
            for (const QString &n : s.tags)
                t.tagIds << tags.value(n);
            for (const QString &n : s.locs)
                t.locationIds << places.value(n);
            if (s.completed()) {
                t.completed = true;
                t.completedAt = s.doneNow ? now : QDateTime(today.addDays(-s.doneDays), QTime(12, 0));
            }
            t.order = ++order;
            t.created = now.addSecs(-qint64(s.createdHours) * 3600);
            t.modified = now.addSecs(-qint64(s.modifiedHours) * 3600);
            store.putTask(t);
            w.task[s.title] = t.id;
            if (s.alarm) {
                Alarm a;
                a.id = store.nextId("alarms");
                a.taskId = t.id;
                a.kind = s.due == NoDay ? Alarm::Absolute : Alarm::Relative;
                a.offset = 15;
                a.at = now.addDays(30);
                a.modified = now;
                store.putAlarm(a);
            }
            if (s.attach >= 0) {
                Attachment at;
                at.id = store.nextId("attachments");
                at.taskId = t.id;
                at.kind = s.attach;
                at.name = s.attach == Attachment::Audio ? "memo.m4a" : "photo.png";
                at.fileName = QString("fixture%1.bin").arg(at.id);
                at.created = at.modified = now;
                store.putAttachment(at);
            }
        }
    }
    store.commit();
    smartWorld() = w;
    return Seed();
}

// A small world for the free-text tests, with the manual's own sample tasks.
inline Seed seedText(Store &store) {
    store.setSetting("appearance.showCalendar", false);
    store.begin("Seed text", false);
    ListGroup g;
    g.id = store.nextId("list_groups");
    g.name = "Home";
    g.order = 1;
    store.putGroup(g);
    TaskList l;
    l.id = store.nextId("lists");
    l.name = "Notes";
    l.groupId = g.id;
    l.order = 1;
    store.putList(l);
    TaskList other;
    other.id = store.nextId("lists");
    other.name = "Elsewhere";
    other.groupId = g.id;
    other.order = 2;
    store.putList(other);
    Tag t;
    t.id = store.nextId("tags");
    t.name = "tagged";
    store.putTag(t);
    const struct { const char *title, *notes; int list; } rows[] = {
        {"Get a haircut one day", "", 0},
        {"Buy a new car some day", "", 0},
        {"Find a new job", "", 0},
        {"Call: Mum, tell about my new job", "", 0},
        {"Call: Tim", "", 0},
        {"Renew passport", "", 0},
        {"Read the handbook", "", 0},
        {"Pack the notebook", "", 0},
        {"Visit the bookshop", "", 0},
        {"Review the project", "", 0},
        {"Pick a subject", "", 0},
        {"Rejected idea", "", 0},
        {"Call the plumber", "", 0},
        {"Pay Stephen", "", 0},
        {"Thank Alexander", "", 0},
        {"Ring John", "", 0},
        {"Quiet title", "the plumber noted this in the notes", 0},
        {"Email a@b.com", "", 0},
        {"Tag text #garden thing", "", 0},
        {"Buy bread and butter", "", 0},
        {"Walk or run", "", 0},
        {"Elsewhere job offer", "", 1},
    };
    double order = 0;
    for (const auto &r : rows) {
        Task task;
        task.id = store.nextId("tasks");
        task.listId = r.list ? other.id : l.id;
        task.title = r.title;
        task.notes = r.notes;
        task.order = ++order;
        if (QString(r.title) == "Quiet title")
            task.tagIds << t.id;
        store.putTask(task);
    }
    store.commit();
    return Seed();
}

// ----------------------------------------------------------- what the pane shows

inline QVariantList paneRows(App &app) {
    QVariantList out;
    for (const QVariantMap &row : app.backend().tasks()->rows())
        if (row.value("kind").toString() == "task")
            out << row;
    return out;
}

// Titles of the tasks the pane lists, sorted; nested rows (checklist items,
// children of a project that is itself listed) only when `nested`.
inline QStringList paneTitles(App &app, bool nested = false) {
    QStringList out;
    for (const QVariant &v : paneRows(app)) {
        const QVariantMap row = v.toMap();
        if (nested || row.value("depth").toInt() == 0)
            out << row.value("title").toString();
    }
    out.sort();
    return out;
}

// Titles of the rows actually drawn (the list virtualises, so a long result
// shows only what fits).
inline QStringList drawnTitles(App &app) {
    QStringList out;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if (i->objectName().startsWith("taskRow:") && i->height() > 0) {
                const qint64 id = i->objectName().mid(8).toLongLong();
                out << app.task(id).title;
            }
            return false;
        });
    out.sort();
    return out;
}

// The pane lists exactly `expected` (as top-level rows), and what is drawn is
// consistent with it: nothing drawn that should not be, and for a short result
// everything drawn.
// What is drawn agrees with the rows: nothing drawn that is not listed and,
// for a short result, everything listed is drawn. Returns a description of the
// first disagreement, or an empty string.
inline QString drawnDisagrees(App &app, const QStringList &want) {
    const QStringList drawn = drawnTitles(app);
    const QStringList all = paneTitles(app, true);
    for (const QString &d : drawn)
        if (!all.contains(d))
            return "drawn but not in the result: " + d;
    if (want.size() <= 8)
        for (const QString &w : want)
            if (!drawn.contains(w))
                return "listed but not drawn: " + w;
    return {};
}

// What is extra and what is missing, for a failure message.
inline QString titleDiff(const QStringList &got, const QStringList &want) {
    QStringList extra, missing;
    for (const QString &g : got)
        if (!want.contains(g))
            extra << g;
    for (const QString &w : want)
        if (!got.contains(w))
            missing << w;
    return QString("rows differ. extra: [%1] missing: [%2] (%3 shown, %4 expected)")
        .arg(extra.join(" | "), missing.join(" | ")).arg(got.size()).arg(want.size());
}

#define SMART_SHOWS(app, expected)                                                                                     \
    do {                                                                                                               \
        const QStringList want_ = sorted(expected);                                                                    \
        QTRY_VERIFY2_WITH_TIMEOUT(paneTitles(app) == want_, qPrintable(titleDiff(paneTitles(app), want_)), 5000);     \
        QTRY_VERIFY2_WITH_TIMEOUT(drawnDisagrees(app, want_).isEmpty(), qPrintable(drawnDisagrees(app, want_)), 5000); \
    } while (0)

// Types `query` into the search bar and checks the pane lists exactly
// `expected`.
#define SMART_QUERY(app, query, expected)                                                                              \
    do {                                                                                                               \
        QVERIFY2(typeSearch(app, query), qPrintable(QString("could not type: ") + (query)));                           \
        SMART_SHOWS(app, expected);                                                                                    \
    } while (0)

inline qint64 idOf(const QString &title) { return smartWorld().task.value(title); }
inline qint64 listId(const QString &name) { return smartWorld().list.value(name); }

// --------------------------------------------------------------- the search bar

inline QQuickItem *searchField() { return findItem("searchField"); }

// Puts a query in the toolbar's search field: Ctrl+F to open and focus it, then
// the text. By default the text is set on the field and its textEdited signal
// raised, which runs the same handler as typing (a few hundred queries one key
// at a time would take minutes); `keys` sends real key presses instead.
inline bool typeSearch(App &app, const QString &query, bool keys = false) {
    if (!settle())
        return false;
    app.press(Qt::Key_F, Qt::ControlModifier);
    if (!waitUntil([&] { return searchField() && searchField()->property("activeFocus").toBool(); }))
        return false;
    if (keys) {
        app.press(Qt::Key_A, Qt::ControlModifier);
        if (query.isEmpty())
            app.press(Qt::Key_Delete);
        else
            app.type(query);
    } else {
        // insert() and remove() edit the text the way typing does, keeping the
        // field's binding to the search; textEdited is what typing raises.
        QMetaObject::invokeMethod(searchField(), "remove", Q_ARG(int, 0), Q_ARG(int, searchField()->property("length").toInt()));
        QMetaObject::invokeMethod(searchField(), "insert", Q_ARG(int, 0), Q_ARG(QString, query));
        QMetaObject::invokeMethod(searchField(), "textEdited");
    }
    return waitUntil([&] { return app.backend().search() == query; });
}

inline bool searchOpen() { return searchField() != nullptr; }

// Clears the search with Escape and waits for the pane to follow.
inline bool escapeSearch(App &app) {
    if (!searchOpen())
        return app.backend().search().isEmpty();
    app.press(Qt::Key_Escape);
    if (!waitUntil([&] { return app.backend().search().isEmpty() && !searchOpen(); }, 2000)) {
        // Escape went to the list (the field had lost focus): use the cross.
        if (QQuickItem *cross = findItem("searchClear"))
            clickSteady(cross);
    }
    return waitUntil([&] { return app.backend().search().isEmpty() && !searchOpen(); });
}

// Ctrl+K (View > Show Scheduled Tasks) until the pane shows, or hides,
// scheduled tasks.
inline bool showScheduled(App &app, bool on) {
    if (app.backend().showScheduled() != on)
        app.press(Qt::Key_K, Qt::ControlModifier);
    return waitUntil([&] { return app.backend().showScheduled() == on; });
}

// Changes a task behind the window's back, as a sync or the command line
// does; the open view has to notice by itself.
inline void externally(App &app, const QString &title, const std::function<void(Task &)> &edit) {
    Task t = app.taskTitled(title);
    edit(t);
    app.store().begin("External edit");
    app.store().putTask(t);
    app.store().commit();
}

// Selects a row by clicking it.
inline bool selectRow(App &app, const QString &title) {
    const qint64 id = idOf(title);
    QQuickItem *row = nullptr;
    if (!waitUntil([&] { return (row = app.row(id)) != nullptr; })) {
        qWarning() << "selectRow: no row for" << title << "in" << app.backend().sourceKey() << "which lists" << paneTitles(app, true);
        return false;
    }
    // Rows slide into place when the list changes; aim again if a click landed
    // on the neighbour that was there a moment ago.
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        QTest::qWait(150);
        row = app.row(id);
        if (!row)
            break;
        click(row, Qt::LeftButton, pointIn(row, 0.5, 0.5));
        ok = waitUntil([&] { return app.backend().currentTaskId() == id; }, 2000);
    }
    if (!ok)
        qWarning() << "selectRow: clicking" << title << "selected" << app.backend().currentTaskId() << "not" << id;
    return ok;
}

// ----------------------------------------------------------- the rule builder

// Scrolls every scrolling view above `item` (the builder has a scroll area
// within a scroll area) until the item is on screen.
inline void scrollAll(QQuickItem *item) {
    for (QQuickItem *up = item->parentItem(); up; up = up->parentItem()) {
        if (!up->inherits("QQuickFlickable"))
            continue;
        // Move the view until the item's centre is well inside the part of the
        // view that shows on screen. The sheet's footer sits over the lower edge
        // of the scroll area, so an item near it takes no clicks.
        for (int step = 0; step < 8; ++step) {
            const QRectF view = up->mapRectToScene(QRectF(0, 0, up->width(), up->height()));
            const qreal cy = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).y();
            const qreal want = view.center().y();
            if (qAbs(cy - want) < view.height() * 0.3)
                break;
            const qreal total = up->property("contentHeight").toReal();
            qreal y = up->property("contentY").toReal() + (cy - want);
            y = qMax<qreal>(0, qMin(y, total - up->height()));
            if (qFuzzyCompare(y + 1, up->property("contentY").toReal() + 1))
                break;
            up->setProperty("contentY", y);
            QTest::qWait(20);
        }
    }
    QTest::qWait(30);
}

inline QObject *builder() { return findObject("searchBuilder"); }

inline bool builderOpen() { return findItem("freeInput") != nullptr; }

// The query the builder has composed so far (what the QUERY box shows).
inline QString builderQuery() { return builder() ? builder()->property("query").toString() : QString(); }

// Opens Search Options with its shortcut and waits for it.
inline bool openBuilder(App &app) {
    if (!settle())
        return false;
    app.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    return waitUntil([] { return builderOpen(); });
}

inline bool closeBuilder(App &app) {
    if (!builderOpen())
        return true;
    app.press(Qt::Key_Escape);
    return waitUntil([] { return !builderOpen(); });
}

// Clicks one of the builder's controls (chips, segments, buttons) by
// objectName, once it has been scrolled into view and has stopped moving.
inline bool pressBuilder(const QString &name, Qt::MouseButton button = Qt::LeftButton) {
    QQuickItem *item = nullptr;
    if (!waitUntil([&] { return (item = findItem(name)) != nullptr; }))
        return false;
    // The sheet scrolls inside a scroll area; a click aimed before the view has
    // settled can land outside the window, so aim until the item is inside it.
    for (int attempt = 0; attempt < 5; ++attempt) {
        scrollAll(item);
        QTest::qWait(60);
        const QPoint c = centreOf(item);
        if (c.y() > 0 && c.y() < item->window()->height() - 2 && c.x() > 0 && c.x() < item->window()->width() - 2)
            break;
    }
    // A chip answers a click by changing state; a click that the opening sheet
    // swallowed is repeated.
    const bool chip = name.contains("Chip:");
    const int before = chip ? item->property("state").toInt() : 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt) {
            scrollAll(item);
            QTest::qWait(60);
        }
        clickSteady(item, button);
        if (!chip || waitUntil([&] { return item->property("state").toInt() != before; }, 2500))
            break;
    }
    return true;
}

inline int chipState(const QString &name) {
    QQuickItem *chip = findItem(name);
    return chip ? chip->property("state").toInt() : -1;
}

// Types into a builder text field (replacing its text).
inline bool fillBuilder(App &app, const QString &name, const QString &text) {
    QQuickItem *field = nullptr;
    if (!waitUntil([&] { return (field = findItem(name)) != nullptr; }))
        return false;
    // A row that has only just appeared moves as the sheet lays out: aim
    // again until the field takes focus.
    for (int attempt = 0; attempt < 4 && !field->property("activeFocus").toBool(); ++attempt) {
        QTest::qWait(100);
        scrollAll(field);
        clickSteady(field);
        waitUntil([&] { return field->property("activeFocus").toBool(); }, 2000);
    }
    if (!field->property("activeFocus").toBool())
        return false;
    app.press(Qt::Key_A, Qt::ControlModifier);
    if (text.isEmpty())
        app.press(Qt::Key_Delete);
    else
        app.type(text);
    return true;
}

// Save as Smart List… in the builder: name it and save. Returns the new
// list's id, or 0.
inline qint64 saveFromBuilder(App &app, const QString &name) {
    if (!pressBuilder("saveButton"))
        return 0;
    QQuickItem *input = nullptr;
    if (!waitUntil([&] { return (input = findItem("nameInput")) != nullptr && input->property("activeFocus").toBool(); }))
        return 0;
    app.type(name);
    if (!pressBuilder("nameSave"))
        return 0;
    qint64 id = 0;
    waitUntil([&] {
        id = 0;
        for (const TaskList &l : app.store().lists())
            if (!l.deleted && l.kind == ListKind::Smart && l.name == name)
                id = l.id;
        return id != 0 && !builderOpen();
    });
    return id;
}

// ------------------------------------------------------------- smart lists

inline QList<TaskList> smartLists(App &app) {
    QList<TaskList> out;
    for (const TaskList &l : app.store().lists())
        if (!l.deleted && l.kind == ListKind::Smart)
            out << l;
    std::sort(out.begin(), out.end(), [](const TaskList &a, const TaskList &b) { return a.order < b.order; });
    return out;
}

inline qint64 smartNamed(App &app, const QString &name) {
    for (const TaskList &l : smartLists(app))
        if (l.name == name)
            return l.id;
    return 0;
}

// Types `query` into the search bar and presses "Save as Smart List". The
// list is named after the query and opens; returns its id (0 on failure).
inline qint64 saveSearchAsSmart(App &app, const QString &query, bool alreadyTyped = false) {
    if (!alreadyTyped && !typeSearch(app, query))
        return 0;
    QQuickItem *save = nullptr;
    if (!waitUntil([&] { return (save = findItem("saveSearchAsSmart")) != nullptr; }))
        return 0;
    clickSteady(save);
    qint64 id = 0;
    waitUntil([&] {
        id = smartNamed(app, query);
        return id != 0 && app.backend().sourceKey() == lk(id);
    });
    return id;
}

// The Smart Lists section of the sidebar, top to bottom, as the names drawn.
inline QStringList smartRowNames(App &app) {
    QSet<QString> smart;
    for (const QVariantMap &row : app.backend().sidebar()->rows())
        if (row.value("kind").toString() == "smart")
            smart.insert(row.value("sourceKey").toString());
    QStringList out;
    for (const QString &key : sidebarOrder())
        if (smart.contains(key))
            out << shownName(findItem("sidebarRow:" + key));
    return out;
}

}  // namespace e2e
