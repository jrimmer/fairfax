#include "nlparse.h"

#include <QRegularExpression>
#include <algorithm>
#include <vector>

namespace {

struct Tok {
    int start = 0;
    int coreEnd = 0;  // End of the word proper, before any trailing , . ; :
    int end = 0;
    QString core;
    QString lower;
    bool literal = false;  // Inside "quotes": never parsed.
    bool used = false;
    bool comma = false;  // The word was followed by a comma.
};

const char *const kMonthNames[] = {"january", "february", "march",     "april",   "may",      "june",
                                   "july",    "august",   "september", "october", "november", "december"};

QDate nextWeekdayAfter(const QDate &base, int weekday) {
    int delta = (weekday - base.dayOfWeek() + 7) % 7;
    if (delta == 0)
        delta = 7;
    return base.addDays(delta);
}

QDate mondayOfNextWeek(const QDate &today) {
    return today.addDays(8 - today.dayOfWeek());
}

QDate upcomingMonthDay(int month, int day, const QDate &base) {
    for (int year = base.year(); year <= base.year() + 8; ++year) {
        const QDate date(year, month, day);
        if (date.isValid() && date >= base)
            return date;
    }
    return {};
}

QDate upcomingDayOfMonth(int day, const QDate &base) {
    const QDate first(base.year(), base.month(), 1);
    for (int k = 0; k < 24; ++k) {
        const QDate firstOfMonth = first.addMonths(k);
        const QDate date(firstOfMonth.year(), firstOfMonth.month(), day);
        if (date.isValid() && date >= base)
            return date;
    }
    return {};
}

int weekdayFull(const QString &w) {
    static const char *const names[] = {"monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"};
    for (int i = 0; i < 7; ++i) {
        if (w == QLatin1String(names[i]))
            return i + 1;
    }
    return 0;
}

// Abbreviations are safe only where an "every" already anchors the phrase.
int weekdayLoose(const QString &w) {
    if (const int full = weekdayFull(w))
        return full;
    static const struct {
        const char *name;
        int day;
    } abbreviations[] = {{"mon", 1}, {"tue", 2}, {"tues", 2}, {"wed", 3}, {"weds", 3}, {"thu", 4},
                         {"thur", 4}, {"thurs", 4}, {"fri", 5}, {"sat", 6}, {"sun", 7}};
    for (const auto &a : abbreviations) {
        if (w == QLatin1String(a.name))
            return a.day;
    }
    return 0;
}

int ordinalWord(const QString &w) {
    static const char *const names[] = {"first", "second", "third", "fourth", "fifth"};
    for (int i = 0; i < 5; ++i) {
        if (w == QLatin1String(names[i]))
            return i + 1;
    }
    if (w == QLatin1String("last"))
        return -1;
    static const QRegularExpression numeric(QStringLiteral("^([1-5])(?:st|nd|rd|th)$"));
    const auto m = numeric.match(w);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

// "30m", "1h30m", "2h", "1d", "1.5h" → minutes, or -1.
int durationMinutes(const QString &text) {
    static const QRegularExpression re(
        QStringLiteral("^(?:(\\d+(?:\\.\\d+)?)(?:d|days?))?(?:(\\d+(?:\\.\\d+)?)(?:h|hrs?|hours?))?"
                       "(?:(\\d+(?:\\.\\d+)?)(?:m|mins?|minutes?))?$"));
    const auto m = re.match(text);
    if (!m.hasMatch() || text.isEmpty())
        return -1;
    const double total = m.captured(1).toDouble() * 1440 + m.captured(2).toDouble() * 60 + m.captured(3).toDouble();
    return static_cast<int>(total + 0.5);
}

QString squashed(const QString &s) {
    QString out;
    for (const QChar c : s) {
        if (!c.isSpace())
            out += c.toLower();
    }
    return out;
}

struct DateHit {
    bool ok = false;
    QDate d;
    QDate d2;  // The end of "September 5-7".
    int time = -1;  // "tonight".
    int first = 0, last = 0, next = 0;
};

struct TimeHit {
    bool ok = false;
    int minutes = -1;
    bool bare = false;  // "at 9": the AM reading, pending a choice of AM or PM.
    int first = 0, last = 0, next = 0;
};

struct Moment {
    bool ok = false;
    int end = 0;
    QDate date, date2;
    int time = -1;
    int dateFirst = -1, dateLast = -1;
    int timeFirst = -1, timeLast = -1;
};

struct Clause {
    bool ok = false;
    int end = 0;
    bool hasStart = false, hasDue = false;
    Moment start, due;
};

class Parser {
public:
    Parser(const QString &text, const QDateTime &now, const QStringList &lists, const QStringList &locations)
        : m_text(text), m_today(now.date()), m_nowMinutes(now.time().hour() * 60 + now.time().minute()),
          m_lists(lists), m_locations(locations) {
        tokenize();
    }

    ParsedEntry run() {
        for (int i = 0; i < m_tokens.size(); ++i) {
            if (!free(i))
                continue;
            if (tryMarker(i) || tryTag(i) || tryList(i) || tryLocation(i) || tryDuration(i) || tryAlert(i) ||
                tryRepeat(i) || tryClause(i))
                continue;
        }
        finalizeDates();
        buildTitle();
        std::sort(m_entry.spans.begin(), m_entry.spans.end(),
                  [](const ParsedEntry::Span &a, const ParsedEntry::Span &b) { return a.start < b.start; });
        return m_entry;
    }

private:
    // ---- Tokens -----------------------------------------------------------

    void addToken(int start, int end, bool literal) {
        Tok t;
        t.start = start;
        t.end = end;
        int coreEnd = end;
        while (coreEnd > start + 1 && QStringLiteral(",.;:").contains(m_text.at(coreEnd - 1)))
            --coreEnd;
        t.coreEnd = coreEnd;
        t.comma = coreEnd < end && m_text.at(end - 1) == QLatin1Char(',');
        t.core = m_text.mid(start, coreEnd - start);
        t.lower = t.core.toLower();
        t.lower.replace(QChar(0x2013), QLatin1Char('-'));
        t.literal = literal;
        m_tokens.append(t);
    }

    void tokenize() {
        const int n = m_text.size();
        m_quoteChar.assign(n, false);
        m_removed.assign(n, false);
        QList<QPair<int, int>> quotes;
        int open = -1;
        for (int i = 0; i < n; ++i) {
            if (m_text.at(i) != QLatin1Char('"'))
                continue;
            if (open < 0) {
                open = i;
            } else {
                quotes.append({open, i});
                m_quoteChar[open] = m_quoteChar[i] = true;
                open = -1;
            }
        }
        static const QRegularExpression dashed(QStringLiteral("^(\\d[\\w:]*)([-\\x{2013}])(\\d[\\w:]*)$"));
        int i = 0;
        while (i < n) {
            if (m_text.at(i).isSpace()) {
                ++i;
                continue;
            }
            int j = i;
            while (j < n && !m_text.at(j).isSpace())
                ++j;
            bool literal = false;
            for (const auto &q : quotes) {
                if (i <= q.second && j > q.first)
                    literal = true;
            }
            const auto m = literal ? QRegularExpressionMatch() : dashed.match(m_text.mid(i, j - i));
            if (m.hasMatch()) {
                const int l = m.captured(1).size();
                addToken(i, i + l, false);
                addToken(i + l, i + l + 1, false);
                addToken(i + l + 1, j, false);
            } else {
                addToken(i, j, literal);
            }
            i = j;
        }
    }

    bool free(int i) const { return i >= 0 && i < m_tokens.size() && !m_tokens[i].literal && !m_tokens[i].used; }
    QString w(int i) const { return free(i) ? m_tokens[i].lower : QString(); }

    void consume(int from, int to) {  // [from, to)
        for (int i = from; i < to; ++i) {
            m_tokens[i].used = true;
            for (int c = m_tokens[i].start; c < m_tokens[i].end; ++c)
                m_removed[c] = true;
        }
    }

    void span(int first, int last, const QString &kind) {
        if (first < 0 || last < first)
            return;
        const int start = m_tokens[first].start;
        m_entry.spans.append({start, m_tokens[last].coreEnd - start, kind});
    }

    bool isInt(const QString &s) const {
        if (s.isEmpty() || s.size() > 4)
            return false;
        return std::all_of(s.begin(), s.end(), [](QChar c) { return c.isDigit() && c.unicode() < 128; });
    }

    // ---- Simple markers ---------------------------------------------------

    bool tryMarker(int i) {
        const QString &c = m_tokens[i].core;
        if (c == QLatin1String("!") || c == QLatin1String("!!") || c == QLatin1String("!!!")) {
            if (m_entry.priority < 0)
                m_entry.priority = c.size();
            consume(i, i + 1);
            span(i, i, QStringLiteral("priority"));
            return true;
        }
        if (c == QLatin1String("*")) {
            m_entry.starred = true;
            consume(i, i + 1);
            span(i, i, QStringLiteral("star"));
            return true;
        }
        return false;
    }

    bool tryTag(int i) {
        static const QRegularExpression re(QStringLiteral("^#([\\p{L}\\p{N}_-]+)$"));
        const auto m = re.match(m_tokens[i].core);
        if (!m.hasMatch())
            return false;
        if (!m_entry.tags.contains(m.captured(1), Qt::CaseInsensitive))
            m_entry.tags << m.captured(1);
        consume(i, i + 1);
        span(i, i, QStringLiteral("tag"));
        return true;
    }

    bool tryList(int i) {
        if (m_lists.isEmpty() || !m_tokens[i].core.startsWith(QLatin1Char('/')) || m_tokens[i].core.size() < 2)
            return false;
        int bestEnd = -1;
        QString bestName;
        QString text;
        for (int j = i; j < m_tokens.size() && j < i + 6; ++j) {
            if (!free(j))
                break;
            QString piece = j == i ? m_tokens[j].core.mid(1) : m_tokens[j].core;
            const bool closed = piece.endsWith(QLatin1Char('/'));
            if (closed)
                piece.chop(1);
            if (piece.contains(QLatin1Char('/')))
                break;
            text += piece;
            const QString key = squashed(text);
            if (key.isEmpty())
                break;
            QString found;
            for (const QString &name : m_lists) {
                if (squashed(name) == key) {
                    found = name;
                    break;
                }
            }
            if (found.isEmpty()) {
                for (const QString &name : m_lists) {
                    if (squashed(name).startsWith(key)) {
                        found = name;
                        break;
                    }
                }
            }
            if (!found.isEmpty()) {
                bestEnd = j + 1;
                bestName = found;
            }
            if (closed)
                break;
        }
        if (bestEnd < 0)
            return false;
        m_entry.list = bestName;
        consume(i, bestEnd);
        span(i, bestEnd - 1, QStringLiteral("list"));
        return true;
    }

    bool tryLocation(int i) {
        if (m_locations.isEmpty() || !m_tokens[i].core.startsWith(QLatin1Char('@')))
            return false;
        int bestEnd = -1;
        QString bestName;
        QString text;
        for (int j = i; j < m_tokens.size() && j < i + 8; ++j) {
            if (!free(j))
                break;
            text += j == i ? m_tokens[j].core.mid(1) : m_tokens[j].core;
            const QString key = squashed(text);
            if (key.isEmpty())
                continue;
            for (const QString &name : m_locations) {
                if (squashed(name) == key) {
                    bestEnd = j + 1;
                    bestName = name;
                    break;
                }
            }
        }
        if (bestEnd < 0)
            return false;
        m_entry.location = bestName;
        consume(i, bestEnd);
        span(i, bestEnd - 1, QStringLiteral("location"));
        return true;
    }

    bool tryDuration(int i) {
        if (!m_tokens[i].core.startsWith(QLatin1Char('[')))
            return false;
        QString text;
        for (int j = i; j < m_tokens.size() && j < i + 4; ++j) {
            if (!free(j))
                return false;
            text += m_tokens[j].core;
            if (!m_tokens[j].core.endsWith(QLatin1Char(']')))
                continue;
            text = text.mid(1, text.size() - 2);
            text.remove(QLatin1Char(' '));
            const int minutes = durationMinutes(text.toLower());
            if (minutes <= 0)
                return false;
            if (m_entry.duration == 0)
                m_entry.duration = minutes;
            consume(i, j + 1);
            span(i, j, QStringLiteral("duration"));
            return true;
        }
        return false;
    }

    // "30m", "1h30m", or "1 hour" starting at token k; returns the token after it.
    int offsetAt(int k, int &minutes) const {
        const QString a = w(k);
        if (a.isEmpty() || !a.at(0).isDigit())
            return -1;
        int m = durationMinutes(a);
        if (m > 0) {
            minutes = m;
            return k + 1;
        }
        const QString b = w(k + 1);
        if (!b.isEmpty() && (m = durationMinutes(a + b)) > 0) {
            minutes = m;
            return k + 2;
        }
        return -1;
    }

    bool tryAlert(int i) {
        const QString a = w(i);
        int j;
        if (a == QLatin1String("alert") || a == QLatin1String("alarm"))
            j = i + 1;
        else if (a == QLatin1String("remind") && w(i + 1) == QLatin1String("me"))
            j = i + 2;
        else
            return false;
        QList<int> offsets;
        int minutes = 0;
        int next = offsetAt(j, minutes);
        if (next < 0)
            return false;
        offsets << minutes;
        for (;;) {
            const bool comma = m_tokens[next - 1].comma;
            int k = next;
            if (!comma && w(k) == QLatin1String("and"))
                ++k;
            else if (!comma)
                break;
            const int after = offsetAt(k, minutes);
            if (after < 0)
                break;
            offsets << minutes;
            next = after;
        }
        int last = next - 1;
        if (w(next) == QLatin1String("before"))
            last = next++;
        for (int o : offsets) {
            if (!m_entry.alertOffsets.contains(o))
                m_entry.alertOffsets << o;
        }
        consume(i, last + 1);
        span(i, last, QStringLiteral("alert"));
        return true;
    }

    // ---- Repeats ----------------------------------------------------------

    bool tryRepeat(int i) {
        if (m_entry.recurrence.isSet())
            return false;
        Recurrence r;
        int end = 0;
        if (!parseRepeat(i, r, end))
            return false;
        m_entry.recurrence = r;
        consume(i, end);
        span(i, end - 1, QStringLiteral("repeat"));
        return true;
    }

    bool parseRepeat(int i, Recurrence &r, int &end) const {
        const QString a = w(i);
        if (a == QLatin1String("daily") || a == QLatin1String("weekly") || a == QLatin1String("monthly") ||
            a == QLatin1String("yearly") || a == QLatin1String("annually")) {
            r.frequency = a == QLatin1String("daily")    ? Repeat::Daily
                          : a == QLatin1String("weekly") ? Repeat::Weekly
                          : a == QLatin1String("monthly") ? Repeat::Monthly
                                                          : Repeat::Yearly;
            end = i + 1;
            return true;
        }
        if (a != QLatin1String("every"))
            return false;
        int j = i + 1;
        QString b = w(j);
        int interval = 1;
        bool explicitInterval = false;
        if (b == QLatin1String("other")) {
            interval = 2;
            explicitInterval = true;
            b = w(++j);
        } else if (isInt(b)) {
            interval = b.toInt();
            if (interval < 1)
                return false;
            explicitInterval = true;
            b = w(++j);
        }
        if (b.isEmpty())
            return false;
        r.interval = interval;
        if (b == QLatin1String("day") || b == QLatin1String("days")) {
            r.frequency = Repeat::Daily;
        } else if (b == QLatin1String("weekday") || b == QLatin1String("weekdays")) {
            if (explicitInterval)
                return false;
            r.frequency = Repeat::Weekdays;
        } else if (b == QLatin1String("weekend") && !explicitInterval) {
            r.frequency = Repeat::Weekly;
            r.weekdays = (1 << 5) | (1 << 6);
        } else if (b == QLatin1String("week") || b == QLatin1String("weeks")) {
            r.frequency = Repeat::Weekly;
        } else if (b == QLatin1String("year") || b == QLatin1String("years")) {
            r.frequency = Repeat::Yearly;
        } else if (b == QLatin1String("month") || b == QLatin1String("months")) {
            r.frequency = Repeat::Monthly;
            // "on the third Friday"
            if (w(j + 1) == QLatin1String("on") && w(j + 2) == QLatin1String("the")) {
                const int ordinal = ordinalWord(w(j + 3));
                const int day = weekdayLoose(w(j + 4));
                if (ordinal != 0 && day != 0) {
                    r.monthOrdinal = ordinal;
                    r.monthWeekday = day;
                    j += 4;
                }
            }
        } else if (weekdayLoose(b)) {
            r.frequency = Repeat::Weekly;
            if (explicitInterval && interval != 2)
                return false;
            int mask = 0;
            for (;;) {
                mask |= 1 << (weekdayLoose(w(j)) - 1);
                if (m_tokens[j].comma && weekdayLoose(w(j + 1)))
                    j += 1;
                else if ((w(j + 1) == QLatin1String("and") || w(j + 1) == QLatin1String("&")) && weekdayLoose(w(j + 2)))
                    j += 2;
                else
                    break;
            }
            r.weekdays = mask;
        } else if (ordinalWord(b) != 0 && !explicitInterval && weekdayLoose(w(j + 1))) {
            r.frequency = Repeat::Monthly;
            r.monthOrdinal = ordinalWord(b);
            r.monthWeekday = weekdayLoose(w(j + 1));
            ++j;
            if (w(j + 1) == QLatin1String("of") && w(j + 2) == QLatin1String("the") && w(j + 3) == QLatin1String("month"))
                j += 3;
        } else {
            return false;
        }
        end = j + 1;
        return true;
    }

    // ---- Dates ------------------------------------------------------------

    int monthAt(int i) const {
        const QString a = w(i);
        if (a.size() < 3)
            return 0;
        for (int m = 0; m < 12; ++m) {
            const QLatin1String full(kMonthNames[m]);
            if (a == full || a == full.left(3) || (m == 8 && a == QLatin1String("sept"))) {
                if (m == 4 && !m_tokens[i].core.at(0).isUpper())
                    return 0;  // "may" the verb.
                return m + 1;
            }
        }
        return 0;
    }

    // A day of the month at token k, with or without an ordinal suffix.
    int dayAt(int k, bool allowSuffix, bool requireSuffix) const {
        static const QRegularExpression re(QStringLiteral("^(\\d{1,2})(st|nd|rd|th)?$"));
        const auto m = re.match(w(k));
        if (!m.hasMatch())
            return 0;
        const bool suffixed = !m.captured(2).isEmpty();
        if ((suffixed && !allowSuffix) || (!suffixed && requireSuffix))
            return 0;
        const int day = m.captured(1).toInt();
        return day >= 1 && day <= 31 ? day : 0;
    }

    int yearAt(int k) const {
        const QString a = w(k);
        if (a.size() != 4 || !isInt(a))
            return 0;
        const int y = a.toInt();
        return y >= 1900 && y <= 2199 ? y : 0;
    }

    DateHit parseDate(int i, const QDate &base, bool afterOn) const {
        DateHit h;
        const QString a = w(i);
        if (a.isEmpty())
            return h;
        auto done = [&](const QDate &d, int last) {
            h.ok = d.isValid();
            h.d = d;
            h.first = i;
            h.last = last;
            h.next = last + 1;
            return h;
        };

        if (a == QLatin1String("today"))
            return done(m_today, i);
        if (a == QLatin1String("tonight")) {
            done(m_today, i);
            h.time = 21 * 60;
            return h;
        }
        if (a == QLatin1String("tomorrow"))
            return done(m_today.addDays(1), i);
        if (a == QLatin1String("day") && w(i + 1) == QLatin1String("after") && w(i + 2) == QLatin1String("tomorrow"))
            return done(m_today.addDays(2), i + 2);
        if (a == QLatin1String("the") && w(i + 1) == QLatin1String("day") && w(i + 2) == QLatin1String("after") &&
            w(i + 3) == QLatin1String("tomorrow"))
            return done(m_today.addDays(2), i + 3);
        if (a == QLatin1String("next")) {
            const QString b = w(i + 1);
            if (b == QLatin1String("week"))
                return done(mondayOfNextWeek(m_today), i + 1);
            if (b == QLatin1String("month"))
                return done(QDate(m_today.year(), m_today.month(), 1).addMonths(1), i + 1);
            if (const int wd = weekdayFull(b))
                return done(mondayOfNextWeek(m_today).addDays(wd - 1), i + 1);
            return h;
        }
        if (a == QLatin1String("this")) {
            if (const int wd = weekdayFull(w(i + 1)))
                return done(nextWeekdayAfter(base, wd), i + 1);
            return h;
        }
        if (const int wd = weekdayFull(a))
            return done(nextWeekdayAfter(base, wd), i);
        if (a == QLatin1String("in")) {
            const QString n = w(i + 1);
            const QString unit = w(i + 2);
            if (!isInt(n) || n.toInt() < 1)
                return h;
            const int count = n.toInt();
            if (unit == QLatin1String("day") || unit == QLatin1String("days"))
                return done(m_today.addDays(count), i + 2);
            if (unit == QLatin1String("week") || unit == QLatin1String("weeks"))
                return done(m_today.addDays(count * 7), i + 2);
            if (unit == QLatin1String("month") || unit == QLatin1String("months"))
                return done(m_today.addMonths(count), i + 2);
            return h;
        }

        // m/d and m/d/y.
        static const QRegularExpression slashed(QStringLiteral("^(\\d{1,2})/(\\d{1,2})(?:/(\\d{4}|\\d{2}))?$"));
        if (const auto m = slashed.match(a); m.hasMatch()) {
            const int month = m.captured(1).toInt();
            const int day = m.captured(2).toInt();
            if (!m.captured(3).isEmpty()) {
                int year = m.captured(3).toInt();
                if (year < 100)
                    year += 2000;
                return done(QDate(year, month, day), i);
            }
            return done(upcomingMonthDay(month, day, base), i);
        }

        // Month name first: "September 5th", "Sep 5-7 2026".
        if (const int month = monthAt(i)) {
            const int day = dayAt(i + 1, true, false);
            if (!day)
                return h;
            int last = i + 1;
            int day2 = 0;
            if (w(last + 1) == QLatin1String("-") && monthAt(last + 3) == 0) {
                day2 = dayAt(last + 2, true, false);
                if (day2)
                    last += 2;
            }
            return monthDay(h, i, last, month, day, day2, yearAt(last + 1), base);
        }

        // Day number first: "5 September", "5th of September", "5-7 September".
        if (const int day = dayAt(i, true, false)) {
            int k = i + 1;
            int day2 = 0;
            if (w(k) == QLatin1String("-") && dayAt(k + 1, true, false)) {
                day2 = dayAt(k + 1, true, false);
                k += 2;
            }
            if (w(k) == QLatin1String("of") && monthAt(k + 1))
                ++k;
            if (const int month = monthAt(k))
                return monthDay(h, i, k, month, day, day2, yearAt(k + 1), base);
        }

        // "the 5th", "on the 5", "on 5th".
        int ordinalAt = -1;
        int day = 0;
        if (a == QLatin1String("the")) {
            day = dayAt(i + 1, true, !afterOn);
            ordinalAt = i + 1;
        } else if (afterOn) {
            day = dayAt(i, true, true);
            ordinalAt = i;
        }
        if (day) {
            if (w(ordinalAt + 1) == QLatin1String("of") && monthAt(ordinalAt + 2))
                return monthDay(h, i, ordinalAt + 2, monthAt(ordinalAt + 2), day, 0, yearAt(ordinalAt + 3), base);
            h.first = i;
            return done(upcomingDayOfMonth(day, base), ordinalAt);
        }
        return h;
    }

    DateHit monthDay(DateHit &h, int first, int last, int month, int day, int day2, int year, const QDate &base) const {
        if (year)
            ++last;
        const auto resolve = [&](int d) { return year ? QDate(year, month, d) : upcomingMonthDay(month, d, base); };
        h.d = resolve(day);
        if (day2) {
            h.d2 = resolve(day2);
            if (h.d2.isValid() && h.d.isValid() && h.d2 < h.d)
                h.d2 = h.d2.addYears(1);
        }
        h.ok = h.d.isValid() && (!day2 || h.d2.isValid());
        h.first = first;
        h.last = last;
        h.next = last + 1;
        return h;
    }

    // ---- Times ------------------------------------------------------------

    static int to24(int hour, int minute, bool pm) { return (hour % 12 + (pm ? 12 : 0)) * 60 + minute; }

    TimeHit parseTime(int i, bool afterDate) const {
        TimeHit h;
        int j = i;
        bool lead = false, atLead = false, dayLead = false;
        const QString a = w(j);
        if (a == QLatin1String("at") || a == QLatin1String("by")) {
            lead = true;
            atLead = a == QLatin1String("at");
            ++j;
        } else if (a == QLatin1String("this")) {
            dayLead = true;
            ++j;
        } else if (a == QLatin1String("in") && w(j + 1) == QLatin1String("the")) {
            dayLead = true;
            j += 2;
        }
        const QString b = w(j);
        if (b.isEmpty())
            return h;
        auto finish = [&](int minutes, int last, bool bare = false) {
            h.ok = true;
            h.minutes = minutes;
            h.bare = bare;
            h.first = j;
            h.last = last;
            h.next = last + 1;
            return h;
        };

        static const struct {
            const char *name;
            int minutes;
        } dayParts[] = {{"morning", 9 * 60}, {"afternoon", 14 * 60}, {"evening", 19 * 60}, {"night", 21 * 60}};
        for (const auto &p : dayParts) {
            if (b == QLatin1String(p.name) && (lead || dayLead || afterDate))
                return finish(p.minutes, j);
        }
        if (dayLead)
            return h;
        if ((b == QLatin1String("noon") || b == QLatin1String("midnight")) && (lead || afterDate))
            return finish(b == QLatin1String("noon") ? 12 * 60 : 0, j);

        static const QRegularExpression clock(QStringLiteral("^(\\d{1,2}):(\\d{2})(am|pm)?$"));
        static const QRegularExpression meridiem(QStringLiteral("^(\\d{1,2})(am|pm)$"));
        if (const auto m = clock.match(b); m.hasMatch()) {
            const int hour = m.captured(1).toInt();
            const int minute = m.captured(2).toInt();
            if (minute > 59)
                return h;
            QString suffix = m.captured(3);
            int last = j;
            if (suffix.isEmpty() && (w(j + 1) == QLatin1String("am") || w(j + 1) == QLatin1String("pm"))) {
                suffix = w(j + 1);
                last = j + 1;
            }
            if (suffix.isEmpty())
                return hour < 24 ? finish(hour * 60 + minute, j) : h;
            if (hour < 1 || hour > 12)
                return h;
            return finish(to24(hour, minute, suffix == QLatin1String("pm")), last);
        }
        if (const auto m = meridiem.match(b); m.hasMatch()) {
            const int hour = m.captured(1).toInt();
            if (hour < 1 || hour > 12)
                return h;
            return finish(to24(hour, 0, m.captured(2) == QLatin1String("pm")), j);
        }
        static const QRegularExpression number(QStringLiteral("^\\d{1,2}$"));
        if (number.match(b).hasMatch()) {
            const int hour = b.toInt();
            const QString next = w(j + 1);
            if (next == QLatin1String("am") || next == QLatin1String("pm")) {
                if (hour < 1 || hour > 12)
                    return h;
                if (lead || afterDate)
                    return finish(to24(hour, 0, next == QLatin1String("pm")), j + 1);
                return h;
            }
            if (atLead && hour < 24)
                return finish(hour * 60, j, hour >= 1 && hour <= 11);
        }
        return h;
    }

    Moment parseMoment(int i, const QDate &base) const {
        Moment m;
        DateHit dh = parseDate(i, base, false);
        if (!dh.ok && (w(i) == QLatin1String("on") || w(i) == QLatin1String("by")))
            dh = parseDate(i + 1, base, w(i) == QLatin1String("on"));
        bool bare = false;
        if (dh.ok) {
            m.date = dh.d;
            m.date2 = dh.d2;
            m.dateFirst = dh.first;
            m.dateLast = dh.last;
            m.end = dh.next;
            if (dh.time >= 0) {
                m.time = dh.time;
            } else if (!dh.d2.isValid()) {
                const TimeHit th = parseTime(m.end, true);
                if (th.ok) {
                    m.time = th.minutes;
                    bare = th.bare;
                    m.timeFirst = th.first;
                    m.timeLast = th.last;
                    m.end = th.next;
                }
            }
        } else {
            const TimeHit th = parseTime(i, false);
            if (!th.ok)
                return m;
            m.time = th.minutes;
            bare = th.bare;
            m.timeFirst = th.first;
            m.timeLast = th.last;
            m.end = th.next;
            DateHit d3 = parseDate(m.end, base, false);
            if (!d3.ok && w(m.end) == QLatin1String("on"))
                d3 = parseDate(m.end + 1, base, true);
            if (d3.ok && !d3.d2.isValid() && d3.time < 0) {
                m.date = d3.d;
                m.dateFirst = d3.first;
                m.dateLast = d3.last;
                m.end = d3.next;
            }
        }
        if (bare && !(m.date.isValid() && m.date != m_today) && m.time <= m_nowMinutes)
            m.time += 12 * 60;
        m.ok = true;
        return m;
    }

    bool isRangeJoiner(int k) const {
        const QString a = w(k);
        return a == QLatin1String("to") || a == QLatin1String("until") || a == QLatin1String("till") ||
               a == QLatin1String("through") || a == QLatin1String("-");
    }

    bool isStartWord(const QString &a) const {
        return a == QLatin1String("starting") || a == QLatin1String("starts") || a == QLatin1String("start") ||
               a == QLatin1String("begins") || a == QLatin1String("beginning") || a == QLatin1String("from");
    }

    bool isEndWord(const QString &a) const {
        return a == QLatin1String("ending") || a == QLatin1String("ends") || a == QLatin1String("finishes") ||
               a == QLatin1String("finishing") || a == QLatin1String("due");
    }

    Clause parseClause(int i) const {
        Clause c;
        int k = i;
        bool startWord = false;
        const QString a = w(k);
        if (isStartWord(a)) {
            startWord = true;
            ++k;
        } else if (a == QLatin1String("due")) {
            ++k;
        } else if (a == QLatin1String("is") && w(k + 1) == QLatin1String("due")) {
            k += 2;
        }
        const Moment m1 = parseMoment(k, m_today);
        if (!m1.ok)
            return c;
        c.ok = true;
        c.end = m1.end;
        const QDate base2 = m1.date.isValid() ? m1.date : m_today;

        if (m1.date2.isValid()) {
            c.hasStart = c.hasDue = true;
            c.start = m1;
            c.start.date2 = QDate();
            c.due = m1;
            c.due.date = m1.date2;
            c.due.dateFirst = c.due.dateLast = -1;
            return c;
        }
        if (isRangeJoiner(m1.end) && a != QLatin1String("due")) {
            const Moment m2 = parseMoment(m1.end + 1, base2);
            if (m2.ok && !m2.date2.isValid()) {
                c.hasStart = c.hasDue = true;
                c.start = m1;
                c.due = m2;
                c.end = m2.end;
                return c;
            }
        }
        if (startWord) {
            c.hasStart = true;
            c.start = m1;
            int e = m1.end;
            if (w(e) == QLatin1String("and"))
                ++e;
            if (w(e) == QLatin1String("is") && w(e + 1) == QLatin1String("due"))
                ++e;
            if (isEndWord(w(e))) {
                const Moment m3 = parseMoment(e + 1, base2);
                if (m3.ok && !m3.date2.isValid()) {
                    c.hasDue = true;
                    c.due = m3;
                    c.end = m3.end;
                }
            }
            return c;
        }
        c.hasDue = true;
        c.due = m1;
        return c;
    }

    bool tryClause(int i) {
        Clause c = parseClause(i);
        if (!c.ok)
            return false;
        if (c.hasStart && c.hasDue) {
            if (!c.start.date.isValid() && c.due.date.isValid())
                c.start.date = c.due.date;
            else if (!c.due.date.isValid() && c.start.date.isValid())
                c.due.date = c.start.date;
        }
        bool applied = false;
        if (c.hasStart)
            applied |= apply(c.start, m_entry.startDate, m_entry.startTime, QStringLiteral("start"));
        if (c.hasDue)
            applied |= apply(c.due, m_entry.dueDate, m_entry.dueTime, QStringLiteral("date"));
        if (!applied)
            return false;
        consume(i, c.end);
        return true;
    }

    bool apply(const Moment &m, QDate &date, int &time, const QString &kind) {
        bool set = false;
        if (m.date.isValid() && !date.isValid()) {
            date = m.date;
            span(m.dateFirst, m.dateLast, kind);
            set = true;
        }
        if (m.time >= 0 && time < 0) {
            time = m.time;
            span(m.timeFirst, m.timeLast, QStringLiteral("time"));
            set = true;
        }
        return set;
    }

    // A time with no date means its next occurrence.
    void finalizeDates() {
        const bool startNeeds = m_entry.startTime >= 0 && !m_entry.startDate.isValid();
        const bool dueNeeds = m_entry.dueTime >= 0 && !m_entry.dueDate.isValid();
        if (!startNeeds && !dueNeeds)
            return;
        QDate ref;
        if (m_entry.startDate.isValid())
            ref = m_entry.startDate;
        else if (m_entry.dueDate.isValid())
            ref = m_entry.dueDate;
        else {
            int earliest = startNeeds ? m_entry.startTime : m_entry.dueTime;
            if (startNeeds && dueNeeds)
                earliest = std::min(m_entry.startTime, m_entry.dueTime);
            ref = earliest > m_nowMinutes ? m_today : m_today.addDays(1);
        }
        if (startNeeds)
            m_entry.startDate = ref;
        if (dueNeeds)
            m_entry.dueDate = m_entry.dueDate.isValid() ? m_entry.dueDate : ref;
    }

    void buildTitle() {
        QString title;
        for (int i = 0; i < m_text.size(); ++i) {
            if (!m_removed[i] && !m_quoteChar[i])
                title += m_text.at(i);
        }
        m_entry.title = title.simplified();
    }

    QString m_text;
    QDate m_today;
    int m_nowMinutes;
    QStringList m_lists, m_locations;
    QList<Tok> m_tokens;
    std::vector<bool> m_quoteChar, m_removed;
    ParsedEntry m_entry;
};

}  // namespace

namespace NaturalLanguage {

ParsedEntry parse(const QString &text, const QDateTime &now, const QStringList &listNames,
                  const QStringList &locationNames) {
    return Parser(text, now, listNames, locationNames).run();
}

}  // namespace NaturalLanguage
