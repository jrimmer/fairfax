#include <QtQml>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest>
#include <time.h>

#include "calendar.h"
#include "testregistry.h"

namespace {

QByteArray wrap(const QString &events) {
    QString text = QStringLiteral("BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//test//EN\n") + events + QStringLiteral("END:VCALENDAR\n");
    return text.replace(QLatin1Char('\n'), QStringLiteral("\r\n")).toUtf8();
}

QString vevent(const QString &body) { return QStringLiteral("BEGIN:VEVENT\nUID:u1\nSUMMARY:Meeting\n") + body + QStringLiteral("\nEND:VEVENT\n"); }

QString local(const QDateTime &t) { return t.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")); }

QStringList starts(const QList<Ics::Event> &events) {
    QStringList out;
    for (const auto &e : events) out << local(e.start);
    return out;
}

QList<Ics::Event> expand(const QString &body, const QString &from, const QString &to) {
    const auto comps = Ics::parseComponents(wrap(body), QStringLiteral("cal"));
    return Ics::occurrences(comps, QDate::fromString(from, Qt::ISODate), QDate::fromString(to, Qt::ISODate));
}

// Local time is whatever the machine says, so tests pin TZ to make
// conversions and daylight saving behaviour deterministic.
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

// Stands in for the Backend: records what a drop asked for.
class BackendStub : public QObject {
    Q_OBJECT
    Q_PROPERTY(int fontSize READ fontSize CONSTANT)
public:
    int fontSize() const { return 13; }
    Q_INVOKABLE void timeBlock(const QString &iso, int start, int duration) { calls << QVariantList{iso, start, duration}; }
    QList<QVariantList> calls;
};

}  // namespace

class CalendarTest : public QObject {
    Q_OBJECT

private slots:
    void parsesAllDay() {
        Zone z("Europe/Berlin");
        const auto events = Ics::parse(wrap(vevent("DTSTART;VALUE=DATE:20260715\nDTEND;VALUE=DATE:20260717")), "cal");
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].allDay);
        QCOMPARE(local(events[0].start), "2026-07-15 00:00");
        QCOMPARE(local(events[0].end), "2026-07-17 00:00");
    }

    void parsesAllDayWithoutEndAsOneDay() {
        Zone z("Europe/Berlin");
        const auto events = Ics::parse(wrap(vevent("DTSTART;VALUE=DATE:20260715")), "cal");
        QCOMPARE(local(events[0].end), "2026-07-16 00:00");
    }

    void parsesUtc() {
        Zone z("Europe/Berlin");
        const auto events = Ics::parse(wrap(vevent("DTSTART:20260715T140000Z\nDTEND:20260715T150000Z")), "cal");
        QCOMPARE(local(events[0].start), "2026-07-15 16:00");
        QCOMPARE(local(events[0].end), "2026-07-15 17:00");
        QVERIFY(!events[0].allDay);
        QCOMPARE(events[0].calendar, "cal");
    }

    void parsesTzid() {
        Zone z("Europe/Berlin");
        const auto events = Ics::parse(wrap(vevent("DTSTART;TZID=America/New_York:20260715T100000\nDTEND;TZID=America/New_York:20260715T113000")), "cal");
        QCOMPARE(events[0].start.toUTC().toString(Qt::ISODate), "2026-07-15T14:00:00Z");
        QCOMPARE(local(events[0].start), "2026-07-15 16:00");
        QCOMPARE(local(events[0].end), "2026-07-15 17:30");
    }

    void parsesFloatingAsLocal() {
        Zone z("America/Los_Angeles");
        const auto events = Ics::parse(wrap(vevent("DTSTART:20260715T100000\nDURATION:PT1H30M")), "cal");
        QCOMPARE(local(events[0].start), "2026-07-15 10:00");
        QCOMPARE(local(events[0].end), "2026-07-15 11:30");
    }

    void unfoldsAndUnescapes() {
        const QString body = QStringLiteral(
            "DTSTART:20260715T100000Z\nSUMMARY:Lunch\\, with Sam\\; bring\\nnotes and a very long\n  title that was folded\n"
            "LOCATION:Cafe \\\\ Main\n\t Street\nDTEND:20260715T110000Z");
        const auto events = Ics::parse(wrap(QStringLiteral("BEGIN:VEVENT\nUID:x\n") + body + QStringLiteral("\nEND:VEVENT\n")), "cal");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].title, "Lunch, with Sam; bring\nnotes and a very long title that was folded");
        QCOMPARE(events[0].location, "Cafe \\ Main Street");
    }

    void skipsCancelledAndIgnoresAlarms() {
        const QString text = vevent("DTSTART:20260715T100000Z\nDTEND:20260715T110000Z\nBEGIN:VALARM\nTRIGGER:-PT15M\nSUMMARY:alarm\nEND:VALARM")
                             + QStringLiteral("BEGIN:VEVENT\nUID:c\nSUMMARY:Gone\nSTATUS:CANCELLED\nDTSTART:20260716T100000Z\nEND:VEVENT\n");
        const auto events = Ics::parse(wrap(text), "cal");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].title, "Meeting");
    }

    void tzidWithQuotedColon() {
        // Quoted parameter values may contain the separators.
        const auto events = Ics::parse(wrap(vevent("DTSTART;TZID=\"America/New_York\":20260715T100000")), "cal");
        QCOMPARE(events[0].start.toUTC().toString(Qt::ISODate), "2026-07-15T14:00:00Z");
    }

    void weeklyByDayWithInterval() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260706T100000Z\nDTEND:20260706T110000Z\nRRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE"), "2026-07-01", "2026-07-31");
        QCOMPARE(starts(e), (QStringList{"2026-07-06 10:00", "2026-07-08 10:00", "2026-07-20 10:00", "2026-07-22 10:00"}));
    }

    void monthlySecondTuesday() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260113T090000Z\nDTEND:20260113T093000Z\nRRULE:FREQ=MONTHLY;BYDAY=2TU"), "2026-01-01", "2026-04-30");
        QCOMPARE(starts(e), (QStringList{"2026-01-13 09:00", "2026-02-10 09:00", "2026-03-10 09:00", "2026-04-14 09:00"}));
    }

    void monthlyLastFriday() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260130T090000Z\nDTEND:20260130T093000Z\nRRULE:FREQ=MONTHLY;BYDAY=-1FR"), "2026-01-01", "2026-04-30");
        QCOMPARE(starts(e), (QStringList{"2026-01-30 09:00", "2026-02-27 09:00", "2026-03-27 09:00", "2026-04-24 09:00"}));
    }

    void monthlyLastDayOfMonth() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260131T090000Z\nRRULE:FREQ=MONTHLY;BYMONTHDAY=-1"), "2026-01-01", "2026-03-31");
        QCOMPARE(starts(e), (QStringList{"2026-01-31 09:00", "2026-02-28 09:00", "2026-03-31 09:00"}));
    }

    void monthlySkipsMissingDays() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260131T090000Z\nRRULE:FREQ=MONTHLY"), "2026-01-01", "2026-04-30");
        QCOMPARE(starts(e), (QStringList{"2026-01-31 09:00", "2026-03-31 09:00"}));
    }

    void yearlyByMonth() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART;VALUE=DATE:20240305\nRRULE:FREQ=YEARLY;BYMONTH=3;BYMONTHDAY=5"), "2026-01-01", "2027-12-31");
        QCOMPARE(starts(e), (QStringList{"2026-03-05 00:00", "2027-03-05 00:00"}));
        QVERIFY(e[0].allDay);
    }

    void countLimitsOccurrences() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260701T100000Z\nRRULE:FREQ=DAILY;COUNT=3"), "2026-06-01", "2026-08-01");
        QCOMPARE(starts(e), (QStringList{"2026-07-01 10:00", "2026-07-02 10:00", "2026-07-03 10:00"}));
    }

    void countIncludesExcludedDates() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260701T100000Z\nRRULE:FREQ=DAILY;COUNT=3\nEXDATE:20260702T100000Z"), "2026-06-01", "2026-08-01");
        QCOMPARE(starts(e), (QStringList{"2026-07-01 10:00", "2026-07-03 10:00"}));
    }

    void untilEndsSeries() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20260701T100000Z\nRRULE:FREQ=WEEKLY;UNTIL=20260715T100000Z"), "2026-06-01", "2026-08-31");
        QCOMPARE(starts(e), (QStringList{"2026-07-01 10:00", "2026-07-08 10:00", "2026-07-15 10:00"}));
        const auto d = expand(vevent("DTSTART:20260701T100000Z\nRRULE:FREQ=WEEKLY;UNTIL=20260714"), "2026-06-01", "2026-08-31");
        QCOMPARE(d.size(), 2);
    }

    void exdateRemovesAnOccurrence() {
        Zone z("America/New_York");
        const auto e = expand(vevent("DTSTART;TZID=America/New_York:20260706T100000\nRRULE:FREQ=WEEKLY\nEXDATE;TZID=America/New_York:20260713T100000"), "2026-07-01", "2026-07-31");
        QCOMPARE(starts(e), (QStringList{"2026-07-06 10:00", "2026-07-20 10:00", "2026-07-27 10:00"}));
    }

    void recurrenceIdOverrides() {
        Zone z("UTC");
        const QString text = vevent("DTSTART:20260706T100000Z\nDTEND:20260706T110000Z\nRRULE:FREQ=WEEKLY")
                             + QStringLiteral("BEGIN:VEVENT\nUID:u1\nSUMMARY:Moved\nRECURRENCE-ID:20260713T100000Z\nDTSTART:20260713T150000Z\nDTEND:20260713T160000Z\nEND:VEVENT\n")
                             + QStringLiteral("BEGIN:VEVENT\nUID:u1\nSUMMARY:Off\nSTATUS:CANCELLED\nRECURRENCE-ID:20260720T100000Z\nDTSTART:20260720T100000Z\nEND:VEVENT\n");
        const auto e = expand(text, "2026-07-01", "2026-07-31");
        QCOMPARE(starts(e), (QStringList{"2026-07-06 10:00", "2026-07-13 15:00", "2026-07-27 10:00"}));
        QCOMPARE(e[1].title, "Moved");
        QCOMPARE(e[0].title, "Meeting");
    }

    void occurrencesAreWindowed() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART:20200101T100000Z\nRRULE:FREQ=DAILY"), "2026-07-15", "2026-07-15");
        QCOMPARE(e.size(), 1);
        QCOMPARE(local(e[0].start), "2026-07-15 10:00");
    }

    void multiDayEventsAppearOnLaterDays() {
        Zone z("UTC");
        const auto e = expand(vevent("DTSTART;VALUE=DATE:20260714\nDTEND;VALUE=DATE:20260717"), "2026-07-16", "2026-07-16");
        QCOMPARE(e.size(), 1);
        QVERIFY(expand(vevent("DTSTART;VALUE=DATE:20260714\nDTEND;VALUE=DATE:20260717"), "2026-07-17", "2026-07-17").isEmpty());
    }

    // 9 AM in New York keeps its wall-clock time through the US spring change
    // (8 March), so for a Berlin viewer, still on winter time until 29 March,
    // the local hour must jump from 15:00 to 14:00.
    void recurringEventsFollowTheirOwnZoneAcrossDst() {
        Zone z("Europe/Berlin");
        const auto e = expand(vevent("DTSTART;TZID=America/New_York:20260301T090000\nDTEND;TZID=America/New_York:20260301T100000\nRRULE:FREQ=WEEKLY"), "2026-03-01", "2026-03-15");
        QCOMPARE(starts(e), (QStringList{"2026-03-01 15:00", "2026-03-08 14:00", "2026-03-15 14:00"}));
        QCOMPARE(e[1].start.secsTo(e[1].end), 3600);
    }

    void allDayStaysMidnightAcrossLocalDst() {
        Zone z("America/New_York");
        const auto e = expand(vevent("DTSTART;VALUE=DATE:20260307\nRRULE:FREQ=DAILY;COUNT=3"), "2026-03-07", "2026-03-10");
        QCOMPARE(starts(e), (QStringList{"2026-03-07 00:00", "2026-03-08 00:00", "2026-03-09 00:00"}));
    }

    void feedTodayAndFreeSlots() {
        Zone z("UTC");
        CalendarFeed feed;
        feed.setPalette({"#ff0000", "#00ff00"});
        feed.setClock([] { return QDateTime(QDate(2026, 7, 15), QTime(8, 0)); });
        // The source list drives colours, so register the source before loading it.
        feed.setSources({"/tmp/work.ics"});
        feed.loadData("/tmp/work.ics", wrap(vevent("DTSTART:20260715T090000Z\nDTEND:20260715T100000Z")
                                           + QStringLiteral("BEGIN:VEVENT\nUID:b\nSUMMARY:Standup\nLOCATION:Room 4\nDTSTART:20260715T103000Z\nDTEND:20260715T110000Z\nEND:VEVENT\n")
                                           + QStringLiteral("BEGIN:VEVENT\nUID:c\nSUMMARY:Review\nDTSTART:20260715T130000Z\nDTEND:20260715T140000Z\nEND:VEVENT\n")
                                           + QStringLiteral("BEGIN:VEVENT\nUID:d\nSUMMARY:Holiday\nDTSTART;VALUE=DATE:20260715\nEND:VEVENT\n")));

        const QVariantList today = feed.today();
        QCOMPARE(today.size(), 4);
        const QVariantMap holiday = today[0].toMap();  // All-day events sort first.
        QVERIFY(holiday["allDay"].toBool());
        QCOMPARE(holiday["end"].toInt(), 1440);
        const QVariantMap first = today[1].toMap();
        QCOMPARE(first["title"].toString(), "Meeting");
        QCOMPARE(first["start"].toInt(), 540);
        QCOMPARE(first["end"].toInt(), 600);
        QCOMPARE(first["color"].value<QColor>(), QColor("#ff0000"));
        QCOMPARE(today[2].toMap()["location"].toString(), "Room 4");

        const QVariantList gaps = feed.freeSlots("2026-07-15", 8 * 60, 17 * 60);
        QStringList labels;
        for (const QVariant &s : gaps) labels << QStringLiteral("%1-%2 %3").arg(s.toMap()["start"].toInt()).arg(s.toMap()["end"].toInt()).arg(s.toMap()["label"].toString());
        QCOMPARE(labels, (QStringList{"480-540 1 hour free", "600-630 30 min free", "660-780 2 hours free", "840-1020 3 hours free"}));
        // A gap shorter than half an hour is not worth offering.
        QCOMPARE(feed.freeSlots("2026-07-15", 540, 555).size(), 0);
    }

    void captionFollowsTheClock() {
        Zone z("UTC");
        CalendarFeed feed;
        feed.setSources({"/tmp/none.ics"});
        feed.loadData("/tmp/none.ics", wrap(QStringLiteral("BEGIN:VEVENT\nUID:j\nSUMMARY:Call with Jeremy\nDTSTART:20260715T100000Z\nDTEND:20260715T103000Z\nEND:VEVENT\n")
                                           + QStringLiteral("BEGIN:VEVENT\nUID:k\nSUMMARY:Dinner\nDTSTART:20260715T183000Z\nDTEND:20260715T193000Z\nEND:VEVENT\n")));
        QTime now(9, 0);
        feed.setClock([&now] { return QDateTime(QDate(2026, 7, 15), now); });
        QCOMPARE(feed.caption(), "Call with Jeremy · upcoming 10 AM");
        QCOMPARE(feed.nowMinute(), 540);
        now = QTime(10, 10);
        QCOMPARE(feed.caption(), "Call with Jeremy · ongoing until 10:30 AM");
        now = QTime(11, 0);
        QCOMPARE(feed.caption(), "Dinner · upcoming 6:30 PM");
        now = QTime(20, 0);
        QCOMPARE(feed.caption(), "No more events today");
        feed.setClock([] { return QDateTime(QDate(2026, 7, 16), QTime(9, 0)); });
        QCOMPARE(feed.caption(), "No events today");
    }

    void webcalBecomesHttps() {
        // Only the scheme mapping is observable without a network: the feed
        // accepts the source and reports no parse error of its own.
        CalendarFeed feed;
        feed.setSources({"webcal://example.invalid/a.ics", " ", "webcal://example.invalid/a.ics"});
        QCOMPARE(feed.sources(), (QStringList{"webcal://example.invalid/a.ics"}));
    }

    void missingFileReportsError() {
        CalendarFeed feed;
        feed.setSources({"/nonexistent/fairfax.ics"});
        QVERIFY(feed.error().contains("Cannot read"));
        QCOMPARE(feed.today().size(), 0);
    }

    void parsesCalendarNameAndDetails() {
        const QByteArray ics = wrap(QStringLiteral("X-WR-CALNAME:Team\\, Work\n") + vevent("DTSTART:20260715T090000Z\nDTEND:20260715T100000Z\nDESCRIPTION:Agenda\\nSecond line\nURL:https://example.com/m\nLOCATION:Room 1"));
        // Calendar-level properties must precede the events, and wrap() puts them there.
        QCOMPARE(Ics::calendarName(ics), QStringLiteral("Team, Work"));
        QCOMPARE(Ics::calendarName(wrap(vevent("DTSTART:20260715T090000Z"))), QString());
        const auto events = Ics::parse(ics, "cal");
        QCOMPARE(events[0].description, QStringLiteral("Agenda\nSecond line"));
        QCOMPARE(events[0].url, QStringLiteral("https://example.com/m"));
    }

    void feedUsesCalendarNameAndDisablesSources() {
        Zone z("UTC");
        CalendarFeed feed;
        feed.setSources({"/nonexistent/a.ics", "/nonexistent/b.ics"});
        feed.loadData("/nonexistent/a.ics", wrap(QStringLiteral("X-WR-CALNAME:Personal\n") + vevent("DTSTART:20260715T090000Z\nDTEND:20260715T100000Z")));
        feed.loadData("/nonexistent/b.ics", wrap(vevent("DTSTART:20260715T110000Z\nDTEND:20260715T120000Z")));
        auto info = feed.sourceInfo();
        QCOMPARE(info[0].toMap()["name"].toString(), QStringLiteral("Personal"));
        QCOMPARE(info[1].toMap()["name"].toString(), QStringLiteral("b"));
        QCOMPARE(feed.eventsFor("2026-07-15").size(), 2);
        QCOMPARE(feed.eventsFor("2026-07-15")[0].toMap()["calendar"].toString(), QStringLiteral("Personal"));

        feed.setDisabledSources({"/nonexistent/a.ics"});
        QCOMPARE(feed.eventsFor("2026-07-15").size(), 1);
        QVERIFY(!feed.sourceInfo()[0].toMap()["enabled"].toBool());

        feed.setSourceColors({{"/nonexistent/b.ics", "#123456"}});
        QCOMPARE(feed.eventsFor("2026-07-15")[0].toMap()["color"].value<QColor>(), QColor("#123456"));
        QVERIFY(feed.sourceInfo()[1].toMap()["customColor"].toBool());
    }

    void declinedEventsMatchMyAddresses() {
        Zone z("UTC");
        CalendarFeed feed;
        feed.setSources({"/nonexistent/d.ics"});
        feed.loadData("/nonexistent/d.ics", wrap(vevent("DTSTART:20260715T090000Z\nDTEND:20260715T100000Z\nATTENDEE;CN=Me;PARTSTAT=DECLINED:mailto:Me@Example.com\nATTENDEE;PARTSTAT=ACCEPTED:mailto:other@example.com")));
        QVERIFY(!feed.eventsFor("2026-07-15")[0].toMap()["declined"].toBool());
        feed.setMyEmails("x@y.org, me@example.com");
        QVERIFY(feed.eventsFor("2026-07-15")[0].toMap()["declined"].toBool());
        // A declined meeting no longer blocks the time.
        QCOMPARE(feed.freeSlots("2026-07-15", 8 * 60, 12 * 60).size(), 1);
        QCOMPARE(feed.freeSlots("2026-07-15", 8 * 60, 12 * 60)[0].toMap()["end"].toInt(), 12 * 60);
        feed.setMyEmails("someone@else.org");
        QVERIFY(!feed.eventsFor("2026-07-15")[0].toMap()["declined"].toBool());
    }

    void dayBarLoadsAndTimeBlocks() {
        Zone z("UTC");
        const QString path = findQml();
        if (path.isEmpty()) QSKIP("DayBar.qml not found relative to the test sources");

        const QDate day = QDate::currentDate();
        const QString d = day.toString(QStringLiteral("yyyyMMdd"));
        CalendarFeed feed;
        feed.setSources({"/nonexistent/day.ics"});
        feed.loadData("/nonexistent/day.ics", wrap(QStringLiteral("BEGIN:VEVENT\nUID:a\nSUMMARY:Planning\nDTSTART:%1T100000Z\nDTEND:%1T110000Z\nEND:VEVENT\n").arg(d)
                                                  + QStringLiteral("BEGIN:VEVENT\nUID:b\nSUMMARY:Lunch\nDTSTART:%1T120000Z\nDTEND:%1T130000Z\nEND:VEVENT\n").arg(d)));

        QQmlEngine engine;
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
        std::unique_ptr<QQmlPropertyMap> themeMap(QQmlPropertyMap::create());
#else
        auto themeMap = std::make_unique<QQmlPropertyMap>();
#endif
        QQmlPropertyMap &theme = *themeMap;
        for (const char *name : {"foreground", "muted", "faint", "divider", "overdue", "today", "accent", "raised", "background"})
            theme.insert(name, QColor("#808080"));
        BackendStub backend;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("calendar", &feed);

        QQmlComponent component(&engine, QUrl::fromLocalFile(path));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(item);
        // Positioners lay out on polish, which only runs inside a window.
        QQuickWindow window;
        window.resize(600, 800);
        item->setParentItem(window.contentItem());
        item->setWidth(600);
        window.show();
        QVERIFY(item->isVisible());
        QTRY_VERIFY(item->height() > 40);

        // Expanding adds the hour grid.
        const qreal collapsed = item->height();
        object->setProperty("expanded", true);
        QTRY_VERIFY(item->height() > collapsed + 300);

        // 10:03 is inside Planning, so a drop snaps to 10:00 and takes 30 minutes.
        QVERIFY(QMetaObject::invokeMethod(object.data(), "commitDrop", Q_ARG(QVariant, 603)));
        // 11:40 snaps to 11:45, leaving 15 minutes before Lunch.
        QVERIFY(QMetaObject::invokeMethod(object.data(), "commitDrop", Q_ARG(QVariant, 700)));
        QCOMPARE(backend.calls.size(), 2);
        QCOMPARE(backend.calls[0], (QVariantList{day.toString(Qt::ISODate), 600, 30}));
        QCOMPARE(backend.calls[1], (QVariantList{day.toString(Qt::ISODate), 705, 15}));

        // No sources hides the bar entirely.
        feed.setSources({});
        QVERIFY(!item->isVisible());
        QCOMPARE(item->height(), 0.0);
    }

private:
    static QString findQml() {
        const QStringList candidates = {
            QFileInfo(QStringLiteral(__FILE__)).absolutePath() + "/../src/qml/DayBar.qml",
            QDir::currentPath() + "/../src/qml/DayBar.qml",
            QDir::currentPath() + "/src/qml/DayBar.qml",
            QCoreApplication::applicationDirPath() + "/../src/qml/DayBar.qml",
            qEnvironmentVariable("FAIRFAX_QML_DIR") + "/DayBar.qml",
        };
        for (const QString &c : candidates)
            if (QFileInfo::exists(c)) return QFileInfo(c).absoluteFilePath();
        return {};
    }
};

REGISTER_TEST(CalendarTest)
#include "test_calendar.moc"
