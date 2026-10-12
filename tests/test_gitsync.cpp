#include <QtTest>
#include <memory>

#include "gitsync.h"
#include "store.h"
#include "testregistry.h"

class GitSyncTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_n = 0;

    struct Machine {
        std::unique_ptr<Store> store;
        std::unique_ptr<GitSync> sync;
    };

    static bool git(const QString &dir, const QStringList &args) {
        QProcess p;
        p.setWorkingDirectory(dir);
        p.start("git", args);
        return p.waitForFinished(20000) && p.exitCode() == 0;
    }

    QString bare(bool rejectFirstPush = false) {
        const QString path = m_dir.filePath(QString("remote%1.git").arg(++m_n));
        git(m_dir.path(), {"init", "--bare", "-q", "-b", "main", path});
        if (rejectFirstPush) {
            QFile hook(path + "/hooks/pre-receive");
            hook.open(QIODevice::WriteOnly);
            hook.write(QString("#!/bin/sh\nif [ ! -f '%1/marker' ]; then touch '%1/marker'; echo rejected >&2; exit 1; fi\n")
                           .arg(m_dir.path())
                           .toUtf8());
            hook.close();
            hook.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        return path;
    }

    Machine machine(const QString &name, const QString &remote = {}) {
        Machine m;
        m.store = std::make_unique<Store>();
        QString err;
        [&] { QVERIFY2(m.store->open(m_dir.filePath(name + QString::number(++m_n) + ".db"), &err), qPrintable(err)); }();
        m.sync = std::make_unique<GitSync>(m.store.get(), m_dir.filePath(name + QString::number(m_n) + "-wc"));
        if (!remote.isEmpty()) {
            m.sync->setRemote(remote);
            [&] { QTRY_VERIFY_WITH_TIMEOUT(!m.sync->busy(), 30000); }();
        }
        return m;
    }

    static qint64 addTask(Store &s, const QString &title, qint64 listId = 0) {
        Task t;
        t.id = s.nextId("tasks");
        t.listId = listId;
        t.title = title;
        s.begin("add");
        s.putTask(t);
        s.commit();
        return t.id;
    }

    static qint64 find(const Store &s, const QString &title) {
        for (const auto &t : s.tasks())
            if (t.title == title)
                return t.id;
        return 0;
    }

private slots:
    void initTestCase() {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git is not installed");
        qputenv("GIT_AUTHOR_NAME", "Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.com");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        QVERIFY(m_dir.isValid());
    }

    void treeRoundTrip() {
        Machine a = machine("a");
        Store &s = *a.store;
        ListGroup g;
        g.id = s.nextId("list_groups");
        g.name = "Work";
        TaskList l;
        l.id = s.nextId("lists");
        l.name = "Projects";
        l.groupId = g.id;
        l.color = 3;
        l.sortAscending = false;
        TagGroup tg;
        tg.id = s.nextId("tag_groups");
        tg.name = "Context";
        Tag t1, t2;
        t1.id = s.nextId("tags");
        t1.name = "home";
        t1.groupId = tg.id;
        t2.id = s.nextId("tags");
        t2.name = "phone";
        Task parent, child;
        parent.id = s.nextId("tasks");
        parent.title = "Parent";
        parent.type = TaskType::Project;
        parent.listId = l.id;
        child.id = s.nextId("tasks");
        child.title = "Child";
        child.notes = "multi\nline \"quoted\" é";
        child.listId = l.id;
        child.parentId = parent.id;
        child.tagIds = {t1.id, t2.id};
        child.dueDate = QDate(2026, 7, 15);
        child.dueTime = 600;
        child.priority = Priority::High;
        child.starred = true;
        child.order = 2.5;
        child.recurrence.frequency = Repeat::Weekly;
        child.recurrence.weekdays = 5;
        child.completedAt = QDateTime::fromSecsSinceEpoch(1700000000);
        Alarm al;
        al.id = s.nextId("alarms");
        al.taskId = child.id;
        al.offset = 15;
        al.sound = false;
        s.begin("all");
        s.putGroup(g);
        s.putList(l);
        s.putTagGroup(tg);
        s.putTag(t1);
        s.putTag(t2);
        s.putTask(child);
        s.putTask(parent);
        s.putAlarm(al);
        s.commit();

        const QString dir = m_dir.filePath("tree");
        QString err;
        QVERIFY2(GitSyncFiles::writeTree(s, dir, QDateTime::currentDateTime(), &err), qPrintable(err));
        QVERIFY(QFile::exists(dir + "/Store.json"));

        // Unchanged files are not rewritten.
        const QString childFile = dir + "/tasks/" + s.task(child.id).uid + ".json";
        const QByteArray before = [&] { QFile f(childFile); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        QVERIFY(before.endsWith("}\n"));
        QVERIFY(before.contains("\"list_uid\""));
        QVERIFY(!before.contains("\"id\""));
        QFile::setPermissions(childFile, QFile::permissions(childFile));
        const QDateTime mtime = QFileInfo(childFile).lastModified();
        QTest::qSleep(30);
        QVERIFY(GitSyncFiles::writeTree(s, dir, QDateTime::currentDateTime()));
        QCOMPARE(QFileInfo(childFile).lastModified(), mtime);

        Machine b = machine("b");
        QCOMPARE(GitSyncFiles::readTree(*b.store, dir), 8);
        QCOMPARE(b.store->tasks().size(), 2);
        const Task bc = b.store->task(find(*b.store, "Child"));
        const Task bp = b.store->task(find(*b.store, "Parent"));
        const Task sc = s.task(child.id);
        QCOMPARE(bc.uid, sc.uid);
        QCOMPARE(bc.notes, sc.notes);
        QCOMPARE(bc.dueDate, sc.dueDate);
        QCOMPARE(bc.dueTime, 600);
        QCOMPARE(bc.priority, int(Priority::High));
        QVERIFY(bc.starred);
        QCOMPARE(bc.order, 2.5);
        QCOMPARE(bc.recurrence.weekdays, 5);
        QCOMPARE(bc.completedAt, sc.completedAt);
        QCOMPARE(bc.modified, sc.modified);
        QCOMPARE(bc.parentId, bp.id);
        QCOMPARE(b.store->list(bc.listId).name, QString("Projects"));
        QCOMPARE(b.store->list(bc.listId).uid, s.list(l.id).uid);
        QVERIFY(!b.store->list(bc.listId).sortAscending);
        QVERIFY(b.store->groups().contains(b.store->list(bc.listId).groupId));
        QCOMPARE(bc.tagIds.size(), 2);
        QCOMPARE(b.store->tag(bc.tagIds.first()).name, QString("home"));
        QVERIFY(b.store->tagGroups().contains(b.store->tag(bc.tagIds.first()).groupId));
        QCOMPARE(b.store->alarms().size(), 1);
        QCOMPARE(b.store->alarms().begin()->taskId, bc.id);
        QCOMPARE(b.store->alarms().begin()->offset, 15);
        QVERIFY(!b.store->alarms().begin()->sound);
        // Reading the same tree again changes nothing.
        QCOMPARE(GitSyncFiles::readTree(*b.store, dir), 0);
    }

    void tombstonePruning() {
        Machine a = machine("a");
        Store &s = *a.store;
        const qint64 id = addTask(s, "Gone");
        Task t = s.task(id);
        t.deleted = true;
        s.begin("del");
        s.putTask(t);
        s.commit();
        const QString dir = m_dir.filePath("tomb");
        const QString file = dir + "/tasks/" + s.task(id).uid + ".json";
        QVERIFY(GitSyncFiles::writeTree(s, dir, QDateTime::currentDateTime().addDays(59)));
        QVERIFY(QFile::exists(file));
        QVERIFY(GitSyncFiles::writeTree(s, dir, QDateTime::currentDateTime().addDays(61)));
        QVERIFY(!QFile::exists(file));
    }

    void twoMachines() {
        const QString remote = bare();
        Machine a = machine("a", remote);
        Machine b = machine("b", remote);
        QVERIFY(a.sync->configured());

        addTask(*a.store, "From A");
        QVERIFY(a.sync->syncBlocking());
        QVERIFY(b.sync->syncBlocking());
        QVERIFY(find(*b.store, "From A"));

        // Edit in B, A picks it up.
        QTest::qSleep(1100);
        {
            Task t = b.store->task(find(*b.store, "From A"));
            t.title = "Edited in B";
            b.store->begin("e");
            b.store->putTask(t);
            b.store->commit();
        }
        QVERIFY(b.sync->syncBlocking());
        QVERIFY(a.sync->syncBlocking());
        QVERIFY(find(*a.store, "Edited in B"));
        QVERIFY(!find(*a.store, "From A"));

        // Concurrent edits of different tasks merge.
        addTask(*a.store, "T1");
        addTask(*b.store, "T2");
        QVERIFY(a.sync->syncBlocking());
        QVERIFY(b.sync->syncBlocking());
        QVERIFY(a.sync->syncBlocking());
        QVERIFY(find(*a.store, "T2"));
        QVERIFY(find(*b.store, "T1"));
        QCOMPARE(a.store->tasks().size(), b.store->tasks().size());

        // The same task on both sides: the later edit wins.
        QTest::qSleep(1100);
        {
            Task t = a.store->task(find(*a.store, "T1"));
            t.title = "A said";
            a.store->begin("e");
            a.store->putTask(t);
            a.store->commit();
        }
        QTest::qSleep(1100);
        {
            Task t = b.store->task(find(*b.store, "T1"));
            t.title = "B said";
            b.store->begin("e");
            b.store->putTask(t);
            b.store->commit();
        }
        QVERIFY(b.sync->syncBlocking());  // B pushes its newer edit first
        QVERIFY(a.sync->syncBlocking());  // A must lose
        QVERIFY(b.sync->syncBlocking());
        QVERIFY(find(*a.store, "B said"));
        QVERIFY(find(*b.store, "B said"));
        QVERIFY(!find(*a.store, "A said"));

        // And the other order: A pushes the older edit last, still loses.
        QTest::qSleep(1100);
        {
            Task t = b.store->task(find(*b.store, "B said"));
            t.title = "B newer";
            b.store->begin("e");
            b.store->putTask(t);
            b.store->commit();
        }
        QTest::qSleep(1100);
        {
            Task t = a.store->task(find(*a.store, "B said"));
            t.title = "A newest";
            a.store->begin("e");
            a.store->putTask(t);
            a.store->commit();
        }
        QVERIFY(a.sync->syncBlocking());
        QVERIFY(b.sync->syncBlocking());
        QVERIFY(find(*b.store, "A newest"));

        // Deletion propagates.
        {
            Task t = a.store->task(find(*a.store, "T2"));
            t.deleted = true;
            a.store->begin("d");
            a.store->putTask(t);
            a.store->commit();
        }
        QVERIFY(a.sync->syncBlocking());
        QVERIFY(b.sync->syncBlocking());
        const Task gone = b.store->task(find(*b.store, "T2"));
        QVERIFY(gone.deleted);
    }

    void rejectedPushRetries() {
        const QString remote = bare(true);
        Machine a = machine("a", remote);  // the first push is rejected by the hook, then retried
        addTask(*a.store, "Retry me");
        QVERIFY(a.sync->syncBlocking());
        Machine b = machine("b", remote);
        QVERIFY(b.sync->syncBlocking());
        QVERIFY(find(*b.store, "Retry me"));
    }

    void notConfigured() {
        Machine a = machine("a");
        QCOMPARE(a.sync->status(), QString("Not set up"));
        QVERIFY(!a.sync->syncBlocking());
    }

    void disconnectClears() {
        const QString remote = bare();
        Machine a = machine("a", remote);
        QVERIFY(a.sync->configured());
        a.sync->disconnectRemote();
        QVERIFY(!a.sync->configured());
        QCOMPARE(a.sync->status(), QString("Not set up"));
        QVERIFY(QFileInfo::exists(a.sync->directory() + "/.git"));
    }
};

REGISTER_TEST(GitSyncTest)
#include "test_gitsync.moc"
