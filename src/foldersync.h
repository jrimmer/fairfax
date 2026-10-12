#pragma once

// Sync providers that keep the per-record tree (synctree.h) somewhere other
// than git: a plain folder, for Syncthing, Dropbox or Nextcloud's desktop
// client to carry around, and a WebDAV collection. Both share TreeProvider's
// timers, status and first-sync choice; they differ only in the Remote they
// hand the sync cycle.
//
// The first time a machine meets a remote that already holds tasks while it
// has tasks of its own, nothing is merged until the user picks Replace
// Local, Replace Server or Merge.

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <memory>

#include "synctree.h"

class Store;

class TreeProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool needsChoice READ needsChoice NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY stateChanged)

public:
    // `key` names the provider in settings ("folder", "webdav"), `folderName`
    // the mirror directory beside the database.
    TreeProvider(Store *store, const QString &key, const QString &folderName, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    bool needsChoice() const { return m_needsChoice; }
    QString status() const { return m_status; }
    QDateTime lastSync() const;
    virtual bool configured() const = 0;

    Q_INVOKABLE void syncNow();
    // After the first-sync question: "merge", "local" (replace local) or "server".
    Q_INVOKABLE void chooseFirstSync(const QString &choice);
    Q_INVOKABLE void disconnectProvider();

    // Runs one cycle synchronously, for tests. Returns the outcome.
    SyncTree::Outcome syncBlocking();

signals:
    void stateChanged();
    void synced(int changedLocally);

protected:
    virtual std::unique_ptr<SyncTree::Remote> makeRemote(QString *error) = 0;
    virtual void clearConfig() = 0;
    // Forget the old remote's bookkeeping (a new folder or server starts as a first sync).
    void resetState();
    void setStatus(const QString &status);
    QString mirror() const;
    QString key() const { return m_key; }
    Store *store() const { return m_store; }

private:
    void run();

    Store *m_store;
    QString m_key;
    QString m_folderName;
    QTimer m_periodic;
    QTimer m_debounce;
    bool m_busy = false;
    bool m_again = false;
    bool m_applying = false;
    bool m_needsChoice = false;
    SyncTree::FirstSync m_mode = SyncTree::FirstSync::Ask;
    QString m_status;
};

class FolderSync : public TreeProvider {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY stateChanged)

public:
    explicit FolderSync(Store *store, QObject *parent = nullptr);
    bool configured() const override { return !folder().isEmpty(); }
    QString folder() const;

    Q_INVOKABLE void setFolder(const QString &path);

protected:
    std::unique_ptr<SyncTree::Remote> makeRemote(QString *error) override;
    void clearConfig() override;
};

class WebDavSync : public TreeProvider {
    Q_OBJECT
    Q_PROPERTY(QString url READ url NOTIFY stateChanged)
    Q_PROPERTY(QString user READ user NOTIFY stateChanged)

public:
    explicit WebDavSync(Store *store, QObject *parent = nullptr);
    bool configured() const override { return !url().isEmpty(); }
    QString url() const;
    QString user() const;

    // The password goes to the desktop keyring (src/keyring.cpp), never into the database.
    Q_INVOKABLE void setServer(const QString &url, const QString &user, const QString &password);

protected:
    std::unique_ptr<SyncTree::Remote> makeRemote(QString *error) override;
    void clearConfig() override;
};
