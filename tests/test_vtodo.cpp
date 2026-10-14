// Unit tests for the per-task iCalendar VTODO conversion: the whole-field
// round trip through toIcs/fromIcs, the verbatim carrying of properties and
// components Fairfax does not understand (the reference app's metadata, other clients'
// X- properties), and a golden sample proving the whole-file .ics export is
// unchanged by the refactor.

#include <QRegularExpression>
#include <QtTest>
#include <memory>

#include "exchange.h"
#include "store.h"
#include "testregistry.h"
#include "vtodo.h"

class VtodoTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;

    QString file(const QString &name) { return m_dir.filePath(name); }

    std::unique_ptr<Store> freshStore() {
        auto s = std::make_unique<Store>();
        QString err;
        [&] { QVERIFY2(s->open(file(QString("store%1.db").arg(++m_counter)), &err), qPrintable(err)); }();
        return s;
    }

    static qint64 addList(Store &s, const QString &name) {
        TaskList l;
        l.id = s.nextId("lists");
        l.name = name;
        s.begin("list");
        s.putList(l);
        s.commit();
        return l.id;
    }

    static qint64 addTag(Store &s, const QString &name) {
        Tag t;
        t.id = s.nextId("tags");
        t.name = name;
        s.begin("tag");
        s.putTag(t);
        s.commit();
        return t.id;
    }

    static qint64 addLocation(Store &s, const QString &name, const QString &address) {
        Location l;
        l.id = s.nextId("locations");
        l.name = name;
        l.address = address;
        s.begin("location");
        s.putLocation(l);
        s.commit();
        return l.id;
    }

    static qint64 addTask(Store &s, Task t) {
        t.id = s.nextId("tasks");
        s.begin("task");
        s.putTask(t);
        s.commit();
        return t.id;
    }

    // The written output with volatile values (uids, timestamps) blanked, so a
    // sample can be compared line for line without a fixed clock.
    static QString normalize(const QByteArray &ics) {
        QString text = QString::fromUtf8(ics);
        text.replace("\r\n", "\n");
        text.replace(QRegularExpression("UID:[A-Za-z0-9-]+"), "UID:XXX");
        text.replace(QRegularExpression("(DTSTAMP|CREATED|LAST-MODIFIED):[0-9TZ]+"), "\\1:T");
        text.replace(QRegularExpression("COMPLETED:[0-9TZ]+"), "COMPLETED:T");
        text.replace(QRegularExpression("RELATED-TO;RELTYPE=PARENT:[A-Za-z0-9-]+"), "RELATED-TO;RELTYPE=PARENT:XXX");
        return text;
    }

    // Unfolds a written resource into its content lines.
    static QStringList contentLines(const QByteArray &ics) {
        QString norm = QString::fromUtf8(ics);
        norm.replace("\r\n", "\n").replace('\r', '\n');
        QStringList lines;
        for (const QString &raw : norm.split('\n')) {
            if (!raw.isEmpty() && (raw[0] == ' ' || raw[0] == '\t') && !lines.isEmpty())
                lines.last() += raw.mid(1);
            else if (!raw.isEmpty())
                lines << raw;
        }
        return lines;
    }

private slots:
    void toIcsRoundTripsEveryField() {
        auto s = freshStore();
        const qint64 list = addList(*s, "Errands");
        const qint64 tag = addTag(*s, "urgent");
        const qint64 place = addLocation(*s, "Home", "1 Main St");
        Task parent;
        parent.listId = list;
        parent.title = "Parent";
        const qint64 pid = addTask(*s, parent);

        Task t;
        t.listId = list;
        t.parentId = pid;
        t.title = "Everything";
        t.notes = "note line";
        t.type = TaskType::Project;
        t.priority = Priority::High;
        t.starred = true;
        t.duration = 30;
        t.startDate = QDate(2026, 3, 1);
        t.startTime = 9 * 60;
        t.dueDate = QDate(2026, 3, 2);
        t.dueTime = 17 * 60 + 45;
        t.completed = true;
        t.completedAt = QDateTime(QDate(2026, 3, 3), QTime(8, 30));
        t.actionType = ActionType::Url;
        t.actionValue = "https://example.test/x";
        t.recurrence.frequency = Repeat::Daily;
        t.recurrence.interval = 2;
        t.recurrence.from = Repeat::FromCompletion;
        t.tagIds = {tag};
        t.locationIds = {place};
        const qint64 tid = addTask(*s, t);

        Alarm rel;
        rel.id = s->nextId("alarms");
        rel.taskId = tid;
        rel.offset = 45;
        Alarm abs;
        abs.id = s->nextId("alarms");
        abs.taskId = tid;
        abs.kind = Alarm::Absolute;
        abs.at = QDateTime(QDate(2026, 3, 1), QTime(6, 0));
        s->begin("alarms");
        s->putAlarm(rel);
        s->putAlarm(abs);
        s->commit();

        const QByteArray ics = Vtodo::toIcs(*s, s->task(tid));
        QVERIFY(ics.contains("BEGIN:VCALENDAR"));
        QVERIFY(ics.contains("BEGIN:VTODO"));
        QVERIFY(ics.contains("BEGIN:VTIMEZONE"));
        QVERIFY(ics.contains(QByteArray("TZID:") + QTimeZone::systemTimeZone().id()));
        QVERIFY(ics.contains("DTSTART;TZID="));
        QVERIFY(ics.contains("LOCATION:Home"));
        QVERIFY(ics.contains("URL:https://example.test/x"));

        const Vtodo::Parsed p = Vtodo::fromIcs(ics);
        QCOMPARE(p.task.title, QString("Everything"));
        QCOMPARE(p.task.notes, QString("note line"));
        QCOMPARE(p.task.type, int(TaskType::Project));
        QCOMPARE(p.task.priority, int(Priority::High));
        QVERIFY(p.task.starred);
        QCOMPARE(p.task.duration, 30);
        QCOMPARE(p.task.startDate, QDate(2026, 3, 1));
        QCOMPARE(p.task.startTime, 9 * 60);
        QCOMPARE(p.task.dueDate, QDate(2026, 3, 2));
        QCOMPARE(p.task.dueTime, 17 * 60 + 45);
        QVERIFY(p.task.completed);
        QCOMPARE(p.task.completedAt.toSecsSinceEpoch(), t.completedAt.toSecsSinceEpoch());
        QCOMPARE(p.task.actionType, int(ActionType::Url));
        QCOMPARE(p.task.actionValue, QString("https://example.test/x"));
        QCOMPARE(p.task.recurrence, t.recurrence);
        QCOMPARE(p.tagNames, QStringList{"urgent"});
        QCOMPARE(p.listName, QString("Errands"));
        QCOMPARE(p.locationName, QString("Home"));
        QCOMPARE(p.parentUid, s->task(pid).uid);
        QCOMPARE(p.alarms.size(), 2);
        QCOMPARE(p.alarms[0].kind, int(Alarm::Relative));
        QCOMPARE(p.alarms[0].offset, 45);
        QCOMPARE(p.alarms[1].kind, int(Alarm::Absolute));
        QCOMPARE(p.alarms[1].at.toSecsSinceEpoch(), abs.at.toSecsSinceEpoch());

        // A date-only task round-trips as DATE values and keeps no time.
        Task d;
        d.listId = list;
        d.title = "Date only";
        d.startDate = QDate(2026, 4, 1);
        d.dueDate = QDate(2026, 4, 2);
        const qint64 did = addTask(*s, d);
        const QByteArray dates = Vtodo::toIcs(*s, s->task(did));
        QVERIFY(dates.contains("DTSTART;VALUE=DATE:20260401"));
        QVERIFY(dates.contains("DUE;VALUE=DATE:20260402"));
        QVERIFY(!dates.contains("VTIMEZONE"));
        const Vtodo::Parsed dp = Vtodo::fromIcs(dates);
        QCOMPARE(dp.task.startDate, QDate(2026, 4, 1));
        QCOMPARE(dp.task.startTime, -1);
        QCOMPARE(dp.task.dueDate, QDate(2026, 4, 2));
        QCOMPARE(dp.task.dueTime, -1);
    }

    void readsForeignTimezone() {
        const QByteArray ics =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VTODO\r\nUID:tz-1\r\nSUMMARY:Zoned\r\n"
            "DTSTART;TZID=America/New_York:20260301T090000\r\n"
            "DUE;TZID=America/New_York:20260302T173000\r\n"
            "END:VTODO\r\nEND:VCALENDAR\r\n";
        const Vtodo::Parsed p = Vtodo::fromIcs(ics);
        QCOMPARE(p.task.startDate, QDate(2026, 3, 1));
        QCOMPARE(p.task.startTime, 9 * 60);
        QCOMPARE(p.task.dueDate, QDate(2026, 3, 2));
        QCOMPARE(p.task.dueTime, 17 * 60 + 30);
    }

    void carriesUnknownContentVerbatim() {
        const QByteArray input =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:carry-1\r\n"
            "SUMMARY:Carried\r\n"
            "DESCRIPTION:Notes here\\n<App Meta>\\n{\"foo\":1}\\n</App Meta>\r\n"
            "DTSTAMP:20260101T000000Z\r\n"
            "X-EXAMPLE-THING;X-P=1:some value\r\n"
            "X-APPLE-SORT-ORDER:123\r\n"
            "X-CUSTOM:hello\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER:-PT10M\r\n"
            "X-ALARM-EXTRA:keep me\r\n"
            "END:VALARM\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER;VALUE=DATE:20260101\r\n"
            "END:VALARM\r\n"
            "BEGIN:X-CUSTOM-COMPONENT\r\n"
            "X-INSIDE:1\r\n"
            "END:X-CUSTOM-COMPONENT\r\n"
            "END:VTODO\r\nEND:VCALENDAR\r\n";

        const Vtodo::Parsed p = Vtodo::fromIcs(input);
        QCOMPARE(p.task.title, QString("Carried"));
        QCOMPARE(p.task.notes, QString("Notes here"));
        QVERIFY(p.extra.meta.contains("<App Meta>"));
        QVERIFY(p.extra.meta.contains("</App Meta>"));
        // One alarm is representable, the other's date-only trigger is not, so
        // it is kept whole as an unknown component along with the X- one.
        QCOMPARE(p.alarms.size(), 1);
        QCOMPARE(p.extra.components.size(), 2);

        // Write it back into a store holding the parsed task, which is what a
        // sync does after reading a resource.
        auto s = freshStore();
        Task t = p.task;
        t.id = s->nextId("tasks");
        s->begin("task");
        s->putTask(t);
        for (Alarm a : p.alarms) {
            a.id = s->nextId("alarms");
            a.taskId = t.id;
            s->putAlarm(a);
        }
        s->commit();

        const QStringList lines = contentLines(Vtodo::toIcs(*s, s->task(t.id), p.extra));
        const QStringList carried{"X-EXAMPLE-THING;X-P=1:some value",
                                  "X-APPLE-SORT-ORDER:123",
                                  "X-CUSTOM:hello",
                                  "X-ALARM-EXTRA:keep me",
                                  "BEGIN:X-CUSTOM-COMPONENT",
                                  "X-INSIDE:1",
                                  "END:X-CUSTOM-COMPONENT",
                                  "TRIGGER;VALUE=DATE:20260101"};
        for (const QString &wanted : carried)
            QVERIFY2(lines.contains(wanted), qPrintable(wanted));
        QVERIFY2(lines.contains("DESCRIPTION:Notes here\\n<App Meta>\\n{\"foo\":1}\\n</App Meta>"),
                 "the the reference app metadata block survives");
    }

    void doesNotInventMetaWhenThereIsNone() {
        const QByteArray input =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:plain\r\n"
            "SUMMARY:Plain\r\nDESCRIPTION:just notes\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";
        const Vtodo::Parsed p = Vtodo::fromIcs(input);
        QCOMPARE(p.task.notes, QString("just notes"));
        QVERIFY(p.extra.meta.isEmpty());
    }

    void exportIcsSampleIsUnchanged() {
        auto src = freshStore();
        const qint64 work = addList(*src, "Work");
        const qint64 home = addList(*src, "Home");
        const qint64 a = addTag(*src, "Alpha");
        const qint64 b = addTag(*src, "beta, gamma");

        Task proj;
        proj.listId = work;
        proj.type = TaskType::Project;
        proj.title = "Proj";
        const qint64 pid = addTask(*src, proj);

        Task child;
        child.listId = work;
        child.parentId = pid;
        child.title = "Child";
        child.priority = Priority::Medium;
        child.dueDate = QDate(2026, 8, 3);
        child.dueTime = 14 * 60 + 30;
        child.startDate = QDate(2026, 8, 1);
        child.duration = 45;
        child.starred = true;
        child.tagIds = {a, b};
        child.notes = "Notes; with, odd\\chars\nand a newline";
        child.recurrence.frequency = Repeat::Monthly;
        child.recurrence.monthOrdinal = -1;
        child.recurrence.monthWeekday = 5;
        child.recurrence.endType = Repeat::AfterCount;
        child.recurrence.endCount = 6;
        child.recurrence.interval = 2;
        const qint64 cid = addTask(*src, child);

        Task weekly;
        weekly.listId = home;
        weekly.title = "Weekly";
        weekly.priority = Priority::High;
        weekly.dueDate = QDate(2026, 9, 1);
        weekly.recurrence.frequency = Repeat::Weekly;
        weekly.recurrence.weekdays = 0b0000101;
        weekly.recurrence.from = Repeat::FromCompletion;
        weekly.recurrence.endType = Repeat::OnDate;
        weekly.recurrence.endDate = QDate(2027, 1, 1);
        weekly.completed = true;
        weekly.completedAt = QDateTime(QDate(2026, 8, 30), QTime(10, 15));
        const qint64 wid = addTask(*src, weekly);

        Alarm rel;
        rel.id = src->nextId("alarms");
        rel.taskId = cid;
        rel.offset = 15;
        Alarm abs;
        abs.id = src->nextId("alarms");
        abs.taskId = wid;
        abs.kind = Alarm::Absolute;
        abs.at = QDateTime(QDate(2026, 8, 31), QTime(7, 30));
        src->begin("alarms");
        src->putAlarm(rel);
        src->putAlarm(abs);
        src->commit();

        QString err;
        QVERIFY2(Exchange::exportIcs(*src, file("golden.ics"), {}, &err), qPrintable(err));
        QFile f(file("golden.ics"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString got = normalize(f.readAll());

        const QStringList expected{
            "BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//Fairfax//EN", "CALSCALE:GREGORIAN",
            "BEGIN:VTODO", "UID:XXX", "SUMMARY:Proj", "DTSTAMP:T", "CREATED:T", "LAST-MODIFIED:T",
            "STATUS:NEEDS-ACTION", "X-FAIRFAX-LIST:Work", "X-FAIRFAX-TYPE:project", "END:VTODO",
            "BEGIN:VTODO", "UID:XXX", "SUMMARY:Child",
            "DESCRIPTION:Notes; with\\, odd\\\\chars\\nand a newline", "DTSTAMP:T", "CREATED:T",
            "LAST-MODIFIED:T", "DTSTART;VALUE=DATE:20260801", "DUE:20260803T143000", "PRIORITY:5",
            "STATUS:NEEDS-ACTION", "CATEGORIES:Alpha,beta\\, gamma", "X-FAIRFAX-LIST:Work",
            "X-FAIRFAX-TYPE:task", "X-FAIRFAX-STARRED:1", "X-FAIRFAX-DURATION:45",
            "RELATED-TO;RELTYPE=PARENT:XXX", "RRULE:FREQ=MONTHLY;INTERVAL=2;BYDAY=-1FR;COUNT=6",
            "BEGIN:VALARM", "ACTION:DISPLAY", "DESCRIPTION:Child", "TRIGGER:-PT15M", "END:VALARM",
            "END:VTODO",
            "BEGIN:VTODO", "UID:XXX", "SUMMARY:Weekly", "DTSTAMP:T", "CREATED:T", "LAST-MODIFIED:T",
            "DUE;VALUE=DATE:20260901", "PRIORITY:1", "STATUS:COMPLETED", "COMPLETED:T",
            "X-FAIRFAX-LIST:Home", "X-FAIRFAX-TYPE:task", "RRULE:FREQ=WEEKLY;BYDAY=MO,WE;UNTIL=20270101",
            "X-FAIRFAX-REPEAT-FROM:COMPLETION", "BEGIN:VALARM", "ACTION:DISPLAY", "DESCRIPTION:Weekly",
            "TRIGGER;VALUE=DATE-TIME:20260831T073000Z", "END:VALARM", "END:VTODO", "END:VCALENDAR"};
        QCOMPARE(got, expected.join('\n') + '\n');
    }
};

REGISTER_TEST(VtodoTest)

#include "test_vtodo.moc"
