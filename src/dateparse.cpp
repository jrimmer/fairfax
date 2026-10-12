#include "dateparse.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

namespace {

struct Context {
    QDate today;
    int nowMinutes = 0;
};

// One reading of the date half of the input. `fixedTime` marks words such as
// "eod" and "now" that carry their own time, so no further time can be added.
struct Candidate {
    QDate date;
    int minutes = -1;
    bool fixedTime = false;
};

const char *const kWeekdayNames[] = {"monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"};
const char *const kMonthNames[] = {"january", "february", "march",     "april",   "may",      "june",
                                   "july",    "august",   "september", "october", "november", "december"};

struct TimeWord {
    const char *name;
    int minutes;
};
const TimeWord kTimeWords[] = {{"morning", 9 * 60}, {"afternoon", 14 * 60}, {"evening", 19 * 60},
                               {"night", 21 * 60},  {"noon", 12 * 60},      {"midnight", 0}};

constexpr int kMaxLookaheadMonths = 14;
constexpr int kMaxLookaheadYears = 8;

QString normalized(const QString &text) {
    QString copy = text.toLower();
    copy.replace(QLatin1Char(','), QLatin1Char(' '));
    return copy.simplified();
}

// Weekdays (1 = Monday) whose name starts with `word`. "weds" is the one common
// abbreviation that is not a prefix of the full name.
QList<int> weekdaysMatching(const QString &word) {
    QList<int> days;
    if (word.isEmpty())
        return days;
    for (int i = 0; i < 7; ++i) {
        if (QLatin1String(kWeekdayNames[i]).startsWith(word) || (i == 2 && word == QLatin1String("weds")))
            days << i + 1;
    }
    return days;
}

// Months need three letters so that "mon" or "sat" never read as a month; a
// lone "d" stands for December in the compact "18d" and "d25" forms.
int monthFor(const QString &word) {
    if (word == QLatin1String("d"))
        return 12;
    if (word.size() < 3)
        return 0;
    for (int i = 0; i < 12; ++i) {
        if (QLatin1String(kMonthNames[i]).startsWith(word))
            return i + 1;
    }
    return 0;
}

QDate nextWeekdayAfter(const QDate &today, int weekday) {
    int delta = (weekday - today.dayOfWeek() + 7) % 7;
    if (delta == 0)
        delta = 7;
    return today.addDays(delta);
}

// The first valid month/day on or after today, rolling into following years.
QDate upcomingMonthDay(int month, int day, const QDate &today) {
    for (int year = today.year(); year <= today.year() + kMaxLookaheadYears; ++year) {
        const QDate date(year, month, day);
        if (date.isValid() && date >= today)
            return date;
    }
    return {};
}

QDate isoWeekMonday(int year, int week) {
    const QDate jan4(year, 1, 4);
    const QDate monday = jan4.addDays(-(jan4.dayOfWeek() - 1)).addDays((week - 1) * 7);
    int weekYear = 0;
    return monday.weekNumber(&weekYear) == week && weekYear == year ? monday : QDate();
}

// Minutes for a clock reading. Without am/pm, hours 1–11 are ambiguous: on
// today's date the AM reading is used only while it is still ahead.
int resolveHour(int hour, int minute, int meridiem, bool exact, bool today, int nowMinutes) {
    if (minute < 0 || minute > 59)
        return -1;
    if (meridiem) {
        if (hour < 1 || hour > 12)
            return -1;
        return (hour % 12 + (meridiem == 'p' ? 12 : 0)) * 60 + minute;
    }
    if (hour < 0 || hour > 23)
        return -1;
    if (exact || hour == 0 || hour >= 12)
        return hour * 60 + minute;
    const int am = hour * 60 + minute;
    return today && am <= nowMinutes ? am + 12 * 60 : am;
}

int meridiemOf(const QString &letter) {
    return letter.isEmpty() ? 0 : letter.at(0).toLatin1();
}

QList<int> timeReadings(const QString &raw, bool today, int nowMinutes) {
    static const QRegularExpression clock(QStringLiteral("^(\\d{1,2}):(\\d{2})\\s*([ap])?m?$"));
    static const QRegularExpression compact(QStringLiteral("^(\\d{1,2})([ap])(\\d{1,2})$"));
    static const QRegularExpression suffixed(QStringLiteral("^(\\d{1,2})\\s*([ap])m?$"));
    static const QRegularExpression digits(QStringLiteral("^(\\d{3,4})\\s*(?:([ap])m?)?$"));
    static const QRegularExpression hourOnly(QStringLiteral("^(\\d{1,2})$"));

    const QString s = raw.trimmed();
    QList<int> out;
    if (s.isEmpty())
        return out;

    if (s.size() >= 3 && s.at(0).isLetter()) {
        for (const auto &word : kTimeWords) {
            if (QLatin1String(word.name).startsWith(s))
                out << word.minutes;
        }
        return out;
    }

    int minutes = -1;
    QRegularExpressionMatch m;
    if ((m = clock.match(s)).hasMatch()) {
        const QString hourText = m.captured(1);
        minutes = resolveHour(hourText.toInt(), m.captured(2).toInt(), meridiemOf(m.captured(3)),
                              hourText.size() == 2 && hourText.startsWith(QLatin1Char('0')), today, nowMinutes);
    } else if ((m = compact.match(s)).hasMatch()) {
        minutes = resolveHour(m.captured(1).toInt(), m.captured(3).toInt(), meridiemOf(m.captured(2)), false, today, nowMinutes);
    } else if ((m = suffixed.match(s)).hasMatch()) {
        minutes = resolveHour(m.captured(1).toInt(), 0, meridiemOf(m.captured(2)), false, today, nowMinutes);
    } else if ((m = digits.match(s)).hasMatch()) {
        const QString d = m.captured(1);
        const int value = d.toInt();
        minutes = resolveHour(value / 100, value % 100, meridiemOf(m.captured(2)), d.size() == 4 && d.startsWith(QLatin1Char('0')),
                              today, nowMinutes);
    } else if ((m = hourOnly.match(s)).hasMatch()) {
        const QString hourText = m.captured(1);
        minutes = resolveHour(hourText.toInt(), 0, 0, hourText.size() == 2 && hourText.startsWith(QLatin1Char('0')), today,
                              nowMinutes);
    }
    if (minutes >= 0)
        out << minutes;
    return out;
}

void addDay(QList<Candidate> &out, const QDate &date) {
    if (date.isValid())
        out.append({date, -1, false});
}

QList<Candidate> keywordReadings(const QString &s, const Context &ctx) {
    QList<Candidate> out;
    const QDate &today = ctx.today;
    const auto matches = [&](const char *word) { return QLatin1String(word).startsWith(s); };

    if (matches("today"))
        addDay(out, today);
    if (matches("tomorrow"))
        addDay(out, today.addDays(1));
    if (matches("day after tomorrow"))
        addDay(out, today.addDays(2));
    if (matches("yesterday"))
        addDay(out, today.addDays(-1));
    if (matches("weekend"))
        addDay(out, today.addDays((6 - today.dayOfWeek() + 7) % 7));
    if (matches("eod"))
        out.append({today, 17 * 60, true});
    if (matches("eow"))
        addDay(out, today.addDays((5 - today.dayOfWeek() + 7) % 7));
    if (matches("eom"))
        addDay(out, QDate(today.year(), today.month(), today.daysInMonth()));
    if (matches("now"))
        out.append({today, ctx.nowMinutes, true});

    if (!s.contains(QLatin1Char(' '))) {
        const QList<int> days = weekdaysMatching(s);
        for (int day : days) {
            // A single named day lists several upcoming occurrences; an ambiguous
            // prefix lists just the nearest of each.
            const QDate first = nextWeekdayAfter(today, day);
            const int count = days.size() == 1 ? 3 : 1;
            for (int i = 0; i < count; ++i)
                addDay(out, first.addDays(7 * i));
        }
    }
    return out;
}

QList<Candidate> thisNextReadings(const QString &s, const Context &ctx) {
    QList<Candidate> out;
    const QStringList tokens = s.split(QLatin1Char(' '));
    if (tokens.size() != 2)
        return out;
    const QString &lead = tokens.at(0);
    const QString &rest = tokens.at(1);
    const bool isNext = lead.size() >= 3 && QLatin1String("next").startsWith(lead);
    const bool isThis = lead.size() >= 3 && QLatin1String("this").startsWith(lead);
    if (!isNext && !isThis)
        return out;

    const QDate monday = ctx.today.addDays(-(ctx.today.dayOfWeek() - 1));
    const auto addPeriods = [&] {
        if (!isNext)
            return;
        if (QLatin1String("week").startsWith(rest))
            addDay(out, nextWeekdayAfter(ctx.today, 1));
        if (QLatin1String("month").startsWith(rest))
            addDay(out, QDate(ctx.today.year(), ctx.today.month(), 1).addMonths(1));
    };
    // "next mon" is a Monday, not "month", so a three-letter day name wins.
    if (rest.size() < 3)
        addPeriods();
    for (int day : weekdaysMatching(rest)) {
        if (isNext) {
            addDay(out, monday.addDays(7 + day - 1));
        } else {
            QDate date = monday.addDays(day - 1);
            if (date < ctx.today)
                date = date.addDays(7);
            addDay(out, date);
        }
    }
    if (rest.size() >= 3)
        addPeriods();
    return out;
}

QList<Candidate> inReadings(const QString &s, const Context &ctx) {
    static const QRegularExpression pattern(QStringLiteral("^in\\s+(\\d{1,4})\\s*([a-z]*)$"));
    QList<Candidate> out;
    const auto m = pattern.match(s);
    if (!m.hasMatch())
        return out;
    const int n = m.captured(1).toInt();
    const QString unit = m.captured(2);
    if (unit.isEmpty() || QLatin1String("days").startsWith(unit)) {
        addDay(out, ctx.today.addDays(n));
    } else if (QLatin1String("weeks").startsWith(unit)) {
        addDay(out, ctx.today.addDays(7 * n));
    } else if (QLatin1String("months").startsWith(unit)) {
        addDay(out, ctx.today.addMonths(n));
    } else if (QLatin1String("years").startsWith(unit)) {
        addDay(out, ctx.today.addYears(n));
    }
    return out;
}

QDate resolveMonthDay(int month, int day, int year, const QDate &today) {
    if (year > 0) {
        const QDate date(year, month, day);
        return date;
    }
    return upcomingMonthDay(month, day, today);
}

bool sensibleYear(int year) { return year >= 1900 && year <= 2199; }

QList<Candidate> monthDayReadings(const QString &s, const Context &ctx) {
    static const QRegularExpression wordFirst(
        QStringLiteral("^([a-z]+)\\s*(\\d{1,2})(?:st|nd|rd|th)?(?:\\s+(\\d{4}))?$"));
    static const QRegularExpression dayFirst(
        QStringLiteral("^(\\d{1,2})(?:st|nd|rd|th)?\\s*([a-z]+)(?:\\s+(\\d{4}))?$"));
    QList<Candidate> out;
    for (int form = 0; form < 2; ++form) {
        const auto m = (form == 0 ? wordFirst : dayFirst).match(s);
        if (!m.hasMatch())
            continue;
        const int month = monthFor(m.captured(form == 0 ? 1 : 2));
        const int day = m.captured(form == 0 ? 2 : 1).toInt();
        const int year = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();
        if (!month || (year && !sensibleYear(year)))
            continue;
        addDay(out, resolveMonthDay(month, day, year, ctx.today));
        if (!out.isEmpty())
            return out;
    }
    return out;
}

QList<Candidate> weekdayInMonthReadings(const QString &s, const Context &ctx) {
    QList<Candidate> out;
    const QStringList tokens = s.split(QLatin1Char(' '));
    if (tokens.size() < 2 || tokens.size() > 3)
        return out;
    int year = 0;
    if (tokens.size() == 3) {
        if (tokens.at(2).size() != 4 || !sensibleYear(tokens.at(2).toInt()))
            return out;
        year = tokens.at(2).toInt();
    }
    for (int order = 0; order < 2; ++order) {
        const QString &monthWord = tokens.at(order);
        const QString &dayWord = tokens.at(1 - order);
        const int month = monthFor(monthWord);
        const QList<int> days = weekdaysMatching(dayWord);
        if (monthWord == QLatin1String("d") || !month || dayWord.size() < 3 || days.size() != 1)
            continue;
        int y = year ? year : ctx.today.year();
        if (!year && QDate(y, month, 1).addMonths(1).addDays(-1) < ctx.today)
            ++y;
        const QDate first(y, month, 1);
        for (int day = 1; day <= first.daysInMonth(); ++day) {
            const QDate date(y, month, day);
            if (date.dayOfWeek() == days.first() && date >= ctx.today)
                out.append({date, -1, false});
        }
        return out;
    }
    return out;
}

QList<Candidate> weekNumberReadings(const QString &s, const Context &ctx) {
    static const QRegularExpression pattern(QStringLiteral("^(?:w|wk|kw|week)\\s*(\\d{1,2})$"));
    QList<Candidate> out;
    const auto m = pattern.match(s);
    if (!m.hasMatch())
        return out;
    const int week = m.captured(1).toInt();
    for (int year = ctx.today.year(); year <= ctx.today.year() + 2; ++year) {
        const QDate monday = isoWeekMonday(year, week);
        if (monday.isValid() && monday >= ctx.today) {
            addDay(out, monday);
            break;
        }
    }
    return out;
}

QList<Candidate> exactDateReadings(const QString &s, const Context &ctx) {
    static const QRegularExpression iso(QStringLiteral("^(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})$"));
    static const QRegularExpression us(QStringLiteral("^(\\d{1,2})/(\\d{1,2})(?:/(\\d{4}))?$"));
    QList<Candidate> out;
    if (const auto m = iso.match(s); m.hasMatch()) {
        addDay(out, QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()));
    } else if (const auto m2 = us.match(s); m2.hasMatch()) {
        int month = m2.captured(1).toInt();
        int day = m2.captured(2).toInt();
        if (month > 12 && day <= 12)
            std::swap(month, day);
        const int year = m2.captured(3).isEmpty() ? 0 : m2.captured(3).toInt();
        if (month >= 1 && month <= 12)
            addDay(out, resolveMonthDay(month, day, year, ctx.today));
    }
    return out;
}

QList<Candidate> dateReadings(const QString &s, const Context &ctx) {
    QList<Candidate> out;
    if (s.isEmpty())
        return out;
    const auto append = [&](const QList<Candidate> &more) { out += more; };
    append(keywordReadings(s, ctx));
    append(thisNextReadings(s, ctx));
    append(inReadings(s, ctx));
    append(monthDayReadings(s, ctx));
    append(weekdayInMonthReadings(s, ctx));
    append(weekNumberReadings(s, ctx));
    append(exactDateReadings(s, ctx));
    return out;
}

QString timeLabel(int minutes) {
    const int hour = minutes / 60 % 24;
    return QStringLiteral("%1:%2 %3")
        .arg(hour % 12 == 0 ? 12 : hour % 12)
        .arg(minutes % 60, 2, 10, QLatin1Char('0'))
        .arg(hour < 12 ? QStringLiteral("AM") : QStringLiteral("PM"));
}

QString dateLabel(const QDate &date, const QDate &today) {
    if (date == today)
        return QStringLiteral("Today");
    if (date == today.addDays(1))
        return QStringLiteral("Tomorrow");
    if (date == today.addDays(-1))
        return QStringLiteral("Yesterday");
    const QString format = date.year() == today.year() ? QStringLiteral("ddd, d MMM") : QStringLiteral("ddd, d MMM yyyy");
    return QLocale::c().toString(date, format);
}

DateSuggestion makeSuggestion(const QDate &date, int minutes, const QDate &today) {
    DateSuggestion s;
    s.date = date;
    s.minutes = minutes;
    if (date.isValid()) {
        s.label = dateLabel(date, today);
        if (minutes >= 0)
            s.label += QStringLiteral(", ") + timeLabel(minutes);
    } else if (minutes >= 0) {
        s.label = timeLabel(minutes);
    }
    return s;
}

}  // namespace

namespace DateParse {

QList<DateSuggestion> suggest(const QString &text, const QDateTime &now, int limit) {
    QList<DateSuggestion> out;
    const QString s = normalized(text);
    if (s.isEmpty() || limit <= 0)
        return out;

    Context ctx;
    ctx.today = now.date();
    ctx.nowMinutes = now.time().hour() * 60 + now.time().minute();

    struct Reading {
        QDate date;
        int minutes;
    };
    QList<Reading> readings;

    static const QRegularExpression loneNumber(QStringLiteral("^\\d{1,2}$"));
    if (loneNumber.match(s).hasMatch()) {
        // A bare number is a time, a day of the month, or a count of days.
        for (int minutes : timeReadings(s, true, ctx.nowMinutes))
            readings.append({QDate(), minutes});
        const int n = s.toInt();
        if (n >= 1 && n <= 31) {
            const QDate firstOfMonth(ctx.today.year(), ctx.today.month(), 1);
            for (int offset = 0; offset < kMaxLookaheadMonths; ++offset) {
                const QDate month = firstOfMonth.addMonths(offset);
                const QDate date(month.year(), month.month(), n);
                if (date.isValid() && date >= ctx.today) {
                    readings.append({date, -1});
                    break;
                }
            }
        }
        readings.append({ctx.today.addDays(s.toInt()), -1});
    } else {
        for (const auto &c : dateReadings(s, ctx))
            readings.append({c.date, c.minutes});
        for (int minutes : timeReadings(s, true, ctx.nowMinutes))
            readings.append({QDate(), minutes});

        const QStringList tokens = s.split(QLatin1Char(' '));
        for (int split = 1; split < tokens.size(); ++split) {
            const QString left = tokens.mid(0, split).join(QLatin1Char(' '));
            const QString right = tokens.mid(split).join(QLatin1Char(' '));
            for (const auto &c : dateReadings(left, ctx)) {
                if (c.fixedTime)
                    continue;
                for (int minutes : timeReadings(right, c.date == ctx.today, ctx.nowMinutes))
                    readings.append({c.date, minutes});
            }
            for (const auto &c : dateReadings(right, ctx)) {
                if (c.fixedTime)
                    continue;
                for (int minutes : timeReadings(left, c.date == ctx.today, ctx.nowMinutes))
                    readings.append({c.date, minutes});
            }
        }
    }

    for (const auto &r : readings) {
        const DateSuggestion suggestion = makeSuggestion(r.date, r.minutes, ctx.today);
        bool seen = false;
        for (const auto &existing : out)
            seen = seen || (existing.date == suggestion.date && existing.minutes == suggestion.minutes);
        if (seen)
            continue;
        out.append(suggestion);
        if (out.size() >= limit)
            break;
    }
    return out;
}

DateSuggestion best(const QString &text, const QDateTime &now) {
    const auto all = suggest(text, now, 1);
    return all.isEmpty() ? DateSuggestion() : all.first();
}

int parseTime(const QString &text, const QDateTime &now) {
    const QList<int> readings =
        timeReadings(normalized(text), true, now.time().hour() * 60 + now.time().minute());
    return readings.isEmpty() ? -1 : readings.first();
}

}  // namespace DateParse
