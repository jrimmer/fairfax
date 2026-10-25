#include <QtTest>
#include <memory>

#include "caldavsync.h"
#include "diagnostics.h"
#include "fakecaldav.h"
#include "keyring.h"
#include "store.h"
#include "synctree.h"
#include "testregistry.h"
#include "vtodo.h"

// The CalDAV sync engine against the fake server (tests/fakecaldav.h) over
// plain HTTP on 127.0.0.1. The engine's syncBlocking() drives a whole cycle
// while the test's event loop runs, so a test can assert on the store and on
// what the server holds afterwards. Nothing here reaches the internet.

namespace {

const char *TaskTwo =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Fairfax//EN\r\nBEGIN:VTODO\r\nUID:two\r\n"
    "SUMMARY:Second\r\nLAST-MODIFIED:20250101T000000Z\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";

// A VTODO carrying another client's own metadata, to prove it survives an edit here.
const char *VendorTask =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Example//EN\r\nBEGIN:VTODO\r\nUID:vendor\r\n"
    "SUMMARY:From another client\r\nDESCRIPTION:Note text\\n<Example Meta>{\"x\":1}</Example Meta>\r\n"
    "X-EXAMPLE-FOO;X=1:bar\r\nLAST-MODIFIED:20250101T000000Z\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";

}  // namespace

// One account: a store, its sync engine and the shared server.
struct Account {
    std::unique_ptr<Store> store;
    std::unique_ptr<CalDavSync> sync;
};

class CalDavSyncTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<FakeCalDav> m_server;
    int m_n = 0;

    Account makeAccount() {
        Account a;
        a.store = std::make_unique<Store>();
        QString error;
        const QString path = m_dir.filePath(QString("sync%1/fairfax.db").arg(++m_n));
        if (!a.store->open(path, &error))
            qFatal("cannot open test store: %s", qPrintable(error));
        a.sync = std::make_unique<CalDavSync>(a.store.get(), nullptr);
        CalDavClient::Timings t;
        t.requestTimeout = 800;
        t.backoffBase = 10;
        t.backoffMax = 50;
        t.maxAttempts = 3;
        a.sync->setTimings(t);
        return a;
    }

    static void connect(Account &a, FakeCalDav &server) {
        a.sync->setQuietConnect(true);
        a.sync->connectAccount(server.baseUrl().toString(), "jane", "app-password");
        // No sync has run: configChanged was emitted, nothing else.
        QVERIFY(a.sync->configured());
    }

    static qint64 addList(Store &s, const QString &name, int color = 0) {
        TaskList l;
        l.id = s.nextId("lists");
        l.name = name;
        l.color = color;
        s.begin("add list");
        s.putList(l);
        s.commit();
        return l.id;
    }

    static qint64 addTask(Store &s, const QString &title, qint64 listId, const QString &uid = {}) {
        Task t;
        t.id = s.nextId("tasks");
        t.listId = listId;
        t.title = title;
        t.uid = uid;
        s.begin("add task");
        s.putTask(t);
        s.commit();
        return t.id;
    }

    static void edit(Store &s, const Task &t, const QString &label = "edit") {
        s.begin(label);
        s.putTask(t);
        s.commit();
    }

    static qint64 find(const Store &s, const QString &title) {
        for (const auto &t : s.tasks())
            if (!t.deleted && t.title == title)
                return t.id;
        return 0;
    }

    static int liveTasks(const Store &s) {
        int n = 0;
        for (const auto &t : s.tasks())
            if (!t.deleted)
                ++n;
        return n;
    }

    static qint64 linkLocal(const Store &s, const QString &entity, const QString &remote) {
        for (const auto &link : s.syncLinks("caldav", entity))
            if (link.remoteId == remote)
                return link.localId;
        return 0;
    }

    // Repoints a task's link at another resource, to stage a specific
    // server-side state in a test.
    static void repoint(Store &s, qint64 localId, const QString &href) {
        const auto links = s.syncLinks("caldav", "tasks");
        Store::SyncLink link;
        link.localId = localId;
        link.remoteId = href;
        link.snapshot = links.value(localId).snapshot;
        s.setSyncLink("caldav", "tasks", link);
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void init() { m_server = std::make_unique<FakeCalDav>(); }
    void cleanup() { m_server.reset(); }

    // --------------------------------------------------------------- mapping

    void firstSyncPairsByNameAndCreatesCalendars() {
        Account a = makeAccount();
        const qint64 tasks = addList(*a.store, "Tasks");
        addTask(*a.store, "Mine", tasks);
        a.store->setSetting("caldav.createCalendars", true);
        connect(a, *m_server);
        const auto r = a.sync->syncBlocking();
        QVERIFY2(r.ok, qPrintable(r.error));
        // The existing "Tasks" calendar was paired, not duplicated.
        QCOMPARE(linkLocal(*a.store, "lists", QStringLiteral("/calendars/jane/tasks/")), tasks);
        int tasksCalendars = 0;
        for (const FakeCalDav::Collection &c : m_server->collections())
            if (c.displayName == "Tasks")
                ++tasksCalendars;
        QCOMPARE(tasksCalendars, 1);
        const auto links = a.store->syncLinks("caldav", "tasks");
        QCOMPARE(links.size(), 1);
        const QString href = links.constBegin()->remoteId;
        QVERIFY(m_server->tasks()->resources.contains(href));
    }

    void createCalendarsOffLeavesTheListLocal() {
        Account a = makeAccount();
        const qint64 tasks = addList(*a.store, "Tasks");
        const qint64 privateList = addList(*a.store, "Mine");
        addTask(*a.store, "Kept local", privateList);
        a.store->setSetting("caldav.createCalendars", false);
        connect(a, *m_server);
        const auto r = a.sync->syncBlocking();
        QVERIFY2(r.ok, qPrintable(r.error));
        // The existing calendar paired; no new one was created.
        QCOMPARE(linkLocal(*a.store, "lists", QStringLiteral("/calendars/jane/tasks/")), tasks);
        for (const FakeCalDav::Collection &c : m_server->collections())
            QVERIFY(c.displayName != "Mine");
        QCOMPARE(a.store->syncLinks("caldav", "tasks").size(), 0);
        QVERIFY(find(*a.store, "Kept local"));
    }

    void calendarWithoutAListCreatesAListInTheAccountGroup() {
        Account a = makeAccount();
        connect(a, *m_server);
        const auto r = a.sync->syncBlocking();
        QVERIFY2(r.ok, qPrintable(r.error));
        qint64 created = 0;
        for (const auto &l : a.store->lists())
            if (!l.deleted && l.name == "Tasks")
                created = l.id;
        QVERIFY(created);
        const TaskList list = a.store->list(created);
        QVERIFY(list.groupId != 0);
        QCOMPARE(a.store->groups().value(list.groupId).name, a.sync->accountName());
    }

    void excludedListIsSkipped() {
        Account a = makeAccount();
        const qint64 shared = addList(*a.store, "Tasks");
        const qint64 secret = addList(*a.store, "Private");
        addTask(*a.store, "Visible", shared);
        addTask(*a.store, "Hidden", secret);
        SyncTree::setListExcluded(*a.store, secret, true);
        FakeCalDav::Collection c;
        c.href = QStringLiteral("/calendars/jane/private/");
        c.displayName = QStringLiteral("Private");
        c.components = {QStringLiteral("VTODO")};
        m_server->collections().append(c);
        connect(a, *m_server);
        const auto r = a.sync->syncBlocking();
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(find(*a.store, "Visible"));
        QVERIFY(find(*a.store, "Hidden"));
        for (const auto &link : a.store->syncLinks("caldav", "lists"))
            QVERIFY(link.localId != secret);
        // The hidden task never reached the server.
        for (const FakeCalDav::Collection &col : m_server->collections())
            for (const QByteArray &body : col.resources)
                QVERIFY(!QString::fromUtf8(body).contains(QStringLiteral("Hidden")));
    }

    // "Don't sync" in the CalDAV mapping is CalDAV's own: it never touches the
    // exclusion every provider honours, and mapping a list that is excluded
    // everywhere does not lift that exclusion.
    void calDavDontSyncLeavesOtherProvidersAlone() {
        Account a = makeAccount();
        const qint64 work = addList(*a.store, "Work");
        const qint64 secret = addList(*a.store, "Private");
        addTask(*a.store, "Report", work);
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);

        a.sync->setCalendarForList(work, QString());
        QVERIFY(a.sync->isListSkipped(work));
        QVERIFY(!SyncTree::isListExcluded(*a.store, work));
        addTask(*a.store, "Stays local", work);
        QVERIFY(a.sync->syncBlocking().ok);
        for (const FakeCalDav::Collection &col : m_server->collections())
            for (const QByteArray &body : col.resources)
                QVERIFY(!QString::fromUtf8(body).contains(QStringLiteral("Stays local")));

        // Mapping it again brings it back for CalDAV.
        const QString href = m_server->collections().value(0).href;
        a.sync->setCalendarForList(work, href);
        QVERIFY(!a.sync->isListSkipped(work));

        // Excluded everywhere: mapping it in CalDAV keeps it excluded and skipped.
        SyncTree::setListExcluded(*a.store, secret, true);
        a.sync->setCalendarForList(secret, href);
        QVERIFY(SyncTree::isListExcluded(*a.store, secret));
        QVERIFY(a.sync->isListSkipped(secret));
    }

    // ------------------------------------------------------------ local edits

    void localCreateEditDeleteReachTheServer() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);

        addTask(*a.store, "Created here", list);
        QVERIFY(a.sync->syncBlocking().ok);
        QCOMPARE(m_server->tasks()->resources.size(), 1);
        const QString href = m_server->tasks()->resources.constBegin().key();
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(href)).contains("Created here"));

        Task t = a.store->task(find(*a.store, "Created here"));
        QTest::qWait(1100);
        t.title = "Edited here";
        edit(*a.store, t);
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(href)).contains("Edited here"));

        Task gone = a.store->task(find(*a.store, "Edited here"));
        gone.deleted = true;
        edit(*a.store, gone, "delete");
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(!m_server->tasks()->resources.contains(href));
        QCOMPARE(a.store->syncLinks("caldav", "tasks").size(), 0);
    }

    // ----------------------------------------------------------- remote edits

    void remoteCreateEditDeleteReachTheStore() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);

        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"), TaskTwo);
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = find(*a.store, "Second");
        QVERIFY(id);
        QCOMPARE(a.store->task(id).listId, list);

        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"),
                                        QByteArray(TaskTwo).replace("Second", "Second, edited"));
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(find(*a.store, "Second, edited"));
        const int before = liveTasks(*a.store);
        QVERIFY(a.sync->syncBlocking().ok);
        QCOMPARE(liveTasks(*a.store), before);

        m_server->removeFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"));
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(!find(*a.store, "Second, edited"));
    }

    // An abandoned task reaches the server as STATUS:CANCELLED, and a
    // cancelled to-do from another client arrives abandoned; a done task
    // stays COMPLETED on the wire.
    void abandonedAndCancelledGoBothWays() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);

        const qint64 wontDo = addTask(*a.store, "Won't do", list);
        const qint64 didIt = addTask(*a.store, "Did it", list);
        QVERIFY(a.sync->syncBlocking().ok);
        QTest::qWait(1100);
        Task w = a.store->task(wontDo);
        w.completed = true;
        w.abandoned = true;
        w.completedAt = QDateTime::currentDateTime();
        edit(*a.store, w, "abandon");
        Task d = a.store->task(didIt);
        d.completed = true;
        d.completedAt = QDateTime::currentDateTime();
        edit(*a.store, d, "complete");
        QVERIFY(a.sync->syncBlocking().ok);

        QString cancelled, completed;
        for (auto it = m_server->tasks()->resources.constBegin(); it != m_server->tasks()->resources.constEnd(); ++it) {
            const QString body = QString::fromUtf8(it.value());
            if (body.contains("Won't do"))
                cancelled = body;
            else if (body.contains("Did it"))
                completed = body;
        }
        QVERIFY(cancelled.contains("STATUS:CANCELLED"));
        QVERIFY(completed.contains("STATUS:COMPLETED"));

        // Another client cancels a to-do; here it is abandoned, and a second
        // sync leaves it that way.
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"), TaskTwo);
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(find(*a.store, "Second"));
        m_server->editFromAnotherClient(
            QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"),
            QByteArray(TaskTwo).replace("SUMMARY:Second", "SUMMARY:Second\r\nSTATUS:CANCELLED").replace("20250101", "20250102"));
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(a.sync->syncBlocking().ok);
        const Task second = a.store->task(find(*a.store, "Second"));
        QVERIFY(second.completed);
        QVERIFY(second.abandoned);
        QVERIFY(a.store->task(wontDo).abandoned);
        QVERIFY(!a.store->task(didIt).abandoned);
    }

    void newestEditWinsRemoteOverLocal() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        addTask(*a.store, "The task", list);
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = find(*a.store, "The task");

        Task remoteTask = a.store->task(id);
        remoteTask.title = "Remote wins";
        remoteTask.modified = QDateTime::currentDateTime().addSecs(60);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("t.ics"),
                                        Vtodo::toIcs(*a.store, remoteTask));
        repoint(*a.store, id, QStringLiteral("/calendars/jane/tasks/t.ics"));
        a.store->removeSyncLink("caldav", "tasks", id);
        repoint(*a.store, id, QStringLiteral("/calendars/jane/tasks/t.ics"));

        Task mine = a.store->task(id);
        mine.title = "Local loses";
        mine.modified = QDateTime::currentDateTime().addSecs(-60);
        edit(*a.store, mine);

        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(find(*a.store, "Remote wins"));
        QVERIFY(!find(*a.store, "Local loses"));
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(QStringLiteral("/calendars/jane/tasks/t.ics")))
                    .contains("Remote wins"));
    }

    void newestEditWinsLocalOverRemote() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        addTask(*a.store, "The task", list);
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = find(*a.store, "The task");

        Task remoteTask = a.store->task(id);
        remoteTask.title = "Remote loses";
        remoteTask.modified = QDateTime::currentDateTime().addSecs(-60);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("t.ics"),
                                        Vtodo::toIcs(*a.store, remoteTask));
        repoint(*a.store, id, QStringLiteral("/calendars/jane/tasks/t.ics"));

        Task mine = a.store->task(id);
        mine.title = "Local wins";
        mine.modified = QDateTime::currentDateTime().addSecs(60);
        edit(*a.store, mine);

        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(find(*a.store, "Local wins"));
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(QStringLiteral("/calendars/jane/tasks/t.ics")))
                    .contains("Local wins"));
    }

    void a412DuringPutIsResolved() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        addTask(*a.store, "The task", list);
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = find(*a.store, "The task");

        // The server changes the resource under us, so the link's etag is
        // stale and the next PUT answers 412.
        Task remoteTask = a.store->task(id);
        remoteTask.title = "Server copy";
        remoteTask.modified = QDateTime::currentDateTime().addSecs(-60);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("t.ics"),
                                        Vtodo::toIcs(*a.store, remoteTask));
        repoint(*a.store, id, QStringLiteral("/calendars/jane/tasks/t.ics"));

        Task mine = a.store->task(id);
        mine.title = "Local retry";
        mine.modified = QDateTime::currentDateTime().addSecs(60);
        edit(*a.store, mine);
        const int conflictsBefore = 0;
        QVERIFY(a.sync->syncBlocking().ok);
        Q_UNUSED(conflictsBefore);
        QVERIFY(find(*a.store, "Local retry"));
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(QStringLiteral("/calendars/jane/tasks/t.ics")))
                    .contains("Local retry"));
    }

    void deletedRemotelyWhileEditedLocallyIsUploadedAgain() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        addTask(*a.store, "Keep me", list);
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = find(*a.store, "Keep me");
        const QString href = a.store->syncLinks("caldav", "tasks").value(id).remoteId;

        // The server removes it, but the task is edited locally meanwhile.
        m_server->removeFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QFileInfo(href).fileName());
        Task mine = a.store->task(id);
        mine.title = "Keep me, edited";
        mine.modified = QDateTime::currentDateTime().addSecs(30);
        edit(*a.store, mine);

        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(find(*a.store, "Keep me, edited"));
        bool found = false;
        for (const QByteArray &body : m_server->tasks()->resources)
            if (QString::fromUtf8(body).contains("Keep me\\, edited"))
                found = true;
        QVERIFY(found);
    }

    void moveBetweenListsMovesBetweenCalendars() {
        Account a = makeAccount();
        const qint64 home = addList(*a.store, "Tasks");
        addTask(*a.store, "The task", home);
        FakeCalDav::Collection work;
        work.href = QStringLiteral("/calendars/jane/work/");
        work.displayName = QStringLiteral("Work");
        work.components = {QStringLiteral("VTODO")};
        m_server->collections().append(work);
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        QCOMPARE(m_server->tasks()->resources.size(), 1);
        qint64 workList = 0;
        for (const auto &l : a.store->lists())
            if (l.name == "Work")
                workList = l.id;
        QVERIFY(workList);

        Task t = a.store->task(find(*a.store, "The task"));
        QTest::qWait(1100);
        t.listId = workList;
        edit(*a.store, t, "move");
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(m_server->tasks()->resources.isEmpty());
        FakeCalDav::Collection *workCol = nullptr;
        for (FakeCalDav::Collection &c : m_server->collections())
            if (c.href == QStringLiteral("/calendars/jane/work/"))
                workCol = &c;
        QVERIFY(workCol);
        QCOMPARE(workCol->resources.size(), 1);
    }

    void subTasksKeepTheirParent() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        const qint64 parent = addTask(*a.store, "Parent", list);
        addTask(*a.store, "Child", list);
        Task child = a.store->task(find(*a.store, "Child"));
        child.parentId = parent;
        edit(*a.store, child, "parent");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        QCOMPARE(m_server->tasks()->resources.size(), 2);

        // A second machine pulls the same server: the child keeps its parent.
        Account b = makeAccount();
        connect(b, *m_server);
        const auto pull = b.sync->syncBlocking();
        QVERIFY2(pull.ok, qPrintable(pull.error));
        const qint64 newChild = find(*b.store, "Child");
        const qint64 newParent = find(*b.store, "Parent");
        QVERIFY(newChild && newParent);
        QCOMPARE(b.store->task(newChild).parentId, newParent);
    }

    // ------------------------------------------------------------- ctag skip

    void unchangedCtagSkipsTheCalendar() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        addTask(*a.store, "Mine", list);
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        const int reports = m_server->countRequests("REPORT");
        const auto r = a.sync->syncBlocking();
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.changed, 0);
        // The unchanged calendar is skipped: no listing REPORT at all.
        QCOMPARE(m_server->countRequests("REPORT"), reports);
    }

    // ------------------------------------------------------------ first sync

    void firstSyncAsksAndMergeKeepsBoth() {
        Account seed = makeAccount();
        const qint64 seedList = addList(*seed.store, "Tasks");
        connect(seed, *m_server);
        QVERIFY(seed.sync->syncBlocking().ok);
        addTask(*seed.store, "Mine", seedList);
        QVERIFY(seed.sync->syncBlocking().ok);

        Account fresh = makeAccount();
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("remote.ics"), TaskTwo);
        const qint64 localList = addList(*fresh.store, "Tasks");
        addTask(*fresh.store, "Local one", localList);
        connect(fresh, *m_server);
        const auto ask = fresh.sync->syncBlocking(SyncTree::FirstSync::Ask);
        QVERIFY(ask.needsChoice);
        QVERIFY(fresh.sync->needsChoice());
        fresh.sync->chooseFirstSync("merge");
        QTRY_VERIFY_WITH_TIMEOUT(!fresh.sync->busy(), 8000);
        QVERIFY(find(*fresh.store, "Mine"));
        QVERIFY(find(*fresh.store, "Second"));
        QVERIFY(find(*fresh.store, "Local one"));
    }

    void replaceLocalMakesABackup() {
        Account fresh = makeAccount();
        const qint64 list = addList(*fresh.store, "Tasks");
        connect(fresh, *m_server);
        addTask(*fresh.store, "Local only", list);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("remote.ics"), TaskTwo);
        const auto ask = fresh.sync->syncBlocking(SyncTree::FirstSync::Ask);
        QVERIFY2(ask.needsChoice, qPrintable(fresh.sync->status()));
        fresh.sync->chooseFirstSync("local");
        QTRY_VERIFY_WITH_TIMEOUT(!fresh.sync->busy(), 8000);
        QVERIFY(!find(*fresh.store, "Local only"));
        QVERIFY(find(*fresh.store, "Second"));
        const QString backups = QFileInfo(fresh.store->path()).absolutePath() + "/backups";
        QVERIFY(QDir(backups).exists());
        QVERIFY(!QDir(backups).entryList(QDir::Files).isEmpty());
    }

    void replaceServerOverwritesTheRemote() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        addTask(*a.store, "Old round", list);
        QVERIFY(a.sync->syncBlocking().ok);
        // Forget the mapping and stage a local task that must survive while
        // the server's old one goes.
        a.store->clearSyncLinks("caldav");
        a.store->removeSetting("caldav.mapped");
        Task gone = a.store->task(find(*a.store, "Old round"));
        gone.deleted = true;
        edit(*a.store, gone, "delete");
        addTask(*a.store, "New round", list);
        const auto r = a.sync->syncBlocking(SyncTree::FirstSync::ReplaceServer);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!m_server->tasks()->resources.isEmpty());
        for (const QByteArray &body : m_server->tasks()->resources)
            QVERIFY(!QString::fromUtf8(body).contains("Old round"));
        bool found = false;
        for (const QByteArray &body : m_server->tasks()->resources)
            if (QString::fromUtf8(body).contains("New round"))
                found = true;
        QVERIFY(found);
    }

    // ------------------------------------------------------------ disconnect

    void disconnectClearsLinksAndKeyringWithoutDeleting() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        addTask(*a.store, "Mine", list);
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(!a.store->syncLinks("caldav", "tasks").isEmpty());
        const int serverTasks = m_server->tasks()->resources.size();
        QVERIFY(serverTasks > 0);
        a.sync->disconnectAccount();
        QVERIFY(a.store->syncLinks("caldav", "tasks").isEmpty());
        QVERIFY(a.store->syncLinks("caldav", "lists").isEmpty());
        QVERIFY(Keyring::lookup("fairfax", "caldav").isEmpty());
        QVERIFY(!a.sync->configured());
        QVERIFY(liveTasks(*a.store) > 0);
        QCOMPARE(m_server->tasks()->resources.size(), serverTasks);
    }

    void vendorMetadataSurvivesAnEdit() {
        Account a = makeAccount();
        addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("vendor.ics"),
                                        QByteArray(VendorTask));
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = find(*a.store, "From another client");
        QVERIFY(id);

        Task t = a.store->task(id);
        QTest::qWait(1100);
        t.title = "Edited in Fairfax";
        edit(*a.store, t);
        QVERIFY(a.sync->syncBlocking().ok);

        const QByteArray body = m_server->tasks()->resources.value(QStringLiteral("/calendars/jane/tasks/vendor.ics"));
        QVERIFY(!body.isEmpty());
        QVERIFY(body.contains("Edited in Fairfax"));
        QVERIFY(body.contains("X-EXAMPLE-FOO;X=1:bar"));
        QVERIFY(body.contains("<Example Meta>{\"x\":1}</Example Meta>"));
    }

    // -------------------------------------------- unknown etag on a new write

    void anUnknownEtagIsNotTreatedAsNew() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        addTask(*a.store, "Mine", list);
        // The PUT omits its ETag, and the follow-up PROPFIND that would read
        // it fails once, so the resource ends up linked with an unknown etag.
        m_server->omitEtagOnPut = true;
        m_server->failEtagFetchOnce = true;
        const auto first = a.sync->syncBlocking();
        QVERIFY2(first.ok, qPrintable(first.error));
        QCOMPARE(m_server->tasks()->resources.size(), 1);
        const int ifNoneMatchAfterFirst = m_server->putsWithIfNoneMatch;

        // A second sync must learn the etag from the listing, never send
        // If-None-Match: * for the existing resource, and never duplicate it.
        m_server->omitEtagOnPut = false;
        const auto second = a.sync->syncBlocking();
        QVERIFY2(second.ok, qPrintable(second.error));
        QCOMPARE(m_server->tasks()->resources.size(), 1);
        QCOMPARE(m_server->putsWithIfNoneMatch, ifNoneMatchAfterFirst);
    }

    // ------------------------------------------------------ calendar 403 write

    void aForbiddenWriteIsReportedWithoutStopping() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Read-only");
        FakeCalDav::Collection ro;
        ro.href = QStringLiteral("/calendars/jane/readonly/");
        ro.displayName = QStringLiteral("Read-only");
        ro.components = {QStringLiteral("VTODO")};
        m_server->collections().append(ro);
        m_server->readOnlyCalendars.insert(ro.href);
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        addTask(*a.store, "Cannot go up", list);
        const auto r = a.sync->syncBlocking();
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(a.sync->status().contains("No permission to write"));
        QVERIFY(find(*a.store, "Cannot go up"));
    }

    // Stages a server-side edit of a synced task and returns its local id.
    qint64 stageSyncedTask(Account &a, const QString &title, qint64 list) {
        addTask(*a.store, title, list);
        if (!a.sync->syncBlocking().ok)
            return 0;
        return find(*a.store, title);
    }

    static void putRemote(FakeCalDav &server, Store &store, qint64 id, const QString &title, int secondsFromNow) {
        Task remoteTask = store.task(id);
        remoteTask.title = title;
        remoteTask.modified = QDateTime::currentDateTime().addSecs(secondsFromNow);
        server.editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("t.ics"),
                                     Vtodo::toIcs(store, remoteTask));
        repoint(store, id, QStringLiteral("/calendars/jane/tasks/t.ics"));
    }

    static void addAlert(Store &s, qint64 taskId, int minutes = 60) {
        Alarm al;
        al.id = s.nextId("alarms");
        al.taskId = taskId;
        al.kind = Alarm::Absolute;
        al.at = QDateTime(QDate(2031, 5, 4), QTime(9, 0)).addSecs(minutes * 60);
        s.begin("Add Alert");
        s.putAlarm(al);
        s.commit();
    }

    void alertOnlyChangesAreSynced() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = stageSyncedTask(a, "Alerted", list);
        QVERIFY(id);
        const QString href = a.store->syncLinks("caldav", "tasks").constBegin()->remoteId;
        QVERIFY(!QString::fromUtf8(m_server->tasks()->resources.value(href)).contains("VALARM"));

        addAlert(*a.store, id);
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(href)).contains("VALARM"));

        a.store->begin("Remove Alert");
        for (Alarm al : a.store->alarmsFor(id)) {
            al.deleted = true;
            a.store->putAlarm(al);
        }
        a.store->commit();
        QVERIFY(a.sync->syncBlocking().ok);
        QVERIFY(!QString::fromUtf8(m_server->tasks()->resources.value(href)).contains("VALARM"));
    }

    void remoteEditAfterLocalAlertFollowsNewestWins() {
        for (const bool remoteNewer : {true, false}) {
            m_server = std::make_unique<FakeCalDav>();
            Account a = makeAccount();
            const qint64 list = addList(*a.store, "Tasks");
            connect(a, *m_server);
            QVERIFY(a.sync->syncBlocking().ok);
            const qint64 id = stageSyncedTask(a, "Alerted", list);
            QVERIFY(id);
            putRemote(*m_server, *a.store, id, "Remote edit", remoteNewer ? 3600 : -3600);
            addAlert(*a.store, id);
            QVERIFY(a.sync->syncBlocking().ok);
            const QString body = QString::fromUtf8(m_server->tasks()->resources.value(QStringLiteral("/calendars/jane/tasks/t.ics")));
            if (remoteNewer) {
                QVERIFY(find(*a.store, "Remote edit"));
                QVERIFY(a.store->alarmsFor(id).isEmpty());
                QVERIFY(!body.contains("VALARM"));
            } else {
                QCOMPARE(a.store->alarmsFor(id).size(), 1);
                QVERIFY(body.contains("VALARM"));
            }
        }
    }

    void conflictIsRecordedInDiagnostics() {
        Account a = makeAccount();
        const qint64 list = addList(*a.store, "Tasks");
        connect(a, *m_server);
        QVERIFY(a.sync->syncBlocking().ok);
        const qint64 id = stageSyncedTask(a, "The task", list);
        QVERIFY(id);
        const QString uid = a.store->task(id).uid;
        putRemote(*m_server, *a.store, id, "Remote edit", -60);
        Task mine = a.store->task(id);
        mine.title = "Local edit";
        mine.modified = QDateTime::currentDateTime().addSecs(60);
        edit(*a.store, mine);
        Diagnostics::clear();
        QVERIFY(a.sync->syncBlocking().ok);
        bool found = false;
        for (const QString &line : Diagnostics::recent())
            if (line.contains("CalDAV conflict") && line.contains(uid) && line.contains("local wins"))
                found = true;
        QVERIFY2(found, qPrintable(Diagnostics::recent().join('\n')));
    }

    void replaceLocalTwiceMakesTwoBackups() {
        Account fresh = makeAccount();
        const qint64 list = addList(*fresh.store, "Tasks");
        connect(fresh, *m_server);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("remote.ics"), TaskTwo);
        const QString backups = QFileInfo(fresh.store->path()).absolutePath() + "/backups";
        for (int round = 1; round <= 2; ++round) {
            addTask(*fresh.store, QString("Local %1").arg(round), list);
            fresh.store->clearSyncLinks("caldav");
            fresh.store->removeSetting("caldav.mapped");
            const auto ask = fresh.sync->syncBlocking(SyncTree::FirstSync::Ask);
            QVERIFY2(ask.needsChoice, qPrintable(fresh.sync->status()));
            fresh.sync->chooseFirstSync("local");
            QTRY_VERIFY_WITH_TIMEOUT(!fresh.sync->busy(), 8000);
            QVERIFY(!find(*fresh.store, QString("Local %1").arg(round)));
            QCOMPARE(QDir(backups).entryList(QDir::Files).size(), round);
        }
    }

    void failedBackupLeavesLocalDataUntouched() {
        Account fresh = makeAccount();
        const qint64 list = addList(*fresh.store, "Tasks");
        connect(fresh, *m_server);
        addTask(*fresh.store, "Local only", list);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("remote.ics"), TaskTwo);
        // A file where the backups folder belongs makes the backup fail.
        QFile blocker(QFileInfo(fresh.store->path()).absolutePath() + "/backups");
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        const auto ask = fresh.sync->syncBlocking(SyncTree::FirstSync::Ask);
        QVERIFY(ask.needsChoice);
        fresh.sync->chooseFirstSync("local");
        QTRY_VERIFY_WITH_TIMEOUT(!fresh.sync->busy(), 8000);
        QVERIFY(find(*fresh.store, "Local only"));
        QVERIFY(!find(*fresh.store, "Second"));
        QVERIFY2(fresh.sync->status().contains("back up"), qPrintable(fresh.sync->status()));
    }
};

REGISTER_TEST(CalDavSyncTest)

#include "test_caldavsync.moc"
