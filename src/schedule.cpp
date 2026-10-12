#include "schedule.h"

#include <QLocale>

namespace {

const QLocale English(QLocale::English, QLocale::UnitedKingdom);

QDateTime at(const QDate &date, int minutes) { return QDateTime(date, QTime(0, 0).addSecs(minutes * 60)); }

QString span(qint64 seconds) {
    const qint64 minutes = seconds / 60;
    const qint64 hours = minutes / 60;
    const qint64 days = hours / 24;
    if (days >= 2)
        return QStringLiteral("%1d").arg(days);
    if (hours >= 1)
        return minutes % 60 ? QStringLiteral("%1h %2m").arg(hours).arg(minutes % 60) : QStringLiteral("%1h").arg(hours);
    return QStringLiteral("%1m").arg(qMax<qint64>(1, minutes));
}

}  // namespace

namespace Schedule {

QDateTime dueMoment(const Task &t) {
    if (!t.dueDate.isValid())
        return {};
    return t.dueTime >= 0 ? at(t.dueDate, t.dueTime) : QDateTime(t.dueDate, QTime(23, 59, 59));
}

QDateTime effectiveStart(const Task &t) {
    if (!t.dueDate.isValid() || t.duration <= 0)
        return {};
    if (t.dueTime >= 0)
        return at(t.dueDate, t.dueTime).addSecs(-qint64(t.duration) * 60);
    // Without a due time only whole days count back; a shorter duration
    // doesn't bring the task forward.
    const int days = t.duration / (24 * 60);
    if (days == 0)
        return {};
    return QDateTime(t.dueDate.addDays(-days), QTime(0, 0));
}

QDateTime dueSortKey(const Task &t) {
    if (!t.dueDate.isValid())
        return {};
    if (t.duration > 0) {
        // Even a sub-day duration moves a date-only task a little earlier
        // among that day's tasks.
        return dueMoment(t).addSecs(-qint64(t.duration) * 60);
    }
    return dueMoment(t);
}

bool isOverdue(const Task &t, const QDateTime &now, const ScheduleSettings &s) {
    if (t.completed || !t.dueDate.isValid())
        return false;
    const QDate today = now.date();
    if (t.dueDate < today)
        return true;
    return s.overdueUsesDueTime && t.dueDate == today && t.dueTime >= 0 && at(t.dueDate, t.dueTime) < now;
}

bool isScheduled(const Task &t, const QDateTime &now) {
    if (!t.startDate.isValid())
        return false;
    if (t.startDate > now.date())
        return true;
    return t.startDate == now.date() && t.startTime >= 0 && at(t.startDate, t.startTime) > now;
}

bool isDueToday(const Task &t, const QDateTime &now) { return t.dueDate.isValid() && t.dueDate == now.date(); }

bool isInProgress(const Task &t, const QDateTime &now) {
    const QDateTime start = effectiveStart(t);
    return start.isValid() && start <= now && now <= dueMoment(t) && !t.completed;
}

bool inToday(const Task &t, const QDateTime &now, const ScheduleSettings &s) {
    if (t.completed)
        return false;
    if (s.todayIncludesStarred && t.starred)
        return true;
    // The start date wins while scheduled tasks are hidden, even over a
    // duration window that reaches back past it.
    if (isScheduled(t, now))
        return false;
    const QDate today = now.date();
    if (isDueToday(t, now))
        return true;
    if (isOverdue(t, now, s))
        return s.todayIncludesOverdue;
    const QDateTime windowStart = effectiveStart(t);
    if (windowStart.isValid() && windowStart.date() <= today && t.dueDate >= today) {
        // With overdue items included the window keeps the task in Today every
        // day up to the deadline; otherwise only on its first day.
        if (s.todayIncludesOverdue || windowStart.date() == today)
            return true;
    }
    if (t.startDate.isValid()) {
        if (t.startDate == today)
            return true;
        if (t.startDate < today && s.todayIncludesStillWorking)
            return true;
    }
    return false;
}

QString friendlyDate(const QDate &date, const QDate &today, bool friendly) {
    if (!date.isValid())
        return {};
    const qint64 days = today.daysTo(date);
    if (friendly) {
        if (days == 0) return QStringLiteral("Today");
        if (days == 1) return QStringLiteral("Tomorrow");
        if (days == -1) return QStringLiteral("Yesterday");
        if (days > 1 && days < 7) return English.dayName(date.dayOfWeek(), QLocale::LongFormat);
        if (days >= 7) return QStringLiteral("In %1 days").arg(days);
        return QStringLiteral("%1 days ago").arg(-days);
    }
    return English.toString(date, date.year() == today.year() ? "ddd, d MMM" : "d MMM yyyy");
}

QString formatTime(int minutes) {
    if (minutes < 0)
        return {};
    return English.toString(QTime(0, 0).addSecs(minutes * 60), "h:mm AP");
}

QString friendlyDateTime(const QDate &date, int minutes, const QDate &today, bool friendly) {
    const QString day = friendlyDate(date, today, friendly);
    return minutes >= 0 ? day + ", " + formatTime(minutes) : day;
}

QString durationBadge(int minutes) {
    if (minutes <= 0)
        return {};
    if (minutes % (24 * 60) == 0)
        return QStringLiteral("%1 d").arg(minutes / (24 * 60));
    if (minutes < 60)
        return QStringLiteral("%1 m").arg(minutes);
    const int h = minutes / 60, m = minutes % 60;
    return m ? QStringLiteral("%1 h %2 m").arg(h).arg(m) : QStringLiteral("%1 h").arg(h);
}

QString durationTotal(int minutes) {
    if (minutes <= 0)
        return {};
    const int h = minutes / 60, m = minutes % 60;
    if (!h) return QStringLiteral("%1m").arg(m);
    return m ? QStringLiteral("%1h %2m").arg(h).arg(m) : QStringLiteral("%1h").arg(h);
}

QString dueHint(const Task &t, const QDateTime &now, const ScheduleSettings &s) {
    if (t.completed && t.completedAt.isValid())
        return QStringLiteral("Completed ") + friendlyDate(t.completedAt.date(), now.date(), s.friendlyDates).toLower();
    if (isScheduled(t, now)) {
        const QDateTime start = t.startTime >= 0 ? at(t.startDate, t.startTime) : QDateTime(t.startDate, QTime(0, 0));
        return QStringLiteral("Starts in ") + span(now.secsTo(start));
    }
    const QDateTime start = effectiveStart(t);
    if (start.isValid() && now < start)
        return QStringLiteral("Starts in ") + span(now.secsTo(start));
    if (!t.dueDate.isValid())
        return {};
    if (isOverdue(t, now, s)) {
        const qint64 days = t.dueDate.daysTo(now.date());
        return days > 0 ? QStringLiteral("Overdue by %1 day%2").arg(days).arg(days == 1 ? "" : "s")
                        : QStringLiteral("Overdue");
    }
    if (start.isValid())
        return span(now.secsTo(dueMoment(t))) + QStringLiteral(" remaining");
    return QStringLiteral("Due in ") + span(qMax<qint64>(60, now.secsTo(dueMoment(t))));
}

}  // namespace Schedule
