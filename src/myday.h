#pragma once

// My Day: a daily plan derived from dates, not a task field. A task is in the
// plan because of its due and start dates, so it
// round-trips through every sync provider without a new column.
//
// `now` is passed in everywhere so the rules are testable and a whole view is
// evaluated against one instant. Nothing here touches QML.

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>
#include <functional>

class Store;
class CalendarFeed;

namespace MyDay {

// The clock My Day reads. It is the wall clock unless a test replaces it,
// exactly as CalendarFeed does for the day bar, so a whole day's planning can
// be exercised against a fixed instant.
QDateTime now();
void setClock(std::function<QDateTime()> clock);

// One task in one section, with the short reason the row shows (empty for
// Today).
struct Entry {
    qint64 taskId = 0;
    QString reason;
};

struct Sections {
    QVector<Entry> overdue;       // Everything past its deadline, oldest first.
    QVector<Entry> today;         // Plan for today, from the Today rules.
    QVector<Entry> suggestions;   // Worth planning next, each task once.
};

// The day's plan. Overdue, Today and Suggestions, with no task in two
// sections. Empty while `myday.enabled` is off.
Sections sections(const Store &store, const QDateTime &now);

// The shutdown review's numbers. completedCount is how many tasks were
// completed today; plannedMinutes is the open Today set's durations,
// as the capacity line counts them; doneMinutes is the durations of the tasks
// completed today. Empty while `myday.enabled` is off.
struct Summary {
    int completedCount = 0;
    int plannedMinutes = 0;
    int doneMinutes = 0;
};
Summary summary(const Store &store, const QDateTime &now);

// Whether the work day is over at `now`: the shutdown review is what opening
// My Day lands on after this. False while `myday.enabled` is off.
bool workEnded(const Store &store, const QDateTime &now);

// The end-of-day reminder, setting `myday.shutdownPrompt` (default false): due
// when My Day is on and the setting is on, the work day is over, and the
// reminder has not already fired today. It fires only once a day.
bool shutdownPromptDue(const Store &store, const QDateTime &now);
void markShutdownPrompted(Store &store, const QDate &day);

// What the header line reports:
//  - plannedMinutes: the durations of Today's tasks (a project's
//    sub-tasks count once each, a parent project itself does not);
//  - withoutDuration: how many of those tasks carry no duration;
//  - blockedMinutes: the parts of today's timed task blocks inside the window;
//  - freeMinutes: the window minus the union of timed calendar events;
//  - windowMinutes: [max(now, workStart), workEnd], empty after workEnd.
struct Capacity {
    int plannedMinutes = 0;
    int withoutDuration = 0;
    int blockedMinutes = 0;
    int freeMinutes = 0;
    int windowMinutes = 0;
};
Capacity capacity(const Store &store, const CalendarFeed &calendar, const QDateTime &now);

// The three planning actions. Each is one undoable change, as the Backend does
// it. addToToday sets a task's due date to today, or its start date when its
// deadline is later; removeFromToday pushes a today date to tomorrow;
// hideForToday keeps a task out of the plan until the date changes.
void addToToday(Store &store, const QList<qint64> &ids);
void removeFromToday(Store &store, const QList<qint64> &ids);
void hideForToday(Store &store, const QList<qint64> &ids);

// The first gap of at least `duration` minutes from max(now, workStart) that
// overlaps neither a timed event nor an existing block, in minutes after
// midnight. -1 when the day has no such gap.
int nextFreeSlot(const Store &store, const CalendarFeed &calendar, const QDateTime &now, int duration);

// Bookkeeping: the once-a-day prompt remembers the day it last offered to
// plan.
QDate lastPlanned(const Store &store);
void markPlanned(Store &store, const QDate &day);

// Bookkeeping: the once-a-day end-of-day reminder remembers the day it fired.
QDate lastShutdownPrompted(const Store &store);

}  // namespace MyDay
