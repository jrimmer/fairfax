#include "foldersync.h"

#include <QDir>
#include <QFileInfo>

#include "keyring.h"
#include "store.h"
#include "webdav.h"

TreeProvider::TreeProvider(Store *store, const QString &key, const QString &folderName, QObject *parent)
    : QObject(parent), m_store(store), m_key(key), m_folderName(folderName) {
    m_periodic.setInterval(5 * 60 * 1000);
    connect(&m_periodic, &QTimer::timeout, this, [this] {
        if (SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    m_periodic.start();
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(20 * 1000);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    // The sync's own writes don't count; an edit made while a sync runs
    // queues another sync right after it.
    connect(store, &Store::changed, this, [this] {
        if (m_applying || !configured() || !SyncTree::autoSyncEnabled(*m_store) || m_needsChoice)
            return;
        if (m_busy)
            m_again = true;
        else
            m_debounce.start();
    });
}

QDateTime TreeProvider::lastSync() const {
    return QDateTime::fromString(m_store->setting(m_key + ".lastSync").toString(), Qt::ISODate);
}

QString TreeProvider::mirror() const {
    return QFileInfo(m_store->path()).absolutePath() + "/" + m_folderName;
}

void TreeProvider::setStatus(const QString &status) {
    m_status = status;
    emit stateChanged();
}

void TreeProvider::resetState() {
    QDir(mirror()).removeRecursively();
    QFile::remove(mirror() + ".state.json");
    m_store->setSetting(m_key + ".lastSync", QString());
    m_needsChoice = false;
    m_mode = SyncTree::FirstSync::Ask;
}

void TreeProvider::syncNow() {
    if (!configured() || m_store->path().isEmpty())
        return;
    if (m_busy) {
        m_again = true;
        return;
    }
    if (m_needsChoice)
        return;
    m_debounce.stop();
    m_busy = true;
    m_status = "Syncing…";
    emit stateChanged();
    // Let the "Syncing…" state paint before the blocking cycle starts.
    QTimer::singleShot(0, this, [this] { run(); });
}

void TreeProvider::run() {
    QString error;
    auto remote = makeRemote(&error);
    SyncTree::Outcome outcome;
    if (remote) {
        m_applying = true;
        outcome = SyncTree::sync(*m_store, *remote, mirror(), m_mode);
        m_applying = false;
    } else {
        outcome.error = error;
    }
    m_busy = false;
    if (outcome.needsChoice) {
        m_needsChoice = true;
        m_status = "This machine and the remote both have tasks: choose how to start";
    } else if (outcome.ok) {
        m_mode = SyncTree::FirstSync::Ask;
        m_store->setSetting(m_key + ".lastSync", QDateTime::currentDateTime().toString(Qt::ISODate));
        m_status = "Synced";
    } else {
        m_status = outcome.error.isEmpty() ? QString("Sync failed") : outcome.error;
    }
    setProperty("lastOk", outcome.ok);
    emit stateChanged();
    if (outcome.ok)
        emit synced(outcome.changedLocally);
    if (m_again) {
        m_again = false;
        syncNow();
    }
}

SyncTree::Outcome TreeProvider::syncBlocking() {
    QString error;
    auto remote = makeRemote(&error);
    SyncTree::Outcome outcome;
    if (!remote) {
        outcome.error = error;
        return outcome;
    }
    m_applying = true;
    outcome = SyncTree::sync(*m_store, *remote, mirror(), m_mode);
    m_applying = false;
    if (outcome.ok)
        m_store->setSetting(m_key + ".lastSync", QDateTime::currentDateTime().toString(Qt::ISODate));
    return outcome;
}

void TreeProvider::chooseFirstSync(const QString &choice) {
    if (!m_needsChoice)
        return;
    if (choice == "local") {
        // Replace Local discards this machine's tasks, so keep a copy first.
        const QString name = QStringLiteral("Precrime_(%1)_before-sync.db")
                                 .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HH_mm_ss"));
        m_store->backupTo(QFileInfo(m_store->path()).absolutePath() + "/backups/" + name);
        m_mode = SyncTree::FirstSync::ReplaceLocal;
    } else if (choice == "server") {
        m_mode = SyncTree::FirstSync::ReplaceServer;
    } else {
        m_mode = SyncTree::FirstSync::Merge;
    }
    m_needsChoice = false;
    syncNow();
}

void TreeProvider::disconnectProvider() {
    resetState();
    clearConfig();
    m_status = "Not set up";
    emit stateChanged();
}

// ---------------------------------------------------------------------------

FolderSync::FolderSync(Store *store, QObject *parent) : TreeProvider(store, "folder", "folder-sync", parent) {}

QString FolderSync::folder() const { return store()->setting("folder.path").toString(); }

void FolderSync::setFolder(const QString &path) {
    if (busy())
        return;
    resetState();
    store()->setSetting("folder.path", path);
    if (!path.isEmpty())
        QDir().mkpath(path);
    syncNow();
}

std::unique_ptr<SyncTree::Remote> FolderSync::makeRemote(QString *error) {
    if (!QDir(folder()).exists()) {
        if (error)
            *error = "The folder " + folder() + " is not there";
        return nullptr;
    }
    return std::make_unique<SyncTree::FolderRemote>(folder());
}

// ---------------------------------------------------------------------------

WebDavSync::WebDavSync(Store *store, QObject *parent) : TreeProvider(store, "webdav", "webdav-sync", parent) {}

QString WebDavSync::url() const { return store()->setting("webdav.url").toString(); }
QString WebDavSync::user() const { return store()->setting("webdav.user").toString(); }

void WebDavSync::setServer(const QString &url, const QString &user, const QString &password) {
    if (busy())
        return;
    QString error;
    if (!Keyring::store("precrime-webdav", user + "@" + url, password, &error)) {
        setStatus(error);
        return;
    }
    resetState();
    store()->setSetting("webdav.url", url);
    store()->setSetting("webdav.user", user);
    syncNow();
}

std::unique_ptr<SyncTree::Remote> WebDavSync::makeRemote(QString *error) {
    const QUrl u(url());
    if (!u.isValid() || (u.scheme() != "http" && u.scheme() != "https")) {
        if (error)
            *error = "The WebDAV address must start with http:// or https://";
        return nullptr;
    }
    return std::make_unique<WebDavRemote>(u, user(), Keyring::lookup("precrime-webdav", user() + "@" + url()));
}

void FolderSync::clearConfig() { store()->setSetting("folder.path", QString()); }

void WebDavSync::clearConfig() {
    Keyring::clear("precrime-webdav", user() + "@" + url());
    store()->setSetting("webdav.url", QString());
    store()->setSetting("webdav.user", QString());
}
