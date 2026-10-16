// My Day's rules: the three sections, their exclusions and ordering, the
// suggestion reasons, the capacity line and the planning actions. All of it
// takes `now` explicitly, so a fixed Wednesday is used throughout and one test
// repeats the core under a US Pacific time zone.

#include <QtTest>
#include <memory>
#include <time.h>

#include "calendar.h"
#include "myday.h"
#include "store.h"
#include "testregistry.h"

namespace {

constexpr int H = 60;

// Wednesday 15 July 2026, 10:30, as test_schedule.cpp uses.
QDateTime at(int y, int m, int d, int hh = 10, int mm = 30) { return QDateTime(QDate(y, m, d), QTime(hh, mm)); }
const QDateTime Now = at(2026, 7, 15);
const QDate Today(2026, 7, 15);

// Pins the process time zone for the life of a test.
class Zone {
public:
    explicit Zone(const char *name) : m_had(qEnvironmentVariableIsSet("TZ")), m_old(qgetenv("TZ")) {
        qputenv("TZ", name);
        tzset();
    }
    ~Zone() {
        if (m_had) qputenv("TZ", m_old);
        else qunsetenv("TZ");
        tzset();
    }

private:
    bool m_had;
    QByteArray m_old;
};

class Fixture {
public:
    Fixture() {
        m_dir = std::make_unique<QTemporaryDir>();
        [&] { QVERIFY2(m_store.open(m_dir->filePath("myday.db"), &m_error), qPrintable(m_error)); }();
        TaskList home;
        home.id = m_store.nextId("lists");
        home.name = "Home";
        m_store.begin("seed", false);
        m_store.putList(home);
        m_store.commit();
        m_home = home.id;
    }

    qint64 add(const QString &title, const std::function<void(Task &)> &fn = {}) {
        Task t;
        t.id = m_store.nextId("tasks");
        t.listId = m_home;
        t.title = title;
        t.order = t.id;
        if (fn)
            fn(t);
        m_store.begin("seed", false);
        m_store.putTask(t);
        m_store.commit();
        return t.id;
    }

    // The ids in one section, in order.
    QList<qint64> ids(const QVector<MyDay::Entry> &entries) const {
        QList<qint64> out;
        for (const auto &e : entries)
            out << e.taskId;
        return out;
    }
    QString reason(qint64 id) const {
        const MyDay::Sections s = MyDay::sections(m_store, Now);
        for (const auto &e : s.overdue)
            if (e.taskId == id) return e.reason;
        for (const auto &e : s.today)
            if (e.taskId == id) return e.reason;
        for (const auto &e : s.suggestions)
            if (e.taskId == id) return e.reason;
        return {};
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    Store m_store;
    QString m_error;
    qint64 m_home = 0;
};

QByteArray wrap(const QString &events) {
    QString text = QStringLiteral("BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//test//EN\n") + events + QStringLiteral("END:VCALENDAR\n");
    return text.replace(QLatin1Char('\n'), QStringLiteral("\r\n")).toUtf8();
}

QString vevent(const QString &uid, const QString &summary, int startMinute, int endMinute) {
    const QDate d = Today;
    auto stamp = [&](int minute) {
        return QDateTime(d, QTime(0, 0).addSecs(qint64(minute) * 60)).toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
    };
    return QStringLiteral("BEGIN:VEVENT\nUID:%1\nSUMMARY:%2\nDTSTART:%3\nDTEND:%4\nEND:VEVENT\n")
        .arg(uid, summary, stamp(startMinute), stamp(endMinute));
}

}  // namespace

class MyDayTest : public QObject {
    Q_OBJECT

private slots:
    // ----------------------------------------------------------- sections

    void overdueGoesInItsOwnSectionOldestFirst() {
        Fixture f;
        const qint64 older = f.add("Older", [](Task &t) { t.dueDate = QDate(2026, 7, 10); });
        const qint64 newer = f.add("Newer", [](Task &t) { t.dueDate = QDate(2026, 7, 14); });
        const qint64 today = f.add("Today", [](Task &t) { t.dueDate = Today; });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QCOMPARE(f.ids(s.overdue), (QList<qint64>{older, newer}));
        QCOMPARE(f.reason(older), QString("5 days overdue"));
        QCOMPARE(f.reason(newer), QString("1 day overdue"));
        QVERIFY(f.ids(s.today).contains(today));
    }

    void overdueCountsDaysNotTime() {
        Fixture f;
        const qint64 id = f.add("Late", [](Task &t) { t.dueDate = QDate(2026, 7, 13); });
        QCOMPARE(f.reason(id), QString("2 days overdue"));
    }

    void alreadyTodayFollowsTheTodayList() {
        Fixture f;
        const qint64 due = f.add("Due", [](Task &t) { t.dueDate = Today; });
        const qint64 started = f.add("Started", [](Task &t) { t.startDate = Today; });
        const qint64 future = f.add("Future", [](Task &t) { t.dueDate = Today.addDays(3); });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        const QList<qint64> today = f.ids(s.today);
        QVERIFY(today.contains(due));
        QVERIFY(today.contains(started));
        QVERIFY(!today.contains(future));
    }

    void listExcludedFromTodayStaysOut() {
        Fixture f;
        const qint64 id = f.add("Hidden list task", [](Task &t) { t.dueDate = Today; });
        TaskList home = f.m_store.list(f.m_home);
        home.showInToday = false;
        f.m_store.begin("hide", false);
        f.m_store.putList(home);
        f.m_store.commit();
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QVERIFY(!f.ids(s.today).contains(id));
        QVERIFY(!f.ids(s.suggestions).contains(id));
    }

    void completedAndDeletedAreNeverShown() {
        Fixture f;
        const qint64 done = f.add("Done", [](Task &t) {
            t.dueDate = Today;
            t.completed = true;
        });
        const qint64 gone = f.add("Gone", [](Task &t) {
            t.dueDate = Today;
            t.deleted = true;
        });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QVERIFY(!f.ids(s.today).contains(done));
        QVERIFY(!f.ids(s.today).contains(gone));
    }

    void checklistItemHidesBehindItsChecklist() {
        Fixture f;
        const qint64 check = f.add("Packing", [](Task &t) {
            t.type = TaskType::Checklist;
            t.dueDate = Today;
        });
        const qint64 item = f.add("Passport", [check](Task &t) { t.parentId = check; });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QVERIFY(f.ids(s.suggestions).contains(check) || f.ids(s.today).contains(check));
        QVERIFY(!f.ids(s.today).contains(item));
        QVERIFY(!f.ids(s.suggestions).contains(item));
        QVERIFY(!f.ids(s.overdue).contains(item));
    }

    void projectChildrenShowOnTheirOwn() {
        Fixture f;
        const qint64 project = f.add("Renovate", [](Task &t) { t.type = TaskType::Project; });
        const qint64 child = f.add("Buy tiles", [project](Task &t) {
            t.parentId = project;
            t.dueDate = Today;
        });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QVERIFY(f.ids(s.today).contains(child));
    }

    void hiddenForTodayIsExcluded() {
        Fixture f;
        // The hiding is keyed by the day it was asked for, so this one uses
        // the real clock rather than the fixed test date.
        const QDate realToday = QDate::currentDate();
        const qint64 id = f.add("Hide me", [realToday](Task &t) { t.dueDate = realToday; });
        MyDay::hideForToday(f.m_store, {id});
        const MyDay::Sections s = MyDay::sections(f.m_store, QDateTime(realToday, QTime(10, 30)));
        QVERIFY(!f.ids(s.today).contains(id));
        QVERIFY(!f.ids(s.suggestions).contains(id));
        QVERIFY(!f.ids(s.overdue).contains(id));
    }

    void hiddenClearsTheNextDay() {
        Fixture f;
        const QDate realToday = QDate::currentDate();
        const qint64 id = f.add("Hide me", [realToday](Task &t) { t.dueDate = realToday.addDays(1); });
        MyDay::hideForToday(f.m_store, {id});
        // The next day's plan no longer reads yesterday's hidden list.
        const MyDay::Sections s = MyDay::sections(f.m_store, QDateTime(realToday.addDays(1), QTime(10, 30)));
        QVERIFY(f.ids(s.today).contains(id));
    }

    void noTaskAppearsTwice() {
        Fixture f;
        f.add("Starred late", [](Task &t) {
            t.starred = true;
            t.dueDate = QDate(2026, 7, 1);
        });
        f.add("Starred today", [](Task &t) {
            t.starred = true;
            t.dueDate = Today;
        });
        f.add("Start today", [](Task &t) { t.startDate = Today; });
        f.add("Due tomorrow", [](Task &t) { t.dueDate = Today.addDays(1); });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QSet<qint64> seen;
        int count = 0;
        auto tally = [&](const QVector<MyDay::Entry> &list) {
            for (const auto &e : list) {
                QVERIFY2(!seen.contains(e.taskId), "a task is in two sections");
                seen.insert(e.taskId);
                ++count;
            }
        };
        tally(s.overdue);
        tally(s.today);
        tally(s.suggestions);
        QCOMPARE(count, seen.size());
    }

    // ------------------------------------------------------- suggestions

    void starSuggestsStarred() {
        Fixture f;
        const qint64 id = f.add("Star", [](Task &t) { t.starred = true; });
        QCOMPARE(f.reason(id), QString("Starred"));
    }

    void startTodaySuggestsStartsToday() {
        Fixture f;
        // A start time later today keeps it out of Already today (it is still
        // scheduled), so the suggestion is what shows it.
        const qint64 id = f.add("Start", [](Task &t) {
            t.startDate = Today;
            t.startTime = 23 * H;
        });
        QCOMPARE(f.reason(id), QString("Starts today"));
    }

    void dueTomorrowSuggestsWeekday() {
        Fixture f;
        const qint64 id = f.add("Due", [](Task &t) { t.dueDate = Today.addDays(2); });
        QCOMPARE(f.reason(id), Today.addDays(2).toString("ddd").prepend("Due "));
    }

    void absoluteAlertTodaySuggestsAlert() {
        Fixture f;
        const qint64 id = f.add("Alerted", [](Task &t) { t.startDate = Today.addDays(5); });
        Alarm a;
        a.id = f.m_store.nextId("alarms");
        a.taskId = id;
        a.kind = Alarm::Absolute;
        a.at = at(2026, 7, 15, 14, 0);
        f.m_store.begin("alarm", false);
        f.m_store.putAlarm(a);
        f.m_store.commit();
        QCOMPARE(f.reason(id), QString("Alert 14:00"));
    }

    void startedLongAgoSuggestsStillWorkingOn() {
        Fixture f;
        // With "Still Working On" off for Today, the started task is not in
        // Already today and falls to the suggestion instead.
        f.m_store.setSetting("today.stillWorking", false);
        const qint64 id = f.add("Old start", [](Task &t) { t.startDate = Today.addDays(-3); });
        QCOMPARE(f.reason(id), QString("Still working on"));
    }

    void firstMatchingReasonWins() {
        Fixture f;
        // Starred beats "Starts today"; the reason is the first match only.
        const qint64 id = f.add("Both", [](Task &t) {
            t.starred = true;
            t.startDate = Today;
            t.startTime = 23 * H;
        });
        QCOMPARE(f.reason(id), QString("Starred"));
    }

    // ------------------------------------------------------------ ordering

    void suggestionsSortByPriorityThenDueThenDuration() {
        Fixture f;
        // Keep the started tasks out of Already today so all four are
        // suggestions and their order is what is being tested.
        f.m_store.setSetting("today.stillWorking", false);
        const qint64 low = f.add("Low", [](Task &t) {
            t.startDate = Today.addDays(-3);
            t.priority = Priority::Low;
        });
        const qint64 high = f.add("High", [](Task &t) {
            t.startDate = Today.addDays(-3);
            t.priority = Priority::High;
        });
        const qint64 mediumLong = f.add("Medium long", [](Task &t) {
            t.startDate = Today.addDays(-3);
            t.priority = Priority::Medium;
            t.duration = 60;
        });
        const qint64 mediumShort = f.add("Medium short", [](Task &t) {
            t.startDate = Today.addDays(-3);
            t.priority = Priority::Medium;
            t.duration = 15;
        });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QCOMPARE(f.ids(s.suggestions), (QList<qint64>{high, mediumShort, mediumLong, low}));
    }

    void todaySortsByPriorityThenDue() {
        Fixture f;
        const qint64 none = f.add("None", [](Task &t) { t.startDate = Today; });
        const qint64 high = f.add("High", [](Task &t) {
            t.startDate = Today;
            t.priority = Priority::High;
        });
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QCOMPARE(f.ids(s.today), (QList<qint64>{high, none}));
    }

    // ------------------------------------------------------------ capacity

    void capacityWithNoEvents() {
        Fixture f;
        f.add("Task", [](Task &t) {
            t.dueDate = Today;
            t.duration = 90;
        });
        f.add("Open", [](Task &t) { t.dueDate = Today; });
        CalendarFeed feed;
        const MyDay::Capacity c = MyDay::capacity(f.m_store, feed, at(2026, 7, 15, 10, 30));
        QCOMPARE(c.plannedMinutes, 90);
        QCOMPARE(c.withoutDuration, 1);
        // 9:00-17:00, now 10:30, so the window is 6.5h and all of it free.
        QCOMPARE(c.windowMinutes, 390);
        QCOMPARE(c.freeMinutes, 390);
        QCOMPARE(c.blockedMinutes, 0);
    }

    void capacityWithOverlappingEvents() {
        Fixture f;
        CalendarFeed feed;
        Zone z("UTC");
        feed.setSources({"/tmp/c.ics"}); feed.loadData("/tmp/c.ics", wrap(vevent("a", "Standup", 10 * H, 11 * H) + vevent("b", "Later", 10 * H + 30, 12 * H)));
        const MyDay::Capacity c = MyDay::capacity(f.m_store, feed, at(2026, 7, 15, 10, 30));
        // The window starts at now (10:30); the merged events run 10:30-12:00.
        QCOMPARE(c.windowMinutes, 390);
        QCOMPARE(c.freeMinutes, 390 - 90);
    }

    void capacityAfterWorkEndIsEmpty() {
        Fixture f;
        CalendarFeed feed;
        const MyDay::Capacity c = MyDay::capacity(f.m_store, feed, at(2026, 7, 15, 18, 0));
        QCOMPARE(c.windowMinutes, 0);
        QCOMPARE(c.freeMinutes, 0);
    }

    void capacityCountsTaskBlocksOnlyInsideTheWindow() {
        Fixture f;
        // A time block 11:30-12:30, inside the window.
        f.add("Blocked", [](Task &t) {
            t.dueDate = Today;
            t.dueTime = 12 * H + 30;
            t.duration = 60;
        });
        CalendarFeed feed;
        const MyDay::Capacity c = MyDay::capacity(f.m_store, feed, at(2026, 7, 15, 10, 30));
        QCOMPARE(c.blockedMinutes, 60);
    }

    void capacityExcludesProjectDuration() {
        Fixture f;
        f.add("Project", [](Task &t) {
            t.type = TaskType::Project;
            t.dueDate = Today;
            t.duration = 300;
        });
        CalendarFeed feed;
        const MyDay::Capacity c = MyDay::capacity(f.m_store, feed, at(2026, 7, 15, 10, 30));
        QCOMPARE(c.plannedMinutes, 0);
        QCOMPARE(c.withoutDuration, 0);
    }

    void freeMinutesCountsAShortGapThatFreeSlotsDrops() {
        CalendarFeed feed;
        Zone z("UTC");
        // A 20-minute gap from 10:00 to 10:20, surrounded by events.
        feed.setSources({"/tmp/g.ics"}); feed.loadData("/tmp/g.ics", wrap(vevent("a", "One", 9 * H, 10 * H) + vevent("b", "Two", 10 * H + 20, 11 * H)));
        QCOMPARE(feed.freeMinutes("2026-07-15", 9 * H, 11 * H), 20);
        // freeSlots only offers gaps of at least half an hour, so it has none.
        QVERIFY(feed.freeSlots("2026-07-15", 9 * H, 11 * H).isEmpty());
    }

    // --------------------------------------------------------- next slot

    void nextFreeSlotPacksThreeBlocksAroundAnEvent() {
        Fixture f;
        CalendarFeed feed;
        Zone z("UTC");
        feed.setSources({"/tmp/n.ics"}); feed.loadData("/tmp/n.ics", wrap(vevent("m", "Meeting", 10 * H, 11 * H)));
        // At 9:00 the first gap is 9:00-10:00, enough for a 30-minute block.
        QCOMPARE(MyDay::nextFreeSlot(f.m_store, feed, at(2026, 7, 15, 9, 0), 30), 9 * H);
        // Block 9:00-9:30; the next opening is 9:30-10:00.
        f.add("First", [](Task &t) {
            t.dueDate = Today;
            t.dueTime = 9 * H + 30;
            t.duration = 30;
        });
        QCOMPARE(MyDay::nextFreeSlot(f.m_store, feed, at(2026, 7, 15, 9, 0), 30), 9 * H + 30);
        // Block that too; the meeting fills 10:00-11:00, so the next is 11:00.
        f.add("Second", [](Task &t) {
            t.dueDate = Today;
            t.dueTime = 10 * H;
            t.duration = 30;
        });
        QCOMPARE(MyDay::nextFreeSlot(f.m_store, feed, at(2026, 7, 15, 9, 0), 30), 11 * H);
    }

    void nextFreeSlotReturnsMinusOneWhenTheDayIsFull() {
        Fixture f;
        CalendarFeed feed;
        Zone z("UTC");
        feed.setSources({"/tmp/f.ics"}); feed.loadData("/tmp/f.ics", wrap(vevent("a", "All morning", 9 * H, 17 * H)));
        QCOMPARE(MyDay::nextFreeSlot(f.m_store, feed, at(2026, 7, 15, 9, 0), 30), -1);
    }

    // ------------------------------------------------------------ actions

    void addToTodaySetsADueDateWhenThereIsNone() {
        Fixture f;
        const qint64 id = f.add("Undated");
        MyDay::addToToday(f.m_store, {id});
        QCOMPARE(f.m_store.task(id).dueDate, QDate::currentDate());
    }

    void addToTodayMovesAPastDeadline() {
        Fixture f;
        const qint64 id = f.add("Late", [](Task &t) { t.dueDate = QDate::currentDate().addDays(-4); });
        MyDay::addToToday(f.m_store, {id});
        QCOMPARE(f.m_store.task(id).dueDate, QDate::currentDate());
    }

    void addToTodayKeepsALaterDeadlineAndStartsToday() {
        Fixture f;
        const QDate deadline = QDate::currentDate().addDays(10);
        const qint64 id = f.add("Later", [deadline](Task &t) { t.dueDate = deadline; });
        MyDay::addToToday(f.m_store, {id});
        const Task t = f.m_store.task(id);
        QCOMPARE(t.startDate, QDate::currentDate());
        QCOMPARE(t.dueDate, deadline);
    }

    void removeFromTodayPushesTodayToTomorrow() {
        Fixture f;
        const qint64 id = f.add("Today", [](Task &t) { t.dueDate = QDate::currentDate(); });
        MyDay::removeFromToday(f.m_store, {id});
        QCOMPARE(f.m_store.task(id).dueDate, QDate::currentDate().addDays(1));
    }

    void addAndRemoveEachUndoWithOneStep() {
        Fixture f;
        const qint64 added = f.add("Undated");
        MyDay::addToToday(f.m_store, {added});
        QCOMPARE(f.m_store.task(added).dueDate, QDate::currentDate());
        QVERIFY(f.m_store.undo());
        QVERIFY(!f.m_store.task(added).dueDate.isValid());

        const qint64 hidden = f.add("Hide", [](Task &t) { t.dueDate = QDate::currentDate(); });
        MyDay::hideForToday(f.m_store, {hidden});
        QVERIFY(f.m_store.setting("myday.hidden").toString().contains(f.m_store.task(hidden).uid));
        QVERIFY(f.m_store.undo());
        QVERIFY(!f.m_store.setting("myday.hidden").toString().contains(f.m_store.task(hidden).uid));

        MyDay::removeFromToday(f.m_store, {hidden});
        QCOMPARE(f.m_store.task(hidden).dueDate, QDate::currentDate().addDays(1));
        QVERIFY(f.m_store.undo());
        QCOMPARE(f.m_store.task(hidden).dueDate, QDate::currentDate());
    }

    void markPlannedRemembersTheDay() {
        Fixture f;
        MyDay::markPlanned(f.m_store, Today);
        QCOMPARE(MyDay::lastPlanned(f.m_store), Today);
    }

    // ---------------------------------------------------------- summary

    void summaryCountsCompletedAndPlannedMinutes() {
        Fixture f;
        // Two open tasks today: one 90 minutes, one 30; a third with no duration.
        f.add("Open long", [](Task &t) {
            t.dueDate = Today;
            t.duration = 90;
        });
        f.add("Open short", [](Task &t) {
            t.dueDate = Today;
            t.duration = 30;
        });
        f.add("Open undated", [](Task &t) { t.dueDate = Today; });
        // Completed today: one 45 minutes, one 60, and one with no duration; a
        // fourth was completed yesterday and does not count.
        f.add("Done long", [](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 15, 9, 0);
            t.duration = 45;
        });
        f.add("Done short", [](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 15, 11, 0);
            t.duration = 60;
        });
        f.add("Done undated", [](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 15, 11, 30);
        });
        f.add("Done yesterday", [](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 14, 17, 0);
            t.duration = 120;
        });
        const MyDay::Summary s = MyDay::summary(f.m_store, Now);
        QCOMPARE(s.completedCount, 3);
        QCOMPARE(s.plannedMinutes, 120);
        QCOMPARE(s.doneMinutes, 105);
    }

    void summaryExcludesProjectsFromTheMinutes() {
        Fixture f;
        // A completed project's own duration is not time spent, as in capacity.
        f.add("Done project", [](Task &t) {
            t.type = TaskType::Project;
            t.completed = true;
            t.completedAt = at(2026, 7, 15, 12, 0);
            t.duration = 300;
        });
        const MyDay::Summary s = MyDay::summary(f.m_store, Now);
        QCOMPARE(s.completedCount, 1);
        QCOMPARE(s.doneMinutes, 0);
    }

    void summaryIsEmptyWhenDisabled() {
        Fixture f;
        f.add("Done", [](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 15, 10, 0);
            t.duration = 30;
        });
        f.m_store.setSetting("myday.enabled", false);
        const MyDay::Summary s = MyDay::summary(f.m_store, Now);
        QCOMPARE(s.completedCount, 0);
        QCOMPARE(s.plannedMinutes, 0);
        QCOMPARE(s.doneMinutes, 0);
    }

    void workEndedAtTheWorkDaysEnd() {
        Fixture f;
        QVERIFY(!MyDay::workEnded(f.m_store, at(2026, 7, 15, 16, 59)));
        QVERIFY(MyDay::workEnded(f.m_store, at(2026, 7, 15, 17, 0)));
        QVERIFY(MyDay::workEnded(f.m_store, at(2026, 7, 15, 20, 0)));
        // The setting moves the end.
        f.m_store.setSetting("myday.workEnd", 18 * 60);
        QVERIFY(!MyDay::workEnded(f.m_store, at(2026, 7, 15, 17, 30)));
        QVERIFY(MyDay::workEnded(f.m_store, at(2026, 7, 15, 18, 0)));
    }

    void shutdownPromptFiresOnceAfterWorkEnd() {
        Fixture f;
        f.m_store.setSetting("myday.shutdownPrompt", true);
        QVERIFY(!MyDay::shutdownPromptDue(f.m_store, at(2026, 7, 15, 16, 0)));
        QVERIFY(MyDay::shutdownPromptDue(f.m_store, at(2026, 7, 15, 17, 0)));
        MyDay::markShutdownPrompted(f.m_store, Today);
        QVERIFY(!MyDay::shutdownPromptDue(f.m_store, at(2026, 7, 15, 17, 30)));
        // The next day is due again.
        QVERIFY(MyDay::shutdownPromptDue(f.m_store, at(2026, 7, 16, 17, 0)));
    }

    void shutdownPromptRespectsItsSettingAndFeature() {
        Fixture f;
        QVERIFY(!MyDay::shutdownPromptDue(f.m_store, at(2026, 7, 15, 17, 0)));
        f.m_store.setSetting("myday.shutdownPrompt", true);
        f.m_store.setSetting("myday.enabled", false);
        QVERIFY(!MyDay::shutdownPromptDue(f.m_store, at(2026, 7, 15, 17, 0)));
        QVERIFY(!MyDay::workEnded(f.m_store, at(2026, 7, 15, 17, 0)));
    }

    // ----------------------------------------------------------- disabled

    void disabledShowsNothing() {
        Fixture f;
        f.add("Due", [](Task &t) { t.dueDate = Today; });
        f.m_store.setSetting("myday.enabled", false);
        const MyDay::Sections s = MyDay::sections(f.m_store, Now);
        QVERIFY(s.overdue.isEmpty() && s.today.isEmpty() && s.suggestions.isEmpty());
        CalendarFeed feed;
        const MyDay::Capacity c = MyDay::capacity(f.m_store, feed, Now);
        QCOMPARE(c.windowMinutes, 0);
        QCOMPARE(MyDay::nextFreeSlot(f.m_store, feed, Now, 30), -1);
    }

    // -------------------------------------------------------- time zones

    void rulesHoldInUsPacific() {
        Zone z("America/Los_Angeles");
        Fixture f;
        const qint64 overdue = f.add("Older", [](Task &t) { t.dueDate = QDate(2026, 7, 10); });
        const qint64 today = f.add("Due", [](Task &t) { t.dueDate = Today; });
        const qint64 starred = f.add("Star", [](Task &t) { t.starred = true; });
        const MyDay::Sections s = MyDay::sections(f.m_store, at(2026, 7, 15, 9, 0));
        QCOMPARE(f.ids(s.overdue), (QList<qint64>{overdue}));
        QVERIFY(f.ids(s.today).contains(today));
        QCOMPARE(f.reason(starred), QString("Starred"));
    }
};

REGISTER_TEST(MyDayTest)

#include "test_myday.moc"
