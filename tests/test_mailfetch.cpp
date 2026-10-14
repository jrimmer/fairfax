#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <memory>

#include "fakeimap.h"
#include "keyring.h"
#include "loopback.h"
#include "mailfetch.h"
#include "mailtask.h"
#include "store.h"
#include "testregistry.h"

// The Mail to Precrime connector against a fake IMAP server (tests/fakeimap.h),
// with the keyring in memory (tests/main.cpp) and the OAuth HTTP calls through
// MailFetch::setHttpTransport. Nothing here touches the network beyond the
// loopback, or the real keyring.

class MailFetchTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<Store> m_store;
    std::unique_ptr<FakeImap> m_server;
    std::unique_ptr<MailFetch> m_fetch;
    qint64 m_inbox = 0, m_work = 0;
    QUrl m_opened;

    qint64 addList(const QString &name, int kind = ListKind::Normal) {
        TaskList l;
        l.id = m_store->nextId("lists");
        l.name = name;
        l.kind = kind;
        l.order = l.id;
        m_store->begin("setup");
        m_store->putList(l);
        m_store->commit();
        return l.id;
    }

    // The tasks that came from mail, oldest first.
    QList<Task> mailTasks(bool includeDeleted = false) const {
        QList<Task> out;
        for (const Task &t : m_store->tasks())
            if (t.uid.startsWith("mail-") && (includeDeleted || !t.deleted))
                out << t;
        std::sort(out.begin(), out.end(), [](const Task &a, const Task &b) { return a.id < b.id; });
        return out;
    }
    QStringList titles() const {
        QStringList out;
        for (const Task &t : mailTasks())
            out << t.title;
        return out;
    }

    // Points the connector at the fake server as an "Other" account with the
    // given filter and after-action; does not turn it on.
    void configure(const QString &toFilter = QString(), const QString &after = "leave", const QString &labelFilter = QString()) {
        m_fetch->setAccount("other");
        m_fetch->setOption("mail.host", "127.0.0.1");
        m_fetch->setOption("mail.port", m_server->port());
        m_fetch->setOption("mail.security", "none");
        m_fetch->setOption("mail.user", "jane");
        m_fetch->setPassword("app-password");
        m_fetch->setOption("mail.toFilter", toFilter);
        m_fetch->setOption("mail.labelFilter", labelFilter);
        m_fetch->setOption("mail.after", after);
        m_fetch->setOption("mail.listId", 0);   // The Inbox, the default.
    }
    void enable() { m_fetch->setOption("mail.enabled", true); }
    void waitIdle() {
        QTRY_VERIFY2_WITH_TIMEOUT(m_fetch->state() == MailFetch::State::Idle,
                                  qPrintable("state " + m_fetch->stateName() + ": " + m_fetch->lastError()), 8000);
    }
    int searchesLogged() const { return m_server->log.filter(QRegularExpression("^UID SEARCH")).size(); }
    QString lastSearch() const {
        const QStringList all = m_server->log.filter(QRegularExpression("^UID SEARCH"));
        return all.isEmpty() ? QString() : all.last();
    }
    int fetchesOf(qint64 uid) const { return m_server->log.filter("UID FETCH " + QString::number(uid) + " (BODY.PEEK[])").size(); }

    static QByteArray plus(const QString &subject, const QString &id = QString(), const QString &to = "jane+precrime@gmail.com") {
        return FakeImap::mail(to, subject, "Body of " + subject, id);
    }

    // Helpers for the OAuth tests.
    // Whether the database file (or its write-ahead log) holds this text anywhere.
    bool databaseHolds(const QByteArray &needle) const {
        for (const QString &suffix : {QString(), QString("-wal")}) {
            QFile f(m_store->path() + suffix);
            if (f.open(QIODevice::ReadOnly) && f.readAll().contains(needle))
                return true;
        }
        return false;
    }

    QString writeClientJson(const QString &body) {
        const QString path = m_dir.filePath("client_secret.json");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString();
        f.write(body.toUtf8());
        return path;
    }
    static QString googleClientJson() {
        return QStringLiteral("{\"installed\":{\"client_id\":\"test-id.apps.example\",\"project_id\":\"p\","
                              "\"auth_uri\":\"https://accounts.google.com/o/oauth2/auth\","
                              "\"token_uri\":\"https://oauth2.googleapis.com/token\",\"client_secret\":\"test-secret\","
                              "\"redirect_uris\":[\"http://localhost\"]}}");
    }

    struct Http {
        QList<QUrl> urls;
        QList<QUrlQuery> forms;
        int status = 200;
        QJsonObject reply{{"access_token", "at-1"}, {"refresh_token", "rt-1"}, {"expires_in", 3600}};
        QList<QJsonObject> replies;   // Used in order before `reply`.
    };
    void fakeHttp(Http &http) {
        m_fetch->setHttpTransport([&http](const QUrl &url, const QUrlQuery &form, std::function<void(int, const QJsonObject &)> done) {
            http.urls << url;
            http.forms << form;
            const QJsonObject r = http.replies.isEmpty() ? http.reply : http.replies.takeFirst();
            done(http.status, r);
        });
    }
    QString param(const QString &name) const { return QUrlQuery(m_opened).queryItemValue(name, QUrl::FullyDecoded); }


private slots:
    void opened(const QUrl &url) { m_opened = url; }

    void init() {
        m_store = std::make_unique<Store>();
        QVERIFY(m_store->open(m_dir.filePath(QString("t%1.db").arg(QRandomGenerator::global()->generate()))));
        m_inbox = addList("Inbox", ListKind::Inbox);
        m_work = addList("Work");
        m_server = std::make_unique<FakeImap>();
        m_fetch = std::make_unique<MailFetch>(m_store.get());
        for (const char *account : {"mail", "mail-client", "mail-oauth"})
            Keyring::clear("precrime", account);
        MailFetch::Timings t;
        t.imapTimeout = 1500;
        t.backoffBase = 80;
        m_fetch->setTimings(t);
        m_opened = QUrl();
        QDesktopServices::setUrlHandler("https", this, "opened");
    }
    void cleanup() {
        QDesktopServices::unsetUrlHandler("https");
        m_fetch.reset();
        m_server.reset();
        m_store.reset();
    }

    // ------------------------------------------------------------ import

    void importsMatchingMailAndLeavesTheRest() {
        m_server->add("INBOX", plus("Buy milk"));
        m_server->add("INBOX", FakeImap::mail("bob@example.com", "Lunch?"));
        m_server->add("INBOX", plus("Call Alex"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(titles(), (QStringList{"Buy milk", "Call Alex"}));
        QCOMPARE(mailTasks().value(0).listId, m_inbox);
        QCOMPARE(m_fetch->imported(), 2);
        QVERIFY(m_fetch->lastCheck().isValid());
        QVERIFY2(m_fetch->statusLine().startsWith("Connected · checked "), qPrintable(m_fetch->statusLine()));
        QVERIFY2(m_fetch->statusLine().endsWith("· 2 imported"), qPrintable(m_fetch->statusLine()));
        // The mail that was not for Precrime is exactly as it was: never fetched, never flagged.
        QCOMPARE(fetchesOf(2), 0);
        QVERIFY(m_server->find("INBOX", 2)->flags.isEmpty());
        QCOMPARE(m_server->count("INBOX"), 3);
        // Fetching with PEEK: nothing was marked read by the fetch itself.
        QVERIFY(!m_server->hasFlag("INBOX", 1, "\\Seen"));
    }

    void tasksGoToTheChosenList() {
        m_server->add("INBOX", plus("Draft the memo"));
        configure("jane+precrime@gmail.com");
        m_fetch->setOption("mail.listId", m_work);
        enable();
        waitIdle();
        QCOMPARE(mailTasks().size(), 1);
        QCOMPARE(mailTasks().value(0).listId, m_work);
    }

    void naturalLanguageSwitch() {
        m_server->add("INBOX", plus("Call Sam tomorrow"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(mailTasks().value(0).title, QString("Call Sam tomorrow"));
        QVERIFY(!mailTasks().value(0).dueDate.isValid());
    }

    void naturalLanguageOn() {
        m_server->add("INBOX", plus("Call Sam tomorrow"));
        configure("jane+precrime@gmail.com");
        m_fetch->setOption("mail.naturalLanguage", true);
        enable();
        waitIdle();
        QCOMPARE(mailTasks().value(0).title, QString("Call Sam"));
        QCOMPARE(mailTasks().value(0).dueDate, QDate::currentDate().addDays(1));
    }

    void filingInstructionsAlwaysWork() {
        m_server->add("INBOX", plus("Pay rent due(2026-12-01) priority(!!!) list(Work)"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        const Task t = mailTasks().value(0);
        QCOMPARE(t.title, QString("Pay rent"));
        QCOMPARE(t.dueDate, QDate(2026, 12, 1));
        QCOMPARE(t.listId, m_work);
    }

    // ----------------------------------------------------------- filters

    // "jane+precrime@gmail.com" is a substring of "jane+precrime@gmail.com.au", so the
    // server's SEARCH finds both; the headers are compared whole before importing.
    void toFilterRecheckRejectsASubstringMatch() {
        m_server->add("INBOX", plus("Not mine", "au", "jane+precrime@gmail.com.au"));
        m_server->add("INBOX", plus("Mine", "mine"));
        m_server->add("INBOX", plus("Not mine either", "pre", "xjane+precrime@gmail.com"));
        configure("jane+precrime@gmail.com", "move");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Mine"});
        QVERIFY2(lastSearch().contains("TO \"jane+precrime@gmail.com\""), qPrintable(lastSearch()));
        QCOMPARE(fetchesOf(1), 1);   // It was fetched (the server said it matched), then turned down.
        // And it is left exactly where it was: not moved, not flagged.
        QVERIFY(m_server->find("INBOX", 1));
        QVERIFY(m_server->find("INBOX", 3));
        QVERIFY(m_server->find("INBOX", 1)->flags.isEmpty());
        QCOMPARE(m_server->count("Precrime"), 1);
        // A turned-down message is not fetched again on the next look.
        m_fetch->fetchNow();
        QTRY_COMPARE_WITH_TIMEOUT(searchesLogged(), 2, 5000);
        waitIdle();
        QCOMPARE(fetchesOf(1), 1);
        QCOMPARE(fetchesOf(3), 1);
    }

    void toFilterIsCaseInsensitiveOnTheBareAddress() {
        m_server->add("INBOX", plus("Shouting", "u", "Jane Doe <JANE+PRECRIME@Gmail.COM>"));
        configure("Jane+Precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Shouting"});
    }

    // An Outlook rule that redirects keeps the original To; the plus-address is in Delivered-To.
    void deliveredToAndXOriginalToCount() {
        m_server->add("INBOX", FakeImap::mail("someone@else.org", "Redirected", "b", "r1", "Delivered-To: jane+precrime@gmail.com\r\n"));
        m_server->add("INBOX", FakeImap::mail("someone@else.org", "Aliased", "b", "r2", "X-Original-To: jane+precrime@gmail.com\r\n"));
        m_server->add("INBOX", FakeImap::mail("someone@else.org", "Copied", "b", "r3", "Cc: Jane <jane+precrime@gmail.com>\r\n"));
        m_server->add("INBOX", FakeImap::mail("someone@else.org", "Unrelated", "b", "r4"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(titles(), (QStringList{"Redirected", "Aliased", "Copied"}));
        QVERIFY(lastSearch().contains("HEADER Delivered-To"));
        QVERIFY(lastSearch().contains("HEADER X-Original-To"));
    }

    // Each hop adds a Delivered-To on top, so a forwarded message names the
    // plus-address in a later one.
    void everyDeliveredToHeaderCounts() {
        m_server->add("INBOX", FakeImap::mail("someone@else.org", "Forwarded", "b", "f1",
                                              "Delivered-To: jane@gmail.com\r\nDelivered-To: jane+precrime@gmail.com\r\n"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Forwarded"});
    }

    void labelFilterUsesAKeywordOnOtherServers() {
        m_server->add("INBOX", plus("Tagged", "k1"), {}, {"precrime"});
        m_server->add("INBOX", plus("Plain", "k2"));
        configure(QString(), "leave", "precrime");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Tagged"});
        QVERIFY2(lastSearch().contains("KEYWORD precrime"), qPrintable(lastSearch()));
        QVERIFY(!lastSearch().contains("X-GM-RAW"));
    }

    void labelFilterUsesXGmRawOnGmail() {
        m_server->gmail = true;
        m_server->add("INBOX", plus("Labelled", "g1"), {}, {"precrime"});
        m_server->add("INBOX", plus("Unlabelled", "g2"));
        configure(QString(), "leave", "precrime");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Labelled"});
        QVERIFY2(lastSearch().endsWith("X-GM-RAW \"label:precrime\""), qPrintable(lastSearch()));
        QVERIFY(!lastSearch().contains("KEYWORD"));
    }

    // X-GM-RAW is only for servers that say X-GM-EXT-1.
    void xGmRawOnlyWhenAdvertised() {
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QVERIFY(!m_server->log.filter("X-GM-RAW").size());
        QCOMPARE(lastSearch(), QString("UID SEARCH NOT DELETED OR OR OR TO \"jane+precrime@gmail.com\" CC \"jane+precrime@gmail.com\" "
                                       "HEADER Delivered-To \"jane+precrime@gmail.com\" HEADER X-Original-To \"jane+precrime@gmail.com\""));

        m_fetch->setOption("mail.enabled", false);
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Off, 3000);
        m_server->log.clear();
        m_store->setSetting("mail.mark.INBOX", "");   // So the second search is not narrowed to newer mail.
        m_server->gmail = true;
        enable();
        waitIdle();
        QCOMPARE(lastSearch(), QString("UID SEARCH NOT DELETED X-GM-RAW \"deliveredto:jane+precrime@gmail.com OR to:jane+precrime@gmail.com\""));
        QCOMPARE(m_server->log.filter("TO \"").size(), 0);
    }

    void gmailToFilterAndLabelTogether() {
        m_server->gmail = true;
        m_server->add("INBOX", plus("Both", "b1"), {}, {"precrime"});
        m_server->add("INBOX", plus("Only address", "b2"));
        m_server->add("INBOX", FakeImap::mail("x@y.org", "Only label", "b", "b3"), {}, {"precrime"});
        configure("jane+precrime@gmail.com", "leave", "precrime");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Both"});
        QVERIFY2(lastSearch().endsWith("X-GM-RAW \"(deliveredto:jane+precrime@gmail.com OR to:jane+precrime@gmail.com) label:precrime\""),
                 qPrintable(lastSearch()));
    }

    void onlyTheChosenFolderIsConsidered() {
        m_server->add("INBOX", plus("In the inbox"));
        m_server->folders["Orders"];
        m_server->add("Orders", plus("In orders"));
        configure("jane+precrime@gmail.com");
        m_fetch->setOption("mail.folder", "Orders");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"In orders"});
        QVERIFY(m_server->log.contains("SELECT \"Orders\""));
        QVERIFY(!m_server->log.contains("SELECT \"INBOX\""));
    }

    void aMissingFolderIsAnError() {
        configure("jane+precrime@gmail.com");
        m_fetch->setOption("mail.folder", "Nowhere");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 8000);
        QVERIFY2(m_fetch->lastError().contains("Nowhere"), qPrintable(m_fetch->lastError()));
        QCOMPARE(m_server->connections, 1);   // A wrong folder name does not fix itself: no retry loop.
        QTest::qWait(400);
        QCOMPARE(m_server->connections, 1);
    }

    void unfilteredWarningFlag() {
        configure(QString(), "leave", QString());
        QVERIFY(m_fetch->unfiltered());
        m_fetch->setOption("mail.toFilter", "a@b.org");
        QVERIFY(!m_fetch->unfiltered());
        m_fetch->setOption("mail.toFilter", "");
        m_fetch->setOption("mail.labelFilter", "x");
        QVERIFY(!m_fetch->unfiltered());
    }

    // ------------------------------------------------------- after-actions

    void afterMoveCreatesTheFolderOnceAndMoves() {
        m_server->add("INBOX", plus("One", "a1"));
        m_server->add("INBOX", plus("Two", "a2"));
        m_server->add("INBOX", FakeImap::mail("bob@example.com", "Not mine", "b", "a3"));
        configure("jane+precrime@gmail.com", "move");
        enable();
        waitIdle();
        QCOMPARE(titles(), (QStringList{"One", "Two"}));
        QVERIFY(m_server->folders.contains("Precrime"));
        QCOMPARE(m_server->count("Precrime"), 2);
        QCOMPARE(m_server->count("INBOX"), 1);
        QCOMPARE(m_server->log.filter("CREATE \"Precrime\"").size(), 1);
        QCOMPARE(m_server->log.filter(QRegularExpression("^UID MOVE")).size(), 2);
        QVERIFY(m_server->find("INBOX", 3));   // The other person's mail stayed.
    }

    void afterMoveToAnExistingFolderIsNotAnError() {
        m_server->folders["Precrime"];
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com", "move");
        enable();
        waitIdle();
        QCOMPARE(m_server->count("Precrime"), 1);
        QVERIFY(m_fetch->lastError().isEmpty());
    }

    void afterMoveUsesTheNameFromTheSettings() {
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com", "move");
        m_fetch->setOption("mail.moveTo", "Tasks/Mail");
        enable();
        waitIdle();
        QCOMPARE(m_server->count("Tasks/Mail"), 1);
    }

    void afterMoveWithoutMoveCapabilityFallsBack() {
        m_server->capabilities = {"IMAP4rev1", "IDLE", "UIDPLUS"};
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com", "move");
        enable();
        waitIdle();
        QCOMPARE(m_server->count("Precrime"), 1);
        QCOMPARE(m_server->count("INBOX"), 0);
        QVERIFY(m_server->log.contains("UID EXPUNGE 1"));
    }

    void afterReadMarksSeenAndSearchesUnseen() {
        m_server->add("INBOX", plus("Fresh", "r1"));
        m_server->add("INBOX", plus("Already read", "r2"), {"\\Seen"});
        configure("jane+precrime@gmail.com", "read");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Fresh"});
        QVERIFY(lastSearch().contains("UNSEEN"));
        QVERIFY(m_server->hasFlag("INBOX", 1, "\\Seen"));
        QCOMPARE(m_server->count("INBOX"), 2);   // Read, not moved.
        QCOMPARE(m_server->count("Precrime"), 0);
        QVERIFY(!m_server->folders.contains("Precrime"));
    }

    void afterLeaveTouchesNothingOnTheServer() {
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com", "leave");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"One"});
        QVERIFY(m_server->find("INBOX", 1)->flags.isEmpty());
        QCOMPARE(m_server->count("INBOX"), 1);
        QVERIFY(m_server->log.filter("STORE").isEmpty());
        QVERIFY(m_server->log.filter("MOVE").isEmpty());
        QVERIFY(m_server->log.filter("CREATE").isEmpty());
        QVERIFY(!lastSearch().contains("UNSEEN"));
    }

    // A move the server refuses leaves the mail where it was and says so; the task already exists, so the
    // next look finds a duplicate and tries the move again.
    void aFailedMoveIsReportedAndRetriedByTheNextLook() {
        m_server->refuse << "UID MOVE";
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com", "move");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 8000);
        QVERIFY2(m_fetch->lastError().contains("Couldn't move"), qPrintable(m_fetch->lastError()));
        QCOMPARE(titles(), QStringList{"One"});
        QCOMPARE(m_server->count("INBOX"), 1);
        m_server->refuse.clear();
        m_fetch->fetchNow();
        waitIdle();
        QCOMPARE(titles(), QStringList{"One"});     // Still one task,
        QCOMPARE(m_server->count("INBOX"), 0);      // and the mail is out of the inbox now.
        QCOMPARE(m_server->count("Precrime"), 1);
    }

    // ------------------------------------------------- duplicates and marks

    void duplicatesAreSkippedButStillMoved() {
        // The same message was imported before (say, on another machine, and arrived through git sync).
        MailTask::Options options;
        options.listId = m_inbox;
        QVERIFY(MailTask::import(*m_store, MailTask::parse(plus("Existing", "same-id")), options).ok());
        QCOMPARE(mailTasks().size(), 1);
        m_server->add("INBOX", plus("Existing", "same-id"));
        configure("jane+precrime@gmail.com", "move");
        enable();
        waitIdle();
        QCOMPARE(mailTasks(true).size(), 1);   // No second task.
        QCOMPARE(m_fetch->imported(), 0);
        QCOMPARE(m_server->count("INBOX"), 0);   // The duplicate got the same after-action.
        QCOMPARE(m_server->count("Precrime"), 1);
    }

    void aDeletedTaskIsStillADuplicate() {
        MailTask::Options options;
        options.listId = m_inbox;
        QVERIFY(MailTask::import(*m_store, MailTask::parse(plus("Gone", "gone-id")), options).ok());
        Task t = mailTasks().value(0);
        t.deleted = true;
        m_store->begin("delete");
        m_store->putTask(t);
        m_store->commit();
        m_server->add("INBOX", plus("Gone", "gone-id"));
        configure("jane+precrime@gmail.com", "read");
        enable();
        waitIdle();
        QCOMPARE(mailTasks().size(), 0);           // Not brought back to life,
        QCOMPARE(mailTasks(true).size(), 1);       // and not created again.
        QVERIFY(m_server->hasFlag("INBOX", 1, "\\Seen"));
    }

    void highWaterMarkKeepsOldMailFromBeingFetchedAgain() {
        m_server->add("INBOX", plus("One", "h1"));
        m_server->add("INBOX", plus("Two", "h2"));
        configure("jane+precrime@gmail.com", "leave");
        enable();
        waitIdle();
        QCOMPARE(m_store->setting("mail.mark.INBOX").toString(), QString("1:2"));   // UIDVALIDITY:last uid.
        QCOMPARE(fetchesOf(1), 1);
        QCOMPARE(fetchesOf(2), 1);

        m_fetch->fetchNow();
        QTRY_COMPARE_WITH_TIMEOUT(searchesLogged(), 2, 5000);
        waitIdle();
        QVERIFY2(lastSearch().contains("UID 3:*"), qPrintable(lastSearch()));
        QCOMPARE(fetchesOf(1), 1);   // Not fetched a second time.
        QCOMPARE(fetchesOf(2), 1);

        m_server->add("INBOX", plus("Three", "h3"));
        m_fetch->fetchNow();
        QTRY_COMPARE_WITH_TIMEOUT(titles(), (QStringList{"One", "Two", "Three"}), 5000);
        QCOMPARE(m_store->setting("mail.mark.INBOX").toString(), QString("1:3"));
        QCOMPARE(fetchesOf(1), 1);
    }

    // A server that renumbers the folder (new UIDVALIDITY) invalidates the mark; the uid check on tasks
    // then keeps everything from being imported twice.
    void aNewUidValidityResetsTheMark() {
        m_server->add("INBOX", plus("One", "v1"));
        configure("jane+precrime@gmail.com", "leave");
        enable();
        waitIdle();
        QCOMPARE(m_store->setting("mail.mark.INBOX").toString(), QString("1:1"));
        m_fetch->setOption("mail.enabled", false);
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Off, 3000);
        m_server->folders["INBOX"].validity = 9;
        enable();
        waitIdle();
        QCOMPARE(fetchesOf(1), 2);   // Looked at again,
        QCOMPARE(mailTasks().size(), 1);   // but it is the same message.
        QCOMPARE(m_store->setting("mail.mark.INBOX").toString(), QString("9:1"));
    }

    void highWaterMarkIsPerFolder() {
        m_server->add("INBOX", plus("One", "p1"));
        m_server->folders["Other"];
        m_server->add("Other", plus("Two", "p2"));
        configure("jane+precrime@gmail.com", "leave");
        enable();
        waitIdle();
        QCOMPARE(m_store->setting("mail.mark.INBOX").toString(), QString("1:1"));
        QVERIFY(!m_store->setting("mail.mark.Other").isValid());
    }

    void markIsNotKeptWhenMessagesAreMovedOrRead() {
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com", "read");
        enable();
        waitIdle();
        QVERIFY(!m_store->setting("mail.mark.INBOX").isValid());
    }

    // ------------------------------------------------------------- limits

    void hugeMessagesAreSkippedAndReported() {
        m_server->add("INBOX", plus("Small", "s1"));
        const qint64 big = m_server->add("INBOX", plus("Huge", "s2"));
        m_server->setSize("INBOX", big, 30 * 1024 * 1024);
        m_server->add("INBOX", plus("Also small", "s3"));
        configure("jane+precrime@gmail.com", "move");
        enable();
        waitIdle();
        QCOMPARE(titles(), (QStringList{"Small", "Also small"}));
        QCOMPARE(fetchesOf(big), 0);               // Never downloaded.
        QVERIFY(m_server->find("INBOX", big));      // Not moved either.
        QCOMPARE(m_fetch->skipped(), 1);
        QVERIFY2(m_fetch->statusLine().contains("1 over 25 MB skipped"), qPrintable(m_fetch->statusLine()));
        m_fetch->fetchNow();
        QTRY_COMPARE_WITH_TIMEOUT(searchesLogged(), 2, 5000);
        waitIdle();
        QCOMPARE(m_fetch->skipped(), 1);           // Not counted again.
    }

    void exactlyTheLimitIsStillImported() {
        const qint64 uid = m_server->add("INBOX", plus("Borderline"));
        m_server->setSize("INBOX", uid, MailFetch::MaxMessageSize);
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"Borderline"});
        QCOMPARE(m_fetch->skipped(), 0);
    }

    void aLargeMailboxIsFetchedInBatches() {
        for (int i = 0; i < 130; ++i)
            m_server->add("INBOX", plus(QString("Task %1").arg(i), QString("bulk%1").arg(i)));
        configure("jane+precrime@gmail.com");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(mailTasks().size(), 130, 20000);
        waitIdle();
        QVERIFY(m_server->log.filter(QRegularExpression("^UID FETCH [0-9,]+ \\(RFC822.SIZE\\)")).size() >= 2);
    }

    // ------------------------------------------------------- scheduling

    void idleBringsNewMailAtOnce() {
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QTRY_COMPARE_WITH_TIMEOUT(m_server->idlers, 1, 5000);
        QCOMPARE(m_fetch->state(), MailFetch::State::Idle);
        m_server->add("INBOX", plus("Arrived", "i1"));
        m_server->announce();
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"Arrived"}, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(m_server->idlers, 1, 5000);   // Back to idling afterwards, on the same connection.
        QCOMPARE(m_server->connections, 1);
    }

    void withoutIdleItPollsAndHangsUpBetweenChecks() {
        m_server->capabilities = {"IMAP4rev1", "MOVE"};
        MailFetch::Timings t;
        t.imapTimeout = 1500;
        t.backoffBase = 80;
        t.pollOverride = 150;
        m_fetch->setTimings(t);
        m_server->add("INBOX", plus("First", "q1"));
        configure("jane+precrime@gmail.com");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"First"}, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(m_server->log.contains("LOGOUT"), 5000);
        QCOMPARE(m_server->idlers, 0);
        m_server->add("INBOX", plus("Second", "q2"));
        QTRY_COMPARE_WITH_TIMEOUT(titles(), (QStringList{"First", "Second"}), 5000);
        QVERIFY(m_server->connections >= 2);
    }

    void pollIntervalComesFromTheSetting() {
        m_server->capabilities = {"IMAP4rev1"};
        configure("jane+precrime@gmail.com");
        m_fetch->setOption("mail.interval", 7);
        QCOMPARE(m_fetch->option("mail.interval"), QString("7"));
        enable();
        waitIdle();
        // No sleeping through seven minutes: the connection is closed and the wait is armed.
        QTRY_VERIFY_WITH_TIMEOUT(m_server->log.contains("LOGOUT"), 5000);
    }

    // IDLE is re-issued on a timer (every 25 minutes in earnest) so the server never times it out, and
    // each renewal looks at the mailbox, so mail whose announcement was lost still arrives.
    void idleIsReissuedPeriodically() {
        MailFetch::Timings t;
        t.imapTimeout = 1500;
        t.backoffBase = 80;
        t.idleRenew = 150;
        m_fetch->setTimings(t);
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QTRY_VERIFY_WITH_TIMEOUT(m_server->log.filter(QRegularExpression("^IDLE$")).size() >= 3, 8000);
        m_server->add("INBOX", plus("Announcement lost", "ri1"));   // No EXISTS sent.
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"Announcement lost"}, 8000);
        QCOMPARE(m_server->connections, 1);   // The same connection throughout.
    }

    // STARTTLS is for a server that offers it; one that does not is said so, not talked to in the clear.
    void starttlsOnAServerWithoutItIsAnError() {
        configure("jane+precrime@gmail.com");
        m_fetch->setOption("mail.security", "starttls");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 8000);
        QVERIFY2(m_fetch->lastError().contains("STARTTLS"), qPrintable(m_fetch->lastError()));
        QCOMPARE(m_server->mechanisms.size(), 0);   // The password was never sent.
    }

    void fetchNowWhileIdlingLooksAtOnce() {
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QTRY_COMPARE_WITH_TIMEOUT(m_server->idlers, 1, 5000);
        m_server->add("INBOX", plus("Quiet arrival", "n1"));   // No EXISTS announcement.
        m_fetch->fetchNow();
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"Quiet arrival"}, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(m_server->idlers, 1, 5000);
        QCOMPARE(m_server->connections, 1);
    }

    void syncNowIsFetchNow() {
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        m_server->add("INBOX", plus("Via sync", "sn"));
        QMetaObject::invokeMethod(m_fetch.get(), "syncNow");   // What SyncHub calls.
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"Via sync"}, 5000);
    }

    void fetchNowWhenOffDoesNothing() {
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com");
        m_fetch->fetchNow();
        QTest::qWait(200);
        QCOMPARE(m_server->connections, 0);
        QCOMPARE(m_fetch->state(), MailFetch::State::Off);
        QVERIFY(mailTasks().isEmpty());
    }

    void startConnectsOnlyWhenEnabled() {
        configure("jane+precrime@gmail.com");
        m_fetch->start();
        QTest::qWait(150);
        QCOMPARE(m_server->connections, 0);
        m_store->setSetting("mail.enabled", true);
        m_fetch->start();
        waitIdle();
        QCOMPARE(m_server->connections, 1);
    }

    void turningItOffDisconnects() {
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QTRY_COMPARE_WITH_TIMEOUT(m_server->idlers, 1, 5000);
        m_fetch->setOption("mail.enabled", false);
        QCOMPARE(m_fetch->state(), MailFetch::State::Off);
        QCOMPARE(m_fetch->statusLine(), QString("Off"));
        QTRY_COMPARE_WITH_TIMEOUT(m_server->idlers, 0, 5000);
        m_server->add("INBOX", plus("Late"));
        m_server->announce();
        QTest::qWait(200);
        QVERIFY(mailTasks().isEmpty());
    }

    void changingASettingWhileOnReconnectsWithIt() {
        m_server->add("INBOX", plus("One", "c1"));
        m_server->folders["Other"];
        m_server->add("Other", plus("Two", "c2"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(titles(), QStringList{"One"});
        m_fetch->setOption("mail.folder", "Other");
        QTRY_COMPARE_WITH_TIMEOUT(titles(), (QStringList{"One", "Two"}), 8000);
    }

    // ----------------------------------------------------- errors, retries

    void aRefusedPasswordIsAnErrorWithoutRetries() {
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com");
        m_fetch->setPassword("wrong");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 8000);
        QVERIFY2(m_fetch->lastError().contains("refused the sign-in"), qPrintable(m_fetch->lastError()));
        QVERIFY2(m_fetch->lastError().contains("Invalid credentials"), qPrintable(m_fetch->lastError()));
        QVERIFY(m_fetch->statusLine().contains("Invalid credentials"));
        QTest::qWait(500);   // Retrying a wrong password would lock the account.
        QCOMPARE(m_server->connections, 1);
        QVERIFY(mailTasks().isEmpty());
        // Fixing the password makes it work, with no restart.
        m_fetch->setPassword("app-password");
        waitIdle();
        QCOMPARE(titles(), QStringList{"One"});
    }

    void noPasswordSavedIsSaidPlainly() {
        configure("jane+precrime@gmail.com");
        m_fetch->setPassword("");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 8000);
        QVERIFY2(m_fetch->lastError().contains("password"), qPrintable(m_fetch->lastError()));
        QCOMPARE(m_server->mechanisms.size(), 0);
    }

    void noServerOrUserIsSaidPlainly() {
        m_fetch->setAccount("other");
        enable();
        QCOMPARE(m_fetch->state(), MailFetch::State::Error);
        QVERIFY2(m_fetch->lastError().contains("server"), qPrintable(m_fetch->lastError()));
    }

    void connectionRefusedRetriesWithBackoff() {
        configure("jane+precrime@gmail.com");
        m_fetch->setOption("mail.port", 1);   // Nobody listens there.
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 8000);
        QVERIFY2(m_fetch->lastError().contains("127.0.0.1"), qPrintable(m_fetch->lastError()));
        // The service comes up; the next retry finds it.
        m_server->add("INBOX", plus("Eventually"));
        m_store->setSetting("mail.port", m_server->port());
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"Eventually"}, 8000);
        QCOMPARE(m_fetch->state(), MailFetch::State::Idle);
    }

    void aDroppedConnectionReconnects() {
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QTRY_COMPARE_WITH_TIMEOUT(m_server->idlers, 1, 5000);
        m_server->dropConnections();
        m_server->add("INBOX", plus("After the outage", "d1"));
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"After the outage"}, 8000);
        QVERIFY(m_server->connections >= 2);
        waitIdle();
        QVERIFY(m_fetch->lastError().isEmpty());
    }

    void aSilentServerTimesOutAndRetries() {
        m_server->silentOn = "SEARCH";
        configure("jane+precrime@gmail.com");
        MailFetch::Timings t;
        t.imapTimeout = 250;
        t.backoffBase = 80;
        m_fetch->setTimings(t);
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 5000);
        QVERIFY2(m_fetch->lastError().contains("in time") || m_fetch->lastError().contains("lost"), qPrintable(m_fetch->lastError()));
        m_server->silentOn.clear();
        m_server->add("INBOX", plus("Back", "t1"));
        QTRY_COMPARE_WITH_TIMEOUT(titles(), QStringList{"Back"}, 8000);
    }

    void noInboxListMeansTheMailIsLeftAlone() {
        // With no list at all there is nowhere for a task to go.
        m_store->begin("remove lists");
        for (qint64 id : {m_inbox, m_work}) {
            TaskList l = m_store->list(id);
            l.deleted = true;
            m_store->putList(l);
        }
        m_store->commit();
        m_server->add("INBOX", plus("Nowhere to go"));
        configure("jane+precrime@gmail.com", "move");
        m_fetch->setOption("mail.listId", 0);
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 8000);
        QVERIFY2(m_fetch->lastError().contains("list"), qPrintable(m_fetch->lastError()));
        QCOMPARE(m_server->count("INBOX"), 1);   // Not moved: it has not become a task.
        QVERIFY(mailTasks().isEmpty());
    }

    void importsAreOneUndoStepEach() {
        m_server->add("INBOX", plus("One"));
        configure("jane+precrime@gmail.com");
        enable();
        waitIdle();
        QCOMPARE(m_store->undoLabel(), QString("Mail"));
        QVERIFY(m_store->undo());
        QVERIFY(mailTasks().isEmpty());
    }

    void garbageInTheMailboxDoesNotStopTheRest() {
        m_server->add("INBOX", "this is not a mail message at all");
        m_server->add("INBOX", plus("Real", "g9"));
        configure(QString(), "leave");
        enable();
        waitIdle();
        QVERIFY(titles().contains("Real"));
        QCOMPARE(m_fetch->state(), MailFetch::State::Idle);
    }

    // -------------------------------------------------- presets and options

    void plusAddressRules() {
        QCOMPARE(MailFetch::plusAddressOf("jane@gmail.com"), QString("jane+precrime@gmail.com"));
        QCOMPARE(MailFetch::plusAddressOf("a.b@example.co.uk"), QString("a.b+precrime@example.co.uk"));
        QCOMPARE(MailFetch::plusAddressOf("jane+news@gmail.com"), QString());   // Already a plus-address.
        QCOMPARE(MailFetch::plusAddressOf("nonsense"), QString());
        QCOMPARE(MailFetch::plusAddressOf(""), QString());
    }

    void gmailIsTheDefaultAccountWithItsPreset() {
        QCOMPARE(m_fetch->option("mail.account"), QString("gmail"));
        QCOMPARE(m_fetch->option("mail.host"), QString("imap.gmail.com"));
        QCOMPARE(m_fetch->option("mail.port"), QString("993"));
        QCOMPARE(m_fetch->option("mail.security"), QString("tls"));
        QCOMPARE(m_fetch->option("mail.folder"), QString("INBOX"));
        QCOMPARE(m_fetch->option("mail.after"), QString("move"));
        QCOMPARE(m_fetch->option("mail.moveTo"), QString("Precrime"));
        QCOMPARE(m_fetch->option("mail.interval"), QString("5"));
        QCOMPARE(m_fetch->option("mail.enabled"), QString("false"));
        QCOMPARE(m_fetch->option("mail.naturalLanguage"), QString("false"));
        QCOMPARE(m_fetch->option("mail.listId"), QString("0"));
    }

    void gmailPlusAddressFollowsTheAddressUntilEdited() {
        m_fetch->setAccount("gmail");   // Writes the preset, filter included, so the filter is a stored value that has to be moved.
        m_fetch->setOption("mail.address", "jane@gmail.com");
        QCOMPARE(m_fetch->option("mail.user"), QString("jane@gmail.com"));
        QCOMPARE(m_fetch->option("mail.toFilter"), QString("jane+precrime@gmail.com"));
        QCOMPARE(m_fetch->plusAddress(), QString("jane+precrime@gmail.com"));
        m_fetch->setOption("mail.address", "jane.doe@gmail.com");
        QCOMPARE(m_fetch->option("mail.toFilter"), QString("jane.doe+precrime@gmail.com"));
        QCOMPARE(m_fetch->option("mail.user"), QString("jane.doe@gmail.com"));
        // The user takes the filter over: it stays theirs.
        m_fetch->setOption("mail.toFilter", "tasks@example.org");
        m_fetch->setOption("mail.address", "other@gmail.com");
        QCOMPARE(m_fetch->option("mail.toFilter"), QString("tasks@example.org"));
        QCOMPARE(m_fetch->plusAddress(), QString("other+precrime@gmail.com"));   // The guidance still follows the address.
    }

    void emptyingTheGmailFilterIsRespected() {
        m_fetch->setOption("mail.address", "jane@gmail.com");
        m_fetch->setOption("mail.toFilter", "");
        QCOMPARE(m_fetch->option("mail.toFilter"), QString());
        QVERIFY(m_fetch->unfiltered());
    }

    void switchingAccountsFillsInPresets() {
        m_fetch->setOption("mail.address", "jane@example.org");
        m_fetch->setAccount("outlook");
        QCOMPARE(m_fetch->option("mail.account"), QString("outlook"));
        QCOMPARE(m_fetch->option("mail.host"), QString("outlook.office365.com"));
        QCOMPARE(m_fetch->option("mail.port"), QString("993"));
        QCOMPARE(m_fetch->option("mail.security"), QString("tls"));
        QCOMPARE(m_fetch->option("mail.user"), QString("jane@example.org"));
        QCOMPARE(m_fetch->option("mail.toFilter"), QString());   // Gmail's plus-address filter does not follow.
        QCOMPARE(m_fetch->option("mail.after"), QString("leave"));

        m_fetch->setAccount("other");
        m_fetch->setOption("mail.host", "mail.example.org");
        m_fetch->setOption("mail.port", 143);
        m_fetch->setOption("mail.security", "starttls");
        QCOMPARE(m_fetch->option("mail.host"), QString("mail.example.org"));   // Everything editable.

        m_fetch->setAccount("gmail");
        QCOMPARE(m_fetch->option("mail.host"), QString("imap.gmail.com"));
        QCOMPARE(m_fetch->option("mail.port"), QString("993"));
        QCOMPARE(m_fetch->option("mail.security"), QString("tls"));
        QCOMPARE(m_fetch->option("mail.toFilter"), QString("jane+precrime@example.org"));
        QCOMPARE(m_fetch->option("mail.after"), QString("move"));
    }

    void davMailPreset() {
        m_fetch->setAccount("outlook");
        m_fetch->useDavMail();
        QCOMPARE(m_fetch->option("mail.account"), QString("other"));
        QCOMPARE(m_fetch->option("mail.host"), QString("127.0.0.1"));
        QCOMPARE(m_fetch->option("mail.port"), QString("1143"));
        QCOMPARE(m_fetch->option("mail.security"), QString("none"));
    }

    void everyOptionKeyIsKnown() {
        for (const QString &key : MailFetch::optionKeys())
            QVERIFY2(key.startsWith("mail."), qPrintable(key));
        const QStringList keys = MailFetch::optionKeys();
        QCOMPARE(keys.size(), 18);
        QCOMPARE(QSet<QString>(keys.begin(), keys.end()).size(), 18);
    }

    void optionsPersistInTheStore() {
        m_fetch->setOption("mail.host", "h.example.org");
        MailFetch again(m_store.get());
        QCOMPARE(again.option("mail.host"), QString("h.example.org"));
        QCOMPARE(m_store->setting("mail.host").toString(), QString("h.example.org"));
    }

    void revisionChangesWithEveryOption() {
        const int before = m_fetch->revision();
        m_fetch->setOption("mail.folder", "X");
        QVERIFY(m_fetch->revision() > before);
    }

    // ---------------------------------------------------------- the password

    void passwordGoesToTheKeyringNotTheDatabase() {
        QVERIFY(!m_fetch->hasPassword());
        m_fetch->setPassword("s3cret!");
        QVERIFY(m_fetch->hasPassword());
        QCOMPARE(Keyring::lookup("precrime", "mail"), QString("s3cret!"));
        for (const QString &key : MailFetch::optionKeys())
            QVERIFY2(!m_fetch->option(key).contains("s3cret"), qPrintable(key));
        m_fetch->setPassword("");
        QVERIFY(!m_fetch->hasPassword());
        QVERIFY(Keyring::lookup("precrime", "mail").isEmpty());
    }

    // --------------------------------------------------------------- OAuth

    void importClientJsonStoresTheClient() {
        QCOMPARE(m_fetch->importClientJson(writeClientJson(googleClientJson())), QString());
        QVERIFY(m_fetch->hasGoogleClient());
        QCOMPARE(m_fetch->option("mail.googleClientId"), QString("test-id.apps.example"));
        QCOMPARE(Keyring::lookup("precrime", "mail-client"), QString("test-secret"));   // The secret is in the keyring only.
        QVERIFY(databaseHolds("mail.googleClientId"));   // (the control: the helper does see the database)
        QVERIFY(!databaseHolds("test-secret"));
    }

    void importClientJsonRejectsOtherFiles() {
        QVERIFY(!m_fetch->importClientJson(m_dir.filePath("missing.json")).isEmpty());
        QVERIFY(m_fetch->importClientJson(writeClientJson("not json")).contains("isn't a Google"));
        QVERIFY(m_fetch->importClientJson(writeClientJson("{\"web\":{\"client_id\":\"x\",\"client_secret\":\"y\"}}")).contains("Web client"));
        QVERIFY(m_fetch->importClientJson(writeClientJson("{\"installed\":{\"client_id\":\"x\"}}")).contains("client_secret"));
        QVERIFY(!m_fetch->hasGoogleClient());
        QVERIFY(Keyring::lookup("precrime", "mail-client").isEmpty());
    }

    void signInNeedsAClient() {
        m_fetch->signIn();
        QCOMPARE(m_fetch->oauthState(), QString("none"));
        QVERIFY2(m_fetch->oauthError().contains("Import your Google client"), qPrintable(m_fetch->oauthError()));
        QVERIFY(m_opened.isEmpty());
        m_fetch->setAccount("outlook");
        m_fetch->signIn();
        QVERIFY2(m_fetch->oauthError().contains("client) ID"), qPrintable(m_fetch->oauthError()));
        QVERIFY(m_opened.isEmpty());
        m_fetch->setAccount("other");
        m_fetch->signIn();
        QVERIFY(m_opened.isEmpty());
    }

    // The whole Gmail story with a client the user brought: import, sign in through the
    // browser, IMAP with XOAUTH2, refresh, sign out.
    void googleSignInThenXoauth2ThenSignOut() {
        Http http;
        fakeHttp(http);
        QCOMPARE(m_fetch->importClientJson(writeClientJson(googleClientJson())), QString());
        m_fetch->setOption("mail.address", "jane@example.org");
        QCOMPARE(m_fetch->oauthState(), QString("none"));
        m_fetch->signIn();
        QCOMPARE(m_fetch->oauthState(), QString("waiting"));
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        QCOMPARE(m_opened.host(), QString("accounts.google.com"));
        QCOMPARE(param("client_id"), QString("test-id.apps.example"));
        const QUrl redirect(param("redirect_uri"));
        QCOMPARE(redirect.host(), QString("127.0.0.1"));
        const QString page = httpGet(redirect.port(), "/?code=google-code&state=" + param("state"));
        QVERIFY2(page.contains("You can close this tab"), qPrintable(page));
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->oauthState(), QString("signedIn"), 3000);
        QCOMPARE(m_fetch->signedInAs(), QString("jane@example.org"));
        QVERIFY(m_fetch->oauthError().isEmpty());

        // The code went out with the verifier whose hash the browser was given, and the client secret.
        QCOMPARE(http.forms.size(), 1);
        QCOMPARE(http.urls[0], QUrl("https://oauth2.googleapis.com/token"));
        QCOMPARE(http.forms[0].queryItemValue("code"), QString("google-code"));
        QCOMPARE(http.forms[0].queryItemValue("client_secret"), QString("test-secret"));
        QCOMPARE(OAuth::challengeFor(http.forms[0].queryItemValue("code_verifier")), param("code_challenge"));
        QCOMPARE(Keyring::lookup("precrime", "mail-oauth"), QString("rt-1"));
        QCOMPARE(m_fetch->option("mail.oauth"), QString("google"));
        QVERIFY(!databaseHolds("rt-1"));   // Tokens are not in the database.
        QVERIFY(!databaseHolds("at-1"));

        // Mail over XOAUTH2, with no app password anywhere.
        QVERIFY(!m_fetch->hasPassword());
        m_server->user = "jane@example.org";
        m_server->accessToken = "at-1";
        m_server->add("INBOX", plus("Via oauth"));
        m_fetch->setOption("mail.host", "127.0.0.1");
        m_fetch->setOption("mail.port", m_server->port());
        m_fetch->setOption("mail.security", "none");
        m_fetch->setOption("mail.after", "leave");
        m_fetch->setOption("mail.toFilter", "jane+precrime@gmail.com");
        m_fetch->setOption("mail.user", "jane@example.org");
        enable();
        waitIdle();
        QCOMPARE(m_server->mechanisms, QStringList{"XOAUTH2"});
        QCOMPARE(titles(), QStringList{"Via oauth"});
        QCOMPARE(http.forms.size(), 1);   // The token was still fresh: no refresh yet.

        // Sign out clears the tokens and stops using them.
        m_fetch->signOut();
        QCOMPARE(m_fetch->oauthState(), QString("none"));
        QVERIFY(Keyring::lookup("precrime", "mail-oauth").isEmpty());
        QVERIFY(m_fetch->option("mail.oauth").isEmpty());
        QVERIFY(m_fetch->signedInAs().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 5000);   // No password either, and it says so.
        QVERIFY2(m_fetch->lastError().contains("app password"), qPrintable(m_fetch->lastError()));
    }

    // The access token is renewed from the refresh token when it is about to expire, and a rotated
    // refresh token replaces the old one in the keyring.
    void accessTokenIsRefreshedBeforeItExpires() {
        Http http;
        http.reply = QJsonObject{{"access_token", "at-1"}, {"refresh_token", "rt-1"}, {"expires_in", 5}};   // Expires within the 60 s margin.
        fakeHttp(http);
        m_fetch->importClientJson(writeClientJson(googleClientJson()));
        m_fetch->setOption("mail.address", "jane@example.org");
        m_fetch->signIn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        httpGet(QUrl(param("redirect_uri")).port(), "/?code=c&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->oauthState(), QString("signedIn"), 3000);
        http.reply = QJsonObject{{"access_token", "at-2"}, {"refresh_token", "rt-2"}, {"expires_in", 3600}};
        m_server->user = "jane@example.org";
        m_server->accessToken = "at-2";
        m_server->add("INBOX", plus("Needs a fresh token"));
        m_fetch->setOption("mail.host", "127.0.0.1");
        m_fetch->setOption("mail.port", m_server->port());
        m_fetch->setOption("mail.security", "none");
        m_fetch->setOption("mail.after", "leave");
        m_fetch->setOption("mail.toFilter", "jane+precrime@gmail.com");
        m_fetch->setOption("mail.user", "jane@example.org");
        enable();
        waitIdle();
        QCOMPARE(http.forms.size(), 2);
        QCOMPARE(http.forms[1].queryItemValue("grant_type"), QString("refresh_token"));
        QCOMPARE(http.forms[1].queryItemValue("refresh_token"), QString("rt-1"));
        QCOMPARE(http.forms[1].queryItemValue("client_secret"), QString("test-secret"));
        QCOMPARE(Keyring::lookup("precrime", "mail-oauth"), QString("rt-2"));
        QCOMPARE(m_server->mechanisms, QStringList{"XOAUTH2"});
        QCOMPARE(titles(), QStringList{"Needs a fresh token"});
    }

    void aRevokedRefreshTokenAsksForANewSignIn() {
        Http http;
        http.reply = QJsonObject{{"access_token", "at-1"}, {"refresh_token", "rt-1"}, {"expires_in", 5}};
        fakeHttp(http);
        m_fetch->importClientJson(writeClientJson(googleClientJson()));
        m_fetch->setOption("mail.address", "jane@example.org");
        m_fetch->signIn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        httpGet(QUrl(param("redirect_uri")).port(), "/?code=c&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->oauthState(), QString("signedIn"), 3000);
        http.status = 400;
        http.reply = QJsonObject{{"error", "invalid_grant"}, {"error_description", "Token has been expired or revoked."}};
        m_fetch->setOption("mail.host", "127.0.0.1");
        m_fetch->setOption("mail.port", m_server->port());
        m_fetch->setOption("mail.security", "none");
        enable();
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->state(), MailFetch::State::Error, 5000);
        QVERIFY2(m_fetch->lastError().contains("expired or revoked"), qPrintable(m_fetch->lastError()));
        QCOMPARE(m_server->connections, 1);   // It connected, and never offered the stale token.
        QCOMPARE(m_server->mechanisms.size(), 0);
        QTest::qWait(400);
        QCOMPARE(m_server->connections, 1);   // And no retry loop: the user has to sign in again.
    }

    void aForgedRedirectDoesNotSignIn() {
        Http http;
        fakeHttp(http);
        m_fetch->importClientJson(writeClientJson(googleClientJson()));
        m_fetch->signIn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        httpGet(QUrl(param("redirect_uri")).port(), "/?code=evil&state=forged");
        QTRY_VERIFY_WITH_TIMEOUT(!m_fetch->oauthError().isEmpty(), 3000);
        QVERIFY2(m_fetch->oauthError().contains("state"), qPrintable(m_fetch->oauthError()));
        QCOMPARE(m_fetch->oauthState(), QString("none"));
        QVERIFY(http.forms.isEmpty());
        QVERIFY(Keyring::lookup("precrime", "mail-oauth").isEmpty());
    }

    void cancellingASignInStopsListening() {
        m_fetch->importClientJson(writeClientJson(googleClientJson()));
        m_fetch->signIn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        const quint16 port = QUrl(param("redirect_uri")).port();
        QCOMPARE(m_fetch->oauthState(), QString("waiting"));
        m_fetch->cancelSignIn();
        QCOMPARE(m_fetch->oauthState(), QString("none"));
        QCOMPARE(httpGet(port, "/?code=c&state=" + param("state")), QString("no connection"));
    }

    void switchingAccountWhileWaitingCancelsTheSignIn() {
        m_fetch->importClientJson(writeClientJson(googleClientJson()));
        m_fetch->signIn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        m_fetch->setAccount("other");
        QCOMPARE(m_fetch->oauthState(), QString("none"));
    }

    void aProviderRefusalIsShown() {
        m_fetch->importClientJson(writeClientJson(googleClientJson()));
        m_fetch->signIn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        httpGet(QUrl(param("redirect_uri")).port(), "/?error=access_denied&error_description=Nope&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->oauthError(), QString("Nope"), 3000);
        QCOMPARE(m_fetch->oauthState(), QString("none"));
    }

    void microsoftSignInWithAPastedClientId() {
        Http http;
        http.reply = QJsonObject{{"access_token", "ms-at"}, {"refresh_token", "ms-rt"}, {"expires_in", 3600}};
        fakeHttp(http);
        m_fetch->setAccount("outlook");
        m_fetch->setOption("mail.address", "jane@contoso.com");
        m_fetch->setOption("mail.msClientId", "00000000-0000-0000-0000-000000000000");
        m_fetch->signIn();
        QCOMPARE(m_fetch->oauthState(), QString("waiting"));
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        QCOMPARE(m_opened.host(), QString("login.microsoftonline.com"));
        QCOMPARE(param("client_id"), QString("00000000-0000-0000-0000-000000000000"));
        QVERIFY(param("scope").contains("IMAP.AccessAsUser.All"));
        QVERIFY(param("scope").contains("offline_access"));
        const QUrl redirect(param("redirect_uri"));
        QCOMPARE(redirect.host(), QString("localhost"));
        httpGet(redirect.port(), "/?code=ms-code&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->oauthState(), QString("signedIn"), 3000);
        QCOMPARE(http.urls[0], QUrl("https://login.microsoftonline.com/common/oauth2/v2.0/token"));
        QVERIFY(!http.forms[0].hasQueryItem("client_secret"));   // A public client has none.
        QCOMPARE(OAuth::challengeFor(http.forms[0].queryItemValue("code_verifier")), param("code_challenge"));
        QCOMPARE(m_fetch->option("mail.oauth"), QString("microsoft"));
        QCOMPARE(Keyring::lookup("precrime", "mail-oauth"), QString("ms-rt"));

        m_server->user = "jane@contoso.com";
        m_server->accessToken = "ms-at";
        m_server->add("INBOX", plus("From Microsoft"));
        m_fetch->setOption("mail.host", "127.0.0.1");
        m_fetch->setOption("mail.port", m_server->port());
        m_fetch->setOption("mail.security", "none");
        m_fetch->setOption("mail.after", "leave");
        enable();
        waitIdle();
        QCOMPARE(m_server->mechanisms, QStringList{"XOAUTH2"});
        QCOMPARE(titles(), QStringList{"From Microsoft"});
        m_fetch->signOut();
        QCOMPARE(m_fetch->oauthState(), QString("none"));
        QVERIFY(Keyring::lookup("precrime", "mail-oauth").isEmpty());
    }

    // A Google sign-in does not authenticate an Outlook account, or the other way round.
    void aSignInBelongsToItsProvider() {
        Http http;
        fakeHttp(http);
        m_fetch->importClientJson(writeClientJson(googleClientJson()));
        m_fetch->setOption("mail.address", "jane@example.org");
        m_fetch->signIn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_opened.isEmpty(), 3000);
        httpGet(QUrl(param("redirect_uri")).port(), "/?code=c&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_fetch->oauthState(), QString("signedIn"), 3000);
        m_fetch->setAccount("outlook");
        QCOMPARE(m_fetch->oauthState(), QString("none"));
        m_fetch->setAccount("gmail");
        QCOMPARE(m_fetch->oauthState(), QString("signedIn"));
    }

    // The repository must never ship an OAuth client: Precrime is open source and runs no
    // registration for forks. Clients come from the user (an imported JSON, a pasted ID).
    void theSourceContainsNoOAuthClient() {
#ifdef PRECRIME_ROOT
        static const QRegularExpression google("[0-9]{6,}-[0-9a-z]{20,}\\.apps\\.googleusercontent\\.com|GOCSPX-[A-Za-z0-9_-]{10,}");
        static const QRegularExpression guid("\\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\b");
        QStringList found;
        QStringList files{QStringLiteral(PRECRIME_ROOT "/README.md")};
        for (const QString &dir : {QStringLiteral(PRECRIME_ROOT "/src"), QStringLiteral(PRECRIME_ROOT "/completions")}) {
            QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                files << it.next();
        }
        QVERIFY(files.size() > 20);
        for (const QString &name : files) {
            QFile f(name);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QString text = QString::fromUtf8(f.readAll());
            if (google.match(text).hasMatch() || guid.match(text).hasMatch())
                found << name;
        }
        QVERIFY2(found.isEmpty(), qPrintable("looks like a client id in: " + found.join(", ")));
#else
        QSKIP("PRECRIME_ROOT not defined");
#endif
    }
};

REGISTER_TEST(MailFetchTest)

#include "test_mailfetch.moc"
