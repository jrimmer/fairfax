#include "vtodo.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUuid>
#include <algorithm>

#include "recurrence.h"
#include "store.h"

namespace {

const char *DayCodes[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
constexpr int MonToFri = 0b0011111;

// An unfolded iCalendar content line, kept raw so unknown properties can be
// written back exactly as they arrived.
struct RawProp {
    QString name;
    QMap<QString, QString> params;
    QString value;
    QString raw;
};

struct RawComponent {
    QString kind;
    QList<RawProp> props;
    QList<RawComponent> children;
    QString first(const QString &name) const {
        for (const RawProp &p : props)
            if (p.name == name)
                return p.value;
        return {};
    }
    const RawProp *find(const QString &name) const {
        for (const RawProp &p : props)
            if (p.name == name)
                return &p;
        return nullptr;
    }
};

QStringList splitOutsideQuotes(const QString &s, QChar sep) {
    QStringList parts;
    QString cur;
    bool quoted = false;
    for (QChar c : s) {
        if (c == '"') quoted = !quoted;
        if (c == sep && !quoted) {
            parts << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts << cur;
    return parts;
}

bool parseLine(const QString &line, RawProp &prop) {
    bool quoted = false;
    int colon = -1;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        else if (line[i] == ':' && !quoted) { colon = i; break; }
    }
    if (colon <= 0)
        return false;
    const QStringList head = splitOutsideQuotes(line.left(colon), ';');
    prop.name = head.first().trimmed().toUpper();
    prop.params.clear();
    for (int i = 1; i < head.size(); ++i) {
        const int eq = head[i].indexOf('=');
        if (eq > 0) {
            QString v = head[i].mid(eq + 1);
            if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"'))
                v = v.mid(1, v.size() - 2);
            prop.params.insert(head[i].left(eq).toUpper(), v);
        }
    }
    prop.value = line.mid(colon + 1);
    prop.raw = line;
    return true;
}

// Unfolds and parses the whole component tree. Unclosed components are closed
// at the end so a truncated file still yields what it had.
QList<RawComponent> parseComponents(const QByteArray &ics) {
    QString norm = QString::fromUtf8(ics);
    norm.replace("\r\n", "\n").replace('\r', '\n');
    QStringList lines;
    for (const QString &raw : norm.split('\n')) {
        if (!raw.isEmpty() && (raw[0] == ' ' || raw[0] == '\t') && !lines.isEmpty())
            lines.last() += raw.mid(1);
        else
            lines << raw;
    }
    QList<RawComponent> roots;
    QList<RawComponent> stack;
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        RawProp p;
        if (!parseLine(line, p))
            continue;
        if (p.name == "BEGIN") {
            RawComponent c;
            c.kind = p.value.trimmed().toUpper();
            stack.append(c);
        } else if (p.name == "END") {
            if (stack.isEmpty())
                continue;
            const RawComponent done = stack.takeLast();
            if (!stack.isEmpty())
                stack.last().children.append(done);
            else
                roots.append(done);
        } else if (!stack.isEmpty()) {
            stack.last().props.append(p);
        }
    }
    while (!stack.isEmpty()) {
        const RawComponent done = stack.takeFirst();
        if (!stack.isEmpty())
            stack.first().children.append(done);
        else
            roots.append(done);
    }
    return roots;
}

void collectTodos(const QList<RawComponent> &components, QList<RawComponent> &out) {
    for (const RawComponent &c : components) {
        if (c.kind == "VTODO")
            out.append(c);
        collectTodos(c.children, out);
    }
}

void componentLines(const RawComponent &c, QStringList &out) {
    out << "BEGIN:" + c.kind;
    for (const RawProp &p : c.props)
        out << p.raw;
    for (const RawComponent &child : c.children)
        componentLines(child, out);
    out << "END:" + c.kind;
}

QStringList splitUnescaped(const QString &s, QChar sep) {
    QStringList parts;
    QString cur;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            cur += s[i];
            cur += s[++i];
        } else if (s[i] == sep) {
            parts << cur;
            cur.clear();
        } else {
            cur += s[i];
        }
    }
    parts << cur;
    return parts;
}

QString icsStampLocal(const QDateTime &utc) {
    return utc.toUTC().toString("yyyyMMdd'T'HHmmss");
}

QString offsetText(int seconds) {
    const QChar sign = seconds < 0 ? '-' : '+';
    const int a = qAbs(seconds);
    return QStringLiteral("%1%2%3").arg(sign).arg(a / 3600, 2, 10, QChar('0')).arg((a % 3600) / 60, 2, 10, QChar('0'));
}

void appendTimezone(QByteArray &out, const QTimeZone &zone) {
    auto line = [&](const QString &l) { Vtodo::appendFolded(out, l); };
    line("BEGIN:VTIMEZONE");
    line("TZID:" + zone.id());

    const QList<QTimeZone::OffsetData> transitions =
        zone.transitions(QDateTime(QDate(2000, 1, 1), QTime(0, 0), QTimeZone::UTC),
                         QDateTime(QDate(2038, 1, 1), QTime(0, 0), QTimeZone::UTC));
    if (transitions.isEmpty()) {
        const int offset = zone.offsetFromUtc(QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC));
        line("BEGIN:STANDARD");
        line("DTSTART:19700101T000000");
        line("TZOFFSETFROM:" + offsetText(offset));
        line("TZOFFSETTO:" + offsetText(offset));
        line("TZNAME:" + (zone.id().isEmpty() ? QStringLiteral("UTC") : zone.id()));
        line("END:STANDARD");
        line("END:VTIMEZONE");
        return;
    }

    // The offset in force just before each transition, as TZOFFSETFROM.
    QList<int> fromOffsets;
    for (int i = 0; i < transitions.size(); ++i) {
        if (i > 0) {
            fromOffsets << transitions[i - 1].offsetFromUtc;
        } else if (transitions[i].daylightTimeOffset != 0) {
            fromOffsets << transitions[i].offsetFromUtc - transitions[i].daylightTimeOffset;
        } else {
            fromOffsets << transitions[i].offsetFromUtc + (zone.hasDaylightTime() ? 3600 : 0);
        }
    }

    auto writeObservance = [&](int i, const char *kind) {
        const int offset = transitions[i].offsetFromUtc;
        const QDateTime local = transitions[i].atUtc.addSecs(fromOffsets[i]);
        line(QStringLiteral("BEGIN:%1").arg(QLatin1String(kind)));
        line("TZOFFSETFROM:" + offsetText(fromOffsets[i]));
        line("TZOFFSETTO:" + offsetText(offset));
        if (!transitions[i].abbreviation.isEmpty())
            line("TZNAME:" + transitions[i].abbreviation);
        line("DTSTART:" + icsStampLocal(local));
        const QDate d = local.date();
        int ordinal = (d.day() + 6) / 7;
        if (d.addDays(7).month() != d.month())
            ordinal = -1;
        line(QStringLiteral("RRULE:FREQ=YEARLY;BYMONTH=%1;BYDAY=%2%3")
                 .arg(d.month())
                 .arg(ordinal)
                 .arg(QLatin1String(DayCodes[d.dayOfWeek() - 1])));
        line(QStringLiteral("END:%1").arg(QLatin1String(kind)));
    };

    int lastDaylight = -1, lastStandard = -1;
    for (int i = 0; i < transitions.size(); ++i) {
        if (transitions[i].daylightTimeOffset != 0)
            lastDaylight = i;
        else
            lastStandard = i;
    }
    if (lastDaylight >= 0)
        writeObservance(lastDaylight, "DAYLIGHT");
    if (lastStandard >= 0)
        writeObservance(lastStandard, "STANDARD");
    line("END:VTIMEZONE");
}

// Splits a description's unescaped text into the notes and a metadata block
// written by another client: it runs from a "<Something Meta>" line to its
// closing "</Something Meta>" line (or the end).
void splitMeta(const QString &description, QString &notes, QString &meta) {
    static const QRegularExpression opener(QStringLiteral("^<([^<>/\\n]+ Meta)>"));
    const QStringList lines = description.split('\n');
    int start = -1;
    QString closer;
    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = opener.match(lines[i]);
        if (m.hasMatch()) {
            start = i;
            closer = QStringLiteral("</") + m.captured(1) + QLatin1Char('>');
            break;
        }
    }
    if (start < 0) {
        notes = description;
        meta.clear();
        return;
    }
    int end = lines.size() - 1;
    for (int i = start + 1; i < lines.size(); ++i) {
        if (lines[i].trimmed() == closer) {
            end = i;
            break;
        }
    }
    notes = lines.mid(0, start).join('\n');
    notes = notes.trimmed().isEmpty() ? QString() : notes;
    meta = lines.mid(start, end - start + 1).join('\n');
}

QStringList locationNames(const Store &store, const Task &task) {
    QStringList names;
    for (qint64 id : task.locationIds) {
        const Location l = store.locations().value(id);
        if (l.deleted)
            continue;
        const QString name = l.name.isEmpty() ? l.address : l.name;
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    return names;
}

bool isSingleValued(const QString &name) {
    static const QSet<QString> names{
        "UID", "SUMMARY", "DESCRIPTION", "DTSTAMP", "CREATED", "LAST-MODIFIED", "DTSTART", "DUE",
        "PRIORITY", "STATUS", "COMPLETED", "PERCENT-COMPLETE", "X-FAIRFAX-LIST", "X-FAIRFAX-TYPE",
        "X-FAIRFAX-STARRED", "X-FAIRFAX-DURATION", "RRULE", "X-FAIRFAX-REPEAT-FROM", "LOCATION", "URL",
        "SEQUENCE", "DURATION"};
    return names.contains(name);
}

}  // namespace

namespace Vtodo {

QString escapeText(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == ';') out += "\\;";
        else if (c == ',') out += "\\,";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') continue;
        else out += c;
    }
    return out;
}

QString unescapeText(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const QChar n = s[++i];
            out += (n == 'n' || n == 'N') ? QChar('\n') : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Folds to 75 octets per line, never splitting a UTF-8 sequence.
void appendFolded(QByteArray &out, const QString &line) {
    int used = 0;
    const int limit = 75;
    int cap = limit;
    for (int i = 0; i < line.size();) {
        int len = 1;
        if (line[i].isHighSurrogate() && i + 1 < line.size())
            len = 2;
        const QByteArray bytes = line.mid(i, len).toUtf8();
        if (used + bytes.size() > cap) {
            out += "\r\n ";
            used = 1;
        }
        out += bytes;
        used += bytes.size();
        i += len;
    }
    out += "\r\n";
}

QString icsDate(const QDate &d) { return d.toString("yyyyMMdd"); }

QString icsLocal(const QDate &d, int minutes) {
    return icsDate(d) + QStringLiteral("T%1%2").arg(minutes / 60, 2, 10, QChar('0')).arg(minutes % 60, 2, 10, QChar('0')) + QStringLiteral("00");
}

QString icsUtc(const QDateTime &dt) { return dt.toUTC().toString("yyyyMMdd'T'HHmmss'Z'"); }

QString triggerText(int minutes) {
    // `minutes` before the due moment.
    const QString sign = minutes > 0 ? "-" : "";
    const int m = qAbs(minutes);
    if (m == 0)
        return QStringLiteral("PT0S");
    if (m % 1440 == 0)
        return QStringLiteral("%1P%2D").arg(sign).arg(m / 1440);
    if (m % 60 == 0)
        return QStringLiteral("%1PT%2H").arg(sign).arg(m / 60);
    return QStringLiteral("%1PT%2M").arg(sign).arg(m);
}

QString rruleText(const Recurrence &r) {
    QString freq;
    QString byday;
    switch (r.frequency) {
    case Repeat::Daily: freq = "DAILY"; break;
    case Repeat::Weekly: freq = "WEEKLY"; break;
    case Repeat::Weekdays: freq = "WEEKLY"; byday = "MO,TU,WE,TH,FR"; break;
    case Repeat::Monthly: freq = "MONTHLY"; break;
    case Repeat::Yearly: freq = "YEARLY"; break;
    default: return {};
    }
    QString out = "FREQ=" + freq;
    if (r.interval > 1)
        out += QStringLiteral(";INTERVAL=%1").arg(r.interval);
    if (r.frequency == Repeat::Weekly && r.weekdays) {
        QStringList days;
        for (int i = 0; i < 7; ++i)
            if (r.weekdays & (1 << i))
                days << DayCodes[i];
        byday = days.join(',');
    }
    if (r.frequency == Repeat::Monthly && r.monthOrdinal && r.monthWeekday >= 1 && r.monthWeekday <= 7)
        byday = QStringLiteral("%1%2").arg(r.monthOrdinal).arg(DayCodes[r.monthWeekday - 1]);
    if (!byday.isEmpty())
        out += ";BYDAY=" + byday;
    if (r.endType == Repeat::AfterCount && r.endCount > 0)
        out += QStringLiteral(";COUNT=%1").arg(r.endCount);
    else if (r.endType == Repeat::OnDate && r.endDate.isValid())
        out += ";UNTIL=" + icsDate(r.endDate);
    return out;
}

IcsDate parseIcsDate(const QString &raw, const QString &valueParam) {
    IcsDate out;
    const QString v = raw.trimmed();
    if (v.size() >= 15 && valueParam != "DATE") {
        QDateTime dt = QDateTime::fromString(v.left(15), "yyyyMMdd'T'HHmmss");
        if (!dt.isValid())
            return out;
        if (v.endsWith('Z', Qt::CaseInsensitive)) {
            dt.setTimeZone(QTimeZone::utc());
            dt = dt.toLocalTime();
        }
        out.date = dt.date();
        out.minutes = dt.time().hour() * 60 + dt.time().minute();
    } else if (v.size() >= 8) {
        out.date = QDate::fromString(v.left(8), "yyyyMMdd");
    }
    return out;
}

QDateTime parseIcsStamp(const QString &raw) {
    const QString v = raw.trimmed();
    if (v.size() < 15)
        return {};
    QDateTime dt = QDateTime::fromString(v.left(15), "yyyyMMdd'T'HHmmss");
    if (v.endsWith('Z', Qt::CaseInsensitive)) {
        dt.setTimeZone(QTimeZone::utc());
        return dt.toLocalTime();
    }
    return dt;
}

// Seconds from an RFC 5545 duration (P1DT2H30M, -PT15M), signed.
bool parseDurationSeconds(const QString &raw, qint64 *seconds) {
    QString v = raw.trimmed();
    int sign = 1;
    if (v.startsWith('-')) { sign = -1; v = v.mid(1); }
    else if (v.startsWith('+')) v = v.mid(1);
    if (!v.startsWith('P'))
        return false;
    qint64 total = 0;
    bool inTime = false;
    QString num;
    for (int i = 1; i < v.size(); ++i) {
        const QChar c = v[i];
        if (c == 'T') { inTime = true; continue; }
        if (c.isDigit()) { num += c; continue; }
        const qint64 n = num.toLongLong();
        num.clear();
        switch (c.toLatin1()) {
        case 'W': total += n * 7 * 86400; break;
        case 'D': total += n * 86400; break;
        case 'H': total += n * 3600; break;
        case 'M': total += inTime ? n * 60 : 0; break;
        case 'S': total += n; break;
        default: return false;
        }
    }
    *seconds = sign * total;
    return true;
}

int dayIndex(const QString &code) {
    for (int i = 0; i < 7; ++i)
        if (code.compare(DayCodes[i], Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

Recurrence parseRrule(const QString &rule) {
    Recurrence r;
    QMap<QString, QString> kv;
    for (const QString &part : rule.split(';')) {
        const int eq = part.indexOf('=');
        if (eq > 0)
            kv.insert(part.left(eq).trimmed().toUpper(), part.mid(eq + 1).trimmed());
    }
    const QString freq = kv.value("FREQ").toUpper();
    if (freq == "DAILY") r.frequency = Repeat::Daily;
    else if (freq == "WEEKLY") r.frequency = Repeat::Weekly;
    else if (freq == "MONTHLY") r.frequency = Repeat::Monthly;
    else if (freq == "YEARLY") r.frequency = Repeat::Yearly;
    else return Recurrence{};
    r.interval = qMax(1, kv.value("INTERVAL", "1").toInt());

    int mask = 0;
    for (const QString &d : kv.value("BYDAY").split(',', Qt::SkipEmptyParts)) {
        QString code = d.trimmed();
        QString ord;
        while (!code.isEmpty() && (code[0].isDigit() || code[0] == '-' || code[0] == '+')) {
            ord += code[0];
            code.remove(0, 1);
        }
        const int idx = dayIndex(code);
        if (idx < 0)
            continue;
        if (r.frequency == Repeat::Monthly || r.frequency == Repeat::Yearly) {
            if (!ord.isEmpty() && r.frequency == Repeat::Monthly && r.monthOrdinal == 0) {
                const int o = ord.toInt();
                if ((o >= 1 && o <= 5) || o == -1) {
                    r.monthOrdinal = o;
                    r.monthWeekday = idx + 1;
                }
            }
        } else {
            mask |= 1 << idx;
        }
    }
    if (r.frequency == Repeat::Weekly && mask) {
        if (mask == MonToFri)
            r.frequency = Repeat::Weekdays;
        else
            r.weekdays = mask;
    }
    if (kv.contains("COUNT") && kv.value("COUNT").toInt() > 0) {
        r.endType = Repeat::AfterCount;
        r.endCount = kv.value("COUNT").toInt();
    } else if (kv.contains("UNTIL")) {
        const IcsDate until = parseIcsDate(kv.value("UNTIL"));
        if (until.valid()) {
            r.endType = Repeat::OnDate;
            r.endDate = until.date;
        }
    }
    return r;
}

bool truthy(const QString &v) {
    const QString s = v.trimmed().toUpper();
    return s == "1" || s == "TRUE" || s == "YES";
}

namespace {

// Maps one VALARM into a Fairfax alarm. Returns false when its trigger is not
// something Fairfax can represent, in which case the caller keeps the whole
// component verbatim.
bool parseAlarm(const RawComponent &component, Alarm *alarm, QStringList *unknownProps) {
    const RawProp *trigger = component.find("TRIGGER");
    if (!trigger)
        return false;
    if (trigger->params.value("VALUE").toUpper() == "DATE-TIME") {
        alarm->kind = Alarm::Absolute;
        alarm->at = parseIcsStamp(trigger->value);
        if (!alarm->at.isValid())
            return false;
    } else {
        qint64 secs = 0;
        if (!parseDurationSeconds(trigger->value, &secs))
            return false;
        alarm->kind = Alarm::Relative;
        alarm->offset = qMax(0, int(qRound(-secs / 60.0)));
    }
    for (const RawProp &p : component.props) {
        if (p.name == "ACTION" || p.name == "DESCRIPTION" || p.name == "TRIGGER")
            continue;
        unknownProps->append(p.raw);
    }
    return true;
}

Parsed parseTodo(const RawComponent &component) {
    Parsed parsed;
    Task &t = parsed.task;
    QString description;
    bool haveDescription = false;
    qint64 durationSeconds = 0;

    auto value = [&](const QString &name) { return component.first(name); };

    parsed.uid = value("UID").trimmed();
    t.title = unescapeText(value("SUMMARY"));
    if (const RawProp *p = component.find("DESCRIPTION")) {
        description = unescapeText(p->value);
        haveDescription = true;
    }
    t.created = parseIcsStamp(value("CREATED"));
    t.modified = parseIcsStamp(value("LAST-MODIFIED"));

    if (const RawProp *p = component.find("DTSTART")) {
        const IcsDate d = parseIcsDate(p->value, p->params.value("VALUE").toUpper());
        t.startDate = d.date;
        t.startTime = d.minutes;
    }
    if (const RawProp *p = component.find("DUE")) {
        const IcsDate d = parseIcsDate(p->value, p->params.value("VALUE").toUpper());
        t.dueDate = d.date;
        t.dueTime = d.minutes;
    }
    const int prio = value("PRIORITY").trimmed().toInt();
    t.priority = prio >= 1 && prio <= 4 ? Priority::High : prio == 5 ? Priority::Medium : prio >= 6 && prio <= 9 ? Priority::Low : Priority::None;

    const QString status = value("STATUS").trimmed().toUpper();
    // CANCELLED is a task closed without being done: abandoned. A to-do that
    // also says it was COMPLETED stays done.
    t.abandoned = status == "CANCELLED" && value("PERCENT-COMPLETE").trimmed() != "100" && component.find("COMPLETED") == nullptr;
    t.completed = t.abandoned || status == "COMPLETED" || status == "CANCELLED" || value("PERCENT-COMPLETE").trimmed() == "100" ||
                  component.find("COMPLETED") != nullptr;
    if (t.completed) {
        t.completedAt = parseIcsStamp(value("COMPLETED"));
        // A cancelled to-do has no COMPLETED time; it was closed about when it last changed.
        if (!t.completedAt.isValid() && t.abandoned)
            t.completedAt = t.modified;
        if (!t.completedAt.isValid())
            t.completedAt = QDateTime::currentDateTime();
    }

    const QString type = value("X-FAIRFAX-TYPE").trimmed().toLower();
    t.type = type == "project" ? TaskType::Project : type == "checklist" ? TaskType::Checklist : TaskType::Task;
    t.starred = truthy(value("X-FAIRFAX-STARRED"));
    parsed.listName = unescapeText(value("X-FAIRFAX-LIST")).trimmed();
    parsed.locationName = unescapeText(value("LOCATION")).trimmed();

    const QString durationText = value("X-FAIRFAX-DURATION");
    if (!durationText.isEmpty())
        t.duration = qMax(0, durationText.trimmed().toInt());
    else if (parseDurationSeconds(value("DURATION"), &durationSeconds) && durationSeconds > 0)
        t.duration = int(durationSeconds / 60);
    if (const RawProp *p = component.find("URL")) {
        t.actionType = ActionType::Url;
        t.actionValue = unescapeText(p->value);
    }

    // Properties are walked in order: the first of each mapped name is used,
    // duplicates and everything unrecognised are kept verbatim for the write
    // back. The first PARENT link wins too; the rest are carried through.
    QSet<QString> seen;
    for (const RawProp &p : component.props) {
        if (p.name == "CATEGORIES") {
            for (const QString &raw : splitUnescaped(p.value, ',')) {
                const QString name = unescapeText(raw).trimmed();
                if (!name.isEmpty() && !parsed.tagNames.contains(name))
                    parsed.tagNames << name;
            }
            continue;
        }
        if (p.name == "RELATED-TO") {
            const QString rel = p.params.value("RELTYPE", "PARENT").toUpper();
            if (rel == "PARENT" && parsed.parentUid.isEmpty())
                parsed.parentUid = p.value.trimmed();
            else
                parsed.extra.properties << p.raw;
            continue;
        }
        if (!isSingleValued(p.name)) {
            parsed.extra.properties << p.raw;
            continue;
        }
        if (seen.contains(p.name)) {
            parsed.extra.properties << p.raw;
            continue;
        }
        seen.insert(p.name);
        if (p.name == "RRULE") {
            t.recurrence = parseRrule(p.value);
            if (t.recurrence.isSet() && value("X-FAIRFAX-REPEAT-FROM").trimmed().toUpper() == "COMPLETION")
                t.recurrence.from = Repeat::FromCompletion;
        } else if (p.name == "SEQUENCE") {
            parsed.extra.sequence = p.value.trimmed().toInt();
        }
    }

    if (!haveDescription)
        description.clear();
    splitMeta(description, t.notes, parsed.extra.meta);

    for (const RawComponent &child : component.children) {
        if (child.kind == "VALARM") {
            Alarm a;
            QStringList unknownProps;
            if (parseAlarm(child, &a, &unknownProps)) {
                parsed.alarms << a;
                parsed.extra.alarmProperties << unknownProps;
            } else {
                QStringList lines;
                componentLines(child, lines);
                parsed.extra.components << lines;
            }
        } else {
            QStringList lines;
            componentLines(child, lines);
            parsed.extra.components << lines;
        }
    }
    return parsed;
}

}  // namespace

void appendTodo(QByteArray &out, const Store &store, const Task &task, const WriteOptions &options) {
    const Extra &extra = options.extra;
    auto line = [&](const QString &l) { appendFolded(out, l); };
    const QTimeZone zone = QTimeZone::systemTimeZone();
    const QString tzid = zone.id();
    const bool useZone = options.caldav && !tzid.isEmpty();

    line("BEGIN:VTODO");
    line("UID:" + (task.uid.isEmpty() ? Store::newUid() : task.uid));
    line("SUMMARY:" + escapeText(task.title));
    QString description = task.notes;
    if (options.caldav && !extra.meta.isEmpty()) {
        if (!description.isEmpty())
            description += "\n";
        description += extra.meta;
    }
    if (!description.isEmpty())
        line("DESCRIPTION:" + escapeText(description));
    line("DTSTAMP:" + (options.stamp.isEmpty() ? icsUtc(QDateTime::currentDateTime()) : options.stamp));
    if (task.created.isValid())
        line("CREATED:" + icsUtc(task.created));
    if (task.modified.isValid())
        line("LAST-MODIFIED:" + icsUtc(task.modified));
    if (options.caldav && extra.sequence >= 0)
        line(QStringLiteral("SEQUENCE:%1").arg(extra.sequence));
    if (task.startDate.isValid()) {
        if (task.startTime >= 0 && useZone)
            line("DTSTART;TZID=" + tzid + ":" + icsLocal(task.startDate, task.startTime));
        else
            line(task.startTime >= 0 ? "DTSTART:" + icsLocal(task.startDate, task.startTime)
                                     : "DTSTART;VALUE=DATE:" + icsDate(task.startDate));
    }
    if (task.dueDate.isValid()) {
        if (task.dueTime >= 0 && useZone)
            line("DUE;TZID=" + tzid + ":" + icsLocal(task.dueDate, task.dueTime));
        else
            line(task.dueTime >= 0 ? "DUE:" + icsLocal(task.dueDate, task.dueTime) : "DUE;VALUE=DATE:" + icsDate(task.dueDate));
    }
    switch (task.priority) {
    case Priority::High: line("PRIORITY:1"); break;
    case Priority::Medium: line("PRIORITY:5"); break;
    case Priority::Low: line("PRIORITY:9"); break;
    default: break;
    }
    if (task.abandoned) {
        line("STATUS:CANCELLED");
    } else if (task.completed) {
        line("STATUS:COMPLETED");
        if (task.completedAt.isValid())
            line("COMPLETED:" + icsUtc(task.completedAt));
    } else {
        line("STATUS:NEEDS-ACTION");
    }
    QStringList cats;
    for (qint64 tid : task.tagIds)
        if (store.tags().contains(tid))
            cats << escapeText(store.tag(tid).name);
    if (!cats.isEmpty())
        line("CATEGORIES:" + cats.join(','));
    if (store.lists().contains(task.listId))
        line("X-FAIRFAX-LIST:" + escapeText(store.list(task.listId).name));
    line(QString("X-FAIRFAX-TYPE:") + (task.type == TaskType::Project ? "project" : task.type == TaskType::Checklist ? "checklist" : "task"));
    if (task.starred)
        line("X-FAIRFAX-STARRED:1");
    if (task.duration > 0)
        line(QStringLiteral("X-FAIRFAX-DURATION:%1").arg(task.duration));
    const QString parentUid = options.parentIncluded && options.parentUid.isEmpty() && task.parentId &&
                                      store.tasks().contains(task.parentId)
                                  ? store.task(task.parentId).uid
                                  : options.parentUid;
    if (!parentUid.isEmpty())
        line("RELATED-TO;RELTYPE=PARENT:" + parentUid);
    if (task.recurrence.isSet()) {
        line("RRULE:" + rruleText(task.recurrence));
        if (task.recurrence.from == Repeat::FromCompletion)
            line("X-FAIRFAX-REPEAT-FROM:COMPLETION");
    }
    if (options.caldav) {
        const QStringList places = locationNames(store, task);
        if (!places.isEmpty())
            line("LOCATION:" + escapeText(places.join(", ")));
        if (task.actionType == ActionType::Url && !task.actionValue.isEmpty())
            line("URL:" + escapeText(task.actionValue));
        for (const QString &raw : extra.properties)
            line(raw);
    }

    int alarmIndex = 0;
    for (const Alarm &a : store.alarmsFor(task.id)) {
        if (a.deleted)
            continue;
        line("BEGIN:VALARM");
        line("ACTION:DISPLAY");
        line("DESCRIPTION:" + escapeText(task.title));
        if (a.kind == Alarm::Absolute && a.at.isValid())
            line("TRIGGER;VALUE=DATE-TIME:" + icsUtc(a.at));
        else if (a.kind == Alarm::DaysBefore)  // Counted back from midnight of the date.
            line("TRIGGER:" + triggerText(a.offset * 24 * 60 - (a.at.time().hour() * 60 + a.at.time().minute())));
        else
            line("TRIGGER:" + triggerText(a.offset));
        if (options.caldav && alarmIndex < extra.alarmProperties.size())
            for (const QString &raw : extra.alarmProperties.at(alarmIndex))
                line(raw);
        line("END:VALARM");
        ++alarmIndex;
    }
    if (options.caldav)
        for (const QStringList &lines : extra.components)
            for (const QString &raw : lines)
                line(raw);
    line("END:VTODO");
}

QByteArray toIcs(const Store &store, const Task &task, const Extra &extra) {
    QByteArray out;
    auto line = [&](const QString &l) { appendFolded(out, l); };
    line("BEGIN:VCALENDAR");
    line("VERSION:2.0");
    line("PRODID:-//Fairfax//EN");
    line("CALSCALE:GREGORIAN");
    if (task.startTime >= 0 || task.dueTime >= 0)
        appendTimezone(out, QTimeZone::systemTimeZone());
    WriteOptions options;
    options.extra = extra;
    options.caldav = true;
    appendTodo(out, store, task, options);
    line("END:VCALENDAR");
    return out;
}

Parsed fromIcs(const QByteArray &ics) {
    QList<RawComponent> todos;
    collectTodos(parseComponents(ics), todos);
    if (todos.isEmpty())
        return {};
    return parseTodo(todos.first());
}

QList<Parsed> allFromIcs(const QByteArray &ics) {
    QList<RawComponent> todos;
    collectTodos(parseComponents(ics), todos);
    QList<Parsed> parsed;
    parsed.reserve(todos.size());
    for (const RawComponent &c : todos)
        parsed << parseTodo(c);
    return parsed;
}

}  // namespace Vtodo
