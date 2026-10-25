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

#include "schedule.h"
#include "store.h"
#include "views.h"

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

// The references a record kind holds, by the uid key of its file and the id
// column of the database. A list reference holds many ids, comma-separated.
struct RefSpec {
    const char *uidKey;
    const char *idKey;
    Kind target;
    bool list;
};

QVector<RefSpec> refsOf(Kind kind) {
    switch (kind) {
    case KList: return {{"group_uid", "group_id", KGroup, false}};
    case KTag: return {{"group_uid", "group_id", KTagGroup, false}};
    case KAlarm:
    case KAttachment: return {{"task_uid", "task_id", KTask, false}};
    case KSection: return {{"list_uid", "list_id", KList, false}};
    case KTask:
        return {{"list_uid", "list_id", KList, false},
                {"section_uid", "section_id", KSection, false},
                {"parent_uid", "parent_id", KTask, false},
                {"tag_uids", "tag_ids", KTag, true},
                {"location_uids", "location_ids", KLocation, true}};
    default: return {};
    }
}

// A reference to a record this machine does not have yet is kept in the
// record's "pending" column, as JSON keyed like the file: a uid string, or an
// array of uids for a list reference.
QJsonObject pendingOf(const QString &text) { return QJsonDocument::fromJson(text.toUtf8()).object(); }

QString pendingText(const QJsonObject &pending) {
    return pending.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(pending).toJson(QJsonDocument::Compact));
}

// The uid a single reference stands for: the pending one if there is one,
// else the uid of the record its id names.
QString refUid(const QVariantMap &map, const char *idKey, const char *uidKey, const QHash<qint64, QString> &rev) {
    const QString raw = pendingOf(map.value("pending").toString()).value(uidKey).toString();
    return raw.isEmpty() ? rev.value(map.value(idKey).toLongLong()) : raw;
}

// "ext" is stored as compact JSON text: a writer's own data, never interpreted.
QString extText(const QJsonValue &value) {
    const QJsonObject o = value.toObject();
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject toJson(Kind kind, QVariantMap map, const Maps &x) {
    map.remove("id");
    const QString ext = map.take("ext").toString();
    const QString modifiedBy = map.take("modified_by").toString();
    const QJsonObject pending = pendingOf(map.value("pending").toString());
    QJsonObject lists;
    for (const RefSpec &r : refsOf(kind)) {
        if (!r.list) {
            map.insert(r.uidKey, refUid(map, r.idKey, r.uidKey, x.rev[r.target]));
            map.remove(r.idKey);
            continue;
        }
        QJsonArray uids;
        for (const auto &part : map.take(r.idKey).toString().split(',', Qt::SkipEmptyParts)) {
            const QString u = x.rev[r.target].value(part.toLongLong());
            if (!u.isEmpty())
                uids.append(u);
        }
        for (const auto &v : pending.value(r.uidKey).toArray())
            if (!v.toString().isEmpty() && !uids.contains(v))
                uids.append(v);
        lists.insert(r.uidKey, uids);
    }
    map.remove("pending");
    QJsonObject obj = QJsonObject::fromVariantMap(map);
    for (auto it = lists.begin(); it != lists.end(); ++it)
        obj.insert(it.key(), it.value());
    const QJsonObject extObject = QJsonDocument::fromJson(ext.toUtf8()).object();
    if (!extObject.isEmpty())
        obj.insert("ext", extObject);
    if (!modifiedBy.isEmpty())
        obj.insert("modified_by", modifiedBy);
    return obj;
}

QByteArray bytesOf(const QJsonObject &obj) { return QJsonDocument(obj).toJson(QJsonDocument::Indented); }

// `defaultList` stands in for a task whose list is not here yet, so it shows
// up somewhere; its file keeps naming the real list (see "pending").
QVariantMap fromJson(Kind kind, const QJsonObject &obj, const Maps &x, qint64 id, qint64 defaultList = 0) {
    QVariantMap map = obj.toVariantMap();
    map.insert("ext", extText(obj.value("ext")));
    map.insert("modified_by", obj.value("modified_by").toString());
    QJsonObject pending;
    for (const RefSpec &r : refsOf(kind)) {
        if (r.list) {
            QStringList ids;
            QJsonArray unresolved;
            for (const auto &v : obj.value(r.uidKey).toArray()) {
                const QString u = v.toString();
                if (u.isEmpty())
                    continue;
                if (const qint64 rid = x.uid[r.target].value(u, 0))
                    ids << QString::number(rid);
                else
                    unresolved.append(u);
            }
            map.remove(r.uidKey);
            map.insert(r.idKey, ids.join(','));
            if (!unresolved.isEmpty())
                pending.insert(r.uidKey, unresolved);
            continue;
        }
        const QString u = map.take(r.uidKey).toString();
        const qint64 rid = u.isEmpty() ? 0 : x.uid[r.target].value(u, 0);
        if (!u.isEmpty() && !rid)
            pending.insert(r.uidKey, u);
        map.insert(r.idKey, rid);
    }
    if (kind == KTask && pending.contains("list_uid"))
        map.insert("list_id", defaultList);
    map.insert("pending", pendingText(pending));
    map.insert("id", id);
    return map;
}

// Settles the pending references of `map` that name records now in `x`.
// Returns whether anything changed.
bool resolvePending(Kind kind, QVariantMap &map, const Maps &x) {
    QJsonObject pending = pendingOf(map.value("pending").toString());
    if (pending.isEmpty())
        return false;
    bool changed = false;
    for (const RefSpec &r : refsOf(kind)) {
        if (!pending.contains(r.uidKey))
            continue;
        if (!r.list) {
            if (const qint64 rid = x.uid[r.target].value(pending.value(r.uidKey).toString(), 0)) {
                map.insert(r.idKey, rid);
                pending.remove(r.uidKey);
                changed = true;
            }
            continue;
        }
        QStringList ids = map.value(r.idKey).toString().split(',', Qt::SkipEmptyParts);
        QJsonArray rest;
        const QJsonArray waiting = pending.value(r.uidKey).toArray();
        for (const auto &v : waiting) {
            const qint64 rid = x.uid[r.target].value(v.toString(), 0);
            if (!rid)
                rest.append(v);
            else if (!ids.contains(QString::number(rid)))
                ids << QString::number(rid);
        }
        if (rest.size() == waiting.size())
            continue;
        map.insert(r.idKey, ids.join(','));
        if (rest.isEmpty())
            pending.remove(r.uidKey);
        else
            pending.insert(r.uidKey, rest);
        changed = true;
    }
    if (changed)
        map.insert("pending", pendingText(pending));
    return changed;
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
    QHash<qint64, QString> listUids;
    for (const auto &l : s.lists())
        if (!l.uid.isEmpty())
            listUids.insert(l.id, l.uid);
    for (const auto &t : s.tasks()) {
        // A task whose list has not arrived yet sits in a stand-in list; its
        // file still names the real one.
        const QString raw = pendingOf(t.pending).value("list_uid").toString();
        if (!t.uid.isEmpty() && e.lists.contains(raw.isEmpty() ? listUids.value(t.listId) : raw))
            e.tasks.insert(t.uid);
    }
    return e;
}

bool localExcluded(Kind kind, const QString &uid, const QVariantMap &map, const Maps &x, const Exclusion &e) {
    if (!e.any())
        return false;
    switch (kind) {
    case KList: return e.lists.contains(uid);
    case KSection:
    case KTask: return e.lists.contains(refUid(map, "list_id", "list_uid", x.rev[KList]));
    case KAlarm:
    case KAttachment: return e.tasks.contains(refUid(map, "task_id", "task_uid", x.rev[KTask]));
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

// The list new tasks go to: the one chosen in Settings, else the first list in
// sidebar order.
qint64 defaultListId(const Store &store) {
    qint64 preferred = store.setting("tasks.defaultList", 0).toLongLong();
    if (preferred < 0)
        preferred = store.setting("tasks.lastList", 0).toLongLong();
    if (preferred && store.lists().contains(preferred) && !store.list(preferred).deleted)
        return preferred;
    const ViewBuilder views(store, QDateTime::currentDateTime(), ScheduleSettings::fromStore(store));
    const auto lists = views.orderedLists();
    return lists.isEmpty() ? 0 : lists.first().id;
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
        QSet<QString> incomingTasks;
        for (const auto &in : std::as_const(incoming)) {
            if (in.kind == KTask)
                incomingTasks.insert(in.uid);
            if (in.kind == KTask && incomingExcluded(in.kind, in.uid, in.obj, excl))
                excl.tasks.insert(in.uid);
        }
        // An alarm or attachment can arrive without its task when the task
        // belongs to an excluded list and was never imported here.
        auto taskExcluded = [&](const QString &taskUid) {
            if (taskUid.isEmpty() || x.uid[KTask].contains(taskUid) || incomingTasks.contains(taskUid))
                return false;
            QFile f(root.filePath(QString("tasks/%1.json").arg(taskUid)));
            return f.open(QIODevice::ReadOnly) &&
                   excl.lists.contains(QJsonDocument::fromJson(f.readAll()).object().value("list_uid").toString());
        };
        for (const auto &in : std::as_const(incoming)) {
            const bool owned = in.kind == KAlarm || in.kind == KAttachment;
            if (!incomingExcluded(in.kind, in.uid, in.obj, excl) && !(owned && taskExcluded(in.obj.value("task_uid").toString())))
                kept.append(in);
        }
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

    // A task whose list has not arrived yet shows up in the default list.
    qint64 standIn = 0;
    for (const auto &in : std::as_const(apply[KTask]))
        if (!in.obj.value("list_uid").toString().isEmpty() && !x.uid[KList].contains(in.obj.value("list_uid").toString())) {
            standIn = defaultListId(store);
            break;
        }

    store.begin("Sync", false, true);
    for (int k = 0; k < KindCount; ++k) {
        for (const auto &in : std::as_const(apply[k])) {
            const QVariantMap m = fromJson(Kind(k), in.obj, x, ids[k].value(in.uid), standIn);
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
    // References that waited for a record that has now arrived.
    for (int k = 0; k < KindCount; ++k) {
        if (refsOf(Kind(k)).isEmpty())
            continue;
        for (const auto &rec : collect(store, Kind(k))) {
            QVariantMap m = rec.map;
            if (!resolvePending(Kind(k), m, x))
                continue;
            switch (k) {
            case KList: store.putList(TaskList::fromMap(m)); break;
            case KTag: store.putTag(Tag::fromMap(m)); break;
            case KTask: store.putTask(Task::fromMap(m)); break;
            case KAlarm: store.putAlarm(Alarm::fromMap(m)); break;
            case KAttachment: store.putAttachment(Attachment::fromMap(m)); break;
            case KSection: store.putSection(ListSection::fromMap(m)); break;
            default: break;
            }
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
