#pragma once

// When a task is visible, due, overdue, scheduled or "in progress", following
// The date and duration rules. Everything takes `now` explicitly so the
// rules are testable and a whole view is evaluated against one instant.

#include <QDateTime>
#include <QString>

#include "model.h"

class Store;

struct ScheduleSettings {
    bool todayIncludesOverdue = true;
    bool todayIncludesStarred = false;
    bool todayIncludesStillWorking = true;
    bool overdueUsesDueTime = false;
    bool friendlyDates = true;
    // "Include 'Still Working On' tasks" for All and lists: off hides a task
    // whose start date has passed unless it is due today or overdue.
    bool hideStartedTasks = false;
    int weekStart = 1;  // Qt::DayOfWeek the week begins on; Monday unless the setting says otherwise.

    // Reads every one of these from the settings table, so the app, the tray,
    // the CLI and the waybar module agree.
    static ScheduleSettings fromStore(const Store &store);
};

namespace Schedule {

// The first day of the week for "general.weekStart": 0 follows the system
// locale, 1 to 7 is a fixed Qt::DayOfWeek.
int resolveWeekStart(int setting);
// The first day of the week containing `date`.
QDate weekStartOf(const QDate &date, int weekStart);

// Started tasks that "hide unless due today" keeps out of All and lists.
bool startedHidden(const Task &task, const QDateTime &now, const ScheduleSettings &settings);

// A date-only deadline counts as the end of its day.
QDateTime dueMoment(const Task &task);

// When a task starts needing attention: its due time minus its duration, or,
// for a date-only deadline, the due day minus each whole day of duration.
// Invalid when the task has no due date or no duration that reaches back.
QDateTime effectiveStart(const Task &task);

// Sort key for "Due Date" order: the effective start when there is one, else
// the due moment. Undated tasks sort last.
QDateTime dueSortKey(const Task &task);

bool isOverdue(const Task &task, const QDateTime &now, const ScheduleSettings &settings);

// A future start date hides the task from regular views ("Smart Scheduling").
bool isScheduled(const Task &task, const QDateTime &now);

bool isDueToday(const Task &task, const QDateTime &now);

// Between its effective start and its deadline: Today's "In Progress" section.
bool isInProgress(const Task &task, const QDateTime &now);

// Whether the Today focus list shows the task, ignoring list exclusions and
// paused tags, which the caller applies.
bool inToday(const Task &task, const QDateTime &now, const ScheduleSettings &settings);

// "Today", "Tomorrow", "Yesterday", a weekday within the coming week, then
// "In 28 days" / "10 days ago" — or a calendar date when friendly dates are off.
QString friendlyDate(const QDate &date, const QDate &today, bool friendly = true);
QString formatTime(int minutes);
QString friendlyDateTime(const QDate &date, int minutes, const QDate &today, bool friendly = true);

// "30 m", "1 h", "1 h 30 m", "2 d" as the row badge shows it.
QString durationBadge(int minutes);
// "2h 45m" as section headers show totals.
QString durationTotal(int minutes);

// The editor hint bar: "Due in 13h 31m", "Starts in 2d", "Overdue by 3 days".
QString dueHint(const Task &task, const QDateTime &now, const ScheduleSettings &settings);

}  // namespace Schedule
