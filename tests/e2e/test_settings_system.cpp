// Settings whose effect is outside the task list: backups, automatic
// maintenance, the sync providers, calendar sources, import and export,
// Automation and Diagnostics. Nothing here touches the user's own data:
// every folder is a temporary one, and the system file chooser (a portal
// dialog on the desktop) is never opened; its answer is sent instead.

#include <QDirIterator>
#include <QProcess>

#include "settingskit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

// Clicks Cancel in whichever dialog is open (Escape would also close Settings).
void clickCancel() {
    QQuickItem *cancel = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((cancel = findText("Cancel")) != nullptr, 3000);
    click(cancel);
}

QStringList backupFiles(App &app) {
    return QDir(app.dir() + "/backups").entryList({"Precrime_*.db"}, QDir::Files, QDir::Time);
}

void clickOk() {
    QQuickItem *ok = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((ok = findText("OK")) != nullptr, 3000);
    click(ok);
}

// Waits until the clock has moved to a new second, so two backups made one
// after the other get different names.
void nextSecond() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QTRY_VERIFY_WITH_TIMEOUT(QDateTime::currentSecsSinceEpoch() > now, 3000);
}

QString readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString runGit(const QStringList &args) {
    QProcess p;
    p.start("git", args);
    p.waitForFinished(15000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// Every file under a folder, relative to it.
QStringList filesUnder(const QString &root) {
    QStringList out;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(root).relativeFilePath(it.next());
    return out;
}

// An iCalendar file with one event today, so the day bar has something to show.
QString writeCalendar(const QString &dir, const QString &name, const QString &event) {
    const QString path = dir + "/" + name + ".ics";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    const QString day = QDate::currentDate().toString("yyyyMMdd");
    f.write(QString("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//e2e//EN\r\nBEGIN:VEVENT\r\nUID:%1@e2e\r\n"
                    "DTSTAMP:%2T080000Z\r\nDTSTART:%2T100000\r\nDTEND:%2T110000\r\nSUMMARY:%1\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
                .arg(event, day).toUtf8());
    return path;
}

}  // namespace

class E2ESettingsSystem : public QObject {
    Q_OBJECT

private slots:
    // "Back Up Now" writes a file into the backups folder and lists it;
    // Delete (after a question) removes it again.
    void backUpNowWritesAndDeletesAFile() {
        Session s;
        s.seed = [](Store &st) { st.setSetting("backup.intervalHours", 0); return seedWorld(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Backups");
        QVERIFY(backupFiles(app).isEmpty());
        QQuickItem *button = nullptr;
        control("backupNow", button);
        click(button);
        QTRY_COMPARE_WITH_TIMEOUT(backupFiles(app).size(), 1, 3000);
        const QString name = backupFiles(app).first();
        QVERIFY(QFileInfo(app.dir() + "/backups/" + name).size() > 0);
        // The Backups hint names the folder, and the list shows the file.
        QVERIFY(textMatching(sw->contentItem(), QRegularExpression("Backups are kept in .*/backups")) != nullptr);
        QQuickItem *del = nullptr;
        control("backupDelete:" + name, del);
        click(del);
        QObject *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = findObject("confirmDelete")) != nullptr && dialog->property("visible").toBool(), 3000);
        clickCancel();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 3000);
        QCOMPARE(backupFiles(app).size(), 1);
        click(del);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 3000);
        clickOk();
        QTRY_VERIFY_WITH_TIMEOUT(backupFiles(app).isEmpty(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("backupDelete:" + name), 3000);
    }

    // "Keep at most": the oldest backups go once there are more than that, and
    // the number comes back after a restart.
    void keepAtMostPrunesOldBackups() {
        Session s;
        s.seed = [](Store &st) { st.setSetting("backup.intervalHours", 0); return seedWorld(st); };
        QVERIFY(s.start());
        App *app = s.app.get();
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Backups");
        setSpin(sw, "setting:backup.keep", 2);
        QCOMPARE(app->store().setting("backup.keep").toString(), QString("2"));
        QQuickItem *button = nullptr;
        control("backupNow", button);
        QStringList first;
        for (int i = 0; i < 3; ++i) {
            nextSecond();
            click(button);
            QTRY_VERIFY_WITH_TIMEOUT(!backupFiles(*app).isEmpty() && QFileInfo(app->dir() + "/backups/" + backupFiles(*app).first()).size() > 0, 3000);
            if (i == 0)
                first = backupFiles(*app);
            QTest::qWait(50);
        }
        QTRY_COMPARE_WITH_TIMEOUT(backupFiles(*app).size(), 2, 3000);
        QVERIFY2(!backupFiles(*app).contains(first.first()), "the oldest backup should have been removed");

        QVERIFY(s.restart());
        app = s.app.get();
        openSettings(*app, sw);
        selectPane(sw, "Backups");
        QQuickItem *spin = nullptr;
        control("setting:backup.keep", spin);
        QCOMPARE(spin->property("value").toInt(), 2);
    }

    // "Restore": the database goes back to the backup, after a question.
    void restoreReturnsToTheBackup() {
        Session s;
        s.seed = [](Store &st) { st.setSetting("backup.intervalHours", 0); return seedWorld(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Backups");
        QQuickItem *button = nullptr;
        control("backupNow", button);
        click(button);
        QTRY_COMPARE_WITH_TIMEOUT(backupFiles(app).size(), 1, 3000);
        const QString name = backupFiles(app).first();
        Task later;
        showSource(app, listKey(s.ids.home));
        newTaskByKeyboard(app, "Made after the backup", later);
        openSettings(app, sw);
        QQuickItem *restore = nullptr;
        control("backupRestore:" + name, restore);
        click(restore);
        clickOk();
        QTRY_VERIFY_WITH_TIMEOUT(newestTitled(app, "Made after the backup").id == 0, 5000);
        QVERIFY(app.taskTitled("Buy milk").id != 0);
    }

    // "Back up automatically" and "Auto-delete completed tasks" run a few
    // seconds after launch. Phase one: backups never, completed tasks go after
    // a week with sub-tasks delayed three weeks more. Phase two, the same
    // database relaunched with both turned round: a backup is made and an old
    // finished task stays.
    void backupFrequencyAndAutoDelete() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedWorld(st);
            const QDateTime tenDays = QDateTime::currentDateTime().addDays(-10);
            addTask(st, ids.home, "Old top level", [&](Task &t) { t.completed = true; t.completedAt = tenDays; });
            const qint64 project = addTask(st, ids.home, "Old project", [](Task &t) { t.type = TaskType::Project; });
            addTask(st, ids.home, "Old child", [&](Task &t) { t.parentId = project; t.completed = true; t.completedAt = tenDays; });
            addTask(st, ids.home, "Ancient child", [&](Task &t) {
                t.parentId = project;
                t.completed = true;
                t.completedAt = QDateTime::currentDateTime().addDays(-40);
            });
            return ids;
        };
        QVERIFY(s.start());
        App *app = s.app.get();
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Backups");
        chooseCombo("setting:backup.intervalHours", 0);  // Never
        QCOMPARE(app->store().setting("backup.intervalHours").toString(), QString("0"));
        selectPane(sw, "Tasks");
        chooseCombo("setting:tasks.autoDeleteDays", 2);  // After 1 week
        setCheck("setting:tasks.autoDeleteDelaySubtasks", true);
        QCOMPARE(app->store().setting("tasks.autoDeleteDays").toString(), QString("7"));

        // The maintenance pass runs five seconds after launch.
        QTRY_VERIFY_WITH_TIMEOUT(app->taskTitled("Old top level").id == 0, 9000);
        QVERIFY2(app->taskTitled("Old child").id != 0, "a sub-task is kept three weeks longer");
        QVERIFY(app->taskTitled("Ancient child").id == 0);
        QVERIFY(app->taskTitled("Buy milk").id != 0);
        QVERIFY2(backupFiles(*app).isEmpty(), "automatic backups are off");

        // Relaunch with auto-delete off and a daily backup.
        editTask(app->store(), app->taskTitled("Old child").id, [](Task &t) { t.completedAt = QDateTime::currentDateTime().addDays(-100); });
        QVERIFY(s.restart());
        app = s.app.get();
        openSettings(*app, sw);
        selectPane(sw, "Tasks");
        chooseCombo("setting:tasks.autoDeleteDays", 0);
        selectPane(sw, "Backups");
        chooseCombo("setting:backup.intervalHours", 1);  // Every 24 hours
        QCOMPARE(app->store().setting("backup.intervalHours").toString(), QString("24"));
        QTRY_VERIFY_WITH_TIMEOUT(!backupFiles(*app).isEmpty(), 9000);
        QVERIFY2(app->taskTitled("Old child").id != 0, "auto-delete is off, so a finished task stays");
    }

    // Sync > Folder: choosing a folder starts a sync that writes the tasks
    // into it as files; Sync Now writes new ones; Disconnect forgets it.
    // "Sync automatically" is remembered as text and read back as off.
    void folderSyncWritesFilesAndDisconnects() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Session s;
        s.withSync = true;
        QVERIFY(s.start());
        App *app = s.app.get();
        QVERIFY(app->services().folderSync);
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        QQuickItem *path = nullptr;
        control("folderPath", path);
        QCOMPARE(path->property("text").toString(), QString("No folder chosen"));
        // The portal's file chooser is not opened; its answer is.
        QQuickItem *choose = nullptr;
        control("folderChoose", choose);
        QCOMPARE(choose->property("text").toString(), QString("Choose Folder…"));
        emit app->services().filePicker.picked(folder.path(), "sync-folder");
        QTRY_VERIFY_WITH_TIMEOUT(app->services().folderSync->configured(), 3000);
        QCOMPARE(app->store().setting("folder.path").toString(), folder.path());
        QTRY_COMPARE_WITH_TIMEOUT(path->property("text").toString(), folder.path(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().folderSync->busy() && !filesUnder(folder.path()).isEmpty(), 15000);
        bool found = false;
        for (const QString &f : filesUnder(folder.path()))
            found = found || readFile(folder.filePath(f)).contains("Buy milk");
        QVERIFY2(found, "the tasks should be written into the folder as files");

        // Sync Now carries a new task across.
        showSource(*app, listKey(s.ids.home));
        Task t;
        newTaskByKeyboard(*app, "Folder synced task", t);
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        QQuickItem *now = nullptr;
        control("folderSyncNow", now);
        click(now);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const QString &f : filesUnder(folder.path()))
                if (readFile(folder.filePath(f)).contains("Folder synced task"))
                    return true;
            return false;
        }(), 15000);

        // "Sync automatically" off: read back as off after a restart.
        setCheck("setting:sync.auto", false);
        QVERIFY(!SyncTree::autoSyncEnabled(app->store()));
        QVERIFY(s.restart());
        app = s.app.get();
        QCOMPARE(app->store().setting("sync.auto").toString(), QString("false"));
        QVERIFY(!SyncTree::autoSyncEnabled(app->store()));
        QVERIFY(app->services().folderSync->configured());
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        QQuickItem *autoBox = nullptr;
        control("setting:sync.auto", autoBox);
        QVERIFY(!isChecked(autoBox));
        control("folderPath", path);
        QCOMPARE(path->property("text").toString(), folder.path());
        setCheck("setting:sync.auto", true);
        QVERIFY(SyncTree::autoSyncEnabled(app->store()));

        // Disconnect.
        QQuickItem *off = nullptr;
        control("folderDisconnect", off);
        click(off);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().folderSync->configured(), 3000);
        QCOMPARE(app->store().setting("folder.path").toString(), QString());
        control("folderPath", path);
        QTRY_COMPARE_WITH_TIMEOUT(path->property("text").toString(), QString("No folder chosen"), 3000);
        QVERIFY(s.restart());
        QVERIFY(!s.app->services().folderSync->configured());
    }

    // Sync > Git: a repository address typed in and connected makes a commit
    // in that repository holding the tasks; Disconnect forgets the address.
    void gitSyncPushesToTheRemote() {
        QTemporaryDir remote;
        QVERIFY(remote.isValid());
        runGit({"init", "--bare", "-q", "-b", "main", remote.path()});
        if (!QFileInfo::exists(remote.path() + "/HEAD"))
            QSKIP("git is not available");
        Session s;
        s.withSync = true;
        QVERIFY(s.start());
        App *app = s.app.get();
        QVERIFY(app->services().gitSync);
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        QQuickItem *connect = nullptr;
        control("gitConnect", connect);
        QVERIFY(!connect->isEnabled());   // Nothing typed yet.
        setText(sw, "gitRemote", remote.path());
        QTRY_VERIFY_WITH_TIMEOUT(connect->isEnabled(), 3000);
        click(connect);
        QTRY_VERIFY_WITH_TIMEOUT(app->services().gitSync->configured(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().gitSync->busy() && !runGit({"--git-dir", remote.path(), "log", "--oneline", "-1"}).isEmpty(), 30000);
        QVERIFY(runGit({"--git-dir", remote.path(), "ls-tree", "-r", "--name-only", "HEAD"}).contains("tasks/"));
        QCOMPARE(app->store().setting("git.remote").toString(), remote.path());
        // The pane says where the working copy is kept.
        QQuickItem *copy = nullptr;
        control("gitWorkingCopy", copy);
        QCOMPARE(copy->property("text").toString(), "Working copy: " + app->services().gitSync->directory());

        QVERIFY(s.restart());
        app = s.app.get();
        QVERIFY(app->services().gitSync->configured());
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        QQuickItem *field = nullptr;
        control("gitRemote", field);
        QCOMPARE(field->property("text").toString(), remote.path());
        QQuickItem *off = nullptr;
        control("gitDisconnect", off);
        click(off);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().gitSync->configured(), 5000);
        QCOMPARE(app->store().setting("git.remote").toString(), QString());
    }

    // Sync > WebDAV and Todoist need a server, so what can be checked is the
    // configuration: the saved address and user come back into the fields,
    // Connect needs a password (or token), an address that is not http(s) is
    // refused with a message, and Disconnect forgets the server.
    void webDavAndTodoistConfiguration() {
        Session s;
        s.withSync = true;
        s.seed = [](Store &st) {
            st.setSetting("webdav.url", "ftp://example.invalid/dav/");
            st.setSetting("webdav.user", "alice");
            return seedWorld(st);
        };
        QVERIFY(s.start());
        App *app = s.app.get();
        QVERIFY(app->services().webDavSync);
        QVERIFY(app->services().webDavSync->configured());
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        QQuickItem *url = nullptr, *user = nullptr, *password = nullptr, *connect = nullptr;
        control("davUrl", url);
        control("davUser", user);
        control("davPassword", password);
        control("davConnect", connect);
        QCOMPARE(url->property("text").toString(), QString("ftp://example.invalid/dav/"));
        QCOMPARE(user->property("text").toString(), QString("alice"));
        QCOMPARE(connect->property("text").toString(), QString("Change"));
        QVERIFY(!connect->isEnabled());   // Needs the password too.
        click(password);
        typeInto(sw, "secret");
        QTRY_VERIFY_WITH_TIMEOUT(connect->isEnabled(), 3000);
        QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(sw, Qt::Key_Delete);
        QTRY_VERIFY_WITH_TIMEOUT(!connect->isEnabled(), 3000);

        QQuickItem *now = nullptr;
        control("davSyncNow", now);
        click(now);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("davStatus") && findItem("davStatus")->property("text").toString().contains("must start with http"), 5000);

        // Todoist: the token box is there and Connect waits for a token.
        QQuickItem *token = nullptr, *tconnect = nullptr;
        control("todoistToken", token);
        control("todoistConnect", tconnect);
        QVERIFY(!tconnect->isEnabled());
        click(token);
        typeInto(sw, "abc");
        QTRY_VERIFY_WITH_TIMEOUT(tconnect->isEnabled(), 3000);
        QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(sw, Qt::Key_Delete);
        QTRY_VERIFY_WITH_TIMEOUT(!tconnect->isEnabled(), 3000);
        QVERIFY(!app->services().todoist->connected());

        QVERIFY(s.restart());
        app = s.app.get();
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        control("davUrl", url);
        QCOMPARE(url->property("text").toString(), QString("ftp://example.invalid/dav/"));

        QQuickItem *off = nullptr;
        control("davDisconnect", off);
        click(off);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().webDavSync->configured(), 3000);
        QCOMPARE(app->store().setting("webdav.url").toString(), QString());
        QCOMPARE(app->store().setting("webdav.user").toString(), QString());
    }

    // Calendar: a calendar file added in Settings puts its events in a day bar
    // above Today; the per-calendar switch hides them; "above every list"
    // moves the bar; everything is back after a restart and clears again.
    void calendarSourcesFeedTheDayBar() {
        QTemporaryDir cal;
        QVERIFY(cal.isValid());
        const QString path = writeCalendar(cal.path(), "work", "Dentist");
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        showSource(*app, "today");
        QTRY_VERIFY_WITH_TIMEOUT(app->row(s.ids.buyMilk) != nullptr, 3000);
        QVERIFY(!findItem("dayBar"));

        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Calendar");
        QQuickItem *sources = nullptr;
        control("setting:calendar.sources", sources);
        click(sources);
        typeInto(sw, path);
        QQuickItem *apply = nullptr;
        control("calendarApply", apply);
        click(apply);
        QCOMPARE(app->store().setting("calendar.sources").toString(), path);
        QTRY_COMPARE_WITH_TIMEOUT(app->services().calendar.sources(), QStringList{path}, 3000);
        focusMain(*app);
        QQuickItem *bar = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((bar = findItem("dayBar")) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().calendar.today().isEmpty()
                                     && app->services().calendar.today().first().toMap().value("title").toString() == "Dentist", 5000);
        QVERIFY(bar->property("events").toList().size() == 1);

        // The calendar's own switch: events hidden, then back.
        openSettings(*app, sw);
        selectPane(sw, "Calendar");
        // The calendar rows are rebuilt when one is switched, so look it up each time.
        auto enabledNow = [] {
            QQuickItem *box = findItem("calendarEnabled:work");
            return box && isChecked(box);
        };
        QQuickItem *enabled = nullptr;
        control("calendarEnabled:work", enabled);
        QVERIFY(enabledNow());
        click(enabled);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("calendarEnabled:work") && !enabledNow(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(app->services().calendar.today().isEmpty(), 3000);

        // Still switched off after a restart, then switched on again.
        QVERIFY(s.restart());
        app = s.app.get();
        QCOMPARE(app->services().calendar.disabledSources(), QStringList{path});
        QVERIFY(app->services().calendar.today().isEmpty());
        openSettings(*app, sw);
        selectPane(sw, "Calendar");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("calendarEnabled:work") && !enabledNow(), 3000);
        control("calendarEnabled:work", enabled);
        click(enabled);
        QTRY_VERIFY_WITH_TIMEOUT(enabledNow(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().calendar.today().isEmpty(), 3000);

        // A colour for the calendar.
        QStringList dotNames;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            dotNames.clear();
            findIn(sw->contentItem(), [&](QQuickItem *i) { if (i->objectName().startsWith("calendarColor:")) dotNames << i->objectName(); return false; });
            return dotNames.size() > 2;
        }(), 3000);
        const QString wanted = dotNames.at(2).mid(QString("calendarColor:").size());
        QQuickItem *dot = nullptr;
        control(dotNames.at(2), dot);
        click(dot);
        QTRY_VERIFY_WITH_TIMEOUT(app->store().setting("calendar.colors").toString().contains(wanted, Qt::CaseInsensitive), 3000);
        QCOMPARE(app->services().calendar.sourceInfo().first().toMap().value("color").value<QColor>(), QColor(wanted));

        // "My email addresses" are kept.
        setText(sw, "setting:calendar.emails", "me@example.com");
        QCOMPARE(app->services().calendar.myEmails(), QString("me@example.com"));

        // "Show events above every list, not just Today."
        focusMain(*app);
        showSource(*app, listKey(s.ids.home));
        QTRY_VERIFY_WITH_TIMEOUT(app->row(s.ids.buyMilk) != nullptr, 3000);
        QVERIFY(!findItem("dayBar"));
        openSettings(*app, sw);
        selectPane(sw, "Calendar");
        setCheck("setting:calendar.inLists", true);
        focusMain(*app);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("dayBar") != nullptr, 3000);

        // The bar opens out and is found open after a restart.
        // Offscreen, the first click after leaving the Settings window is
        // sometimes not delivered (both windows report active), so retry it.
        for (int attempt = 0; attempt < 3 && !findItem("dayBar")->property("expanded").toBool(); ++attempt) {
            click(findItem("dayBarToggle"));
            QTest::qWaitFor([] { return findItem("dayBar")->property("expanded").toBool(); }, 1000);
        }
        QCOMPARE(findItem("dayBar")->property("expanded").toBool(), true);
        QCOMPARE(app->store().setting("calendar.expanded").toString(), QString("true"));

        QVERIFY(s.restart());
        app = s.app.get();
        showSource(*app, listKey(s.ids.home));
        QTRY_VERIFY_WITH_TIMEOUT((bar = findItem("dayBar")) != nullptr, 3000);
        QVERIFY(bar->property("expanded").toBool());
        QCOMPARE(app->services().calendar.sources(), QStringList{path});
        QCOMPARE(app->services().calendar.myEmails(), QString("me@example.com"));
        QCOMPARE(app->services().calendar.sourceInfo().first().toMap().value("color").value<QColor>(), QColor(wanted));
        click(findItem("dayBarToggle"));
        QTRY_COMPARE_WITH_TIMEOUT(findItem("dayBar")->property("expanded").toBool(), false, 3000);
        QVERIFY(s.restart());
        app = s.app.get();
        showSource(*app, listKey(s.ids.home));
        QTRY_VERIFY_WITH_TIMEOUT((bar = findItem("dayBar")) != nullptr, 3000);
        QVERIFY2(!bar->property("expanded").toBool(), "the stored text \"false\" must read as collapsed");

        // Clear the sources: the bar goes.
        openSettings(*app, sw);
        selectPane(sw, "Calendar");
        control("setting:calendar.sources", sources);
        QCOMPARE(sources->property("text").toString(), path);
        click(sources);
        QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(sw, Qt::Key_Delete);
        control("calendarApply", apply);
        click(apply);
        QTRY_VERIFY_WITH_TIMEOUT(app->services().calendar.sources().isEmpty(), 3000);
        focusMain(*app);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("dayBar"), 3000);
    }

    // Import & Export: the buttons are there, "Choose Lists…" opens the export
    // panel, and the chooser's answers are carried out: CSV and .ics are
    // written to the chosen file, and an imported .ics lands in the default
    // list (Tasks > Default list).
    void importAndExportRoutes() {
        QTemporaryDir out;
        QVERIFY(out.isValid());
        Session s;
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Import & Export");
        for (const char *name : {"exportChoose", "exportCsv", "exportIcs", "importTickTick", "importTaskDb", "importIcs", "importThings", "importOmnifocus"}) {
            QQuickItem *b = nullptr;
            control(name, b);
            QVERIFY2(b->isEnabled(), name);
        }
        QQuickItem *choose = nullptr;
        control("exportChoose", choose);
        click(choose);
        QQuickItem *export_ = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(findText("Export Tasks") != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT((export_ = findText("Export…")) != nullptr, 3000);
        clickCancel();
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Export…"), 3000);

        // Export: the chooser answers with a path.
        const QString csv = out.filePath("all.csv"), ics = out.filePath("all.ics");
        emit app.services().filePicker.picked(csv, "export-csv");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(csv).size() > 0, 3000);
        const QString csvText = readFile(csv);
        QVERIFY(csvText.contains("Buy milk") && csvText.contains("Write status report"));
        emit app.services().filePicker.picked(ics, "export-ics");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(ics).size() > 0, 3000);
        QVERIFY(readFile(ics).contains("BEGIN:VCALENDAR") && readFile(ics).contains("Buy milk"));

        // Import into the default list chosen in Settings.
        selectPane(sw, "Tasks");
        chooseCombo("setting:tasks.defaultList", 3);   // First list, Last used, Home, Work.
        const QString todo = out.filePath("in.ics");
        {
            QFile f(todo);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//e2e//EN\r\nBEGIN:VTODO\r\nUID:imp1@e2e\r\n"
                    "DTSTAMP:20260101T000000Z\r\nSUMMARY:Imported errand\r\nEND:VTODO\r\nEND:VCALENDAR\r\n");
        }
        emit app.services().filePicker.picked(todo, "import-ics");
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Imported errand").id != 0, 3000);
        QCOMPARE(app.taskTitled("Imported errand").listId, s.ids.work);
        focusMain(app);
        showSource(app, listKey(s.ids.work));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.taskTitled("Imported errand").id) != nullptr, 3000);
    }

    // Diagnostics: the report is built when the pane opens and describes this
    // install; Refresh brings it up to date; Copy puts it on the clipboard.
    // Automation has nothing to switch: it shows how to reach Precrime.
    void diagnosticsAndAutomationPanes() {
        Session s;
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Automation");
        QQuickItem *hint = nullptr;
        control("automationHint", hint);
        QVERIFY(hint->property("text").toString().contains("claude mcp add precrime -- precrime mcp"));
        QVERIFY(hint->property("text").toString().contains("precrime --waybar"));
        // No switch, box or list on this pane.
        int controls = 0;
        findIn(sw->contentItem(), [&](QQuickItem *i) { controls += i->objectName().startsWith("setting:"); return false; });
        QCOMPARE(controls, 0);

        selectPane(sw, "Diagnostics");
        QQuickItem *report = nullptr;
        control("diagReport", report);
        QVERIFY(report->property("text").toString().contains("Precrime"));
        QVERIFY(report->property("text").toString().contains("Tasks: 7 open, 1 completed"));
        QVERIFY(report->property("text").toString().contains("Lists: 2, tags: 1"));
        QVERIFY(report->property("text").toString().contains("Git sync: off"));

        focusMain(app);
        showSource(app, listKey(s.ids.home));
        Task t;
        newTaskByKeyboard(app, "One more task", t);
        openSettings(app, sw);
        selectPane(sw, "Diagnostics");
        QQuickItem *refresh = nullptr;
        control("diagRefresh", refresh);
        click(refresh);
        QTRY_VERIFY_WITH_TIMEOUT(report->property("text").toString().contains("Tasks: 8 open, 1 completed"), 3000);
        QGuiApplication::clipboard()->setText("not the report");
        QQuickItem *copy = nullptr;
        control("diagCopy", copy);
        click(copy);
        QTRY_VERIFY_WITH_TIMEOUT(QGuiApplication::clipboard()->text().contains("Tasks: 8 open, 1 completed"), 3000);
        QVERIFY(QGuiApplication::clipboard()->text().startsWith("Precrime "));
    }
};

REGISTER_TEST(E2ESettingsSystem)

#include "test_settings_system.moc"
