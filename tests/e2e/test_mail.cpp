// Mail to Precrime, from the user's side: Settings > Mail, and the connector
// against a fake IMAP server on 127.0.0.1 (tests/fakeimap.h). The the reference app manual
// page "Mail to the reference app" (email-to-tasks.md) describes a hosted address; Precrime
// reaches a mailbox you own instead, so these tests quote the manual for what
// becomes a task and follow the Mail spec for how mail arrives.
//
// Nothing here touches the network beyond the loopback or the real keyring (the
// keyring is in memory, see main.cpp), and the OAuth HTTP calls go through
// MailFetch::setHttpTransport.

#include <QClipboard>
#include <QDesktopServices>
#include <QJsonObject>

#include "../fakeimap.h"
#include "keyring.h"
#include "../loopback.h"
#include "settingskit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

QString textOf(const QString &name) {
    QQuickItem *i = findItem(name);
    return i ? i->property("text").toString() : QString("<no such item: " + name + ">");
}

// Opens Settings on the Mail pane and waits until its scrolling area is as tall as its content: the
// pane is long, and a control scrolled to while it is still growing ends up out of view.
void openMailPane(App &app, QQuickWindow *&sw) {
    openSettings(app, sw);
    selectPane(sw, "Mail");
    QQuickItem *last = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((last = findItem("mailPipe")) != nullptr, 5000);
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (QQuickItem *p = last->parentItem(); p; p = p->parentItem())
            if (p->inherits("QQuickFlickable")) {
                auto *content = p->property("contentItem").value<QQuickItem *>();
                const QRectF r = last->mapRectToItem(content, QRectF(0, 0, last->width(), last->height()));
                return p->property("contentHeight").toReal() >= r.bottom();
            }
        return false;
    }(), 5000);
}

// The index of the entry whose label is `label` in a combo box.
int comboIndex(const QString &name, const QString &label) {
    QQuickItem *combo = findItem(name);
    if (!combo)
        return -1;
    for (int i = 0; i < combo->property("count").toInt(); ++i) {
        QString text;
        QMetaObject::invokeMethod(combo, "textAt", Q_RETURN_ARG(QString, text), Q_ARG(int, i));
        if (text == label)
            return i;
    }
    return -1;
}

void choose(const QString &name, const QString &label) {
    QQuickItem *combo = nullptr;
    control(name, combo);
    const int index = comboIndex(name, label);
    QVERIFY2(index >= 0, qPrintable(name + " has no entry " + label));
    chooseCombo(name, index);
}

QString comboText(const QString &name) {
    QQuickItem *c = findItem(name);
    return c ? c->property("currentText").toString() : QString("<no such combo: " + name + ">");
}

// Empties a text field the way a user does (select all, delete, Return). A field whose empty
// value shows a default (the folder) does not read back as empty, so nothing waits for that.
void clearField(QQuickWindow *sw, const QString &name) {
    QQuickItem *field = nullptr;
    control(name, field);
    click(field);
    QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(sw, Qt::Key_Delete);
    QTest::keyClick(sw, Qt::Key_Return);
}

// Clicks Fetch Now once the pane has stopped moving (a click that lands on a pane still laying itself out is lost).
void clickFetchNow() {
    QQuickItem *button = nullptr;
    control("mailFetchNow", button);
    click(button);
}

// Types a password into the masked field and commits it; the field then empties itself.
void typePassword(QQuickWindow *sw, const QString &password) {
    QQuickItem *field = nullptr;
    control("mailPassword", field);
    click(field);
    typeInto(sw, password);
    QTest::keyClick(sw, Qt::Key_Return);
}

QByteArray plusMail(const QString &subject, const QString &id, const QString &to = "jane+precrime@example.org") {
    return FakeImap::mail(to, subject, "Body of " + subject, id);
}

QString writeFile(const QString &dir, const QString &name, const QString &body) {
    QFile f(dir + "/" + name);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(body.toUtf8());
    return f.fileName();
}

const char *GoogleClient = R"({"installed":{"client_id":"e2e-client.apps.example","client_secret":"e2e-secret",
    "auth_uri":"https://accounts.google.com/o/oauth2/auth","token_uri":"https://oauth2.googleapis.com/token"}})";

class UrlSink : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void opened(const QUrl &url) { urls << url; }
};

struct Http {
    QList<QUrl> urls;
    QList<QUrlQuery> forms;
    QJsonObject reply{{"access_token", "e2e-at"}, {"refresh_token", "e2e-rt"}, {"expires_in", 3600}};
};

void fakeHttp(App &app, Http &http) {
    app.services().mail->setHttpTransport([&http](const QUrl &url, const QUrlQuery &form, std::function<void(int, const QJsonObject &)> done) {
        http.urls << url;
        http.forms << form;
        done(200, http.reply);
    });
}

// Points the pane at the fake server as an Other account, through the controls.
void fillServer(QQuickWindow *sw, FakeImap &server, const QString &toFilter) {
    choose("setting:mail.account", "Other");
    setText(sw, "setting:mail.address", "jane@example.org");
    setText(sw, "setting:mail.host", "127.0.0.1");
    setText(sw, "setting:mail.port", QString::number(server.port()));
    choose("setting:mail.security", "None");
    setText(sw, "setting:mail.user", "jane");
    typePassword(sw, "app-password");
    setText(sw, "setting:mail.toFilter", toFilter);
}

}  // namespace

class E2EMail : public QObject {
    Q_OBJECT

    // Waits for the status line to read this (a substring).
    static void waitStatus(const QString &part, int timeout = 8000) {
        QTRY_VERIFY2_WITH_TIMEOUT(textOf("mailStatus").contains(part), qPrintable("status is \"" + textOf("mailStatus") + "\", wanted " + part), timeout);
    }

private slots:
    void init() {
        for (const char *account : {"mail", "mail-client", "mail-oauth"})
            Keyring::clear("precrime", account);
        QApplication::clipboard()->setText("untouched");
    }
    void cleanup() { QDesktopServices::unsetUrlHandler("https"); }

    // ------------------------------------------------------------- the pane

    // "Settings > Mail" comes after Sync, with Gmail selected and its preset in place.
    void paneComesAfterSyncWithGmailSelected() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        const QStringList panes = sw->property("panes").toStringList();
        QCOMPARE(panes.indexOf("Mail"), panes.indexOf("Sync") + 1);
        selectPane(sw, "Mail");
        QQuickItem *account = nullptr;
        control("setting:mail.account", account);
        QCOMPARE(comboText("setting:mail.account"), QString("Gmail"));
        QVERIFY(!isChecked(findItem("setting:mail.enabled")));
        QCOMPARE(textOf("mailStatus"), QString("Off"));
        // Gmail's own sections are up, the other accounts' are not.
        QVERIFY(findItem("mailPassword"));
        QVERIFY(findItem("mailAppPasswordHelp"));
        QVERIFY(findItem("mailPlusGuide"));
        QVERIFY(!findItem("mailUseDavMail"));
        QVERIFY(!findItem("setting:mail.host"));
        // Filters and what happens afterwards show Gmail's defaults.
        QCOMPARE(comboText("setting:mail.after"), QString("Move to"));
        QCOMPARE(textOf("setting:mail.moveTo"), QString("Precrime"));
        QCOMPARE(textOf("setting:mail.folder"), QString("INBOX"));
        QCOMPARE(comboText("setting:mail.listId"), QString("Inbox"));
        QVERIFY(!isChecked(findItem("setting:mail.naturalLanguage")));
        QCOMPARE(findItem("setting:mail.interval")->property("value").toInt(), 5);
    }

    // "Send or forward mail to jane+precrime@gmail.com (shown live from the address)".
    void plusAddressGuidanceFollowsTheAddressLive() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        QQuickItem *guide = nullptr;
        control("mailPlusGuide", guide);
        QVERIFY(guide->property("text").toString().contains("you+precrime@gmail.com"));   // The example until there is an address.
        setText(sw, "setting:mail.address", "jane@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailPlusGuide").contains("<b>jane+precrime@gmail.com</b>"), 5000);
        QVERIFY(textOf("mailPlusGuide").contains("files it under the Precrime label and out of your inbox"));
        QCOMPARE(textOf("setting:mail.toFilter"), QString("jane+precrime@gmail.com"));   // The filter is prefilled and follows.
        setText(sw, "setting:mail.address", "jane.doe@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailPlusGuide").contains("<b>jane.doe+precrime@gmail.com</b>"), 5000);
        QCOMPARE(textOf("setting:mail.toFilter"), QString("jane.doe+precrime@gmail.com"));
        // Edited by hand, the filter stays put when the address changes.
        setText(sw, "setting:mail.toFilter", "tasks@gmail.com");
        setText(sw, "setting:mail.address", "jane@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailPlusGuide").contains("<b>jane+precrime@gmail.com</b>"), 5000);
        QCOMPARE(textOf("setting:mail.toFilter"), QString("tasks@gmail.com"));
        QCOMPARE(app.store().setting("mail.toFilter").toString(), QString("tasks@gmail.com"));
    }

    void copyButtonPutsTheAddressOnTheClipboard() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        QQuickItem *copy = nullptr;
        control("mailCopyAddress", copy);
        QVERIFY(!copy->isEnabled());   // Nothing to copy until there is an address.
        setText(sw, "setting:mail.address", "jane@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailCopyAddress")->isEnabled(), 5000);
        click(findItem("mailCopyAddress"));
        QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text(), QString("jane+precrime@gmail.com"), 5000);
        // Cleared again, nothing more to copy.
        setText(sw, "setting:mail.address", "");
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("mailCopyAddress")->isEnabled(), 5000);
    }

    // Switching accounts, and back: each shows its own sections and fills its preset.
    void switchingToOutlookAndOtherAndBack() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        setText(sw, "setting:mail.address", "jane@example.org");

        choose("setting:mail.account", "Outlook");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailUseDavMail") != nullptr, 5000);
        QVERIFY(findItem("mailOutlookForward"));
        QVERIFY(findItem("mailOutlookDavmail"));
        QVERIFY(findItem("mailMsAdvanced"));
        QVERIFY(!findItem("mailPlusGuide"));
        QVERIFY(!findItem("mailPassword"));   // Outlook.com has no password route: guidance, not a password box.
        QCOMPARE(app.store().setting("mail.account").toString(), QString("outlook"));
        QCOMPARE(app.store().setting("mail.host").toString(), QString("outlook.office365.com"));
        QCOMPARE(app.store().setting("mail.port").toString(), QString("993"));
        QCOMPARE(app.store().setting("mail.security").toString(), QString("tls"));
        QCOMPARE(comboText("setting:mail.after"), QString("Leave as is"));
        QCOMPARE(textOf("setting:mail.toFilter"), QString());   // Gmail's plus-address filter is left behind.

        choose("setting:mail.account", "Other");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("setting:mail.host") != nullptr, 5000);
        QVERIFY(findItem("setting:mail.port"));
        QVERIFY(findItem("setting:mail.security"));
        QVERIFY(findItem("setting:mail.user"));
        QVERIFY(findItem("mailPassword"));
        QVERIFY(!findItem("mailUseDavMail"));
        QVERIFY(!findItem("mailPlusGuide"));
        QCOMPARE(app.store().setting("mail.account").toString(), QString("other"));

        choose("setting:mail.account", "Gmail");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailPlusGuide") != nullptr, 5000);
        QVERIFY(!findItem("setting:mail.host"));
        QCOMPARE(app.store().setting("mail.host").toString(), QString("imap.gmail.com"));
        QCOMPARE(textOf("setting:mail.toFilter"), QString("jane+precrime@example.org"));
        QCOMPARE(comboText("setting:mail.after"), QString("Move to"));
    }

    // "Use DavMail" switches to Other with 127.0.0.1:1143 and no TLS.
    void useDavMailFillsTheOtherPreset() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        choose("setting:mail.account", "Outlook");
        QQuickItem *davmail = nullptr;
        control("mailUseDavMail", davmail);
        QVERIFY(textOf("mailOutlookDavmail").contains("DavMail"));
        click(davmail);
        QTRY_COMPARE_WITH_TIMEOUT(comboText("setting:mail.account"), QString("Other"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("setting:mail.host") != nullptr, 5000);
        QCOMPARE(textOf("setting:mail.host"), QString("127.0.0.1"));
        QCOMPARE(textOf("setting:mail.port"), QString("1143"));
        QCOMPARE(comboText("setting:mail.security"), QString("None"));
        QCOMPARE(app.store().setting("mail.host").toString(), QString("127.0.0.1"));
        QCOMPARE(app.store().setting("mail.port").toString(), QString("1143"));
        QCOMPARE(app.store().setting("mail.security").toString(), QString("none"));
        // Reversed: Gmail's preset replaces it.
        choose("setting:mail.account", "Gmail");
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("setting:mail.host"), 5000);
        QCOMPARE(app.store().setting("mail.host").toString(), QString("imap.gmail.com"));
        QCOMPARE(app.store().setting("mail.security").toString(), QString("tls"));
    }

    // "Or pipe it in": the commands are there to be selected and copied.
    void pipeHintIsShownAsSelectableText() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        QQuickItem *pipe = nullptr;
        control("mailPipe", pipe);
        QVERIFY2(pipe->width() <= 700, qPrintable(QString("the command text is %1 px wide in a 720 px window").arg(pipe->width())));
        QVERIFY(pipe->property("text").toString().contains("precrime import-mail message.eml"));
        QVERIFY(pipe->property("text").toString().contains("precrime import-mail --maildir"));
        QVERIFY(pipe->property("readOnly").toBool());
        QVERIFY(pipe->property("selectByMouse").toBool());
        QVERIFY(pipe->property("font").value<QFont>().family().contains("mono", Qt::CaseInsensitive));
    }

    // "With every filter empty on a primary mailbox, show a warning line."
    void everyMessageWarning() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        choose("setting:mail.account", "Other");
        QQuickItem *warning = nullptr;
        control("mailUnfilteredWarning", warning);
        QCOMPARE(warning->property("text").toString(), QString("Every message in INBOX will become a task."));
        setText(sw, "setting:mail.folder", "Orders");
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailUnfilteredWarning"), QString("Every message in Orders will become a task."), 5000);
        setText(sw, "setting:mail.toFilter", "me+tasks@example.org");
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("mailUnfilteredWarning"), 5000);
        setText(sw, "setting:mail.toFilter", "");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailUnfilteredWarning") != nullptr, 5000);
        setText(sw, "setting:mail.labelFilter", "precrime");
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("mailUnfilteredWarning"), 5000);
    }

    // ---------------------------------------------------------- the password

    // Gmail's default is an app password, with the steps to make one; it is kept in the keyring.
    void gmailAppPasswordIsTheDefault() {
        UrlSink sink;
        QDesktopServices::setUrlHandler("https", &sink, "opened");
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        QQuickItem *help = nullptr;
        control("mailAppPasswordHelp", help);
        const QString text = help->property("text").toString();
        QVERIFY(text.contains("2-Step Verification"));
        QVERIFY(text.contains("myaccount.google.com/apppasswords"));
        QVERIFY(text.contains("IMAP"));
        QQuickItem *link = nullptr;
        control("mailAppPasswordLink", link);
        click(link);
        QTRY_VERIFY_WITH_TIMEOUT(!sink.urls.isEmpty(), 5000);
        QCOMPARE(sink.urls.last().toString(), QString("https://myaccount.google.com/apppasswords"));

        QQuickItem *field = nullptr;
        control("mailPassword", field);
        QCOMPARE(field->property("echoMode").toInt(), 2);   // TextInput.Password: masked.
        QCOMPARE(field->property("placeholderText").toString(), QString("Not set"));
        QVERIFY(!app.services().mail->hasPassword());
        typePassword(sw, "abcd efgh ijkl mnop");
        QTRY_VERIFY_WITH_TIMEOUT(app.services().mail->hasPassword(), 5000);
        QCOMPARE(Keyring::lookup("precrime", "mail"), QString("abcd efgh ijkl mnop"));
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailPassword"), QString(), 5000);   // The field never shows it again.
        QTRY_COMPARE_WITH_TIMEOUT(findItem("mailPassword")->property("placeholderText").toString(), QString("Saved in your keyring"), 5000);
        // Not in the database.
        QFile db(app.store().path());
        QVERIFY(db.open(QIODevice::ReadOnly));
        QVERIFY(!db.readAll().contains("abcd efgh"));
        // Forget removes it.
        QQuickItem *forget = nullptr;
        control("mailPasswordClear", forget);
        click(forget);
        QTRY_VERIFY_WITH_TIMEOUT(!app.services().mail->hasPassword(), 5000);
        QVERIFY(Keyring::lookup("precrime", "mail").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(findItem("mailPassword")->property("placeholderText").toString(), QString("Not set"), 5000);
    }

    // ------------------------------------------- every field, applied and removed

    // Each Mail setting, through its own control: stored on change, still there after a restart,
    // and gone again when put back. Settings that need a browser (the OAuth client and sign-in) have
    // their own tests below. The table at the bottom of everyMailSettingHasACase names them all.
    void everyFieldPersistsAndComesBack() {
        FakeImap server;   // A listening server so that turning the switch on has somewhere to connect.
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openMailPane(*s.app, sw);

        // Applied.
        choose("setting:mail.account", "Other");
        setText(sw, "setting:mail.address", "jane@example.org");
        setText(sw, "setting:mail.host", "127.0.0.1");
        setText(sw, "setting:mail.port", QString::number(server.port()));
        choose("setting:mail.security", "STARTTLS");
        setText(sw, "setting:mail.user", "jane-login");
        setText(sw, "setting:mail.toFilter", "me+tasks@example.org");
        setText(sw, "setting:mail.labelFilter", "precrime");
        setText(sw, "setting:mail.folder", "Orders");
        choose("setting:mail.after", "Mark as read");
        choose("setting:mail.listId", "Work");
        setCheck("setting:mail.naturalLanguage", true);
        setSpin(sw, "setting:mail.interval", 12);
        choose("setting:mail.after", "Move to");
        setText(sw, "setting:mail.moveTo", "Tasks");
        setCheck("setting:mail.enabled", true);
        auto st = [&]() -> Store & { return s.app->store(); };   // The app is rebuilt by restart(): never hold on to its store.
        QCOMPARE(st().setting("mail.account").toString(), QString("other"));
        QCOMPARE(st().setting("mail.address").toString(), QString("jane@example.org"));
        QCOMPARE(st().setting("mail.host").toString(), QString("127.0.0.1"));
        QCOMPARE(st().setting("mail.port").toString(), QString::number(server.port()));
        QCOMPARE(st().setting("mail.security").toString(), QString("starttls"));
        QCOMPARE(st().setting("mail.user").toString(), QString("jane-login"));
        QCOMPARE(st().setting("mail.toFilter").toString(), QString("me+tasks@example.org"));
        QCOMPARE(st().setting("mail.labelFilter").toString(), QString("precrime"));
        QCOMPARE(st().setting("mail.folder").toString(), QString("Orders"));
        QCOMPARE(st().setting("mail.after").toString(), QString("move"));
        QCOMPARE(st().setting("mail.moveTo").toString(), QString("Tasks"));
        QCOMPARE(st().setting("mail.listId").toLongLong(), s.ids.work);
        QCOMPARE(st().setting("mail.naturalLanguage").toString(), QString("true"));
        QCOMPARE(st().setting("mail.interval").toString(), QString("12"));
        QCOMPARE(st().setting("mail.enabled").toString(), QString("true"));

        // Quit and relaunch: all still set, in the controls.
        QVERIFY(s.restart());
        openMailPane(*s.app, sw);
        QQuickItem *probe = nullptr;
        control("setting:mail.host", probe);
        QCOMPARE(comboText("setting:mail.account"), QString("Other"));
        QCOMPARE(textOf("setting:mail.address"), QString("jane@example.org"));
        QCOMPARE(textOf("setting:mail.host"), QString("127.0.0.1"));
        QCOMPARE(textOf("setting:mail.port"), QString::number(server.port()));
        QCOMPARE(comboText("setting:mail.security"), QString("STARTTLS"));
        QCOMPARE(textOf("setting:mail.user"), QString("jane-login"));
        QCOMPARE(textOf("setting:mail.toFilter"), QString("me+tasks@example.org"));
        QCOMPARE(textOf("setting:mail.labelFilter"), QString("precrime"));
        QCOMPARE(textOf("setting:mail.folder"), QString("Orders"));
        QCOMPARE(comboText("setting:mail.after"), QString("Move to"));
        QCOMPARE(textOf("setting:mail.moveTo"), QString("Tasks"));
        QCOMPARE(comboText("setting:mail.listId"), QString("Work"));
        QVERIFY(isChecked(findItem("setting:mail.naturalLanguage")));
        QCOMPARE(findItem("setting:mail.interval")->property("value").toInt(), 12);
        QVERIFY(isChecked(findItem("setting:mail.enabled")));

        // Put back. (The password has its own test; here the rest goes back to what it was.)
        setCheck("setting:mail.enabled", false);
        setCheck("setting:mail.naturalLanguage", false);
        setSpin(sw, "setting:mail.interval", 5);
        choose("setting:mail.listId", "Inbox");
        clearField(sw, "setting:mail.moveTo");   // (Only shown while "Move to" is chosen.)
        QTRY_COMPARE_WITH_TIMEOUT(textOf("setting:mail.moveTo"), QString("Precrime"), 5000);   // Emptied: the default again.
        choose("setting:mail.after", "Leave as is");
        clearField(sw, "setting:mail.folder");
        setText(sw, "setting:mail.labelFilter", "");
        setText(sw, "setting:mail.toFilter", "");
        setText(sw, "setting:mail.user", "");
        choose("setting:mail.security", "TLS");
        clearField(sw, "setting:mail.port");
        setText(sw, "setting:mail.host", "");
        setText(sw, "setting:mail.address", "");
        QCOMPARE(st().setting("mail.enabled").toString(), QString("false"));
        QCOMPARE(st().setting("mail.naturalLanguage").toString(), QString("false"));
        QCOMPARE(st().setting("mail.interval").toString(), QString("5"));
        QCOMPARE(st().setting("mail.listId").toString(), QString("0"));
        QCOMPARE(st().setting("mail.after").toString(), QString("leave"));
        QCOMPARE(st().setting("mail.folder").toString(), QString());
        QCOMPARE(st().setting("mail.labelFilter").toString(), QString());
        QCOMPARE(st().setting("mail.toFilter").toString(), QString());
        QCOMPARE(st().setting("mail.address").toString(), QString());
        QCOMPARE(st().setting("mail.security").toString(), QString("tls"));
        // An emptied folder field means the default again, in the pane and for the connector.
        QCOMPARE(textOf("setting:mail.folder"), QString("INBOX"));
        QCOMPARE(s.app->services().mail->option("mail.folder"), QString("INBOX"));
        QCOMPARE(s.app->services().mail->state(), MailFetch::State::Off);
    }

    // The guard, as for the rest of Settings: every key the connector knows has a test here.
    void everyMailSettingHasACase() {
        const QMap<QString, QString> cases{
            {"mail.enabled", "everyFieldPersistsAndComesBack"},
            {"mail.account", "everyFieldPersistsAndComesBack"},
            {"mail.address", "everyFieldPersistsAndComesBack"},
            {"mail.host", "everyFieldPersistsAndComesBack"},
            {"mail.port", "everyFieldPersistsAndComesBack"},
            {"mail.security", "everyFieldPersistsAndComesBack"},
            {"mail.user", "everyFieldPersistsAndComesBack"},
            {"mail.folder", "everyFieldPersistsAndComesBack"},
            {"mail.toFilter", "everyFieldPersistsAndComesBack"},
            {"mail.labelFilter", "everyFieldPersistsAndComesBack"},
            {"mail.after", "everyFieldPersistsAndComesBack"},
            {"mail.moveTo", "everyFieldPersistsAndComesBack"},
            {"mail.listId", "everyFieldPersistsAndComesBack"},
            {"mail.naturalLanguage", "everyFieldPersistsAndComesBack"},
            {"mail.interval", "everyFieldPersistsAndComesBack"},
            {"mail.googleClientId", "importClientJsonThenSignInWithGoogle"},
            {"mail.msClientId", "yourOwnClientIdThenSignInWithMicrosoft"},
            {"mail.oauth", "importClientJsonThenSignInWithGoogle"},
        };
        const QStringList keys = MailFetch::optionKeys();
        for (const QString &key : keys) {
            QVERIFY2(cases.contains(key), qPrintable("mail setting with no test: " + key));
            QVERIFY2(metaObject()->indexOfSlot(cases.value(key).toLatin1() + "()") >= 0, qPrintable(cases.value(key) + " does not exist"));
        }
        for (const QString &key : cases.keys())
            QVERIFY2(keys.contains(key), qPrintable(key + " is tested but unknown to the connector"));
        // Every key that has a control in the pane is actually named in SettingsWindow.qml.
        QFile qml(":/qml/SettingsWindow.qml");
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(qml.readAll());
        for (const QString &key : keys)
            if (key != "mail.googleClientId" && key != "mail.oauth")
                QVERIFY2(text.contains("\"" + key + "\""), qPrintable(key + " has no control in SettingsWindow.qml"));
    }

    // ---------------------------------------------------- the connector at work

    // The point of it all: matching mail becomes tasks in the chosen list, nothing else is touched,
    // and the status line follows what happens.
    void fetchNowImportsMatchingMailAndLeavesTheRest() {
        FakeImap server;
        server.add("INBOX", plusMail("Book the venue", "e1"));
        server.add("INBOX", FakeImap::mail("bob@example.org", "Lunch on Friday?", "b", "e2"));
        server.add("INBOX", plusMail("Send the invoice", "e3"));
        server.add("INBOX", plusMail("Not for me", "e4", "jane+precrime@example.org.au"));
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+precrime@example.org");
        choose("setting:mail.after", "Leave as is");
        choose("setting:mail.listId", "Work");
        QCOMPARE(textOf("mailStatus"), QString("Off"));
        clickFetchNow();
        // Fetch Now turns the connector on when it was off, and then it reports.
        waitStatus("Connected · checked ");
        QVERIFY2(textOf("mailStatus").endsWith("· 2 imported"), qPrintable(textOf("mailStatus")));
        QVERIFY(isChecked(findItem("setting:mail.enabled")));
        QCOMPARE(app.store().setting("mail.enabled").toString(), QString("true"));
        const Task venue = app.taskTitled("Book the venue"), invoice = app.taskTitled("Send the invoice");
        QVERIFY(venue.id && invoice.id);
        QCOMPARE(venue.listId, app.ids.work);
        QCOMPARE(invoice.listId, app.ids.work);
        QVERIFY(venue.uid.startsWith("mail-"));
        QVERIFY(!app.taskTitled("Lunch on Friday?").id);
        QVERIFY(!app.taskTitled("Not for me").id);
        // Mail that was not for Precrime is exactly as it was.
        QCOMPARE(server.count("INBOX"), 4);
        for (qint64 uid : {2, 4})
            QVERIFY(server.find("INBOX", uid)->flags.isEmpty());
        // And the tasks are on screen in their list.
        showSource(app, listKey(app.ids.work));
        QTRY_VERIFY_WITH_TIMEOUT(rowTitle(app, venue.id) != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowTitle(app, invoice.id) != nullptr, 5000);

        // New mail, another Fetch Now: the status moves on.
        openMailPane(app, sw);
        server.add("INBOX", plusMail("Water the office plants", "e5"));
        clickFetchNow();
        QTRY_VERIFY2_WITH_TIMEOUT(textOf("mailStatus").endsWith("· 3 imported"), qPrintable(textOf("mailStatus")), 8000);
        QVERIFY(app.taskTitled("Water the office plants").id);

        // A dropped connection shows as an error, in the same place, and not as a success.
        server.dropConnections();
        QTRY_VERIFY2_WITH_TIMEOUT(!textOf("mailStatus").startsWith("Connected"), qPrintable(textOf("mailStatus")), 8000);
        QVERIFY2(textOf("mailStatus").contains("lost"), qPrintable(textOf("mailStatus")));

        // Switched off, the status says so and the connector hangs up.
        setCheck("setting:mail.enabled", false);
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailStatus"), QString("Off"), 5000);
    }

    // the reference app manual: "Move to" is the Gmail default; the mail goes under the Precrime label and out
    // of the inbox, and a message that was already a task is moved all the same.
    void movedMailLeavesTheInbox() {
        FakeImap server;
        server.gmail = true;
        server.add("INBOX", plusMail("Call the plumber", "m1"));
        server.add("INBOX", FakeImap::mail("friend@example.org", "Holiday photos", "b", "m2"));
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+precrime@example.org");
        choose("setting:mail.after", "Move to");
        QCOMPARE(textOf("setting:mail.moveTo"), QString("Precrime"));
        clickFetchNow();
        waitStatus("Connected");
        QVERIFY(app.taskTitled("Call the plumber").id);
        QTRY_COMPARE_WITH_TIMEOUT(server.count("Precrime"), 1, 5000);
        QCOMPARE(server.count("INBOX"), 1);   // Only the friend's mail is left.
        QVERIFY(server.find("INBOX", 2));
    }

    // Marking as read leaves the mail where it is and skips it next time.
    void markAsReadLeavesTheMailInPlace() {
        FakeImap server;
        server.add("INBOX", plusMail("Renew the passport", "r1"));
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+precrime@example.org");
        choose("setting:mail.after", "Mark as read");
        clickFetchNow();
        waitStatus("· 1 imported");
        QVERIFY(app.taskTitled("Renew the passport").id);
        QVERIFY(server.hasFlag("INBOX", 1, "\\Seen"));
        QCOMPARE(server.count("INBOX"), 1);
    }

    void aWrongPasswordIsShownInTheStatusLine() {
        FakeImap server;
        server.add("INBOX", plusMail("One", "w1"));
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+precrime@example.org");
        choose("setting:mail.after", "Leave as is");
        typePassword(sw, "wrong");
        clickFetchNow();
        waitStatus("refused the sign-in");
        QVERIFY(textOf("mailStatus").contains("Invalid credentials"));
        QVERIFY(!app.taskTitled("One").id);
        // The right one, and it recovers by itself.
        typePassword(sw, "app-password");
        waitStatus("Connected", 10000);
        QVERIFY(app.taskTitled("One").id);
    }

    // "Natural language" is off by default; the reference app's own due(), list() and the rest always work.
    void naturalLanguageSwitchControlsDatesInTheSubject() {
        FakeImap server;
        server.add("INBOX", plusMail("Pay the rent tomorrow", "n1"));
        server.add("INBOX", plusMail("Pay the bill due(2026-12-24)", "n2"));
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+precrime@example.org");
        choose("setting:mail.after", "Leave as is");
        clickFetchNow();
        waitStatus("· 2 imported");
        QCOMPARE(app.taskTitled("Pay the rent tomorrow").id != 0, true);   // The words stay in the title,
        QVERIFY(!app.taskTitled("Pay the rent tomorrow").dueDate.isValid());   // with no date.
        QCOMPARE(app.taskTitled("Pay the bill").dueDate, QDate(2026, 12, 24));   // due() always works.
        // Switched on, a new message is read the way Quick Entry reads it.
        setCheck("setting:mail.naturalLanguage", true);
        server.add("INBOX", plusMail("Buy flowers tomorrow", "n3"));
        QTest::qWait(900);   // Past the settings debounce, so the connector has reconnected with the switch.
        clickFetchNow();
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Buy flowers").id != 0, 10000);
        QCOMPARE(app.taskTitled("Buy flowers").dueDate, QDate::currentDate().addDays(1));
    }

    // Ctrl+S and the sidebar's sync button fetch mail too, like the other providers.
    void manualSyncAlsoFetchesMail() {
        FakeImap server;
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+precrime@example.org");
        choose("setting:mail.after", "Leave as is");
        setCheck("setting:mail.enabled", true);
        waitStatus("Connected");
        closeSettings(app);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("syncButton") != nullptr, 5000);   // Mail alone makes the sync button appear.
        server.add("INBOX", plusMail("Arrived quietly", "s1"));
        activate(app.window);
        app.press(Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Arrived quietly").id != 0, 8000);
        server.add("INBOX", plusMail("Arrived again", "s2"));
        click(findItem("syncButton"));
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Arrived again").id != 0, 8000);
    }

    // ------------------------------------------------------------- own OAuth client

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
        click(advanced);
        QQuickItem *import = nullptr;
        control("mailImportClient", import);
        QCOMPARE(textOf("mailClientStatus"), QString("No client imported"));

        // Signing in before there is a client says what to do.
        QQuickItem *signIn = nullptr;
        control("mailSignIn", signIn);
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
        emit app.services().filePicker.picked(writeFile(dir, "client_secret.json", GoogleClient), "mail-client-json");
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
        click(advanced);
        // The client is set up directly: importing it is not what this test is about.
        QCOMPARE(app.services().mail->importClientJson(writeFile(QFileInfo(app.store().path()).absolutePath(), "c.json", GoogleClient)), QString());
        QQuickItem *signIn = nullptr;
        control("mailSignIn", signIn);
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
        click(advanced);
        QQuickItem *idField = nullptr;
        control("setting:mail.msClientId", idField);

        // No client ID yet: it says so.
        QQuickItem *signIn = nullptr;
        control("mailSignIn", signIn);
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

REGISTER_TEST(E2EMail)

#include "test_mail.moc"
