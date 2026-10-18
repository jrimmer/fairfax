#include <QtSql>
#include <QtTest>
#include <memory>

#include "exchange.h"
#include "store.h"
#include "testregistry.h"

class ExchangeTest : public QObject {
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

    static qint64 addList(Store &s, const QString &name, int kind = ListKind::Normal) {
        TaskList l;
        l.id = s.nextId("lists");
        l.name = name;
        l.kind = kind;
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

    static qint64 addTask(Store &s, Task t) {
        t.id = s.nextId("tasks");
        s.begin("task");
        s.putTask(t);
        s.commit();
        return t.id;
    }

    static Task byTitle(const Store &s, const QString &title) {
        for (const Task &t : s.tasks())
            if (t.title == title)
                return t;
        return {};
    }

    static QString exec(QSqlDatabase &db, const QString &sql) {
        QSqlQuery q(db);
        return q.exec(sql) ? QString() : q.lastError().text() + " in " + sql;
    }

private slots:
    void csvQuotingAndColumns() {
        auto s = freshStore();
        const qint64 list = addList(*s, "Work, Home");
        const qint64 tag = addTag(*s, "urgent");
        const qint64 tag2 = addTag(*s, "phone");
        Task parent;
        parent.listId = list;
        parent.title = "Big \"project\"";
        parent.type = TaskType::Project;
        const qint64 pid = addTask(*s, parent);
        Task child;
        child.listId = list;
        child.parentId = pid;
        child.title = "Child";
        child.notes = "line1\nline2, with comma";
        child.priority = Priority::High;
        child.starred = true;
        child.dueDate = QDate(2026, 7, 15);
        child.dueTime = 9 * 60 + 5;
        child.startDate = QDate(2026, 7, 14);
        child.duration = 30;
        child.tagIds = {tag, tag2};
        child.recurrence.frequency = Repeat::Daily;
        addTask(*s, child);

        QString err;
        QVERIFY2(Exchange::exportCsv(*s, file("out.csv"), {}, &err), qPrintable(err));
        QFile f(file("out.csv"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QVERIFY(raw.startsWith("\xEF\xBB\xBF"));
        const QString text = QString::fromUtf8(raw.mid(3));
        QVERIFY(text.startsWith("Title,List,Type,Parent,Notes,Priority,Starred,Start,Due,Duration,Tags,Repeat,Completed,Completed At,Created\r\n"));
        QVERIFY(text.contains("\"Big \"\"project\"\"\",\"Work, Home\",Project,"));
        QVERIFY(text.contains("Child,\"Work, Home\",Task,\"Big \"\"project\"\"\",\"line1\nline2, with comma\",High,Yes,2026-07-14,2026-07-15 09:05,30,\"urgent, phone\",Daily,No,"));

        // A subset of ids exports only those.
        QVERIFY(Exchange::exportCsv(*s, file("one.csv"), {pid}, &err));
        QFile g(file("one.csv"));
        QVERIFY(g.open(QIODevice::ReadOnly));
        QCOMPARE(g.readAll().count("\r\n"), 2);
    }

    void icsFormat() {
        auto s = freshStore();
        const qint64 list = addList(*s, "L");
        Task t;
        t.listId = list;
        t.title = "Semi; comma, back\\slash " + QString(60, QChar(0x00e9));
        t.notes = "a\nb";
        t.priority = Priority::Low;
        t.dueDate = QDate(2026, 7, 15);
        t.startDate = QDate(2026, 7, 14);
        t.startTime = 8 * 60;
        t.recurrence.frequency = Repeat::Weekdays;
        addTask(*s, t);
        QString err;
        QVERIFY2(Exchange::exportIcs(*s, file("a.ics"), {}, &err), qPrintable(err));
        QFile f(file("a.ics"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        for (const QByteArray &line : raw.split('\n'))
            QVERIFY2(line.size() <= 76, line.constData());  // 75 octets plus the CR
        QString text = QString::fromUtf8(raw);
        text.replace("\r\n ", "");
        QVERIFY(text.contains("VERSION:2.0\r\n"));
        QVERIFY(text.contains("PRODID:-//Fairfax//EN"));
        QVERIFY(text.contains("DUE;VALUE=DATE:20260715"));
        QVERIFY(text.contains("DTSTART:20260714T080000"));
        QVERIFY(text.contains("PRIORITY:9"));
        QVERIFY(text.contains("STATUS:NEEDS-ACTION"));
        QVERIFY(text.contains("RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"));
        QVERIFY(text.contains("DESCRIPTION:a\\nb"));
        QVERIFY(text.contains("Semi\\; comma\\, back\\\\slash"));
    }

    void icsRoundTrip() {
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
        QVERIFY2(Exchange::exportIcs(*src, file("rt.ics"), {}, &err), qPrintable(err));

        auto dst = freshStore();
        const qint64 fallback = addList(*dst, "Fallback");
        const int listsBefore = dst->lists().size();
        const ImportResult r = Exchange::importIcs(*dst, file("rt.ics"), fallback);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 3);
        QCOMPARE(r.tags, 2);
        QCOMPARE(r.alarms, 2);
        QCOMPARE(r.lists, 2);
        QCOMPARE(dst->lists().size(), listsBefore + 2);

        const Task p2 = byTitle(*dst, "Proj");
        const Task c2 = byTitle(*dst, "Child");
        const Task w2 = byTitle(*dst, "Weekly");
        QCOMPARE(p2.type, int(TaskType::Project));
        QCOMPARE(c2.parentId, p2.id);
        QCOMPARE(dst->list(c2.listId).name, QString("Work"));
        QCOMPARE(dst->list(w2.listId).name, QString("Home"));
        QCOMPARE(c2.priority, int(Priority::Medium));
        QCOMPARE(c2.dueDate, QDate(2026, 8, 3));
        QCOMPARE(c2.dueTime, 14 * 60 + 30);
        QCOMPARE(c2.startDate, QDate(2026, 8, 1));
        QCOMPARE(c2.startTime, -1);
        QCOMPARE(c2.duration, 45);
        QVERIFY(c2.starred);
        QCOMPARE(c2.notes, child.notes);
        QStringList names;
        for (qint64 id : c2.tagIds)
            names << dst->tag(id).name;
        names.sort();
        QCOMPARE(names, (QStringList{"Alpha", "beta, gamma"}));
        QCOMPARE(c2.recurrence, child.recurrence);
        QVERIFY(c2.uid != child.uid);

        QCOMPARE(w2.recurrence, weekly.recurrence);
        QVERIFY(w2.completed);
        QCOMPARE(w2.completedAt.toSecsSinceEpoch(), weekly.completedAt.toSecsSinceEpoch());
        QCOMPARE(w2.priority, int(Priority::High));

        const auto ca = dst->alarmsFor(c2.id);
        QCOMPARE(ca.size(), 1);
        QCOMPARE(ca[0].kind, int(Alarm::Relative));
        QCOMPARE(ca[0].offset, 15);
        const auto wa = dst->alarmsFor(w2.id);
        QCOMPARE(wa.size(), 1);
        QCOMPARE(wa[0].kind, int(Alarm::Absolute));
        QCOMPARE(wa[0].at, abs.at);

        // One change undoes the whole import.
        QVERIFY(dst->undo());
        QVERIFY(byTitle(*dst, "Proj").id == 0);
        QCOMPARE(dst->lists().size(), listsBefore);
    }

    void icsForeignFile() {
        const QString ics =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nSUMMARY:Meeting\r\nEND:VEVENT\r\n"
            "BEGIN:VTODO\r\nUID:p\r\nSUMMARY:Parent task\r\nSTATUS:COMPLETED\r\nPRIORITY:2\r\n"
            "CATEGORIES:Home,work\r\nDUE:20260710T093000Z\r\nEND:VTODO\r\n"
            "BEGIN:VTODO\r\nUID:c\r\nSUMMARY:Folded sum\r\n mary\r\nRELATED-TO:p\r\nPRIORITY:6\r\n"
            "RRULE:FREQ=DAILY;INTERVAL=3;UNTIL=20261231\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT1H30M\r\nEND:VALARM\r\nEND:VTODO\r\n"
            "BEGIN:VTODO\r\nUID:loop1\r\nSUMMARY:Loop 1\r\nRELATED-TO:loop2\r\nEND:VTODO\r\n"
            "BEGIN:VTODO\r\nUID:loop2\r\nSUMMARY:Loop 2\r\nRELATED-TO:loop1\r\nEND:VTODO\r\n"
            "END:VCALENDAR\r\n";
        QFile f(file("foreign.ics"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(ics.toUtf8());
        f.close();

        auto s = freshStore();
        addTag(*s, "HOME");
        const qint64 fallback = addList(*s, "Inbox", ListKind::Inbox);
        const ImportResult r = Exchange::importIcs(*s, file("foreign.ics"), fallback);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 4);
        QCOMPARE(r.tags, 1);  // "Home" reuses "HOME" case-insensitively
        const Task p = byTitle(*s, "Parent task");
        const Task c = byTitle(*s, "Folded summary");
        QCOMPARE(p.listId, fallback);
        QVERIFY(p.completed);
        QCOMPARE(p.priority, int(Priority::High));  // PRIORITY:2 is in the 1-4 "high" band
        QCOMPARE(c.parentId, p.id);
        QCOMPARE(c.priority, int(Priority::Low));
        QCOMPARE(c.recurrence.frequency, int(Repeat::Daily));
        QCOMPARE(c.recurrence.interval, 3);
        QCOMPARE(c.recurrence.endDate, QDate(2026, 12, 31));
        QCOMPARE(s->alarmsFor(c.id).first().offset, 90);
        QCOMPARE(p.tagIds.size(), 2);
        QVERIFY(byTitle(*s, "Loop 1").parentId == 0 || byTitle(*s, "Loop 2").parentId == 0);
    }

    void icsMissingFile() {
        auto s = freshStore();
        const ImportResult r = Exchange::importIcs(*s, file("nope.ics"), 1);
        QVERIFY(!r.error.isEmpty());
    }

    void twoDoImport() {
        const QString dbFile = file("tasks.db");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "maketaskdb");
            db.setDatabaseName(dbFile);
            QVERIFY(db.open());
            const QStringList sql{
                R"(CREATE TABLE calgroups (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(100), groupname VARCHAR(255) NOT NULL,
                   displayorder INTEGER NOT NULL DEFAULT 0, isdeleted INTEGER NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE calendars (primid INTEGER PRIMARY KEY NOT NULL, title TEXT, uid VARCHAR(255) NOT NULL,
                   colorid INTEGER DEFAULT 0, displayorder INTEGER DEFAULT 0, canshowinall INTEGER NOT NULL DEFAULT 1,
                   canshowintoday INTEGER NOT NULL DEFAULT 1, isdeleted INTEGER NOT NULL DEFAULT 0, smartsearch VARCHAR(255),
                   showindone INTEGER NOT NULL DEFAULT 1, starred INTEGER NOT NULL DEFAULT 0, donecal INTEGER NOT NULL DEFAULT 0,
                   isscheduled INTEGER NOT NULL DEFAULT 0, isallcal INTEGER NOT NULL DEFAULT 0, istodaycal INTEGER NOT NULL DEFAULT 0,
                   parentuid VARCHAR(100), isarchived INTEGER NOT NULL DEFAULT 0, isinboxcal INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE taggroups (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(100), groupname VARCHAR(255) NOT NULL,
                   displayorder INTEGER NOT NULL DEFAULT 0, isdeleted INTEGER NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE tags (primid INTEGER PRIMARY KEY NOT NULL, tag VARCHAR(255) NOT NULL, displayorder INTEGER NOT NULL DEFAULT 0,
                   isdeleted INTEGER NOT NULL DEFAULT 0, uid VARCHAR(100), parentuid VARCHAR(100), isheld INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE tasks (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(255) NOT NULL, calendaruid VARCHAR(255) NOT NULL,
                   title TEXT NOT NULL, notes TEXT, datestamp DOUBLE NOT NULL DEFAULT 0, duedate DOUBLE DEFAULT 0,
                   priority INTEGER NOT NULL DEFAULT 0, iscompleted INTEGER NOT NULL DEFAULT 0, completeddate INTEGER DEFAULT 0,
                   isdeleted INTEGER NOT NULL DEFAULT 0, tags TEXT, actionvalue TEXT, actiontype INTEGER NOT NULL DEFAULT -1,
                   displayorder INTEGER NOT NULL DEFAULT 0, recurrence INTEGER NOT NULL DEFAULT 0, repeatvalue INTEGER NOT NULL DEFAULT 0,
                   repeattype INTEGER NOT NULL DEFAULT 0, parent VARCHAR(255), tasktype INTEGER NOT NULL DEFAULT 0,
                   startdate DOUBLE DEFAULT 0, duetime DOUBLE DEFAULT 999999, archived INTEGER DEFAULT 0,
                   starred INTEGER NOT NULL DEFAULT 0, recurrenceendtype INTEGER NOT NULL DEFAULT 0,
                   recurrenceendrepeats INTEGER NOT NULL DEFAULT 0, recurrenceenddate DOUBLE NOT NULL DEFAULT 0,
                   creationstamp DOUBLE NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0,
                   duration INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE alarms (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(255) NOT NULL, taskuid VARCHAR(255) NOT NULL,
                   action TEXT NOT NULL, relativetrigger DOUBLE DEFAULT 0, absolutetrigger DOUBLE DEFAULT 0,
                   isdeleted INTEGER NOT NULL DEFAULT 0))",
                "INSERT INTO calgroups(uid,groupname,displayorder) VALUES('g1','Personal',1)",
                "INSERT INTO calendars(title,uid,colorid,displayorder,isinboxcal) VALUES('Inbox','c0',0,0,1)",
                "INSERT INTO calendars(title,uid,colorid,displayorder,parentuid) VALUES('Errands','c1',14,1,'g1')",
                "INSERT INTO calendars(title,uid,displayorder,smartsearch) VALUES('Hot','c2',2,'priority:high')",
                "INSERT INTO calendars(title,uid,isallcal) VALUES('All','c3',1)",
                "INSERT INTO calendars(title,uid,istodaycal) VALUES('Today','c4',1)",
                "INSERT INTO calendars(title,uid,starred) VALUES('Starred','c5',1)",
                "INSERT INTO calendars(title,uid,isdeleted) VALUES('Gone','c6',1)",
                "INSERT INTO taggroups(uid,groupname) VALUES('tg1','Contexts')",
                "INSERT INTO tags(tag,uid,parentuid,isheld) VALUES('phone','t1','tg1',1)",
                // 1783987200 = 2026-07-14 00:00 UTC; duetime 36000 s = 10:00
                "INSERT INTO tasks(uid,calendaruid,title,notes,duedate,duetime,startdate,priority,starred,tags,tasktype,duration,"
                "  recurrence,repeatvalue,repeattype,recurrenceendtype,recurrenceendrepeats,actiontype,actionvalue,displayorder)"
                " VALUES('k1','c1','Buy milk','2%',1783987200,36000,1783900800,3,1,'phone, newtag',2,5400,2,2,1,1,5,0,'555-1234',1)",
                "INSERT INTO tasks(uid,calendaruid,title,parent,tasktype,iscompleted,completeddate,duedate,displayorder)"
                " VALUES('k2','c1','Sub item','k1',0,1,1784000000,0,2)",
                "INSERT INTO tasks(uid,calendaruid,title,archived) VALUES('k3','c0','Archived',1)",
                "INSERT INTO tasks(uid,calendaruid,title,isdeleted) VALUES('k4','c1','Deleted',1)",
                "INSERT INTO tasks(uid,calendaruid,title) VALUES('k5','c3','In focus list')",
                "INSERT INTO alarms(uid,taskuid,action,relativetrigger) VALUES('a1','k1','n',-900)",
                "INSERT INTO alarms(uid,taskuid,action,absolutetrigger) VALUES('a2','k1','n',1783990000)",
                "INSERT INTO alarms(uid,taskuid,action,relativetrigger,isdeleted) VALUES('a3','k1','n',60,1)",
            };
            for (const QString &q : sql)
                { const QString e = exec(db, q); QVERIFY2(e.isEmpty(), qPrintable(e)); }
            db.close();
        }
        QSqlDatabase::removeDatabase("maketaskdb");

        auto s = freshStore();
        const ImportResult r = Exchange::importTwoDo(*s, dbFile);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 4);  // k1, k2, k3, k5 (k5's list is skipped, so it lands in the inbox)
        QCOMPARE(r.alarms, 2);
        QCOMPARE(r.tags, 2);   // phone, newtag

        QHash<QString, TaskList> lists;
        for (const TaskList &l : s->lists())
            lists.insert(l.name, l);
        QVERIFY(lists.contains("Inbox"));
        QCOMPARE(lists["Inbox"].kind, int(ListKind::Inbox));
        QVERIFY(lists.contains("Errands"));
        QVERIFY(lists.contains("Hot"));
        QCOMPARE(lists["Hot"].kind, int(ListKind::Smart));
        QCOMPARE(lists["Hot"].smartQuery, QString("priority:high"));
        QVERIFY(!lists.contains("All"));
        QVERIFY(!lists.contains("Today"));
        QVERIFY(!lists.contains("Starred"));
        QVERIFY(!lists.contains("Gone"));
        QCOMPARE(lists["Errands"].color, 14 % 13);
        QCOMPARE(r.lists, 3);
        QCOMPARE(s->groups().size(), 1);
        QCOMPARE(s->groups().cbegin().value().name, QString("Personal"));
        QCOMPARE(lists["Errands"].groupId, s->groups().cbegin().value().id);

        const Task milk = byTitle(*s, "Buy milk");
        QCOMPARE(milk.listId, lists["Errands"].id);
        QCOMPARE(milk.notes, QString("2%"));
        QCOMPARE(milk.type, int(TaskType::Project));
        QCOMPARE(milk.priority, int(Priority::High));
        QVERIFY(milk.starred);
        QCOMPARE(milk.dueDate, QDate(2026, 7, 14));
        QCOMPARE(milk.dueTime, 600);
        QCOMPARE(milk.startDate, QDate(2026, 7, 13));
        QCOMPARE(milk.duration, 90);
        QCOMPARE(milk.actionType, int(ActionType::Call));
        QCOMPARE(milk.actionValue, QString("555-1234"));
        QCOMPARE(milk.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(milk.recurrence.interval, 2);
        QCOMPARE(milk.recurrence.from, int(Repeat::FromCompletion));
        QCOMPARE(milk.recurrence.endType, int(Repeat::AfterCount));
        QCOMPARE(milk.recurrence.endCount, 5);
        QCOMPARE(milk.tagIds.size(), 2);
        bool paused = false;
        for (const Tag &t : s->tags())
            if (t.name == "phone")
                paused = t.paused && t.groupId != 0;
        QVERIFY(paused);

        const Task sub = byTitle(*s, "Sub item");
        QCOMPARE(sub.parentId, milk.id);
        QVERIFY(sub.completed);
        QVERIFY(!sub.dueDate.isValid());
        QCOMPARE(sub.dueTime, -1);
        QVERIFY(byTitle(*s, "Archived").completed);
        QVERIFY(byTitle(*s, "Deleted").id == 0);
        QCOMPARE(byTitle(*s, "In focus list").listId, lists["Inbox"].id);

        int rel = 0, abs = 0;
        for (const Alarm &a : s->alarmsFor(milk.id)) {
            if (a.kind == Alarm::Relative) { ++rel; QCOMPARE(a.offset, 15); }
            else { ++abs; QCOMPARE(a.at.toSecsSinceEpoch(), qint64(1783990000)); }
        }
        QCOMPARE(rel, 1);
        QCOMPARE(abs, 1);

        QVERIFY(s->undo());
        QVERIFY(s->tasks().isEmpty());
        QVERIFY(s->groups().isEmpty());
    }

    void twoDoErrors() {
        auto s = freshStore();
        QVERIFY(!Exchange::importTwoDo(*s, file("missing.db")).error.isEmpty());

        QFile junk(file("junk.db"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(5000, 'x'));
        junk.close();
        QVERIFY(!Exchange::importTwoDo(*s, file("junk.db")).error.isEmpty());

        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "emptytaskdb");
            db.setDatabaseName(file("empty.db"));
            QVERIFY(db.open());
            QVERIFY(exec(db, "CREATE TABLE other (x INTEGER)").isEmpty());
            db.close();
        }
        QSqlDatabase::removeDatabase("emptytaskdb");
        const ImportResult r = Exchange::importTwoDo(*s, file("empty.db"));
        QVERIFY(r.error.contains("tasks") || r.error.contains("calendars"));
        QVERIFY(!s->canUndo());
    }

    void selectTasksByListAndScope() {
        auto s = freshStore();
        const qint64 a = addList(*s, "A"), b = addList(*s, "B");
        Task open;
        open.title = "open";
        open.listId = a;
        Task done = open;
        done.title = "done";
        done.completed = true;
        Task other = open;
        other.title = "elsewhere";
        other.listId = b;
        const qint64 openId = addTask(*s, open), doneId = addTask(*s, done), otherId = addTask(*s, other);
        QCOMPARE(Exchange::selectTasks(*s, {a}, Exchange::Scope::All), (QList<qint64>{openId, doneId}));
        QCOMPARE(Exchange::selectTasks(*s, {a}, Exchange::Scope::NotDone), (QList<qint64>{openId}));
        QCOMPARE(Exchange::selectTasks(*s, {a, b}, Exchange::Scope::Done), (QList<qint64>{doneId}));
        QCOMPARE(Exchange::selectTasks(*s, {b}, Exchange::Scope::All), (QList<qint64>{otherId}));
        QVERIFY(Exchange::selectTasks(*s, {}, Exchange::Scope::All).isEmpty());
    }

    void eventIcsTimedAndAllDay() {
        auto s = freshStore();
        const qint64 list = addList(*s, "A");
        Task t;
        t.title = "Dentist, 3rd floor";
        t.notes = "Bring forms";
        t.listId = list;
        t.dueDate = QDate(2026, 10, 14);
        t.dueTime = 23 * 60 + 30;
        t.duration = 45;
        t.id = addTask(*s, t);
        const QString timed = QString::fromUtf8(Exchange::eventIcs(*s, t));
        QVERIFY(timed.contains("BEGIN:VEVENT"));
        QVERIFY(!timed.contains("VTODO"));
        QVERIFY(timed.contains("SUMMARY:Dentist\\, 3rd floor"));
        QVERIFY(timed.contains("DTSTART:20261014T233000"));
        QVERIFY(timed.contains("DTEND:20261015T001500"));  // 45 minutes, across midnight.
        QVERIFY(timed.contains("DESCRIPTION:Bring forms"));

        t.dueTime = -1;
        const QString allDay = QString::fromUtf8(Exchange::eventIcs(*s, t));
        QVERIFY(allDay.contains("DTSTART;VALUE=DATE:20261014"));
        QVERIFY(allDay.contains("DTEND;VALUE=DATE:20261015"));

        t.dueDate = QDate();
        QVERIFY(Exchange::eventIcs(*s, t).isEmpty());
        t.startDate = QDate(2026, 10, 20);
        QVERIFY(QString::fromUtf8(Exchange::eventIcs(*s, t)).contains("DTSTART;VALUE=DATE:20261020"));
    }

    void thingsJson() {
        auto s = freshStore();
        const QByteArray json = R"([
          {"type":"project","uuid":"P1","title":"Garden","area":"Home","notes":"Spring work","deadline":"2026-07-04",
           "items":[{"type":"to-do","uuid":"T1","title":"Buy seeds","tags":["errand","shop"],"when":"2026-06-20"},
                    {"type":"to-do","uuid":"T2","title":"Done thing","status":"completed","completion_date":"2026-06-01T10:00:00Z"}]},
          {"type":"to-do","uuid":"T3","title":"Plan trip","checklist":[{"title":"Passport"},{"title":"Tickets","completed":true}]},
          {"type":"to-do","attributes":{"title":"Wrapped","deadline":"2026-08-01","tags":["x"],"notes":"hi"}},
          {"type":"to-do","uuid":"T4","title":"Dropped","status":"canceled"}
        ])";
        const ImportResult r = Exchange::importThingsJson(*s, json);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 6);  // Checklist items are not counted.
        const Task garden = byTitle(*s, "Garden");
        QCOMPARE(garden.type, int(TaskType::Project));
        QCOMPARE(garden.dueDate, QDate(2026, 7, 4));
        QCOMPARE(s->list(garden.listId).name, "Home");
        const Task seeds = byTitle(*s, "Buy seeds");
        QCOMPARE(seeds.parentId, garden.id);
        QCOMPARE(seeds.startDate, QDate(2026, 6, 20));
        QCOMPARE(seeds.tagIds.size(), 2);
        QVERIFY(byTitle(*s, "Done thing").completed);
        const Task trip = byTitle(*s, "Plan trip");
        QCOMPARE(trip.type, int(TaskType::Checklist));
        QCOMPARE(s->list(trip.listId).name, "Inbox");
        QVERIFY(byTitle(*s, "Tickets").completed);
        QVERIFY(!byTitle(*s, "Passport").completed);
        QCOMPARE(byTitle(*s, "Wrapped").dueDate, QDate(2026, 8, 1));
        QCOMPARE(byTitle(*s, "Wrapped").notes, "hi");
        QVERIFY(byTitle(*s, "Dropped").completed);

        // Importing again adds nothing for items that carry an id.
        const ImportResult again = Exchange::importThingsJson(*s, json);
        QCOMPARE(again.tasks, 1);  // Only the one without an id.
        QVERIFY(!Exchange::importThingsJson(*s, "nope").error.isEmpty());
        QVERIFY(!Exchange::importThingsJson(*s, "[]").error.isEmpty());
    }

    void omniFocusTaskPaper() {
        auto s = freshStore();
        const QString text = QString::fromUtf8(
            "- Inbox thing @flagged\n"
            "Work:\n"
            "\tWebsite:\n"
            "\t\t- Write copy @due(2026-07-04 17:00) @tags(writing, urgent) @estimate(1h30m)\n"
            "\t\t\tNeeds the brief.\n"
            "\t\t\t- Outline @home\n"
            "\t\t- Ship it @defer(2026-07-01) @done(2026-07-02)\n"
            "Personal:\n"
            "\t- Call mom @due(2026-07-10)\n");
        const ImportResult r = Exchange::importTaskPaperText(*s, text);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 5);
        QCOMPARE(s->list(byTitle(*s, "Inbox thing").listId).name, "Inbox");
        QVERIFY(byTitle(*s, "Inbox thing").starred);
        const Task copy = byTitle(*s, "Write copy");
        QCOMPARE(copy.dueDate, QDate(2026, 7, 4));
        QCOMPARE(copy.dueTime, 17 * 60);
        QCOMPARE(copy.duration, 90);
        QCOMPARE(copy.notes, "Needs the brief.");
        QCOMPARE(copy.tagIds.size(), 2);
        QCOMPARE(copy.type, int(TaskType::Project));
        QCOMPARE(byTitle(*s, "Outline").parentId, copy.id);
        QCOMPARE(byTitle(*s, "Outline").tagIds.size(), 1);
        const Task ship = byTitle(*s, "Ship it");
        QVERIFY(ship.completed);
        QCOMPARE(ship.startDate, QDate(2026, 7, 1));
        QCOMPARE(ship.parentId, 0);
        // The folder became a group holding the Website list; Personal is its own list.
        const TaskList website = s->list(copy.listId);
        QCOMPARE(website.name, "Website");
        QCOMPARE(s->groups().value(website.groupId).name, "Work");
        QCOMPARE(s->list(byTitle(*s, "Call mom").listId).groupId, qint64(0));
        QVERIFY(!Exchange::importTaskPaperText(*s, "").error.isEmpty());
    }

    void tasksAsTextAndMailto() {
        auto s = freshStore();
        const qint64 list = addList(*s, "Home");
        Task parent;
        parent.listId = list;
        parent.title = "Trip";
        parent.type = TaskType::Checklist;
        parent.dueDate = QDate(2026, 7, 4);
        parent.notes = "Bring snacks\nand water";
        const qint64 pid = addTask(*s, parent);
        Task child;
        child.listId = list;
        child.parentId = pid;
        child.title = "Passport";
        child.completed = true;
        const qint64 cid = addTask(*s, child);
        Task other;
        other.listId = list;
        other.title = "Mail & more";
        const qint64 oid = addTask(*s, other);

        const QString text = Exchange::tasksAsText(*s, {pid, cid, oid});
        QVERIFY(text.startsWith("[ ] Trip  (due "));
        QVERIFY(text.contains("    Bring snacks\n    and water\n"));
        QVERIFY(text.contains("    [x] Passport\n"));
        QVERIFY(text.contains("[ ] Mail & more\n"));
        // The sub-task appears once, under its parent.
        QCOMPARE(text.count("Passport"), 1);

        const QUrl one = Exchange::mailtoForTasks(*s, {oid});
        QCOMPARE(one.scheme(), "mailto");
        const QString raw = one.toString(QUrl::FullyEncoded);
        QVERIFY(raw.contains("subject=Mail%20%26%20more"));
        QVERIFY(raw.contains("body=%5B%20%5D%20Mail%20%26%20more"));
        QVERIFY(Exchange::mailtoForTasks(*s, {pid, oid}).toString(QUrl::FullyEncoded).contains("subject=2%20tasks"));
    }

    void printHtmlAndPdf() {
        auto s = freshStore();
        Exchange::PrintItem item;
        item.title = "Pay <rent>";
        item.due = "Fri";
        item.notes = "Landlord";
        item.starred = true;
        Exchange::PrintItem done;
        done.title = "Done thing";
        done.done = true;
        done.depth = 1;
        const QVector<Exchange::PrintSection> sections = {{"Overdue", {item, done}}, {"Empty", {}}};
        const QString withNotes = Exchange::printHtml("Home", sections, true);
        QVERIFY(withNotes.contains("Pay &lt;rent&gt;"));
        QVERIFY(withNotes.contains("Landlord"));
        QVERIFY(withNotes.contains("Overdue"));
        QVERIFY(!withNotes.contains("Empty"));
        QVERIFY(withNotes.contains("<s style"));
        QVERIFY(!Exchange::printHtml("Home", sections, false).contains("Landlord"));

        const QString pdf = file("out.pdf");
        QString error;
        QVERIFY2(Exchange::writePdf(pdf, "Home", sections, true, &error), qPrintable(error));
        QFile f(pdf);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().startsWith("%PDF-"));
        QVERIFY(!Exchange::writePdf(file("missing/dir/out.pdf"), "Home", sections, true, &error));
    }

    void emlHeaders() {
        const QByteArray mail =
            "From: =?UTF-8?B?SsO8cmdlbg==?= <j@example.com>\r\n"
            "Subject: =?UTF-8?Q?Caf=C3=A9_menu?=\r\n =?UTF-8?Q?_for_Friday?=\r\n"
            "Message-ID: <abc.123@mail.example.com>\r\n"
            "\r\nSubject: not a header\r\n";
        const Exchange::Eml eml = Exchange::parseEml(mail);
        QCOMPARE(eml.subject, QString::fromUtf8("Café menu for Friday"));
        QCOMPARE(eml.from, QString::fromUtf8("Jürgen <j@example.com>"));
        QCOMPARE(eml.messageId, "abc.123@mail.example.com");
        const Exchange::Eml plain = Exchange::parseEml("Subject: Hello\nmessage-id: <x@y>\n\nbody");
        QCOMPARE(plain.subject, "Hello");
        QCOMPARE(plain.messageId, "x@y");
        QVERIFY(Exchange::parseEml("just text").subject.isEmpty());
    }
};

REGISTER_TEST(ExchangeTest)
#include "test_exchange.moc"
