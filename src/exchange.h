#pragma once

// Getting tasks in and out: CSV export, iCalendar VTODO export and
// import (with hierarchy carried in RELATED-TO), and import of a task
// database file so moving over keeps lists, groups, tags, dates, repeats and
// alerts.

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>

class Store;
struct Task;

struct ImportResult {
    int lists = 0;
    int tasks = 0;
    int tags = 0;
    int alarms = 0;
    int updated = 0;  // Already-imported tasks that were filled in (e.g. their section).
    QString error;  // Empty on success.
};

namespace Exchange {

// Decodes RFC 2047 encoded words in a mail header value; plain text passes
// through. Shared by the .eml reader and Mail to Fairfax.
QString decodeMimeWords(const QString &raw);

// Which tasks an export includes, as in the "Tasks to export:" popup.
enum class Scope { All, NotDone, Done };

// The non-deleted tasks in `listIds` that match `scope`, in id order. Unlike
// the exporters' own "empty means everything", an empty list set selects
// nothing, since no file is written when no list is ticked.
QList<qint64> selectTasks(const Store &store, const QSet<qint64> &listIds, Scope scope);

// A one-event VCALENDAR for a task, so it can be handed to a calendar app.
// The event runs from the due date and time (else the start) for the task's
// duration, an hour by default; with no time it is an all-day event. Empty
// when the task has no date at all.
QByteArray eventIcs(const Store &store, const Task &task);

// Writes `taskIds` (all non-deleted tasks when empty) as CSV with a header
// row: Title, List, Type, Parent, Notes, Priority, Starred, Start, Due,
// Duration, Tags, Repeat, Completed, Completed At, Created.
bool exportCsv(const Store &store, const QString &path, const QList<qint64> &taskIds = {}, QString *error = nullptr);

// RFC 5545 VCALENDAR of VTODOs. Each VTODO carries its list name in
// X-FAIRFAX-LIST, tags in CATEGORIES,
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

// Imports a task database (an SQLite file with tasks and calendars tables,
// recognised by its contents) as one undoable change, skipping the built-in focus lists.
ImportResult importTaskDatabase(Store &store, const QString &path);

// Imports a Things 3 JSON export (things:///json style with "attributes", or
// the flat title/notes/status/deadline/tags form other exporters write) as one
// undoable change. Areas become lists, projects become projects, checklist
// items stay checklists, "when" becomes the start date and the deadline the due
// date. Items with an id keep it in their uid, so a second import skips them.
ImportResult importThingsJson(Store &store, const QByteArray &json);
ImportResult importThings(Store &store, const QString &path);
bool isThingsExport(const QString &path);

// Imports an OmniFocus TaskPaper export ("File > Export > TaskPaper"): folders
// become list groups, projects lists, nested items sub-tasks (their parent
// becomes a project), @due/@defer/@done/@estimate/@flagged map onto the task's
// fields, other @tags become tags and indented plain lines become notes.
ImportResult importTaskPaperText(Store &store, const QString &text);
ImportResult importOmniFocus(Store &store, const QString &path);
bool isTaskPaper(const QString &path);

// Print / Save as PDF: a clean checklist of a list's tasks under their
// section headings, with due dates, tags and (optionally) notes.
struct PrintItem {
    QString title, due, tags, notes;
    int depth = 0;
    int priority = 0;
    bool done = false;
    bool abandoned = false;
    bool starred = false;
};
struct PrintSection {
    QString heading;
    QVector<PrintItem> items;
};
PrintItem printItem(const Store &store, const Task &task, int depth);
QString printHtml(const QString &title, const QVector<PrintSection> &sections, bool includeNotes);
bool writePdf(const QString &path, const QString &title, const QVector<PrintSection> &sections, bool includeNotes, QString *error = nullptr);

// Tasks as plain text, one "[ ] Title (due …)" line each with notes
// indented under it and listed sub-tasks nested under their parent. Used by
// Email These Tasks and for dragging tasks out of the window as a file.
QString tasksAsText(const Store &store, const QList<qint64> &taskIds);
QUrl mailtoForTasks(const Store &store, const QList<qint64> &taskIds);

// The headers of a mail message (.eml) that matter for linking to it.
struct Eml {
    QString subject, from, messageId;  // The id without its angle brackets.
};
Eml parseEml(const QByteArray &bytes);

}  // namespace Exchange
