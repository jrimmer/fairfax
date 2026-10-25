#include <QtTest>
#include <functional>
#include <memory>

#include "store.h"
#include "testregistry.h"
#include "views.h"

namespace {

QDateTime at(int y, int m, int d, int hh = 10, int mm = 30) { return QDateTime(QDate(y, m, d), QTime(hh, mm)); }
const QDateTime Now = at(2026, 7, 15);  // Wednesday
const QDate Today(2026, 7, 15);

QDate day(int offset) { return Today.addDays(offset); }

}  // namespace

class ViewsTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Store> m_store;
    ScheduleSettings m_settings;
    double m_order = 0;

    qint64 addList(const QString &name, std::function<void(TaskList &)> edit = {}) {
        TaskList l;
        l.id = m_store->nextId("lists");
        l.name = name;
        l.order = ++m_order;
        if (edit) edit(l);
        m_store->begin("list");
        m_store->putList(l);
        m_store->commit();
        return l.id;
    }

    qint64 addTask(qint64 listId, const QString &title, std::function<void(Task &)> edit = {}) {
        Task t;
        t.id = m_store->nextId("tasks");
        t.listId = listId;
        t.title = title;
        t.order = ++m_order;
        t.created = Now.addDays(-30);
        if (edit) edit(t);
        m_store->begin("task");
        m_store->putTask(t);
        m_store->commit();
        return t.id;
    }

    qint64 addTag(const QString &name, bool paused) {
        Tag t;
        t.id = m_store->nextId("tags");
        t.name = name;
        t.paused = paused;
        m_store->begin("tag");
        m_store->putTag(t);
        m_store->commit();
        return t.id;
    }

    QVector<Section> build(const Source &src, ViewOptions o = {}, SortSpec s = {}) {
        return ViewBuilder(*m_store, Now, m_settings).build(src, s, o);
    }

    static QStringList keys(const QVector<Section> &sections) {
        QStringList k;
        for (const auto &s : sections) k << s.key;
        return k;
    }

    QStringList titles(const QVector<Section> &sections) {
        QStringList t;
        for (const auto &s : sections)
            for (const auto &r : s.rows) t << m_store->task(r.taskId).title;
        return t;
    }

    void storeList(const TaskList &l) {
        m_store->begin("edit");
        m_store->putList(l);
        m_store->commit();
    }

    static QStringList sorted(QStringList l) {
        l.sort();
        return l;
    }

    static Source list(qint64 id) { return Source{Source::List, id}; }

private slots:
    void init() {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_store = std::make_unique<Store>();
        QVERIFY(m_store->open(m_dir->filePath("views.db")));
        m_settings = ScheduleSettings{};
        m_order = 0;
    }

    void cleanup() {
        m_store.reset();
        m_dir.reset();
    }

    // ---- focus lists ----

    void allExcludesListsHiddenFromAll() {
        const auto shown = addList("Shown");
        const auto hidden = addList("Hidden", [](TaskList &l) { l.showInAll = false; });
        addTask(shown, "a");
        addTask(hidden, "b");
        const auto done = addTask(shown, "c", [](Task &t) { t.completed = true; t.completedAt = Now.addSecs(-60); });
        Q_UNUSED(done);
        QCOMPARE(titles(build(Source{Source::All})), QStringList{"a"});
        // The list itself still shows its tasks.
        QCOMPARE(titles(build(list(hidden))), QStringList{"b"});
    }

    void todayExcludesListsHiddenFromToday() {
        const auto shown = addList("Shown");
        const auto hidden = addList("Hidden", [](TaskList &l) { l.showInToday = false; });
        addTask(shown, "a", [](Task &t) { t.dueDate = Today; });
        addTask(hidden, "b", [](Task &t) { t.dueDate = Today; });
        QCOMPARE(titles(build(Source{Source::Today})), QStringList{"a"});
    }

    void todayStarredFromExcludedListNeedsSetting() {
        const auto hidden = addList("Hidden", [](TaskList &l) { l.showInToday = false; });
        addTask(hidden, "star", [](Task &t) { t.starred = true; });
        QVERIFY(titles(build(Source{Source::Today})).isEmpty());
        m_settings.todayIncludesStarred = true;
        QCOMPARE(titles(build(Source{Source::Today})), QStringList{"star"});
    }

    void todayShowsOverdueAndStillWorking() {
        const auto l = addList("L");
        addTask(l, "overdue", [](Task &t) { t.dueDate = day(-3); });
        addTask(l, "started", [](Task &t) { t.startDate = day(-2); });
        addTask(l, "none");
        addTask(l, "later", [](Task &t) { t.dueDate = day(3); });
        const auto got = titles(build(Source{Source::Today}));
        QCOMPARE(got.size(), 2);
        QVERIFY(got.contains("overdue") && got.contains("started"));
    }

    void starredFocus() {
        const auto l = addList("L");
        addTask(l, "s", [](Task &t) { t.starred = true; });
        addTask(l, "n");
        addTask(l, "done", [](Task &t) { t.starred = true; t.completed = true; t.completedAt = Now; });
        QCOMPARE(titles(build(Source{Source::Starred})), QStringList{"s"});
    }

    void scheduledFocusShowsFutureStarts() {
        const auto l = addList("L");
        addTask(l, "future", [](Task &t) { t.startDate = day(5); });
        addTask(l, "laterToday", [](Task &t) { t.startDate = Today; t.startTime = 20 * 60; });
        addTask(l, "now", [](Task &t) { t.startDate = Today; });
        addTask(l, "plain");
        const auto got = titles(build(Source{Source::Scheduled}));
        QCOMPARE(got.size(), 2);
        QVERIFY(got.contains("future") && got.contains("laterToday"));
        // The regular views hide them.
        QVERIFY(!titles(build(Source{Source::All})).contains("future"));
    }

    void doneFocusShowsCompleted() {
        const auto l = addList("L");
        const auto skip = addList("Skip", [](TaskList &x) { x.showInDone = false; });
        addTask(l, "open");
        addTask(l, "done", [](Task &t) { t.completed = true; t.completedAt = Now.addDays(-10); });
        addTask(skip, "skipped", [](Task &t) { t.completed = true; t.completedAt = Now; });
        // Done ignores the completed window.
        QCOMPARE(titles(build(Source{Source::Done})), QStringList{"done"});
    }

    // An abandoned task is closed like a completed one: it leaves the lists and
    // focus lists under the completed rule, shows in Done, and is found by
    // `type: abandoned` there.
    void abandonedTasksFollowTheCompletedRuleAndShowInDone() {
        const auto l = addList("L");
        addTask(l, "open");
        addTask(l, "done", [](Task &t) { t.completed = true; t.completedAt = Now.addSecs(-3600); });
        addTask(l, "dropped", [](Task &t) { t.completed = true; t.abandoned = true; t.completedAt = Now.addSecs(-3600); });
        addTask(l, "dropped long ago", [](Task &t) { t.completed = true; t.abandoned = true; t.completedAt = Now.addDays(-10); });

        // The default window is a day: the recent ones are listed, the old one is not.
        QCOMPARE(sorted(titles(build(list(l)))), (QStringList{"done", "dropped", "open"}));
        // "Never" hides both kinds.
        ViewOptions never;
        never.completedWindowMinutes = 0;
        QCOMPARE(titles(build(list(l), never)), QStringList{"open"});
        // All and the other focus lists leave closed tasks out.
        QCOMPARE(titles(build(Source{Source::All})), QStringList{"open"});
        // Done holds both, whatever the window says.
        QCOMPARE(sorted(titles(build(Source{Source::Done}, never))), (QStringList{"done", "dropped", "dropped long ago"}));
        // And can be narrowed to either kind by search.
        ViewOptions abandoned;
        abandoned.search = "type: abandoned";
        QCOMPARE(sorted(titles(build(Source{Source::Done}, abandoned))), (QStringList{"dropped", "dropped long ago"}));
        ViewOptions completed;
        completed.search = "type: completed";
        QCOMPARE(titles(build(Source{Source::Done}, completed)), QStringList{"done"});
    }

    // ---- scheduled / paused ----

    void scheduledHiddenUnlessShowScheduled() {
        const auto l = addList("L");
        addTask(l, "future", [](Task &t) { t.startDate = day(4); });
        addTask(l, "now");
        QCOMPARE(titles(build(list(l))), QStringList{"now"});
        ViewOptions o;
        o.showScheduled = true;
        QCOMPARE(titles(build(list(l), o)).size(), 2);
    }

    void pausedTagHidesUnlessShowPaused() {
        const auto l = addList("L");
        const auto paused = addTag("someday", true);
        const auto live = addTag("work", false);
        addTask(l, "p", [&](Task &t) { t.tagIds = {paused}; });
        addTask(l, "w", [&](Task &t) { t.tagIds = {live}; });
        QCOMPARE(titles(build(list(l))), QStringList{"w"});
        QCOMPARE(titles(build(Source{Source::All})), QStringList{"w"});
        ViewOptions o;
        o.showPaused = true;
        QCOMPARE(titles(build(list(l), o)).size(), 2);
    }

    void pausedProjectHidesChildren() {
        const auto l = addList("L");
        const auto paused = addTag("someday", true);
        const auto proj = addTask(l, "proj", [&](Task &t) { t.type = TaskType::Project; t.tagIds = {paused}; });
        addTask(l, "child", [&](Task &t) { t.parentId = proj; });
        QVERIFY(titles(build(Source{Source::All})).isEmpty());
        QVERIFY(titles(build(list(l))).isEmpty());
        ViewOptions o;
        o.showPaused = true;
        QCOMPARE(titles(build(Source{Source::All}, o)).size(), 2);
    }

    // ---- checklists and projects ----

    void checklistItemsNeverSurfaceInFocus() {
        const auto l = addList("L");
        const auto cl = addTask(l, "packing", [](Task &t) { t.type = TaskType::Checklist; t.dueDate = Today; });
        addTask(l, "passport", [&](Task &t) { t.parentId = cl; t.dueDate = Today; });
        addTask(l, "charger", [&](Task &t) { t.parentId = cl; t.starred = true; });
        // Items only ever appear nested under their checklist (depth 1), never as top-level rows.
        for (int kind : {Source::All, Source::Today, Source::Starred}) {
            QStringList top;
            for (const auto &s : build(Source{kind}))
                for (const auto &r : s.rows)
                    if (r.depth == 0) top << m_store->task(r.taskId).title;
            QCOMPARE(top, kind == Source::Starred ? QStringList{} : QStringList{"packing"});
        }
    }

    void projectChildrenSurfaceWithBreadcrumb() {
        const auto l = addList("L");
        const auto proj = addTask(l, "proj", [](Task &t) { t.type = TaskType::Project; });
        const auto child = addTask(l, "child", [&](Task &t) { t.parentId = proj; t.dueDate = Today; });
        const auto sections = build(Source{Source::Today});
        QCOMPARE(titles(sections), QStringList{"child"});
        QCOMPARE(sections[0].rows[0].taskId, child);
        QVERIFY(sections[0].rows[0].breadcrumb);
        QCOMPARE(sections[0].rows[0].depth, 0);
        // In All both the project and its child appear; only the child has a breadcrumb.
        const auto all = build(Source{Source::All});
        QCOMPARE(titles(all).size(), 2);
        for (const auto &s : all)
            for (const auto &r : s.rows)
                QCOMPARE(r.breadcrumb, r.taskId == child);
    }

    // ---- sectioning ----

    void dueSectionOrderAscending() {
        const auto l = addList("L");
        addTask(l, "none");
        addTask(l, "later", [](Task &t) { t.dueDate = day(5); });
        addTask(l, "dayafter", [](Task &t) { t.dueDate = day(2); });
        addTask(l, "tomorrow", [](Task &t) { t.dueDate = day(1); });
        addTask(l, "today", [](Task &t) { t.dueDate = day(0); });
        addTask(l, "overdue", [](Task &t) { t.dueDate = day(-4); });
        const auto s = build(list(l));
        QCOMPARE(keys(s), (QStringList{"overdue", "d:2026-07-15", "d:2026-07-16", "d:2026-07-17", "d:2026-07-20", "nodue"}));
        QCOMPARE(s[0].title, QString("Overdue"));
        QCOMPARE(s[1].title, QString("Today"));
        QCOMPARE(s[2].title, QString("Tomorrow"));
        QCOMPARE(s[3].title, QString("Day After Tomorrow"));
        QCOMPARE(s[5].title, QString("No Due Date"));
        QCOMPARE(s[0].tone, QString("overdue"));
    }

    void dueSectionOrderDescendingPinsOverdueFirstNoDueLast() {
        const auto l = addList("L");
        addTask(l, "none");
        addTask(l, "later", [](Task &t) { t.dueDate = day(5); });
        addTask(l, "tomorrow", [](Task &t) { t.dueDate = day(1); });
        addTask(l, "today", [](Task &t) { t.dueDate = day(0); });
        addTask(l, "overdue", [](Task &t) { t.dueDate = day(-4); });
        SortSpec spec;
        spec.ascending = false;
        const auto s = build(list(l), {}, spec);
        QCOMPARE(keys(s), (QStringList{"overdue", "d:2026-07-20", "d:2026-07-16", "d:2026-07-15", "nodue"}));
    }

    void inProgressSectionInToday() {
        const auto l = addList("L");
        addTask(l, "kids", [](Task &t) { t.dueDate = Today; t.dueTime = 15 * 60; t.duration = 8 * 60; });  // from 7:00
        addTask(l, "plain", [](Task &t) { t.dueDate = Today; });
        const auto s = build(Source{Source::Today});
        QCOMPARE(keys(s).first(), QString("progress"));
        QCOMPARE(s[0].title, QString("In Progress"));
        QCOMPARE(titles(s), (QStringList{"kids", "plain"}));
    }

    void prioritySections() {
        const auto l = addList("L");
        addTask(l, "n");
        addTask(l, "lo", [](Task &t) { t.priority = Priority::Low; });
        addTask(l, "hi", [](Task &t) { t.priority = Priority::High; });
        addTask(l, "me", [](Task &t) { t.priority = Priority::Medium; });
        SortSpec spec;
        spec.field = SortField::Priority;
        const auto s = build(list(l), {}, spec);
        QStringList names;
        for (const auto &sec : s) names << sec.title;
        QCOMPARE(names, (QStringList{"High", "Medium", "Low", "None"}));
    }

    void notSeparatedGivesOneSection() {
        const auto l = addList("Chores");
        addTask(l, "a", [](Task &t) { t.dueDate = day(1); });
        addTask(l, "b");
        SortSpec spec;
        spec.separated = false;
        const auto s = build(list(l), {}, spec);
        QCOMPARE(s.size(), 1);
        QCOMPARE(s[0].title, QString("All Tasks in Chores"));
    }

    void sectionCountsAndDurationTotals() {
        const auto l = addList("L");
        addTask(l, "a", [](Task &t) { t.dueDate = day(1); t.duration = 30; });
        addTask(l, "b", [](Task &t) { t.dueDate = day(1); t.duration = 60; });
        addTask(l, "c", [](Task &t) { t.dueDate = day(1); t.completed = true; t.completedAt = Now; t.duration = 500; });
        const auto s = build(list(l));
        QCOMPARE(s.size(), 1);
        QCOMPARE(s[0].openCount, 2);
        QCOMPARE(s[0].durationTotal, 90);
        QCOMPARE(titles(s).last(), QString("c"));  // completed settles to the bottom
    }

    // ---- completed visibility ----

    void completedHiddenAfterWindow() {
        const auto l = addList("L");
        addTask(l, "recent", [](Task &t) { t.completed = true; t.completedAt = Now.addSecs(-30 * 60); });
        addTask(l, "old", [](Task &t) { t.completed = true; t.completedAt = Now.addSecs(-3 * 3600); });
        ViewOptions o;
        o.completedWindowMinutes = 60;
        QCOMPARE(titles(build(list(l), o)), QStringList{"recent"});
        o.completedWindowMinutes = 24 * 60;
        QCOMPARE(titles(build(list(l), o)).size(), 2);
        o.completedWindowMinutes = -1;
        QCOMPARE(titles(build(list(l), o)).size(), 2);
        o.completedWindowMinutes = 0;
        QVERIFY(titles(build(list(l), o)).isEmpty());
        o.completedWindowMinutes = 24 * 60;
        o.hideCompleted = true;
        QVERIFY(titles(build(list(l), o)).isEmpty());
    }

    void completedKeptWhenKeepVisible() {
        const auto l = addList("L");
        const auto id = addTask(l, "old", [](Task &t) { t.completed = true; t.completedAt = Now.addDays(-5); });
        ViewOptions o;
        o.completedWindowMinutes = 60;
        QVERIFY(titles(build(list(l), o)).isEmpty());
        o.keepVisible.insert(id);
        QCOMPARE(titles(build(list(l), o)), QStringList{"old"});
    }

    void completedNeverInToday() {
        const auto l = addList("L");
        const auto id = addTask(l, "done", [](Task &t) { t.completed = true; t.completedAt = Now; t.dueDate = Today; });
        ViewOptions o;
        o.completedWindowMinutes = -1;
        QVERIFY(titles(build(Source{Source::Today}, o)).isEmpty());
        // Just completed from Today: it stays until the view changes.
        o.keepVisible.insert(id);
        QCOMPARE(titles(build(Source{Source::Today}, o)), QStringList{"done"});
    }

    // ---- nesting ----

    void childrenNestUnderChecklist() {
        const auto l = addList("L");
        const auto cl = addTask(l, "list", [](Task &t) { t.type = TaskType::Checklist; });
        const auto a = addTask(l, "a", [&](Task &t) { t.parentId = cl; });
        const auto b = addTask(l, "b", [&](Task &t) { t.parentId = cl; });
        const auto s = build(list(l));
        QCOMPARE(s.size(), 1);
        QCOMPARE(s[0].rows.size(), 3);
        QCOMPARE(s[0].rows[0].taskId, cl);
        QCOMPARE(s[0].rows[0].depth, 0);
        QCOMPARE(s[0].rows[0].visibleChildren, 2);
        QCOMPARE(s[0].rows[1].taskId, a);
        QCOMPARE(s[0].rows[1].depth, 1);
        QCOMPARE(s[0].rows[2].taskId, b);
        QCOMPARE(s[0].rows[2].depth, 1);
        QCOMPARE(s[0].openCount, 1);  // children don't count as top-level tasks
    }

    void collapsedParentHidesChildren() {
        const auto l = addList("L");
        const auto cl = addTask(l, "list", [](Task &t) { t.type = TaskType::Checklist; t.collapsed = true; });
        addTask(l, "a", [&](Task &t) { t.parentId = cl; });
        const auto s = build(list(l));
        QCOMPARE(s[0].rows.size(), 1);
        QCOMPARE(s[0].rows[0].visibleChildren, 1);
    }

    void projectViewListsChildrenEvenWhenCollapsed() {
        const auto l = addList("L");
        const auto proj = addTask(l, "proj", [](Task &t) { t.type = TaskType::Project; t.collapsed = true; });
        addTask(l, "a", [&](Task &t) { t.parentId = proj; });
        const auto s = build(Source{Source::Project, proj});
        QCOMPARE(s[0].rows.size(), 2);
        QCOMPARE(s[0].rows[1].depth, 1);
    }

    void childrenOrderedManually() {
        const auto l = addList("L");
        const auto cl = addTask(l, "list", [](Task &t) { t.type = TaskType::Checklist; });
        addTask(l, "second", [&](Task &t) { t.parentId = cl; t.order = 20; });
        addTask(l, "first", [&](Task &t) { t.parentId = cl; t.order = 10; });
        QCOMPARE(titles(build(list(l))), (QStringList{"list", "first", "second"}));
    }

    // ---- sidebar counts ----

    void countsForListExclusions() {
        const auto l = addList("L");
        const auto paused = addTag("p", true);
        const auto proj = addTask(l, "proj", [](Task &t) { t.type = TaskType::Project; });
        addTask(l, "open");
        addTask(l, "overdue", [](Task &t) { t.dueDate = day(-1); });
        addTask(l, "sub", [&](Task &t) { t.parentId = proj; t.dueDate = day(-1); });
        addTask(l, "scheduled", [](Task &t) { t.startDate = day(3); });
        addTask(l, "paused", [&](Task &t) { t.tagIds = {paused}; });
        addTask(l, "done", [](Task &t) { t.completed = true; t.completedAt = Now; });
        const auto c = ViewBuilder(*m_store, Now, m_settings).countsForList(l);
        QCOMPARE(c.undone, 3);  // proj, open, overdue
        QCOMPARE(c.overdue, 1);
        QVERIFY(!c.dueToday);
    }

    void countsDueTodayFlag() {
        const auto l = addList("L");
        addTask(l, "today", [](Task &t) { t.dueDate = Today; });
        const auto c = ViewBuilder(*m_store, Now, m_settings).countsForList(l);
        QCOMPARE(c.undone, 1);
        QCOMPARE(c.overdue, 0);
        QVERIFY(c.dueToday);
    }

    void countsForFocus() {
        const auto l = addList("L");
        addTask(l, "today", [](Task &t) { t.dueDate = Today; });
        addTask(l, "overdue", [](Task &t) { t.dueDate = day(-2); });
        addTask(l, "future", [](Task &t) { t.startDate = day(3); });
        const ViewBuilder vb(*m_store, Now, m_settings);
        QCOMPARE(vb.countsForFocus(Source::Today).undone, 2);
        QCOMPARE(vb.countsForFocus(Source::Today).overdue, 1);
        QCOMPARE(vb.countsForFocus(Source::All).undone, 2);
        QCOMPARE(vb.countsForFocus(Source::Scheduled).undone, 1);
        QCOMPARE(vb.countsForFocus(Source::Done).undone, 0);
    }

    // ---- queries, scope and completed tasks ----

    void completedQueryReturnsCompletedTasks() {
        const auto l = addList("L");
        addTask(l, "open");
        addTask(l, "old", [](Task &t) { t.completed = true; t.completedAt = Now.addDays(-3); });
        addTask(l, "fresh", [](Task &t) { t.completed = true; t.completedAt = Now.addSecs(-3600); });
        ViewOptions o;
        o.completedWindowMinutes = 0;
        // The window would hide every completed task; the query overrides it.
        o.search = "completed: 7d";
        QCOMPARE(sorted(titles(build(list(l), o))), (QStringList{"fresh", "old"}));
        o.search = "type: donetoday";
        QCOMPARE(titles(build(Source{Source::Today}, o)), QStringList{"fresh"});
        o.search = "completed: 1d";
        QCOMPARE(titles(build(Source{Source::All}, o)), QStringList{"fresh"});
    }

    void negatedCompletedDoesNotRevealCompleted() {
        const auto l = addList("L");
        addTask(l, "open");
        addTask(l, "done", [](Task &t) { t.completed = true; t.completedAt = Now.addSecs(-60); });
        ViewOptions o;
        o.completedWindowMinutes = 0;
        o.search = "type: !donetoday";
        QCOMPARE(titles(build(list(l), o)), QStringList{"open"});
    }

    void listsTokenSearchesEveryList() {
        const auto work = addList("Work");
        const auto home = addList("Home");
        addTask(work, "report");
        addTask(home, "laundry");
        ViewOptions o;
        o.search = "lists: Work";
        // Whatever is on screen, the search leaves it.
        QCOMPARE(titles(build(list(home), o)), QStringList{"report"});
        QCOMPARE(titles(build(Source{Source::Today}, o)), QStringList{"report"});
        o.search = "report";
        QCOMPARE(titles(build(list(home), o)), QStringList{});
    }

    void groupsTokenSearchesEveryList() {
        ListGroup g;
        g.id = m_store->nextId("list_groups");
        g.name = "Office";
        m_store->begin("group");
        m_store->putGroup(g);
        m_store->commit();
        const auto work = addList("Work", [&](TaskList &l) { l.groupId = g.id; });
        const auto home = addList("Home");
        addTask(work, "report");
        addTask(home, "laundry");
        ViewOptions o;
        o.search = "groups: Office";
        QCOMPARE(titles(build(list(home), o)), QStringList{"report"});
    }

    void locationTokensMatchLocations() {
        Location place;
        place.id = m_store->nextId("locations");
        place.name = "Shop";
        m_store->begin("place");
        m_store->putLocation(place);
        m_store->commit();
        const auto l = addList("L");
        addTask(l, "buy", [&](Task &t) { t.locationIds = {place.id}; });
        addTask(l, "stay");
        ViewOptions o;
        o.search = "locations: Shop";
        QCOMPARE(titles(build(list(l), o)), QStringList{"buy"});
        o.search = "type: anylocation";
        QCOMPARE(titles(build(list(l), o)), QStringList{"buy"});
        o.search = "type: nolocation";
        QCOMPARE(titles(build(list(l), o)), QStringList{"stay"});
    }

    void alarmAndAttachmentTypes() {
        const auto l = addList("L");
        const auto ringing = addTask(l, "ringing");
        const auto pic = addTask(l, "pic");
        const auto voice = addTask(l, "voice");
        Alarm a;
        a.id = m_store->nextId("alarms");
        a.taskId = ringing;
        m_store->begin("alarm");
        m_store->putAlarm(a);
        Attachment image;
        image.id = m_store->nextId("attachments");
        image.taskId = pic;
        image.kind = Attachment::Image;
        m_store->putAttachment(image);
        Attachment audio;
        audio.id = m_store->nextId("attachments");
        audio.taskId = voice;
        audio.kind = Attachment::Audio;
        m_store->putAttachment(audio);
        m_store->commit();
        ViewOptions o;
        o.search = "type: alarms";
        QCOMPARE(titles(build(list(l), o)), QStringList{"ringing"});
        o.search = "type: pict";
        QCOMPARE(titles(build(list(l), o)), QStringList{"pic"});
        o.search = "type: audio";
        QCOMPARE(titles(build(list(l), o)), QStringList{"voice"});
    }

    void smartListHonoursCompletedAndScheduledSwitches() {
        const auto l = addList("L");
        addTask(l, "open", [](Task &t) { t.priority = Priority::High; });
        addTask(l, "later", [](Task &t) { t.priority = Priority::High; t.startDate = day(5); });
        addTask(l, "done", [](Task &t) {
            t.priority = Priority::High;
            t.completed = true;
            t.completedAt = Now.addDays(-5);
        });
        const auto smart = addList("Hot", [](TaskList &sl) { sl.kind = ListKind::Smart; sl.smartQuery = "type: hiprio"; });
        QCOMPARE(titles(build(list(smart))), QStringList{"open"});

        TaskList sl = m_store->list(smart);
        sl.includeScheduled = true;
        storeList(sl);
        QCOMPARE(sorted(titles(build(list(smart)))), (QStringList{"later", "open"}));

        sl.completedMode = 1;
        storeList(sl);
        QCOMPARE(sorted(titles(build(list(smart)))), (QStringList{"done", "later", "open"}));

        sl.completedMode = 2;
        storeList(sl);
        QCOMPARE(sorted(titles(build(list(smart)))), (QStringList{"later", "open"}));
    }

    // A search that names scheduled or paused tasks finds them, though they
    // are hidden by default: otherwise `type: scheduled` could match nothing.
    void queryNamingScheduledOrPausedShowsThem() {
        const auto l = addList("L");
        const auto onHold = addTag("onhold", true);
        addTask(l, "open");
        addTask(l, "later", [](Task &t) { t.startDate = day(5); });
        addTask(l, "held", [onHold](Task &t) { t.tagIds << onHold; });
        ViewOptions o;
        o.search = "type: scheduled";
        QCOMPARE(titles(build(Source{Source::All}, o)), QStringList{"later"});
        o.search = "type: paused";
        QCOMPARE(titles(build(Source{Source::All}, o)), QStringList{"held"});
        // Excluding them does not ask for them.
        o.search = "type: !scheduled";
        QCOMPARE(titles(build(Source{Source::All}, o)), QStringList{"open"});
        // A Smart List built on the preset behaves the same.
        const auto smart = addList("Soon", [](TaskList &sl) { sl.kind = ListKind::Smart; sl.smartQuery = "type: scheduled"; });
        QCOMPARE(titles(build(list(smart))), QStringList{"later"});
    }

    void smartListDateRange() {
        const auto l = addList("L");
        addTask(l, "soon", [](Task &t) { t.dueDate = day(2); });
        addTask(l, "far", [](Task &t) { t.dueDate = day(40); });
        addTask(l, "undated");
        const auto smart = addList("Range", [](TaskList &sl) { sl.kind = ListKind::Smart; sl.smartRange = "next:1:w"; });
        QCOMPARE(titles(build(list(smart))), QStringList{"soon"});
        TaskList sl = m_store->list(smart);
        sl.smartRange = "nodue";
        storeList(sl);
        QCOMPARE(titles(build(list(smart))), QStringList{"undated"});
        sl.smartRange = "fixed:" + day(30).toString(Qt::ISODate) + ":";
        storeList(sl);
        QCOMPARE(titles(build(list(smart))), QStringList{"far"});
    }

    void listCompletedModeOverridesSetting() {
        const auto l = addList("L");
        addTask(l, "open");
        addTask(l, "done", [](Task &t) { t.completed = true; t.completedAt = Now.addDays(-9); });
        ViewOptions o;
        o.completedWindowMinutes = 24 * 60;
        QCOMPARE(titles(build(list(l), o)), QStringList{"open"});
        TaskList ll = m_store->list(l);
        ll.completedMode = 1;
        storeList(ll);
        QCOMPARE(sorted(titles(build(list(l), o))), (QStringList{"done", "open"}));
        ll.completedMode = 2;
        storeList(ll);
        o.completedWindowMinutes = -1;
        QCOMPARE(titles(build(list(l), o)), QStringList{"open"});
    }

    void inheritedSearchFiltersProjectChildren() {
        const auto l = addList("L");
        const auto proj = addTask(l, "proj", [](Task &t) { t.type = TaskType::Project; });
        addTask(l, "urgent", [&](Task &t) { t.parentId = proj; t.priority = Priority::High; });
        addTask(l, "casual", [&](Task &t) { t.parentId = proj; });
        QCOMPARE(titles(build(Source{Source::Project, proj})).size(), 3);
        ViewOptions o;
        o.inheritSearch = true;
        o.inheritedRule = "type: hiprio";
        QCOMPARE(sorted(titles(build(Source{Source::Project, proj}, o))), (QStringList{"proj", "urgent"}));
        // The typed search applies too, without hiding the project itself.
        o.inheritedRule.clear();
        o.search = "casual";
        QCOMPARE(sorted(titles(build(Source{Source::Project, proj}, o))), (QStringList{"casual", "proj"}));
        // Off, the project shows everything.
        o.inheritSearch = false;
        o.search.clear();
        QCOMPARE(titles(build(Source{Source::Project, proj}, o)).size(), 3);
    }

    // ---- sorting ----

    void sortByNote() {
        const auto l = addList("L");
        addTask(l, "none");
        addTask(l, "b", [](Task &t) { t.notes = "banana"; });
        addTask(l, "a", [](Task &t) { t.notes = "apple"; });
        SortSpec s;
        s.field = SortField::Note;
        s.separated = false;
        QCOMPARE(titles(build(list(l), {}, s)), (QStringList{"a", "b", "none"}));
        s.ascending = false;
        QCOMPARE(titles(build(list(l), {}, s)), (QStringList{"b", "a", "none"}));
    }

    void sortByLocation() {
        Location attic, zoo;
        attic.id = m_store->nextId("locations");
        attic.name = "Attic";
        zoo.id = attic.id + 1;
        zoo.name = "Zoo";
        m_store->begin("places");
        m_store->putLocation(attic);
        m_store->putLocation(zoo);
        m_store->commit();
        const auto l = addList("L");
        addTask(l, "nowhere");
        addTask(l, "zoo", [&](Task &t) { t.locationIds = {zoo.id}; });
        addTask(l, "attic", [&](Task &t) { t.locationIds = {attic.id}; });
        SortSpec s;
        s.field = SortField::Location;
        s.separated = false;
        QCOMPARE(titles(build(list(l), {}, s)), (QStringList{"attic", "zoo", "nowhere"}));
        s.separated = true;
        QCOMPARE(keys(build(list(l), {}, s)), (QStringList{"loc:attic", "loc:zoo", "noloc"}));
    }

    // ---- sorting by tag ----

    void sortByTagPlacesATaskUnderEveryTag() {
        const auto l = addList("L");
        const auto work = addTag("work", false);
        const auto home = addTag("home", false);
        addTask(l, "both", [&](Task &t) { t.tagIds = {work, home}; });
        addTask(l, "only home", [&](Task &t) { t.tagIds = {home}; });
        addTask(l, "bare");
        const auto sections = build(list(l), {}, SortSpec{SortField::Tag, true, true});
        // Alphabetical: home, work, then the untagged.
        QCOMPARE(sections.size(), 3);
        QCOMPARE(sections.at(0).title, QString("home"));
        QCOMPARE(sections.at(0).rows.size(), 2);
        QCOMPARE(sections.at(1).title, QString("work"));
        QCOMPARE(sections.at(1).rows.size(), 1);
        QCOMPARE(sections.at(2).title, QString("No Tags"));
    }

    void sortByTagFollowsManualTagOrder() {
        const auto l = addList("L");
        const auto a = addTag("alpha", false);
        const auto z = addTag("zulu", false);
        addTask(l, "x", [&](Task &t) { t.tagIds = {a}; });
        addTask(l, "y", [&](Task &t) { t.tagIds = {z}; });
        // Putting zulu first by hand puts its section first.
        Tag tz = m_store->tag(z), ta = m_store->tag(a);
        tz.order = 1;
        ta.order = 2;
        m_store->begin("order");
        m_store->putTag(tz);
        m_store->putTag(ta);
        m_store->commit();
        m_store->setSetting("tags.manual", true);
        const auto sections = build(list(l), {}, SortSpec{SortField::Tag, true, true});
        QCOMPARE(sections.at(0).title, QString("zulu"));
        QCOMPARE(sections.at(1).title, QString("alpha"));
    }

    void projectSortByTagUsesAllTagsInOrder() {
        const auto l = addList("L");
        const auto a = addTag("alpha", false);
        const auto b = addTag("bravo", false);
        const auto project = addTask(l, "P", [](Task &t) { t.type = TaskType::Project; });
        addTask(l, "has b", [&](Task &t) { t.parentId = project; t.tagIds = {b}; });
        addTask(l, "has both", [&](Task &t) { t.parentId = project; t.tagIds = {b, a}; });
        addTask(l, "none", [&](Task &t) { t.parentId = project; });
        m_store->setSetting(QStringLiteral("sort.project:%1.field").arg(project), SortField::Tag);
        const auto sections = build(list(l));
        QStringList seen;
        for (const auto &r : sections.at(0).rows)
            seen << m_store->task(r.taskId).title;
        QCOMPARE(seen, (QStringList{"P", "has both", "has b", "none"}));
    }

    void projectSortByNoteSendsEmptyNotesLast() {
        const auto l = addList("L");
        const auto project = addTask(l, "P", [](Task &t) { t.type = TaskType::Project; });
        addTask(l, "plain", [&](Task &t) { t.parentId = project; });
        addTask(l, "noted", [&](Task &t) { t.parentId = project; t.notes = "hello"; });
        m_store->setSetting(QStringLiteral("sort.project:%1.field").arg(project), SortField::Note);
        const auto sections = build(list(l));
        QStringList seen;
        for (const auto &r : sections.at(0).rows)
            seen << m_store->task(r.taskId).title;
        QCOMPARE(seen, (QStringList{"P", "noted", "plain"}));
    }

    void sortingByStartDateRevealsScheduled() {
        const auto l = addList("L");
        addTask(l, "visible");
        addTask(l, "later", [](Task &t) { t.startDate = day(5); });
        QCOMPARE(titles(build(list(l))), QStringList{"visible"});
        SortSpec byStart;
        byStart.field = SortField::StartDate;
        const auto shown = titles(build(list(l), {}, byStart));
        QCOMPARE(sorted(shown), (QStringList{"later", "visible"}));
        // Focus lists keep hiding them.
        QVERIFY(!titles(build(Source{Source::All}, {}, byStart)).contains("later"));
    }

    // ---- settings that change what the views show ----

    void subtasksListedSeparatelyOrUnderTheirProject() {
        const auto l = addList("L");
        const auto proj = addTask(l, "proj", [](Task &t) { t.type = TaskType::Project; t.dueDate = Today; });
        addTask(l, "child", [&](Task &t) { t.parentId = proj; t.dueDate = Today; });
        QCOMPARE(titles(build(Source{Source::Today})).size(), 2);
        ViewOptions o;
        o.subtasksSeparately = false;
        QCOMPARE(titles(build(Source{Source::Today}, o)), QStringList{"proj"});
        // A child whose project is not in the list still shows.
        const auto other = addTask(l, "away", [](Task &t) { t.type = TaskType::Project; });
        addTask(l, "kid", [&](Task &t) { t.parentId = other; t.dueDate = Today; });
        QCOMPARE(sorted(titles(build(Source{Source::Today}, o))), (QStringList{"kid", "proj"}));
    }

    void sortByListFollowsListOrderOrName() {
        const auto zed = addList("Zed");
        const auto alpha = addList("Alpha");
        addTask(zed, "in zed");
        addTask(alpha, "in alpha");
        SortSpec byList;
        byList.field = SortField::List;
        QCOMPARE(titles(build(Source{Source::All}, {}, byList)), (QStringList{"in zed", "in alpha"}));
        ViewOptions o;
        o.listOrder = false;
        QCOMPARE(titles(build(Source{Source::All}, o, byList)), (QStringList{"in alpha", "in zed"}));
    }

    void completedRuleAppliesToProjectChildrenOnlyWhenAsked() {
        const auto l = addList("L");
        const auto proj = addTask(l, "proj", [](Task &t) { t.type = TaskType::Project; });
        addTask(l, "old done", [&](Task &t) {
            t.parentId = proj; t.completed = true; t.completedAt = Now.addDays(-10);
        });
        QCOMPARE(titles(build(list(l))), QStringList{"proj"});
        ViewOptions o;
        o.completedRuleSubtasks = false;
        QCOMPARE(titles(build(list(l), o)), (QStringList{"proj", "old done"}));
    }

    void startedTasksHiddenUnlessDueTodayWhenAsked() {
        const auto l = addList("L");
        addTask(l, "started", [](Task &t) { t.startDate = day(-3); t.dueDate = day(5); });
        addTask(l, "started due today", [](Task &t) { t.startDate = day(-3); t.dueDate = Today; });
        addTask(l, "starts today", [](Task &t) { t.startDate = Today; });
        QCOMPARE(titles(build(Source{Source::All})).size(), 3);
        m_settings.hideStartedTasks = true;
        QCOMPARE(sorted(titles(build(Source{Source::All}))), (QStringList{"started due today", "starts today"}));
        QCOMPARE(sorted(titles(build(list(l)))), (QStringList{"started due today", "starts today"}));
        // The sidebar count agrees with the list.
        QCOMPARE(ViewBuilder(*m_store, Now, m_settings).countsForList(l).undone, 2);
    }

    void durationHeadingsSkipTotalsWhenSortedByDuration() {
        const auto l = addList("L");
        addTask(l, "a", [](Task &t) { t.dueDate = Today; t.duration = 90; });
        addTask(l, "b", [](Task &t) { t.dueDate = Today; t.duration = 30; });
        auto sections = build(Source{Source::All});
        QCOMPARE(sections.at(0).durationTotal, 120);
        SortSpec byDuration;
        byDuration.field = SortField::Duration;
        sections = build(Source{Source::All}, {}, byDuration);
        for (const auto &s : sections)
            QCOMPARE(s.durationTotal, 0);
    }

    void thisWeekFollowsTheWeekStart() {
        const auto l = addList("L");
        const auto sunday = addTask(l, "sunday", [](Task &t) { t.dueDate = QDate(2026, 7, 12); });
        Task t = m_store->task(sunday);
        m_settings.weekStart = Qt::Monday;
        QVERIFY(!ViewBuilder::matchesFocusCondition(t, "thisweek", Now, m_settings));
        m_settings.weekStart = Qt::Sunday;
        QVERIFY(ViewBuilder::matchesFocusCondition(t, "thisweek", Now, m_settings));
        QCOMPARE(Schedule::weekStartOf(Today, Qt::Saturday), QDate(2026, 7, 11));
        m_store->setSetting("general.weekStart", int(Qt::Sunday));
        QCOMPARE(ScheduleSettings::fromStore(*m_store).weekStart, int(Qt::Sunday));
    }

    void badgeCountsByMetric() {
        const auto l = addList("L");
        addTask(l, "late", [](Task &t) { t.dueDate = day(-2); });
        addTask(l, "now1", [](Task &t) { t.dueDate = Today; });
        addTask(l, "now2", [](Task &t) { t.dueDate = Today; });
        const ViewBuilder views(*m_store, Now, m_settings);
        QCOMPARE(views.badgeCount("overdue"), 1);
        QCOMPARE(views.badgeCount("today"), 2);
        QCOMPARE(views.badgeCount("both"), 3);
        QCOMPARE(views.badgeCount("none"), 0);
    }

    // ---- archived and hidden lists ----

    void archivedListsStillFeedSmartListsAndDone() {
        const auto live = addList("Live");
        const auto old = addList("Old", [](TaskList &l) { l.archived = true; });
        addTask(live, "open here");
        addTask(old, "open there");
        addTask(old, "finished there", [](Task &t) { t.completed = true; t.completedAt = Now.addSecs(-300); });
        const auto smart = addList("Everything", [](TaskList &l) { l.kind = ListKind::Smart; l.smartRange = "nodue"; l.completedMode = 2; });

        QCOMPARE(titles(build(Source{Source::All})), QStringList{"open here"});
        QCOMPARE(sorted(titles(build(list(smart)))), (QStringList{"open here", "open there"}));
        QCOMPARE(titles(build(Source{Source::Done})), QStringList{"finished there"});
        // Counts and the sidebar order still leave the archive out.
        QCOMPARE(ViewBuilder(*m_store, Now, m_settings).countsForFocus(Source::All).undone, 1);
        QCOMPARE(ViewBuilder(*m_store, Now, m_settings).orderedLists().size(), 1);
    }

    void hiddenListsVanishUntilRevealed() {
        const auto shown = addList("Shown");
        const auto secret = addList("Secret");
        addTask(shown, "visible");
        addTask(secret, "tucked away");
        m_store->setSetting("lists.hidden", m_store->list(secret).uid);

        ViewBuilder hidden(*m_store, Now, m_settings);
        QCOMPARE(titles(build(Source{Source::All})), QStringList{"visible"});
        QCOMPARE(hidden.orderedLists().size(), 1);
        QCOMPARE(hidden.countsForFocus(Source::All).undone, 1);
        ViewOptions search;
        search.search = "tucked";
        QVERIFY(titles(build(Source{Source::All}, search)).isEmpty());

        m_store->setSetting("lists.hiddenRevealed", true);
        QCOMPARE(sorted(titles(build(Source{Source::All}))), (QStringList{"tucked away", "visible"}));
        QCOMPARE(ViewBuilder(*m_store, Now, m_settings).orderedLists().size(), 2);
    }

    void customListColorsKeepPaletteIndexesApart() {
        QVERIFY(!ListColor::isCustom(12));
        const int c = ListColor::custom(0x336699);
        QVERIFY(ListColor::isCustom(c));
        QCOMPARE(ListColor::rgb(c), 0x336699u);
    }
};

REGISTER_TEST(ViewsTest)
#include "test_views.moc"
