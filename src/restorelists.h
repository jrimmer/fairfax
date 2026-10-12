#pragma once

// "Restore Selected Lists" (Settings > Backups): pulls chosen lists out of a
// backup and adds them to the current database as new records, without
// touching anything that is already there. They land in a "Restored" group,
// with their tasks, sub-tasks, sections, tags, locations, alarms and
// attachments, as one undoable change.

#include <QSet>
#include <QString>
#include <QVector>

class Store;

namespace RestoreLists {

struct Info {
    QString uid;
    QString name;
    int tasks = 0;  // Tasks that are not deleted.
};

// The lists a backup holds that can be restored (not smart lists, not deleted).
QVector<Info> listsIn(const Store &backup);

struct Result {
    int lists = 0;
    int tasks = 0;
};

// Copies the lists with these uids from `backup` into `into`. Attachment
// files are read from `attachmentsFolder` (the live attachments folder; a
// backup only holds the database) and copied under new names.
Result merge(Store &into, const Store &backup, const QSet<QString> &listUids, const QString &attachmentsFolder);

}  // namespace RestoreLists
