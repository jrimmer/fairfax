#include <QJsonDocument>
#include <QtTest>
#include <memory>

#include "loopback.h"
#include "oauth.h"
#include "testregistry.h"

// The installed-app loopback flow with PKCE (RFC 8252, RFC 7636). The browser
// is the test: it reads the authorisation URL the flow "opens" and sends the
// redirect to the listener itself. All HTTP goes through the Transport seam, so
// nothing leaves the machine.

namespace {

struct Request {
    QUrl url;
    QUrlQuery form;
};

}  // namespace

class OAuthTest : public QObject {
    Q_OBJECT

    std::unique_ptr<OAuth::Flow> m_flow;
    QUrl m_opened;
    QList<Request> m_requests;
    int m_status = 200;
    QJsonObject m_reply;
    QList<OAuth::Tokens> m_tokens;
    QStringList m_errors;

    void startFlow(const OAuth::Provider &provider) {
        m_flow->start(provider);
        QVERIFY(!m_opened.isEmpty());
    }

    QString param(const QString &name) const { return QUrlQuery(m_opened).queryItemValue(name, QUrl::FullyDecoded); }

private slots:
    void init() {
        m_flow = std::make_unique<OAuth::Flow>();
        m_opened = QUrl();
        m_requests.clear();
        m_tokens.clear();
        m_errors.clear();
        m_status = 200;
        m_reply = QJsonObject{{"access_token", "at-1"}, {"refresh_token", "rt-1"}, {"expires_in", 3600}};
        m_flow->setOpener([this](const QUrl &u) { m_opened = u; });
        m_flow->setTransport([this](const QUrl &url, const QUrlQuery &form, std::function<void(int, const QJsonObject &)> done) {
            m_requests.append({url, form});
            done(m_status, m_reply);
        });
        connect(m_flow.get(), &OAuth::Flow::finished, this, [this](const OAuth::Tokens &t) { m_tokens << t; });
        connect(m_flow.get(), &OAuth::Flow::failed, this, [this](const QString &e) { m_errors << e; });
    }

    // The worked example of RFC 7636 appendix B.
    void pkceChallengeMatchesTheRfcExample() {
        QCOMPARE(OAuth::challengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), QString("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }

    void verifiersAreLongRandomAndUrlSafe() {
        QSet<QString> seen;
        for (int i = 0; i < 20; ++i) {
            const QString v = OAuth::newVerifier();
            QVERIFY(v.size() >= 43 && v.size() <= 128);
            QVERIFY2(QRegularExpression("^[A-Za-z0-9_-]+$").match(v).hasMatch(), qPrintable(v));
            seen.insert(v);
        }
        QCOMPARE(seen.size(), 20);
    }

    // The URL carries what Google needs for a desktop client.
    void googleAuthorisationUrl() {
        startFlow(OAuth::google("my-client.apps.googleusercontent.com", "my-secret"));
        QCOMPARE(m_opened.scheme(), QString("https"));
        QCOMPARE(m_opened.host(), QString("accounts.google.com"));
        QCOMPARE(m_opened.path(), QString("/o/oauth2/v2/auth"));
        QCOMPARE(param("response_type"), QString("code"));
        QCOMPARE(param("client_id"), QString("my-client.apps.googleusercontent.com"));
        QCOMPARE(param("redirect_uri"), QString("http://127.0.0.1:%1").arg(m_flow->port()));
        QCOMPARE(param("scope"), QString("https://mail.google.com/"));
        QCOMPARE(param("access_type"), QString("offline"));
        QCOMPARE(param("prompt"), QString("consent"));
        QCOMPARE(param("code_challenge_method"), QString("S256"));
        QVERIFY(param("code_challenge").size() >= 43);
        QVERIFY(param("state").size() >= 16);
        QVERIFY(!m_opened.toString().contains("my-secret"));   // The secret never rides on the URL.
        QVERIFY(m_flow->waiting());
        QVERIFY(m_flow->port() > 0);
    }

    void microsoftAuthorisationUrl() {
        startFlow(OAuth::microsoft("11111111-2222-3333-4444-555555555555"));
        QCOMPARE(m_opened.host(), QString("login.microsoftonline.com"));
        QCOMPARE(m_opened.path(), QString("/common/oauth2/v2.0/authorize"));
        QCOMPARE(param("redirect_uri"), QString("http://localhost:%1").arg(m_flow->port()));
        QCOMPARE(param("scope"), QString("https://outlook.office.com/IMAP.AccessAsUser.All offline_access"));
        QCOMPARE(param("client_id"), QString("11111111-2222-3333-4444-555555555555"));
        QCOMPARE(param("code_challenge_method"), QString("S256"));
        QVERIFY(!QUrlQuery(m_opened).hasQueryItem("access_type"));   // Google-only parameters stay off.
    }

    // The whole round trip: redirect in, code and verifier out.
    void googleRoundTrip() {
        startFlow(OAuth::google("cid", "csecret"));
        const quint16 port = m_flow->port();
        const QString page = httpGet(port, "/?code=the-code&state=" + param("state"));
        QVERIFY2(page.contains("200 OK"), qPrintable(page));
        QVERIFY2(page.contains("You can close this tab"), qPrintable(page));
        QTRY_COMPARE_WITH_TIMEOUT(m_tokens.size(), 1, 3000);
        QCOMPARE(m_tokens[0].accessToken, QString("at-1"));
        QCOMPARE(m_tokens[0].refreshToken, QString("rt-1"));
        QVERIFY(m_tokens[0].expires > QDateTime::currentDateTime().addSecs(3000));
        QVERIFY(m_errors.isEmpty());

        QCOMPARE(m_requests.size(), 1);
        QCOMPARE(m_requests[0].url, QUrl("https://oauth2.googleapis.com/token"));
        const QUrlQuery &f = m_requests[0].form;
        QCOMPARE(f.queryItemValue("grant_type"), QString("authorization_code"));
        QCOMPARE(f.queryItemValue("code"), QString("the-code"));
        QCOMPARE(f.queryItemValue("client_id"), QString("cid"));
        QCOMPARE(f.queryItemValue("client_secret"), QString("csecret"));   // Google wants it, even for a desktop client.
        QCOMPARE(f.queryItemValue("redirect_uri"), QString("http://127.0.0.1:%1").arg(port));
        // PKCE: the verifier sent now hashes to the challenge sent in the browser URL.
        QVERIFY(!f.queryItemValue("code_verifier").isEmpty());
        QCOMPARE(OAuth::challengeFor(f.queryItemValue("code_verifier")), param("code_challenge"));
        // One-shot: the listener is gone.
        QVERIFY(!m_flow->waiting());
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(!probe.waitForConnected(500));
    }

    void microsoftRoundTripSendsNoSecret() {
        startFlow(OAuth::microsoft("ms-client"));
        httpGet(m_flow->port(), "/?code=abc&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_tokens.size(), 1, 3000);
        QCOMPARE(m_requests[0].url, QUrl("https://login.microsoftonline.com/common/oauth2/v2.0/token"));
        QVERIFY(!m_requests[0].form.hasQueryItem("client_secret"));
        // The token request names the same redirect URI as the browser did, as OAuth requires.
        QCOMPARE(m_requests[0].form.queryItemValue("redirect_uri"), param("redirect_uri"));
        QCOMPARE(OAuth::challengeFor(m_requests[0].form.queryItemValue("code_verifier")), param("code_challenge"));
    }

    // A redirect whose state is not the one we sent is somebody else's (a forged request).
    void wrongStateIsRejected() {
        startFlow(OAuth::google("cid", "s"));
        const QString page = httpGet(m_flow->port(), "/?code=evil&state=not-the-state");
        QVERIFY2(page.contains("400"), qPrintable(page));
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);
        QVERIFY2(m_errors[0].contains("state"), qPrintable(m_errors[0]));
        QVERIFY(m_requests.isEmpty());   // No token request was made.
        QVERIFY(m_tokens.isEmpty());
        QVERIFY(!m_flow->waiting());
    }

    void missingStateIsRejected() {
        startFlow(OAuth::google("cid", "s"));
        httpGet(m_flow->port(), "/?code=evil");
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);
        QVERIFY(m_requests.isEmpty());
    }

    void providerErrorIsReported() {
        startFlow(OAuth::google("cid", "s"));
        httpGet(m_flow->port(), "/?error=access_denied&error_description=The+user+said+no&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);
        QCOMPARE(m_errors[0], QString("The user said no"));
        QVERIFY(m_requests.isEmpty());
    }

    // A browser asking for /favicon.ico is not the redirect; the listener keeps waiting.
    void strayRequestsAreIgnored() {
        startFlow(OAuth::google("cid", "s"));
        const QString page = httpGet(m_flow->port(), "/favicon.ico");
        QVERIFY2(page.contains("404"), qPrintable(page));
        QVERIFY(m_flow->waiting());
        QVERIFY(m_errors.isEmpty());
        httpGet(m_flow->port(), "/?code=c&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_tokens.size(), 1, 3000);
    }

    void cancelClosesTheListenerAndStaysQuiet() {
        startFlow(OAuth::google("cid", "s"));
        const quint16 port = m_flow->port();
        m_flow->cancel();
        QVERIFY(!m_flow->waiting());
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(!probe.waitForConnected(500));
        QTest::qWait(50);
        QVERIFY(m_errors.isEmpty());
        QVERIFY(m_tokens.isEmpty());
    }

    // A token answer that arrives after Cancel is dropped.
    void lateAnswerAfterCancelIsDropped() {
        std::function<void(int, const QJsonObject &)> held;
        m_flow->setTransport([&](const QUrl &, const QUrlQuery &, std::function<void(int, const QJsonObject &)> done) { held = done; });
        startFlow(OAuth::google("cid", "s"));
        httpGet(m_flow->port(), "/?code=c&state=" + param("state"));
        QTRY_VERIFY_WITH_TIMEOUT(bool(held), 3000);
        m_flow->cancel();
        held(200, m_reply);
        QVERIFY(m_tokens.isEmpty());
    }

    void timesOutWaitingForTheBrowser() {
        m_flow->setWaitLimit(150);
        startFlow(OAuth::google("cid", "s"));
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);
        QVERIFY2(m_errors[0].contains("Timed out"), qPrintable(m_errors[0]));
        QVERIFY(!m_flow->waiting());
    }

    void startingAgainReplacesTheOldAttempt() {
        startFlow(OAuth::google("cid", "s"));
        const QString firstState = param("state");
        const quint16 firstPort = m_flow->port();
        startFlow(OAuth::google("cid", "s"));
        QVERIFY(param("state") != firstState);
        // The first attempt's redirect no longer works.
        if (m_flow->port() != firstPort)
            QVERIFY(httpGet(firstPort, "/?code=c&state=" + firstState).contains("no connection"));
        httpGet(m_flow->port(), "/?code=c&state=" + firstState);
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);   // The old state is the wrong state now.
        QVERIFY(m_tokens.isEmpty());
    }

    void tokenEndpointErrorsAreReported() {
        m_status = 400;
        m_reply = QJsonObject{{"error", "invalid_grant"}, {"error_description", "Bad Request"}};
        startFlow(OAuth::google("cid", "s"));
        httpGet(m_flow->port(), "/?code=c&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);
        QCOMPARE(m_errors[0], QString("Bad Request"));
        QVERIFY(m_tokens.isEmpty());
    }

    void unreachableTokenEndpointIsReported() {
        m_status = 0;
        m_reply = QJsonObject();
        startFlow(OAuth::google("cid", "s"));
        httpGet(m_flow->port(), "/?code=c&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);
        QVERIFY2(m_errors[0].contains("reach"), qPrintable(m_errors[0]));
    }

    // Without a refresh token Fairfax could not stay signed in, so that is a failed sign-in.
    void noRefreshTokenIsAFailure() {
        m_reply = QJsonObject{{"access_token", "at"}, {"expires_in", 10}};
        startFlow(OAuth::google("cid", "s"));
        httpGet(m_flow->port(), "/?code=c&state=" + param("state"));
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, 3000);
        QVERIFY2(m_errors[0].contains("refresh token"), qPrintable(m_errors[0]));
    }

    void refreshKeepsTheOldTokenWhenNoNewOneComes() {
        m_reply = QJsonObject{{"access_token", "at-2"}, {"expires_in", 1800}};
        m_flow->refresh(OAuth::google("cid", "csecret"), "rt-old");
        QCOMPARE(m_tokens.size(), 1);
        QCOMPARE(m_tokens[0].accessToken, QString("at-2"));
        QCOMPARE(m_tokens[0].refreshToken, QString("rt-old"));
        const QUrlQuery &f = m_requests[0].form;
        QCOMPARE(f.queryItemValue("grant_type"), QString("refresh_token"));
        QCOMPARE(f.queryItemValue("refresh_token"), QString("rt-old"));
        QCOMPARE(f.queryItemValue("client_secret"), QString("csecret"));
    }

    // Microsoft rotates refresh tokens: the new one replaces the old.
    void refreshTakesARotatedToken() {
        m_reply = QJsonObject{{"access_token", "at-3"}, {"refresh_token", "rt-new"}, {"expires_in", 60}};
        m_flow->refresh(OAuth::microsoft("ms"), "rt-old");
        QCOMPARE(m_tokens.size(), 1);
        QCOMPARE(m_tokens[0].refreshToken, QString("rt-new"));
        QVERIFY(!m_requests[0].form.hasQueryItem("client_secret"));
    }

    void refreshFailureIsReported() {
        m_status = 400;
        m_reply = QJsonObject{{"error", "invalid_grant"}};
        m_flow->refresh(OAuth::google("cid", "s"), "revoked");
        QCOMPARE(m_errors, QStringList{"invalid_grant"});
        QVERIFY(m_tokens.isEmpty());
    }
};

REGISTER_TEST(OAuthTest)

#include "test_oauth.moc"
