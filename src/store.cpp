#include "store.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <algorithm>

namespace {

constexpr int SchemaVersion = 1;
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

const QStringList Tables{"tasks", "lists", "list_groups", "tags", "tag_groups"};

QString errorText(const QSqlQuery &query) { return query.lastError().text(); }

}  // namespace

Store::Store(QObject *parent)
    : QObject(parent), m_connection(QStringLiteral("precrime-%1").arg(reinterpret_cast<quintptr>(this))) {}

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
    return true;
}

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
        QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion),
    };

    m_db.transaction();
    for (const auto &sql : statements) {
        if (!q.exec(sql)) {
            if (error)
                *error = errorText(q);
            m_db.rollback();
            return false;
        }
    }
    return m_db.commit();
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

void Store::begin(const QString &label) {
    m_inChange = true;
    m_pending = Change{label, {}};
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
        copy.uid = newUid();
    copy.modified = QDateTime::currentDateTime();
    if (!copy.created.isValid())
        copy.created = copy.modified;
    record("tasks", copy.id, cached("tasks", copy.id), copy.toMap());
}

void Store::putList(const TaskList &l) {
    TaskList copy = l;
    if (copy.uid.isEmpty())
        copy.uid = newUid();
    copy.modified = QDateTime::currentDateTime();
    record("lists", copy.id, cached("lists", copy.id), copy.toMap());
}

void Store::putGroup(const ListGroup &g) {
    ListGroup copy = g;
    if (copy.uid.isEmpty())
        copy.uid = newUid();
    copy.modified = QDateTime::currentDateTime();
    record("list_groups", copy.id, cached("list_groups", copy.id), copy.toMap());
}

void Store::putTag(const Tag &t) {
    Tag copy = t;
    if (copy.uid.isEmpty())
        copy.uid = newUid();
    copy.modified = QDateTime::currentDateTime();
    record("tags", copy.id, cached("tags", copy.id), copy.toMap());
}

void Store::putTagGroup(const TagGroup &g) {
    TagGroup copy = g;
    if (copy.uid.isEmpty())
        copy.uid = newUid();
    copy.modified = QDateTime::currentDateTime();
    record("tag_groups", copy.id, cached("tag_groups", copy.id), copy.toMap());
}

bool Store::commit() {
    m_inChange = false;
    if (m_pending.edits.isEmpty())
        return true;
    const bool ok = write(m_pending.edits, true);
    m_undo.push_back(m_pending);
    if (m_undo.size() > UndoDepth)
        m_undo.removeFirst();
    m_redo.clear();
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
        const QVariantMap &row = forward ? edit.after : edit.before;
        ok = (row.isEmpty() ? deleteRow(edit.table, edit.id) : writeRow(edit.table, row)) && ok;
    }
    if (!ok) {
        m_db.rollback();
        qWarning("precrime: failed to write a change; it stays in memory only");
        return false;
    }
    return m_db.commit();
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
    columns << "dirty";
    placeholders << "1";
    updates << "dirty = 1";
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
        qWarning("precrime: %s", qPrintable(errorText(q)));
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
    if (table == "tasks") {
        if (row.isEmpty()) m_tasks.remove(id); else m_tasks.insert(id, Task::fromMap(row));
    } else if (table == "lists") {
        if (row.isEmpty()) m_lists.remove(id); else m_lists.insert(id, TaskList::fromMap(row));
    } else if (table == "list_groups") {
        if (row.isEmpty()) m_groups.remove(id); else m_groups.insert(id, ListGroup::fromMap(row));
    } else if (table == "tags") {
        if (row.isEmpty()) m_tags.remove(id); else m_tags.insert(id, Tag::fromMap(row));
    } else if (table == "tag_groups") {
        if (row.isEmpty()) m_tagGroups.remove(id); else m_tagGroups.insert(id, TagGroup::fromMap(row));
    }
}

QVariantMap Store::cached(const QString &table, qint64 id) const {
    if (table == "tasks" && m_tasks.contains(id)) return m_tasks.value(id).toMap();
    if (table == "lists" && m_lists.contains(id)) return m_lists.value(id).toMap();
    if (table == "list_groups" && m_groups.contains(id)) return m_groups.value(id).toMap();
    if (table == "tags" && m_tags.contains(id)) return m_tags.value(id).toMap();
    if (table == "tag_groups" && m_tagGroups.contains(id)) return m_tagGroups.value(id).toMap();
    return {};
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
