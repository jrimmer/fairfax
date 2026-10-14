#include "calendar.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtQml/qqml.h>
#include <algorithm>

namespace Ics {

namespace {

struct Property {
    QString name, value;
    QMap<QString, QString> params;
};

// RFC 5545 folds long lines by inserting CRLF plus one space or tab.
QStringList unfold(const QByteArray &ics) {
    QString text = QString::fromUtf8(ics);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QStringLiteral("\n "), QString());
    text.replace(QStringLiteral("\n\t"), QString());
    return text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString unescape(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar c = s[++i];
            out += (c == QLatin1Char('n') || c == QLatin1Char('N')) ? QLatin1Char('\n') : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Splits "NAME;P=V;P2=\"a:b\":value". Colons and semicolons inside quoted
// parameter values (TZID names occasionally contain them) must not split.
bool parseLine(const QString &line, Property &p) {
    bool quoted = false;
    int colon = -1;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == QLatin1Char('"')) quoted = !quoted;
        else if (line[i] == QLatin1Char(':') && !quoted) { colon = i; break; }
    }
    if (colon < 0) return false;
    p.value = line.mid(colon + 1);
    const QString head = line.left(colon);
    QStringList parts;
    QString current;
    quoted = false;
    for (const QChar c : head) {
        if (c == QLatin1Char('"')) quoted = !quoted;
        if (c == QLatin1Char(';') && !quoted) { parts << current; current.clear(); }
        else current += c;
    }
    parts << current;
    p.name = parts.takeFirst().toUpper();
    p.params.clear();
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        QString v = part.mid(eq + 1);
        if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) v = v.mid(1, v.size() - 2);
        p.params.insert(part.left(eq).toUpper(), v);
    }
    return true;
}

// Calendars in the wild use IANA ids, Outlook's Windows names, and Mozilla's
// "/mozilla.org/20070129_1/Europe/Berlin" prefix; anything unrecognised falls
// back to local time, which is what a floating time means anyway.
QTimeZone zoneFor(const QString &tzid) {
    QTimeZone zone(tzid.toUtf8());
    if (zone.isValid()) return zone;
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(tzid.toUtf8());
    if (!iana.isEmpty()) {
        zone = QTimeZone(iana);
        if (zone.isValid()) return zone;
    }
    const QStringList segments = tzid.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int n = 3; n >= 2; --n) {
        if (segments.size() < n) continue;
        zone = QTimeZone(segments.last(n).join(QLatin1Char('/')).toUtf8());
        if (zone.isValid()) return zone;
    }
    return QTimeZone::systemTimeZone();
}

struct Stamp {
    QDate date;
    QTime time;
    QTimeZone zone;
    bool dateOnly = false;
    bool valid = false;

    QDateTime instant() const {
        return dateOnly ? QDateTime(date, QTime(0, 0)) : QDateTime(date, time, zone);
    }
};

Stamp parseStamp(const QString &value, const QMap<QString, QString> &params) {
    Stamp s;
    const QString v = value.trimmed();
    s.date = QDate::fromString(v.left(8), QStringLiteral("yyyyMMdd"));
    if (!s.date.isValid()) return s;
    if (params.value(QStringLiteral("VALUE")).toUpper() == QLatin1String("DATE") || v.size() == 8) {
        s.dateOnly = true;
        s.valid = true;
        return s;
    }
    s.time = QTime::fromString(v.mid(9, 6), QStringLiteral("HHmmss"));
    if (!s.time.isValid()) return s;
    if (v.endsWith(QLatin1Char('Z'))) s.zone = QTimeZone::UTC;
    else if (params.contains(QStringLiteral("TZID"))) s.zone = zoneFor(params.value(QStringLiteral("TZID")));
    else s.zone = QTimeZone::systemTimeZone();
    s.valid = true;
    return s;
}

// PnW, PnDTnHnMnS and the signed forms; returns seconds.
qint64 parseDuration(const QString &value) {
    static const QRegularExpression re(QStringLiteral("^([+-])?P(?:(\\d+)W)?(?:(\\d+)D)?(?:T(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+)S)?)?$"));
    const auto m = re.match(value.trimmed().toUpper());
    if (!m.hasMatch()) return 0;
    qint64 secs = m.captured(2).toLongLong() * 7 * 86400 + m.captured(3).toLongLong() * 86400 + m.captured(4).toLongLong() * 3600
                  + m.captured(5).toLongLong() * 60 + m.captured(6).toLongLong();
    return m.captured(1) == QLatin1String("-") ? -secs : secs;
}

int weekdayFromCode(const QString &code) {
    static const QStringList codes = {QStringLiteral("MO"), QStringLiteral("TU"), QStringLiteral("WE"), QStringLiteral("TH"),
                                      QStringLiteral("FR"), QStringLiteral("SA"), QStringLiteral("SU")};
    const int i = codes.indexOf(code.toUpper());
    return i < 0 ? 0 : i + 1;
}

QList<int> intList(const QString &csv) {
    QList<int> out;
    for (const QString &part : csv.split(QLatin1Char(','), Qt::SkipEmptyParts)) out << part.toInt();
    return out;
}

Rule parseRule(const QString &value, const QTimeZone &zone) {
    Rule rule;
    for (const QString &pair : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        const QString key = pair.left(eq).toUpper();
        const QString v = pair.mid(eq + 1);
        if (key == QLatin1String("FREQ")) {
            const QString f = v.toUpper();
            rule.frequency = f == QLatin1String("DAILY")     ? Rule::Daily
                             : f == QLatin1String("WEEKLY")  ? Rule::Weekly
                             : f == QLatin1String("MONTHLY") ? Rule::Monthly
                             : f == QLatin1String("YEARLY")  ? Rule::Yearly
                                                             : Rule::None;
        } else if (key == QLatin1String("INTERVAL")) {
            rule.interval = std::max(1, v.toInt());
        } else if (key == QLatin1String("COUNT")) {
            rule.count = std::max(0, v.toInt());
        } else if (key == QLatin1String("UNTIL")) {
            if (v.size() == 8) rule.until = QDateTime(QDate::fromString(v, QStringLiteral("yyyyMMdd")), QTime(23, 59, 59), zone);
            else {
                Stamp s = parseStamp(v, {});
                if (s.valid) rule.until = QDateTime(s.date, s.time, v.endsWith(QLatin1Char('Z')) ? QTimeZone(QTimeZone::UTC) : zone);
            }
        } else if (key == QLatin1String("WKST")) {
            if (const int d = weekdayFromCode(v)) rule.weekStart = d;
        } else if (key == QLatin1String("BYDAY")) {
            static const QRegularExpression re(QStringLiteral("^([+-]?\\d+)?([A-Za-z]{2})$"));
            for (const QString &token : v.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const auto m = re.match(token.trimmed());
                if (!m.hasMatch()) continue;
                if (const int d = weekdayFromCode(m.captured(2))) rule.byDay.append({m.captured(1).toInt(), d});
            }
        } else if (key == QLatin1String("BYMONTHDAY")) {
            rule.byMonthDay = intList(v);
        } else if (key == QLatin1String("BYMONTH")) {
            rule.byMonth = intList(v);
        }
    }
    return rule;
}

// Dates within one month matching BYMONTHDAY and BYDAY (with ordinals counted
// from either end of the month). With neither, the start's day of month wins.
QList<QDate> monthDates(int year, int month, const Rule &rule, QDate start) {
    QList<QDate> out;
    const int dim = QDate(year, month, 1).daysInMonth();
    for (int day = 1; day <= dim; ++day) {
        const QDate d(year, month, day);
        if (rule.byMonthDay.isEmpty() && rule.byDay.isEmpty()) {
            if (day != start.day()) continue;
        }
        if (!rule.byMonthDay.isEmpty()) {
            bool ok = false;
            for (int md : rule.byMonthDay) ok |= (md > 0 ? md == day : dim + 1 + md == day);
            if (!ok) continue;
        }
        if (!rule.byDay.isEmpty()) {
            bool ok = false;
            for (const auto &[ordinal, weekday] : rule.byDay) {
                if (d.dayOfWeek() != weekday) continue;
                if (ordinal == 0 || (ordinal > 0 && (day - 1) / 7 + 1 == ordinal) || (ordinal < 0 && (dim - day) / 7 + 1 == -ordinal)) ok = true;
            }
            if (!ok) continue;
        }
        out << d;
    }
    return out;
}

bool monthAllowed(const Rule &rule, int month) { return rule.byMonth.isEmpty() || rule.byMonth.contains(month); }

// The anchor is a date no later than any date the period can produce; the
// driver uses it to know when to stop without enumerating forever.
QDate periodAnchor(const Rule &rule, QDate start, int k) {
    const qint64 step = qint64(k) * rule.interval;
    switch (rule.frequency) {
    case Rule::Daily: return start.addDays(step);
    case Rule::Weekly: return start.addDays(-((start.dayOfWeek() - rule.weekStart + 7) % 7) + step * 7);
    case Rule::Monthly: return QDate(start.year(), start.month(), 1).addMonths(int(step));
    default: return QDate(start.year() + int(step), 1, 1);
    }
}

QList<QDate> periodDates(const Rule &rule, QDate start, int k) {
    QList<QDate> out;
    const QDate anchor = periodAnchor(rule, start, k);
    switch (rule.frequency) {
    case Rule::Daily: {
        bool ok = monthAllowed(rule, anchor.month());
        if (ok && !rule.byDay.isEmpty()) {
            ok = false;
            for (const auto &entry : rule.byDay) ok |= entry.second == anchor.dayOfWeek();
        }
        if (ok && !rule.byMonthDay.isEmpty()) {
            ok = false;
            for (int md : rule.byMonthDay) ok |= (md > 0 ? md == anchor.day() : anchor.daysInMonth() + 1 + md == anchor.day());
        }
        if (ok) out << anchor;
        break;
    }
    case Rule::Weekly: {
        QList<int> days;
        for (const auto &entry : rule.byDay) days << entry.second;
        if (days.isEmpty()) days << start.dayOfWeek();
        for (int i = 0; i < 7; ++i) {
            const QDate d = anchor.addDays(i);
            if (days.contains(d.dayOfWeek()) && monthAllowed(rule, d.month())) out << d;
        }
        break;
    }
    case Rule::Monthly:
        if (monthAllowed(rule, anchor.month())) out = monthDates(anchor.year(), anchor.month(), rule, start);
        break;
    default: {
        QList<int> months = rule.byMonth;
        if (months.isEmpty()) months << start.month();
        std::sort(months.begin(), months.end());
        for (int m : months)
            if (m >= 1 && m <= 12) out += monthDates(anchor.year(), m, rule, start);
        break;
    }
    }
    return out;
}

QDateTime occurrenceStart(const Component &c, QDate date) {
    if (c.event.allDay) return QDateTime(date, QTime(0, 0));
    return QDateTime(date, c.startTime, c.zone).toLocalTime();
}

Event makeEvent(const Component &c, QDate date) {
    Event e = c.event;
    e.start = occurrenceStart(c, date);
    e.end = c.event.allDay ? QDateTime(date.addDays(c.durationDays), QTime(0, 0)) : e.start.addSecs(c.durationSecs);
    return e;
}

bool overlaps(const Event &e, const QDateTime &from, const QDateTime &to) {
    return e.start < to && (e.end > from || e.start >= from);
}

}  // namespace

QString calendarName(const QByteArray &ics) {
    for (const QString &line : unfold(ics)) {
        const QString upper = line.trimmed().toUpper();
        if (upper == QLatin1String("BEGIN:VEVENT")) break;  // Calendar-level properties come first.
        Property p;
        if (upper.startsWith(QLatin1String("X-WR-CALNAME")) && parseLine(line, p) && p.name == QLatin1String("X-WR-CALNAME"))
            return unescape(p.value).trimmed();
    }
    return QString();
}

QList<Component> parseComponents(const QByteArray &ics, const QString &calendarName) {
    QList<Component> out;
    QList<Property> props;
    bool inEvent = false;
    int alarmDepth = 0;

    auto finish = [&] {
        Component c;
        c.event.calendar = calendarName;
        Property dtstart, dtend, duration, rrule, recurrenceId;
        bool haveEnd = false, haveDuration = false;
        QList<Property> exdates;
        QString status;
        for (const Property &p : props) {
            if (p.name == QLatin1String("UID")) c.event.uid = p.value.trimmed();
            else if (p.name == QLatin1String("SUMMARY")) c.event.title = unescape(p.value);
            else if (p.name == QLatin1String("LOCATION")) c.event.location = unescape(p.value);
            else if (p.name == QLatin1String("DESCRIPTION")) c.event.description = unescape(p.value).trimmed();
            else if (p.name == QLatin1String("URL")) c.event.url = p.value.trimmed();
            else if (p.name == QLatin1String("ATTENDEE")) {
                if (p.params.value(QStringLiteral("PARTSTAT")).toUpper() == QLatin1String("DECLINED")) {
                    QString who = p.value.trimmed();
                    if (who.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) who = who.mid(7);
                    c.event.declinedBy << who.toLower();
                }
            }
            else if (p.name == QLatin1String("STATUS")) status = p.value.trimmed().toUpper();
            else if (p.name == QLatin1String("DTSTART")) dtstart = p;
            else if (p.name == QLatin1String("DTEND")) { dtend = p; haveEnd = true; }
            else if (p.name == QLatin1String("DURATION")) { duration = p; haveDuration = true; }
            else if (p.name == QLatin1String("RRULE")) rrule = p;
            else if (p.name == QLatin1String("EXDATE")) exdates << p;
            else if (p.name == QLatin1String("RECURRENCE-ID")) { recurrenceId = p; c.hasRecurrenceId = true; }
        }
        const Stamp start = parseStamp(dtstart.value, dtstart.params);
        if (!start.valid) return;
        c.cancelled = status == QLatin1String("CANCELLED");
        if (c.cancelled && !c.hasRecurrenceId) return;

        c.event.allDay = start.dateOnly;
        c.zone = start.dateOnly ? QTimeZone::systemTimeZone() : start.zone;
        c.startDate = start.date;
        c.startTime = start.dateOnly ? QTime(0, 0) : start.time;

        const Stamp end = haveEnd ? parseStamp(dtend.value, dtend.params) : Stamp();
        if (start.dateOnly) {
            int days = 1;
            if (end.valid) days = int(start.date.daysTo(end.date));
            else if (haveDuration) days = int(parseDuration(duration.value) / 86400);
            c.durationDays = std::max(1, days);
        } else if (end.valid) {
            c.durationSecs = std::max<qint64>(0, start.instant().secsTo(end.instant()));
        } else if (haveDuration) {
            c.durationSecs = std::max<qint64>(0, parseDuration(duration.value));
        }

        if (!rrule.value.isEmpty()) c.rule = parseRule(rrule.value, c.zone);
        for (const Property &p : exdates)
            for (const QString &v : p.value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const Stamp s = parseStamp(v, p.params);
                if (s.valid) c.exdates << s.instant();
            }
        if (c.hasRecurrenceId) {
            const Stamp s = parseStamp(recurrenceId.value, recurrenceId.params);
            if (s.valid) c.recurrenceId = s.instant();
            else c.hasRecurrenceId = false;
        }
        c.event = makeEvent(c, c.startDate);
        out << c;
    };

    for (const QString &line : unfold(ics)) {
        const QString upper = line.trimmed().toUpper();
        if (upper == QLatin1String("BEGIN:VEVENT")) { inEvent = true; props.clear(); alarmDepth = 0; continue; }
        if (!inEvent) continue;
        if (upper == QLatin1String("BEGIN:VALARM")) { ++alarmDepth; continue; }
        if (upper == QLatin1String("END:VALARM")) { --alarmDepth; continue; }
        if (upper == QLatin1String("END:VEVENT")) { inEvent = false; finish(); continue; }
        if (alarmDepth > 0) continue;
        Property p;
        if (parseLine(line, p)) props << p;
    }
    return out;
}

QList<Event> parse(const QByteArray &ics, const QString &calendarName) {
    QList<Event> events;
    for (const Component &c : parseComponents(ics, calendarName))
        if (!c.cancelled) events << c.event;
    return events;
}

QList<Event> occurrences(const QList<Component> &components, QDate from, QDate to) {
    QList<Event> out;
    if (!from.isValid() || !to.isValid() || to < from) return out;
    const QDateTime windowStart(from, QTime(0, 0)), windowEnd(to.addDays(1), QTime(0, 0));

    // Overrides hide the occurrence they replace, whether or not they were
    // moved into the window themselves.
    QHash<QString, QList<QDateTime>> replaced;
    for (const Component &c : components)
        if (c.hasRecurrenceId) replaced[c.event.uid] << c.recurrenceId;

    for (const Component &c : components) {
        if (c.cancelled) continue;
        if (c.hasRecurrenceId || c.rule.frequency == Rule::None) {
            if (overlaps(c.event, windowStart, windowEnd)) out << c.event;
            continue;
        }
        // Zone conversion can move an occurrence a day either way, so enumerate
        // two days past the window and let the overlap test decide.
        const QDate limit = to.addDays(2);
        const QList<QDateTime> &skipped = replaced.value(c.event.uid);
        int emitted = 0;
        bool done = false;
        for (int k = 0; k < 200000 && !done; ++k) {
            if (periodAnchor(c.rule, c.startDate, k) > limit) break;
            for (const QDate &d : periodDates(c.rule, c.startDate, k)) {
                if (d < c.startDate) continue;
                const QDateTime start = occurrenceStart(c, d);
                if (c.rule.until.isValid() && start > c.rule.until) { done = true; break; }
                if (c.rule.count > 0 && ++emitted > c.rule.count) { done = true; break; }
                if (d > limit) { done = true; break; }
                if (c.exdates.contains(start) || skipped.contains(start)) continue;
                const Event e = makeEvent(c, d);
                if (overlaps(e, windowStart, windowEnd)) out << e;
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Event &a, const Event &b) {
        if (a.allDay != b.allDay) return a.allDay;
        return a.start < b.start;
    });
    return out;
}

}  // namespace Ics

namespace {

const QStringList kDefaultPalette = {QStringLiteral("#3b82f6"), QStringLiteral("#f59e0b"), QStringLiteral("#10b981"),
                                     QStringLiteral("#a855f7"), QStringLiteral("#ef4444"), QStringLiteral("#14b8a6")};

bool isUrl(const QString &source) {
    return source.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) || source.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
           || source.startsWith(QLatin1String("webcal://"), Qt::CaseInsensitive) || source.startsWith(QLatin1String("webcals://"), Qt::CaseInsensitive);
}

QUrl urlFor(const QString &source) {
    QString s = source.trimmed();
    if (s.startsWith(QLatin1String("webcal"), Qt::CaseInsensitive)) {
        const bool secure = s.startsWith(QLatin1String("webcals"), Qt::CaseInsensitive);
        // Plain webcal:// is https in practice; every hosted calendar redirects there anyway.
        s = QStringLiteral("https://") + s.mid(secure ? 10 : 9);
    }
    return QUrl(s);
}

QString localPath(const QString &source) {
    QString s = source.trimmed();
    if (s.startsWith(QLatin1String("file://"))) return QUrl(s).toLocalFile();
    if (s.startsWith(QLatin1String("~/"))) return QDir::homePath() + s.mid(1);
    return s;
}

QString displayName(const QString &source) {
    if (isUrl(source)) return urlFor(source).host();
    return QFileInfo(localPath(source)).completeBaseName();
}

QString clockText(int minute) {
    const int h = minute / 60 % 24, m = minute % 60;
    const int h12 = h % 12 == 0 ? 12 : h % 12;
    return QStringLiteral("%1%2 %3").arg(h12).arg(m ? QStringLiteral(":%1").arg(m, 2, 10, QLatin1Char('0')) : QString(), h < 12 ? QStringLiteral("AM") : QStringLiteral("PM"));
}

QString freeLabel(int minutes) {
    if (minutes < 60) return QStringLiteral("%1 min free").arg(minutes);
    const int h = minutes / 60, m = minutes % 60;
    if (m == 0) return QStringLiteral("%1 %2 free").arg(h).arg(h == 1 ? QStringLiteral("hour") : QStringLiteral("hours"));
    return QStringLiteral("%1 hr %2 min free").arg(h).arg(m);
}

}  // namespace

QStringList CalendarFeed::myAddresses() const {
    QStringList out;
    for (const QString &a : m_myEmails.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) out << a.toLower();
    return out;
}

QVariantMap CalendarFeed::eventMap(const Ics::Event &e, const QDate &day) const {
    const QDateTime dayStart(day, QTime(0, 0)), dayEnd(day.addDays(1), QTime(0, 0));
    QVariantMap m;
    m[QStringLiteral("title")] = e.title;
    m[QStringLiteral("allDay")] = e.allDay;
    m[QStringLiteral("start")] = e.allDay ? 0 : int(dayStart.secsTo(std::max(e.start, dayStart)) / 60);
    m[QStringLiteral("end")] = e.allDay ? 1440 : int(dayStart.secsTo(std::min(e.end, dayEnd)) / 60);
    m[QStringLiteral("color")] = e.color;
    m[QStringLiteral("location")] = e.location;
    m[QStringLiteral("calendar")] = e.calendar;
    m[QStringLiteral("description")] = e.description;
    m[QStringLiteral("url")] = e.url;
    const QStringList mine = myAddresses();
    bool declined = false;
    for (const QString &who : e.declinedBy) declined |= mine.contains(who);
    m[QStringLiteral("declined")] = declined;
    return m;
}

CalendarFeed::CalendarFeed(QObject *parent) : QObject(parent), m_palette(kDefaultPalette) {
    m_clock = [] { return QDateTime::currentDateTime(); };
    m_lastDay = now().date();
    // Calendars change rarely, so a quarter hour is a fair compromise between
    // freshness and hammering a hosted feed.
    m_refreshTimer.setInterval(15 * 60 * 1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &CalendarFeed::refresh);
    m_refreshTimer.start();
    // The caption and now-marker follow the clock, so re-evaluate every half minute.
    m_tickTimer.setInterval(30 * 1000);
    connect(&m_tickTimer, &QTimer::timeout, this, &CalendarFeed::tick);
    m_tickTimer.start();
}

void CalendarFeed::registerType() { qmlRegisterType<CalendarFeed>("Fairfax", 1, 0, "CalendarFeed"); }

void CalendarFeed::setClock(std::function<QDateTime()> clock) {
    m_clock = std::move(clock);
    m_lastDay = now().date();
    emit changed();
}

QDateTime CalendarFeed::now() const { return m_clock(); }

void CalendarFeed::tick() {
    // The marker and caption follow the clock and the day can roll over, so
    // bindings are simply re-evaluated; the cost is a handful of QML bindings.
    m_lastDay = now().date();
    emit changed();
}

void CalendarFeed::setSources(const QStringList &sources) {
    QStringList cleaned;
    for (const QString &s : sources)
        if (!s.trimmed().isEmpty() && !cleaned.contains(s.trimmed())) cleaned << s.trimmed();
    if (cleaned == m_sources) return;
    m_sources = cleaned;
    for (const QString &gone : m_data.keys())
        if (!m_sources.contains(gone)) m_data.remove(gone);
    for (const QString &gone : m_errors.keys())
        if (!m_sources.contains(gone)) m_errors.remove(gone);
    for (const QString &gone : m_pending.keys())
        if (!m_sources.contains(gone)) m_pending.take(gone)->abort();
    emit sourcesChanged();
    emit sourceInfoChanged();
    emit errorChanged();
    emit changed();
    refresh();
}

void CalendarFeed::setPalette(const QStringList &palette) {
    if (palette == m_palette) return;
    m_palette = palette;
    emit paletteChanged();
    emit sourceInfoChanged();
    emit changed();
}

QColor CalendarFeed::colorFor(int index) const {
    const QColor custom(m_colors.value(m_sources.at(index)).toString());
    if (custom.isValid()) return custom;
    const QStringList &colors = m_palette.isEmpty() ? kDefaultPalette : m_palette;
    return QColor(colors.at(index % colors.size()));
}

void CalendarFeed::setDisabledSources(const QStringList &sources) {
    if (sources == m_disabled) return;
    m_disabled = sources;
    emit sourceInfoChanged();
    emit changed();
}

void CalendarFeed::setSourceColors(const QVariantMap &colors) {
    if (colors == m_colors) return;
    m_colors = colors;
    emit sourceInfoChanged();
    emit changed();
}

void CalendarFeed::setMyEmails(const QString &emails) {
    if (emails == m_myEmails) return;
    m_myEmails = emails;
    emit changed();
}

QVariantList CalendarFeed::sourceInfo() const {
    QVariantList out;
    for (int i = 0; i < m_sources.size(); ++i) {
        const QString &source = m_sources.at(i);
        QVariantMap m;
        m[QStringLiteral("source")] = source;
        m[QStringLiteral("name")] = m_names.contains(source) ? m_names.value(source) : displayName(source);
        m[QStringLiteral("color")] = colorFor(i);
        m[QStringLiteral("customColor")] = m_colors.contains(source);
        m[QStringLiteral("enabled")] = !m_disabled.contains(source);
        out << m;
    }
    return out;
}

QList<Ics::Component> CalendarFeed::components() const {
    QList<Ics::Component> all;
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_disabled.contains(m_sources.at(i))) continue;
        const QColor color = colorFor(i);
        for (Ics::Component c : m_data.value(m_sources.at(i))) {
            c.event.color = color;
            all << c;
        }
    }
    return all;
}

QVariantList CalendarFeed::eventMaps(const QDate &day) const {
    QVariantList out;
    for (const Ics::Event &e : Ics::occurrences(components(), day, day)) out << eventMap(e, day);
    return out;
}

QVariantList CalendarFeed::today() const { return eventMaps(now().date()); }

QVariantList CalendarFeed::eventsFor(const QString &isoDate) const {
    const QDate day = QDate::fromString(isoDate, Qt::ISODate);
    return day.isValid() ? eventMaps(day) : QVariantList();
}

int CalendarFeed::nowMinute() const {
    const QDateTime n = now();
    return n.time().hour() * 60 + n.time().minute();
}

namespace {

// Picks the event the caption talks about: whatever is in progress, otherwise
// the next one to start. All-day events never headline; they have their own pills.
QVariantMap headline(const QVariantList &today, int nowMinute, bool *ongoing) {
    QVariantMap upcoming;
    bool haveUpcoming = false;
    for (const QVariant &v : today) {
        const QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("allDay")).toBool() || e.value(QStringLiteral("declined")).toBool()) continue;
        if (e.value(QStringLiteral("start")).toInt() <= nowMinute && nowMinute < e.value(QStringLiteral("end")).toInt()) {
            *ongoing = true;
            return e;
        }
        if (!haveUpcoming && e.value(QStringLiteral("start")).toInt() > nowMinute) {
            upcoming = e;
            haveUpcoming = true;
        }
    }
    *ongoing = false;
    return upcoming;
}

}  // namespace

QString CalendarFeed::caption() const {
    const QVariantList events = today();
    if (events.isEmpty()) return QStringLiteral("No events today");
    bool ongoing = false;
    const QVariantMap e = headline(events, nowMinute(), &ongoing);
    if (e.isEmpty()) return QStringLiteral("No more events today");
    return ongoing ? QStringLiteral("%1 · ongoing until %2").arg(e.value(QStringLiteral("title")).toString(), clockText(e.value(QStringLiteral("end")).toInt()))
                   : QStringLiteral("%1 · upcoming %2").arg(e.value(QStringLiteral("title")).toString(), clockText(e.value(QStringLiteral("start")).toInt()));
}

QColor CalendarFeed::captionColor() const {
    bool ongoing = false;
    const QVariantMap e = headline(today(), nowMinute(), &ongoing);
    return e.isEmpty() ? QColor() : e.value(QStringLiteral("color")).value<QColor>();
}

QVariantList CalendarFeed::freeSlots(const QString &isoDate, int fromMinute, int toMinute) const {
    QVariantList out;
    QList<QPair<int, int>> busy;
    for (const QVariant &v : eventsFor(isoDate)) {
        const QVariantMap e = v.toMap();
        // A declined invitation leaves the time free.
        if (e.value(QStringLiteral("allDay")).toBool() || e.value(QStringLiteral("declined")).toBool()) continue;
        busy.append({e.value(QStringLiteral("start")).toInt(), e.value(QStringLiteral("end")).toInt()});
    }
    std::sort(busy.begin(), busy.end());
    int cursor = fromMinute;
    auto emitGap = [&](int end) {
        if (end - cursor >= 30) {
            QVariantMap gap;
            gap[QStringLiteral("start")] = cursor;
            gap[QStringLiteral("end")] = end;
            gap[QStringLiteral("label")] = freeLabel(end - cursor);
            out << gap;
        }
    };
    for (const auto &[start, end] : busy) {
        if (end <= cursor) continue;
        if (start >= toMinute) break;
        emitGap(std::min(start, toMinute));
        cursor = std::max(cursor, end);
    }
    if (cursor < toMinute) emitGap(toMinute);
    return out;
}

QString CalendarFeed::error() const {
    QStringList lines;
    for (const QString &source : m_sources)
        if (m_errors.contains(source)) lines << m_errors.value(source);
    return lines.join(QLatin1Char('\n'));
}

void CalendarFeed::loadData(const QString &source, const QByteArray &ics) {
    const QString name = Ics::calendarName(ics);
    if (name.isEmpty()) m_names.remove(source);
    else m_names.insert(source, name);
    m_data.insert(source, Ics::parseComponents(ics, name.isEmpty() ? displayName(source) : name));
    m_errors.remove(source);
    emit errorChanged();
    emit sourceInfoChanged();
    emit changed();
}

QString CalendarFeed::cachePath(const QString &source) const {
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha1).toHex());
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/calendars/") + hash + QStringLiteral(".ics");
}

void CalendarFeed::fail(const QString &source, const QString &message) {
    m_errors.insert(source, message);
    emit errorChanged();
}

void CalendarFeed::refresh() {
    for (const QString &source : m_sources) {
        if (isUrl(source)) {
            fetch(source);
            continue;
        }
        QFile file(localPath(source));
        if (!file.open(QIODevice::ReadOnly)) {
            m_data.remove(source);
            fail(source, QStringLiteral("Cannot read %1: %2").arg(file.fileName(), file.errorString()));
            emit changed();
            continue;
        }
        loadData(source, file.readAll());
    }
}

void CalendarFeed::fetch(const QString &source) {
    // Show the last good copy immediately so the bar is not empty while the
    // network answers, or when the machine is offline.
    if (!m_data.contains(source)) {
        QFile cached(cachePath(source));
        if (cached.open(QIODevice::ReadOnly)) loadData(source, cached.readAll());
    }
    if (m_pending.contains(source)) return;
    if (!m_network) m_network = new QNetworkAccessManager(this);

    QNetworkRequest request(urlFor(source));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30 * 1000);
    request.setRawHeader("Accept", "text/calendar, */*;q=0.5");
    QNetworkReply *reply = m_network->get(request);
    m_pending.insert(source, reply);
    connect(reply, &QNetworkReply::finished, this, [this, source, reply] {
        reply->deleteLater();
        if (m_pending.value(source) == reply) m_pending.remove(source);
        if (reply->error() == QNetworkReply::OperationCanceledError) return;
        if (reply->error() != QNetworkReply::NoError) {
            fail(source, QStringLiteral("Cannot fetch %1: %2").arg(displayName(source), reply->errorString()));
            return;
        }
        const QByteArray body = reply->readAll();
        if (!body.contains("BEGIN:VCALENDAR")) {
            fail(source, QStringLiteral("%1 did not return an iCalendar feed").arg(displayName(source)));
            return;
        }
        const QString path = cachePath(source);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile cache(path);
        if (cache.open(QIODevice::WriteOnly | QIODevice::Truncate)) cache.write(body);
        loadData(source, body);
    });
}
