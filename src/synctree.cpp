#include "synctree.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSet>

#include "store.h"

// Conflict handling: records are merged one by one, the newer `modified`
// wins (a tie with different content is broken by comparing the serialized
// bytes, so both machines pick the same side). Store::begin(..., keepStamps)
// lets a merged-in remote record keep its own `modified`, so the stamp that
// is written back is the true edit time and nothing ping-pongs between
// machines.

namespace {

// In dependency order: a record's references always point at earlier kinds.
enum Kind { KGroup, KList, KSection, KTagGroup, KTag, KLocation, KTask, KAlarm, KAttachment, KindCount };
const char *const Folders[KindCount] = {"list-groups", "lists", "sections", "tag-groups", "tags", "locations", "tasks", "alarms", "attachments"};
const char *const Tables[KindCount] = {"list_groups", "lists", "sections", "tag_groups", "tags", "locations", "tasks", "alarms", "attachments"};
constexpr int TombstoneDays = 60;

struct Maps {
    QHash<QString, qint64> uid[KindCount];
    QHash<qint64, QString> rev[KindCount];
};

template <typename Hash>
void addMaps(Maps &m, Kind k, const Hash &records) {
    for (const auto &r : records) {
        if (r.uid.isEmpty())
            continue;
        m.uid[k].insert(r.uid, r.id);
        m.rev[k].insert(r.id, r.uid);
    }
}

Maps buildMaps(const Store &s) {
    Maps m;
    addMaps(m, KGroup, s.groups());
    addMaps(m, KList, s.lists());
    addMaps(m, KTagGroup, s.tagGroups());
    addMaps(m, KTag, s.tags());
    addMaps(m, KTask, s.tasks());
    addMaps(m, KAlarm, s.alarms());
    addMaps(m, KLocation, s.locations());
    addMaps(m, KAttachment, s.attachments());
    addMaps(m, KSection, s.sections());
    return m;
}

void swapRef(QVariantMap &map, const char *idKey, const char *uidKey, const QHash<qint64, QString> &rev) {
    const qint64 id = map.take(idKey).toLongLong();
    map.insert(uidKey, rev.value(id));
}

QJsonArray uidList(const QString &ids, const QHash<qint64, QString> &rev) {
    QJsonArray uids;
    for (const auto &part : ids.split(',', Qt::SkipEmptyParts)) {
        const QString u = rev.value(part.toLongLong());
        if (!u.isEmpty())
            uids.append(u);
    }
    return uids;
}

QString idList(const QJsonArray &uids, const QHash<QString, qint64> &uid) {
    QStringList ids;
    for (const auto &v : uids)
        if (const qint64 id = uid.value(v.toString(), 0))
            ids << QString::number(id);
    return ids.join(',');
}

// "ext" is stored as compact JSON text: a writer's own data, never interpreted.
QString extText(const QJsonValue &value) {
    const QJsonObject o = value.toObject();
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject toJson(Kind kind, QVariantMap map, const Maps &x) {
    map.remove("id");
    const QString ext = map.take("ext").toString();
    QJsonArray tagUids;
    QJsonArray locationUids;
    switch (kind) {
    case KList: swapRef(map, "group_id", "group_uid", x.rev[KGroup]); break;
    case KTag: swapRef(map, "group_id", "group_uid", x.rev[KTagGroup]); break;
    case KAlarm: swapRef(map, "task_id", "task_uid", x.rev[KTask]); break;
    case KAttachment: swapRef(map, "task_id", "task_uid", x.rev[KTask]); break;
    case KSection: swapRef(map, "list_id", "list_uid", x.rev[KList]); break;
    case KTask: {
        swapRef(map, "list_id", "list_uid", x.rev[KList]);
        swapRef(map, "section_id", "section_uid", x.rev[KSection]);
        swapRef(map, "parent_id", "parent_uid", x.rev[KTask]);
        locationUids = uidList(map.take("location_ids").toString(), x.rev[KLocation]);
        for (const auto &part : map.take("tag_ids").toString().split(',', Qt::SkipEmptyParts)) {
            const QString u = x.rev[KTag].value(part.toLongLong());
            if (!u.isEmpty())
                tagUids.append(u);
        }
        break;
    }
    default: break;
    }
    QJsonObject obj = QJsonObject::fromVariantMap(map);
    const QJsonObject extObject = QJsonDocument::fromJson(ext.toUtf8()).object();
    if (!extObject.isEmpty())
        obj.insert("ext", extObject);
    if (kind == KTask) {
        obj.insert("tag_uids", tagUids);
        obj.insert("location_uids", locationUids);
    }
    return obj;
}

QByteArray bytesOf(const QJsonObject &obj) { return QJsonDocument(obj).toJson(QJsonDocument::Indented); }

QVariantMap fromJson(Kind kind, const QJsonObject &obj, const Maps &x, qint64 id) {
    QVariantMap map = obj.toVariantMap();
    map.insert("ext", extText(obj.value("ext")));
    auto swapBack = [&](const char *uidKey, const char *idKey, Kind target) {
        const QString u = map.take(uidKey).toString();
        map.insert(idKey, u.isEmpty() ? 0 : x.uid[target].value(u, 0));
    };
    switch (kind) {
    case KList: swapBack("group_uid", "group_id", KGroup); break;
    case KTag: swapBack("group_uid", "group_id", KTagGroup); break;
    case KAlarm: swapBack("task_uid", "task_id", KTask); break;
    case KAttachment: swapBack("task_uid", "task_id", KTask); break;
    case KSection: swapBack("list_uid", "list_id", KList); break;
    case KTask: {
        swapBack("list_uid", "list_id", KList);
        swapBack("section_uid", "section_id", KSection);
        swapBack("parent_uid", "parent_id", KTask);
        map.remove("location_uids");
        map.insert("location_ids", idList(obj.value("location_uids").toArray(), x.uid[KLocation]));
        map.remove("tag_uids");
        QStringList ids;
        for (const auto &v : obj.value("tag_uids").toArray()) {
            const qint64 tid = x.uid[KTag].value(v.toString(), 0);
            if (tid)
                ids << QString::number(tid);
        }
        map.insert("tag_ids", ids.join(','));
        break;
    }
    default: break;
    }
    map.insert("id", id);
    return map;
}

qint64 stampOf(const QDateTime &t) { return t.isValid() ? t.toSecsSinceEpoch() : 0; }


// Local records of one kind as {uid, map, modified, deleted}.
struct LocalRec {
    QVariantMap map;
    QString uid;
    qint64 modified = 0;
    bool deleted = false;
};

template <typename Hash>
void collectInto(QVector<LocalRec> &out, const Hash &records) {
    for (const auto &r : records) {
        if (r.uid.isEmpty())
            continue;
        out.append({r.toMap(), r.uid, stampOf(r.modified), r.deleted});
    }
}

QVector<LocalRec> collect(const Store &s, Kind k) {
    QVector<LocalRec> out;
    switch (k) {
    case KGroup: collectInto(out, s.groups()); break;
    case KList: collectInto(out, s.lists()); break;
    case KTagGroup: collectInto(out, s.tagGroups()); break;
    case KTag: collectInto(out, s.tags()); break;
    case KTask: collectInto(out, s.tasks()); break;
    case KAlarm: collectInto(out, s.alarms()); break;
    case KLocation: collectInto(out, s.locations()); break;
    case KAttachment: collectInto(out, s.attachments()); break;
    case KSection: collectInto(out, s.sections()); break;
    default: break;
    }
    return out;
}

bool writeIfChanged(const QString &path, const QByteArray &bytes, QString *error) {
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        const QByteArray old = existing.readAll();
        existing.close();
        if (old == bytes)
            return true;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error)
            *error = QString("Cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}


// Lists the user keeps out of sync ("Don't Sync This List"), and everything
// that belongs to them. The sets hold uids, which is what both machines share.
struct Exclusion {
    QSet<QString> lists;
    QSet<QString> tasks;
    bool any() const { return !lists.isEmpty(); }
};

Exclusion exclusionOf(const Store &s) {
    Exclusion e;
    e.lists = SyncTree::excludedListUids(s);
    if (e.lists.isEmpty())
        return e;
    for (const auto &l : s.lists())
        if (!l.uid.isEmpty() && e.lists.contains(l.uid))
            for (const auto &t : s.tasks())
                if (t.listId == l.id && !t.uid.isEmpty())
                    e.tasks.insert(t.uid);
    return e;
}

bool localExcluded(Kind kind, const QString &uid, const QVariantMap &map, const Maps &x, const Exclusion &e) {
    if (!e.any())
        return false;
    switch (kind) {
    case KList: return e.lists.contains(uid);
    case KSection:
    case KTask: return e.lists.contains(x.rev[KList].value(map.value("list_id").toLongLong()));
    case KAlarm:
    case KAttachment: return e.tasks.contains(x.rev[KTask].value(map.value("task_id").toLongLong()));
    default: return false;
    }
}

bool incomingExcluded(Kind kind, const QString &uid, const QJsonObject &obj, const Exclusion &e) {
    if (!e.any())
        return false;
    switch (kind) {
    case KList: return e.lists.contains(uid);
    case KSection:
    case KTask: return e.lists.contains(obj.value("list_uid").toString());
    case KAlarm:
    case KAttachment: return e.tasks.contains(obj.value("task_uid").toString());
    default: return false;
    }
}

struct Incoming {
    Kind kind;
    QString uid;
    QJsonObject obj;
    QByteArray bytes;
};

}  // namespace

namespace SyncTree {

bool writeTree(const Store &store, const QString &directory, const QDateTime &now, QString *error) {
    const Maps x = buildMaps(store);
    const Exclusion excl = exclusionOf(store);
    QDir root(directory);
    if (!root.mkpath(".")) {
        if (error)
            *error = "Cannot create " + directory;
        return false;
    }
    const qint64 cutoff = stampOf(now.addDays(-TombstoneDays));
    QJsonObject meta;
    meta.insert("format", "fairfax-sync");
    meta.insert("version", 1);
    if (!writeIfChanged(root.filePath("Store.json"), bytesOf(meta), error))
        return false;

    for (int k = 0; k < KindCount; ++k) {
        const QString folder = root.filePath(Folders[k]);
        QDir().mkpath(folder);
        QSet<QString> expected;
        for (const auto &rec : collect(store, Kind(k))) {
            if (rec.deleted && rec.modified > 0 && rec.modified < cutoff)
                continue;
            expected.insert(rec.uid + ".json");
            // An excluded record is not written, but a copy another machine
            // already shared stays where it is.
            if (localExcluded(Kind(k), rec.uid, rec.map, x, excl))
                continue;
            if (!writeIfChanged(folder + "/" + rec.uid + ".json", bytesOf(toJson(Kind(k), rec.map, x)), error))
                return false;
        }
        const auto existing = QDir(folder).entryList({"*.json"}, QDir::Files);
        for (const auto &name : existing)
            if (!expected.contains(name))
                QFile::remove(folder + "/" + name);
    }
    // Attachment files are copied into attachment-files/ so other machines
    // get the pictures and audio notes too; files of removed attachments go.
    {
        const QString filesDir = root.filePath("attachment-files");
        QDir().mkpath(filesDir);
        QSet<QString> wanted;
        for (const auto &a : store.attachments()) {
            if (a.deleted || a.fileName.isEmpty())
                continue;
            wanted.insert(a.fileName);
            if (excl.tasks.contains(x.rev[KTask].value(a.taskId)))
                continue;
            const QString source = store.attachmentsFolder() + "/" + a.fileName;
            const QString target = filesDir + "/" + a.fileName;
            if (!QFile::exists(target) && QFile::exists(source))
                QFile::copy(source, target);
        }
        for (const auto &name : QDir(filesDir).entryList(QDir::Files))
            if (!wanted.contains(name))
                QFile::remove(filesDir + "/" + name);
    }
    return true;
}

int readTree(Store &store, const QString &directory, const QStringList &onlyPaths, const QStringList &removedPaths,
             QString *error) {
    Q_UNUSED(error);
    const QDir root(directory);

    // Which files to look at.
    QStringList paths = onlyPaths;
    if (paths.isEmpty()) {
        for (int k = 0; k < KindCount; ++k)
            for (const auto &name : QDir(root.filePath(Folders[k])).entryList({"*.json"}, QDir::Files))
                paths << QString("%1/%2").arg(Folders[k], name);
    }
    QVector<Incoming> incoming;
    for (const auto &path : std::as_const(paths)) {
        const int slash = path.indexOf('/');
        if (slash < 0 || !path.endsWith(".json"))
            continue;
        int kind = -1;
        for (int k = 0; k < KindCount; ++k)
            if (path.left(slash) == Folders[k])
                kind = k;
        if (kind < 0)
            continue;
        QFile f(root.filePath(path));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = f.readAll();
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QString uid = QFileInfo(path).completeBaseName();
        QJsonObject obj = doc.object();
        obj.insert("uid", uid);
        incoming.append({Kind(kind), uid, obj, bytes});
    }

    int changed = 0;
    Maps x = buildMaps(store);
    Exclusion excl = exclusionOf(store);
    if (excl.any()) {
        QVector<Incoming> kept;
        for (const auto &in : std::as_const(incoming))
            if (in.kind == KTask && incomingExcluded(in.kind, in.uid, in.obj, excl))
                excl.tasks.insert(in.uid);
        for (const auto &in : std::as_const(incoming))
            if (!incomingExcluded(in.kind, in.uid, in.obj, excl))
                kept.append(in);
        incoming = kept;
    }

    // Deletions made on the remote (tombstones that were pruned).
    for (const auto &path : removedPaths) {
        const int slash = path.indexOf('/');
        if (slash < 0 || !path.endsWith(".json"))
            continue;
        for (int k = 0; k < KindCount; ++k) {
            if (path.left(slash) != Folders[k])
                continue;
            const QString uid = QFileInfo(path).completeBaseName();
            const qint64 id = x.uid[k].value(uid, 0);
            if (id && excl.any()) {
                bool skip = false;
                for (const auto &rec : collect(store, Kind(k)))
                    if (rec.uid == uid)
                        skip = localExcluded(Kind(k), uid, rec.map, x, excl);
                if (skip)
                    continue;
            }
            if (id) {
                store.discard(Tables[k], id);
                x.uid[k].remove(QFileInfo(path).completeBaseName());
                x.rev[k].remove(id);
                ++changed;
            }
        }
    }

    // Decide what to apply and hand out ids to new records, kind by kind so
    // later kinds can already resolve references to earlier ones.
    QVector<Incoming> apply[KindCount];
    QHash<QString, qint64> ids[KindCount];
    for (int k = 0; k < KindCount; ++k) {
        QHash<QString, LocalRec> local;
        for (const auto &in : std::as_const(incoming))
            if (in.kind == k && x.uid[k].contains(in.uid)) {
                for (const auto &rec : collect(store, Kind(k)))
                    local.insert(rec.uid, rec);
                break;
            }
        for (const auto &in : std::as_const(incoming)) {
            if (in.kind != k)
                continue;
            const qint64 existing = x.uid[k].value(in.uid, 0);
            if (!existing) {
                if ((k == KAlarm || k == KAttachment) && !x.uid[KTask].contains(in.obj.value("task_uid").toString()))
                    continue;  // its task is gone
                const qint64 id = store.nextId(Tables[k]);
                x.uid[k].insert(in.uid, id);
                x.rev[k].insert(id, in.uid);
                ids[k].insert(in.uid, id);
                apply[k].append(in);
                continue;
            }
            // Compare with the local copy.
            const qint64 localModified = local.value(in.uid).modified;
            const QVariantMap localMap = local.value(in.uid).map;
            const qint64 remoteModified = in.obj.value("modified").toVariant().toLongLong();
            bool take = remoteModified > localModified;
            if (remoteModified == localModified) {
                const QByteArray local = bytesOf(toJson(Kind(k), localMap, x));
                take = in.bytes > local;
            }
            if (take) {
                ids[k].insert(in.uid, existing);
                apply[k].append(in);
            }
        }
    }

    // Tasks parents-first.
    QVector<Incoming> orderedTasks;
    {
        QSet<QString> pending;
        for (const auto &in : apply[KTask])
            pending.insert(in.uid);
        QVector<Incoming> rest = apply[KTask];
        while (!rest.isEmpty()) {
            QVector<Incoming> next;
            for (const auto &in : std::as_const(rest)) {
                const QString parent = in.obj.value("parent_uid").toString();
                if (parent.isEmpty() || !pending.contains(parent))
                    orderedTasks.append(in);
                else
                    next.append(in);
            }
            for (const auto &in : std::as_const(orderedTasks))
                pending.remove(in.uid);
            if (next.size() == rest.size()) {  // a cycle: just take them
                orderedTasks += next;
                break;
            }
            rest = next;
        }
    }
    apply[KTask] = orderedTasks;

    store.begin("Sync", false, true);
    for (int k = 0; k < KindCount; ++k) {
        for (const auto &in : std::as_const(apply[k])) {
            const QVariantMap m = fromJson(Kind(k), in.obj, x, ids[k].value(in.uid));
            switch (k) {
            case KGroup: store.putGroup(ListGroup::fromMap(m)); break;
            case KList: store.putList(TaskList::fromMap(m)); break;
            case KTagGroup: store.putTagGroup(TagGroup::fromMap(m)); break;
            case KTag: store.putTag(Tag::fromMap(m)); break;
            case KTask: store.putTask(Task::fromMap(m)); break;
            case KAlarm: store.putAlarm(Alarm::fromMap(m)); break;
            case KLocation: store.putLocation(Location::fromMap(m)); break;
            case KAttachment: store.putAttachment(Attachment::fromMap(m)); break;
            case KSection: store.putSection(ListSection::fromMap(m)); break;
            default: break;
            }
            ++changed;
        }
    }
    store.commit();
    // Attachment files ride along in attachment-files/; copy in any that
    // arrived from another machine.
    {
        const QDir from(QDir(directory).filePath("attachment-files"));
        QDir().mkpath(store.attachmentsFolder());
        for (const auto &a : store.attachments()) {
            if (a.deleted || a.fileName.isEmpty())
                continue;
            const QString target = store.attachmentsFolder() + "/" + a.fileName;
            if (!QFile::exists(target) && from.exists(a.fileName))
                QFile::copy(from.filePath(a.fileName), target);
        }
    }
    return changed;
}

}  // namespace SyncTree

// ---------------------------------------------------------------------------
// Exclusions

namespace SyncTree {

static const char *const ExcludedSetting = "sync.excludedLists";

QSet<QString> excludedListUids(const Store &store) {
    QSet<QString> out;
    for (const auto &u : store.setting(ExcludedSetting).toString().split(',', Qt::SkipEmptyParts))
        out.insert(u.trimmed());
    return out;
}

QSet<qint64> excludedListIds(const Store &store) {
    QSet<qint64> out;
    const QSet<QString> uids = excludedListUids(store);
    if (uids.isEmpty())
        return out;
    for (const auto &l : store.lists())
        if (uids.contains(l.uid))
            out.insert(l.id);
    return out;
}

bool isListExcluded(const Store &store, qint64 listId) {
    const QString uid = store.list(listId).uid;
    return !uid.isEmpty() && excludedListUids(store).contains(uid);
}

void setListExcluded(Store &store, qint64 listId, bool excluded) {
    const QString uid = store.list(listId).uid;
    if (uid.isEmpty())
        return;
    QSet<QString> uids = excludedListUids(store);
    if (excluded)
        uids.insert(uid);
    else
        uids.remove(uid);
    QStringList sorted(uids.begin(), uids.end());
    sorted.sort();
    store.setSetting(ExcludedSetting, sorted.join(','));
}

bool autoSyncEnabled(const Store &store) { return store.setting("sync.auto", true).toBool(); }

void wipeLocal(Store &store) {
    const Maps x = buildMaps(store);
    const Exclusion excl = exclusionOf(store);
    for (int k = KindCount - 1; k >= 0; --k) {
        QVector<qint64> doomed;
        for (const auto &rec : collect(store, Kind(k)))
            if (!localExcluded(Kind(k), rec.uid, rec.map, x, excl))
                doomed << rec.map.value("id").toLongLong();
        for (qint64 id : std::as_const(doomed))
            store.discard(Tables[k], id);
    }
}

bool isTreePath(const QString &path) {
    if (path == "Store.json")
        return true;
    static const QRegularExpression record(R"(^([a-z-]+)/[A-Za-z0-9_-]+\.json$)");
    const auto m = record.match(path);
    if (m.hasMatch()) {
        for (int k = 0; k < KindCount; ++k)
            if (m.captured(1) == Folders[k])
                return true;
        return false;
    }
    static const QRegularExpression attachment(R"(^attachment-files/[^/]+$)");
    return attachment.match(path).hasMatch() && !path.endsWith(".tmp") && !path.contains("/.");
}

// ---------------------------------------------------------------------------
// A directory as the remote

bool FolderRemote::list(QMap<QString, QString> *signatures, QString *error) {
    const QDir root(m_directory);
    if (!root.exists()) {
        if (error)
            *error = "The folder " + m_directory + " does not exist";
        return false;
    }
    QDirIterator it(m_directory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = root.relativeFilePath(it.filePath());
        if (isTreePath(rel))
            signatures->insert(rel, QString("%1:%2").arg(it.fileInfo().size()).arg(it.fileInfo().lastModified().toMSecsSinceEpoch()));
    }
    return true;
}

bool FolderRemote::get(const QString &path, QByteArray *bytes, QString *error) {
    QFile f(QDir(m_directory).filePath(path));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = "Cannot read " + f.fileName();
        return false;
    }
    *bytes = f.readAll();
    return true;
}

bool FolderRemote::put(const QString &path, const QByteArray &bytes, QString *error) {
    const QString target = QDir(m_directory).filePath(path);
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error)
            *error = QString("Cannot write %1: %2").arg(target, f.errorString());
        return false;
    }
    return true;
}

bool FolderRemote::remove(const QString &path, QString *error) {
    const QString target = QDir(m_directory).filePath(path);
    if (QFile::exists(target) && !QFile::remove(target)) {
        if (error)
            *error = "Cannot remove " + target;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The sync cycle

namespace {

struct FileState {
    QString signature;
    QString hash;
};
using State = QMap<QString, FileState>;

QString hashOf(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

State loadState(const QString &file) {
    State state;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return state;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        state.insert(it.key(), {it.value().toObject().value("sig").toString(), it.value().toObject().value("hash").toString()});
    return state;
}

void saveState(const QString &file, const State &state) {
    QJsonObject obj;
    for (auto it = state.cbegin(); it != state.cend(); ++it)
        obj.insert(it.key(), QJsonObject{{"sig", it->signature}, {"hash", it->hash}});
    QSaveFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

bool isRecordFile(const QString &path) { return path != "Store.json" && !path.startsWith("attachment-files/"); }

}  // namespace

Outcome sync(Store &store, Remote &remote, const QString &mirror, FirstSync first, const QDateTime &now) {
    Outcome out;
    QMap<QString, QString> listing;
    if (!remote.list(&listing, &out.error))
        return out;
    const QString stateFile = mirror + ".state.json";
    const bool isFirst = !QFile::exists(stateFile);
    State state = loadState(stateFile);
    const QDir root(mirror);

    if (isFirst && first == FirstSync::Ask) {
        bool remoteHasTasks = false;
        for (auto it = listing.cbegin(); it != listing.cend(); ++it)
            if (it.key().startsWith("tasks/"))
                remoteHasTasks = true;
        bool localHasTasks = false;
        for (const auto &t : store.tasks())
            if (!t.deleted)
                localHasTasks = true;
        if (remoteHasTasks && localHasTasks) {
            out.needsChoice = true;
            return out;
        }
    }

    // Pull what changed on the remote since the last sync.
    QStringList changed, removed;
    if (!(isFirst && first == FirstSync::ReplaceServer)) {
        for (auto it = listing.cbegin(); it != listing.cend(); ++it) {
            const QString &path = it.key();
            const auto known = state.constFind(path);
            if (known != state.constEnd() && known->signature == it.value() && root.exists(path))
                continue;
            QByteArray bytes;
            if (!remote.get(path, &bytes, &out.error))
                return out;
            QDir().mkpath(QFileInfo(root.filePath(path)).absolutePath());
            QSaveFile f(root.filePath(path));
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
                out.error = "Cannot write " + root.filePath(path);
                return out;
            }
            state.insert(path, {it.value(), hashOf(bytes)});
            if (isRecordFile(path))
                changed << path;
        }
        const QStringList known = state.keys();
        for (const auto &path : known) {
            if (listing.contains(path))
                continue;
            state.remove(path);
            QFile::remove(root.filePath(path));
            if (isRecordFile(path))
                removed << path;
        }
        if (isFirst && first == FirstSync::ReplaceLocal)
            wipeLocal(store);
        if (!changed.isEmpty() || !removed.isEmpty())
            out.changedLocally = readTree(store, mirror, changed.isEmpty() ? QStringList{"-"} : changed, removed, &out.error);
    }

    QString writeError;
    if (!writeTree(store, mirror, now, &writeError)) {
        saveState(stateFile, state);
        out.error = writeError;
        return out;
    }

    // Push what differs from the remote's copy; delete what the mirror no
    // longer has (pruned tombstones, files of removed attachments). A first
    // "Replace Server" also clears whatever else the remote holds.
    QStringList local;
    {
        QDirIterator it(mirror, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString rel = root.relativeFilePath(it.filePath());
            if (isTreePath(rel))
                local << rel;
        }
        local.sort();
    }
    QSet<QString> pushedNow;
    for (const auto &path : std::as_const(local)) {
        QFile f(root.filePath(path));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = f.readAll();
        const auto known = state.constFind(path);
        if (known != state.constEnd() && known->hash == hashOf(bytes) && listing.contains(path))
            continue;
        if (!remote.put(path, bytes, &out.error)) {
            saveState(stateFile, state);
            return out;
        }
        pushedNow.insert(path);
        state.insert(path, {QString(), hashOf(bytes)});
        ++out.pushed;
    }
    const QSet<QString> localSet(local.begin(), local.end());
    QStringList gone;
    for (auto it = listing.cbegin(); it != listing.cend(); ++it) {
        if (localSet.contains(it.key()))
            continue;
        // A remote file the mirror never had is another machine's new record
        // that appeared after we listed; leave it for the next sync.
        if (!state.contains(it.key()) && !(isFirst && first == FirstSync::ReplaceServer))
            continue;
        gone << it.key();
    }
    for (const auto &path : std::as_const(gone)) {
        if (!remote.remove(path, &out.error)) {
            saveState(stateFile, state);
            return out;
        }
        state.remove(path);
        ++out.removed;
    }

    // Remember the signatures the remote now reports for what we pushed.
    if (!pushedNow.isEmpty()) {
        QMap<QString, QString> after;
        if (!remote.list(&after, &out.error)) {
            saveState(stateFile, state);
            return out;
        }
        for (const auto &path : std::as_const(pushedNow))
            state[path].signature = after.value(path);
    }
    saveState(stateFile, state);
    out.ok = true;
    return out;
}

}  // namespace SyncTree
