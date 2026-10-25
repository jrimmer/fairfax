#include <QtTest>

#include "recurrence.h"
#include "testregistry.h"

namespace {

Recurrence rule(int frequency, int interval = 1) {
    Recurrence r;
    r.frequency = frequency;
    r.interval = interval;
    return r;
}

constexpr int Mon = 1 << 0, Tue = 1 << 1, Wed = 1 << 2, Thu = 1 << 3, Fri = 1 << 4, Sat = 1 << 5, Sun = 1 << 6;

}  // namespace

class RecurrenceTest : public QObject {
    Q_OBJECT
private slots:
    void noneHasNoNext() {
        QVERIFY(!Recurrences::next(rule(Repeat::None), QDate(2026, 7, 15)).isValid());
        QCOMPARE(Recurrences::describe(rule(Repeat::None)), QString());
    }

    void daily() {
        QCOMPARE(Recurrences::next(rule(Repeat::Daily), QDate(2026, 12, 31)), QDate(2027, 1, 1));
        QCOMPARE(Recurrences::next(rule(Repeat::Daily, 3), QDate(2026, 7, 15)), QDate(2026, 7, 18));
    }

    void intervalBelowOneIsOne() {
        QCOMPARE(Recurrences::next(rule(Repeat::Daily, 0), QDate(2026, 7, 15)), QDate(2026, 7, 16));
        QCOMPARE(Recurrences::next(rule(Repeat::Weekly, -4), QDate(2026, 7, 15)), QDate(2026, 7, 22));
    }

    void weekdaysSkipWeekend() {
        QCOMPARE(Recurrences::next(rule(Repeat::Weekdays), QDate(2026, 7, 15)), QDate(2026, 7, 16));
        QCOMPARE(Recurrences::next(rule(Repeat::Weekdays), QDate(2026, 7, 17)), QDate(2026, 7, 20));
        QCOMPARE(Recurrences::next(rule(Repeat::Weekdays), QDate(2026, 7, 18)), QDate(2026, 7, 20));
        QCOMPARE(Recurrences::next(rule(Repeat::Weekdays, 5), QDate(2026, 7, 19)), QDate(2026, 7, 20));
    }

    void weeklyPlain() {
        QCOMPARE(Recurrences::next(rule(Repeat::Weekly), QDate(2026, 7, 15)), QDate(2026, 7, 22));
        QCOMPARE(Recurrences::next(rule(Repeat::Weekly, 2), QDate(2026, 7, 15)), QDate(2026, 7, 29));
    }

    void weeklyMask() {
        Recurrence r = rule(Repeat::Weekly);
        r.weekdays = Mon | Wed;
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 13)), QDate(2026, 7, 15));  // Mon -> Wed
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 15)), QDate(2026, 7, 20));  // Wed -> next Mon
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 16)), QDate(2026, 7, 20));  // Thu -> next Mon
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 19)), QDate(2026, 7, 20));  // Sun -> Mon
    }

    void weeklyMaskWithInterval() {
        Recurrence r = rule(Repeat::Weekly, 2);
        r.weekdays = Mon | Wed;
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 13)), QDate(2026, 7, 15));
        // After Wednesday the week is used up, so jump two weeks from that Monday.
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 15)), QDate(2026, 7, 27));
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 27)), QDate(2026, 7, 29));
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 19)), QDate(2026, 7, 27));
    }

    void weeklyMaskSundayWraps() {
        Recurrence r = rule(Repeat::Weekly, 2);
        r.weekdays = Fri | Sun;
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 17)), QDate(2026, 7, 19));
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 19)), QDate(2026, 7, 31));
    }

    void monthlyClampsToMonthLength() {
        QCOMPARE(Recurrences::next(rule(Repeat::Monthly), QDate(2026, 1, 31)), QDate(2026, 2, 28));
        QCOMPARE(Recurrences::next(rule(Repeat::Monthly), QDate(2028, 1, 31)), QDate(2028, 2, 29));
        QCOMPARE(Recurrences::next(rule(Repeat::Monthly), QDate(2026, 3, 31)), QDate(2026, 4, 30));
        QCOMPARE(Recurrences::next(rule(Repeat::Monthly, 3), QDate(2026, 11, 30)), QDate(2027, 2, 28));
        QCOMPARE(Recurrences::next(rule(Repeat::Monthly), QDate(2026, 12, 15)), QDate(2027, 1, 15));
    }

    void monthlyOrdinalWeekday() {
        Recurrence r = rule(Repeat::Monthly);
        r.monthOrdinal = 2;
        r.monthWeekday = 2;  // 2nd Tuesday
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 14)), QDate(2026, 8, 11));
        QCOMPARE(Recurrences::next(r, QDate(2026, 12, 8)), QDate(2027, 1, 12));
    }

    void monthlyLastWeekday() {
        Recurrence r = rule(Repeat::Monthly);
        r.monthOrdinal = -1;
        r.monthWeekday = 5;  // last Friday
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 31)), QDate(2026, 8, 28));
        QCOMPARE(Recurrences::next(r, QDate(2026, 8, 28)), QDate(2026, 9, 25));
        r.monthWeekday = 7;  // last Sunday
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 26)), QDate(2026, 8, 30));
    }

    void monthlyFifthWeekdaySkipsShortMonths() {
        Recurrence r = rule(Repeat::Monthly);
        r.monthOrdinal = 5;
        r.monthWeekday = 5;  // 5th Friday: Jul 2026 has one (31st), Aug does not, Oct does.
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 31)), QDate(2026, 10, 30));
    }

    void monthlyOrdinalWithInterval() {
        Recurrence r = rule(Repeat::Monthly, 2);
        r.monthOrdinal = 1;
        r.monthWeekday = 1;  // 1st Monday every two months
        QCOMPARE(Recurrences::next(r, QDate(2026, 7, 6)), QDate(2026, 9, 7));
    }

    void yearly() {
        QCOMPARE(Recurrences::next(rule(Repeat::Yearly), QDate(2026, 7, 15)), QDate(2027, 7, 15));
        QCOMPARE(Recurrences::next(rule(Repeat::Yearly, 2), QDate(2026, 7, 15)), QDate(2028, 7, 15));
        QCOMPARE(Recurrences::next(rule(Repeat::Yearly), QDate(2028, 2, 29)), QDate(2029, 2, 28));
    }

    void advanceFromDueKeepsGap() {
        Task t;
        t.id = 7;
        t.uid = "abc";
        t.title = "Report";
        t.tagIds = {1, 2};
        t.startDate = QDate(2026, 7, 13);
        t.dueDate = QDate(2026, 7, 15);
        t.startTime = 9 * 60;
        t.dueTime = 17 * 60;
        t.duration = 45;
        t.recurrence = rule(Repeat::Weekly);
        t.completed = true;
        t.completedAt = QDateTime(QDate(2026, 7, 16), QTime(8, 0));
        const auto next = Recurrences::advance(t, QDate(2026, 7, 16));
        QVERIFY(next.has_value());
        QCOMPARE(next->dueDate, QDate(2026, 7, 22));
        QCOMPARE(next->startDate, QDate(2026, 7, 20));
        QCOMPARE(next->startTime, 9 * 60);
        QCOMPARE(next->dueTime, 17 * 60);
        QCOMPARE(next->duration, 45);
        QCOMPARE(next->recurrence.occurrencesDone, 1);
        QVERIFY(!next->completed);
        QVERIFY(!next->completedAt.isValid());
        QCOMPARE(next->id, qint64(7));
        QCOMPARE(next->uid, QString("abc"));
        QCOMPARE(next->tagIds, QList<qint64>({1, 2}));
        QCOMPARE(next->title, QString("Report"));
    }

    void advanceStartOnly() {
        Task t;
        t.startDate = QDate(2026, 7, 15);
        t.startTime = 8 * 60;
        t.recurrence = rule(Repeat::Daily);
        const auto next = Recurrences::advance(t, QDate(2026, 7, 20));
        QVERIFY(next.has_value());
        QCOMPARE(next->startDate, QDate(2026, 7, 16));
        QVERIFY(!next->dueDate.isValid());
        QCOMPARE(next->startTime, 8 * 60);
    }

    void advanceFromCompletion() {
        Task t;
        t.dueDate = QDate(2026, 7, 1);
        t.startDate = QDate(2026, 6, 29);
        t.recurrence = rule(Repeat::Weekly, 2);
        t.recurrence.from = Repeat::FromCompletion;
        const auto next = Recurrences::advance(t, QDate(2026, 7, 10));
        QVERIFY(next.has_value());
        QCOMPARE(next->dueDate, QDate(2026, 7, 24));
        QCOMPARE(next->startDate, QDate(2026, 7, 22));
    }

    void advanceWithoutDatesUsesCompletion() {
        Task t;
        t.recurrence = rule(Repeat::Daily);
        const auto next = Recurrences::advance(t, QDate(2026, 7, 10));
        QVERIFY(next.has_value());
        QCOMPARE(next->recurrence.occurrencesDone, 1);
    }

    void endAfterCount() {
        Task t;
        t.dueDate = QDate(2026, 7, 15);
        t.recurrence = rule(Repeat::Daily);
        t.recurrence.endType = Repeat::AfterCount;
        t.recurrence.endCount = 3;
        auto a = Recurrences::advance(t, QDate(2026, 7, 15));
        QVERIFY(a);
        auto b = Recurrences::advance(*a, QDate(2026, 7, 16));
        QVERIFY(b);
        QCOMPARE(b->recurrence.occurrencesDone, 2);
        QVERIFY(!Recurrences::advance(*b, QDate(2026, 7, 17)).has_value());
    }

    // An abandoned occurrence is skipped, not done: the task moves on but the
    // end-after-N count stays where it was.
    void abandonedOccurrenceDoesNotCount() {
        Task t;
        t.dueDate = QDate(2026, 7, 15);
        t.recurrence = rule(Repeat::Daily);
        t.recurrence.endType = Repeat::AfterCount;
        t.recurrence.endCount = 2;
        auto a = Recurrences::advance(t, QDate(2026, 7, 15), false);
        QVERIFY(a);
        QCOMPARE(a->dueDate, QDate(2026, 7, 16));
        QCOMPARE(a->recurrence.occurrencesDone, 0);
        QVERIFY(!a->abandoned);
        // Skipping any number of times never ends the series by count.
        auto b = Recurrences::advance(*a, QDate(2026, 7, 16), false);
        QVERIFY(b);
        auto c = Recurrences::advance(*b, QDate(2026, 7, 17), false);
        QVERIFY(c);
        QCOMPARE(c->recurrence.occurrencesDone, 0);
        // A done occurrence after skipped ones counts once.
        auto d = Recurrences::advance(*c, QDate(2026, 7, 18));
        QVERIFY(d);
        QCOMPARE(d->recurrence.occurrencesDone, 1);
    }

    // An end date still ends the series, abandoned or not.
    void abandonedOccurrenceStillEndsOnDate() {
        Task t;
        t.dueDate = QDate(2026, 7, 16);
        t.recurrence = rule(Repeat::Daily);
        t.recurrence.endType = Repeat::OnDate;
        t.recurrence.endDate = QDate(2026, 7, 16);
        QVERIFY(!Recurrences::advance(t, QDate(2026, 7, 16), false).has_value());
    }

    void endOnDate() {
        Task t;
        t.dueDate = QDate(2026, 7, 15);
        t.recurrence = rule(Repeat::Daily);
        t.recurrence.endType = Repeat::OnDate;
        t.recurrence.endDate = QDate(2026, 7, 17);
        auto a = Recurrences::advance(t, QDate(2026, 7, 15));
        QVERIFY(a);
        auto b = Recurrences::advance(*a, QDate(2026, 7, 16));
        QVERIFY(b);
        QCOMPARE(b->dueDate, QDate(2026, 7, 17));
        QVERIFY(!Recurrences::advance(*b, QDate(2026, 7, 17)).has_value());
    }

    void advanceWithNoFrequency() {
        Task t;
        t.dueDate = QDate(2026, 7, 15);
        QVERIFY(!Recurrences::advance(t, QDate(2026, 7, 15)).has_value());
    }

    void describeBasics() {
        QCOMPARE(Recurrences::describe(rule(Repeat::Daily)), QString("Daily"));
        QCOMPARE(Recurrences::describe(rule(Repeat::Daily, 3)), QString("Every 3 days"));
        QCOMPARE(Recurrences::describe(rule(Repeat::Weekdays)), QString("Weekdays"));
        QCOMPARE(Recurrences::describe(rule(Repeat::Weekly)), QString("Weekly"));
        QCOMPARE(Recurrences::describe(rule(Repeat::Weekly, 2)), QString("Every 2 weeks"));
        QCOMPARE(Recurrences::describe(rule(Repeat::Monthly)), QString("Monthly"));
        QCOMPARE(Recurrences::describe(rule(Repeat::Yearly)), QString("Yearly"));
        QCOMPARE(Recurrences::describe(rule(Repeat::Yearly, 2)), QString("Every 2 years"));
        const QString quarterly = Recurrences::describe(rule(Repeat::Monthly, 3));
        QVERIFY(quarterly == "Every 3 months" || quarterly == "Quarterly");
    }

    void describeWeekdaysAndOrdinals() {
        Recurrence r = rule(Repeat::Weekly);
        r.weekdays = Mon | Wed;
        QCOMPARE(Recurrences::describe(r), QString("Weekly on Mon, Wed"));
        r = rule(Repeat::Weekly, 2);
        r.weekdays = Fri;
        QCOMPARE(Recurrences::describe(r), QString("Every 2 weeks on Fri"));
        r = rule(Repeat::Monthly);
        r.monthOrdinal = 2;
        r.monthWeekday = 2;
        QCOMPARE(Recurrences::describe(r), QString("Monthly on the 2nd Tue"));
        r.monthOrdinal = -1;
        r.monthWeekday = 5;
        QCOMPARE(Recurrences::describe(r), QString("Monthly on the last Fri"));
    }

    void describeSuffixes() {
        Recurrence r = rule(Repeat::Yearly);
        r.from = Repeat::FromCompletion;
        QCOMPARE(Recurrences::describe(r), QString("Yearly · after completion"));
        r = rule(Repeat::Daily);
        r.endType = Repeat::AfterCount;
        r.endCount = 5;
        QCOMPARE(Recurrences::describe(r), QString("Daily · 5 times"));
        r = rule(Repeat::Weekly);
        r.endType = Repeat::OnDate;
        r.endDate = QDate(2026, 12, 31);
        QCOMPARE(Recurrences::describe(r), QString("Weekly · until 31 Dec 2026"));
    }

    void carryOverNotesDropsStampLines() {
        QCOMPARE(Recurrences::carryOverNotes("Call the bank\n2026-07-15 10:30\nAsk about fees"),
                 QString("Call the bank\nAsk about fees"));
        QCOMPARE(Recurrences::carryOverNotes("Plan\n[15 Jul 2026, 10:30 AM]\n\nRoom 12"), QString("Plan\n\nRoom 12"));
        QCOMPARE(Recurrences::carryOverNotes("Just words"), QString("Just words"));
    }
};

REGISTER_TEST(RecurrenceTest)
#include "test_recurrence.moc"
