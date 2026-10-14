#include "myday.h"

#include <QSet>
#include <QTime>
#include <algorithm>

#include "calendar.h"
#include "model.h"
#include "schedule.h"
#include "store.h"
#include "views.h"

namespace {

// The keys and defaults the whole feature reads.
constexpr int kDefaultWorkStart = 9 * 60;
constexpr int kDefaultWorkEnd = 17 * 60;

bool enabled(const Store &store) { return store.setting("myday.enabled", true).toBool(); }
int workStart(const Store &store) { return store.setting("myday.workStart", kDefaultWorkStart).toInt(); }
int workEnd(const Store &store) { return store.setting("myday.workEnd", kDefaultWorkEnd).toInt(); }

// The uids hidden for `day`, as "<iso date>|<uid>,<uid>". A stored date that is
// not the day we are planning counts as empty: the hiding only ever lasts one
// day and is cleared when the date changes.
QSet<QString> hiddenUids(const Store &store, const QDate &day) {
    const QString stored = store.setting("myday.hidden").toString();
    const int bar = stored.indexOf(QLatin1Char('|'));
    if (bar < 0)
        return {};
    if (QDate::fromString(stored.left(bar), Qt::ISODate) != day)
        return {};
    const QStringList uids = stored.mid(bar + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
    return QSet<QString>(uids.begin(), uids.end());
}

QString hiddenSetting(const QDate &day, const QSet<QString> &uids) {
    QStringList sorted(uids.begin(), uids.end());
    sorted.sort();
    return day.toString(Qt::ISODate) + QLatin1Char('|') + sorted.join(QLatin1Char(','));
}

// Project children are planned in their own right; checklist items are not, so
// only the checklist row shows. Parents that are projects count once each for
// capacity, but never as a task of their own.
bool excluded(const Store &store, const ViewBuilder &views, const QSet<QString> &hidden, const Task &t) {
    if (t.deleted || t.completed)
        return true;
    if (hidden.contains(t.uid))
        return true;
    if (!views.listVisible(t.listId))
        return true;
    if (t.parentId) {
        const Task parent = store.task(t.parentId);
        if (!parent.id || parent.deleted || parent.type != TaskType::Project)
            return true;
    }
    return false;
}

// Priority descending, then due date ascending (undated last), then duration
// ascending, then id, so the order is stable and testable.
bool plannedOrder(const Task &a, const Task &b) {
    if (a.priority != b.priority)
        return a.priority > b.priority;
    if (a.dueDate.isValid() != b.dueDate.isValid())
        return a.dueDate.isValid();
    if (a.dueDate != b.dueDate)
        return a.dueDate < b.dueDate;
    if (a.duration != b.duration)
        return a.duration < b.duration;
    return a.id < b.id;
}

QString overdueReason(const Task &t, const QDate &today) {
    const qint64 days = t.dueDate.daysTo(today);
    if (days <= 0)
        return QStringLiteral("Overdue");
    if (days == 1)
        return QStringLiteral("1 day overdue");
    return QStringLiteral("%1 days overdue").arg(days);
}

// The first suggestion rule that matches, in the order the design asks for.
QString suggestionReason(const Store &store, const Task &t, const QDateTime &now) {
    const QDate today = now.date();
    if (t.starred)
        return QStringLiteral("Starred");
    if (t.startDate == today)
        return QStringLiteral("Starts today");
    if (t.dueDate == today.addDays(1) || t.dueDate == today.addDays(2))
        return QStringLiteral("Due ") + t.dueDate.toString(QStringLiteral("ddd"));
    for (const Alarm &a : store.alarmsFor(t.id)) {
        if (a.deleted || a.kind != Alarm::Absolute || !a.at.isValid())
            continue;
        if (a.at.date() == today)
            return QStringLiteral("Alert %1").arg(a.at.time().toString(QStringLiteral("HH:mm")));
    }
    if (t.startDate.isValid() && t.startDate < today && !t.completed)
        return QStringLiteral("Still working on");
    return {};
}

// A task's time block: it ends at its due time and lasts `duration`.
bool blockOf(const Task &t, const QDate &day, int *start, int *end) {
    if (t.deleted || t.completed || t.dueDate != day || t.dueTime < 0 || t.duration <= 0)
        return false;
    *end = t.dueTime;
    *start = qMax(0, t.dueTime - t.duration);
    return *end > *start;
}

QList<QPair<int, int>> busyIntervals(const Store &store, const CalendarFeed &calendar, const QDate &day) {
    QList<QPair<int, int>> busy;
    for (const QVariant &v : calendar.eventsFor(day.toString(Qt::ISODate))) {
        const QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("allDay")).toBool() || e.value(QStringLiteral("declined")).toBool())
            continue;
        busy.append({e.value(QStringLiteral("start")).toInt(), e.value(QStringLiteral("end")).toInt()});
    }
    for (const auto &t : store.tasks()) {
        int start = 0, end = 0;
        if (blockOf(t, day, &start, &end))
            busy.append({start, end});
    }
    std::sort(busy.begin(), busy.end());
    return busy;
}

}  // namespace

namespace MyDay {

QDate lastPlanned(const Store &store) {
    return QDate::fromString(store.setting("myday.lastPlanned").toString(), Qt::ISODate);
}

void markPlanned(Store &store, const QDate &day) { store.setSetting("myday.lastPlanned", day.toString(Qt::ISODate)); }

Sections sections(const Store &store, const QDateTime &now) {
    Sections out;
    if (!enabled(store))
        return out;

    const QDate today = now.date();
    const ScheduleSettings settings = ScheduleSettings::fromStore(store);
    ViewBuilder views(store, now, settings);
    const QSet<QString> hidden = hiddenUids(store, today);
    QSet<qint64> placed;

    // Overdue: everything past its deadline, oldest first.
    QVector<Task> overdue;
    for (const auto &t : store.tasks()) {
        if (excluded(store, views, hidden, t))
            continue;
        if (!Schedule::isOverdue(t, now, settings))
            continue;
        overdue.append(t);
    }
    std::stable_sort(overdue.begin(), overdue.end(), [](const Task &a, const Task &b) {
        if (a.dueDate != b.dueDate)
            return a.dueDate < b.dueDate;
        return a.id < b.id;
    });
    for (const Task &t : overdue) {
        out.overdue.append({t.id, overdueReason(t, today)});
        placed.insert(t.id);
    }

    // Already today: the Today rules, minus anything overdue already sits in
    // the section above.
    QVector<Task> todayTasks;
    for (const auto &t : store.tasks()) {
        if (placed.contains(t.id) || excluded(store, views, hidden, t))
            continue;
        if (!views.matchesTodayList(t))
            continue;
        todayTasks.append(t);
    }
    std::stable_sort(todayTasks.begin(), todayTasks.end(), plannedOrder);
    for (const Task &t : todayTasks) {
        out.today.append(Entry{t.id, {}});
        placed.insert(t.id);
    }

    // Suggestions: every task once, first matching reason.
    QVector<QPair<Task, QString>> suggestions;
    for (const auto &t : store.tasks()) {
        if (placed.contains(t.id) || excluded(store, views, hidden, t))
            continue;
        const QString reason = suggestionReason(store, t, now);
        if (reason.isEmpty())
            continue;
        suggestions.append({t, reason});
    }
    std::stable_sort(suggestions.begin(), suggestions.end(),
                     [](const QPair<Task, QString> &a, const QPair<Task, QString> &b) { return plannedOrder(a.first, b.first); });
    for (const auto &s : suggestions) {
        out.suggestions.append({s.first.id, s.second});
        placed.insert(s.first.id);
    }

    return out;
}

Capacity capacity(const Store &store, const CalendarFeed &calendar, const QDateTime &now) {
    Capacity out;
    if (!enabled(store))
        return out;

    const QDate today = now.date();
    const Sections day = sections(store, now);
    for (const Entry &e : day.today) {
        const Task t = store.task(e.taskId);
        if (!t.id || t.type == TaskType::Project)
            continue;  // A project's own duration is not time spent; its sub-tasks count.
        if (t.duration > 0)
            out.plannedMinutes += t.duration;
        else
            ++out.withoutDuration;
    }

    const int nowMinute = now.time().hour() * 60 + now.time().minute();
    const int start = qMax(nowMinute, workStart(store));
    const int end = workEnd(store);
    out.windowMinutes = qMax(0, end - start);

    const QString iso = today.toString(Qt::ISODate);
    if (out.windowMinutes > 0) {
        out.freeMinutes = calendar.freeMinutes(iso, start, end);
        for (const auto &t : store.tasks()) {
            int blockStart = 0, blockEnd = 0;
            if (!blockOf(t, today, &blockStart, &blockEnd))
                continue;
            out.blockedMinutes += qMax(0, qMin(blockEnd, end) - qMax(blockStart, start));
        }
    }
    return out;
}

void addToToday(Store &store, const QList<qint64> &ids) {
    if (ids.isEmpty())
        return;
    const QDate today = QDate::currentDate();
    store.begin(QStringLiteral("Add to My Day"));
    for (qint64 id : ids) {
        if (!store.tasks().contains(id))
            continue;
        Task t = store.task(id);
        if (t.deleted)
            continue;
        // A past or missing deadline moves to today; a later one keeps its
        // deadline and the task starts today instead.
        if (!t.dueDate.isValid() || t.dueDate <= today)
            t.dueDate = today;
        else
            t.startDate = today;
        store.putTask(t);
    }
    store.commit();
}

void removeFromToday(Store &store, const QList<qint64> &ids) {
    if (ids.isEmpty())
        return;
    const QDate today = QDate::currentDate(), tomorrow = today.addDays(1);
    store.begin(QStringLiteral("Remove from My Day"));
    for (qint64 id : ids) {
        if (!store.tasks().contains(id))
            continue;
        Task t = store.task(id);
        if (t.deleted)
            continue;
        if (t.dueDate == today)
            t.dueDate = tomorrow;
        else if (t.startDate == today)
            t.startDate = tomorrow;
        store.putTask(t);
    }
    store.commit();
}

void hideForToday(Store &store, const QList<qint64> &ids) {
    if (ids.isEmpty())
        return;
    const QDate today = QDate::currentDate();
    QSet<QString> hidden = hiddenUids(store, today);
    for (qint64 id : ids) {
        const Task t = store.task(id);
        if (t.id && !t.deleted)
            hidden.insert(t.uid);
    }
    store.putSetting(QStringLiteral("myday.hidden"), hiddenSetting(today, hidden));
}

int nextFreeSlot(const Store &store, const CalendarFeed &calendar, const QDateTime &now, int duration) {
    if (!enabled(store) || duration <= 0)
        return -1;
    const QDate today = now.date();
    const int nowMinute = now.time().hour() * 60 + now.time().minute();
    const int windowStart = qMax(nowMinute, workStart(store));
    const int windowEnd = workEnd(store);
    if (windowEnd <= windowStart)
        return -1;

    const auto busy = busyIntervals(store, calendar, today);
    int cursor = windowStart;
    for (const auto &[start, end] : busy) {
        if (end <= cursor)
            continue;
        if (start >= windowEnd)
            break;
        if (qMin(start, windowEnd) - cursor >= duration)
            return cursor;
        cursor = qMax(cursor, end);
    }
    return windowEnd - cursor >= duration ? cursor : -1;
}

}  // namespace MyDay
