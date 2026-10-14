#include <QBuffer>
#include <QtTest>
#include <memory>

#include "cli.h"
#include "store.h"
#include "testregistry.h"

// `fairfax import-mail`: a message on stdin, message files, or a Maildir.

namespace {

QByteArray mail(const QString &id, const QString &subject, const QString &body = QStringLiteral("body")) {
    return QStringLiteral("Message-ID: <%1@cli.test>\nDate: Tue, 13 Oct 2026 09:30:00 +0200\nFrom: Sam <sam@example.com>\n"
                          "Subject: %2\nContent-Type: text/plain; charset=utf-8\n\n%3\n")
        .arg(id, subject, body).toUtf8();
}

}  // namespace

class CliTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<Store> m_store;
    qint64 m_inbox = 0, m_work = 0;

    struct Run {
        int code = -1;
        QString out, err;
    };

    Run importMail(const QStringList &args, const QByteArray &stdinBytes = {}) {
        QBuffer in;
        in.setData(stdinBytes);
        in.open(QIODevice::ReadOnly);
        Run run;
        QTextStream out(&run.out), err(&run.err);
        run.code = Cli::importMail(*m_store, args, in, out, err);
        out.flush();
        err.flush();
        return run;
    }

    QString write(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("can't write %s", qPrintable(path));
        f.write(bytes);
        return path;
    }

    QString path(const QString &rel) const { return m_dir.filePath(rel); }

    QList<Task> tasks() const {
        QList<Task> out;
        for (const Task &t : m_store->tasks())
            if (!t.deleted)
                out << t;
        std::sort(out.begin(), out.end(), [](const Task &a, const Task &b) { return a.id < b.id; });
        return out;
    }

private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        qputenv("NO_COLOR", "1");  // The CLI paints only for a terminal; keep the output plain even then.
        m_store = std::make_unique<Store>();
        QVERIFY(m_store->open(path(QStringLiteral("cli-%1.db").arg(QTest::currentTestFunction()))));
        TaskList inbox, work;
        inbox.id = m_store->nextId("lists");
        inbox.kind = ListKind::Inbox;
        inbox.name = "Inbox";
        work.id = m_store->nextId("lists");
        work.name = "Work";
        work.order = 1;
        m_store->begin("setup");
        m_store->putList(inbox);
        m_store->putList(work);
        m_store->commit();
        m_inbox = inbox.id;
        m_work = work.id;
    }

    void stdinMessage() {
        const Run r = importMail({}, mail("s1", "Re: Buy milk priority(!)", "2 litres"));
        QCOMPARE(r.err, "");
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, "Added Buy milk to Inbox\n");
        QCOMPARE(tasks().size(), 1);
        QCOMPARE(tasks().first().title, "Buy milk");
        QCOMPARE(tasks().first().notes, "2 litres");
        QCOMPARE(tasks().first().priority, int(Priority::Low));
        QCOMPARE(m_store->undoLabel(), "Mail");
    }

    void stdinDuplicateIsSkippedWithExitZero() {
        QCOMPARE(importMail({}, mail("s2", "Once")).code, 0);
        const Run again = importMail({}, mail("s2", "Once"));
        QCOMPARE(again.code, 0);
        QCOMPARE(again.out, "Skipped: duplicate\n");
        QCOMPARE(tasks().size(), 1);
    }

    void garbageOnStdinIsAnError() {
        const Run r = importMail({}, "this is not an email\nat all\n");
        QCOMPARE(r.code, 1);
        QVERIFY2(r.err.contains("stdin: not an email message"), qPrintable(r.err));
        QCOMPARE(r.out, "");
        QVERIFY(tasks().isEmpty());
        QCOMPARE(importMail({}, "").code, 1);
    }

    void filesAreImportedInOrder() {
        const QString a = write(path("a.eml"), mail("f1", "First"));
        const QString b = write(path("b.eml"), mail("f2", "Second"));
        const Run r = importMail({a, b});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, "Added First to Inbox\nAdded Second to Inbox\n");
        QCOMPARE(tasks().size(), 2);
        // Files are not touched.
        QVERIFY(QFile::exists(a) && QFile::exists(b));
    }

    void aBadFileDoesNotStopTheOthers() {
        const QString good = write(path("good.eml"), mail("f3", "Good"));
        const QString junk = write(path("junk.eml"), "nothing like a message");
        const Run r = importMail({path("missing.eml"), junk, good});
        QCOMPARE(r.code, 1);
        QCOMPARE(r.out, "Added Good to Inbox\n");
        QVERIFY(r.err.contains("can't read"));
        QVERIFY(r.err.contains("junk.eml: not an email message"));
        QCOMPARE(tasks().size(), 1);
    }

    void duplicateAmongFilesIsStillExitZero() {
        const QString a = write(path("d1.eml"), mail("f4", "Same"));
        const QString b = write(path("d2.eml"), mail("f4", "Same"));
        const Run r = importMail({a, b});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, "Added Same to Inbox\nSkipped: duplicate\n");
    }

    void listOptionChoosesTheDestination() {
        const Run r = importMail({"--list", "work"}, mail("l1", "To work"));
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, "Added To work to Work\n");
        QCOMPARE(tasks().first().listId, m_work);
    }

    void unknownListIsAUsageError() {
        const Run r = importMail({"--list", "Nope"}, mail("l2", "x"));
        QCOMPARE(r.code, 2);
        QVERIFY(r.err.contains("no list called"));
        QVERIFY(tasks().isEmpty());
    }

    void usageErrors() {
        QCOMPARE(importMail({"--bogus"}, mail("u1", "x")).code, 2);
        QCOMPARE(importMail({"--list"}, mail("u2", "x")).code, 2);
        QCOMPARE(importMail({"--maildir"}).code, 2);
        QCOMPARE(importMail({"--maildir", path("md"), "a.eml"}).code, 2);
        QVERIFY(tasks().isEmpty());
    }

    void aDatabaseWithoutListsIsAnError() {
        Store bare;
        QVERIFY(bare.open(path("bare.db")));
        QBuffer in;
        in.setData(mail("b1", "Nowhere to go"));
        in.open(QIODevice::ReadOnly);
        QString out, err;
        QTextStream o(&out), e(&err);
        QCOMPARE(Cli::importMail(bare, {}, in, o, e), 1);
        e.flush();
        QVERIFY2(err.contains("no list to put the task in"), qPrintable(err));
        QVERIFY(bare.tasks().isEmpty());
    }

    void naturalLanguageFlag() {
        const QByteArray raw = mail("n1", "Call Sam tomorrow #family");
        QCOMPARE(importMail({}, raw).out, "Added Call Sam tomorrow #family to Inbox\n");
        const Run r = importMail({"--natural-language"}, mail("n2", "Call Sam tomorrow #family"));
        QCOMPARE(r.out, "Added Call Sam to Inbox\n");
        const Task t = tasks().last();
        QVERIFY(t.dueDate.isValid());
        QCOMPARE(t.tagIds.size(), 1);
    }

    void maildirMovesNewToCurAsSeen() {
        const QString md = path("maildir");
        write(md + "/new/1700000001.M1.host", mail("m1", "One"));
        write(md + "/new/1700000002.M2.host", mail("m2", "Two"));
        const Run r = importMail({"--maildir", md});
        QCOMPARE(r.err, "");
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, "Added One to Inbox\nAdded Two to Inbox\n");
        QCOMPARE(tasks().size(), 2);
        QVERIFY(QDir(md + "/new").entryList(QDir::Files).isEmpty());
        QCOMPARE(QDir(md + "/cur").entryList(QDir::Files, QDir::Name),
                 (QStringList{"1700000001.M1.host:2,S", "1700000002.M2.host:2,S"}));
        // The message is moved as it was.
        QFile moved(md + "/cur/1700000001.M1.host:2,S");
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), mail("m1", "One"));
    }

    void maildirDuplicatesAreMovedToo() {
        const QString md = path("maildir2");
        QCOMPARE(importMail({}, mail("m3", "Seen before")).code, 0);
        write(md + "/new/dup.host", mail("m3", "Seen before"));
        const Run r = importMail({"--maildir", md});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, "Skipped: duplicate\n");
        QVERIFY(QDir(md + "/new").entryList(QDir::Files).isEmpty());
        QVERIFY(QFile::exists(md + "/cur/dup.host:2,S"));
        QCOMPARE(tasks().size(), 1);
    }

    void maildirLeavesBadMessagesInNewAndKeepsGoing() {
        const QString md = path("maildir3");
        write(md + "/new/a.junk", "not a message");
        write(md + "/new/b.good", mail("m4", "Good one"));
        write(md + "/new/.hidden", mail("m5", "Hidden"));
        const Run r = importMail({"--maildir", md, "--list", "Work"});
        QCOMPARE(r.code, 1);
        QCOMPARE(r.out, "Added Good one to Work\n");
        QVERIFY(QFile::exists(md + "/new/a.junk"));  // Left for a human to look at.
        QVERIFY(QFile::exists(md + "/cur/b.good:2,S"));
        QVERIFY2(QFile::exists(md + "/new/.hidden"), "dot files in a Maildir are not messages");
        QCOMPARE(tasks().size(), 1);
    }

    void maildirWithoutNewFolder() {
        QDir().mkpath(path("empty-maildir/cur"));
        const Run r = importMail({"--maildir", path("empty-maildir")});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("no new/ folder"));
        QCOMPARE(importMail({"--maildir", path("nowhere")}).code, 1);
    }

    void maildirCreatesCurAndRerunsFindNothing() {
        const QString md = path("maildir4");
        write(md + "/new/x.host", mail("m6", "Only once"));
        QCOMPARE(importMail({"--maildir", md}).code, 0);
        const Run again = importMail({"--maildir", md});
        QCOMPARE(again.code, 0);
        QCOMPARE(again.out, "");
        QCOMPARE(tasks().size(), 1);
    }

    void shorthandNameFromArgv0() {
        // Linux and macOS: the last path section must be exactly "frfx".
        for (const bool windows : {false, true}) {
            QVERIFY(Cli::isShorthandName("frfx", windows));
            QVERIFY(Cli::isShorthandName("/usr/bin/frfx", windows));
            QVERIFY(Cli::isShorthandName("/usr/local/bin/frfx", windows));
            QVERIFY(Cli::isShorthandName("./frfx", windows));
            QVERIFY(!Cli::isShorthandName("fairfax", windows));
            QVERIFY(!Cli::isShorthandName("/opt/frfx/fairfax", windows));
            QVERIFY(!Cli::isShorthandName("", windows));
        }
        QVERIFY(!Cli::isShorthandName("/Applications/Fairfax.app/Contents/MacOS/Fairfax", false));
        QVERIFY(!Cli::isShorthandName("FRFX", false));
        QVERIFY(!Cli::isShorthandName("frfx.exe", false));
        // Windows: .exe, backslashes and letter case.
        QVERIFY(Cli::isShorthandName("frfx.exe", true));
        QVERIFY(Cli::isShorthandName("C:\\Users\\me\\AppData\\Local\\Programs\\Fairfax\\frfx.exe", true));
        QVERIFY(Cli::isShorthandName("c:/tools/FRFX.EXE", true));
        QVERIFY(Cli::isShorthandName("FrFx", true));
        QVERIFY(!Cli::isShorthandName("C:\\Program Files\\frfx\\fairfax.exe", true));
        QVERIFY(!Cli::isShorthandName("C:\\Fairfax\\fairfax.exe", true));
    }
};

REGISTER_TEST(CliTest)

#include "test_cli.moc"
