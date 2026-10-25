#include "store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSysInfo>
#include <QUuid>
#include <algorithm>

namespace {

constexpr int SchemaVersion = 10;
constexpr int UndoDepth = 200;

// Every syncable table carries the same bookkeeping: a uid that is stable
// across devices, a dirty flag for changes not yet pushed, the provider's id
// for the record, and a soft-delete flag so deletions can be synced too.
const char *SyncColumns = R"(
    uid TEXT NOT NULL UNIQUE,
    modified INTEGER,
    deleted INTEGER NOT NULL DEFAULT 0,
    dirty INTEGER NOT NULL DEFAULT 1,
    remote_id TEXT
)";

const QStringList Tables{"tasks", "lists", "list_groups", "tags", "tag_groups", "alarms", "locations", "attachments", "sections", "proposals"};

// A record saved without a uid is a new one (a copy of another, say): it gets
// its own uid and none of the other writers' data the original carried.
template <typename Record>
void startRecord(Record &r) {
    r.uid = Store::newUid();
    r.ext.clear();
    r.modifiedBy.clear();
    if constexpr (requires { r.pending; })
        r.pending.clear();
}

// A local change: stamped with the time and with this machine as its author.
template <typename Record>
void stampLocal(Record &r) {
    static const QString writer = QStringLiteral("fairfax@") + QSysInfo::machineHostName();
    r.modified = QDateTime::currentDateTime();
    r.modifiedBy = writer;
}

// A pending reference stands for a uid Fairfax cannot resolve yet. Moving the
// record somewhere else (a local edit) ends it.
void dropPending(QString &pending, const char *key) {
    QJsonObject o = QJsonDocument::fromJson(pending.toUtf8()).object();
    o.remove(key);
    pending = o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString errorText(const QSqlQuery &query) { return query.lastError().text(); }

}  // namespace

Store::Store(QObject *parent)
    : QObject(parent), m_connection(QStringLiteral("fairfax-%1").arg(reinterpret_cast<quintptr>(this))) {}

Store::~Store() {
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

QString Store::newUid() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper(); }

bool Store::open(const QString &path, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_path = path;
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA foreign_keys=ON");
    if (!migrate(error))
        return false;
    load();
    migrateSettings();
    return true;
}

void Store::migrateSettings() {
    // "Alerts on date-only tasks fire at" used to share "tasks.defaultDueTime".
    // It starts from that value once, so nobody's alert time changes.
    if (!m_settings.contains("alerts.dateOnlyTime") && m_settings.contains("tasks.defaultDueTime"))
        setSetting("alerts.dateOnlyTime", m_settings.value("tasks.defaultDueTime"));
}

int Store::schemaVersion() { return SchemaVersion; }

bool Store::migrate(QString *error) {
    QSqlQuery q(m_db);
    q.exec("PRAGMA user_version");
    const int version = q.next() ? q.value(0).toInt() : 0;
    if (version >= SchemaVersion)
        return true;

    const QStringList statements{
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS list_groups (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL DEFAULT '',
            sort_order REAL NOT NULL DEFAULT 0,
            collapsed INTEGER NOT NULL DEFAULT 0,
            %1))").arg(SyncColumns),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS lists (
            id INTEGER PRIMARY KEY,
            kind INTEGER NOT NULL DEFAULT 0,
            group_id INTEGER NOT NULL DEFAULT 0,
            name TEXT NOT NULL DEFAULT '',
            color INTEGER NOT NULL DEFAULT 0,
            sort_order REAL NOT NULL DEFAULT 0,
            sort_field INTEGER NOT NULL DEFAULT 0,
            sort_ascending INTEGER NOT NULL DEFAULT 1,
            separated INTEGER NOT NULL DEFAULT 1,
            show_in_all INTEGER NOT NULL DEFAULT 1,
            show_in_today INTEGER NOT NULL DEFAULT 1,
            show_in_done INTEGER NOT NULL DEFAULT 1,
            archived INTEGER NOT NULL DEFAULT 0,
            smart_query TEXT NOT NULL DEFAULT '',
            %1))").arg(SyncColumns),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS tasks (
            id INTEGER PRIMARY KEY,
            list_id INTEGER NOT NULL DEFAULT 0,
            parent_id INTEGER NOT NULL DEFAULT 0,
            type INTEGER NOT NULL DEFAULT 0,
            title TEXT NOT NULL DEFAULT '',
            notes TEXT NOT NULL DEFAULT '',
            priority INTEGER NOT NULL DEFAULT 0,
            starred INTEGER NOT NULL DEFAULT 0,
            start_date TEXT,
            start_time INTEGER NOT NULL DEFAULT -1,
            due_date TEXT,
            due_time INTEGER NOT NULL DEFAULT -1,
            duration INTEGER NOT NULL DEFAULT 0,
            completed INTEGER NOT NULL DEFAULT 0,
            completed_at INTEGER,
            action_type INTEGER NOT NULL DEFAULT 0,
            action_value TEXT NOT NULL DEFAULT '',
            rec_frequency INTEGER NOT NULL DEFAULT 0,
            rec_interval INTEGER NOT NULL DEFAULT 1,
            rec_from INTEGER NOT NULL DEFAULT 0,
            rec_weekdays INTEGER NOT NULL DEFAULT 0,
            rec_month_ordinal INTEGER NOT NULL DEFAULT 0,
            rec_month_weekday INTEGER NOT NULL DEFAULT 0,
            rec_end_type INTEGER NOT NULL DEFAULT 0,
            rec_end_count INTEGER NOT NULL DEFAULT 0,
            rec_end_date TEXT,
            rec_done INTEGER NOT NULL DEFAULT 0,
            series_uid TEXT NOT NULL DEFAULT '',
            sort_order REAL NOT NULL DEFAULT 0,
            collapsed INTEGER NOT NULL DEFAULT 0,
            tag_ids TEXT NOT NULL DEFAULT '',
            created INTEGER,
            %1))").arg(SyncColumns),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS tag_groups (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL DEFAULT '',
            sort_order REAL NOT NULL DEFAULT 0,
            collapsed INTEGER NOT NULL DEFAULT 0,
            %1))").arg(SyncColumns),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS tags (
            id INTEGER PRIMARY KEY,
            group_id INTEGER NOT NULL DEFAULT 0,
            name TEXT NOT NULL DEFAULT '',
            paused INTEGER NOT NULL DEFAULT 0,
            sort_order REAL NOT NULL DEFAULT 0,
            %1))").arg(SyncColumns),
        QStringLiteral("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS tasks_list ON tasks(list_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS tasks_parent ON tasks(parent_id)"),
    };
    // Version 2 adds alarms.
    const QStringList version2{
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS alarms (
            id INTEGER PRIMARY KEY,
            task_id INTEGER NOT NULL DEFAULT 0,
            kind INTEGER NOT NULL DEFAULT 0,
            offset_minutes INTEGER NOT NULL DEFAULT 0,
            at INTEGER,
            sound INTEGER NOT NULL DEFAULT 1,
            snoozed_until INTEGER,
            fired_for INTEGER,
            %1))").arg(SyncColumns),
        QStringLiteral("CREATE INDEX IF NOT EXISTS alarms_task ON alarms(task_id)"),
    };

    // Version 3 adds sync bookkeeping.
    const QStringList version3{
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS sync_links (
            provider TEXT NOT NULL,
            entity TEXT NOT NULL,
            local_id INTEGER NOT NULL,
            remote_id TEXT NOT NULL,
            snapshot TEXT NOT NULL DEFAULT '',
            PRIMARY KEY (provider, entity, local_id)))"),
    };

    // Version 4 adds locations and attachments.
    const QStringList version4{
        QStringLiteral("ALTER TABLE tasks ADD COLUMN location_ids TEXT NOT NULL DEFAULT ''"),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS locations (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL DEFAULT '',
            address TEXT NOT NULL DEFAULT '',
            latitude REAL NOT NULL DEFAULT 0,
            longitude REAL NOT NULL DEFAULT 0,
            paused INTEGER NOT NULL DEFAULT 0,
            %1))").arg(SyncColumns),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS attachments (
            id INTEGER PRIMARY KEY,
            task_id INTEGER NOT NULL DEFAULT 0,
            kind INTEGER NOT NULL DEFAULT 2,
            name TEXT NOT NULL DEFAULT '',
            file_name TEXT NOT NULL DEFAULT '',
            bytes INTEGER NOT NULL DEFAULT 0,
            created INTEGER,
            %1))").arg(SyncColumns),
    };

    // Version 5 gives Smart Lists a date range.
    const QStringList version5{
        QStringLiteral("ALTER TABLE lists ADD COLUMN smart_range TEXT NOT NULL DEFAULT ''"),
    };

    // Version 6 adds sections within lists.
    const QStringList version6{
        QStringLiteral("ALTER TABLE tasks ADD COLUMN section_id INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS sections (
            id INTEGER PRIMARY KEY,
            list_id INTEGER NOT NULL DEFAULT 0,
            name TEXT NOT NULL DEFAULT '',
            sort_order REAL NOT NULL DEFAULT 0,
            %1))").arg(SyncColumns),
    };

    // Version 7 adds per-list completed/scheduled/alert switches and list
    // group colours.
    const QStringList version7{
        QStringLiteral("ALTER TABLE lists ADD COLUMN completed_mode INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE lists ADD COLUMN include_scheduled INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE lists ADD COLUMN mute_alerts INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE list_groups ADD COLUMN color INTEGER NOT NULL DEFAULT -1"),
    };

    // Version 8 adds the agent's proposal queue.
    const QStringList version8{
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS proposals (
            id INTEGER PRIMARY KEY,
            kind TEXT NOT NULL DEFAULT '',
            payload TEXT NOT NULL DEFAULT '',
            created INTEGER))"),
    };

    // Version 9 keeps other writers' data (the sync files' "ext") as JSON text,
    // who last changed a record ("modified_by"), and the uids a record names
    // that are not here yet ("pending").
    QStringList version9;
    for (const char *table : {"tasks", "lists", "list_groups", "tags", "tag_groups", "alarms", "locations", "attachments", "sections"})
        version9 << QStringLiteral("ALTER TABLE %1 ADD COLUMN ext TEXT NOT NULL DEFAULT ''").arg(table)
                 << QStringLiteral("ALTER TABLE %1 ADD COLUMN modified_by TEXT NOT NULL DEFAULT ''").arg(table);

    for (const char *table : {"tasks", "lists", "tags", "alarms", "attachments", "sections"})
        version9 << QStringLiteral("ALTER TABLE %1 ADD COLUMN pending TEXT NOT NULL DEFAULT ''").arg(table);

    // Version 10 adds abandoned tasks ("won't do"): closed, but not done.
    const QStringList version10{
        QStringLiteral("ALTER TABLE tasks ADD COLUMN abandoned INTEGER NOT NULL DEFAULT 0"),
    };

    QStringList steps;
    if (version < 1)
        steps += statements;
    if (version < 2)
        steps += version2;
    if (version < 3)
        steps += version3;
    if (version < 4)
        steps += version4;
    if (version < 5)
        steps += version5;
    if (version < 6)
        steps += version6;
    if (version < 7)
        steps += version7;
    if (version < 8)
        steps += version8;
    if (version < 9)
        steps += version9;
    if (version < 10)
        steps += version10;
    steps << QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion);

    m_db.transaction();
    for (const auto &sql : steps) {
        if (!q.exec(sql)) {
            if (error)
                *error = errorText(q);
            m_db.rollback();
            return false;
        }
    }
    return m_db.commit();
}

void Store::reload() {
    m_tasks.clear();
    m_lists.clear();
    m_groups.clear();
    m_tags.clear();
    m_tagGroups.clear();
    m_alarms.clear();
    m_locations.clear();
    m_attachments.clear();
    m_sections.clear();
    m_proposals.clear();
    m_settings.clear();
    load();
    emit changed();
}

bool Store::reloadIfChangedElsewhere() {
    QSqlQuery q(m_db);
    if (!q.exec("PRAGMA data_version") || !q.next())
        return false;
    const qint64 version = q.value(0).toLongLong();
    const bool changed = m_dataVersion >= 0 && version != m_dataVersion;
    m_dataVersion = version;
    if (changed)
        reload();
    return changed;
}

QVector<Alarm> Store::alarmsFor(qint64 taskId) const {
    QVector<Alarm> result;
    for (const auto &a : m_alarms)
        if (a.taskId == taskId && !a.deleted)
            result.append(a);
    std::sort(result.begin(), result.end(), [](const Alarm &x, const Alarm &y) { return x.id < y.id; });
    return result;
}

void Store::load() {
    for (const auto &table : Tables) {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT * FROM %1").arg(table));
        qint64 maxId = 0;
        while (q.next()) {
            QVariantMap row;
            const auto rec = q.record();
            for (int i = 0; i < rec.count(); ++i)
                row.insert(rec.fieldName(i), q.value(i));
            const qint64 id = row.value("id").toLongLong();
            maxId = qMax(maxId, id);
            applyToCache(table, row, id);
        }
        m_maxIds[table] = maxId;
    }
    QSqlQuery s(m_db);
    s.exec("SELECT key, value FROM settings");
    while (s.next())
        m_settings.insert(s.value(0).toString(), s.value(1));
}

qint64 Store::nextId(const QString &table) { return ++m_maxIds[table]; }

void Store::begin(const QString &label, bool undoable, bool keepStamps) {
    m_inChange = true;
    m_pending = Change{label, {}, undoable, keepStamps};
}

void Store::record(const QString &table, qint64 id, const QVariantMap &before, const QVariantMap &after) {
    // Several puts of the same record in one change collapse into one edit
    // that keeps the very first "before".
    for (auto &edit : m_pending.edits) {
        if (edit.table == table && edit.id == id) {
            edit.after = after;
            applyToCache(table, after, id);
            return;
        }
    }
    m_pending.edits.push_back({table, id, before, after});
    applyToCache(table, after, id);
}

void Store::putTask(const Task &t) {
    Task copy = t;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    if (!copy.created.isValid())
        copy.created = copy.modified;
    if (!m_pending.keepStamps && !copy.pending.isEmpty()) {
        const auto old = m_tasks.constFind(copy.id);
        if (old != m_tasks.cend()) {
            if (old->listId != copy.listId)
                dropPending(copy.pending, "list_uid");
            if (old->sectionId != copy.sectionId)
                dropPending(copy.pending, "section_uid");
            if (old->parentId != copy.parentId)
                dropPending(copy.pending, "parent_uid");
        }
    }
    record("tasks", copy.id, cached("tasks", copy.id), copy.toMap());
}

void Store::putList(const TaskList &l) {
    TaskList copy = l;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    if (!m_pending.keepStamps && !copy.pending.isEmpty()) {
        const auto old = m_lists.constFind(copy.id);
        if (old != m_lists.cend() && old->groupId != copy.groupId)
            dropPending(copy.pending, "group_uid");
    }
    record("lists", copy.id, cached("lists", copy.id), copy.toMap());
}

void Store::putGroup(const ListGroup &g) {
    ListGroup copy = g;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    record("list_groups", copy.id, cached("list_groups", copy.id), copy.toMap());
}

void Store::putTag(const Tag &t) {
    Tag copy = t;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    if (!m_pending.keepStamps && !copy.pending.isEmpty()) {
        const auto old = m_tags.constFind(copy.id);
        if (old != m_tags.cend() && old->groupId != copy.groupId)
            dropPending(copy.pending, "group_uid");
    }
    record("tags", copy.id, cached("tags", copy.id), copy.toMap());
}

void Store::putAlarm(const Alarm &a) {
    Alarm copy = a;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    record("alarms", copy.id, cached("alarms", copy.id), copy.toMap());
}

void Store::putLocation(const Location &l) {
    Location copy = l;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    record("locations", copy.id, cached("locations", copy.id), copy.toMap());
}

void Store::putSection(const ListSection &section) {
    ListSection copy = section;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    if (!m_pending.keepStamps && !copy.pending.isEmpty()) {
        const auto old = m_sections.constFind(copy.id);
        if (old != m_sections.cend() && old->listId != copy.listId)
            dropPending(copy.pending, "list_uid");
    }
    record("sections", copy.id, cached("sections", copy.id), copy.toMap());
}

QVector<ListSection> Store::sectionsOf(qint64 listId) const {
    QVector<ListSection> result;
    for (const auto &s : m_sections)
        if (s.listId == listId && !s.deleted)
            result.append(s);
    std::sort(result.begin(), result.end(), [](const ListSection &a, const ListSection &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return result;
}

void Store::putAttachment(const Attachment &a) {
    Attachment copy = a;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    if (!copy.created.isValid())
        copy.created = copy.modified;
    record("attachments", copy.id, cached("attachments", copy.id), copy.toMap());
}

void Store::putProposal(const Proposal &p) {
    Proposal copy = p;
    if (!copy.created.isValid())
        copy.created = QDateTime::currentDateTime();
    record("proposals", copy.id, cached("proposals", copy.id), copy.toMap());
}

void Store::removeProposal(qint64 id) {
    record("proposals", id, cached("proposals", id), {});
}

QVector<Proposal> Store::proposals() const {
    QVector<Proposal> result;
    for (const auto &p : m_proposals)
        result.append(p);
    std::sort(result.begin(), result.end(), [](const Proposal &a, const Proposal &b) {
        return a.created != b.created ? a.created < b.created : a.id < b.id;
    });
    return result;
}

void Store::putSetting(const QString &key, const QVariant &value) {
    // Settings are not rows, so the journal carries them as a synthetic table
    // whose one field is the value; undo writes the old text back.
    const QVariantMap before = m_settings.contains(key) ? QVariantMap{{"value", m_settings.value(key)}} : QVariantMap{};
    m_settings.insert(key, value);
    const QVariantMap after{{"value", value}};
    record("settings:" + key, 0, before, after);
}

QVector<Attachment> Store::attachmentsFor(qint64 taskId) const {
    QVector<Attachment> result;
    for (const auto &a : m_attachments)
        if (a.taskId == taskId && !a.deleted)
            result.append(a);
    std::sort(result.begin(), result.end(), [](const Attachment &x, const Attachment &y) { return x.id < y.id; });
    return result;
}

QString Store::attachmentsFolder() const { return QFileInfo(m_path).absolutePath() + "/attachments"; }

void Store::putTagGroup(const TagGroup &g) {
    TagGroup copy = g;
    if (copy.uid.isEmpty())
        startRecord(copy);
    if (!m_pending.keepStamps || !copy.modified.isValid())
        stampLocal(copy);
    record("tag_groups", copy.id, cached("tag_groups", copy.id), copy.toMap());
}

bool Store::commit() {
    m_inChange = false;
    if (m_pending.edits.isEmpty())
        return true;
    const bool ok = write(m_pending.edits, true);
    if (m_pending.undoable) {
        m_undo.push_back(m_pending);
        if (m_undo.size() > UndoDepth)
            m_undo.removeFirst();
        m_redo.clear();
    }
    m_pending = {};
    emit changed();
    return ok;
}

bool Store::undo() {
    if (m_undo.isEmpty())
        return false;
    const Change change = m_undo.takeLast();
    for (const auto &edit : change.edits)
        applyToCache(edit.table, edit.before, edit.id);
    const bool ok = write(change.edits, false);
    m_redo.push_back(change);
    emit changed();
    return ok;
}

bool Store::redo() {
    if (m_redo.isEmpty())
        return false;
    const Change change = m_redo.takeLast();
    for (const auto &edit : change.edits)
        applyToCache(edit.table, edit.after, edit.id);
    const bool ok = write(change.edits, true);
    m_undo.push_back(change);
    emit changed();
    return ok;
}

void Store::discard(const QString &table, qint64 id) {
    applyToCache(table, {}, id);
    deleteRow(table, id);
    for (auto *stack : {&m_undo, &m_redo}) {
        for (int i = stack->size() - 1; i >= 0; --i) {
            auto &edits = (*stack)[i].edits;
            edits.erase(std::remove_if(edits.begin(), edits.end(),
                                       [&](const Edit &e) { return e.table == table && e.id == id; }),
                        edits.end());
            if (edits.isEmpty())
                stack->removeAt(i);
        }
    }
    emit changed();
}

bool Store::write(const QVector<Edit> &edits, bool forward) {
    m_db.transaction();
    bool ok = true;
    for (const auto &edit : edits) {
        if (edit.table.startsWith(QLatin1String("settings:"))) {
            ok = writeSetting(edit.table.mid(int(qstrlen("settings:"))), forward ? edit.after : edit.before) && ok;
            continue;
        }
        const QVariantMap &row = forward ? edit.after : edit.before;
        ok = (row.isEmpty() ? deleteRow(edit.table, edit.id) : writeRow(edit.table, row)) && ok;
    }
    if (!ok) {
        m_db.rollback();
        qWarning("fairfax: failed to write a change; it stays in memory only");
        return false;
    }
    return m_db.commit();
}

bool Store::writeSetting(const QString &key, const QVariantMap &row) {
    if (row.isEmpty()) {
        QSqlQuery q(m_db);
        q.prepare("DELETE FROM settings WHERE key = ?");
        q.addBindValue(key);
        return q.exec();
    }
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.addBindValue(key);
    q.addBindValue(row.value("value").toString());
    return q.exec();
}

bool Store::writeRow(const QString &table, const QVariantMap &row) {
    // Rows come straight from a record's toMap(), whose keys are the table's
    // column names, so one upsert serves every table.
    QStringList columns = row.keys();
    QStringList placeholders, updates;
    for (const auto &c : columns) {
        placeholders << ":" + c;
        if (c != "id")
            updates << QStringLiteral("%1 = excluded.%1").arg(c);
    }
    // The proposals table carries no sync bookkeeping, so no dirty flag.
    if (table != QLatin1String("proposals")) {
        columns << "dirty";
        placeholders << "1";
        updates << "dirty = 1";
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3) ON CONFLICT(id) DO UPDATE SET %4")
                  .arg(table, columns.join(", "), placeholders.join(", "), updates.join(", ")));
    for (auto it = row.cbegin(); it != row.cend(); ++it) {
        QVariant value = it.value();
        if (value.typeId() == QMetaType::Bool)
            value = value.toBool() ? 1 : 0;
        // An empty QString binds as SQL NULL; text columns want ''.
        else if (value.typeId() == QMetaType::QString && value.toString().isNull())
            value = QStringLiteral("");
        q.bindValue(":" + it.key(), value);
    }
    if (!q.exec()) {
        qWarning("fairfax: %s", qPrintable(errorText(q)));
        return false;
    }
    return true;
}

bool Store::deleteRow(const QString &table, qint64 id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(table));
    q.addBindValue(id);
    return q.exec();
}

void Store::applyToCache(const QString &table, const QVariantMap &row, qint64 id) {
    if (table.startsWith(QLatin1String("settings:"))) {
        const QString key = table.mid(int(qstrlen("settings:")));
        if (row.isEmpty()) m_settings.remove(key);
        else m_settings.insert(key, row.value("value"));
    } else if (table == "tasks") {
        if (row.isEmpty()) m_tasks.remove(id); else m_tasks.insert(id, Task::fromMap(row));
    } else if (table == "lists") {
        if (row.isEmpty()) m_lists.remove(id); else m_lists.insert(id, TaskList::fromMap(row));
    } else if (table == "list_groups") {
        if (row.isEmpty()) m_groups.remove(id); else m_groups.insert(id, ListGroup::fromMap(row));
    } else if (table == "tags") {
        if (row.isEmpty()) m_tags.remove(id); else m_tags.insert(id, Tag::fromMap(row));
    } else if (table == "tag_groups") {
        if (row.isEmpty()) m_tagGroups.remove(id); else m_tagGroups.insert(id, TagGroup::fromMap(row));
    } else if (table == "alarms") {
        if (row.isEmpty()) m_alarms.remove(id); else m_alarms.insert(id, Alarm::fromMap(row));
    } else if (table == "locations") {
        if (row.isEmpty()) m_locations.remove(id); else m_locations.insert(id, Location::fromMap(row));
    } else if (table == "attachments") {
        if (row.isEmpty()) m_attachments.remove(id); else m_attachments.insert(id, Attachment::fromMap(row));
    } else if (table == "sections") {
        if (row.isEmpty()) m_sections.remove(id); else m_sections.insert(id, ListSection::fromMap(row));
    } else if (table == "proposals") {
        if (row.isEmpty()) m_proposals.remove(id); else m_proposals.insert(id, Proposal::fromMap(row));
    }
}

QVariantMap Store::cached(const QString &table, qint64 id) const {
    if (table == "tasks" && m_tasks.contains(id)) return m_tasks.value(id).toMap();
    if (table == "lists" && m_lists.contains(id)) return m_lists.value(id).toMap();
    if (table == "list_groups" && m_groups.contains(id)) return m_groups.value(id).toMap();
    if (table == "tags" && m_tags.contains(id)) return m_tags.value(id).toMap();
    if (table == "tag_groups" && m_tagGroups.contains(id)) return m_tagGroups.value(id).toMap();
    if (table == "alarms" && m_alarms.contains(id)) return m_alarms.value(id).toMap();
    if (table == "locations" && m_locations.contains(id)) return m_locations.value(id).toMap();
    if (table == "attachments" && m_attachments.contains(id)) return m_attachments.value(id).toMap();
    if (table == "sections" && m_sections.contains(id)) return m_sections.value(id).toMap();
    if (table == "proposals" && m_proposals.contains(id)) return m_proposals.value(id).toMap();
    return {};
}

QHash<qint64, Store::SyncLink> Store::syncLinks(const QString &provider, const QString &entity) const {
    QHash<qint64, SyncLink> links;
    QSqlQuery q(m_db);
    q.prepare("SELECT local_id, remote_id, snapshot FROM sync_links WHERE provider = ? AND entity = ?");
    q.addBindValue(provider);
    q.addBindValue(entity);
    q.exec();
    while (q.next()) {
        SyncLink link;
        link.localId = q.value(0).toLongLong();
        link.remoteId = q.value(1).toString();
        link.snapshot = QJsonDocument::fromJson(q.value(2).toByteArray()).object().toVariantMap();
        links.insert(link.localId, link);
    }
    return links;
}

void Store::setSyncLink(const QString &provider, const QString &entity, const SyncLink &link) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO sync_links (provider, entity, local_id, remote_id, snapshot) VALUES (?, ?, ?, ?, ?) "
              "ON CONFLICT(provider, entity, local_id) DO UPDATE SET remote_id = excluded.remote_id, "
              "snapshot = excluded.snapshot");
    q.addBindValue(provider);
    q.addBindValue(entity);
    q.addBindValue(link.localId);
    q.addBindValue(link.remoteId);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(link.snapshot)).toJson(QJsonDocument::Compact)));
    q.exec();
}

void Store::removeSyncLink(const QString &provider, const QString &entity, qint64 localId) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM sync_links WHERE provider = ? AND entity = ? AND local_id = ?");
    q.addBindValue(provider);
    q.addBindValue(entity);
    q.addBindValue(localId);
    q.exec();
}

void Store::clearSyncLinks(const QString &provider) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM sync_links WHERE provider = ?");
    q.addBindValue(provider);
    q.exec();
}

QVariant Store::setting(const QString &key, const QVariant &fallback) const {
    return m_settings.value(key, fallback);
}

void Store::setSetting(const QString &key, const QVariant &value) {
    m_settings.insert(key, value);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO settings (key, value) VALUES (?, ?) "
              "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.addBindValue(key);
    q.addBindValue(value.toString());
    q.exec();
}

void Store::removeSetting(const QString &key) {
    m_settings.remove(key);
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM settings WHERE key = ?");
    q.addBindValue(key);
    q.exec();
}

bool Store::backupTo(const QString &file, QString *error) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSqlQuery q(m_db);
    q.prepare("VACUUM INTO ?");
    q.addBindValue(file);
    if (!q.exec()) {
        if (error)
            *error = errorText(q);
        return false;
    }
    return true;
}

QString Store::backupFolder() const {
    return QFileInfo(m_path).absolutePath() + "/backups";
}

QString Store::takeBackup(const QString &reason, QString *error) {
    const QString name = QStringLiteral("Fairfax_(%1)%2.db")
                             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HH_mm_ss"),
                                  reason.isEmpty() ? QString() : "_" + reason);
    if (!backupTo(backupFolder() + "/" + name, error))
        return {};
    setSetting("backup.last", QDateTime::currentSecsSinceEpoch());
    return name;
}

void Store::pruneBackups() {
    // Keep at most N automatic backups; safety backups taken before a
    // restore or import are pruned the same way.
    const int keep = qMax(1, setting("backup.keep", 10).toInt());
    QDir dir(backupFolder());
    const auto files = dir.entryInfoList({"Fairfax_*.db"}, QDir::Files, QDir::Time);
    for (int i = keep; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

bool Store::replaceWith(const QString &file, QString *error) {
    {
        // Check the candidate before touching anything.
        const QString probeName = m_connection + "-probe";
        {
            QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", probeName);
            probe.setDatabaseName(file);
            probe.setConnectOptions("QSQLITE_OPEN_READONLY");
            bool ok = probe.open();
            if (ok) {
                QSqlQuery q(probe);
                ok = q.exec("SELECT count(*) FROM tasks") && q.exec("SELECT count(*) FROM lists");
            }
            probe.close();
            if (!ok) {
                if (error)
                    *error = QStringLiteral("%1 isn't a Fairfax database.").arg(file);
                QSqlDatabase::removeDatabase(probeName);
                return false;
            }
        }
        QSqlDatabase::removeDatabase(probeName);
    }

    m_db.close();
    // Copy beside the target, then rename over it, so a failed copy never
    // leaves a half-written database.
    const QString staging = m_path + ".restoring";
    QFile::remove(staging);
    bool ok = QFile::copy(file, staging);
    if (ok) {
        QFile::remove(m_path + "-wal");
        QFile::remove(m_path + "-shm");
        QFile::remove(m_path);
        ok = QFile::rename(staging, m_path);
    }
    if (!m_db.open() || !ok) {
        if (error)
            *error = QStringLiteral("Couldn't restore from %1.").arg(file);
        return false;
    }
    QString migrateError;
    migrate(&migrateError);
    m_undo.clear();
    m_redo.clear();
    reload();
    migrateSettings();
    return true;
}

