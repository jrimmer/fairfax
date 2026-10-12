#include <QtTest>

#include "dateparse.h"
#include "testregistry.h"

namespace {

// Wednesday 15 July 2026, 10:30.
const QDateTime kNow(QDate(2026, 7, 15), QTime(10, 30));

QList<DateSuggestion> sug(const QString &text, int limit = 6) { return DateParse::suggest(text, kNow, limit); }

QStringList labels(const QString &text, int limit = 6) {
    QStringList out;
    for (const auto &s : sug(text, limit))
        out << s.label;
    return out;
}

DateSuggestion best(const QString &text) { return DateParse::best(text, kNow); }

void check(const QString &text, const QDate &date, int minutes) {
    const auto s = best(text);
    QVERIFY2(s.date == date && s.minutes == minutes,
             qPrintable(QStringLiteral("%1 -> %2 %3 (%4)").arg(text, s.date.toString(Qt::ISODate)).arg(s.minutes).arg(s.label)));
}

}  // namespace

class DateParseTest : public QObject {
    Q_OBJECT
private slots:
    void emptyAndGarbage() {
        QVERIFY(sug("").isEmpty());
        QVERIFY(sug("   ").isEmpty());
        QVERIFY(sug("zzz qqq").isEmpty());
        QVERIFY(!best("zzz").date.isValid());
        QCOMPARE(best("zzz").minutes, -1);
    }

    void prefixMatching() {
        // Thursday is tomorrow, so de-duplication leaves three.
        QCOMPARE(labels("t"), QStringList({"Today", "Tomorrow", "Tue, 21 Jul"}));
        const QDateTime sunday(QDate(2026, 7, 12), QTime(9, 0));
        const auto s = DateParse::suggest("t", sunday);
        QCOMPARE(s.size(), 4);
        QCOMPARE(s[2].date, QDate(2026, 7, 14));
        QCOMPARE(s[3].date, QDate(2026, 7, 16));
        QCOMPARE(labels("tom"), QStringList({"Tomorrow"}));
        QCOMPARE(labels("tod"), QStringList({"Today"}));
        QCOMPARE(labels("  TOMORROW "), QStringList({"Tomorrow"}));
    }

    void relativeDays() {
        check("today", QDate(2026, 7, 15), -1);
        check("yesterday", QDate(2026, 7, 14), -1);
        check("day after tomorrow", QDate(2026, 7, 17), -1);
        check("in 3 days", QDate(2026, 7, 18), -1);
        check("in 3d", QDate(2026, 7, 18), -1);
        check("in 2 weeks", QDate(2026, 7, 29), -1);
        check("in 2w", QDate(2026, 7, 29), -1);
        check("in 2 months", QDate(2026, 9, 15), -1);
        check("next week", QDate(2026, 7, 20), -1);
        check("next month", QDate(2026, 8, 1), -1);
    }

    void timeOfDayWords() {
        check("tom mor", QDate(2026, 7, 16), 9 * 60);
        check("tomorrow afternoon", QDate(2026, 7, 16), 14 * 60);
        check("today evening", QDate(2026, 7, 15), 19 * 60);
        check("tom night", QDate(2026, 7, 16), 21 * 60);
        check("tom noon", QDate(2026, 7, 16), 12 * 60);
        check("tom midnight", QDate(2026, 7, 16), 0);
        QCOMPARE(best("tom mor").label, QString("Tomorrow, 9:00 AM"));
        QVERIFY(!best("noon").date.isValid());
        QCOMPARE(best("noon").minutes, 12 * 60);
    }

    void dayNames() {
        const auto s = sug("wed");
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].date, QDate(2026, 7, 22));  // Strictly after today.
        QCOMPARE(s[1].date, QDate(2026, 7, 29));
        QCOMPARE(s[2].date, QDate(2026, 8, 5));
        check("fri", QDate(2026, 7, 17), -1);
        check("tues", QDate(2026, 7, 21), -1);
        check("thurs", QDate(2026, 7, 16), -1);
        check("thur", QDate(2026, 7, 16), -1);
        check("mon", QDate(2026, 7, 20), -1);
    }

    void thisAndNextDay() {
        check("this thu", QDate(2026, 7, 16), -1);
        check("this mon", QDate(2026, 7, 20), -1);  // Already passed this week.
        check("this wed", QDate(2026, 7, 15), -1);
        check("next fri", QDate(2026, 7, 24), -1);
        check("next mon", QDate(2026, 7, 20), -1);
    }

    void monthAndDay() {
        check("april 19", QDate(2027, 4, 19), -1);  // Already passed this year.
        check("apr 19", QDate(2027, 4, 19), -1);
        check("19 apr", QDate(2027, 4, 19), -1);
        check("jan19", QDate(2027, 1, 19), -1);
        check("aug 3", QDate(2026, 8, 3), -1);
        check("jul 15", QDate(2026, 7, 15), -1);
        check("april 19 2028", QDate(2028, 4, 19), -1);
        check("19th april 2028", QDate(2028, 4, 19), -1);
        QCOMPARE(best("aug 3").label, QString("Mon, 3 Aug"));
        QCOMPARE(best("april 19 2028").label, QString("Wed, 19 Apr 2028"));
    }

    void compactDecember() {
        check("18d", QDate(2026, 12, 18), -1);
        check("d25", QDate(2026, 12, 25), -1);
        check("18d 2028", QDate(2028, 12, 18), -1);
    }

    void weekdayInMonth() {
        const auto s = sug("dec fri", 10);
        QCOMPARE(s.size(), 4);
        QCOMPARE(s[0].date, QDate(2026, 12, 4));
        QCOMPARE(s[3].date, QDate(2026, 12, 25));
        const auto t = sug("sun jun", 10);
        QCOMPARE(t.size(), 4);
        QCOMPARE(t[0].date, QDate(2027, 6, 6));  // June 2026 has gone.
        QCOMPARE(sug("dec fri", 2).size(), 2);
    }

    void weekNumbers() {
        check("w19", QDate(2027, 5, 10), -1);
        check("w40", QDate(2026, 9, 28), -1);
    }

    void exactDates() {
        check("2026-12-25", QDate(2026, 12, 25), -1);
        check("2026/12/25", QDate(2026, 12, 25), -1);
        check("12/25/2026", QDate(2026, 12, 25), -1);
        check("12/25", QDate(2026, 12, 25), -1);
        check("3/4", QDate(2027, 3, 4), -1);
        QVERIFY(sug("2026-02-30").isEmpty());
    }

    void times() {
        const auto now = kNow;
        QCOMPARE(DateParse::parseTime("742", now), 19 * 60 + 42);  // 7:42 AM has passed.
        QCOMPARE(DateParse::parseTime("2pm", now), 14 * 60);
        QCOMPARE(DateParse::parseTime("2 pm", now), 14 * 60);
        QCOMPARE(DateParse::parseTime("9p45", now), 21 * 60 + 45);
        QCOMPARE(DateParse::parseTime("9a", now), 9 * 60);
        QCOMPARE(DateParse::parseTime("14:30", now), 14 * 60 + 30);
        QCOMPARE(DateParse::parseTime("1630", now), 16 * 60 + 30);
        QCOMPARE(DateParse::parseTime("8:30a", now), 8 * 60 + 30);
        QCOMPARE(DateParse::parseTime("12am", now), 0);
        QCOMPARE(DateParse::parseTime("12pm", now), 12 * 60);
        QCOMPARE(DateParse::parseTime("noon", now), 12 * 60);
        QCOMPARE(DateParse::parseTime("25:00", now), -1);
        QCOMPARE(DateParse::parseTime("garbage", now), -1);
        // Early in the day the AM reading is still ahead.
        QCOMPARE(DateParse::parseTime("742", QDateTime(QDate(2026, 7, 15), QTime(6, 0))), 7 * 60 + 42);
    }

    void timeOnlySuggestion() {
        const auto s = best("2pm");
        QVERIFY(!s.date.isValid());
        QCOMPARE(s.minutes, 14 * 60);
        QCOMPARE(s.label, QString("2:00 PM"));
        QCOMPARE(best("9p45").label, QString("9:45 PM"));
        QCOMPARE(best("1630").label, QString("4:30 PM"));
    }

    void loneNumber() {
        const auto s = sug("18");
        QCOMPARE(s.size(), 3);
        QVERIFY(!s[0].date.isValid());
        QCOMPARE(s[0].minutes, 18 * 60);
        QCOMPARE(s[1].date, QDate(2026, 7, 18) );
        QCOMPARE(s[2].date, QDate(2026, 8, 2));
        const auto six = sug("6");
        QCOMPARE(six[0].minutes, 18 * 60);  // 6 AM has passed today.
        QCOMPARE(six[1].date, QDate(2026, 8, 6));  // The 6th has passed this month.
        QCOMPARE(six[2].date, QDate(2026, 7, 21));
        QCOMPARE(sug("45").size(), 1);  // Only "in 45 days" is meaningful.
    }

    void dateWithTime() {
        check("wed 8:30a", QDate(2026, 7, 22), 8 * 60 + 30);
        check("next fri 14:30", QDate(2026, 7, 24), 14 * 60 + 30);
        check("april 19 2pm", QDate(2027, 4, 19), 14 * 60);
        check("tomorrow 1630", QDate(2026, 7, 16), 16 * 60 + 30);
        check("in 3 days 9am", QDate(2026, 7, 18), 9 * 60);
        check("tom 3pm", QDate(2026, 7, 16), 15 * 60);
        check("in 3w 3pm", QDate(2026, 8, 5), 15 * 60);
        check("tom 2 pm", QDate(2026, 7, 16), 14 * 60);
        QCOMPARE(best("tom 3pm").label, QString("Tomorrow, 3:00 PM"));
        QCOMPARE(best("today 3pm").label, QString("Today, 3:00 PM"));
        QCOMPARE(best("next fri 14:30").label, QString("Fri, 24 Jul, 2:30 PM"));
    }

    void ambiguousHourDependsOnDate() {
        check("today 6", QDate(2026, 7, 15), 18 * 60);
        check("tom 6", QDate(2026, 7, 16), 6 * 60);
        check("today 11", QDate(2026, 7, 15), 11 * 60);
        check("tom 18", QDate(2026, 7, 16), 18 * 60);
    }

    void periodWords() {
        check("weekend", QDate(2026, 7, 18), -1);
        check("eod", QDate(2026, 7, 15), 17 * 60);
        check("eow", QDate(2026, 7, 17), -1);
        check("eom", QDate(2026, 7, 31), -1);
        check("now", QDate(2026, 7, 15), 10 * 60 + 30);
        QCOMPARE(best("now").label, QString("Today, 10:30 AM"));
        QCOMPARE(best("eod").label, QString("Today, 5:00 PM"));
    }

    void periodWordsOnWeekends() {
        const QDateTime saturday(QDate(2026, 7, 18), QTime(9, 0));
        QCOMPARE(DateParse::best("weekend", saturday).date, QDate(2026, 7, 18));
        QCOMPARE(DateParse::best("eow", saturday).date, QDate(2026, 7, 24));
        const QDateTime friday(QDate(2026, 7, 17), QTime(9, 0));
        QCOMPARE(DateParse::best("eow", friday).date, QDate(2026, 7, 17));
    }

    void labelsAndYears() {
        QCOMPARE(best("sat").label, QString("Sat, 18 Jul"));
        QCOMPARE(best("2027-01-02").label, QString("Sat, 2 Jan 2027"));
        QCOMPARE(best("tom").label, QString("Tomorrow"));
    }

    void deduplicationAndLimit() {
        const auto s = sug("t");
        for (int i = 0; i < s.size(); ++i)
            for (int j = i + 1; j < s.size(); ++j)
                QVERIFY(!(s[i].date == s[j].date && s[i].minutes == s[j].minutes));
        QCOMPARE(sug("t", 2).size(), 2);
        QCOMPARE(sug("wed", 1).size(), 1);
    }

    void bestIsFirstSuggestion() {
        QCOMPARE(best("t"), sug("t").first());
    }
};

REGISTER_TEST(DateParseTest)
#include "test_dateparse.moc"
