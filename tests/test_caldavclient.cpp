#include <QtTest>
#include <memory>

#include "caldavclient.h"
#include "fakecaldav.h"
#include "testregistry.h"

// The CalDAV client against a scripted fake server (tests/fakecaldav.h) over
// plain HTTP on 127.0.0.1, which is the only host allowed to carry a password
// in the clear. Nothing here touches the network beyond the loopback.

namespace {

const char *TaskOne =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Fairfax//EN\r\nBEGIN:VTODO\r\nUID:one\r\nSUMMARY:First\r\n"
    "END:VTODO\r\nEND:VCALENDAR\r\n";
const char *TaskTwo =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Fairfax//EN\r\nBEGIN:VTODO\r\nUID:two\r\nSUMMARY:Second\r\n"
    "END:VTODO\r\nEND:VCALENDAR\r\n";

}  // namespace

class CalDavClientTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeCalDav> m_server;
    std::unique_ptr<CalDavClient> m_client;

    // A client pointed at the server, with a short timeout and a fast backoff
    // so the retry tests do not wait half a minute.
    CalDavClient *client(int backoffBase = 20) {
        m_client = std::make_unique<CalDavClient>();
        m_client->setCredentials("jane", "app-password");
        // Discovery sets the base; the operations below are relative to that
        // home, so point the client at the fake server's root up front.
        m_client->setBaseUrl(m_server->baseUrl());
        CalDavClient::Timings t;
        t.requestTimeout = 500;
        t.backoffBase = backoffBase;
        t.backoffMax = 200;
        t.maxAttempts = 4;
        m_client->setTimings(t);
        return m_client.get();
    }

    struct Disc {
        bool done = false;
        bool ok = false;
        bool authFailed = false;
        QList<CalDavClient::Calendar> calendars;
        QString error;
    };
    Disc discover(const QUrl &url) {
        Disc out;
        client()->discover(url, [&](const CalDavClient::Outcome &o, const QList<CalDavClient::Calendar> &cs) {
            out.done = true;
            out.ok = o.ok;
            out.authFailed = o.authFailed;
            out.calendars = cs;
            out.error = o.error;
        });
        QTest::qWaitFor([&] { return out.done; }, 8000);
        return out;
    }

    struct Etags {
        bool done = false, ok = false;
        QMap<QString, QString> etags;
    };
    Etags listEtags(const QString &href) {
        Etags out;
        client()->listEtags(href, [&](const CalDavClient::Outcome &o, const QMap<QString, QString> &e) {
            out.done = true;
            out.ok = o.ok;
            out.etags = e;
        });
        QTest::qWaitFor([&] { return out.done; }, 8000);
        return out;
    }

    struct Synced {
        bool done = false, ok = false;
        QStringList changed, removed;
        QString token;
        QString error;
    };
    Synced sync(const QString &href, const QString &token) {
        Synced out;
        client()->syncCollection(href, token, [&](const CalDavClient::Outcome &o, const QStringList &ch,
                                                   const QStringList &rm, const QString &t) {
            out.done = true;
            out.ok = o.ok;
            out.changed = ch;
            out.removed = rm;
            out.token = t;
            out.error = o.error;
        });
        QTest::qWaitFor([&] { return out.done; }, 8000);
        return out;
    }

    struct Multigot {
        bool done = false, ok = false;
        QMap<QString, CalDavClient::Resource> resources;
    };
    Multigot multiget(const QString &href, const QStringList &refs) {
        Multigot out;
        client()->multiget(href, refs, [&](const CalDavClient::Outcome &o, const QMap<QString, CalDavClient::Resource> &r) {
            out.done = true;
            out.ok = o.ok;
            out.resources = r;
        });
        QTest::qWaitFor([&] { return out.done; }, 8000);
        return out;
    }

private slots:
    void init() { m_server = std::make_unique<FakeCalDav>(); }
    void cleanup() {
        m_client.reset();
        m_server.reset();
    }

    // ---------------------------------------------------------- discovery

    void discoveryThroughWellKnownPrincipalAndHome() {
        const Disc d = discover(m_server->baseUrl());
        QVERIFY2(d.ok, qPrintable(d.error));
        QCOMPARE(d.calendars.size(), 1);   // The VEVENT-only calendar is filtered out.
        const CalDavClient::Calendar &tasks = d.calendars.first();
        QCOMPARE(tasks.href, QStringLiteral("/calendars/jane/tasks/"));
        QCOMPARE(tasks.displayName, QStringLiteral("Tasks"));
        QCOMPARE(tasks.color, QStringLiteral("#00ff00ff"));
        QVERIFY(tasks.supportsVTodo);
        QVERIFY(!tasks.ctag.isEmpty());
        QVERIFY(!tasks.syncToken.isEmpty());
        QCOMPARE(m_client->baseUrl().path(), QStringLiteral("/calendars/jane/tasks/"));
    }

    void directCalendarUrlIsAccepted() {
        const Disc d = discover(m_server->baseUrl().resolved(QUrl(QStringLiteral("/calendars/jane/tasks/"))));
        QVERIFY2(d.ok, qPrintable(d.error));
        QCOMPARE(d.calendars.size(), 1);
        QCOMPARE(d.calendars.first().href, QStringLiteral("/calendars/jane/tasks/"));
    }

    void directHomeUrlListsTheCalendars() {
        const Disc d = discover(m_server->baseUrl().resolved(QUrl(QStringLiteral("/calendars/jane/"))));
        QVERIFY2(d.ok, qPrintable(d.error));
        QCOMPARE(d.calendars.size(), 1);
        QCOMPARE(d.calendars.first().href, QStringLiteral("/calendars/jane/tasks/"));
    }

    void onlyVTodoCalendarsAreKept() {
        const Disc d = discover(m_server->baseUrl());
        QVERIFY(d.ok);
        for (const auto &c : d.calendars)
            QVERIFY(c.supportsVTodo);
        for (const auto &c : d.calendars)
            QVERIFY(!c.href.contains(QStringLiteral("events")));
    }

    // -------------------------------------------------------------- listing

    void etagListing() {
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"), TaskTwo);
        const Etags e = listEtags(QStringLiteral("/calendars/jane/tasks/"));
        QVERIFY(e.ok);
        QCOMPARE(e.etags.size(), 2);
        QVERIFY(e.etags.contains(QStringLiteral("/calendars/jane/tasks/one.ics")));
        QVERIFY(!e.etags.value(QStringLiteral("/calendars/jane/tasks/one.ics")).isEmpty());
    }

    void syncCollectionReturnsChangedAndRemoved() {
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        const Synced first = sync(QStringLiteral("/calendars/jane/tasks/"), QString());
        QVERIFY2(first.ok, qPrintable(first.error));
        QVERIFY(first.changed.contains(QStringLiteral("/calendars/jane/tasks/one.ics")));
        QVERIFY(!first.token.isEmpty());

        // Nothing new since the token.
        const Synced quiet = sync(QStringLiteral("/calendars/jane/tasks/"), first.token);
        QVERIFY(quiet.ok);
        QVERIFY(quiet.changed.isEmpty());

        // A change and a removal after the token.
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"), TaskTwo);
        m_server->removeFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"));
        const Synced next = sync(QStringLiteral("/calendars/jane/tasks/"), first.token);
        QVERIFY(next.ok);
        QVERIFY(next.changed.contains(QStringLiteral("/calendars/jane/tasks/two.ics")));
        QVERIFY(next.removed.contains(QStringLiteral("/calendars/jane/tasks/one.ics")));
    }

    void syncCollectionFallsBackToEtagsWhenOff() {
        m_server->syncCollection = false;
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        const Synced s = sync(QStringLiteral("/calendars/jane/tasks/"), QString());
        QVERIFY2(s.ok, qPrintable(s.error));
        QVERIFY(s.token.isEmpty());   // No token: the caller keeps using etags.
        QVERIFY(s.changed.contains(QStringLiteral("/calendars/jane/tasks/one.ics")));
        QVERIFY(s.removed.isEmpty());
    }

    void multigetFetchesSeveralResources() {
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("two.ics"), TaskTwo);
        const Multigot m = multiget(QStringLiteral("/calendars/jane/tasks/"),
                                    {QStringLiteral("/calendars/jane/tasks/one.ics"),
                                     QStringLiteral("/calendars/jane/tasks/two.ics")});
        QVERIFY(m.ok);
        QCOMPARE(m.resources.size(), 2);
        const auto one = m.resources.value(QStringLiteral("/calendars/jane/tasks/one.ics"));
        QVERIFY(QString::fromUtf8(one.data).contains(QStringLiteral("SUMMARY:First")));
        QVERIFY(!one.etag.isEmpty());
    }

    // -------------------------------------------------------------- writing

    void putCreatesANewResource() {
        client();
        QString etag;
        bool done = false;
        bool conflict = false;
        m_client->put(QStringLiteral("/calendars/jane/tasks/new.ics"), TaskOne, QString(),
                      [&](const CalDavClient::Outcome &o, const QString &e) {
                          done = true;
                          conflict = o.conflict;
                          QVERIFY(o.ok);
                          etag = e;
                      });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(!conflict);
        QVERIFY(!etag.isEmpty());
        QVERIFY(m_server->tasks()->resources.contains(QStringLiteral("/calendars/jane/tasks/new.ics")));
    }

    void putUpdatesWithIfMatch() {
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        const QString oldEtag = m_server->tasks()->etags.value(QStringLiteral("/calendars/jane/tasks/one.ics"));

        client();
        bool done = false;
        QString newEtag;
        m_client->put(QStringLiteral("/calendars/jane/tasks/one.ics"), TaskTwo, oldEtag,
                      [&](const CalDavClient::Outcome &o, const QString &e) {
                          done = true;
                          QVERIFY(o.ok);
                          newEtag = e;
                      });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(!newEtag.isEmpty());
        QVERIFY(newEtag != oldEtag);
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(QStringLiteral("/calendars/jane/tasks/one.ics")))
                    .contains(QStringLiteral("SUMMARY:Second")));
    }

    void putWithAStaleEtagConflicts() {
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        client();
        bool done = false;
        bool conflict = false;
        m_client->put(QStringLiteral("/calendars/jane/tasks/one.ics"), TaskTwo, QStringLiteral("\"stale\""),
                      [&](const CalDavClient::Outcome &o, const QString &) {
                          done = true;
                          conflict = o.conflict;
                      });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(conflict);
        // The resource is untouched.
        QVERIFY(QString::fromUtf8(m_server->tasks()->resources.value(QStringLiteral("/calendars/jane/tasks/one.ics")))
                    .contains(QStringLiteral("SUMMARY:First")));
    }

    void putNewWithIfNoneMatchConflictsWhenItExists() {
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        client();
        bool done = false, conflict = false;
        m_client->put(QStringLiteral("/calendars/jane/tasks/one.ics"), TaskTwo, QString(),
                      [&](const CalDavClient::Outcome &o, const QString &) {
                          done = true;
                          conflict = o.conflict;
                      });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(conflict);
    }

    void removeDeletesWithIfMatch() {
        m_server->editFromAnotherClient(QStringLiteral("/calendars/jane/tasks/"), QStringLiteral("one.ics"), TaskOne);
        const QString etag = m_server->tasks()->etags.value(QStringLiteral("/calendars/jane/tasks/one.ics"));
        client();
        bool done = false, ok = false;
        m_client->remove(QStringLiteral("/calendars/jane/tasks/one.ics"), etag, [&](const CalDavClient::Outcome &o) {
            done = true;
            ok = o.ok;
        });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(ok);
        QVERIFY(!m_server->tasks()->resources.contains(QStringLiteral("/calendars/jane/tasks/one.ics")));
    }

    void removeMissingIsFine() {
        client();
        bool done = false, ok = false;
        m_client->remove(QStringLiteral("/calendars/jane/tasks/gone.ics"), QString(), [&](const CalDavClient::Outcome &o) {
            done = true;
            ok = o.ok;
        });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(ok);
    }

    void mkcalendarCreatesAVTodoOnlyCalendar() {
        client();
        bool done = false;
        bool ok = false;
        QString href;
        m_client->mkcalendar(QStringLiteral("/calendars/jane/"), QStringLiteral("Work"), QStringLiteral("#123456ff"),
                             [&](const CalDavClient::Outcome &o, const QString &h) {
                                 done = true;
                                 ok = o.ok;
                                 href = h;
                             });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(ok);
        QVERIFY(href.contains(QStringLiteral("Work")));
        bool found = false;
        for (const FakeCalDav::Collection &col : m_server->collections())
            if (col.displayName == QStringLiteral("Work"))
                found = true;
        QVERIFY(found);
    }

    // --------------------------------------------------------------- errors

    void wrongPasswordIsReportedAndNotRetried() {
        CalDavClient::Timings t;
        t.requestTimeout = 500;
        t.backoffBase = 10;
        t.backoffMax = 20;
        m_client = std::make_unique<CalDavClient>();
        m_client->setCredentials("jane", "wrong");
        m_client->setBaseUrl(m_server->baseUrl());
        m_client->setTimings(t);
        bool done = false;
        bool authFailed = false;
        m_client->discover(m_server->baseUrl(), [&](const CalDavClient::Outcome &o, const QList<CalDavClient::Calendar> &) {
            done = true;
            authFailed = o.authFailed;
        });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(authFailed);
        // One request per step, none repeated: the well-known GET and the three
        // PROPFINDs of the handshake, and no retry.
        QCOMPARE(m_server->countRequests("GET"), 0);
        // One PROPFIND (the direct Depth 0 probe) that got the 401, and no retry.
        QCOMPARE(m_server->countRequests("PROPFIND"), 1);
    }

    void aTimeoutIsRetriedWithBackoff() {
        m_client = std::make_unique<CalDavClient>();
        m_client->setCredentials("jane", "app-password");
        m_client->setBaseUrl(m_server->baseUrl());
        CalDavClient::Timings t;
        // Long enough for a slow machine's server to read each request before
        // the client gives up on it.
        t.requestTimeout = 400;
        t.backoffBase = 20;
        t.backoffMax = 200;
        t.maxAttempts = 4;
        m_client->setTimings(t);
        m_server->silentOn = QStringLiteral("PROPFIND");
        int retries = 0;
        int lastDelay = 0;
        connect(m_client.get(), &CalDavClient::retrying, this, [&](int, int delay) {
            ++retries;
            lastDelay = delay;
        });
        bool done = false, ok = true;
        m_client->listEtags(QStringLiteral("/calendars/jane/tasks/"),
                            [&](const CalDavClient::Outcome &o, const QMap<QString, QString> &) {
                                done = true;
                                ok = o.ok;
                            });
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(!ok);
        QVERIFY2(retries >= 2, "the timeout was not retried");
        QVERIFY(lastDelay >= 20);
        QTRY_COMPARE_WITH_TIMEOUT(m_server->countRequests("PROPFIND"), 4, 2000);   // maxAttempts,
        QTest::qWait(300);
        QCOMPARE(m_server->countRequests("PROPFIND"), 4);                          // and no more.
    }

    void aDroppedConnectionIsRetried() {
        client(20);
        m_server->silentOn = QStringLiteral("PROPFIND");   // Hold the first request open.
        int retries = 0;
        connect(m_client.get(), &CalDavClient::retrying, this, [&](int, int) { ++retries; });
        bool done = false;
        bool ok = false;
        m_client->listEtags(QStringLiteral("/calendars/jane/tasks/"),
                            [&](const CalDavClient::Outcome &o, const QMap<QString, QString> &) {
                                done = true;
                                ok = o.ok;
                            });
        // Wait until the first PROPFIND is in flight, drop it, and let the
        // server answer normally again so the retry succeeds.
        QTRY_VERIFY_WITH_TIMEOUT(m_server->countRequests("PROPFIND") >= 1, 5000);
        m_server->dropConnections();
        m_server->silentOn.clear();
        QTest::qWaitFor([&] { return done; }, 8000);
        QVERIFY(ok);
        QVERIFY(retries >= 1);
    }
};

REGISTER_TEST(CalDavClientTest)

#include "test_caldavclient.moc"
