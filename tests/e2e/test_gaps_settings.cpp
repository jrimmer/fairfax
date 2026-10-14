// End-to-end gap tests: the parts of Settings the other settings tests leave
// out. The About block's project link (the URL it hands to the desktop), the
// question a first folder sync asks when both sides already hold tasks, and
// Todoist's Sync Now and Disconnect buttons (with the network replaced).

#include <QDesktopServices>
#include <QDirIterator>
#include <QJsonObject>

#include "gapskit2.h"
#include "foldersync.h"
#include "todoist.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::g2;

namespace {

QStringList &seen() {
    static QStringList urls;
    return urls;
}

class UrlSink : public QObject {
    Q_OBJECT
public slots:
    void opened(const QUrl &url) { seen() << QString::fromUtf8(url.toEncoded()); }
};

QStringList filesUnder(const QString &root) {
    QStringList out;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(root).relativeFilePath(it.next());
    return out;
}

// Whether any file in the folder mentions the text (a task's JSON file does).
bool folderMentions(const QString &root, const QString &text) {
    for (const QString &f : filesUnder(root))
        if (!f.endsWith(".state.json") && mk::readFile(root + "/" + f).contains(text))
            return true;
    return false;
}

void copyTree(const QString &from, const QString &to) {
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString target = to + "/" + QDir(from).relativeFilePath(it.filePath());
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::copy(it.filePath(), target);
    }
}

// A world with one task of its own, "Local only", and none of the default
// world's.
Seed seedLocalOnly(Store &store) {
    Seed s;
    store.begin("Seed", false);
    TaskList l;
    l.id = store.nextId("lists");
    l.name = "Mine";
    l.order = 1;
    store.putList(l);
    Task t;
    t.id = store.nextId("tasks");
    t.listId = l.id;
    t.title = "Local only";
    t.order = t.id;
    store.putTask(t);
    store.commit();
    s.home = l.id;
    return s;
}

}  // namespace

class E2EGapsSettings : public QObject {
    Q_OBJECT
    UrlSink m_sink;
    QTemporaryDir m_remote;   // A synced folder that already holds the default world's tasks.

private slots:
    void initTestCase() {
        QDesktopServices::setUrlHandler("https", &m_sink, "opened");
        // Another machine's tasks: the default world, synced into a folder once.
        Session other;
        other.withSync = true;
        QVERIFY(other.start());
        QVERIFY(other->services().folderSync);
        emit other->services().filePicker.picked(m_remote.path(), "sync-folder");
        QTRY_VERIFY_WITH_TIMEOUT(other->services().folderSync->configured(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!other->services().folderSync->busy() && folderMentions(m_remote.path(), "Buy milk"), 15000);
    }
    void cleanupTestCase() { QDesktopServices::unsetUrlHandler("https"); }
    void init() { seen().clear(); }

    // "Fairfax" block in the corner of Settings: the project address is a link.
    void aboutLinkOpensTheProjectPage() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        QQuickItem *link = nullptr;
        control("aboutLink", link);
        click(link);
        QTRY_COMPARE_WITH_TIMEOUT(seen(), QStringList{"https://github.com/jrimmer/fairfax"}, 5000);
    }

    void firstFolderSyncChoice_data() {
        QTest::addColumn<QString>("button");
        QTest::newRow("merge") << "firstSyncMerge";
        QTest::newRow("replace local") << "firstSyncReplaceLocal";
        QTest::newRow("replace server") << "firstSyncReplaceServer";
    }

    // "This machine and the remote both have tasks. How should the first sync
    // go?" Merge keeps both sides' tasks, Replace Local takes the remote's
    // (after a backup of this machine's), Replace Server overwrites the remote
    // with this machine's.
    void firstFolderSyncChoice() {
        QFETCH(QString, button);
        QTemporaryDir folder;
        copyTree(m_remote.path(), folder.path());
        Session s;
        s.withSync = true;
        s.seed = seedLocalOnly;
        QVERIFY(s.start());
        App &app = *s.app;
        auto *sync = app.services().folderSync.get();
        QVERIFY(sync);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Sync");
        QVERIFY(!findItem("firstSyncChoice"));

        emit app.services().filePicker.picked(folder.path(), "sync-folder");
        QTRY_VERIFY_WITH_TIMEOUT(sync->needsChoice(), 15000);
        // Nothing has been exchanged yet.
        QCOMPARE(app.taskTitled("Buy milk").id, qint64(0));
        QVERIFY(!folderMentions(folder.path(), "Local only"));
        QQuickItem *choice = nullptr;
        control("firstSyncChoice", choice);
        QQuickItem *pick = nullptr;
        control(button, pick);
        click(pick);
        QTRY_VERIFY_WITH_TIMEOUT(!sync->needsChoice(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!sync->busy() && sync->status() == "Synced", 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("firstSyncChoice"), 5000);

        const QStringList backups = QDir(app.dir() + "/backups").entryList({"*before-sync.db"}, QDir::Files);
        if (button == "firstSyncMerge") {
            QVERIFY(app.taskTitled("Local only").id != 0);
            QVERIFY(app.taskTitled("Buy milk").id != 0);
            QVERIFY(folderMentions(folder.path(), "Local only"));
            QVERIFY(folderMentions(folder.path(), "Buy milk"));
            QVERIFY(backups.isEmpty());
        } else if (button == "firstSyncReplaceLocal") {
            QCOMPARE(app.taskTitled("Local only").id, qint64(0));
            QVERIFY(app.taskTitled("Buy milk").id != 0);
            QVERIFY(!folderMentions(folder.path(), "Local only"));
            QVERIFY(folderMentions(folder.path(), "Buy milk"));
            QCOMPARE(backups.size(), 1);   // This machine's tasks were kept first.
        } else {
            QVERIFY(app.taskTitled("Local only").id != 0);
            QCOMPARE(app.taskTitled("Buy milk").id, qint64(0));
            QVERIFY(folderMentions(folder.path(), "Local only"));
            QVERIFY(!folderMentions(folder.path(), "Buy milk"));
            QVERIFY(backups.isEmpty());
        }

        // A later sync no longer asks.
        QQuickItem *now = nullptr;
        control("folderSyncNow", now);
        click(now);
        QTRY_VERIFY_WITH_TIMEOUT(!sync->busy(), 15000);
        QVERIFY(!sync->needsChoice());
        QVERIFY(!findItem("firstSyncChoice"));
    }

    // Todoist, with the network replaced: Sync Now sends a request and reports
    // the result, Disconnect forgets the account and brings the token box back.
    void todoistSyncNowAndDisconnect() {
        Session s;
        s.withSync = true;
        QVERIFY(s.start());
        App &app = *s.app;
        auto *todoist = app.services().todoist.get();
        QVERIFY(todoist);
        int requests = 0;
        todoist->setTransport([&requests](const QUrlQuery &) {
            ++requests;
            return QJsonObject{{"sync_token", "token-1"}};
        });
        QVERIFY(todoist->connected());
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Sync");
        QQuickItem *now = nullptr, *off = nullptr;
        control("todoistSyncNow", now);
        control("todoistDisconnect", off);
        QVERIFY(!findItem("todoistToken"));   // Connected: no token box.
        QVERIFY(app.store().setting("todoist.lastSync").toString().isEmpty());

        click(now);
        QTRY_VERIFY_WITH_TIMEOUT(requests > 0, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!todoist->busy() && todoist->status().startsWith("Synced"), 5000);
        QVERIFY(!app.store().setting("todoist.lastSync").toString().isEmpty());
        QVERIFY(todoist->lastSync().isValid());

        control("todoistDisconnect", off);
        click(off);
        QTRY_VERIFY_WITH_TIMEOUT(!todoist->connected(), 5000);
        QCOMPARE(todoist->status(), QString("Not connected"));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("todoistSyncNow") && !findItem("todoistDisconnect"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("todoistToken") != nullptr && findItem("todoistConnect") != nullptr, 5000);
        QVERIFY(app.store().setting("todoist.lastSync").toString().isEmpty());
        const int after = requests;
        todoist->syncNow();   // Nothing to talk to any more.
        QCOMPARE(requests, after);
    }
};

REGISTER_TEST(E2EGapsSettings)

#include "test_gaps_settings.moc"
