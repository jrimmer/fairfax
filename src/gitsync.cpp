#include "gitsync.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <memory>

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

QJsonObject toJson(Kind kind, QVariantMap map, const Maps &x) {
    map.remove("id");
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
    if (kind == KTask) {
        obj.insert("tag_uids", tagUids);
        obj.insert("location_uids", locationUids);
    }
    return obj;
}

QByteArray bytesOf(const QJsonObject &obj) { return QJsonDocument(obj).toJson(QJsonDocument::Indented); }

QVariantMap fromJson(Kind kind, const QJsonObject &obj, const Maps &x, qint64 id) {
    QVariantMap map = obj.toVariantMap();
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

struct Incoming {
    Kind kind;
    QString uid;
    QJsonObject obj;
    QByteArray bytes;
};

}  // namespace

namespace GitSyncFiles {

bool writeTree(const Store &store, const QString &directory, const QDateTime &now, QString *error) {
    const Maps x = buildMaps(store);
    QDir root(directory);
    if (!root.mkpath(".")) {
        if (error)
            *error = "Cannot create " + directory;
        return false;
    }
    const qint64 cutoff = stampOf(now.addDays(-TombstoneDays));
    QJsonObject meta;
    meta.insert("format", "precrime-sync");
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

    // Deletions made on the remote (tombstones that were pruned).
    for (const auto &path : removedPaths) {
        const int slash = path.indexOf('/');
        if (slash < 0 || !path.endsWith(".json"))
            continue;
        for (int k = 0; k < KindCount; ++k) {
            if (path.left(slash) != Folders[k])
                continue;
            const qint64 id = x.uid[k].value(QFileInfo(path).completeBaseName(), 0);
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

}  // namespace GitSyncFiles

// ---------------------------------------------------------------------------

namespace {

using GitDone = std::function<void(int code, const QString &out, const QString &err)>;

QString lastLine(const QString &text) {
    const auto lines = text.split('\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

void runGit(QObject *owner, const QString &dir, const QStringList &args, GitDone done) {
    auto *p = new QProcess(owner);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GIT_TERMINAL_PROMPT", "0");
    env.insert("GIT_SSH_COMMAND", "ssh -o BatchMode=yes");
    env.insert("LC_ALL", "C");
    p->setProcessEnvironment(env);
    if (!dir.isEmpty())
        p->setWorkingDirectory(dir);
    QObject::connect(p, &QProcess::errorOccurred, owner, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            done(-1, {}, "git could not be started");
        }
    });
    QObject::connect(p, &QProcess::finished, owner, [p, done](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        done(st == QProcess::NormalExit ? code : -1, out, err);
    });
    p->start("git", args);
}

}  // namespace

GitSync::GitSync(Store *store, const QString &directory, QObject *parent)
    : QObject(parent), m_store(store), m_directory(directory) {
    if (m_directory.isEmpty())
        m_directory = QFileInfo(store->path()).absolutePath() + "/sync";
    m_status = configured() ? "Not synced yet" : "Not set up";

    m_periodic.setInterval(5 * 60 * 1000);
    connect(&m_periodic, &QTimer::timeout, this, &GitSync::syncNow);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(20 * 1000);
    connect(&m_debounce, &QTimer::timeout, this, &GitSync::syncNow);
    // The sync's own writes don't count; an edit made while a sync runs
    // queues another sync right after it.
    connect(store, &Store::changed, this, [this] {
        if (!configured() || m_applying)
            return;
        if (m_busy)
            m_again = true;
        else
            m_debounce.start();
    });
    if (configured())
        m_periodic.start();
}

bool GitSync::configured() const { return !remote().isEmpty(); }
QString GitSync::remote() const { return m_store->setting("git.remote").toString(); }
QDateTime GitSync::lastSync() const {
    return QDateTime::fromString(m_store->setting("git.lastSync").toString(), Qt::ISODate);
}

void GitSync::finish(bool ok, const QString &message) {
    m_busy = false;
    if (ok) {
        m_store->setSetting("git.lastSync", QDateTime::currentDateTime().toString(Qt::ISODate));
        m_status = "Synced";
    } else {
        m_status = message.isEmpty() ? QString("Sync failed") : message;
    }
    setProperty("lastOk", ok);
    emit stateChanged();
    if (ok)
        emit synced(property("changedLocally").toInt());
    if (m_again) {
        m_again = false;
        syncNow();
    }
}

void GitSync::syncNow() {
    if (!configured())
        return;
    if (m_busy) {
        m_again = true;
        return;
    }
    m_debounce.stop();
    m_busy = true;
    m_status = "Syncing…";
    emit stateChanged();
    runCycle([this](bool ok, const QString &msg) { finish(ok, msg); });
}

bool GitSync::syncBlocking(int timeoutMs) {
    if (!configured())
        return false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    connect(this, &GitSync::stateChanged, &loop, [&] {
        if (!m_busy && !m_again)
            loop.quit();
    });
    timeout.start(timeoutMs);
    setProperty("lastOk", false);
    syncNow();
    if (m_busy)
        loop.exec();
    return !timedOut && property("lastOk").toBool();
}

void GitSync::disconnectRemote() {
    m_periodic.stop();
    m_debounce.stop();
    m_store->setSetting("git.remote", QString());
    m_store->setSetting("git.lastCommit", QString());
    m_store->setSetting("git.lastSync", QString());
    m_store->clearSyncLinks("git");
    m_status = "Not set up";
    emit stateChanged();
}

void GitSync::setRemote(const QString &url) {
    if (m_busy)
        return;
    m_busy = true;
    m_status = "Connecting…";
    emit stateChanged();

    auto fail = [this](const QString &msg) { finish(false, msg); };
    auto adopt = [this, url] {
        m_store->setSetting("git.remote", url);
        m_store->setSetting("git.lastCommit", QString());
        m_periodic.start();
        // Run the first cycle through the normal path.
        m_busy = false;
        syncNow();
    };
    auto initRepo = [this, url, fail, adopt] {
        QDir().mkpath(m_directory);
        runGit(this, m_directory, {"init", "-b", "main"}, [=, this](int c, const QString &, const QString &e) {
            if (c)
                return fail(lastLine(e));
            runGit(this, m_directory, {"remote", "add", "origin", url}, [=, this](int c2, const QString &, const QString &e2) {
                if (c2)
                    return fail(lastLine(e2));
                adopt();
            });
        });
    };

    if (QFileInfo::exists(m_directory + "/.git")) {
        runGit(this, m_directory, {"remote", "set-url", "origin", url}, [=, this](int c, const QString &, const QString &) {
            if (!c)
                return adopt();
            runGit(this, m_directory, {"remote", "add", "origin", url}, [=, this](int c2, const QString &, const QString &e2) {
                if (c2)
                    return fail(lastLine(e2));
                adopt();
            });
        });
        return;
    }
    const bool occupied = QDir(m_directory).exists() && !QDir(m_directory).isEmpty();
    if (occupied) {
        initRepo();
        return;
    }
    QDir().mkpath(QFileInfo(m_directory).absolutePath());
    runGit(this, QFileInfo(m_directory).absolutePath(), {"clone", "-o", "origin", url, m_directory},
           [=, this](int c, const QString &, const QString &e) {
               if (!c)
                   return adopt();
               if (e.contains("empty", Qt::CaseInsensitive))
                   return initRepo();
               fail(lastLine(e));
           });
}

namespace {
struct Ctx {
    int attempt = 0;
    QString branch = "main";
    bool remoteHasBranch = false;
    QStringList changed, removed;
    int total = 0;
    std::function<void(bool, const QString &)> done;
    std::function<void()> fetch, findBranch, diff, apply, add, identity, commit, push, finishOk;
    QStringList identityArgs;
    bool ended = false;
};
}  // namespace

void GitSync::runCycle(std::function<void(bool ok, const QString &message)> done) {
    auto c = std::make_shared<Ctx>();
    c->done = std::move(done);
    const QString dir = m_directory;

    auto end = [this, c](bool ok, const QString &msg) {
        if (c->ended)
            return;
        c->ended = true;
        QTimer::singleShot(0, this, [this, c, ok, msg] {
            setProperty("changedLocally", c->total);
            auto done = c->done;
            c->fetch = c->findBranch = c->diff = c->apply = c->add = c->identity = c->commit = c->push =
                c->finishOk = nullptr;
            done(ok, msg);
        });
    };
    auto fail = [end](const QString &msg) { end(false, msg); };
    auto git = [this, dir](const QStringList &args, GitDone cb) { runGit(this, dir, args, cb); };

    c->fetch = [=, this] {
        m_status = "Fetching…";
        emit stateChanged();
        git({"fetch", "origin"}, [=, this](int code, const QString &, const QString &err) {
            if (code)
                return fail(lastLine(err));
            c->findBranch();
        });
    };

    c->findBranch = [=, this] {
        git({"for-each-ref", "--format=%(refname)", "refs/remotes/origin"},
            [=, this](int code, const QString &out, const QString &err) {
                if (code)
                    return fail(lastLine(err));
                const QStringList refs = out.split('\n', Qt::SkipEmptyParts);
                const QString prefix = "refs/remotes/origin/";
                QStringList names;
                for (const auto &r : refs)
                    if (r.startsWith(prefix) && r != prefix + "HEAD")
                        names << r.mid(prefix.size());
                if (names.isEmpty()) {
                    c->remoteHasBranch = false;
                    c->branch = "main";
                    return c->add();
                }
                c->remoteHasBranch = true;
                auto choose = [=, this](const QString &head) {
                    if (!head.isEmpty() && names.contains(head))
                        c->branch = head;
                    else if (names.contains("main"))
                        c->branch = "main";
                    else if (names.contains("master"))
                        c->branch = "master";
                    else
                        c->branch = names.first();
                    c->diff();
                };
                if (refs.contains(prefix + "HEAD")) {
                    git({"symbolic-ref", "--short", "refs/remotes/origin/HEAD"},
                        [=, this](int hc, const QString &hout, const QString &) {
                            QString head = hout.trimmed();
                            if (head.startsWith("origin/"))
                                head = head.mid(7);
                            choose(hc ? QString() : head);
                        });
                } else {
                    choose({});
                }
            });
    };

    c->diff = [=, this] {
        const QString last = m_store->setting("git.lastCommit").toString();
        const QString target = "origin/" + c->branch;
        auto all = [=, this] {
            git({"ls-tree", "-r", "--name-only", target}, [=, this](int code, const QString &out, const QString &err) {
                if (code)
                    return fail(lastLine(err));
                c->changed = out.split('\n', Qt::SkipEmptyParts);
                c->removed.clear();
                c->apply();
            });
        };
        if (last.isEmpty())
            return all();
        git({"diff", "--name-status", "--no-renames", last, target},
            [=, this](int code, const QString &out, const QString &) {
                if (code)
                    return all();  // lastCommit unknown to this clone
                c->changed.clear();
                c->removed.clear();
                for (const auto &line : out.split('\n', Qt::SkipEmptyParts)) {
                    const int tab = line.indexOf('\t');
                    if (tab < 1)
                        continue;
                    (line[0] == 'D' ? c->removed : c->changed) << line.mid(tab + 1);
                }
                c->apply();
            });
    };

    c->apply = [=, this] {
        m_status = "Merging…";
        emit stateChanged();
        git({"reset", "--hard", "origin/" + c->branch}, [=, this](int code, const QString &, const QString &err) {
            if (code)
                return fail(lastLine(err));
            QString error;
            m_applying = true;
            c->total += GitSyncFiles::readTree(*m_store, dir, c->changed, c->removed, &error);
            m_applying = false;
            c->add();
        });
    };

    c->add = [=, this] {
        QString error;
        if (!GitSyncFiles::writeTree(*m_store, dir, QDateTime::currentDateTime(), &error))
            return fail(error);
        git({"add", "-A"}, [=, this](int code, const QString &, const QString &err) {
            if (code)
                return fail(lastLine(err));
            git({"diff", "--cached", "--quiet"}, [=, this](int dc, const QString &, const QString &derr) {
                if (dc == 0)
                    return c->push();  // nothing staged
                if (dc != 1)
                    return fail(lastLine(derr));
                c->identity();
            });
        });
    };

    c->identity = [=, this] {
        git({"config", "user.name"}, [=, this](int nc, const QString &nout, const QString &) {
            git({"config", "user.email"}, [=, this](int ec, const QString &eout, const QString &) {
                c->identityArgs.clear();
                if (nc || nout.trimmed().isEmpty())
                    c->identityArgs << "-c" << "user.name=Precrime";
                if (ec || eout.trimmed().isEmpty())
                    c->identityArgs << "-c" << "user.email=precrime@localhost";
                c->commit();
            });
        });
    };

    c->commit = [=, this] {
        QStringList args = c->identityArgs;
        args << "commit" << "-q" << "-m" << "Sync from " + QSysInfo::machineHostName();
        git(args, [=, this](int code, const QString &out, const QString &err) {
            if (code)
                return fail(lastLine(err.isEmpty() ? out : err));
            c->push();
        });
    };

    c->push = [=, this] {
        m_status = "Pushing…";
        emit stateChanged();
        git({"rev-parse", "--verify", "-q", "HEAD"}, [=, this](int hc, const QString &, const QString &) {
            if (hc)
                return c->finishOk();  // nothing committed yet, nothing to push
            git({"push", "origin", "HEAD:" + c->branch}, [=, this](int code, const QString &, const QString &err) {
                if (code == 0)
                    return c->finishOk();
                const bool rejected = err.contains("rejected") || err.contains("non-fast-forward") ||
                                      err.contains("fetch first") || err.contains("failed to update ref");
                if (rejected && ++c->attempt < 3)
                    return c->fetch();
                fail(lastLine(err));
            });
        });
    };

    c->finishOk = [=, this] {
        git({"rev-parse", "HEAD"}, [=, this](int code, const QString &out, const QString &) {
            if (!code)
                m_store->setSetting("git.lastCommit", out.trimmed());
            end(true, {});
        });
    };

    c->fetch();
}
