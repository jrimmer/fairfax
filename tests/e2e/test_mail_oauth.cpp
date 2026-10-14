// Mail to Precrime with a client the user brings: Gmail's "Import client JSON…" and
// "Sign in with Google", and Outlook's "Use your own client ID" with "Sign in with
// Microsoft". The browser is the test (it reads the URL Precrime opens and sends the
// redirect itself) and the token endpoint is a fake HTTP seam; Precrime ships no OAuth
// client of its own, so the client comes from the test.

#include "mailkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

class UrlSink : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void opened(const QUrl &url) { urls << url; }
};

}  // namespace

class E2EMailOAuth : public QObject {
    Q_OBJECT

private slots:
    void init() {
        for (const char *account : {"mail", "mail-client", "mail-oauth"})
            Keyring::clear("precrime", account);
    }
    void cleanup() { QDesktopServices::unsetUrlHandler("https"); }

    // Gmail, advanced: "Import client JSON…" through the file picker, then "Sign in with Google" through a
    // browser, with Sign Out at the end. The HTTP calls and the browser are fakes.
    void importClientJsonThenSignInWithGoogle() {
        UrlSink browser;
        QDesktopServices::setUrlHandler("https", &browser, "opened");
        Http http;
        FakeImap server;
        server.user = "jane@gmail.com";
        server.accessToken = "e2e-at";
        server.add("INBOX", plusMail("Via Google", "g1", "jane+precrime@gmail.com"));
        App app;
        fakeHttp(app, http);
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        setText(sw, "setting:mail.address", "jane@gmail.com");
        // The advanced section starts folded.
        QVERIFY(!findItem("mailImportClient"));
        QQuickItem *advanced = nullptr;
        control("mailGoogleAdvanced", advanced);
        QVERIFY(advanced);
        click(advanced);
        QQuickItem *import = nullptr;
        control("mailImportClient", import);
        QVERIFY(import);
        QCOMPARE(textOf("mailClientStatus"), QString("No client imported"));

        // Signing in before there is a client says what to do.
        QQuickItem *signIn = nullptr;
        control("mailSignIn", signIn);
        QVERIFY(signIn);
        QCOMPARE(signIn->property("text").toString(), QString("Sign in with Google"));
        click(signIn);
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailOAuthError").contains("Import your Google client JSON first"), 5000);
        QVERIFY(browser.urls.isEmpty());

        // A file that isn't a client is refused in words.
        click(import);
        QTRY_COMPARE_WITH_TIMEOUT(app.services().filePicker.lastPurpose(), QString("mail-client-json"), 5000);
        const QString dir = QFileInfo(app.store().path()).absolutePath();
        emit app.services().filePicker.picked(writeFile(dir, "bad.json", "{\"web\":{\"client_id\":\"x\",\"client_secret\":\"y\"}}"), "mail-client-json");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailClientError").contains("Web client"), 5000);
        QCOMPARE(textOf("mailClientStatus"), QString("No client imported"));

        // The real thing.
        click(findItem("mailImportClient"));
        emit app.services().filePicker.picked(writeFile(dir, "client_secret.json", GoogleClient()), "mail-client-json");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailClientStatus").contains("e2e… imported"), 5000);
        QVERIFY(!findItem("mailClientError"));
        QCOMPARE(app.store().setting("mail.googleClientId").toString(), QString("e2e-client.apps.example"));
        QCOMPARE(Keyring::lookup("precrime", "mail-client"), QString("e2e-secret"));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("mailOAuthError"), 5000);

        click(findItem("mailSignIn"));
        QTRY_VERIFY_WITH_TIMEOUT(!browser.urls.isEmpty(), 5000);
        QCOMPARE(textOf("mailOAuthStatus"), QString("Waiting for the browser…"));
        QVERIFY(findItem("mailCancelSignIn"));
        QVERIFY(!findItem("mailSignIn"));
        const QUrlQuery q(browser.urls.last());
        QCOMPARE(q.queryItemValue("client_id"), QString("e2e-client.apps.example"));
        const QUrl redirect(q.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        // Cancel, then try again: the first listener is gone.
        click(findItem("mailCancelSignIn"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailSignIn") != nullptr, 5000);
        QCOMPARE(httpGet(redirect.port(), "/?code=late&state=" + q.queryItemValue("state")), QString("no connection"));
        click(findItem("mailSignIn"));
        QTRY_COMPARE_WITH_TIMEOUT(browser.urls.size(), 2, 5000);
        const QUrlQuery q2(browser.urls.last());
        QVERIFY(q2.queryItemValue("state") != q.queryItemValue("state"));
        httpGet(QUrl(q2.queryItemValue("redirect_uri", QUrl::FullyDecoded)).port(), "/?code=g-code&state=" + q2.queryItemValue("state"));
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailOAuthStatus"), QString("Signed in as jane@gmail.com"), 5000);
        QVERIFY(findItem("mailSignOut"));
        QVERIFY(!findItem("mailSignIn"));
        QCOMPARE(http.forms.size(), 1);
        QCOMPARE(http.forms[0].queryItemValue("code"), QString("g-code"));
        QCOMPARE(http.forms[0].queryItemValue("client_secret"), QString("e2e-secret"));
        QCOMPARE(OAuth::challengeFor(http.forms[0].queryItemValue("code_verifier")), q2.queryItemValue("code_challenge"));
        QCOMPARE(app.store().setting("mail.oauth").toString(), QString("google"));
        QCOMPARE(Keyring::lookup("precrime", "mail-oauth"), QString("e2e-rt"));

        // Mail flows over XOAUTH2 with no app password.
        // The server is the test's own, so it is set up directly rather than through the Other account's fields.
        app.services().mail->setOption("mail.host", "127.0.0.1");
        app.services().mail->setOption("mail.port", server.port());
        app.services().mail->setOption("mail.security", "none");
        choose("setting:mail.after", "Leave as is");
        QVERIFY(!app.services().mail->hasPassword());
        clickFetchNow();
        waitStatus("Connected");
        QVERIFY(app.taskTitled("Via Google").id);
        QCOMPARE(server.mechanisms, QStringList{"XOAUTH2"});

        // Sign Out clears the tokens.
        click(findItem("mailSignOut"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailSignIn") != nullptr, 5000);
        QVERIFY(Keyring::lookup("precrime", "mail-oauth").isEmpty());
        QVERIFY(app.store().setting("mail.oauth").toString().isEmpty());
        QVERIFY(!findItem("mailSignOut"));
        QCOMPARE(app.services().mail->oauthState(), QString("none"));
    }

    // A forged redirect (wrong state) does not sign anybody in, and the pane says why.
    void aForgedRedirectIsRefusedInThePane() {
        UrlSink browser;
        QDesktopServices::setUrlHandler("https", &browser, "opened");
        Http http;
        App app;
        fakeHttp(app, http);
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        QQuickItem *advanced = nullptr;
        control("mailGoogleAdvanced", advanced);
        QVERIFY(advanced);
        click(advanced);
        // The client is set up directly: importing it is not what this test is about.
        QCOMPARE(app.services().mail->importClientJson(writeFile(QFileInfo(app.store().path()).absolutePath(), "c.json", GoogleClient())), QString());
        QQuickItem *signIn = nullptr;
        control("mailSignIn", signIn);
        QVERIFY(signIn);
        click(signIn);
        QTRY_VERIFY_WITH_TIMEOUT(!browser.urls.isEmpty(), 5000);
        const QUrlQuery q(browser.urls.last());
        httpGet(QUrl(q.queryItemValue("redirect_uri", QUrl::FullyDecoded)).port(), "/?code=evil&state=forged");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailOAuthError").contains("state"), 5000);
        QVERIFY(findItem("mailSignIn"));
        QVERIFY(!findItem("mailSignOut"));
        QVERIFY(http.forms.isEmpty());
        QVERIFY(Keyring::lookup("precrime", "mail-oauth").isEmpty());
    }

    // Outlook, advanced: paste your own client ID, then "Sign in with Microsoft".
    void yourOwnClientIdThenSignInWithMicrosoft() {
        UrlSink browser;
        QDesktopServices::setUrlHandler("https", &browser, "opened");
        Http http;
        http.reply = QJsonObject{{"access_token", "ms-at"}, {"refresh_token", "ms-rt"}, {"expires_in", 3600}};
        App app;
        fakeHttp(app, http);
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        choose("setting:mail.account", "Outlook");
        setText(sw, "setting:mail.address", "jane@contoso.com");
        QVERIFY(!findItem("setting:mail.msClientId"));   // Folded away until asked for.
        QQuickItem *advanced = nullptr;
        control("mailMsAdvanced", advanced);
        QVERIFY(advanced);
        click(advanced);
        QQuickItem *idField = nullptr;
        control("setting:mail.msClientId", idField);
        QVERIFY(idField);

        // No client ID yet: it says so.
        QQuickItem *signIn = nullptr;
        control("mailSignIn", signIn);
        QVERIFY(signIn);
        QCOMPARE(signIn->property("text").toString(), QString("Sign in with Microsoft"));
        click(signIn);
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailOAuthError").contains("client) ID"), 5000);
        QVERIFY(browser.urls.isEmpty());

        setText(sw, "setting:mail.msClientId", "00000000-aaaa-bbbb-cccc-000000000000");
        QCOMPARE(app.store().setting("mail.msClientId").toString(), QString("00000000-aaaa-bbbb-cccc-000000000000"));
        click(findItem("mailSignIn"));
        QTRY_VERIFY_WITH_TIMEOUT(!browser.urls.isEmpty(), 5000);
        QCOMPARE(browser.urls.last().host(), QString("login.microsoftonline.com"));
        const QUrlQuery q(browser.urls.last());
        QCOMPARE(q.queryItemValue("client_id"), QString("00000000-aaaa-bbbb-cccc-000000000000"));
        QVERIFY(q.queryItemValue("scope", QUrl::FullyDecoded).contains("IMAP.AccessAsUser.All"));
        const QUrl redirect(q.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QCOMPARE(redirect.host(), QString("localhost"));
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailOAuthStatus"), QString("Waiting for the browser…"), 5000);
        const QString page = httpGet(redirect.port(), "/?code=ms-code&state=" + q.queryItemValue("state"));
        QVERIFY2(page.contains("You can close this tab"), qPrintable(page));
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailOAuthStatus"), QString("Signed in as jane@contoso.com"), 5000);
        QCOMPARE(http.urls.value(0), QUrl("https://login.microsoftonline.com/common/oauth2/v2.0/token"));
        QVERIFY(!http.forms.value(0).hasQueryItem("client_secret"));
        QCOMPARE(OAuth::challengeFor(http.forms.value(0).queryItemValue("code_verifier")), q.queryItemValue("code_challenge"));
        QCOMPARE(app.store().setting("mail.oauth").toString(), QString("microsoft"));
        QCOMPARE(Keyring::lookup("precrime", "mail-oauth"), QString("ms-rt"));

        click(findItem("mailSignOut"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailSignIn") != nullptr, 5000);
        QVERIFY(Keyring::lookup("precrime", "mail-oauth").isEmpty());
        QVERIFY(app.store().setting("mail.oauth").toString().isEmpty());
        // The client ID is the user's own setting and stays after a sign-out.
        QCOMPARE(textOf("setting:mail.msClientId"), QString("00000000-aaaa-bbbb-cccc-000000000000"));
    }
};

REGISTER_TEST(E2EMailOAuth)

#include "test_mail_oauth.moc"
