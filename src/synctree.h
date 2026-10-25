#pragma once

// The per-record file tree that git sync, folder sync and WebDAV sync share.
// The database never goes into it; instead every record is a small JSON file,
// laid out as a folder tree:
//
//   Store.json                  {"format": "fairfax-sync", "version": 1}
//   list-groups/<uid>.json
//   lists/<uid>.json
//   sections/<uid>.json
//   tag-groups/<uid>.json
//   tags/<uid>.json
//   locations/<uid>.json
//   tasks/<uid>.json
//   alarms/<uid>.json
//   attachments/<uid>.json
//   attachment-files/<file>     the pictures and audio notes themselves
//
// Records refer to each other by uid (list_uid, parent_uid, group_uid,
// tag_uids, task_uid), never by local ids, which differ per machine.
// Files are pretty-printed with sorted keys so diffs stay readable.
//
// Conflicts are settled record by record. When both sides changed a record
// since the last sync, the two versions are merged field by field against the
// file as it was at that sync (see mergeRecords in synctree.cpp); the side
// with the larger `modified` wins a field both changed, and a tie is broken
// by comparing the serialized bytes so every machine picks the same side.
// Without that earlier version the newer `modified` replaces the whole record.
// Deletions are tombstones ("deleted": true), pruned 60 days after the
// deletion.
//
// Lists the user marked "Don't Sync This List" are skipped in both
// directions, together with their tasks, sections, alarms and attachments.

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

class Store;

namespace SyncTree {

// Writes the whole store into `directory` (creating it), removing files for
// records that no longer exist and pruning tombstones older than 60 days
// relative to `now`. Returns false with `error` on failure.
bool writeTree(const Store &store, const QString &directory, const QDateTime &now, QString *error = nullptr);

// Merges the record files under `directory` into the store as one
// non-undoable change: records new to this machine are inserted, records
// whose files are listed in `removedPaths` (relative paths deleted on the
// remote since the last sync) are deleted locally, and a record that exists
// on both sides is merged. `bases` holds, by relative path, each file as it
// was when this machine last synced it; with one, the record is merged field
// by field (three-way), without one the file with the newer `modified`
// replaces the local copy as a whole. `onlyPaths`, when non-empty, limits
// reading to those relative paths. Returns how many local records changed.
int readTree(Store &store, const QString &directory, const QStringList &onlyPaths = {},
             const QStringList &removedPaths = {}, QString *error = nullptr,
             const QHash<QString, QByteArray> &bases = {});

// Deletes every local record that is not excluded from sync, for "Replace
// Local" on a first sync. Not undoable; callers take a safety backup first.
void wipeLocal(Store &store);

// Sync exclusions, kept as a comma-separated list of list uids in the
// setting "sync.excludedLists" (so there is no schema change, and the choice
// stays on this machine).
QSet<QString> excludedListUids(const Store &store);
QSet<qint64> excludedListIds(const Store &store);
bool isListExcluded(const Store &store, qint64 listId);
void setListExcluded(Store &store, qint64 listId, bool excluded);

// "Sync automatically" (Settings > Sync): when off, providers sync only when
// asked. On by default, which is how sync always behaved.
bool autoSyncEnabled(const Store &store);

// Whether `path` (relative, with '/') is one the tree uses: Store.json, a
// record file named <uid>.json in a record folder, or an attachment file.
// Anything else in a shared folder (a Syncthing ".sync-conflict" copy, a
// Dropbox "conflicted copy", editor leftovers) is ignored.
bool isTreePath(const QString &path);

// Where the tree lives. Folder sync reads and writes a directory; WebDAV
// does the same over HTTP. Calls block (WebDAV spins a local event loop).
class Remote {
public:
    virtual ~Remote() = default;
    // Every file with a signature that changes when its content does (an
    // ETag, or size and modification time).
    virtual bool list(QMap<QString, QString> *signatures, QString *error) = 0;
    virtual bool get(const QString &path, QByteArray *bytes, QString *error) = 0;
    virtual bool put(const QString &path, const QByteArray &bytes, QString *error) = 0;
    virtual bool remove(const QString &path, QString *error) = 0;
};

// A plain directory, as shared by Syncthing, Dropbox or Nextcloud's desktop client.
class FolderRemote : public Remote {
public:
    explicit FolderRemote(const QString &directory) : m_directory(directory) {}
    bool list(QMap<QString, QString> *signatures, QString *error) override;
    bool get(const QString &path, QByteArray *bytes, QString *error) override;
    bool put(const QString &path, const QByteArray &bytes, QString *error) override;
    bool remove(const QString &path, QString *error) override;

private:
    QString m_directory;
};

// What a first sync does when this machine and the remote both have tasks.
enum class FirstSync { Ask, Merge, ReplaceLocal, ReplaceServer };

struct Outcome {
    bool ok = false;
    bool needsChoice = false;  // First sync, both sides have data, and `first` was Ask.
    int changedLocally = 0;
    int pushed = 0;
    int removed = 0;
    QString error;
};

// One complete sync against `remote`: pull the files that changed since the
// last sync into `mirror` (a private copy of the tree beside the database),
// merge them into the store, write the merged store back to the mirror, and
// push what differs. `mirror` + ".state.json" remembers what the remote held
// at the last sync; without it this is a first sync.
Outcome sync(Store &store, Remote &remote, const QString &mirror, FirstSync first = FirstSync::Ask,
             const QDateTime &now = QDateTime::currentDateTime());

}  // namespace SyncTree
