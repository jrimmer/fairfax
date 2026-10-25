#pragma once

// The store keeps every record in memory and writes through to SQLite. All
// edits go through a change: begin() names it, put*() records each record's
// before and after state, and commit() writes them in one transaction and
// pushes the change onto the undo stack. Undo and redo replay those states, so
// every edit in the app is undoable without per-feature undo code.

#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QVariant>
#include <QVector>

#include "model.h"

class Store : public QObject {
    Q_OBJECT

public:
    explicit Store(QObject *parent = nullptr);
    ~Store() override;

    bool open(const QString &path, QString *error = nullptr);
    QString path() const { return m_path; }
    static int schemaVersion();

    const QHash<qint64, Task> &tasks() const { return m_tasks; }
    const QHash<qint64, TaskList> &lists() const { return m_lists; }
    const QHash<qint64, ListGroup> &groups() const { return m_groups; }
    const QHash<qint64, Tag> &tags() const { return m_tags; }
    const QHash<qint64, TagGroup> &tagGroups() const { return m_tagGroups; }
    const QHash<qint64, Alarm> &alarms() const { return m_alarms; }
    const QHash<qint64, Location> &locations() const { return m_locations; }
    const QHash<qint64, ListSection> &sections() const { return m_sections; }
    QVector<ListSection> sectionsOf(qint64 listId) const;  // In order.
    const QHash<qint64, Attachment> &attachments() const { return m_attachments; }
    // Pending agent proposals, oldest first, as the review sheet shows them.
    QVector<Proposal> proposals() const;
    QVector<Attachment> attachmentsFor(qint64 taskId) const;
    QString attachmentsFolder() const;
    QVector<Alarm> alarmsFor(qint64 taskId) const;

    Task task(qint64 id) const { return m_tasks.value(id); }
    TaskList list(qint64 id) const { return m_lists.value(id); }
    Tag tag(qint64 id) const { return m_tags.value(id); }

    // Reloads everything from disk, for when another process (the alarm
    // service, a second window) has written to the database.
    void reload();

    // Reloads when another process (fairfax ctl, the MCP server) has
    // committed to the database since we last looked, using SQLite's
    // data_version, which only changes for other connections' writes.
    bool reloadIfChangedElsewhere();

    // Ids are handed out here rather than by SQLite so a change can refer to
    // records it has not written yet, and undoing a creation is exact.
    qint64 nextId(const QString &table);
    static QString newUid();

    // An `undoable` false change is bookkeeping (an alarm firing) that is
    // written but never offered to undo. `keepStamps` writes records with the
    // `modified` (and `created`) they carry instead of now, for records
    // arriving from sync, so last-writer-wins compares the real edit times.
    void begin(const QString &label, bool undoable = true, bool keepStamps = false);
    void putTask(const Task &task);
    void putList(const TaskList &list);
    void putGroup(const ListGroup &group);
    void putTag(const Tag &tag);
    void putTagGroup(const TagGroup &group);
    void putAlarm(const Alarm &alarm);
    void putLocation(const Location &location);
    void putSection(const ListSection &section);
    void putAttachment(const Attachment &attachment);
    void putProposal(const Proposal &proposal);
    void removeProposal(qint64 id);
    bool commit();

    // A setting written as part of a change, so one Ctrl+Z puts it back.
    // Ordinary setSetting() is immediate bookkeeping and is not undoable.
    void putSetting(const QString &key, const QVariant &value);

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoLabel() const { return m_undo.isEmpty() ? QString() : m_undo.last().label; }
    QString redoLabel() const { return m_redo.isEmpty() ? QString() : m_redo.last().label; }
    bool undo();
    bool redo();

    // Removes a record outright and scrubs it from the undo history, for a new
    // task the user abandoned before giving it a title.
    void discard(const QString &table, qint64 id);

    // Sync bookkeeping, per provider and entity ("tasks", "lists", "tags"):
    // the remote id of each linked record and a snapshot of the record as it
    // was when last synced, so a sync can tell exactly what changed locally.
    struct SyncLink {
        qint64 localId = 0;
        QString remoteId;
        QVariantMap snapshot;
    };
    QHash<qint64, SyncLink> syncLinks(const QString &provider, const QString &entity) const;
    void setSyncLink(const QString &provider, const QString &entity, const SyncLink &link);
    void removeSyncLink(const QString &provider, const QString &entity, qint64 localId);
    void clearSyncLinks(const QString &provider);

    QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    void setSetting(const QString &key, const QVariant &value);
    void removeSetting(const QString &key);

    // Copies the database aside with SQLite's VACUUM INTO, which produces a
    // consistent file even while the app is writing.
    bool backupTo(const QString &file, QString *error = nullptr);

    // The backups folder beside the database, and a named backup in it:
    // "Fairfax_(date-time)_reason.db". Returns the file name, or an empty
    // string with `error` set. Both the app and `fairfax import` use this.
    // pruneBackups() removes the oldest beyond "backup.keep"; takeBackup()
    // does not do that itself, so a caller can discard its own backup first.
    QString backupFolder() const;
    QString takeBackup(const QString &reason, QString *error = nullptr);
    void pruneBackups();

    // Replaces the whole database with `file` (a backup), after checking it
    // opens as a Fairfax database. Undo history is cleared: there is no
    // undoing past a restore, which is why a safety backup is made first.
    bool replaceWith(const QString &file, QString *error = nullptr);

signals:
    void changed();

private:
    struct Edit {
        QString table;
        qint64 id = 0;
        QVariantMap before;  // Empty when the change created the record.
        QVariantMap after;
    };
    struct Change {
        QString label;
        QVector<Edit> edits;
        bool undoable = true;
        bool keepStamps = false;
    };

    void record(const QString &table, qint64 id, const QVariantMap &before, const QVariantMap &after);
    bool write(const QVector<Edit> &edits, bool forward);
    bool writeRow(const QString &table, const QVariantMap &row);
    bool writeSetting(const QString &key, const QVariantMap &row);
    bool deleteRow(const QString &table, qint64 id);
    void applyToCache(const QString &table, const QVariantMap &row, qint64 id);
    QVariantMap cached(const QString &table, qint64 id) const;
    bool migrate(QString *error);
    void load();

    QString m_connection;
    QString m_path;
    QSqlDatabase m_db;
    QHash<qint64, Task> m_tasks;
    QHash<qint64, TaskList> m_lists;
    QHash<qint64, ListGroup> m_groups;
    QHash<qint64, Tag> m_tags;
    QHash<qint64, TagGroup> m_tagGroups;
    QHash<qint64, Alarm> m_alarms;
    QHash<qint64, Location> m_locations;
    QHash<qint64, ListSection> m_sections;
    QHash<qint64, Attachment> m_attachments;
    QHash<qint64, Proposal> m_proposals;
    QHash<QString, qint64> m_maxIds;
    QHash<QString, QVariant> m_settings;
    bool m_inChange = false;
    qint64 m_dataVersion = -1;
    Change m_pending;
    QVector<Change> m_undo;
    QVector<Change> m_redo;
};
