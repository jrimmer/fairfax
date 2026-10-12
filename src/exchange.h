#pragma once

// Getting tasks in and out: CSV export (as the reference app's File > Export), iCalendar
// VTODO export and import (as the reference app's .ics support, with hierarchy carried in
// RELATED-TO), and import of a the reference app database or .taskdb backup so moving from
// the Mac keeps lists, groups, tags, dates, repeats and alerts.

#include <QList>
#include <QString>

class Store;

struct ImportResult {
    int lists = 0;
    int tasks = 0;
    int tags = 0;
    int alarms = 0;
    QString error;  // Empty on success.
};

namespace Exchange {

// Writes `taskIds` (all non-deleted tasks when empty) as CSV with a header
// row: Title, List, Type, Parent, Notes, Priority, Starred, Start, Due,
// Duration, Tags, Repeat, Completed, Completed At, Created.
bool exportCsv(const Store &store, const QString &path, const QList<qint64> &taskIds = {}, QString *error = nullptr);

// RFC 5545 VCALENDAR of VTODOs. Each VTODO carries its list name in
// X-PRECRIME-LIST, tags in CATEGORIES,
// priority in PRIORITY (1 high, 5 medium, 9 low), start in DTSTART, due in
// DUE, recurrence in RRULE, alerts as VALARM, parents via RELATED-TO.
bool exportIcs(const Store &store, const QString &path, const QList<qint64> &taskIds = {}, QString *error = nullptr);

// Imports VTODOs as one undoable change. Unknown lists are created; tasks
// without a list go to `fallbackListId`.
ImportResult importIcs(Store &store, const QString &path, qint64 fallbackListId);

// Imports a TickTick backup (Settings > Account > Generate Backup, a CSV)
// as one undoable change: folders become list groups, lists become lists
// (Inbox into the Inbox when there is one), checklists keep their items, and
// dates are read in each task's own time zone so all-day tasks keep their day.
// Each task keeps its TickTick id in its uid, so importing the same backup
// again skips what is already here.
ImportResult importTickTick(Store &store, const QString &path);

// True when `path` looks like a TickTick backup CSV.
bool isTickTickBackup(const QString &path);

// Imports a the reference app database (tasks.db, or a .taskdb backup, which is the same
// SQLite file) as one undoable change, skipping the reference app's built-in focus lists.
ImportResult importTwoDo(Store &store, const QString &path);

}  // namespace Exchange
