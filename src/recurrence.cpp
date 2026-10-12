#include "recurrence.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

namespace {

const char *const kDayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

// How many months ahead to look for a month that has a 5th weekday before giving up.
constexpr int kMaxMonthSearch = 48;

QDate nthWeekdayOfMonth(int year, int month, int ordinal, int weekday) {
    const QDate first(year, month, 1);
    if (!first.isValid())
        return {};
    if (ordinal < 0) {
        const QDate last(year, month, first.daysInMonth());
        return last.addDays(-((last.dayOfWeek() - weekday + 7) % 7));
    }
    const int day = 1 + (weekday - first.dayOfWeek() + 7) % 7 + 7 * (ordinal - 1);
    return day <= first.daysInMonth() ? QDate(year, month, day) : QDate();
}

QDate nextWeekly(const Recurrence &rule, const QDate &anchor, int interval) {
    if (!(rule.weekdays & 0x7f))
        return anchor.addDays(7 * interval);
    const int dow = anchor.dayOfWeek();
    for (int wd = dow + 1; wd <= 7; ++wd) {
        if (rule.weekdays & (1 << (wd - 1)))
            return anchor.addDays(wd - dow);
    }
    // The anchor's week is used up, so jump whole intervals from that week's Monday.
    const QDate monday = anchor.addDays(-(dow - 1)).addDays(7 * interval);
    for (int wd = 1; wd <= 7; ++wd) {
        if (rule.weekdays & (1 << (wd - 1)))
            return monday.addDays(wd - 1);
    }
    return {};
}

QDate nextMonthly(const Recurrence &rule, const QDate &anchor, int interval) {
    if (!rule.monthOrdinal)
        return anchor.addMonths(interval);
    const int weekday = rule.monthWeekday >= 1 && rule.monthWeekday <= 7 ? rule.monthWeekday : anchor.dayOfWeek();
    const QDate firstOfMonth(anchor.year(), anchor.month(), 1);
    // A month without a 5th weekday is skipped rather than clamped.
    for (int step = 1; step <= kMaxMonthSearch; ++step) {
        const QDate month = firstOfMonth.addMonths(interval * step);
        const QDate date = nthWeekdayOfMonth(month.year(), month.month(), rule.monthOrdinal, weekday);
        if (date.isValid())
            return date;
    }
    return {};
}

QString ordinalName(int ordinal) {
    switch (ordinal) {
    case -1: return QStringLiteral("last");
    case 1: return QStringLiteral("1st");
    case 2: return QStringLiteral("2nd");
    case 3: return QStringLiteral("3rd");
    default: return QStringLiteral("%1th").arg(ordinal);
    }
}

QString unitSummary(int interval, const QString &everyUnit, const QString &single) {
    return interval == 1 ? single : QStringLiteral("Every %1 %2").arg(interval).arg(everyUnit);
}

}  // namespace

namespace Recurrences {

QDate next(const Recurrence &rule, const QDate &anchor) {
    if (!anchor.isValid())
        return {};
    const int interval = qMax(1, rule.interval);
    switch (rule.frequency) {
    case Repeat::Daily:
        return anchor.addDays(interval);
    case Repeat::Weekdays: {
        QDate date = anchor.addDays(1);
        while (date.dayOfWeek() > 5)
            date = date.addDays(1);
        return date;
    }
    case Repeat::Weekly:
        return nextWeekly(rule, anchor, interval);
    case Repeat::Monthly:
        return nextMonthly(rule, anchor, interval);
    case Repeat::Yearly:
        return anchor.addYears(interval);
    default:
        return {};
    }
}

std::optional<Task> advance(const Task &task, const QDate &completedOn) {
    const Recurrence &rule = task.recurrence;
    QDate anchor;
    if (rule.from == Repeat::FromCompletion)
        anchor = completedOn;
    else if (task.dueDate.isValid())
        anchor = task.dueDate;
    else if (task.startDate.isValid())
        anchor = task.startDate;
    else
        anchor = completedOn;

    const QDate date = next(rule, anchor);
    if (!date.isValid())
        return std::nullopt;

    Task result = task;
    if (task.dueDate.isValid()) {
        result.dueDate = date;
        if (task.startDate.isValid())
            result.startDate = date.addDays(-task.startDate.daysTo(task.dueDate));
    } else if (task.startDate.isValid()) {
        result.startDate = date;
    }
    result.recurrence.occurrencesDone = rule.occurrencesDone + 1;
    result.completed = false;
    result.completedAt = QDateTime();

    if (rule.endType == Repeat::AfterCount && result.recurrence.occurrencesDone >= rule.endCount)
        return std::nullopt;
    if (rule.endType == Repeat::OnDate && rule.endDate.isValid()) {
        const QDate governing = result.dueDate.isValid() ? result.dueDate : result.startDate.isValid() ? result.startDate : date;
        if (governing > rule.endDate)
            return std::nullopt;
    }
    return result;
}

QString carryOverNotes(const QString &notes) {
    // The stamp formats Insert Time Stamp offers: ISO dates, "12 Oct 2026",
    // "Mon, 12 Oct", and clock times, alone or together on a line, optionally
    // in brackets.
    static const QRegularExpression stamp(QStringLiteral(
        "^\\s*[\\[(]?\\s*"
        "(?:(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun)[a-z]*,?\\s+)?"
        "(?:\\d{4}-\\d{2}-\\d{2}|\\d{1,2}[/.]\\d{1,2}[/.]\\d{2,4}|\\d{1,2}\\s+(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]*\\.?(?:\\s+\\d{4})?|(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]*\\.?\\s+\\d{1,2}(?:,\\s*\\d{4})?)?"
        "[,T\\s]*(?:\\d{1,2}:\\d{2}(?::\\d{2})?\\s*(?:[AaPp][Mm])?)?"
        "\\s*[\\])]?\\s*$"));
    QStringList kept;
    bool changed = false;
    for (const QString &line : notes.split(QLatin1Char('\n'))) {
        // A blank line is not a stamp, and must not be dropped by the pattern.
        if (!line.trimmed().isEmpty() && stamp.match(line).hasMatch()) {
            changed = true;
            continue;
        }
        kept << line;
    }
    return changed ? kept.join(QLatin1Char('\n')).trimmed() : notes;
}

QString describe(const Recurrence &rule) {
    const int interval = qMax(1, rule.interval);
    QString text;
    switch (rule.frequency) {
    case Repeat::Daily:
        text = unitSummary(interval, QStringLiteral("days"), QStringLiteral("Daily"));
        break;
    case Repeat::Weekdays:
        text = QStringLiteral("Weekdays");
        break;
    case Repeat::Weekly: {
        text = unitSummary(interval, QStringLiteral("weeks"), QStringLiteral("Weekly"));
        QStringList names;
        for (int i = 0; i < 7; ++i) {
            if (rule.weekdays & (1 << i))
                names << QString::fromLatin1(kDayNames[i]);
        }
        if (!names.isEmpty())
            text += QStringLiteral(" on ") + names.join(QStringLiteral(", "));
        break;
    }
    case Repeat::Monthly:
        text = unitSummary(interval, QStringLiteral("months"), QStringLiteral("Monthly"));
        if (rule.monthOrdinal && rule.monthWeekday >= 1 && rule.monthWeekday <= 7) {
            text += QStringLiteral(" on the %1 %2").arg(ordinalName(rule.monthOrdinal), QString::fromLatin1(kDayNames[rule.monthWeekday - 1]));
        }
        break;
    case Repeat::Yearly:
        text = unitSummary(interval, QStringLiteral("years"), QStringLiteral("Yearly"));
        break;
    default:
        return {};
    }

    if (rule.from == Repeat::FromCompletion)
        text += QStringLiteral(" · after completion");
    if (rule.endType == Repeat::AfterCount)
        text += QStringLiteral(" · %1 times").arg(rule.endCount);
    else if (rule.endType == Repeat::OnDate && rule.endDate.isValid())
        text += QStringLiteral(" · until ") + QLocale::c().toString(rule.endDate, QStringLiteral("d MMM yyyy"));
    return text;
}

}  // namespace Recurrences
