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

    const QHash<qint64, Task> &tasks() const { return m_tasks; }
    const QHash<qint64, TaskList> &lists() const { return m_lists; }
    const QHash<qint64, ListGroup> &groups() const { return m_groups; }
    const QHash<qint64, Tag> &tags() const { return m_tags; }
    const QHash<qint64, TagGroup> &tagGroups() const { return m_tagGroups; }
    const QHash<qint64, Alarm> &alarms() const { return m_alarms; }
    QVector<Alarm> alarmsFor(qint64 taskId) const;

    Task task(qint64 id) const { return m_tasks.value(id); }
    TaskList list(qint64 id) const { return m_lists.value(id); }
    Tag tag(qint64 id) const { return m_tags.value(id); }

    // Reloads everything from disk, for when another process (the alarm
    // service, a second window) has written to the database.
    void reload();

    // Ids are handed out here rather than by SQLite so a change can refer to
    // records it has not written yet, and undoing a creation is exact.
    qint64 nextId(const QString &table);
    static QString newUid();

    // An `undoable` false change is bookkeeping (an alarm firing) that is
    // written but never offered to undo.
    void begin(const QString &label, bool undoable = true);
    void putTask(const Task &task);
    void putList(const TaskList &list);
    void putGroup(const ListGroup &group);
    void putTag(const Tag &tag);
    void putTagGroup(const TagGroup &group);
    void putAlarm(const Alarm &alarm);
    bool commit();

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoLabel() const { return m_undo.isEmpty() ? QString() : m_undo.last().label; }
    QString redoLabel() const { return m_redo.isEmpty() ? QString() : m_redo.last().label; }
    bool undo();
    bool redo();

    // Removes a record outright and scrubs it from the undo history, for a new
    // task the user abandoned before giving it a title.
    void discard(const QString &table, qint64 id);

    QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    void setSetting(const QString &key, const QVariant &value);

    // Copies the database aside with SQLite's VACUUM INTO, which produces a
    // consistent file even while the app is writing.
    bool backupTo(const QString &file, QString *error = nullptr);

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
    };

    void record(const QString &table, qint64 id, const QVariantMap &before, const QVariantMap &after);
    bool write(const QVector<Edit> &edits, bool forward);
    bool writeRow(const QString &table, const QVariantMap &row);
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
    QHash<QString, qint64> m_maxIds;
    QHash<QString, QVariant> m_settings;
    bool m_inChange = false;
    Change m_pending;
    QVector<Change> m_undo;
    QVector<Change> m_redo;
};
