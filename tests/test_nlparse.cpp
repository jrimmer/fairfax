#include <QtTest>

#include "dateparse.h"
#include "nlparse.h"
#include "testregistry.h"

namespace {

// Wednesday 15 July 2026, 10:30.
const QDateTime kNow(QDate(2026, 7, 15), QTime(10, 30));
constexpr int Mon = 1 << 0, Wed = 1 << 2, Fri = 1 << 4, Sat = 1 << 5, Sun = 1 << 6;

ParsedEntry parse(const QString &text, const QStringList &lists = {}, const QStringList &locations = {}) {
    return NaturalLanguage::parse(text, kNow, lists, locations);
}

int mins(int h, int m = 0) { return h * 60 + m; }

// The text each span covers, "kind:text", in order.
QStringList spanTexts(const QString &text, const ParsedEntry &e) {
    QStringList out;
    for (const auto &s : e.spans)
        out << s.kind + QLatin1Char(':') + text.mid(s.start, s.length);
    return out;
}

}  // namespace

class NlParseTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { DateParse::setNumericDayFirst(false); }
    void plainTitleUntouched() {
        const auto e = parse(QStringLiteral("Buy some bread"));
        QCOMPARE(e.title, QStringLiteral("Buy some bread"));
        QVERIFY(!e.dueDate.isValid() && !e.startDate.isValid());
        QCOMPARE(e.dueTime, -1);
        QCOMPARE(e.priority, -1);
        QVERIFY(e.spans.isEmpty());
        QVERIFY(!e.recurrence.isSet());
    }

    void emptyInput() {
        const auto e = parse(QString());
        QVERIFY(e.title.isEmpty());
        QVERIFY(e.spans.isEmpty());
    }

    // ---- Dates ----

    void today() {
        const auto e = parse(QStringLiteral("Call the plumber today"));
        QCOMPARE(e.title, QStringLiteral("Call the plumber"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        QVERIFY(!e.startDate.isValid());
        QCOMPARE(e.dueTime, -1);
    }

    void tomorrow() {
        const auto e = parse(QStringLiteral("Book a car service tomorrow"));
        QCOMPARE(e.title, QStringLiteral("Book a car service"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
    }

    void dayAfterTomorrow() {
        const auto e = parse(QStringLiteral("Collect the parcel day after tomorrow"));
        QCOMPARE(e.title, QStringLiteral("Collect the parcel"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        QCOMPARE(spanTexts(QStringLiteral("Collect the parcel day after tomorrow"), e),
                 QStringList{QStringLiteral("date:day after tomorrow")});
    }

    void weekdayIsNextOccurrence() {
        auto e = parse(QStringLiteral("Submit the timesheet Friday"));
        QCOMPARE(e.title, QStringLiteral("Submit the timesheet"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        // Today is a Wednesday, so "Wednesday" means next week.
        e = parse(QStringLiteral("Review Wednesday"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 22));
        e = parse(QStringLiteral("Review monday"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 20));
    }

    void nextWeekday() {
        auto e = parse(QStringLiteral("Book the dentist next Friday"));
        QCOMPARE(e.title, QStringLiteral("Book the dentist"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 24));
        e = parse(QStringLiteral("Meeting next Tuesday"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 21));
    }

    void nextWeekAndMonth() {
        auto e = parse(QStringLiteral("Plan the family trip next week"));
        QCOMPARE(e.title, QStringLiteral("Plan the family trip"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 20));
        e = parse(QStringLiteral("Renew the gym membership next month"));
        QCOMPARE(e.title, QStringLiteral("Renew the gym membership"));
        QCOMPARE(e.dueDate, QDate(2026, 8, 1));
    }

    void dayMonthForms() {
        auto e = parse(QStringLiteral("Pay the school fees on 5 September"));
        QCOMPARE(e.title, QStringLiteral("Pay the school fees"));
        QCOMPARE(e.dueDate, QDate(2026, 9, 5));
        e = parse(QStringLiteral("Order the textbooks September 5th"));
        QCOMPARE(e.title, QStringLiteral("Order the textbooks"));
        QCOMPARE(e.dueDate, QDate(2026, 9, 5));
        e = parse(QStringLiteral("Order Sep 5"));
        QCOMPARE(e.dueDate, QDate(2026, 9, 5));
        e = parse(QStringLiteral("Visit 3rd of October"));
        QCOMPARE(e.dueDate, QDate(2026, 10, 3));
        e = parse(QStringLiteral("Party 5 September 2027"));
        QCOMPARE(e.dueDate, QDate(2027, 9, 5));
        QCOMPARE(e.title, QStringLiteral("Party"));
    }

    void pastMonthDayRollsToNextYear() {
        const auto e = parse(QStringLiteral("Anniversary 3 March"));
        QCOMPARE(e.dueDate, QDate(2027, 3, 3));
    }

    void ordinalDay() {
        auto e = parse(QStringLiteral("Send the birthday card on the 5th"));
        QCOMPARE(e.title, QStringLiteral("Send the birthday card"));
        QCOMPARE(e.dueDate, QDate(2026, 8, 5));
        e = parse(QStringLiteral("Pay rent the 20th"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 20));
        e = parse(QStringLiteral("Pay rent on the 20"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 20));
    }

    void slashDate() {
        const auto e = parse(QStringLiteral("File the insurance claim 9/5"));
        QCOMPARE(e.title, QStringLiteral("File the insurance claim"));
        QCOMPARE(e.dueDate, QDate(2026, 9, 5));
        QVERIFY(!parse(QStringLiteral("Odd 13/45")).dueDate.isValid());
    }

    void inCount() {
        auto e = parse(QStringLiteral("Water the plants in 3 days"));
        QCOMPARE(e.title, QStringLiteral("Water the plants"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 18));
        e = parse(QStringLiteral("Review the household budget in 2 weeks"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 29));
        e = parse(QStringLiteral("Renew in 1 month"));
        QCOMPARE(e.dueDate, QDate(2026, 8, 15));
    }

    void leadWordsAreConsumed() {
        auto e = parse(QStringLiteral("Report by Friday"));
        QCOMPARE(e.title, QStringLiteral("Report"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        e = parse(QStringLiteral("Report due Friday"));
        QCOMPARE(e.title, QStringLiteral("Report"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        e = parse(QStringLiteral("Report is due tomorrow"));
        QCOMPARE(e.title, QStringLiteral("Report"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        e = parse(QStringLiteral("Mow the lawn on Saturday"));
        QCOMPARE(e.title, QStringLiteral("Mow the lawn"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 18));
    }

    void dateAtStartOfTitle() {
        const auto e = parse(QStringLiteral("tomorrow call Sam"));
        QCOMPARE(e.title, QStringLiteral("call Sam"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
    }

    // ---- Times ----

    void timeFormsWithDate() {
        auto e = parse(QStringLiteral("Ring the clinic tomorrow at 3pm"));
        QCOMPARE(e.title, QStringLiteral("Ring the clinic"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(15));
        e = parse(QStringLiteral("Dinner Friday 7:30pm"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        QCOMPARE(e.dueTime, mins(19, 30));
        e = parse(QStringLiteral("Dinner Friday 19:45"));
        QCOMPARE(e.dueTime, mins(19, 45));
        e = parse(QStringLiteral("Dinner Friday 7 pm"));
        QCOMPARE(e.dueTime, mins(19));
        QCOMPARE(e.title, QStringLiteral("Dinner"));
    }

    void timeWithoutDateIsNextOccurrence() {
        auto e = parse(QStringLiteral("Call mum at 3pm"));
        QCOMPARE(e.title, QStringLiteral("Call mum"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        QCOMPARE(e.dueTime, mins(15));
        e = parse(QStringLiteral("Take the medicine at 15:30"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        QCOMPARE(e.dueTime, mins(15, 30));
        // 9am has already passed at 10:30.
        e = parse(QStringLiteral("Stretch 9am"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(9));
        QCOMPARE(e.title, QStringLiteral("Stretch"));
    }

    void noonAndMidnight() {
        auto e = parse(QStringLiteral("Break for lunch at noon"));
        QCOMPARE(e.title, QStringLiteral("Break for lunch"));
        QCOMPARE(e.dueTime, mins(12));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        e = parse(QStringLiteral("Lock the front door at midnight"));
        QCOMPARE(e.dueTime, 0);
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        e = parse(QStringLiteral("Lunch Friday at 12pm"));
        QCOMPARE(e.dueTime, mins(12));
        e = parse(QStringLiteral("Lock up Friday at 12am"));
        QCOMPARE(e.dueTime, 0);
    }

    void bareHourAfterAt() {
        // 9 has passed this morning, so it means 9pm; 11 is still ahead.
        auto e = parse(QStringLiteral("Read at 9"));
        QCOMPARE(e.dueTime, mins(21));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        QCOMPARE(e.title, QStringLiteral("Read"));
        e = parse(QStringLiteral("Read at 11"));
        QCOMPARE(e.dueTime, mins(11));
        e = parse(QStringLiteral("Read tomorrow at 9"));
        QCOMPARE(e.dueTime, mins(9));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        e = parse(QStringLiteral("Train at 17"));
        QCOMPARE(e.dueTime, mins(17));
    }

    void dayParts() {
        auto e = parse(QStringLiteral("Run tomorrow morning"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(9));
        QCOMPARE(e.title, QStringLiteral("Run"));
        e = parse(QStringLiteral("Call Friday afternoon"));
        QCOMPARE(e.dueTime, mins(14));
        e = parse(QStringLiteral("Call Friday evening"));
        QCOMPARE(e.dueTime, mins(19));
        e = parse(QStringLiteral("Call Friday at night"));
        QCOMPARE(e.dueTime, mins(21));
        e = parse(QStringLiteral("Stargaze this evening"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        QCOMPARE(e.dueTime, mins(19));
        // A day part alone is just a word.
        e = parse(QStringLiteral("Good morning speech"));
        QCOMPARE(e.dueTime, -1);
        QCOMPARE(e.title, QStringLiteral("Good morning speech"));
    }

    void timeBeforeDate() {
        const auto e = parse(QStringLiteral("Dentist 3pm tomorrow"));
        QCOMPARE(e.title, QStringLiteral("Dentist"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(15));
    }

    // ---- Ranges, start and end ----

    void timeRange() {
        auto e = parse(QStringLiteral("Study session 1pm-3pm"));
        QCOMPARE(e.title, QStringLiteral("Study session"));
        QCOMPARE(e.startTime, mins(13));
        QCOMPARE(e.dueTime, mins(15));
        QCOMPARE(e.startDate, QDate(2026, 7, 15));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        e = parse(QStringLiteral("Join the food drive 9am-11am tomorrow"));
        QCOMPARE(e.title, QStringLiteral("Join the food drive"));
        QCOMPARE(e.startDate, QDate(2026, 7, 16));
        QCOMPARE(e.startTime, mins(9));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(11));
        e = parse(QStringLiteral("Study for the exam 9am-11am tomorrow #study"));
        QCOMPARE(e.title, QStringLiteral("Study for the exam"));
        QCOMPARE(e.tags, QStringList{QStringLiteral("study")});
    }

    void dateRange() {
        const QString text = QStringLiteral("Sand the garden fence 19 July to 21 July");
        const auto e = parse(text);
        QCOMPARE(e.title, QStringLiteral("Sand the garden fence"));
        QCOMPARE(e.startDate, QDate(2026, 7, 19));
        QCOMPARE(e.dueDate, QDate(2026, 7, 21));
        QCOMPARE(spanTexts(text, e), (QStringList{QStringLiteral("start:19 July"), QStringLiteral("date:21 July")}));
    }

    void dashedDayRange() {
        auto e = parse(QStringLiteral("Family visit September 5-7"));
        QCOMPARE(e.title, QStringLiteral("Family visit"));
        QCOMPARE(e.startDate, QDate(2026, 9, 5));
        QCOMPARE(e.dueDate, QDate(2026, 9, 7));
        e = parse(QStringLiteral("Family visit 5-7 September"));
        QCOMPARE(e.startDate, QDate(2026, 9, 5));
        QCOMPARE(e.dueDate, QDate(2026, 9, 7));
    }

    void rangeWithTimesOnEachEnd() {
        auto e = parse(QStringLiteral("Project starts tomorrow 2pm - 8 September 3:40pm"));
        QCOMPARE(e.title, QStringLiteral("Project"));
        QCOMPARE(e.startDate, QDate(2026, 7, 16));
        QCOMPARE(e.startTime, mins(14));
        QCOMPARE(e.dueDate, QDate(2026, 9, 8));
        QCOMPARE(e.dueTime, mins(15, 40));
        e = parse(QStringLiteral("Exam prep starting Monday 9am through Friday 5pm"));
        QCOMPARE(e.title, QStringLiteral("Exam prep"));
        QCOMPARE(e.startDate, QDate(2026, 7, 20));
        QCOMPARE(e.startTime, mins(9));
        QCOMPARE(e.dueDate, QDate(2026, 7, 24));
        QCOMPARE(e.dueTime, mins(17));
    }

    void fromUntil() {
        auto e = parse(QStringLiteral("Field trip from 19 July until 21 July"));
        QCOMPARE(e.title, QStringLiteral("Field trip"));
        QCOMPARE(e.startDate, QDate(2026, 7, 19));
        QCOMPARE(e.dueDate, QDate(2026, 7, 21));
        e = parse(QStringLiteral("Practise the piano piece from Monday until Friday #study"));
        QCOMPARE(e.title, QStringLiteral("Practise the piano piece"));
        QCOMPARE(e.startDate, QDate(2026, 7, 20));
        QCOMPARE(e.dueDate, QDate(2026, 7, 24));
        e = parse(QStringLiteral("Course from Monday to Friday"));
        QCOMPARE(e.startDate, QDate(2026, 7, 20));
        QCOMPARE(e.dueDate, QDate(2026, 7, 24));
    }

    void startWords() {
        const QStringList words = {QStringLiteral("starting"), QStringLiteral("starts"), QStringLiteral("start"),
                                   QStringLiteral("begins"),   QStringLiteral("beginning"), QStringLiteral("from")};
        for (const QString &word : words) {
            const auto e = parse(QStringLiteral("Work on the report ") + word + QStringLiteral(" tomorrow"));
            QCOMPARE(e.title, QStringLiteral("Work on the report"));
            QCOMPARE(e.startDate, QDate(2026, 7, 16));
            QVERIFY(!e.dueDate.isValid());
        }
        const auto e = parse(QStringLiteral("Volunteer shift from 5 September"));
        QCOMPARE(e.title, QStringLiteral("Volunteer shift"));
        QCOMPARE(e.startDate, QDate(2026, 9, 5));
        QVERIFY(!e.dueDate.isValid());
    }

    void startThenEnd() {
        auto e = parse(QStringLiteral("Tidy the store room starting tomorrow ending Friday"));
        QCOMPARE(e.title, QStringLiteral("Tidy the store room"));
        QCOMPARE(e.startDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        e = parse(QStringLiteral("Set up the stall starting tomorrow at 3pm ending Saturday at 5pm"));
        QCOMPARE(e.title, QStringLiteral("Set up the stall"));
        QCOMPARE(e.startTime, mins(15));
        QCOMPARE(e.dueDate, QDate(2026, 7, 18));
        QCOMPARE(e.dueTime, mins(17));
        e = parse(QStringLiteral("Essay begins Monday, due Friday"));
        QCOMPARE(e.title, QStringLiteral("Essay"));
        QCOMPARE(e.startDate, QDate(2026, 7, 20));
        QCOMPARE(e.dueDate, QDate(2026, 7, 24));
        e = parse(QStringLiteral("Attic sort-out starts Saturday 10am and finishes Sunday 4pm"));
        QCOMPARE(e.title, QStringLiteral("Attic sort-out"));
        QCOMPARE(e.startDate, QDate(2026, 7, 18));
        QCOMPARE(e.startTime, mins(10));
        QCOMPARE(e.dueDate, QDate(2026, 7, 19));
        QCOMPARE(e.dueTime, mins(16));
        e = parse(QStringLiteral("Redecorate begins 5 September and is due 8 September"));
        QCOMPARE(e.title, QStringLiteral("Redecorate"));
        QCOMPARE(e.startDate, QDate(2026, 9, 5));
        QCOMPARE(e.dueDate, QDate(2026, 9, 8));
    }

    void endWordWithoutStartStaysInTitle() {
        auto e = parse(QStringLiteral("Finish report ending Friday"));
        QCOMPARE(e.title, QStringLiteral("Finish report ending"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        QVERIFY(!e.startDate.isValid());
        e = parse(QStringLiteral("Finish report due Friday"));
        QCOMPARE(e.title, QStringLiteral("Finish report"));
    }

    void startKindSpan() {
        const QString text = QStringLiteral("Tidy starting tomorrow ending Friday");
        const auto e = parse(text);
        QCOMPARE(spanTexts(text, e), (QStringList{QStringLiteral("start:tomorrow"), QStringLiteral("date:Friday")}));
    }

    void fromAsOrdinaryWord() {
        const auto e = parse(QStringLiteral("Get a letter from Sam"));
        QCOMPARE(e.title, QStringLiteral("Get a letter from Sam"));
        QVERIFY(!e.startDate.isValid());
    }

    // ---- Repeats ----

    void repeatSimple() {
        auto e = parse(QStringLiteral("Take vitamins tomorrow every day"));
        QCOMPARE(e.title, QStringLiteral("Take vitamins"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Daily));
        QCOMPARE(e.recurrence.interval, 1);
        e = parse(QStringLiteral("Stretch daily"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Daily));
        QCOMPARE(e.title, QStringLiteral("Stretch"));
        e = parse(QStringLiteral("Weigh in weekly"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekly));
        e = parse(QStringLiteral("Budget every month"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Monthly));
        e = parse(QStringLiteral("Budget monthly"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Monthly));
        e = parse(QStringLiteral("Review every year"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Yearly));
        e = parse(QStringLiteral("Review yearly"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Yearly));
        e = parse(QStringLiteral("Review every week"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(e.recurrence.weekdays, 0);
    }

    void repeatWithoutDateLeavesDatesUnset() {
        const auto e = parse(QStringLiteral("Submit the timesheet every weekday"));
        QCOMPARE(e.title, QStringLiteral("Submit the timesheet"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekdays));
        QVERIFY(!e.dueDate.isValid());
        QVERIFY(!e.startDate.isValid());
    }

    void repeatWeekdays() {
        auto e = parse(QStringLiteral("Team review every Monday"));
        QCOMPARE(e.title, QStringLiteral("Team review"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(e.recurrence.weekdays, Mon);
        QVERIFY(!e.dueDate.isValid());
        e = parse(QStringLiteral("Visit the parents every Monday and Wednesday"));
        QCOMPARE(e.title, QStringLiteral("Visit the parents"));
        QCOMPARE(e.recurrence.weekdays, Mon | Wed);
        e = parse(QStringLiteral("Gym every Monday, Wednesday and Friday"));
        QCOMPARE(e.title, QStringLiteral("Gym"));
        QCOMPARE(e.recurrence.weekdays, Mon | Wed | Fri);
        e = parse(QStringLiteral("Hike every weekend"));
        QCOMPARE(e.recurrence.weekdays, Sat | Sun);
    }

    void repeatInterval() {
        auto e = parse(QStringLiteral("Change the water filter Friday every 2 weeks"));
        QCOMPARE(e.title, QStringLiteral("Change the water filter"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 17));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(e.recurrence.interval, 2);
        e = parse(QStringLiteral("Water the plants Saturday every 2 weeks"));
        QCOMPARE(e.recurrence.interval, 2);
        e = parse(QStringLiteral("Mop every other week"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(e.recurrence.interval, 2);
        e = parse(QStringLiteral("Mop every 3 days"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Daily));
        QCOMPARE(e.recurrence.interval, 3);
        e = parse(QStringLiteral("Mop every other Friday"));
        QCOMPARE(e.recurrence.interval, 2);
        QCOMPARE(e.recurrence.weekdays, Fri);
    }

    void repeatMonthOnDay() {
        auto e = parse(QStringLiteral("Pay the rent every month on the 15th"));
        QCOMPARE(e.title, QStringLiteral("Pay the rent"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Monthly));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        e = parse(QStringLiteral("Pay the rent every month on the 3rd"));
        QCOMPARE(e.dueDate, QDate(2026, 8, 3));
        e = parse(QStringLiteral("Meetup every month on the third Friday"));
        QCOMPARE(e.title, QStringLiteral("Meetup"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Monthly));
        QCOMPARE(e.recurrence.monthOrdinal, 3);
        QCOMPARE(e.recurrence.monthWeekday, 5);
        e = parse(QStringLiteral("Meetup every last Friday"));
        QCOMPARE(e.recurrence.monthOrdinal, -1);
        QCOMPARE(e.recurrence.monthWeekday, 5);
    }

    void repeatYearOnDate() {
        auto e = parse(QStringLiteral("Renew the membership every year on September 5"));
        QCOMPARE(e.title, QStringLiteral("Renew the membership"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Yearly));
        QCOMPARE(e.dueDate, QDate(2026, 9, 5));
        e = parse(QStringLiteral("Renew the membership September 5 every year"));
        QCOMPARE(e.title, QStringLiteral("Renew the membership"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Yearly));
        QCOMPARE(e.dueDate, QDate(2026, 9, 5));
    }

    void repeatWithTime() {
        const auto e = parse(QStringLiteral("Read to the children every day at 8pm"));
        QCOMPARE(e.title, QStringLiteral("Read to the children"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Daily));
        QCOMPARE(e.dueTime, mins(20));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
    }

    void everyAsOrdinaryWord() {
        auto e = parse(QStringLiteral("Thank every person"));
        QCOMPARE(e.title, QStringLiteral("Thank every person"));
        QVERIFY(!e.recurrence.isSet());
        e = parse(QStringLiteral("Do it every"));
        QVERIFY(!e.recurrence.isSet());
    }

    // ---- Alerts ----

    void alerts() {
        auto e = parse(QStringLiteral("Ring the clinic tomorrow at 3pm alert 30m"));
        QCOMPARE(e.title, QStringLiteral("Ring the clinic"));
        QCOMPARE(e.alertOffsets, QList<int>{30});
        QCOMPARE(e.dueTime, mins(15));
        e = parse(QStringLiteral("Send the invoice Thursday at 5pm alarm 1 hour before"));
        QCOMPARE(e.title, QStringLiteral("Send the invoice"));
        QCOMPARE(e.alertOffsets, QList<int>{60});
        e = parse(QStringLiteral("Fetch the parcel Saturday at 4pm alert 15m, 45m"));
        QCOMPARE(e.title, QStringLiteral("Fetch the parcel"));
        QCOMPARE(e.alertOffsets, (QList<int>{15, 45}));
        e = parse(QStringLiteral("Call Friday at 3pm remind me 2h before"));
        QCOMPARE(e.title, QStringLiteral("Call"));
        QCOMPARE(e.alertOffsets, QList<int>{120});
        e = parse(QStringLiteral("Book a boiler check next Friday at 9am alert 1h"));
        QCOMPARE(e.alertOffsets, QList<int>{60});
        QCOMPARE(e.dueDate, QDate(2026, 7, 24));
        e = parse(QStringLiteral("Call Friday alert 1d"));
        QCOMPARE(e.alertOffsets, QList<int>{1440});
        e = parse(QStringLiteral("Call Friday alert 1h30m"));
        QCOMPARE(e.alertOffsets, QList<int>{90});
    }

    void alertSpan() {
        const QString text = QStringLiteral("Call Friday alert 10m, 30m before");
        const auto e = parse(text);
        QCOMPARE(e.alertOffsets, (QList<int>{10, 30}));
        QCOMPARE(spanTexts(text, e), (QStringList{QStringLiteral("date:Friday"), QStringLiteral("alert:alert 10m, 30m before")}));
    }

    void alertWordsWithoutAmountStay() {
        auto e = parse(QStringLiteral("Be alert at work"));
        QCOMPARE(e.title, QStringLiteral("Be alert at work"));
        QVERIFY(e.alertOffsets.isEmpty());
        e = parse(QStringLiteral("Remind me to call Sam"));
        QCOMPARE(e.title, QStringLiteral("Remind me to call Sam"));
    }

    // ---- Duration ----

    void durations() {
        auto e = parse(QStringLiteral("Review the lesson plan [45m]"));
        QCOMPARE(e.title, QStringLiteral("Review the lesson plan"));
        QCOMPARE(e.duration, 45);
        e = parse(QStringLiteral("Deep clean the kitchen [2h]"));
        QCOMPARE(e.duration, 120);
        e = parse(QStringLiteral("Hike [1h30m]"));
        QCOMPARE(e.duration, 90);
        e = parse(QStringLiteral("Road trip [1d]"));
        QCOMPARE(e.duration, 1440);
        e = parse(QStringLiteral("Hike [1h 30m] today"));
        QCOMPARE(e.duration, 90);
        QCOMPARE(e.title, QStringLiteral("Hike"));
    }

    void bracketsThatAreNotDurations() {
        const auto e = parse(QStringLiteral("Fix bug [urgent] and [x]"));
        QCOMPARE(e.title, QStringLiteral("Fix bug [urgent] and [x]"));
        QCOMPARE(e.duration, 0);
    }

    // ---- Priority and star ----

    void priority() {
        auto e = parse(QStringLiteral("Buy milk !"));
        QCOMPARE(e.title, QStringLiteral("Buy milk"));
        QCOMPARE(e.priority, 1);
        e = parse(QStringLiteral("Return the library books !!"));
        QCOMPARE(e.priority, 2);
        e = parse(QStringLiteral("Pay the electricity bill !!!"));
        QCOMPARE(e.priority, 3);
        QCOMPARE(e.title, QStringLiteral("Pay the electricity bill"));
    }

    void exclamationInsideWordsIsKept() {
        auto e = parse(QStringLiteral("Say hello!"));
        QCOMPARE(e.title, QStringLiteral("Say hello!"));
        QCOMPARE(e.priority, -1);
        e = parse(QStringLiteral("Say hello!!"));
        QCOMPARE(e.title, QStringLiteral("Say hello!!"));
        QCOMPARE(e.priority, -1);
        e = parse(QStringLiteral("Wow!!! great"));
        QCOMPARE(e.priority, -1);
    }

    void starAndPriority() {
        auto e = parse(QStringLiteral("Call mum *"));
        QCOMPARE(e.title, QStringLiteral("Call mum"));
        QVERIFY(e.starred);
        QCOMPARE(e.priority, -1);
        e = parse(QStringLiteral("Send the school form !!! *"));
        QCOMPARE(e.title, QStringLiteral("Send the school form"));
        QCOMPARE(e.priority, 3);
        QVERIFY(e.starred);
        e = parse(QStringLiteral("Multiply 3*4"));
        QVERIFY(!e.starred);
        QCOMPARE(e.title, QStringLiteral("Multiply 3*4"));
    }

    // ---- Tags ----

    void tags() {
        auto e = parse(QStringLiteral("Plan the week #work"));
        QCOMPARE(e.title, QStringLiteral("Plan the week"));
        QCOMPARE(e.tags, QStringList{QStringLiteral("work")});
        e = parse(QStringLiteral("Buy milk #errands #shopping"));
        QCOMPARE(e.title, QStringLiteral("Buy milk"));
        QCOMPARE(e.tags, (QStringList{QStringLiteral("errands"), QStringLiteral("shopping")}));
        e = parse(QStringLiteral("Buy milk #to-do_1, #b2"));
        QCOMPARE(e.tags, (QStringList{QStringLiteral("to-do_1"), QStringLiteral("b2")}));
        QCOMPARE(e.title, QStringLiteral("Buy milk"));
    }

    void tagsRunTogetherStayInTitle() {
        auto e = parse(QStringLiteral("Chase #sales#urgent"));
        QCOMPARE(e.title, QStringLiteral("Chase #sales#urgent"));
        QVERIFY(e.tags.isEmpty());
        e = parse(QStringLiteral("Learn C# today"));
        QCOMPARE(e.title, QStringLiteral("Learn C#"));
        QVERIFY(e.tags.isEmpty());
        e = parse(QStringLiteral("Lone # hash"));
        QVERIFY(e.tags.isEmpty());
    }

    // ---- Lists ----

    void listPrefixMatch() {
        const QStringList lists = {QStringLiteral("Errands"), QStringLiteral("Home Projects"), QStringLiteral("Health")};
        auto e = parse(QStringLiteral("Buy milk /errands"), lists);
        QCOMPARE(e.title, QStringLiteral("Buy milk"));
        QCOMPARE(e.list, QStringLiteral("Errands"));
        e = parse(QStringLiteral("Clear out the loft /homeproj"), lists);
        QCOMPARE(e.title, QStringLiteral("Clear out the loft"));
        QCOMPARE(e.list, QStringLiteral("Home Projects"));
        e = parse(QStringLiteral("Book the dentist /Health/ next Tuesday"), lists);
        QCOMPARE(e.title, QStringLiteral("Book the dentist"));
        QCOMPARE(e.list, QStringLiteral("Health"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 21));
        e = parse(QStringLiteral("Plan the family trip next week /Home Projects/"), lists);
        QCOMPARE(e.title, QStringLiteral("Plan the family trip"));
        QCOMPARE(e.list, QStringLiteral("Home Projects"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 20));
    }

    void listUnclosedMultiWordIsGreedy() {
        const QStringList lists = {QStringLiteral("Home Projects"), QStringLiteral("Work")};
        auto e = parse(QStringLiteral("Paint /home projects"), lists);
        QCOMPARE(e.title, QStringLiteral("Paint"));
        QCOMPARE(e.list, QStringLiteral("Home Projects"));
        // Words that do not continue a list name are left alone.
        e = parse(QStringLiteral("Paint /home tomorrow"), lists);
        QCOMPARE(e.list, QStringLiteral("Home Projects"));
        QCOMPARE(e.title, QStringLiteral("Paint"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
    }

    void listWithoutMatchStaysInTitle() {
        const QStringList lists = {QStringLiteral("Errands")};
        auto e = parse(QStringLiteral("Buy milk /groceries now"), lists);
        QCOMPARE(e.title, QStringLiteral("Buy milk /groceries now"));
        QVERIFY(e.list.isEmpty());
        e = parse(QStringLiteral("Buy milk /errands"));
        QCOMPARE(e.title, QStringLiteral("Buy milk /errands"));
        QVERIFY(e.list.isEmpty());
    }

    void listIgnoresPathsAndUrls() {
        const QStringList lists = {QStringLiteral("Usr"), QStringLiteral("Errands")};
        auto e = parse(QStringLiteral("Check /usr/bin/env"), lists);
        QCOMPARE(e.title, QStringLiteral("Check /usr/bin/env"));
        QVERIFY(e.list.isEmpty());
        e = parse(QStringLiteral("Read https://example.com/errands"), lists);
        QCOMPARE(e.title, QStringLiteral("Read https://example.com/errands"));
        QVERIFY(e.list.isEmpty());
    }

    // ---- Locations ----

    void locations() {
        const QStringList places = {QStringLiteral("Pharmacy"), QStringLiteral("Central Library"), QStringLiteral("Central")};
        auto e = parse(QStringLiteral("Collect the prescription @ Pharmacy"), {}, places);
        QCOMPARE(e.title, QStringLiteral("Collect the prescription"));
        QCOMPARE(e.location, QStringLiteral("Pharmacy"));
        e = parse(QStringLiteral("Meet Amina @centrallibrary tomorrow at 4pm"), {}, places);
        QCOMPARE(e.title, QStringLiteral("Meet Amina"));
        QCOMPARE(e.location, QStringLiteral("Central Library"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(16));
        e = parse(QStringLiteral("Return the DVDs at @ Central Library and buy eggs"), {}, places);
        QCOMPARE(e.title, QStringLiteral("Return the DVDs at and buy eggs"));
        QCOMPARE(e.location, QStringLiteral("Central Library"));
        e = parse(QStringLiteral("Collect the prescription @ pharmacy #errands"), {}, places);
        QCOMPARE(e.location, QStringLiteral("Pharmacy"));
        QCOMPARE(e.tags, QStringList{QStringLiteral("errands")});
    }

    void locationMustMatchFullName() {
        const QStringList places = {QStringLiteral("Central Library")};
        auto e = parse(QStringLiteral("Meet @central today"), {}, places);
        QVERIFY(e.location.isEmpty());
        QCOMPARE(e.title, QStringLiteral("Meet @central"));
        e = parse(QStringLiteral("Meet @ Nowhere"), {}, places);
        QVERIFY(e.location.isEmpty());
        QCOMPARE(e.title, QStringLiteral("Meet @ Nowhere"));
        e = parse(QStringLiteral("Meet @ Pharmacy"));
        QVERIFY(e.location.isEmpty());
    }

    void locationLongestWins() {
        const QStringList places = {QStringLiteral("Central"), QStringLiteral("Central Library")};
        const auto e = parse(QStringLiteral("Study @ Central Library"), {}, places);
        QCOMPARE(e.location, QStringLiteral("Central Library"));
        QCOMPARE(e.title, QStringLiteral("Study"));
    }

    // ---- Quotes ----

    void quotedWordsAreLiteral() {
        auto e = parse(QStringLiteral("Read \"tomorrow\""));
        QCOMPARE(e.title, QStringLiteral("Read tomorrow"));
        QVERIFY(!e.dueDate.isValid());
        QVERIFY(e.spans.isEmpty());
        e = parse(QStringLiteral("Read \"tomorrow\" tomorrow"));
        QCOMPARE(e.title, QStringLiteral("Read tomorrow"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
    }

    void quotedPhrasesAreNeverParsed() {
        const auto e = parse(QStringLiteral("Say \"call me at 3pm #tag !!\" loudly"));
        QCOMPARE(e.title, QStringLiteral("Say call me at 3pm #tag !! loudly"));
        QCOMPARE(e.dueTime, -1);
        QVERIFY(e.tags.isEmpty());
        QCOMPARE(e.priority, -1);
    }

    void quotedWordBlocksMultiWordPhrases() {
        const auto e = parse(QStringLiteral("Do \"next\" week"));
        QCOMPARE(e.title, QStringLiteral("Do next week"));
        QVERIFY(!e.dueDate.isValid());
    }

    void quoteSpansUseOriginalPositions() {
        const QString text = QStringLiteral("Read \"tomorrow\" tomorrow");
        const auto e = parse(text);
        QCOMPARE(e.spans.size(), 1);
        QCOMPARE(e.spans[0].start, 16);
        QCOMPARE(e.spans[0].length, 8);
    }

    // ---- Over-matching ----

    void monthNameWithoutDayIsNotADate() {
        auto e = parse(QStringLiteral("Meet May at the station"));
        QCOMPARE(e.title, QStringLiteral("Meet May at the station"));
        QVERIFY(!e.dueDate.isValid());
        QCOMPARE(e.dueTime, -1);
        e = parse(QStringLiteral("Visit in September"));
        QVERIFY(!e.dueDate.isValid());
        QCOMPARE(e.title, QStringLiteral("Visit in September"));
        e = parse(QStringLiteral("I think 5 may work"));
        QVERIFY(!e.dueDate.isValid());
    }

    void bareNumbersAreNotDates() {
        auto e = parse(QStringLiteral("Read chapter 5"));
        QCOMPARE(e.title, QStringLiteral("Read chapter 5"));
        QVERIFY(!e.dueDate.isValid());
        e = parse(QStringLiteral("Buy 12 eggs"));
        QCOMPARE(e.title, QStringLiteral("Buy 12 eggs"));
        QVERIFY(!e.dueDate.isValid());
        QCOMPARE(e.dueTime, -1);
        e = parse(QStringLiteral("Read the 5th chapter"));  // "the 5th" is deliberately a date.
        QCOMPARE(e.dueDate, QDate(2026, 8, 5));
        e = parse(QStringLiteral("Take 5th Avenue"));
        QVERIFY(!e.dueDate.isValid());
        e = parse(QStringLiteral("Meet at the station"));
        QCOMPARE(e.title, QStringLiteral("Meet at the station"));
        QCOMPARE(e.dueTime, -1);
    }

    void wordsInsideOtherWordsNeverMatch() {
        for (const QString &text : {QStringLiteral("Todays plan"), QStringLiteral("Fridays off"), QStringLiteral("Friday's plan"),
                                    QStringLiteral("Tomorrowland tickets"), QStringLiteral("Nextel call"),
                                    QStringLiteral("Mondayish")}) {
            const auto e = parse(text);
            QCOMPARE(e.title, text);
            QVERIFY(!e.dueDate.isValid());
        }
        auto e = parse(QStringLiteral("Buy #tags!bad"));
        QVERIFY(e.tags.isEmpty());
        e = parse(QStringLiteral("alerting 30m"));
        QVERIFY(e.alertOffsets.isEmpty());
        e = parse(QStringLiteral("Get Pat's 3pmish"));
        QCOMPARE(e.dueTime, -1);
    }

    void weekdayAbbreviationsAreNotDates() {
        const auto e = parse(QStringLiteral("The cat sat on the mat"));
        QCOMPARE(e.title, QStringLiteral("The cat sat on the mat"));
        QVERIFY(!e.dueDate.isValid());
    }

    void secondDateIsLeftAlone() {
        const auto e = parse(QStringLiteral("Call tomorrow or Friday"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.title, QStringLiteral("Call or Friday"));
    }

    // ---- Title and spans ----

    void titleKeepsOtherWhitespaceTidy() {
        const auto e = parse(QStringLiteral("  Call   the   dentist    tomorrow   "));
        QCOMPARE(e.title, QStringLiteral("Call the dentist"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
    }

    void titleKeepsNonTokenWords() {
        const auto e = parse(QStringLiteral("tomorrow !! #x"));
        QVERIFY(e.title.isEmpty());
        QCOMPARE(e.priority, 2);
    }

    void spansUseOriginalPositions() {
        const QString text = QStringLiteral("Call Sam tomorrow at 3pm #family !! * [30m]");
        const auto e = parse(text);
        QCOMPARE(spanTexts(text, e),
                 (QStringList{QStringLiteral("date:tomorrow"), QStringLiteral("time:3pm"), QStringLiteral("tag:#family"),
                              QStringLiteral("priority:!!"), QStringLiteral("star:*"), QStringLiteral("duration:[30m]")}));
        QCOMPARE(e.spans[0].start, 9);
        QCOMPARE(e.spans[0].length, 8);
        QCOMPARE(e.spans[1].start, 21);
        QCOMPARE(e.spans[1].length, 3);
    }

    void spansForEveryKind() {
        const QString text = QStringLiteral("Drop off tomorrow every Monday alert 5m /fam @ Park");
        const auto e = parse(text, {QStringLiteral("Family")}, {QStringLiteral("Park")});
        QCOMPARE(spanTexts(text, e),
                 (QStringList{QStringLiteral("date:tomorrow"), QStringLiteral("repeat:every Monday"),
                              QStringLiteral("alert:alert 5m"), QStringLiteral("list:/fam"), QStringLiteral("location:@ Park")}));
    }

    void spansAreSortedAndDoNotOverlap() {
        const QString text = QStringLiteral("#a Call Friday at 5pm every day !");
        const auto e = parse(text);
        for (int i = 1; i < e.spans.size(); ++i)
            QVERIFY(e.spans[i - 1].start + e.spans[i - 1].length <= e.spans[i].start);
        QCOMPARE(e.spans.size(), 5);
    }

    void trailingPunctuationOutsideSpan() {
        const QString text = QStringLiteral("Call Sam tomorrow, then relax");
        const auto e = parse(text);
        QCOMPARE(spanTexts(text, e), QStringList{QStringLiteral("date:tomorrow")});
        QCOMPARE(e.title, QStringLiteral("Call Sam then relax"));
    }

    // ---- Combined sentences ----

    void combinedSentence() {
        const QString text = QStringLiteral("Call Sam tomorrow at 3pm #family !! every Monday [30m] /fam");
        const auto e = parse(text, {QStringLiteral("Family"), QStringLiteral("Work")});
        QCOMPARE(e.title, QStringLiteral("Call Sam"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(15));
        QCOMPARE(e.tags, QStringList{QStringLiteral("family")});
        QCOMPARE(e.priority, 2);
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(e.recurrence.weekdays, Mon);
        QCOMPARE(e.duration, 30);
        QCOMPARE(e.list, QStringLiteral("Family"));
        QCOMPARE(spanTexts(text, e),
                 (QStringList{QStringLiteral("date:tomorrow"), QStringLiteral("time:3pm"), QStringLiteral("tag:#family"),
                              QStringLiteral("priority:!!"), QStringLiteral("repeat:every Monday"),
                              QStringLiteral("duration:[30m]"), QStringLiteral("list:/fam")}));
    }

    void combinedManualExamples() {
        auto e = parse(QStringLiteral("Meet Anna at the station tomorrow at 3pm #family"));
        QCOMPARE(e.title, QStringLiteral("Meet Anna at the station"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 16));
        QCOMPARE(e.dueTime, mins(15));
        QCOMPARE(e.tags, QStringList{QStringLiteral("family")});

        e = parse(QStringLiteral("Call the landlord tomorrow at 11am !! *"));
        QCOMPARE(e.title, QStringLiteral("Call the landlord"));
        QCOMPARE(e.dueTime, mins(11));
        QCOMPARE(e.priority, 2);
        QVERIFY(e.starred);

        e = parse(QStringLiteral("Deep clean the kitchen Saturday [2h] #home"));
        QCOMPARE(e.title, QStringLiteral("Deep clean the kitchen"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 18));
        QCOMPARE(e.duration, 120);
        QCOMPARE(e.tags, QStringList{QStringLiteral("home")});

        e = parse(QStringLiteral("Submit the timesheet every weekday at 5pm"));
        QCOMPARE(e.title, QStringLiteral("Submit the timesheet"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Weekdays));
        QCOMPARE(e.dueTime, mins(17));

        e = parse(QStringLiteral("Book the dentist in 3 days #health"));
        QCOMPARE(e.title, QStringLiteral("Book the dentist"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 18));

        e = parse(QStringLiteral("Drop the donation off Friday at 1pm #community"));
        QCOMPARE(e.dueTime, mins(13));
        QCOMPARE(e.title, QStringLiteral("Drop the donation off"));

        e = parse(QStringLiteral("Sand the garden fence 19 July to 21 July !!"));
        QCOMPARE(e.title, QStringLiteral("Sand the garden fence"));
        QCOMPARE(e.startDate, QDate(2026, 7, 19));
        QCOMPARE(e.dueDate, QDate(2026, 7, 21));
        QCOMPARE(e.priority, 2);

        e = parse(QStringLiteral("Paint the garden shed starting Saturday #home"));
        QCOMPARE(e.title, QStringLiteral("Paint the garden shed"));
        QCOMPARE(e.startDate, QDate(2026, 7, 18));
        QVERIFY(!e.dueDate.isValid());

        e = parse(QStringLiteral("Repaint the landing begins 6 September, due 9 September !!"));
        QCOMPARE(e.title, QStringLiteral("Repaint the landing"));
        QCOMPARE(e.startDate, QDate(2026, 9, 6));
        QCOMPARE(e.dueDate, QDate(2026, 9, 9));
        QCOMPARE(e.priority, 2);

        e = parse(QStringLiteral("Renew the van insurance every year on September 7 #home"));
        QCOMPARE(e.title, QStringLiteral("Renew the van insurance"));
        QCOMPARE(e.dueDate, QDate(2026, 9, 7));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Yearly));
    }

    void firstMarkerWins() {
        const auto e = parse(QStringLiteral("Do it ! !!! [10m] [20m]"));
        QCOMPARE(e.priority, 1);
        QCOMPARE(e.duration, 10);
    }

    // ---- Locale order, quarters, end-of-period words, bare ordinals ----

    void slashDateDayFirst() {
        DateParse::setNumericDayFirst(true);
        auto e = parse(QStringLiteral("File the claim 9/5"));
        DateParse::setNumericDayFirst(false);
        QCOMPARE(e.dueDate, QDate(2027, 5, 9));
    }

    void quarterlyRepeats() {
        auto e = parse(QStringLiteral("Pay estimated tax quarterly"));
        QCOMPARE(e.title, QStringLiteral("Pay estimated tax"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Monthly));
        QCOMPARE(e.recurrence.interval, 3);
        e = parse(QStringLiteral("Review goals every quarter"));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Monthly));
        QCOMPARE(e.recurrence.interval, 3);
        e = parse(QStringLiteral("Review goals every other quarter"));
        QCOMPARE(e.recurrence.interval, 6);
    }

    void endOfPeriods() {
        auto e = parse(QStringLiteral("Close the books eom"));
        QCOMPARE(e.title, QStringLiteral("Close the books"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 31));
        e = parse(QStringLiteral("Annual review eoy"));
        QCOMPARE(e.dueDate, QDate(2026, 12, 31));
        e = parse(QStringLiteral("Annual review by end of year"));
        QCOMPARE(e.title, QStringLiteral("Annual review"));
        QCOMPARE(e.dueDate, QDate(2026, 12, 31));
        e = parse(QStringLiteral("Invoices end of the month"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 31));
    }

    void tonightKeepsEveningTime() {
        const auto e = parse(QStringLiteral("Call mum tonight"));
        QCOMPARE(e.title, QStringLiteral("Call mum"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        QCOMPARE(e.dueTime, mins(21));
    }

    void bareOrdinalIsNextSuchDay() {
        auto e = parse(QStringLiteral("Pay rent 15th"));
        QCOMPARE(e.title, QStringLiteral("Pay rent"));
        QCOMPARE(e.dueDate, QDate(2026, 7, 15));
        e = parse(QStringLiteral("Pay rent 3rd"));
        QCOMPARE(e.dueDate, QDate(2026, 8, 3));
        // Mid-sentence ordinals are ordinary words.
        e = parse(QStringLiteral("Visit 3rd floor"));
        QCOMPARE(e.title, QStringLiteral("Visit 3rd floor"));
        QVERIFY(!parse(QStringLiteral("Lunch at 5th Avenue deli")).dueDate.isValid());
    }

    void locationLongestNameWins() {
        const QStringList places{"Home", "Home Depot"};
        auto e = parse(QStringLiteral("Buy screws @Home Depot"), {}, places);
        QCOMPARE(e.location, QStringLiteral("Home Depot"));
        QCOMPARE(e.title, QStringLiteral("Buy screws"));
        e = parse(QStringLiteral("Buy screws @Home soon"), {}, places);
        QCOMPARE(e.location, QStringLiteral("Home"));
        QCOMPARE(e.title, QStringLiteral("Buy screws soon"));
    }
};

REGISTER_TEST(NlParseTest)
#include "test_nlparse.moc"
