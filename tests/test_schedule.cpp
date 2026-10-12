#include <QtTest>

#include "schedule.h"
#include "testregistry.h"

namespace {

constexpr int H = 60;

// A fixed clock: Wednesday 15 July 2026, 10:30.
QDateTime at(int y, int m, int d, int hh = 10, int mm = 30) { return QDateTime(QDate(y, m, d), QTime(hh, mm)); }
const QDateTime Now = at(2026, 7, 15);
const QDate Today(2026, 7, 15);

Task task(const QDate &start = {}, int startTime = -1, const QDate &due = {}, int dueTime = -1, int duration = 0) {
    Task t;
    t.id = 1;
    t.startDate = start;
    t.startTime = startTime;
    t.dueDate = due;
    t.dueTime = dueTime;
    t.duration = duration;
    return t;
}

ScheduleSettings defaults() { return {}; }

}  // namespace

class ScheduleTest : public QObject {
    Q_OBJECT
private slots:
    // ---- overdue / scheduled ----

    void overdueByDate() {
        QVERIFY(Schedule::isOverdue(task({}, -1, QDate(2026, 7, 14)), Now, defaults()));
        QVERIFY(!Schedule::isOverdue(task({}, -1, Today), Now, defaults()));
        QVERIFY(!Schedule::isOverdue(task({}, -1, QDate(2026, 7, 16)), Now, defaults()));
        QVERIFY(!Schedule::isOverdue(task(), Now, defaults()));
    }

    void completedIsNeverOverdue() {
        Task t = task({}, -1, QDate(2026, 7, 1));
        t.completed = true;
        QVERIFY(!Schedule::isOverdue(t, Now, defaults()));
    }

    void startDateNeverMakesOverdue() {
        QVERIFY(!Schedule::isOverdue(task(QDate(2026, 7, 1)), Now, defaults()));
    }

    void overdueDueTimeOffByDefault() {
        const Task t = task({}, -1, Today, 9 * H);  // due 9:00, now 10:30
        QVERIFY(!Schedule::isOverdue(t, Now, defaults()));
    }

    void overdueUsesDueTime() {
        ScheduleSettings s;
        s.overdueUsesDueTime = true;
        QVERIFY(Schedule::isOverdue(task({}, -1, Today, 9 * H), Now, s));
        QVERIFY(!Schedule::isOverdue(task({}, -1, Today, 11 * H), Now, s));
        QVERIFY(!Schedule::isOverdue(task({}, -1, Today), Now, s));  // date-only: end of day
        QVERIFY(!Schedule::isOverdue(task({}, -1, QDate(2026, 7, 16), 1 * H), Now, s));
    }

    void scheduledFutureDate() {
        QVERIFY(Schedule::isScheduled(task(QDate(2026, 7, 16)), Now));
        QVERIFY(!Schedule::isScheduled(task(Today), Now));
        QVERIFY(!Schedule::isScheduled(task(QDate(2026, 7, 14)), Now));
        QVERIFY(!Schedule::isScheduled(task(), Now));
    }

    void scheduledTodayWithFutureTime() {
        QVERIFY(Schedule::isScheduled(task(Today, 20 * H), Now));
        QVERIFY(!Schedule::isScheduled(task(Today, 9 * H), Now));
        QVERIFY(!Schedule::isScheduled(task(Today, 10 * H + 30), Now));  // exactly now has started
    }

    // ---- effective start ----

    void effectiveStartDueTimeMinusDuration() {
        // "Pick up the kids": due 3:15 PM, 20 minutes -> 2:55 PM.
        QCOMPARE(Schedule::effectiveStart(task({}, -1, Today, 15 * H + 15, 20)), at(2026, 7, 15, 14, 55));
        // "Submit proposal": Fri 5 PM, 2 hours -> 3 PM.
        QCOMPARE(Schedule::effectiveStart(task({}, -1, QDate(2026, 7, 17), 17 * H, 2 * H)), at(2026, 7, 17, 15, 0));
        // "Join team meeting": 3 PM, 30 minutes -> 2:30 PM.
        QCOMPARE(Schedule::effectiveStart(task({}, -1, QDate(2026, 7, 16), 15 * H, 30)), at(2026, 7, 16, 14, 30));
    }

    void effectiveStartCrossesMidnight() {
        QCOMPARE(Schedule::effectiveStart(task({}, -1, Today, 1 * H, 2 * H)), at(2026, 7, 14, 23, 0));
    }

    void effectiveStartDateOnlyWholeDays() {
        // "Pack for a trip": Fri, 2 days -> Wednesday.
        QCOMPARE(Schedule::effectiveStart(task({}, -1, QDate(2026, 7, 17), -1, 2 * 24 * H)).date(), QDate(2026, 7, 15));
        // "Hand in history essay": Fri, 3 days -> Tuesday.
        QCOMPARE(Schedule::effectiveStart(task({}, -1, QDate(2026, 7, 17), -1, 3 * 24 * H)).date(), QDate(2026, 7, 14));
    }

    void effectiveStartDateOnlySubDayIsInvalid() {
        QVERIFY(!Schedule::effectiveStart(task(QDate(2026, 7, 18), 15 * H, QDate(2026, 7, 18), -1, 30)).isValid());
        QVERIFY(!Schedule::effectiveStart(task({}, -1, Today, -1, 23 * H)).isValid());
    }

    void effectiveStartInvalidWithoutDueOrDuration() {
        QVERIFY(!Schedule::effectiveStart(task({}, -1, {}, -1, 60)).isValid());
        QVERIFY(!Schedule::effectiveStart(task({}, -1, Today, 15 * H, 0)).isValid());
    }

    void dueMomentDateOnlyIsEndOfDay() {
        QCOMPARE(Schedule::dueMoment(task({}, -1, Today)), QDateTime(Today, QTime(23, 59, 59)));
        QCOMPARE(Schedule::dueMoment(task({}, -1, Today, 20 * H)), at(2026, 7, 15, 20, 0));
        QVERIFY(!Schedule::dueMoment(task()).isValid());
    }

    // ---- inToday: At Home table ----

    void buyMilkNotInToday() { QVERIFY(!Schedule::inToday(task(), Now, defaults())); }

    void electricityBill() {
        const Task bill = task({}, -1, QDate(2026, 7, 25));
        QVERIFY(!Schedule::inToday(bill, at(2026, 7, 24), defaults()));
        QVERIFY(Schedule::inToday(bill, at(2026, 7, 25), defaults()));
        QVERIFY(!Schedule::isOverdue(bill, at(2026, 7, 25, 23, 0), defaults()));
        QVERIFY(Schedule::isOverdue(bill, at(2026, 7, 26, 0, 5), defaults()));
        QVERIFY(Schedule::inToday(bill, at(2026, 7, 26), defaults()));  // overdue carries forward
    }

    void binsDueWednesdayEvening() {
        const Task t = task({}, -1, Today, 20 * H);
        QVERIFY(Schedule::inToday(t, Now, defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 15, 0, 1), defaults()));
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 14), defaults()));
        QVERIFY(!Schedule::isOverdue(t, Now, defaults()));
    }

    void binsStartWednesdayEvening() {
        const Task t = task(Today, 20 * H);
        QVERIFY(Schedule::isScheduled(t, Now));
        QVERIFY(!Schedule::inToday(t, Now, defaults()));
        QVERIFY(!Schedule::isScheduled(t, at(2026, 7, 15, 20, 0)));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 15, 20, 1), defaults()));
    }

    void mowTheLawn() {
        const Task t = task(QDate(2026, 7, 18), 9 * H, {}, -1, 1 * H);
        QVERIFY(!Schedule::inToday(t, Now, defaults()));
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 18, 8, 59), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 18, 9, 0), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 18, 12, 0), defaults()));  // not a 10 AM deadline
        QVERIFY(!Schedule::isOverdue(t, at(2026, 7, 18, 12, 0), defaults()));
        QVERIFY(!Schedule::isOverdue(t, at(2026, 7, 20, 12, 0), defaults()));  // start never makes it late
        QVERIFY(Schedule::inToday(t, at(2026, 7, 19), defaults()));  // stays until done
    }

    void visitAFriend() {
        const QDate sat(2026, 7, 18);
        const Task t = task(sat, 15 * H, sat, -1, 30);
        QVERIFY(!Schedule::inToday(t, Now, defaults()));  // sub-day duration doesn't bring it early
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 17, 12, 0), defaults()));
        QVERIFY(Schedule::isScheduled(t, at(2026, 7, 18, 10, 0)));
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 18, 10, 0), defaults()));
        QVERIFY(!Schedule::isScheduled(t, at(2026, 7, 18, 15, 0)));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 18, 15, 0), defaults()));
    }

    void pickUpTheKids() {
        const Task t = task({}, -1, Today, 15 * H + 15, 20);
        QCOMPARE(Schedule::effectiveStart(t), at(2026, 7, 15, 14, 55));
        QVERIFY(Schedule::inToday(t, Now, defaults()));
        QVERIFY(!Schedule::isInProgress(t, Now));
        QVERIFY(Schedule::isInProgress(t, at(2026, 7, 15, 15, 0)));
        QVERIFY(!Schedule::isInProgress(t, at(2026, 7, 15, 15, 16)));
    }

    void packForATrip() {
        const Task t = task({}, -1, QDate(2026, 7, 17), -1, 2 * 24 * H);
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 14), defaults()));
        QVERIFY(Schedule::inToday(t, Now, defaults()));  // from Wednesday
        QVERIFY(Schedule::inToday(t, at(2026, 7, 16), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 17), defaults()));
        QVERIFY(!Schedule::isOverdue(t, at(2026, 7, 17, 23, 0), defaults()));
        QVERIFY(Schedule::isOverdue(t, at(2026, 7, 18, 1, 0), defaults()));
    }

    void renewCarInsurance() {
        const Task t = task(QDate(2027, 3, 1), -1, QDate(2027, 3, 31), -1, 30);
        QVERIFY(Schedule::isScheduled(t, at(2027, 2, 28)));
        QVERIFY(!Schedule::inToday(t, at(2027, 2, 28), defaults()));
        QVERIFY(Schedule::inToday(t, at(2027, 3, 1), defaults()));
        QVERIFY(Schedule::inToday(t, at(2027, 3, 15), defaults()));  // still working
        QVERIFY(Schedule::inToday(t, at(2027, 3, 31), defaults()));
        QVERIFY(Schedule::isOverdue(t, at(2027, 4, 1), defaults()));
    }

    // ---- inToday: At Work table ----

    void joinTeamMeeting() {
        const Task t = task({}, -1, QDate(2026, 7, 16), 15 * H, 30);
        QCOMPARE(Schedule::effectiveStart(t), at(2026, 7, 16, 14, 30));
        QVERIFY(!Schedule::inToday(t, Now, defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 16, 9, 0), defaults()));  // due that day
    }

    void submitProposal() {
        const Task t = task({}, -1, QDate(2026, 7, 17), 17 * H, 2 * H);
        QCOMPARE(Schedule::effectiveStart(t), at(2026, 7, 17, 15, 0));
        QVERIFY(!Schedule::inToday(t, Now, defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 17, 8, 0), defaults()));
        QVERIFY(Schedule::isInProgress(t, at(2026, 7, 17, 16, 0)));
        QVERIFY(!Schedule::isInProgress(t, at(2026, 7, 17, 14, 0)));
    }

    void readChapterFive() {
        const QDate mon(2026, 7, 13);
        const Task t = task(mon, 16 * H, {}, -1, 2 * H);
        QVERIFY(Schedule::isScheduled(t, at(2026, 7, 13, 10, 0)));
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 13, 10, 0), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 13, 16, 0), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 13, 19, 0), defaults()));  // not due at 6 PM
        QVERIFY(!Schedule::isOverdue(t, at(2026, 7, 13, 19, 0), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 14), defaults()));
    }

    void handInHistoryEssay() {
        const Task t = task({}, -1, QDate(2026, 7, 17), -1, 3 * 24 * H);
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 13), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 14), defaults()));  // Tuesday
        QVERIFY(Schedule::inToday(t, at(2026, 7, 16), defaults()));
    }

    void prepQuarterlyReview() {
        const Task t = task(QDate(2026, 7, 13), -1, QDate(2026, 7, 17));
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 12), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 13), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 15), defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 17), defaults()));
    }

    void groupPresentation() {
        const Task t = task(QDate(2026, 7, 15), -1, QDate(2026, 7, 17), 14 * H, 3 * 24 * H);
        QCOMPARE(Schedule::effectiveStart(t), at(2026, 7, 14, 14, 0));
        // The start date wins over the lead-up window reaching back to Tuesday.
        QVERIFY(Schedule::isScheduled(t, at(2026, 7, 14, 15, 0)));
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 14, 15, 0), defaults()));
        QVERIFY(Schedule::inToday(t, Now, defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 17, 9, 0), defaults()));
    }

    void weeklyStatusReport() {
        const Task t = task({}, -1, QDate(2026, 7, 17), 16 * H, 1 * H);
        QCOMPARE(Schedule::effectiveStart(t), at(2026, 7, 17, 15, 0));
        QVERIFY(!Schedule::inToday(t, Now, defaults()));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 17, 8, 0), defaults()));
    }

    // ---- settings ----

    void completedNeverInToday() {
        Task t = task({}, -1, Today);
        t.completed = true;
        t.starred = true;
        ScheduleSettings s;
        s.todayIncludesStarred = true;
        QVERIFY(!Schedule::inToday(t, Now, s));
    }

    void starredIncludedOnlyWithSetting() {
        Task t = task();
        t.starred = true;
        QVERIFY(!Schedule::inToday(t, Now, defaults()));
        ScheduleSettings s;
        s.todayIncludesStarred = true;
        QVERIFY(Schedule::inToday(t, Now, s));
    }

    void starredBeatsScheduled() {
        Task t = task(QDate(2026, 7, 20));
        t.starred = true;
        ScheduleSettings s;
        s.todayIncludesStarred = true;
        QVERIFY(Schedule::inToday(t, Now, s));
    }

    void overdueInTodayFollowsSetting() {
        const Task t = task({}, -1, QDate(2026, 7, 10));
        QVERIFY(Schedule::inToday(t, Now, defaults()));
        ScheduleSettings s;
        s.todayIncludesOverdue = false;
        QVERIFY(!Schedule::inToday(t, Now, s));
    }

    void dueTodayIgnoresOverdueSetting() {
        ScheduleSettings s;
        s.todayIncludesOverdue = false;
        QVERIFY(Schedule::inToday(task({}, -1, Today), Now, s));
    }

    void stillWorkingOnOff() {
        const Task t = task(QDate(2026, 7, 10));
        QVERIFY(Schedule::inToday(t, Now, defaults()));
        ScheduleSettings s;
        s.todayIncludesStillWorking = false;
        QVERIFY(!Schedule::inToday(t, Now, s));
        // A start date of today still counts.
        QVERIFY(Schedule::inToday(task(Today), Now, s));
    }

    void leadUpWindowWithOverdueIncluded() {
        const Task t = task({}, -1, QDate(2026, 7, 17), -1, 3 * 24 * H);  // window opens Tue 14
        for (int d : {14, 15, 16, 17})
            QVERIFY2(Schedule::inToday(t, at(2026, 7, d), defaults()), qPrintable(QString::number(d)));
    }

    void leadUpWindowWithOverdueExcluded() {
        ScheduleSettings s;
        s.todayIncludesOverdue = false;
        const Task t = task({}, -1, QDate(2026, 7, 17), -1, 3 * 24 * H);  // window opens Tue 14
        QVERIFY(Schedule::inToday(t, at(2026, 7, 14), s));   // first day of the window
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 15), s));
        QVERIFY(!Schedule::inToday(t, at(2026, 7, 16), s));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 17), s));   // the due date brings it back
    }

    void leadUpWindowDueTimeExcluded() {
        ScheduleSettings s;
        s.todayIncludesOverdue = false;
        const Task t = task({}, -1, QDate(2026, 7, 16), 15 * H, 30 * H);  // opens Wed 15 9:00
        QVERIFY(Schedule::inToday(t, at(2026, 7, 15, 12, 0), s));
        QVERIFY(Schedule::inToday(t, at(2026, 7, 16, 8, 0), s));
    }

    // ---- friendly dates ----

    void friendlyNames() {
        QCOMPARE(Schedule::friendlyDate(Today, Today), QString("Today"));
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 16), Today), QString("Tomorrow"));
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 14), Today), QString("Yesterday"));
        QVERIFY(Schedule::friendlyDate(QDate(), Today).isEmpty());
    }

    void friendlyWeekdayWithinAWeek() {
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 17), Today), QString("Friday"));
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 18), Today), QString("Saturday"));
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 21), Today), QString("Tuesday"));
    }

    void friendlyInNDays() {
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 22), Today), QString("In 7 days"));
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 8, 12), Today), QString("In 28 days"));
    }

    void friendlyDaysAgo() {
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 13), Today), QString("2 days ago"));
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 5), Today), QString("10 days ago"));
    }

    void friendlyOffUsesCalendarDates() {
        QCOMPARE(Schedule::friendlyDate(QDate(2026, 7, 17), Today, false), QString("Fri, 17 Jul"));
        QCOMPARE(Schedule::friendlyDate(QDate(2027, 1, 2), Today, false), QString("2 Jan 2027"));
    }

    void formatTimeAndDateTime() {
        QCOMPARE(Schedule::formatTime(15 * H + 5), QString("3:05 PM"));
        QVERIFY(Schedule::formatTime(-1).isEmpty());
        QCOMPARE(Schedule::friendlyDateTime(QDate(2026, 7, 16), 15 * H, Today), QString("Tomorrow, 3:00 PM"));
        QCOMPARE(Schedule::friendlyDateTime(QDate(2026, 7, 16), -1, Today), QString("Tomorrow"));
    }

    // ---- durations ----

    void durationBadge() {
        QVERIFY(Schedule::durationBadge(0).isEmpty());
        QCOMPARE(Schedule::durationBadge(30), QString("30 m"));
        QCOMPARE(Schedule::durationBadge(60), QString("1 h"));
        QCOMPARE(Schedule::durationBadge(90), QString("1 h 30 m"));
        QCOMPARE(Schedule::durationBadge(24 * 60), QString("1 d"));
        QCOMPARE(Schedule::durationBadge(2 * 24 * 60), QString("2 d"));
    }

    void durationTotal() {
        QVERIFY(Schedule::durationTotal(0).isEmpty());
        QCOMPARE(Schedule::durationTotal(45), QString("45m"));
        QCOMPARE(Schedule::durationTotal(120), QString("2h"));
        QCOMPARE(Schedule::durationTotal(165), QString("2h 45m"));
    }

    // ---- dueHint ----

    void dueHintDueIn() {
        QCOMPARE(Schedule::dueHint(task({}, -1, Today, 20 * H), Now, defaults()), QString("Due in 9h 30m"));
        QCOMPARE(Schedule::dueHint(task({}, -1, Today, 10 * H + 45), Now, defaults()), QString("Due in 15m"));
    }

    void dueHintStartsIn() {
        QCOMPARE(Schedule::dueHint(task(QDate(2026, 7, 17), 10 * H + 30), Now, defaults()), QString("Starts in 2d"));
        QCOMPARE(Schedule::dueHint(task(Today, 12 * H), Now, defaults()), QString("Starts in 1h 30m"));
    }

    void dueHintStartsInBeforeLeadUp() {
        const Task t = task({}, -1, QDate(2026, 7, 20), 15 * H, 2 * H);  // opens Mon 13:00
        QVERIFY(Schedule::dueHint(t, Now, defaults()).startsWith("Starts in "));
    }

    void dueHintRemainingInWindow() {
        const Task t = task({}, -1, Today, 12 * H, 4 * H);  // 8:00 - 12:00
        QCOMPARE(Schedule::dueHint(t, Now, defaults()), QString("1h 30m remaining"));
    }

    void dueHintOverdue() {
        QCOMPARE(Schedule::dueHint(task({}, -1, QDate(2026, 7, 12)), Now, defaults()), QString("Overdue by 3 days"));
        QCOMPARE(Schedule::dueHint(task({}, -1, QDate(2026, 7, 14)), Now, defaults()), QString("Overdue by 1 day"));
        ScheduleSettings s;
        s.overdueUsesDueTime = true;
        QCOMPARE(Schedule::dueHint(task({}, -1, Today, 9 * H), Now, s), QString("Overdue"));
    }

    void dueHintCompletedAndEmpty() {
        Task t = task({}, -1, Today);
        t.completed = true;
        t.completedAt = at(2026, 7, 14, 9, 0);
        QCOMPARE(Schedule::dueHint(t, Now, defaults()), QString("Completed yesterday"));
        QVERIFY(Schedule::dueHint(task(), Now, defaults()).isEmpty());
    }
};

REGISTER_TEST(ScheduleTest)
#include "test_schedule.moc"
