#include <QtTest>
#include <memory>

#include "fakeimap.h"
#include "imapclient.h"
#include "testregistry.h"

// The IMAP client against a scripted fake server (tests/fakeimap.h) over plain
// TCP with security None, as a ProtonMail Bridge or DavMail on 127.0.0.1 would
// be reached. Nothing here touches the network beyond the loopback.

namespace {

using Response = ImapClient::Response;

}  // namespace

class ImapClientTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeImap> m_server;
    std::unique_ptr<ImapClient> m_client;

    // Connects and waits for the greeting.
    bool connectClient(int timeout = 5000) {
        m_client = std::make_unique<ImapClient>();
        m_client->setTimeout(timeout);
        bool done = false, ok = false;
        m_client->connectToServer("127.0.0.1", m_server->port(), ImapClient::Security::None, [&](const Response &r) { done = true; ok = r.ok; });
        QTest::qWaitFor([&] { return done; }, 5000);
        return ok;
    }

    // Runs a command that reports through a Done and waits for it.
    Response run(const std::function<void(ImapClient::Done)> &start) {
        Response out;
        bool done = false;
        start([&](const Response &r) { out = r; done = true; });
        QTest::qWaitFor([&] { return done; }, 8000);
        if (!done)
            out.status = "HUNG";
        return out;
    }

    bool login(const QString &user = "jane", const QString &password = "app-password") {
        return run([&](ImapClient::Done d) { m_client->login(user, password, d); }).ok;
    }

    ImapClient::Selection select(const QString &folder = "INBOX") {
        ImapClient::Selection out;
        bool done = false;
        m_client->select(folder, [&](const ImapClient::Selection &s) { out = s; done = true; });
        QTest::qWaitFor([&] { return done; }, 5000);
        return out;
    }

    QList<qint64> search(const QByteArray &criteria, bool *ok = nullptr) {
        QList<qint64> out;
        bool done = false, good = false;
        m_client->uidSearch(criteria, [&](bool o, const QList<qint64> &uids, const QString &) { out = uids; good = o; done = true; });
        QTest::qWaitFor([&] { return done; }, 5000);
        if (ok)
            *ok = good;
        return out;
    }

private slots:
    void init() { m_server = std::make_unique<FakeImap>(); }
    void cleanup() {
        m_client.reset();
        m_server.reset();
    }

    // Connecting reads the greeting and the capabilities it carries.
    void greetingAndCapabilities() {
        QVERIFY(connectClient());
        QVERIFY(m_client->hasCapability("idle"));   // Names are case-insensitive.
        QVERIFY(m_client->hasCapability("MOVE"));
        QVERIFY(!m_client->hasCapability("X-GM-EXT-1"));
        m_server->gmail = true;
        QVERIFY(run([&](ImapClient::Done d) { m_client->capability(d); }).ok);
        QVERIFY(m_client->hasCapability("X-GM-EXT-1"));
        QVERIFY(m_server->log.contains("CAPABILITY"));
    }

    void connectionRefusedIsAFailedGreeting() {
        const quint16 dead = m_server->port();
        m_server.reset();   // Nothing listens there now.
        m_client = std::make_unique<ImapClient>();
        bool done = false;
        Response got;
        m_client->connectToServer("127.0.0.1", dead, ImapClient::Security::None, [&](const Response &r) { got = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!got.ok);
        QVERIFY(got.disconnected);
        QVERIFY(m_client->isClosed());
    }

    // ---------------------------------------------------------------- auth

    void loginWithPassword() {
        QVERIFY(connectClient());
        QVERIFY(login());
        QCOMPARE(m_server->mechanisms, QStringList{"LOGIN"});
        QVERIFY(m_server->log.contains("LOGIN \"jane\" \"app-password\""));
    }

    void wrongPasswordIsNo() {
        QVERIFY(connectClient());
        const Response r = run([&](ImapClient::Done d) { m_client->login("jane", "nope", d); });
        QVERIFY(!r.ok);
        QCOMPARE(r.status, QString("NO"));
        QVERIFY2(r.text.contains("Invalid credentials"), qPrintable(r.text));
        QVERIFY(!m_client->isClosed());   // A refused login does not end the connection.
    }

    // Quotes and backslashes in a password are escaped, not sent raw.
    void passwordWithQuotesIsEscaped() {
        m_server->password = "pa\"ss\\word";
        QVERIFY(connectClient());
        QVERIFY(login("jane", "pa\"ss\\word"));
    }

    // A password a quoted string cannot carry goes by AUTHENTICATE PLAIN.
    void nonAsciiPasswordUsesPlain() {
        m_server->password = QString::fromUtf8("pässwort");
        QVERIFY(connectClient());
        QVERIFY(login("jane", QString::fromUtf8("pässwort")));
        QCOMPARE(m_server->mechanisms, QStringList{"PLAIN"});
    }

    void loginDisabledUsesPlain() {
        m_server->capabilities << "LOGINDISABLED";
        QVERIFY(connectClient());
        QVERIFY(login());
        QCOMPARE(m_server->mechanisms, QStringList{"PLAIN"});
    }

    // The SASL XOAUTH2 string is "user=U^Aauth=Bearer T^A^A", base64.
    void xoauth2() {
        m_server->accessToken = "ya29.token";
        QVERIFY(connectClient());
        const Response r = run([&](ImapClient::Done d) { m_client->authenticateXoauth2("jane", "ya29.token", d); });
        QVERIFY2(r.ok, qPrintable(r.text));
        QCOMPARE(m_server->mechanisms, QStringList{"XOAUTH2"});
        QVERIFY(select().ok);   // Authenticated: SELECT is allowed now.
    }

    // A refused token gets the server's JSON error as a continuation; the client answers
    // it with an empty line, and the tagged NO then arrives.
    void xoauth2RefusedToken() {
        m_server->accessToken = "good";
        QVERIFY(connectClient());
        const Response r = run([&](ImapClient::Done d) { m_client->authenticateXoauth2("jane", "stale", d); });
        QVERIFY(!r.ok);
        QCOMPARE(r.status, QString("NO"));
        QVERIFY(!m_client->isClosed());
        // The same connection can try again.
        QVERIFY(run([&](ImapClient::Done d) { m_client->authenticateXoauth2("jane", "good", d); }).ok);
    }

    // ------------------------------------------------------ SELECT, SEARCH, FETCH

    void selectReportsValidityAndCount() {
        m_server->folders["INBOX"].validity = 4242;
        m_server->add("INBOX", FakeImap::mail("a@x.org", "one"));
        m_server->add("INBOX", FakeImap::mail("a@x.org", "two"));
        QVERIFY(connectClient());
        QVERIFY(login());
        const auto s = select();
        QVERIFY(s.ok);
        QCOMPARE(s.uidValidity, qint64(4242));
        QCOMPARE(s.exists, qint64(2));
        const auto missing = select("Nowhere");
        QVERIFY(!missing.ok);
        QVERIFY2(missing.error.contains("no such folder"), qPrintable(missing.error));
    }

    void searchWithQuotedStringsAndBooleans() {
        m_server->add("INBOX", FakeImap::mail("jane+precrime@gmail.com", "one"));
        m_server->add("INBOX", FakeImap::mail("bob@example.com", "two"));
        m_server->add("INBOX", FakeImap::mail("Jane Doe <jane+precrime@gmail.com>, bob@example.com", "three"), {"\\Seen"});
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QCOMPARE(search("ALL"), (QList<qint64>{1, 2, 3}));
        QCOMPARE(search("TO \"jane+precrime@gmail.com\""), (QList<qint64>{1, 3}));
        QCOMPARE(search("UNSEEN TO \"jane+precrime@gmail.com\""), (QList<qint64>{1}));
        QCOMPARE(search("OR TO \"bob@example.com\" CC \"nobody\""), (QList<qint64>{2, 3}));
        QCOMPARE(search("UID 2:*"), (QList<qint64>{2, 3}));
        bool ok = true;
        QVERIFY(search("TO \"zzz\"", &ok).isEmpty());
        QVERIFY(ok);   // An empty result is not an error.
    }

    // A message body comes back as a literal: {n}, CRLF, n bytes, which may themselves
    // contain CRLF, braces, parentheses and "* 1 FETCH" lookalikes.
    void fetchBodyAsLiteral() {
        const QByteArray tricky = FakeImap::mail("a@x.org", "tricky", "line one\r\n* 9 FETCH (UID 9)\r\n{12}\r\n)\r\nA001 OK fake\r\nlast line");
        m_server->add("INBOX", tricky);
        m_server->add("INBOX", FakeImap::mail("a@x.org", "second"));
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QByteArray got;
        bool done = false, ok = false;
        m_client->uidFetchMessage(1, [&](bool o, const QByteArray &bytes, const QString &) { ok = o; got = bytes; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(ok);
        QCOMPARE(got, tricky);
        // And it was fetched with PEEK: not marked read.
        QVERIFY(!m_server->hasFlag("INBOX", 1, "\\Seen"));
        QVERIFY(m_server->log.contains("UID FETCH 1 (BODY.PEEK[])"));
    }

    void fetchOfAMissingMessageFails() {
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        bool done = false, ok = true;
        QString error;
        m_client->uidFetchMessage(77, [&](bool o, const QByteArray &, const QString &e) { ok = o; error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!ok);
        QVERIFY2(error.contains("77"), qPrintable(error));
    }

    void fetchSizes() {
        m_server->add("INBOX", FakeImap::mail("a@x.org", "small"));
        const qint64 big = m_server->add("INBOX", FakeImap::mail("a@x.org", "big"));
        m_server->setSize("INBOX", big, 30 * 1024 * 1024);
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QHash<qint64, qint64> sizes;
        bool done = false;
        m_client->uidFetchSizes({1, 2}, [&](bool, const QHash<qint64, qint64> &s, const QString &) { sizes = s; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(sizes.size(), 2);
        QCOMPARE(sizes.value(1), qint64(m_server->find("INBOX", 1)->raw.size()));
        QCOMPARE(sizes.value(2), qint64(30 * 1024 * 1024));
        // No uids: nothing is sent.
        const int before = m_server->log.size();
        done = false;
        m_client->uidFetchSizes({}, [&](bool ok, const QHash<qint64, qint64> &, const QString &) { QVERIFY(ok); done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(m_server->log.size(), before);
    }

    // A response that arrives in tiny pieces, with a literal split across them, is read whole.
    void responseSplitAcrossPackets() {
        QTcpServer raw;
        QVERIFY(raw.listen(QHostAddress::LocalHost, 0));
        QTcpSocket *peer = nullptr;
        connect(&raw, &QTcpServer::newConnection, this, [&] {
            peer = raw.nextPendingConnection();
            peer->write("* OK [CAPABILITY IMAP4rev1] hi\r\n");
            connect(peer, &QTcpSocket::readyRead, peer, [peer] {
                const QByteArray line = peer->readAll();
                const QByteArray tag = line.left(line.indexOf(' '));
                const QByteArray reply = "* 1 FETCH (UID 5 BODY[] {12}\r\nhello\r\nworld)\r\n" + tag + " OK done\r\n";
                // Three bytes at a time, flushed each time.
                auto *timer = new QTimer(peer);
                auto *pos = new int(0);
                connect(timer, &QTimer::timeout, peer, [=] {
                    peer->write(reply.mid(*pos, 3));
                    peer->flush();
                    *pos += 3;
                    if (*pos >= reply.size())
                        timer->stop();
                });
                timer->start(1);
            });
        });
        m_client = std::make_unique<ImapClient>();
        bool greeted = false;
        m_client->connectToServer("127.0.0.1", raw.serverPort(), ImapClient::Security::None, [&](const Response &) { greeted = true; });
        QTRY_VERIFY_WITH_TIMEOUT(greeted, 5000);
        QByteArray got;
        bool done = false;
        m_client->uidFetchMessage(5, [&](bool, const QByteArray &bytes, const QString &) { got = bytes; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(got, QByteArray("hello\r\nworld"));
    }

    // ---------------------------------------------------------- STORE, MOVE

    void storeSeen() {
        m_server->add("INBOX", FakeImap::mail("a@x.org", "one"));
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QVERIFY(run([&](ImapClient::Done d) { m_client->uidStoreSeen(1, d); }).ok);
        QVERIFY(m_server->hasFlag("INBOX", 1, "\\Seen"));
    }

    void createFolderAndItsAlreadyExistsAnswer() {
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(run([&](ImapClient::Done d) { m_client->createFolder("Precrime", d); }).ok);
        QVERIFY(m_server->folders.contains("Precrime"));
        const Response again = run([&](ImapClient::Done d) { m_client->createFolder("Precrime", d); });
        QVERIFY(!again.ok);
        QVERIFY(again.text.contains("ALREADYEXISTS"));
    }

    void moveUsesUidMove() {
        m_server->add("INBOX", FakeImap::mail("a@x.org", "one"));
        m_server->folders["Precrime"];
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QVERIFY(run([&](ImapClient::Done d) { m_client->uidMove(1, "Precrime", d); }).ok);
        QCOMPARE(m_server->count("INBOX"), 0);
        QCOMPARE(m_server->count("Precrime"), 1);
        QVERIFY(m_server->log.contains("UID MOVE 1 \"Precrime\""));
    }

    // Without MOVE: COPY, mark the original \Deleted, UID EXPUNGE it.
    void moveFallsBackToCopyDeleteExpunge() {
        m_server->capabilities = {"IMAP4rev1", "IDLE", "UIDPLUS"};
        m_server->add("INBOX", FakeImap::mail("a@x.org", "one"));
        m_server->add("INBOX", FakeImap::mail("a@x.org", "two"), {"\\Deleted"});   // Someone else's pending delete: must survive.
        m_server->folders["Precrime"];
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QVERIFY(run([&](ImapClient::Done d) { m_client->uidMove(1, "Precrime", d); }).ok);
        QCOMPARE(m_server->count("Precrime"), 1);
        QVERIFY(!m_server->find("INBOX", 1));
        QVERIFY2(m_server->find("INBOX", 2), "UID EXPUNGE removed a message it was not asked to");
        const QStringList expected{"UID COPY 1 \"Precrime\"", "UID STORE 1 +FLAGS (\\Deleted)", "UID EXPUNGE 1"};
        QStringList seen;
        for (const QString &l : m_server->log)
            if (l.startsWith("UID COPY") || l.startsWith("UID STORE") || l.startsWith("UID EXPUNGE"))
                seen << l;
        QCOMPARE(seen, expected);
        QVERIFY(!m_server->log.filter("UID MOVE").size());
    }

    // No UIDPLUS either: the copy is made and the original is only flagged, never expunged wholesale.
    void moveFallbackWithoutUidplusDoesNotExpunge() {
        m_server->capabilities = {"IMAP4rev1"};
        m_server->add("INBOX", FakeImap::mail("a@x.org", "one"));
        m_server->folders["Precrime"];
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QVERIFY(run([&](ImapClient::Done d) { m_client->uidMove(1, "Precrime", d); }).ok);
        QVERIFY(m_server->hasFlag("INBOX", 1, "\\Deleted"));
        QVERIFY(m_server->log.filter("EXPUNGE").isEmpty());
    }

    void moveToAMissingFolderIsNo() {
        m_server->add("INBOX", FakeImap::mail("a@x.org", "one"));
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        const Response r = run([&](ImapClient::Done d) { m_client->uidMove(1, "Nope", d); });
        QVERIFY(!r.ok);
        QVERIFY(r.text.contains("TRYCREATE"));
        QCOMPARE(m_server->count("INBOX"), 1);
    }

    // ----------------------------------------------------------------- IDLE

    void idleWakesOnExists() {
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        QList<qint64> seen;
        bool ended = false;
        Response endResponse;
        m_client->idle([&](qint64 n) { seen << n; }, [&](const Response &r) { ended = true; endResponse = r; });
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isIdling(), 5000);
        QCOMPARE(m_server->idlers, 1);
        m_server->add("INBOX", FakeImap::mail("a@x.org", "new"));
        m_server->announce();
        QTRY_COMPARE_WITH_TIMEOUT(seen, (QList<qint64>{1}), 5000);
        QVERIFY(!ended);
        m_client->stopIdle();
        QTRY_VERIFY_WITH_TIMEOUT(ended, 5000);
        QVERIFY(endResponse.ok);
        QVERIFY(!m_client->isIdling());
        QCOMPARE(m_server->idlers, 0);
        // The connection is usable again after DONE.
        QCOMPARE(search("ALL"), (QList<qint64>{1}));
    }

    // stopIdle() called before the server's "+ idling" still ends the idle.
    void stopIdleBeforeTheServerIsReady() {
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(select().ok);
        bool ended = false;
        m_client->idle([](qint64) {}, [&](const Response &) { ended = true; });
        m_client->stopIdle();
        QTRY_VERIFY_WITH_TIMEOUT(ended, 5000);
        QCOMPARE(m_server->idlers, 0);
    }

    // IDLE has no timeout: a quiet hour is not an error.
    void idleOutlastsTheCommandTimeout() {
        QVERIFY(connectClient(300));
        QVERIFY(login());
        QVERIFY(select().ok);
        bool ended = false;
        m_client->idle([](qint64) {}, [&](const Response &) { ended = true; });
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isIdling(), 5000);
        QTest::qWait(900);
        QVERIFY(!ended);
        QVERIFY(!m_client->isClosed());
        m_client->stopIdle();
        QTRY_VERIFY_WITH_TIMEOUT(ended, 5000);
    }

    // -------------------------------------------------- errors and timeouts

    void aNoResponseCarriesItsText() {
        QVERIFY(connectClient());
        QVERIFY(login());
        const Response r = run([&](ImapClient::Done d) { m_client->command("SELECT \"Missing\"", d); });
        QVERIFY(!r.ok);
        QCOMPARE(r.status, QString("NO"));
        QVERIFY(r.text.contains("NONEXISTENT"));
        QVERIFY(!r.timedOut);
    }

    void aBadResponseIsReported() {
        QVERIFY(connectClient());
        QVERIFY(login());
        const Response r = run([&](ImapClient::Done d) { m_client->command("FROBNICATE", d); });
        QCOMPARE(r.status, QString("BAD"));
    }

    void aSilentServerTimesOut() {
        m_server->silentOn = "SELECT";
        QVERIFY(connectClient(300));
        QVERIFY(login());
        bool closed = false;
        connect(m_client.get(), &ImapClient::closed, this, [&] { closed = true; });
        QElapsedTimer clock;
        clock.start();
        ImapClient::Selection result;
        bool done = false;
        m_client->select("INBOX", [&](const ImapClient::Selection &s) { result = s; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!result.ok);
        QVERIFY2(clock.elapsed() < 3000, "the timeout did not fire promptly");
        QVERIFY(closed);
        QVERIFY(m_client->isClosed());
        // Later commands fail at once instead of hanging.
        const Response r = run([&](ImapClient::Done d) { m_client->capability(d); });
        QVERIFY(!r.ok);
        QVERIFY(r.disconnected);
    }

    // The timeout is reported as one: callers retry those, but not a plain NO.
    void timeoutIsFlagged() {
        m_server->silentOn = "NOOP";
        QVERIFY(connectClient(250));
        const Response r = run([&](ImapClient::Done d) { m_client->command("NOOP", d); });
        QVERIFY(r.timedOut);
        QVERIFY(r.disconnected);
    }

    void aDroppedConnectionFailsThePendingCommand() {
        QVERIFY(connectClient());
        QVERIFY(login());
        m_server->silentOn = "SELECT";
        bool done = false;
        Response got;
        m_client->command("SELECT \"INBOX\"", [&](const Response &r) { got = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(m_server->log.contains("SELECT \"INBOX\""), 5000);
        m_server->dropConnections();
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(got.disconnected);
        QVERIFY(!got.timedOut);
        QVERIFY(m_client->isClosed());
    }

    void commandsRunOneAtATimeInOrder() {
        m_server->add("INBOX", FakeImap::mail("a@x.org", "one"));
        QVERIFY(connectClient());
        QVERIFY(login());
        QStringList order;
        m_client->command("NOOP", [&](const Response &) { order << "noop"; });
        m_client->command("SELECT \"INBOX\"", [&](const Response &) { order << "select"; });
        m_client->command("UID SEARCH ALL", [&](const Response &r) { order << "search"; QVERIFY(r.ok); });
        QTRY_COMPARE_WITH_TIMEOUT(order, (QStringList{"noop", "select", "search"}), 5000);
    }

    void startTlsRefusedByAServerWithoutIt() {
        QVERIFY(connectClient());
        const Response r = run([&](ImapClient::Done d) { m_client->startTls(d); });
        QVERIFY(!r.ok);   // The fake has no STARTTLS: BAD, and the connection stays plain and usable.
        QVERIFY(login());
    }

    void logoutEndsTheSession() {
        QVERIFY(connectClient());
        QVERIFY(login());
        QVERIFY(run([&](ImapClient::Done d) { m_client->logout(d); }).ok);
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isClosed(), 5000);
    }
};

REGISTER_TEST(ImapClientTest)

#include "test_imapclient.moc"
