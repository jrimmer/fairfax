// CalDAV sync from the user's side: Settings > Sync and the provider against
// the fake CalDAV server (tests/fakecaldav.h) over plain HTTP on 127.0.0.1.
// The provider is created by the real AppServices, so these tests drive
// exactly what ships. Nothing here touches the network beyond the loopback or
// the real keyring (the keyring is in memory, see main.cpp).

#include "settingskit.h"
#include "mailkit.h"
#include "../fakecaldav.h"
#include "../testregistry.h"
#include "keyring.h"
#include "vtodo.h"

using namespace e2e;

namespace {

// Opens Settings on the Sync pane, fills in the fake server and connects.
// `quiet` connects without a sync so a test can drive the first cycle itself.
void connectCalDav(App &app, FakeCalDav &server, bool quiet = false, const QString &password = "app-password") {
    app.services().caldavSync->setQuietConnect(quiet);
    // Automatic sync would fire its own debounced run while a test is driving
    // the pane, moving the controls under the clicks; every run here is
    // started explicitly.
    app.store().setSetting("sync.auto", false);
    QQuickWindow *sw = nullptr;
    openSettings(app, sw);
    selectPane(sw, "Sync");
    choose("setting:caldav.account", "Other");
    setText(sw, "setting:caldav.url", server.baseUrl().toString().chopped(1));
    setText(sw, "setting:caldav.user", server.user);
    QQuickItem *field = nullptr;
    control("setting:caldav.password", field);
    click(field);
    typeInto(sw, password);
    QQuickItem *button = nullptr;
    control("caldavConnect", button);
    QVERIFY(button);
    QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
    click(button);
    QTRY_VERIFY_WITH_TIMEOUT(app.services().caldavSync->configured(), 5000);
    if (!quiet)
        QTRY_VERIFY_WITH_TIMEOUT(!app.services().caldavSync->busy(), 20000);
}

// Clicks Sync Now and waits for the cycle to end.
void syncNow(App &app) {
    QQuickItem *button = nullptr;
    control("caldavSyncNow", button);
    QVERIFY(button);
    click(button);
    // The click can be lost while the pane is still settling; wait until the
    // run it starts really begins, then for that run to finish.
    QTRY_VERIFY_WITH_TIMEOUT(app.services().caldavSync->busy(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!app.services().caldavSync->busy(), 20000);
}

FakeCalDav::Collection *collectionNamed(FakeCalDav &server, const QString &name) {
    for (FakeCalDav::Collection &c : server.collections())
        if (c.displayName == name)
            return &c;
    return nullptr;
}

// Whether any calendar on the server holds a resource whose body contains
// `text`.
bool serverHasText(FakeCalDav &server, const QString &text) {
    for (const FakeCalDav::Collection &c : server.collections())
        for (const QByteArray &body : c.resources)
            if (QString::fromUtf8(body).contains(text))
                return true;
    return false;
}

int totalRequests(FakeCalDav &server) {
    int n = 0;
    for (auto it = server.requests.begin(); it != server.requests.end(); ++it)
        n += it.value();
    return n;
}

int liveTasks(const Store &store) {
    int n = 0;
    for (const Task &t : store.tasks())
        if (!t.deleted)
            ++n;
    return n;
}

// A VTODO a second client would write.
const char *OtherClientTask() {
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Other//EN\r\nBEGIN:VTODO\r\nUID:other-1\r\n"
           "SUMMARY:Written elsewhere\r\nLAST-MODIFIED:20300101T000000Z\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";
}

void fastTimings(App &app) {
    app.services().caldavSync->setTimings(CalDavClient::Timings{2000, 10, 50, 3});
}

}  // namespace

class E2ECalDav : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeCalDav> m_server;

private slots:
    void init() {
        Keyring::clear("fairfax", "caldav");
        m_server = std::make_unique<FakeCalDav>();
    }
    void cleanup() { m_server.reset(); }

    // 1. Connect with Other, URL, user and password: the status shows
    // connected and the lists table appears.
    void connectShowsStatusAndListsTable() {
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        fastTimings(app);
        connectCalDav(app, *m_server, true);
        QCOMPARE(textOf("caldavStatus"), QString("Connected"));
        QVERIFY(findItem("caldavMap:" + QString::number(app.ids.home)));
        QVERIFY(findItem("caldavMap:" + QString::number(app.ids.work)));
        QCOMPARE(app.store().setting("caldav.url").toString(), m_server->baseUrl().toString().chopped(1));
        QCOMPARE(app.store().setting("caldav.user").toString(), m_server->user);
        QVERIFY(!Keyring::lookup("fairfax", "caldav").isEmpty());
    }

    // 2. A first sync with an empty server makes a calendar per list and
    // uploads the tasks.
    void firstSyncCreatesCalendarsAndUploads() {
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        fastTimings(app);
        connectCalDav(app, *m_server, true);
        syncNow(app);

        for (const QString &name : {QStringLiteral("Home"), QStringLiteral("Work")}) {
            FakeCalDav::Collection *c = collectionNamed(*m_server, name);
            QVERIFY2(c, qPrintable("no calendar named " + name));
            QVERIFY(!c->resources.isEmpty());
        }
        QVERIFY(serverHasText(*m_server, "Buy milk"));
        QVERIFY(serverHasText(*m_server, "Write status report"));
    }

    // 3. A local edit reaches the server, and a resource another client wrote
    // reaches the app on Sync Now.
    void editsTravelBothWays() {
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        fastTimings(app);
        connectCalDav(app, *m_server, true);
        syncNow(app);

        // Local edit.
        const qint64 id = app.ids.buyMilk;
        const QString href = app.store().syncLinks("caldav", "tasks").value(id).remoteId;
        QVERIFY(!href.isEmpty());
        editTask(app.store(), id, [](Task &t) { t.title = "Buy oat milk"; t.modified = QDateTime::currentDateTime().addSecs(5); });
        syncNow(app);
        QVERIFY(serverHasText(*m_server, "Buy oat milk"));

        // A write from another client, pulled on the next sync.
        const QString homeHref = app.store().syncLinks("caldav", "lists").value(app.ids.home).remoteId;
        m_server->editFromAnotherClient(homeHref, QStringLiteral("other.ics"), OtherClientTask());
        syncNow(app);
        QVERIFY(app.taskTitled("Written elsewhere").id);
    }

    // 4. "Don't sync" unmaps a list: its tasks stay local and its calendar is
    // left alone.
    void dontSyncLeavesTheCalendarAndTasks() {
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        fastTimings(app);
        connectCalDav(app, *m_server, true);
        syncNow(app);

        const qint64 home = app.ids.home;
        QVERIFY(!app.store().syncLinks("caldav", "lists").value(home).remoteId.isEmpty());
        FakeCalDav::Collection *c = collectionNamed(*m_server, "Home");
        QVERIFY(c);
        const int before = c->resources.size();

        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Sync");
        choose("caldavMap:" + QString::number(home), "Don't sync");
        QTRY_VERIFY_WITH_TIMEOUT(app.store().syncLinks("caldav", "lists").value(home).remoteId.isEmpty(), 5000);
        // Only CalDAV skips it: the other providers keep syncing the list.
        QVERIFY(app.services().caldavSync->isListSkipped(home));
        QVERIFY(!SyncTree::isListExcluded(app.store(), home));

        addTask(app.store(), home, "Should stay local");
        syncNow(app);
        QVERIFY(app.taskTitled("Should stay local").id);
        c = collectionNamed(*m_server, "Home");
        QVERIFY2(c, "the calendar must be left alone");
        QCOMPARE(c->resources.size(), before);
        QVERIFY(!serverHasText(*m_server, "Should stay local"));
    }

    // 5. A wrong password is shown in the status line.
    void aWrongPasswordIsShown() {
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        fastTimings(app);
        connectCalDav(app, *m_server, false, "wrong-password");
        QTRY_VERIFY2_WITH_TIMEOUT(app.services().caldavSync->status().contains("rejected"),
                                  qPrintable(app.services().caldavSync->status()), 15000);
    }

    // 6. Ctrl+S syncs CalDAV too: more requests reach the server and a new
    // task goes up.
    void ctrlSIncludesCalDav() {
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        fastTimings(app);
        connectCalDav(app, *m_server, true);
        syncNow(app);

        const int before = totalRequests(*m_server);
        addTask(app.store(), app.ids.home, "Sent by Ctrl S");
        closeSettings(app);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("syncButton") != nullptr, 5000);
        activate(app.window);
        app.press(Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(totalRequests(*m_server) > before, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(serverHasText(*m_server, "Sent by Ctrl S"), 5000);
    }

    // 7. Each field through its control, across a restart, and back.
    void everyFieldPersistsAndComesBack() {
        Session s;
        s.withSync = true;
        QVERIFY(s.start());
        App *app = s.app.get();
        fastTimings(*app);
        app->store().setSetting("sync.auto", false);
        app->services().caldavSync->setQuietConnect(true);
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Sync");

        choose("setting:caldav.account", "Nextcloud");
        setText(sw, "setting:caldav.url", m_server->baseUrl().toString().chopped(1));
        setText(sw, "setting:caldav.user", "jane");
        setCheck("setting:caldav.createCalendars", false);
        QQuickItem *field = nullptr;
        control("setting:caldav.password", field);
        click(field);
        typeInto(sw, "app-password");
        QQuickItem *button = nullptr;
        control("caldavConnect", button);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        click(button);
        QTRY_VERIFY_WITH_TIMEOUT(app->services().caldavSync->configured(), 5000);
        QCOMPARE(app->store().setting("caldav.account").toString(), QString("nextcloud"));
        QCOMPARE(app->store().setting("caldav.url").toString(), m_server->baseUrl().toString().chopped(1));
        QCOMPARE(app->store().setting("caldav.user").toString(), QString("jane"));
        QCOMPARE(app->store().setting("caldav.createCalendars").toString(), QString("false"));
        QVERIFY(!Keyring::lookup("fairfax", "caldav").isEmpty());

        // Relaunch: the fields and the keyring entry come back.
        QVERIFY(s.restart());
        app = s.app.get();
        fastTimings(*app);
        app->store().setSetting("sync.auto", false);
        openSettings(*app, sw);
        selectPane(sw, "Sync");
        QVERIFY(app->services().caldavSync->configured());
        QCOMPARE(comboText("setting:caldav.account"), QString("Nextcloud"));
        QCOMPARE(textOf("setting:caldav.url"), m_server->baseUrl().toString().chopped(1));
        QCOMPARE(textOf("setting:caldav.user"), QString("jane"));
        QVERIFY(!isChecked(findItem("setting:caldav.createCalendars")));
        QVERIFY(!Keyring::lookup("fairfax", "caldav").isEmpty());

        // Put it all back.
        QQuickItem *off = nullptr;
        control("caldavDisconnect", off);
        click(off);
        QTRY_VERIFY_WITH_TIMEOUT(!app->services().caldavSync->configured(), 5000);
        choose("setting:caldav.account", "Other");
        clearField(sw, "setting:caldav.url");
        clearField(sw, "setting:caldav.user");
        QCOMPARE(app->store().setting("caldav.account").toString(), QString("other"));
        QCOMPARE(app->store().setting("caldav.url").toString(), QString());
        QCOMPARE(app->store().setting("caldav.user").toString(), QString());
        QVERIFY(Keyring::lookup("fairfax", "caldav").isEmpty());
    }

    // 8. Disconnect forgets the links and the keyring and deletes nothing.
    void disconnectDeletesNothing() {
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        fastTimings(app);
        connectCalDav(app, *m_server, true);
        syncNow(app);

        const int localBefore = liveTasks(app.store());
        const int serverBefore = collectionNamed(*m_server, "Home")->resources.size();
        QVERIFY(localBefore > 0);
        QVERIFY(serverBefore > 0);
        QVERIFY(!app.store().syncLinks("caldav", "tasks").isEmpty());

        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Sync");
        QQuickItem *off = nullptr;
        control("caldavDisconnect", off);
        click(off);
        QTRY_VERIFY_WITH_TIMEOUT(!app.services().caldavSync->configured(), 5000);
        QVERIFY(app.store().syncLinks("caldav", "tasks").isEmpty());
        QVERIFY(app.store().syncLinks("caldav", "lists").isEmpty());
        QVERIFY(Keyring::lookup("fairfax", "caldav").isEmpty());
        QCOMPARE(liveTasks(app.store()), localBefore);
        QCOMPARE(collectionNamed(*m_server, "Home")->resources.size(), serverBefore);
    }
};

REGISTER_TEST(E2ECalDav)

#include "test_caldav.moc"
