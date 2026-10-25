// Mail to Fairfax, from the user's side: Settings > Mail, and the connector
// against a fake IMAP server on 127.0.0.1 (tests/fakeimap.h). Fairfax
// reaches a mailbox you own rather than a hosted address, so these tests cover
// what becomes a task and follow the Mail spec for how mail arrives.
//
// Nothing here touches the network beyond the loopback or the real keyring (the
// keyring is in memory, see main.cpp), and the OAuth HTTP calls go through
// MailFetch::setHttpTransport.

#include <QClipboard>
#include <QDesktopServices>
#include <QJsonObject>

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

class E2EMail : public QObject {
    Q_OBJECT

private slots:
    void init() {
        for (const char *account : {"mail", "mail-client", "mail-oauth"})
            Keyring::clear("fairfax", account);
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
        QVERIFY(account);
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
        QCOMPARE(textOf("setting:mail.moveTo"), QString("Fairfax"));
        QCOMPARE(textOf("setting:mail.folder"), QString("INBOX"));
        QCOMPARE(comboText("setting:mail.listId"), QString("Inbox"));
        QVERIFY(!isChecked(findItem("setting:mail.naturalLanguage")));
        QCOMPARE(findItem("setting:mail.interval")->property("value").toInt(), 5);
    }

    // "Send or forward mail to jane+fairfax@gmail.com (shown live from the address)".
    void plusAddressGuidanceFollowsTheAddressLive() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        QQuickItem *guide = nullptr;
        control("mailPlusGuide", guide);
        QVERIFY(guide);
        QVERIFY(guide->property("text").toString().contains("you+fairfax@gmail.com"));    // The example until there is an address.
        setText(sw, "setting:mail.address", "jane@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailPlusGuide").contains("<b>jane+fairfax@gmail.com</b>"), 5000);
        QVERIFY(textOf("mailPlusGuide").contains("files it under the Fairfax label and out of your inbox"));
        QCOMPARE(textOf("setting:mail.toFilter"), QString("jane+fairfax@gmail.com"));    // The filter is prefilled and follows.
        setText(sw, "setting:mail.address", "jane.doe@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailPlusGuide").contains("<b>jane.doe+fairfax@gmail.com</b>"), 5000);
        QCOMPARE(textOf("setting:mail.toFilter"), QString("jane.doe+fairfax@gmail.com"));
        // Edited by hand, the filter stays put when the address changes.
        setText(sw, "setting:mail.toFilter", "tasks@gmail.com");
        setText(sw, "setting:mail.address", "jane@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(textOf("mailPlusGuide").contains("<b>jane+fairfax@gmail.com</b>"), 5000);
        QCOMPARE(textOf("setting:mail.toFilter"), QString("tasks@gmail.com"));
        QCOMPARE(app.store().setting("mail.toFilter").toString(), QString("tasks@gmail.com"));
    }

    void copyButtonPutsTheAddressOnTheClipboard() {
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        QQuickItem *copy = nullptr;
        control("mailCopyAddress", copy);
        QVERIFY(copy);
        QVERIFY(!copy->isEnabled());   // Nothing to copy until there is an address.
        setText(sw, "setting:mail.address", "jane@gmail.com");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailCopyAddress")->isEnabled(), 5000);
        click(findItem("mailCopyAddress"));
        QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text(), QString("jane+fairfax@gmail.com"), 5000);
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
        QCOMPARE(textOf("setting:mail.toFilter"), QString("jane+fairfax@example.org"));
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
        QVERIFY(davmail);
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
        QVERIFY(pipe);
        QVERIFY2(pipe->width() <= 700, qPrintable(QString("the command text is %1 px wide in a 720 px window").arg(pipe->width())));
        QVERIFY(pipe->property("text").toString().contains("fairfax import-mail message.eml"));
        QVERIFY(pipe->property("text").toString().contains("fairfax import-mail --maildir"));
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
        QVERIFY(warning);
        QCOMPARE(warning->property("text").toString(), QString("Every message in INBOX will become a task."));
        setText(sw, "setting:mail.folder", "Orders");
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailUnfilteredWarning"), QString("Every message in Orders will become a task."), 5000);
        setText(sw, "setting:mail.toFilter", "me+tasks@example.org");
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("mailUnfilteredWarning"), 5000);
        setText(sw, "setting:mail.toFilter", "");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("mailUnfilteredWarning") != nullptr, 5000);
        setText(sw, "setting:mail.labelFilter", "fairfax");
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
        QVERIFY(help);
        const QString text = help->property("text").toString();
        QVERIFY(text.contains("2-Step Verification"));
        QVERIFY(text.contains("myaccount.google.com/apppasswords"));
        QVERIFY(text.contains("IMAP"));
        QQuickItem *link = nullptr;
        control("mailAppPasswordLink", link);
        QVERIFY(link);
        click(link);
        QTRY_VERIFY_WITH_TIMEOUT(!sink.urls.isEmpty(), 5000);
        QCOMPARE(sink.urls.last().toString(), QString("https://myaccount.google.com/apppasswords"));

        QQuickItem *field = nullptr;
        control("mailPassword", field);
        QVERIFY(field);
        QCOMPARE(field->property("echoMode").toInt(), 2);   // TextInput.Password: masked.
        QCOMPARE(field->property("placeholderText").toString(), QString("Not set"));
        QVERIFY(!app.services().mail->hasPassword());
        typePassword(sw, "abcd efgh ijkl mnop");
        QTRY_VERIFY_WITH_TIMEOUT(app.services().mail->hasPassword(), 5000);
        QCOMPARE(Keyring::lookup("fairfax", "mail"), QString("abcd efgh ijkl mnop"));
        QTRY_COMPARE_WITH_TIMEOUT(textOf("mailPassword"), QString(), 5000);   // The field never shows it again.
        QTRY_COMPARE_WITH_TIMEOUT(findItem("mailPassword")->property("placeholderText").toString(), QString("Saved in your keyring"), 5000);
        // Not in the database.
        QFile db(app.store().path());
        QVERIFY(db.open(QIODevice::ReadOnly));
        QVERIFY(!db.readAll().contains("abcd efgh"));
        // Forget removes it.
        QQuickItem *forget = nullptr;
        control("mailPasswordClear", forget);
        QVERIFY(forget);
        click(forget);
        QTRY_VERIFY_WITH_TIMEOUT(!app.services().mail->hasPassword(), 5000);
        QVERIFY(Keyring::lookup("fairfax", "mail").isEmpty());
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
        setText(sw, "setting:mail.labelFilter", "fairfax");
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
        QCOMPARE(st().setting("mail.labelFilter").toString(), QString("fairfax"));
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
        QVERIFY(probe);
        QCOMPARE(comboText("setting:mail.account"), QString("Other"));
        QCOMPARE(textOf("setting:mail.address"), QString("jane@example.org"));
        QCOMPARE(textOf("setting:mail.host"), QString("127.0.0.1"));
        QCOMPARE(textOf("setting:mail.port"), QString::number(server.port()));
        QCOMPARE(comboText("setting:mail.security"), QString("STARTTLS"));
        QCOMPARE(textOf("setting:mail.user"), QString("jane-login"));
        QCOMPARE(textOf("setting:mail.toFilter"), QString("me+tasks@example.org"));
        QCOMPARE(textOf("setting:mail.labelFilter"), QString("fairfax"));
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
        QTRY_COMPARE_WITH_TIMEOUT(textOf("setting:mail.moveTo"), QString("Fairfax"), 5000);    // Emptied: the default again.
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
            {"mail.enabled", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.account", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.address", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.host", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.port", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.security", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.user", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.folder", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.toFilter", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.labelFilter", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.after", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.moveTo", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.listId", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.naturalLanguage", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.interval", "E2EMail::everyFieldPersistsAndComesBack"},
            {"mail.googleClientId", "E2EMailOAuth::importClientJsonThenSignInWithGoogle"},
            {"mail.msClientId", "E2EMailOAuth::yourOwnClientIdThenSignInWithMicrosoft"},
            {"mail.oauth", "E2EMailOAuth::importClientJsonThenSignInWithGoogle"},
        };
        const QStringList keys = MailFetch::optionKeys();
        for (const QString &key : keys) {
            QVERIFY2(cases.contains(key), qPrintable("mail setting with no test: " + key));
            // The test named must exist (in this class or the sign-in one).
            const QString how = cases.value(key);
            bool exists = false;
            for (const auto &factory : testRegistry()) {
                std::unique_ptr<QObject> test(factory());
                if (how.section("::", 0, 0) == test->metaObject()->className() && test->metaObject()->indexOfSlot(qPrintable(how.section("::", 1) + "()")) >= 0)
                    exists = true;
            }
            QVERIFY2(exists, qPrintable(how + " does not exist"));
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
        server.add("INBOX", plusMail("Not for me", "e4", "jane+fairfax@example.org.au"));
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+fairfax@example.org");
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
        // Mail that was not for Fairfax is exactly as it was.
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

    // "Move to" is the Gmail default; the mail goes under the Fairfax label and out
    // of the inbox, and a message that was already a task is moved all the same.
    void movedMailLeavesTheInbox() {
        FakeImap server;
        server.gmail = true;
        server.add("INBOX", plusMail("Call the plumber", "m1"));
        server.add("INBOX", FakeImap::mail("friend@example.org", "Holiday photos", "b", "m2"));
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+fairfax@example.org");
        choose("setting:mail.after", "Move to");
        QCOMPARE(textOf("setting:mail.moveTo"), QString("Fairfax"));
        clickFetchNow();
        waitStatus("Connected");
        QVERIFY(app.taskTitled("Call the plumber").id);
        QTRY_COMPARE_WITH_TIMEOUT(server.count("Fairfax"), 1, 5000);
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
        fillServer(sw, server, "jane+fairfax@example.org");
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
        fillServer(sw, server, "jane+fairfax@example.org");
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

    // "Natural language" is off by default; the due(), list() and the rest always work.
    void naturalLanguageSwitchControlsDatesInTheSubject() {
        FakeImap server;
        server.add("INBOX", plusMail("Pay the rent tomorrow", "n1"));
        server.add("INBOX", plusMail("Pay the bill due(2026-12-24)", "n2"));
        App app;
        QQuickWindow *sw = nullptr;
        openMailPane(app, sw);
        fillServer(sw, server, "jane+fairfax@example.org");
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
        fillServer(sw, server, "jane+fairfax@example.org");
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
};

REGISTER_TEST(E2EMail)

#include "test_mail.moc"
