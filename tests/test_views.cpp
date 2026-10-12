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
};

REGISTER_TEST(ViewsTest)
#include "test_views.moc"
