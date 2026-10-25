// End-to-end tests: getting tasks in and out. The Export dialog's choices and
// the file it asks for, an .ics export read back into a fresh database, imports
// from each supported source through Settings > Import & Export (fixture files
// under tests/e2e/fixtures), Print / Save as PDF, the task menu's export and
// event entries, and the backup flows for getting a deleted task back.

#include <QDesktopServices>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "misckit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::mk;

namespace {

QString fixture(const QString &name) { return QString(SRCDIR) + "/../tests/e2e/fixtures/" + name; }

// Catches the files the app asks the desktop to open (the PDF to print, the
// event to add to a calendar), so none is opened for real.
class UrlSink : public QObject {
    Q_OBJECT
public slots:
    void opened(const QUrl &url) { counters().urls << url.toString(); }
};

// What a control that opens the file chooser asked for. The chooser is a
// portal dialog on the desktop; the tests have no session bus, so a request is
// reported as failed and the answer is sent by hand.
struct Asked {
    QString purpose, name;
    bool ok = false;
};

template <class Trigger>
Asked asked(App &app, Trigger trigger) {
    int failures = 0;
    const auto link = QObject::connect(&app.services().filePicker, &FilePicker::failed, &app.backend(), [&] { ++failures; });
    trigger();
    Asked a;
    a.ok = waitUntil([&] { return failures > 0; });
    QObject::disconnect(link);
    a.purpose = app.services().filePicker.lastPurpose();
    a.name = app.services().filePicker.lastSuggestedName();
    return a;
}

// The task menu is taller than the popup window the platform gives it, and its
// last entries can sit out of reach of the pointer, so the entry's action is run
// as a click on it would (the menu is then closed).
bool pickLowEntry(const QString &text) {
    QQuickItem *item = nullptr;
    if (!waitUntil([&] { return (item = findText(text, "QQuickMenuItem")) != nullptr; }))
        return false;
    QWindow *w = item->window();
    QMetaObject::invokeMethod(item, "triggered");
    QTest::keyClick(w, Qt::Key_Escape);
    return waitUntil([] { return popupsClosed(); });
}

void answer(App &app, const QString &path, const QString &purpose) { emit app.services().filePicker.picked(path, purpose); }

// A world with one of everything an export should keep.
Seed seedRich(Store &store) {
    Seed s = seedMisc(store);
    const QDate today = QDate::currentDate();
    store.begin("Seed rich", false, true);
    Tag rent;
    rent.id = store.nextId("tags");
    rent.name = "rent";
    store.putTag(rent);
    Task t;
    t.id = store.nextId("tasks");
    t.listId = s.home;
    t.title = "Pay rent, on time";
    t.notes = "Landlord: call first\nSecond line; with a semicolon";
    t.dueDate = today.addDays(5);
    t.dueTime = 9 * 60 + 30;
    t.startDate = today.addDays(-1);
    t.priority = Priority::High;
    t.starred = true;
    t.duration = 45;
    t.tagIds << rent.id << mw().mango;
    t.recurrence.frequency = Repeat::Monthly;
    t.recurrence.interval = 2;
    t.order = 50;
    store.putTask(t);
    Alarm alarm;
    alarm.id = store.nextId("alarms");
    alarm.taskId = t.id;
    alarm.kind = Alarm::Relative;
    alarm.offset = 15;
    store.putAlarm(alarm);
    Task done;
    done.id = store.nextId("tasks");
    done.listId = s.home;
    done.title = "Renew the licence";
    done.completed = true;
    done.completedAt = QDateTime::currentDateTime().addDays(-2);
    done.order = 51;
    store.putTask(done);
    store.commit();
    return s;
}

// A CSV file as rows of fields (quotes and embedded line breaks honoured; the BOM dropped).
QList<QStringList> parseCsv(QString text) {
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            row << field;
            field.clear();
        } else if (c == '\r') {
            continue;
        } else if (c == '\n') {
            row << field;
            rows << row;
            row.clear();
            field.clear();
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !row.isEmpty()) { row << field; rows << row; }
    return rows;
}

QStringList column(const QList<QStringList> &rows, int index) {
    QStringList out;
    for (int i = 1; i < rows.size(); ++i) out << rows.at(i).value(index);
    out.sort();
    return out;
}

QStringList liveTitles(App &app) {
    QStringList out;
    for (const Task &t : app.store().tasks())
        if (!t.deleted) out << t.title;
    out.sort();
    return out;
}

Task named(App &app, const QString &title) { return app.taskTitled(title); }
QString listName(App &app, const Task &t) { return app.store().list(t.listId).name; }
qint64 listIdNamed(App &app, const QString &name) {
    for (const TaskList &l : app.store().lists())
        if (!l.deleted && l.name == name) return l.id;
    return 0;
}

QStringList backupFiles(App &app) {
    return QDir(app.dir() + "/backups").entryList({"Fairfax_*.db"}, QDir::Files, QDir::Time);
}

// Imports a fixture the way a user does: Settings > Import & Export, the
// button, and the file chooser's answer.
void importFixture(App &app, const QString &button, const QString &purpose, const QString &file) {
    QQuickWindow *sw = nullptr;
    openSettings(app, sw);
    selectPane(sw, "Import & Export");
    QQuickItem *b = nullptr;
    control(button, b);
    const Asked a = asked(app, [&] { click(b); });
    QVERIFY2(a.ok, "the import button asked for no file");
    QCOMPARE(a.purpose, purpose);
    answer(app, file, a.purpose);
    focusMain(app);
}

}  // namespace

class E2EMiscIO : public QObject {
    Q_OBJECT
    UrlSink m_sink;

private slots:
    void initTestCase() {
        for (const char *scheme : {"file", "mailto"})
            QDesktopServices::setUrlHandler(scheme, &m_sink, "opened");
    }
    void cleanupTestCase() {
        for (const char *scheme : {"file", "mailto"})
            QDesktopServices::unsetUrlHandler(scheme);
    }

    // "Export > Spreadsheet (.csv)": the dialog (Ctrl+Shift+D) lists the lists,
    // a scope and a format; Export… asks for a file, and what is written is
    // the ticked lists' tasks of that scope.
    void exportDialogWritesTheChosenListsAsCsv() {
        QTemporaryDir out;
        App app(seedRich);
        QVERIFY(app.ok);
        app.press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Export Tasks") != nullptr, 5000);
        QQuickItem *go = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((go = findText("Export…", "QQuickAbstractButton")) != nullptr, 5000);
        QVERIFY(go->isEnabled());
        // Everything ticked to begin with; Select all clears and sets them.
        QQuickItem *all = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((all = findItem("exportSelectAll")) != nullptr && all->property("checked").toBool(), 5000);
        const QList<qint64> lists{app.ids.home, app.ids.work, mw().sorting};
        for (qint64 id : lists)
            QVERIFY(checked(QString("exportList:%1").arg(id)));
        clickSteady(all);
        QTRY_VERIFY_WITH_TIMEOUT(!go->isEnabled(), 5000);   // Nothing ticked: nothing to export.
        for (qint64 id : lists)
            QVERIFY(!checked(QString("exportList:%1").arg(id)));
        clickSteady(findItem(QString("exportList:%1").arg(app.ids.home)));
        QTRY_VERIFY_WITH_TIMEOUT(go->isEnabled(), 5000);
        QVERIFY(pickCombo("exportScope", "Not Done"));
        QCOMPARE(findItem("exportFormat")->property("currentText").toString(), QString("Spreadsheet (.csv)"));

        const QString file = out.filePath("home.csv");
        const Asked a = asked(app, [&] { clickSteady(go); });
        QVERIFY(a.ok);
        QCOMPARE(a.purpose, QString("export:main:csv:1:%1").arg(app.ids.home));
        QCOMPARE(a.name, QString("Fairfax.csv"));
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Export Tasks"), 5000);
        answer(app, file, a.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(file).size() > 0, 5000);

        const QList<QStringList> rows = parseCsv(readFile(file));
        QCOMPARE(rows.first(), (QStringList{"Title", "List", "Type", "Parent", "Notes", "Priority", "Starred", "Start", "Due", "Duration",
                                            "Tags", "Repeat", "Completed", "Completed At", "Created"}));
        QCOMPARE(column(rows, 0), (QStringList{"Buy milk", "Fix the shelf", "Pay electricity bill", "Pay rent, on time", "Water the plants"}));
        QCOMPARE(column(rows, 1), QStringList(5, "Home"));
        // The rent task keeps every field in its column.
        QStringList rent;
        for (const QStringList &r : rows)
            if (r.value(0) == "Pay rent, on time") rent = r;
        QCOMPARE(rent.value(4), QString("Landlord: call first\nSecond line; with a semicolon"));
        QCOMPARE(rent.value(5), QString("High"));
        QCOMPARE(rent.value(6), QString("Yes"));
        QCOMPARE(rent.value(7), QDate::currentDate().addDays(-1).toString(Qt::ISODate));
        QVERIFY2(rent.value(8).startsWith(QDate::currentDate().addDays(5).toString(Qt::ISODate)) && rent.value(8).contains("09:30"), qPrintable(rent.value(8)));
        QCOMPARE(rent.value(9), QString("45"));
        QCOMPARE(rent.value(10), QString("rent, mango"));
        QVERIFY2(rent.value(11).contains("2") && rent.value(11).contains("month", Qt::CaseInsensitive), qPrintable(rent.value(11)));
        QCOMPARE(rent.value(12), QString("No"));

        // The "Done" scope picks the completed ones.
        app.press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Export Tasks") != nullptr, 5000);
        QVERIFY(pickCombo("exportScope", "Done"));
        QQuickItem *go2 = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((go2 = findText("Export…", "QQuickAbstractButton")) != nullptr && go2->isEnabled(), 5000);
        const Asked b = asked(app, [&] { clickSteady(go2); });
        QVERIFY(b.ok);
        QVERIFY2(b.purpose.startsWith("export:main:csv:2:"), qPrintable(b.purpose));
        const QString done = out.filePath("done.csv");
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Export Tasks"), 5000);
        answer(app, done, b.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(done).size() > 0, 5000);
        QCOMPARE(column(parseCsv(readFile(done)), 0), (QStringList{"Old finished thing", "Renew the licence"}));
    }

    // Cancel writes nothing and asks for nothing.
    void cancellingTheExportDialogAsksForNoFile() {
        App app;
        QVERIFY(app.ok);
        int asks = 0;
        QObject::connect(&app.services().filePicker, &FilePicker::failed, &app.backend(), [&] { ++asks; });
        app.press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Export Tasks") != nullptr, 5000);
        QVERIFY(clickDialogButton("Cancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Export Tasks"), 5000);
        QTest::qWait(100);
        QCOMPARE(asks, 0);
    }

    // The .ics export, read back into a fresh database through Settings > Import:
    // lists, tags, dates, notes, repeats, alerts, priorities and sub-tasks survive.
    void icsExportRoundTripsIntoAFreshDatabase() {
        QTemporaryDir out;
        const QString file = out.filePath("everything.ics");
        QList<Task> before;
        QHash<qint64, QStringList> tagsBefore;
        QHash<qint64, QString> parentBefore, listBefore;
        QHash<qint64, QList<int>> alarmsBefore;
        {
            App app(seedRich);
            QVERIFY(app.ok);
            app.press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
            QTRY_VERIFY_WITH_TIMEOUT(findText("Export Tasks") != nullptr, 5000);
            QVERIFY(pickCombo("exportFormat", "Calendar file (.ics)"));
            QQuickItem *go = findText("Export…", "QQuickAbstractButton");
            QVERIFY(go && go->isEnabled());
            const Asked a = asked(app, [&] { clickSteady(go); });
            QVERIFY(a.ok);
            QVERIFY2(a.purpose.startsWith("export:main:ics:0:"), qPrintable(a.purpose));
            QCOMPARE(a.name, QString("Fairfax.ics"));
            answer(app, file, a.purpose);
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(file).size() > 0, 5000);
            for (const Task &t : app.store().tasks()) {
                if (t.deleted) continue;
                before << t;
                tagsBefore[t.id] = tk::tagNames(app, t);
                parentBefore[t.id] = t.parentId ? app.store().task(t.parentId).title : QString();
                listBefore[t.id] = app.store().list(t.listId).name;
                for (const Alarm &al : app.store().alarmsFor(t.id))
                    if (!al.deleted) alarmsBefore[t.id] << al.offset;
            }
            QVERIFY(before.size() > 15);
        }
        const QString text = readFile(file);
        QVERIFY(text.contains("BEGIN:VCALENDAR") && text.contains("SUMMARY:Pay rent\\, on time"));
        QVERIFY(text.contains("RRULE:FREQ=MONTHLY;INTERVAL=2") && text.contains("TRIGGER:-PT15M"));

        App fresh(nullptr);
        QVERIFY(fresh.ok);
        const int liveBefore = liveTitles(fresh).size();
        importFixture(fresh, "importIcs", "import-ics", file);
        QTRY_VERIFY_WITH_TIMEOUT(named(fresh, "Pay rent, on time").id != 0, 5000);
        QCOMPARE(liveTitles(fresh).size() - liveBefore, before.size());
        for (const Task &t : before) {
            const Task got = named(fresh, t.title);
            QVERIFY2(got.id != 0, qPrintable("missing after import: " + t.title));
            QCOMPARE(listName(fresh, got), listBefore[t.id]);
            QCOMPARE(got.type, t.type);
            QCOMPARE(got.notes, t.notes);
            QCOMPARE(got.dueDate, t.dueDate);
            QCOMPARE(got.dueTime, t.dueTime);
            QCOMPARE(got.startDate, t.startDate);
            QCOMPARE(got.priority, t.priority);
            QCOMPARE(got.starred, t.starred);
            QCOMPARE(got.duration, t.duration);
            QCOMPARE(got.completed, t.completed);
            QCOMPARE(got.recurrence.frequency, t.recurrence.frequency);
            QCOMPARE(got.recurrence.interval, t.recurrence.interval);
            QCOMPARE(tk::tagNames(fresh, got), tagsBefore[t.id]);
            QCOMPARE(got.parentId ? fresh.store().task(got.parentId).title : QString(), parentBefore[t.id]);
            QList<int> alarms;
            for (const Alarm &al : fresh.store().alarmsFor(got.id))
                if (!al.deleted) alarms << al.offset;
            QCOMPARE(alarms, alarmsBefore.value(t.id));
        }
        // And it shows: the lists are in the sidebar, the task in its list.
        QVERIFY(fresh.openSource(lk(listIdNamed(fresh, "Home"))));
        QTRY_VERIFY_WITH_TIMEOUT(fresh.row(named(fresh, "Pay rent, on time").id) != nullptr, 5000);
        // One Ctrl+Z undoes the whole import, and a backup was taken first.
        QVERIFY(!backupFiles(fresh).isEmpty());
        fresh.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(named(fresh, "Pay rent, on time").id == 0, 5000);
        QCOMPARE(liveTitles(fresh).size(), liveBefore);
    }

    // From TickTick: lists and folders, tags, dates in the task's own time zone,
    // repeats, checklists and completed history; the same backup again adds nothing.
    void importFromTickTick() {
        App app(nullptr);
        QVERIFY(app.ok);
        importFixture(app, "importTickTick", "import-ticktick", fixture("ticktick.csv"));
        QTRY_VERIFY_WITH_TIMEOUT(named(app, "Tune the drums").id != 0, 5000);
        const Task drums = named(app, "Tune the drums");
        QCOMPARE(listName(app, drums), QString("Sessions"));
        QCOMPARE(app.store().groups().value(app.store().list(drums.listId).groupId).name, QString("Studio"));
        QCOMPARE(drums.dueDate, QDate(2027, 3, 1));
        QCOMPARE(drums.dueTime, 10 * 60);
        QCOMPARE(drums.notes, QString("Skins first"));
        QCOMPARE(drums.priority, int(Priority::High));
        QCOMPARE(tk::tagNames(app, drums), QStringList{"audio"});
        QCOMPARE(drums.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(drums.recurrence.interval, 2);
        QCOMPARE(app.store().alarmsFor(drums.id).size(), 1);
        const Task email = named(app, "Email the label");
        QCOMPARE(listName(app, email), QString("Inbox"));
        QCOMPARE(email.dueDate, QDate(2027, 3, 2));   // All day, in Los Angeles.
        QCOMPARE(email.dueTime, -1);
        const Task gear = named(app, "Load-in");
        QCOMPARE(gear.type, int(TaskType::Checklist));
        QVERIFY(named(app, "Amps").parentId == gear.id && named(app, "Cables").parentId == gear.id);
        QVERIFY(named(app, "Old idea").completed);
        QVERIFY(!named(app, "Old idea").abandoned);
        // TickTick's "won't do" is closed but not done.
        QVERIFY(named(app, "Parked idea").completed && named(app, "Parked idea").abandoned);
        // It shows in the sidebar's lists.
        QVERIFY(app.sidebarRow(lk(drums.listId)) != nullptr);
        const int count = liveTitles(app).size();
        importFixture(app, "importTickTick", "import-ticktick", fixture("ticktick.csv"));
        QTest::qWait(300);
        QCOMPARE(liveTitles(app).size(), count);
    }

    // From Things: areas are lists, projects stay projects, checklist items stay
    // checklists; "when" is the start date and the deadline the due date.
    void importFromThings() {
        App app(nullptr);
        QVERIFY(app.ok);
        importFixture(app, "importThings", "import-things", fixture("things.json"));
        QTRY_VERIFY_WITH_TIMEOUT(named(app, "Studio move").id != 0, 5000);
        const Task project = named(app, "Studio move");
        QCOMPARE(project.type, int(TaskType::Project));
        QCOMPARE(listName(app, project), QString("Work"));
        QCOMPARE(project.dueDate, QDate(2027, 4, 2));
        QCOMPARE(project.notes, QString("Crates first"));
        const Task crates = named(app, "Order crates");
        QCOMPARE(crates.parentId, project.id);
        QCOMPARE(crates.startDate, QDate(2027, 3, 20));
        QCOMPARE(tk::tagNames(app, crates), (QStringList{"boxes", "urgent"}));
        QVERIFY(named(app, "Cancel the lease").completed && !named(app, "Cancel the lease").abandoned);
        QVERIFY(named(app, "Book the gala").completed && named(app, "Book the gala").abandoned);
        const Task kit = named(app, "Pack the kit");
        QCOMPARE(kit.type, int(TaskType::Checklist));
        QVERIFY(named(app, "Microphones").completed && !named(app, "Cables").completed);
        QCOMPARE(named(app, "Wrapped entry").dueDate, QDate(2027, 5, 1));
        QCOMPARE(named(app, "Wrapped entry").notes, QString("Wrapped notes"));
        // The new list and project are in the sidebar; the project opens.
        QVERIFY(app.sidebarRow(lk(project.listId)) != nullptr);
        // A second import adds only what carries no id.
        const int count = liveTitles(app).size();
        importFixture(app, "importThings", "import-things", fixture("things.json"));
        QTRY_COMPARE_WITH_TIMEOUT(liveTitles(app).size(), count + 1, 5000);   // "Wrapped entry" has none.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(liveTitles(app).size(), count, 5000);
    }

    // From OmniFocus (TaskPaper): folders are groups, projects lists, nested
    // items sub-tasks, @due / @defer / @done / @estimate / @flagged map across.
    void importFromOmniFocus() {
        App app(nullptr);
        QVERIFY(app.ok);
        importFixture(app, "importOmnifocus", "import-omnifocus", fixture("omnifocus.taskpaper"));
        QTRY_VERIFY_WITH_TIMEOUT(named(app, "Mix track four").id != 0, 5000);
        const Task passport = named(app, "Renew passport");
        QVERIFY(passport.starred);
        QCOMPARE(listName(app, passport), QString("Inbox"));
        const Task mix = named(app, "Mix track four");
        QCOMPARE(mix.dueDate, QDate(2027, 3, 5));
        QCOMPARE(mix.dueTime, 18 * 60 + 30);
        QCOMPARE(mix.duration, 120);
        QCOMPARE(mix.notes, QString("Check the low end."));
        QCOMPARE(tk::tagNames(app, mix), (QStringList{"audio", "deadline"}));
        QCOMPARE(mix.type, int(TaskType::Project));   // It has a sub-task.
        QCOMPARE(named(app, "Bounce stems").parentId, mix.id);
        QCOMPARE(tk::tagNames(app, named(app, "Bounce stems")), QStringList{"desk"});
        const Task master = named(app, "Master the record");
        QVERIFY(master.completed);
        QCOMPARE(master.startDate, QDate(2027, 2, 1));
        QCOMPARE(listName(app, mix), QString("Album"));
        QCOMPARE(app.store().groups().value(app.store().list(mix.listId).groupId).name, QString("Studio"));
        QCOMPARE(listName(app, named(app, "Post the parcel")), QString("Errands"));
        QCOMPARE(named(app, "Post the parcel").dueDate, QDate(2027, 3, 9));
        // OmniFocus's dropped items are closed but not done.
        QVERIFY(named(app, "Skip the fair").completed && named(app, "Skip the fair").abandoned);
        QVERIFY(!master.abandoned);
        QVERIFY(app.sidebarRow(lk(mix.listId)) != nullptr);
    }

    // From a task database file: lists, groups, tags, dates, repeats, alerts.
    void importFromTaskDb() {
        QTemporaryDir dir;
        const QString db = dir.filePath("tasks.db");
        {
            QSqlDatabase conn = QSqlDatabase::addDatabase("QSQLITE", "e2etaskdb");
            conn.setDatabaseName(db);
            QVERIFY(conn.open());
            const QStringList sql{
                "CREATE TABLE calgroups (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(100), groupname VARCHAR(255) NOT NULL,"
                " displayorder INTEGER NOT NULL DEFAULT 0, isdeleted INTEGER NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0)",
                "CREATE TABLE calendars (primid INTEGER PRIMARY KEY NOT NULL, title TEXT, uid VARCHAR(255) NOT NULL,"
                " colorid INTEGER DEFAULT 0, displayorder INTEGER DEFAULT 0, canshowinall INTEGER NOT NULL DEFAULT 1,"
                " canshowintoday INTEGER NOT NULL DEFAULT 1, isdeleted INTEGER NOT NULL DEFAULT 0, smartsearch VARCHAR(255),"
                " showindone INTEGER NOT NULL DEFAULT 1, starred INTEGER NOT NULL DEFAULT 0, donecal INTEGER NOT NULL DEFAULT 0,"
                " isscheduled INTEGER NOT NULL DEFAULT 0, isallcal INTEGER NOT NULL DEFAULT 0, istodaycal INTEGER NOT NULL DEFAULT 0,"
                " parentuid VARCHAR(100), isarchived INTEGER NOT NULL DEFAULT 0, isinboxcal INTEGER NOT NULL DEFAULT 0)",
                "CREATE TABLE taggroups (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(100), groupname VARCHAR(255) NOT NULL,"
                " displayorder INTEGER NOT NULL DEFAULT 0, isdeleted INTEGER NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0)",
                "CREATE TABLE tags (primid INTEGER PRIMARY KEY NOT NULL, tag VARCHAR(255) NOT NULL, displayorder INTEGER NOT NULL DEFAULT 0,"
                " isdeleted INTEGER NOT NULL DEFAULT 0, uid VARCHAR(100), parentuid VARCHAR(100), isheld INTEGER NOT NULL DEFAULT 0)",
                "CREATE TABLE tasks (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(255) NOT NULL, calendaruid VARCHAR(255) NOT NULL,"
                " title TEXT NOT NULL, notes TEXT, datestamp DOUBLE NOT NULL DEFAULT 0, duedate DOUBLE DEFAULT 0,"
                " priority INTEGER NOT NULL DEFAULT 0, iscompleted INTEGER NOT NULL DEFAULT 0, completeddate INTEGER DEFAULT 0,"
                " isdeleted INTEGER NOT NULL DEFAULT 0, tags TEXT, actionvalue TEXT, actiontype INTEGER NOT NULL DEFAULT -1,"
                " displayorder INTEGER NOT NULL DEFAULT 0, recurrence INTEGER NOT NULL DEFAULT 0, repeatvalue INTEGER NOT NULL DEFAULT 0,"
                " repeattype INTEGER NOT NULL DEFAULT 0, parent VARCHAR(255), tasktype INTEGER NOT NULL DEFAULT 0,"
                " startdate DOUBLE DEFAULT 0, duetime DOUBLE DEFAULT 999999, archived INTEGER DEFAULT 0,"
                " starred INTEGER NOT NULL DEFAULT 0, recurrenceendtype INTEGER NOT NULL DEFAULT 0,"
                " recurrenceendrepeats INTEGER NOT NULL DEFAULT 0, recurrenceenddate DOUBLE NOT NULL DEFAULT 0,"
                " creationstamp DOUBLE NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0, duration INTEGER NOT NULL DEFAULT 0)",
                "CREATE TABLE alarms (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(255) NOT NULL, taskuid VARCHAR(255) NOT NULL,"
                " action TEXT NOT NULL, relativetrigger DOUBLE DEFAULT 0, absolutetrigger DOUBLE DEFAULT 0,"
                " isdeleted INTEGER NOT NULL DEFAULT 0)",
                "INSERT INTO calgroups(uid,groupname,displayorder) VALUES('g1','Household',1)",
                "INSERT INTO calendars(title,uid,colorid,displayorder,isinboxcal) VALUES('Inbox','c0',0,0,1)",
                "INSERT INTO calendars(title,uid,colorid,displayorder,parentuid) VALUES('Chores','c1',14,1,'g1')",
                "INSERT INTO taggroups(uid,groupname) VALUES('tg1','Rooms')",
                "INSERT INTO tags(tag,uid,parentuid) VALUES('kitchen','t1','tg1')",
                // 1799971200 = 2027-01-15 00:00 UTC; duetime 43200 s = 12:00.
                "INSERT INTO tasks(uid,calendaruid,title,notes,duedate,duetime,priority,starred,tags,tasktype,duration,"
                "  recurrence,repeatvalue,repeattype,displayorder)"
                " VALUES('k1','c1','Descale the kettle','Use vinegar',1799971200,43200,3,1,'kitchen, deep',0,1200,2,3,0,1)",
                "INSERT INTO tasks(uid,calendaruid,title,parent,iscompleted,completeddate,displayorder)"
                " VALUES('k2','c1','Buy vinegar','k1',1,1800100000,2)",
                "INSERT INTO alarms(uid,taskuid,action,relativetrigger) VALUES('a1','k1','n',-900)",
            };
            for (const QString &q : sql) {
                QSqlQuery query(conn);
                QVERIFY2(query.exec(q), qPrintable(query.lastError().text()));
            }
            conn.close();
        }
        QSqlDatabase::removeDatabase("e2etaskdb");

        App app(nullptr);
        QVERIFY(app.ok);
        importFixture(app, "importTaskDb", "import-taskdb", db);
        QTRY_VERIFY_WITH_TIMEOUT(named(app, "Descale the kettle").id != 0, 5000);
        const Task kettle = named(app, "Descale the kettle");
        QCOMPARE(listName(app, kettle), QString("Chores"));
        QCOMPARE(app.store().groups().value(app.store().list(kettle.listId).groupId).name, QString("Household"));
        QCOMPARE(kettle.dueDate, QDate(2027, 1, 15));
        QCOMPARE(kettle.dueTime, 12 * 60);
        QCOMPARE(kettle.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(kettle.recurrence.interval, 3);
        QCOMPARE(kettle.notes, QString("Use vinegar"));
        QCOMPARE(kettle.priority, int(Priority::High));
        QVERIFY(kettle.starred);
        QCOMPARE(kettle.duration, 20);
        QCOMPARE(tk::tagNames(app, kettle), (QStringList{"deep", "kitchen"}));
        QCOMPARE(app.store().alarmsFor(kettle.id).size(), 1);
        QCOMPARE(named(app, "Buy vinegar").parentId, kettle.id);
        QVERIFY(named(app, "Buy vinegar").completed);
        // The group and list are in the sidebar, the task in its list.
        QVERIFY(app.openSource(lk(kettle.listId)));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(kettle.id) != nullptr, 5000);
        QVERIFY(!backupFiles(app).isEmpty());
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(named(app, "Descale the kettle").id == 0, 5000);
    }

    // A file that is not what the button says is reported, and nothing changes.
    void importingTheWrongKindOfFileFails() {
        QTemporaryDir dir;
        const QString junk = dir.filePath("junk.csv");
        {
            QFile f(junk);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("this is not a backup\n");
        }
        App app;
        QVERIFY(app.ok);
        const QStringList before = liveTitles(app);
        const QList<QPair<QString, QString>> sources{{"importTickTick", "import-ticktick"}, {"importThings", "import-things"},
                                                     {"importOmnifocus", "import-omnifocus"}, {"importTaskDb", "import-taskdb"}};
        for (const auto &src : sources) {
            importFixture(app, src.first, src.second, junk);
            QTRY_VERIFY2_WITH_TIMEOUT(app.backend().statusMessage().startsWith("Import failed"), qPrintable(src.first), 5000);
            QCOMPARE(liveTitles(app), before);
        }
        const QString missing = dir.filePath("missing.ics");
        importFixture(app, "importIcs", "import-ics", missing);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().statusMessage().startsWith("Import failed"), 5000);
        QCOMPARE(liveTitles(app), before);
    }

    // Ctrl+P: "Print / Save as PDF", a PDF of the list as shown, then handed to the viewer.
    void printWritesAPdfOfTheList() {
        QTemporaryDir dir;
        App app(seedRich);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) != nullptr, 5000);
        app.window->requestActivate();
        counters().urls.clear();
        const Asked a = asked(app, [&] { app.press(Qt::Key_P, Qt::ControlModifier); });
        QVERIFY(a.ok);
        QCOMPARE(a.purpose, QString("print-pdf"));
        QCOMPARE(a.name, QString("Home.pdf"));
        const QString file = dir.filePath("Home.pdf");
        answer(app, file, a.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(file).size() > 500, 5000);
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.read(5).startsWith("%PDF-"));
        QTRY_VERIFY_WITH_TIMEOUT(counters().urls == QStringList{QUrl::fromLocalFile(file).toString()}, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().statusMessage() == "Saved Home.pdf", 5000);
    }

    // The task menu: Export Selection… (csv or .ics by the name chosen), Save as
    // Event (.ics)… and Add to Calendar.
    void taskMenuExportsAndEvents() {
        QTemporaryDir dir;
        App app(seedRich);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        Asked a = asked(app, [&] { QVERIFY(pickLowEntry("Export Selection…")); });
        QVERIFY(a.ok);
        QCOMPARE(a.purpose, QString("export-selection"));
        const QString ics = dir.filePath("one.ics"), csv = dir.filePath("one.csv");
        answer(app, ics, a.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(ics).size() > 0, 5000);
        QVERIFY(readFile(ics).contains("SUMMARY:Buy milk") && !readFile(ics).contains("SUMMARY:Water the plants"));
        answer(app, csv, a.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(csv).size() > 0, 5000);
        QCOMPARE(column(parseCsv(readFile(csv)), 0), QStringList{"Buy milk"});

        // Save as Event: a VEVENT for the dated task.
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        a = asked(app, [&] { QVERIFY(pickLowEntry("Save as Event (.ics)…")); });
        QVERIFY(a.ok);
        QCOMPARE(a.purpose, QString("event:%1").arg(app.ids.buyMilk));
        const QString event = dir.filePath("event.ics");
        answer(app, event, a.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(event).size() > 0, 5000);
        const QString eventText = readFile(event);
        QVERIFY(eventText.contains("BEGIN:VEVENT") && eventText.contains("SUMMARY:Buy milk"));
        QVERIFY(eventText.contains("DTSTART;VALUE=DATE:" + QDate::currentDate().toString("yyyyMMdd")));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().statusMessage().startsWith("Saved the event"), 5000);

        // A task with no date has no event to save.
        QVERIFY(contextMenuOn(app, app.ids.shelf));
        a = asked(app, [&] { QVERIFY(pickLowEntry("Save as Event (.ics)…")); });
        QVERIFY(a.ok);
        const QString nodate = dir.filePath("nodate.ics");
        answer(app, nodate, a.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().statusMessage() == "Give the task a due or start date first", 5000);
        QVERIFY(!QFileInfo(nodate).exists());

        // Add to Calendar hands an .ics to the desktop.
        counters().urls.clear();
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        QVERIFY(pickLowEntry("Add to Calendar"));
        QTRY_VERIFY_WITH_TIMEOUT(counters().urls.size() == 1 && counters().urls.first().endsWith(QString("fairfax-event-%1.ics").arg(app.ids.buyMilk)), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().statusMessage().startsWith("Opened “Buy milk”"), 5000);
    }

    // "Recovering deleted tasks": a task deleted after a backup is back after
    // Restore (the whole database), shown in its list again.
    void restoringABackupBringsADeletedTaskBack() {
        Keep keep([](Store &st) { st.setSetting("backup.intervalHours", 0); return seedWorld(st); });
        QVERIFY(keep.start());
        App &app = *keep;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Backups");
        QQuickItem *now = nullptr;
        control("backupNow", now);
        click(now);
        QTRY_COMPARE_WITH_TIMEOUT(backupFiles(app).size(), 1, 5000);
        const QString name = backupFiles(app).first();
        focusMain(app);

        // Delete it from the list with the Delete key.
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(selectRow(app, app.ids.buyMilk));
        app.press(Qt::Key_Delete);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.buyMilk).deleted && app.row(app.ids.buyMilk) == nullptr, 5000);

        openSettings(app, sw);
        selectPane(sw, "Backups");
        QQuickItem *restore = nullptr;
        control("backupRestore:" + name, restore);
        click(restore);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Restore this backup?") != nullptr, 5000);
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(app.ids.buyMilk).deleted && app.taskTitled("Buy milk").id == app.ids.buyMilk, 5000);
        focusMain(app);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) != nullptr, 5000);
        // The restore took a safety backup of what it replaced.
        QVERIFY(backupFiles(app).size() >= 2);
    }

    // Lists… on a backup: pick lists and they come back as new lists in a
    // "Restored" group, with their tasks; nothing now is replaced; Ctrl+Z undoes it.
    void restoreSelectedListsFromABackup() {
        Session s;
        s.seed = [](Store &st) { st.setSetting("backup.intervalHours", 0); return seedWorld(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Backups");
        QQuickItem *now = nullptr;
        control("backupNow", now);
        click(now);
        QTRY_COMPARE_WITH_TIMEOUT(backupFiles(app).size(), 1, 5000);
        const QString name = backupFiles(app).first();
        // Changed since: a task deleted, one added.
        QQuickItem *lists = nullptr;
        control("backupLists:" + name, lists);
        click(lists);
        QQuickItem *work = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((work = findItem("restoreList:Work")) != nullptr, 5000);
        QVERIFY(findItem("restoreList:Home") != nullptr);
        QQuickItem *go = findItem("restoreSelected");
        QVERIFY(go && !go->isEnabled());   // Nothing picked yet.
        click(work);
        QTRY_VERIFY_WITH_TIMEOUT(go->isEnabled(), 5000);
        click(go);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("restoreSelected"), 5000);
        int groups = 0;
        qint64 restoredGroup = 0;
        for (const ListGroup &g : app.store().groups())
            if (!g.deleted && g.name == "Restored") { ++groups; restoredGroup = g.id; }
        QCOMPARE(groups, 1);
        qint64 copy = 0;
        for (const TaskList &l : app.store().lists())
            if (!l.deleted && l.groupId == restoredGroup && l.name == "Work") copy = l.id;
        QVERIFY(copy != 0 && copy != app.ids.work);
        int tasks = 0;
        for (const Task &t : app.store().tasks())
            tasks += !t.deleted && t.listId == copy;
        QCOMPARE(tasks, 4);   // Report, studio, Call Alex and the finished one.
        // What was there is as it was.
        QVERIFY(!app.task(app.ids.report).deleted && app.task(app.ids.report).listId == app.ids.work);
        focusMain(app);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(copy)) != nullptr, 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().lists().contains(copy) || app.store().list(copy).deleted, 5000);
    }

    // Save a Copy… hands the backup to the chooser's destination; Restore from
    // File… takes a path back from it.
    void saveACopyAndRestoreFromFile() {
        QTemporaryDir dir;
        Session s;
        s.seed = [](Store &st) { st.setSetting("backup.intervalHours", 0); return seedWorld(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Backups");
        QQuickItem *now = nullptr;
        control("backupNow", now);
        click(now);
        QTRY_COMPARE_WITH_TIMEOUT(backupFiles(app).size(), 1, 5000);
        const QString name = backupFiles(app).first();
        QQuickItem *copyButton = nullptr;
        control("backupCopy:" + name, copyButton);
        const Asked a = asked(app, [&] { click(copyButton); });
        QVERIFY(a.ok);
        QCOMPARE(a.purpose, QString("backup-copy"));
        QCOMPARE(a.name, name);
        const QString copy = dir.filePath("kept.db");
        answer(app, copy, a.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(copy).size() > 0, 5000);
        QCOMPARE(QFileInfo(copy).size(), QFileInfo(app.dir() + "/backups/" + name).size());

        // A change after the copy; Restore from File… with the copy brings the old state back.
        focusMain(app);
        Task later;
        newTaskByKeyboard(app, "Made after the copy", later);
        QVERIFY(later.id != 0);
        openSettings(app, sw);
        selectPane(sw, "Backups");
        QQuickItem *fromFile = findText("Restore from File…", "QQuickAbstractButton");
        QVERIFY(fromFile);
        const Asked b = asked(app, [&] { click(fromFile); });
        QVERIFY(b.ok);
        QCOMPARE(b.purpose, QString("restore"));
        answer(app, copy, b.purpose);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Made after the copy").id == 0, 5000);
        QVERIFY(app.taskTitled("Buy milk").id != 0);
    }
};

REGISTER_TEST(E2EMiscIO)

#include "test_misc_io.moc"
