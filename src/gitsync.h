#pragma once

// Sync through a git repository, the primary way to share tasks between
// machines. The database never goes into git; instead every record is a
// small JSON file, laid out like the reference app's own folder-sync tree (Tasks.sync/):
//
//   Store.json                  {"format": "precrime-sync", "version": 1}
//   list-groups/<uid>.json
//   lists/<uid>.json
//   tag-groups/<uid>.json
//   tags/<uid>.json
//   tasks/<uid>.json
//   alarms/<uid>.json
//
// Records refer to each other by uid (list_uid, parent_uid, group_uid,
// tag_uids, task_uid), never by local ids, which differ per machine.
// Files are pretty-printed with sorted keys so diffs stay readable.
//
// A sync never produces git conflicts: fetch; merge the remote's changed
// files into the database record by record (the newer `modified` wins);
// move HEAD onto the remote with `git reset --soft`; write every file from
// the merged database; commit on top; push. A rejected push (someone pushed
// meanwhile) simply runs the cycle again. Deletions are tombstones
// ("deleted": true) that are pruned 60 days after the deletion.
//
// git runs as a child process with prompts disabled (GIT_TERMINAL_PROMPT=0,
// ssh BatchMode), using the user's own keys and config.

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <functional>

class Store;

namespace GitSyncFiles {

// Writes the whole store into `directory` (creating it), removing files for
// records that no longer exist and pruning tombstones older than 60 days
// relative to `now`. Returns false with `error` on failure.
bool writeTree(const Store &store, const QString &directory, const QDateTime &now, QString *error = nullptr);

// Merges the record files under `directory` into the store as one
// non-undoable change: records new to this machine are inserted, records
// whose file is newer than the local copy (by `modified`) are updated, and
// records whose files are listed in `removedPaths` (relative paths deleted
// on the remote since the last sync) are deleted locally. `onlyPaths`, when
// non-empty, limits reading to those relative paths. Returns how many local
// records changed.
int readTree(Store &store, const QString &directory, const QStringList &onlyPaths = {},
             const QStringList &removedPaths = {}, QString *error = nullptr);

}  // namespace GitSyncFiles

class GitSync : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString remote READ remote NOTIFY stateChanged)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY stateChanged)

public:
    // `directory` is the working copy; empty means the default beside the
    // database (…/precrime/sync).
    explicit GitSync(Store *store, const QString &directory = {}, QObject *parent = nullptr);

    bool configured() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString remote() const;
    QDateTime lastSync() const;
    QString directory() const { return m_directory; }

    // Sets up the working copy for `url` (any URL git accepts, including a
    // local path or file://): clones it, or initialises an empty repository
    // with that remote when the clone finds nothing. Then syncs.
    Q_INVOKABLE void setRemote(const QString &url);
    Q_INVOKABLE void disconnectRemote();
    Q_INVOKABLE void syncNow();

    // Runs one complete cycle synchronously (for tests and for a last push
    // on quit). Returns true on success.
    bool syncBlocking(int timeoutMs = 60000);

signals:
    void stateChanged();
    void synced(int changedLocally);

private:
    struct Step;
    void runCycle(std::function<void(bool ok, const QString &message)> done);
    void finish(bool ok, const QString &message);

    Store *m_store;
    QString m_directory;
    QTimer m_periodic;
    QTimer m_debounce;
    bool m_busy = false;
    bool m_again = false;
    bool m_applying = false;  // True while the sync itself writes to the store.
    QString m_status;
};
