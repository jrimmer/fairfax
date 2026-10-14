#include "exchange.h"
#include <QRegularExpression>

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QStringDecoder>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QLocale>
#include <QMarginsF>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <functional>

#include "model.h"
#include "recurrence.h"
#include "store.h"

namespace {

// ------------------------------------------------------------------ shared

bool setError(QString *error, const QString &text) {
    if (error)
        *error = text;
    return false;
}

QList<qint64> exportIds(const Store &store, const QList<qint64> &requested) {
    QList<qint64> ids;
    if (requested.isEmpty()) {
        for (const Task &t : store.tasks())
            if (!t.deleted)
                ids << t.id;
    } else {
        for (qint64 id : requested) {
            const auto it = store.tasks().constFind(id);
            if (it != store.tasks().constEnd() && !it->deleted && !ids.contains(id))
                ids << id;
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

QString isoDate(const QDate &d) { return d.toString(Qt::ISODate); }

QString isoDateTime(const QDate &d, int minutes) {
    if (!d.isValid())
        return {};
    QString s = isoDate(d);
    if (minutes >= 0)
        s += QStringLiteral(" %1:%2").arg(minutes / 60, 2, 10, QChar('0')).arg(minutes % 60, 2, 10, QChar('0'));
    return s;
}

bool writeFile(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, file.errorString());
    if (file.write(bytes) != bytes.size())
        return setError(error, file.errorString());
    if (!file.commit())
        return setError(error, file.errorString());
    return true;
}

// ------------------------------------------------------------------- CSV

QString csvField(const QString &s) {
    if (s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r'))
        return '"' + QString(s).replace('"', "\"\"") + '"';
    return s;
}

QString priorityName(int p) {
    switch (p) {
    case Priority::Low: return QStringLiteral("Low");
    case Priority::Medium: return QStringLiteral("Medium");
    case Priority::High: return QStringLiteral("High");
    }
    return QStringLiteral("None");
}

QString typeName(int t) {
    switch (t) {
    case TaskType::Project: return QStringLiteral("Project");
    case TaskType::Checklist: return QStringLiteral("Checklist");
    }
    return QStringLiteral("Task");
}

// ------------------------------------------------------------------- ICS

const char *DayCodes[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
constexpr int MonToFri = 0b0011111;

QString escapeText(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == ';') out += "\;";
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

struct Prop {
    QString name;
    QMap<QString, QString> params;
    QString value;
};

struct Component {
    QString kind;
    QList<Prop> props;
    QList<Component> children;
    QString first(const QString &name) const {
        for (const Prop &p : props)
            if (p.name == name)
                return p.value;
        return {};
    }
    const Prop *find(const QString &name) const {
        for (const Prop &p : props)
            if (p.name == name)
                return &p;
        return nullptr;
    }
};

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

bool parseLine(const QString &line, Prop &prop) {
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
    return true;
}

// Reads the VTODO components (anywhere in the nesting) from unfolded text.
QList<Component> parseTodos(const QString &text) {
    QString norm = text;
    norm.replace("\r\n", "\n").replace('\r', '\n');
    QStringList lines;
    for (const QString &raw : norm.split('\n')) {
        if (!raw.isEmpty() && (raw[0] == ' ' || raw[0] == '\t') && !lines.isEmpty())
            lines.last() += raw.mid(1);
        else
            lines << raw;
    }
    QList<Component> todos;
    QList<Component> stack;  // Open components; popped ones attach to their parent.
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        Prop p;
        if (!parseLine(line, p))
            continue;
        if (p.name == "BEGIN") {
            Component c;
            c.kind = p.value.trimmed().toUpper();
            stack.append(c);
        } else if (p.name == "END") {
            if (stack.isEmpty())
                continue;
            Component done = stack.takeLast();
            if (done.kind == "VTODO")
                todos.append(done);
            else if (!stack.isEmpty() && stack.last().kind == "VTODO")
                stack.last().children.append(done);
        } else if (!stack.isEmpty()) {
            stack.last().props.append(p);
        }
    }
    return todos;
}

struct IcsDate {
    QDate date;
    int minutes = -1;
    bool valid() const { return date.isValid(); }
};

IcsDate parseIcsDate(const QString &raw, const QString &valueParam = {}) {
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

// Finds or creates lists and tags within one import change, so repeated
// names resolve to the same record.
struct Resolver {
    Store &store;
    QHash<QString, qint64> lists;  // lower-case name -> id
    QHash<QString, qint64> tags;
    int listsCreated = 0;
    int tagsCreated = 0;

    explicit Resolver(Store &s) : store(s) {
        for (const TaskList &l : store.lists())
            if (!l.deleted && l.kind != ListKind::Smart)
                lists.insert(l.name.toLower(), l.id);
        for (const Tag &t : store.tags())
            if (!t.deleted)
                tags.insert(t.name.toLower(), t.id);
    }

    qint64 listNamed(const QString &name) {
        const QString key = name.toLower();
        if (lists.contains(key))
            return lists.value(key);
        TaskList l;
        l.id = store.nextId("lists");
        l.name = name;
        l.order = l.id;
        l.color = int(store.lists().size()) % 13;
        store.putList(l);
        lists.insert(key, l.id);
        ++listsCreated;
        return l.id;
    }

    qint64 tagNamed(const QString &name, qint64 groupId = 0, bool paused = false) {
        const QString key = name.toLower();
        if (tags.contains(key))
            return tags.value(key);
        Tag t;
        t.id = store.nextId("tags");
        t.name = name;
        t.groupId = groupId;
        t.paused = paused;
        t.order = t.id;
        store.putTag(t);
        tags.insert(key, t.id);
        ++tagsCreated;
        return t.id;
    }
};

// Breaks parent loops by turning the offending task into a top-level task.
void breakCycles(QList<Task> &tasks) {
    QHash<qint64, int> index;
    for (int i = 0; i < tasks.size(); ++i)
        index.insert(tasks[i].id, i);
    for (int i = 0; i < tasks.size(); ++i) {
        QSet<qint64> seen{tasks[i].id};
        qint64 p = tasks[i].parentId;
        while (p) {
            if (seen.contains(p)) {
                tasks[i].parentId = 0;
                break;
            }
            seen.insert(p);
            p = index.contains(p) ? tasks[index.value(p)].parentId : 0;
        }
    }
}

// ----------------------------------------------------------------- the reference app DB

// One row of a SELECT *, tolerant of columns an older the reference app database lacks.
struct Row {
    QSqlRecord rec;
    QSqlQuery *q;
    QVariant get(const char *col) const {
        const int i = rec.indexOf(QLatin1String(col));
        return i < 0 ? QVariant() : q->value(i);
    }
    QString str(const char *col) const { return get(col).toString(); }
    qint64 num(const char *col, qint64 fallback = 0) const {
        const QVariant v = get(col);
        return v.isNull() || !v.isValid() ? fallback : v.toLongLong();
    }
    double dbl(const char *col, double fallback = 0) const {
        const QVariant v = get(col);
        return v.isNull() || !v.isValid() ? fallback : v.toDouble();
    }
    bool flag(const char *col) const { return num(col) != 0; }
};

QDate dateFromSecs(double secs) {
    if (secs <= 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(qint64(secs), QTimeZone::utc()).date();
}

QStringList splitTagNames(const QString &s) {
    QStringList out;
    for (const QString &p : s.split(',', Qt::SkipEmptyParts)) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

ImportResult importTwoDoImpl(Store &store, QSqlDatabase &db) {
    ImportResult result;
    const QStringList tables = db.tables();
    for (const char *needed : {"tasks", "calendars"})
        if (!tables.contains(QLatin1String(needed), Qt::CaseInsensitive)) {
            result.error = QStringLiteral("This does not look like a the reference app database: table \"%1\" is missing.").arg(needed);
            return result;
        }

    auto rows = [&](const char *table, const QString &where, const QString &order, const std::function<void(const Row &)> &fn) {
        if (!tables.contains(QLatin1String(table), Qt::CaseInsensitive))
            return true;
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (!q.exec(QStringLiteral("SELECT * FROM %1%2%3").arg(table, where.isEmpty() ? "" : " WHERE " + where,
                                                              order.isEmpty() ? "" : " ORDER BY " + order))) {
            result.error = QStringLiteral("Could not read %1: %2").arg(table, q.lastError().text());
            return false;
        }
        const QSqlRecord rec = q.record();
        while (q.next())
            fn(Row{rec, &q});
        return true;
    };

    store.begin(QStringLiteral("Import from the reference app"));
    Resolver res(store);
    auto fail = [&]() {
        // No rollback API: commit what was staged and undo it again.
        store.commit();
        if (store.undoLabel() == QLatin1String("Import from the reference app"))
            store.undo();
        result.tasks = result.lists = result.tags = result.alarms = 0;
        return result;
    };

    // List groups.
    QHash<QString, qint64> groupByUid;
    if (!rows("calgroups", "COALESCE(isdeleted,0)=0", "displayorder", [&](const Row &r) {
            // the reference app's sidebar sections (FOCUS, LISTS, SMART LISTS, …) are rows
            // here too, with uids like AppCalGroupFocus and grouptype 2–4;
            // only the user's own groups become list groups.
            if (r.str("uid").startsWith("AppCalGroup") || (r.num("grouptype") >= 2 && r.num("grouptype") <= 5))
                return;
            ListGroup g;
            g.id = store.nextId("list_groups");
            g.name = r.str("groupname");
            g.order = r.dbl("displayorder", g.id);
            g.collapsed = r.flag("iscollapsed");
            store.putGroup(g);
            groupByUid.insert(r.str("uid"), g.id);
        })) {
        return fail();
    }

    // Lists. Focus lists are skipped; an inbox merges into the store's own.
    qint64 existingInbox = 0;
    for (const TaskList &l : store.lists())
        if (!l.deleted && l.kind == ListKind::Inbox)
            existingInbox = l.id;
    qint64 inboxId = existingInbox;
    QHash<QString, qint64> listByUid;
    if (!rows("calendars", "COALESCE(isdeleted,0)=0", "displayorder", [&](const Row &r) {
            if (r.flag("isallcal") || r.flag("istodaycal") || r.flag("starred") || r.flag("isscheduled") ||
                r.flag("donecal") || r.flag("isbirthday") || r.flag("isanniversary"))
                return;
            const bool inbox = r.flag("isinboxcal");
            if (inbox && existingInbox) {
                listByUid.insert(r.str("uid"), existingInbox);
                return;
            }
            TaskList l;
            l.id = store.nextId("lists");
            l.name = r.str("title");
            if (l.name.isEmpty())
                l.name = inbox ? QStringLiteral("Inbox") : QStringLiteral("Untitled");
            const QString smart = r.str("smartsearch");
            l.kind = inbox ? ListKind::Inbox : (!smart.isEmpty() ? ListKind::Smart : ListKind::Normal);
            // "*" is the reference app's match-everything search, used with a date range.
            l.smartQuery = smart.trimmed() == "*" ? QString() : smart;
            if (l.kind == ListKind::Smart) {
                // the reference app's date filter: small numbers are a Dynamic Range (N, unit
                // 1 days … 4 years, as in its "Next 3 Days" preset: 3, 1);
                // large ones are a fixed range of timestamps.
                const double from = r.dbl("smartrangefrom", 0), to = r.dbl("smartrangeto", 0);
                if (from > 0 && from < 10000 && to >= 1 && to <= 4)
                    l.smartRange = QStringLiteral("next:%1:%2").arg(int(from)).arg(QString("dwmy").at(int(to) - 1));
                else if (from > 100000 && to > 100000)
                    l.smartRange = QStringLiteral("fixed:%1:%2")
                                       .arg(QDateTime::fromSecsSinceEpoch(qint64(from), QTimeZone::UTC).date().toString(Qt::ISODate),
                                            QDateTime::fromSecsSinceEpoch(qint64(to), QTimeZone::UTC).date().toString(Qt::ISODate));
            }
            // the reference app's list sorts as seen in a real database: 1 due date,
            // 11 start date, 8 completion; anything else falls back to due date.
            const int sort = int(r.num("tasksortfield", 1));
            l.sortField = sort == 11 ? SortField::StartDate : sort == 8 ? SortField::Completion : SortField::DueDate;
            l.sortAscending = r.num("tasksortorder", 0) == 0;
            l.color = int(qAbs(r.num("colorid")) % 13);
            l.order = r.dbl("displayorder", l.id);
            l.groupId = groupByUid.value(r.str("parentuid"), 0);
            l.showInAll = r.num("canshowinall", 1) != 0;
            l.showInToday = r.num("canshowintoday", 1) != 0;
            l.showInDone = r.num("showindone", 1) != 0;
            l.archived = r.flag("isarchived");
            store.putList(l);
            listByUid.insert(r.str("uid"), l.id);
            if (inbox)
                inboxId = l.id;
        })) {
        return fail();
    }
    result.lists = int(listByUid.size() - (existingInbox ? [&] {
        int n = 0;
        for (qint64 id : listByUid)
            n += id == existingInbox;
        return n;
    }() : 0));
    auto fallbackList = [&]() -> qint64 {
        if (!inboxId) {
            TaskList l;
            l.id = store.nextId("lists");
            l.name = QStringLiteral("Inbox");
            l.kind = ListKind::Inbox;
            l.order = l.id;
            store.putList(l);
            inboxId = l.id;
            ++result.lists;
        }
        return inboxId;
    };

    // Tag groups and tags.
    QHash<QString, qint64> tagGroupByUid;
    if (!rows("taggroups", "COALESCE(isdeleted,0)=0", "displayorder", [&](const Row &r) {
            TagGroup g;
            g.id = store.nextId("tag_groups");
            g.name = r.str("groupname");
            g.order = r.dbl("displayorder", g.id);
            g.collapsed = r.flag("iscollapsed");
            store.putTagGroup(g);
            tagGroupByUid.insert(r.str("uid"), g.id);
        })) {
        return fail();
    }
    if (!rows("tags", "COALESCE(isdeleted,0)=0", "displayorder", [&](const Row &r) {
            const QString name = r.str("tag").trimmed();
            if (name.isEmpty())
                return;
            const int before = res.tagsCreated;
            const qint64 id = res.tagNamed(name, tagGroupByUid.value(r.str("parentuid"), 0), r.flag("isheld"));
            if (res.tagsCreated > before) {
                Tag t = store.tag(id);
                t.order = r.dbl("displayorder", t.id);
                store.putTag(t);
            }
        })) {
        return fail();
    }

    // Sections within lists (the reference app's tasksections, confirmed in a 4.21 database).
    QHash<QString, qint64> sectionByUid;
    if (!rows("tasksections", "COALESCE(isdeleted,0)=0", "displayorder", [&](const Row &r) {
            const qint64 listId = listByUid.value(r.str("calendaruid"), 0);
            if (!listId)
                return;
            ListSection sec;
            sec.id = store.nextId("sections");
            sec.listId = listId;
            sec.name = r.str("sectionname").isEmpty() ? QStringLiteral("Section") : r.str("sectionname");
            sec.order = r.dbl("displayorder", sec.id);
            store.putSection(sec);
            sectionByUid.insert(r.str("uid"), sec.id);
        })) {
        return fail();
    }

    // Tasks. Ids are assigned up front so parents resolve by uid.
    QList<Task> tasks;
    QHash<QString, qint64> taskByUid;
    QHash<qint64, QString> parentUidOf;
    if (!rows("tasks", "COALESCE(isdeleted,0)=0", "displayorder", [&](const Row &r) {
            Task t;
            t.id = store.nextId("tasks");
            t.listId = listByUid.contains(r.str("calendaruid")) ? listByUid.value(r.str("calendaruid")) : fallbackList();
            t.sectionId = sectionByUid.value(r.str("sectionuid"), 0);
            t.title = r.str("title");
            t.notes = r.str("notes");
            // the reference app's tasktype: 0 task, 1 checklist, 2 project (ours: Project=1, Checklist=2).
            const int tt = int(r.num("tasktype"));
            t.type = tt == 1 ? TaskType::Checklist : tt == 2 ? TaskType::Project : TaskType::Task;
            // the reference app's priority: 0 none, 1 low, 2 medium, 3 high; same as ours.
            t.priority = int(qBound<qint64>(0, r.num("priority"), qint64(3)));
            t.starred = r.flag("starred");
            t.dueDate = dateFromSecs(r.dbl("duedate"));
            t.startDate = dateFromSecs(r.dbl("startdate"));
            const double dueTime = r.dbl("duetime", 999999);
            if (t.dueDate.isValid() && dueTime >= 0 && dueTime < 86400)
                t.dueTime = int(dueTime) / 60;
            t.duration = int(qMax<qint64>(0, r.num("duration")) / 60);
            // Archived tasks are kept, as completed.
            t.completed = r.flag("iscompleted") || r.flag("archived");
            if (t.completed) {
                const qint64 done = r.num("completeddate");
                t.completedAt = done > 0 ? QDateTime::fromSecsSinceEpoch(done) : QDateTime::currentDateTime();
            }
            // actiontype -1 is none; 0..5 follow the order of our presets.
            const int at = int(r.num("actiontype", -1));
            if (at >= 0 && at <= 5) {
                t.actionType = at + 1;
                t.actionValue = r.str("actionvalue");
            }
            // Repeat (best effort, no sample data): `recurrence` is read as the
            // frequency (1 daily, 2 weekly, 3 monthly, 4 yearly, 5 weekdays,
            // 6 bi-weekly), `repeatvalue` as the interval, `repeattype` 1 as
            // "from completion", recurrenceendtype 1/2 as after N / on date.
            const int rec = int(r.num("recurrence"));
            switch (rec) {
            case 1: t.recurrence.frequency = Repeat::Daily; break;
            case 2: t.recurrence.frequency = Repeat::Weekly; break;
            case 3: t.recurrence.frequency = Repeat::Monthly; break;
            case 4: t.recurrence.frequency = Repeat::Yearly; break;
            case 5: t.recurrence.frequency = Repeat::Weekdays; break;
            case 6: t.recurrence.frequency = Repeat::Weekly; break;
            default: break;
            }
            if (t.recurrence.isSet()) {
                t.recurrence.interval = int(qMax<qint64>(1, r.num("repeatvalue", 1)));
                if (rec == 6)
                    t.recurrence.interval *= 2;
                t.recurrence.from = r.num("repeattype") == 1 ? Repeat::FromCompletion : Repeat::FromDue;
                const int endType = int(r.num("recurrenceendtype"));
                if (endType == 1 && r.num("recurrenceendrepeats") > 0) {
                    t.recurrence.endType = Repeat::AfterCount;
                    t.recurrence.endCount = int(r.num("recurrenceendrepeats"));
                } else if (endType == 2 && r.dbl("recurrenceenddate") > 0) {
                    t.recurrence.endType = Repeat::OnDate;
                    t.recurrence.endDate = dateFromSecs(r.dbl("recurrenceenddate"));
                }
            }
            t.order = r.dbl("displayorder", t.id);
            t.collapsed = r.flag("iscollapsed");
            const double created = r.dbl("creationstamp") > 0 ? r.dbl("creationstamp") : r.dbl("datestamp");
            if (created > 0)
                t.created = QDateTime::fromSecsSinceEpoch(qint64(created));
            for (const QString &name : splitTagNames(r.str("tags"))) {
                const qint64 id = res.tagNamed(name);
                if (!t.tagIds.contains(id))
                    t.tagIds << id;
            }
            taskByUid.insert(r.str("uid"), t.id);
            if (!r.str("parent").isEmpty())
                parentUidOf.insert(t.id, r.str("parent"));
            tasks.append(t);
        })) {
        return fail();
    }
    for (Task &t : tasks)
        t.parentId = taskByUid.value(parentUidOf.value(t.id), 0);
    breakCycles(tasks);
    for (const Task &t : tasks)
        store.putTask(t);
    result.tasks = int(tasks.size());

    // Alarms.
    rows("alarms", "COALESCE(isdeleted,0)=0", "", [&](const Row &r) {
        const qint64 taskId = taskByUid.value(r.str("taskuid"), 0);
        if (!taskId)
            return;
        Alarm a;
        a.id = store.nextId("alarms");
        a.taskId = taskId;
        const double rel = r.dbl("relativetrigger");
        const double abs = r.dbl("absolutetrigger");
        if (rel == 0 && abs > 0) {
            a.kind = Alarm::Absolute;
            a.at = QDateTime::fromSecsSinceEpoch(qint64(abs));
        } else {
            a.kind = Alarm::Relative;
            a.offset = int(qRound(qAbs(rel) / 60.0));
        }
        store.putAlarm(a);
        ++result.alarms;
    });
    if (!result.error.isEmpty()) {
        return fail();
    }

    result.tags = res.tagsCreated;
    if (!store.commit())
        result.error = QStringLiteral("Could not save the imported data.");
    return result;
}

}  // namespace

namespace Exchange {

bool exportCsv(const Store &store, const QString &path, const QList<qint64> &taskIds, QString *error) {
    QString text;
    const QStringList header{"Title", "List", "Type", "Parent", "Notes", "Priority", "Starred", "Start",
                             "Due", "Duration", "Tags", "Repeat", "Completed", "Completed At", "Created"};
    text += header.join(',') + "\r\n";
    for (qint64 id : exportIds(store, taskIds)) {
        const Task t = store.task(id);
        QStringList tagNames;
        for (qint64 tid : t.tagIds)
            if (store.tags().contains(tid))
                tagNames << store.tag(tid).name;
        const QStringList cols{
            t.title,
            store.lists().contains(t.listId) ? store.list(t.listId).name : QString(),
            typeName(t.type),
            t.parentId && store.tasks().contains(t.parentId) ? store.task(t.parentId).title : QString(),
            t.notes,
            priorityName(t.priority),
            t.starred ? "Yes" : "No",
            isoDateTime(t.startDate, t.startTime),
            isoDateTime(t.dueDate, t.dueTime),
            t.duration > 0 ? QString::number(t.duration) : QString(),
            tagNames.join(", "),
            Recurrences::describe(t.recurrence),
            t.completed ? "Yes" : "No",
            t.completed && t.completedAt.isValid() ? t.completedAt.toString("yyyy-MM-dd HH:mm") : QString(),
            t.created.isValid() ? t.created.toString("yyyy-MM-dd HH:mm") : QString(),
        };
        QStringList quoted;
        for (const QString &c : cols)
            quoted << csvField(c);
        text += quoted.join(',') + "\r\n";
    }
    return writeFile(path, QByteArray("\xEF\xBB\xBF") + text.toUtf8(), error);
}

bool exportIcs(const Store &store, const QString &path, const QList<qint64> &taskIds, QString *error) {
    const QList<qint64> ids = exportIds(store, taskIds);
    const QSet<qint64> included(ids.begin(), ids.end());
    const QString stamp = icsUtc(QDateTime::currentDateTime());
    QByteArray out;
    auto line = [&](const QString &l) { appendFolded(out, l); };
    line("BEGIN:VCALENDAR");
    line("VERSION:2.0");
    line("PRODID:-//Fairfax//EN");
    line("CALSCALE:GREGORIAN");
    for (qint64 id : ids) {
        const Task t = store.task(id);
        line("BEGIN:VTODO");
        line("UID:" + (t.uid.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : t.uid));
        line("SUMMARY:" + escapeText(t.title));
        if (!t.notes.isEmpty())
            line("DESCRIPTION:" + escapeText(t.notes));
        line("DTSTAMP:" + stamp);
        if (t.created.isValid())
            line("CREATED:" + icsUtc(t.created));
        if (t.modified.isValid())
            line("LAST-MODIFIED:" + icsUtc(t.modified));
        if (t.startDate.isValid())
            line(t.startTime >= 0 ? "DTSTART:" + icsLocal(t.startDate, t.startTime)
                                  : "DTSTART;VALUE=DATE:" + icsDate(t.startDate));
        if (t.dueDate.isValid())
            line(t.dueTime >= 0 ? "DUE:" + icsLocal(t.dueDate, t.dueTime) : "DUE;VALUE=DATE:" + icsDate(t.dueDate));
        switch (t.priority) {
        case Priority::High: line("PRIORITY:1"); break;
        case Priority::Medium: line("PRIORITY:5"); break;
        case Priority::Low: line("PRIORITY:9"); break;
        default: break;
        }
        if (t.completed) {
            line("STATUS:COMPLETED");
            if (t.completedAt.isValid())
                line("COMPLETED:" + icsUtc(t.completedAt));
        } else {
            line("STATUS:NEEDS-ACTION");
        }
        QStringList cats;
        for (qint64 tid : t.tagIds)
            if (store.tags().contains(tid))
                cats << escapeText(store.tag(tid).name);
        if (!cats.isEmpty())
            line("CATEGORIES:" + cats.join(','));
        if (store.lists().contains(t.listId))
            line("X-FAIRFAX-LIST:" + escapeText(store.list(t.listId).name));
        line(QString("X-FAIRFAX-TYPE:") + (t.type == TaskType::Project ? "project" : t.type == TaskType::Checklist ? "checklist" : "task"));
        if (t.starred)
            line("X-FAIRFAX-STARRED:1");
        if (t.duration > 0)
            line(QStringLiteral("X-FAIRFAX-DURATION:%1").arg(t.duration));
        if (t.parentId && included.contains(t.parentId) && store.tasks().contains(t.parentId))
            line("RELATED-TO;RELTYPE=PARENT:" + store.task(t.parentId).uid);
        if (t.recurrence.isSet()) {
            line("RRULE:" + rruleText(t.recurrence));
            if (t.recurrence.from == Repeat::FromCompletion)
                line("X-FAIRFAX-REPEAT-FROM:COMPLETION");
        }
        for (const Alarm &a : store.alarmsFor(t.id)) {
            if (a.deleted)
                continue;
            line("BEGIN:VALARM");
            line("ACTION:DISPLAY");
            line("DESCRIPTION:" + escapeText(t.title));
            if (a.kind == Alarm::Absolute && a.at.isValid())
                line("TRIGGER;VALUE=DATE-TIME:" + icsUtc(a.at));
            else if (a.kind == Alarm::DaysBefore)  // Counted back from midnight of the date.
                line("TRIGGER:" + triggerText(a.offset * 24 * 60 - (a.at.time().hour() * 60 + a.at.time().minute())));
            else
                line("TRIGGER:" + triggerText(a.offset));
            line("END:VALARM");
        }
        line("END:VTODO");
    }
    line("END:VCALENDAR");
    return writeFile(path, out, error);
}

QList<qint64> selectTasks(const Store &store, const QSet<qint64> &listIds, Scope scope) {
    QList<qint64> ids;
    for (const Task &t : store.tasks()) {
        if (t.deleted || !listIds.contains(t.listId))
            continue;
        if ((scope == Scope::NotDone && t.completed) || (scope == Scope::Done && !t.completed))
            continue;
        ids << t.id;
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

QByteArray eventIcs(const Store &store, const Task &t) {
    const bool useDue = t.dueDate.isValid();
    const QDate date = useDue ? t.dueDate : t.startDate;
    if (!date.isValid())
        return {};
    const int minutes = useDue ? t.dueTime : t.startTime;
    QByteArray out;
    auto line = [&](const QString &l) { appendFolded(out, l); };
    line("BEGIN:VCALENDAR");
    line("VERSION:2.0");
    line("PRODID:-//Fairfax//EN");
    line("CALSCALE:GREGORIAN");
    line("METHOD:PUBLISH");
    line("BEGIN:VEVENT");
    line("UID:event-" + (t.uid.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : t.uid));
    line("DTSTAMP:" + icsUtc(QDateTime::currentDateTime()));
    line("SUMMARY:" + escapeText(t.title));
    if (minutes >= 0) {
        const int length = t.duration > 0 ? t.duration : 60;
        const QDateTime end = QDateTime(date, QTime(minutes / 60, minutes % 60)).addSecs(length * 60);
        line("DTSTART:" + icsLocal(date, minutes));
        line("DTEND:" + icsLocal(end.date(), end.time().hour() * 60 + end.time().minute()));
    } else {
        // An all-day event's end date is exclusive.
        line("DTSTART;VALUE=DATE:" + icsDate(date));
        line("DTEND;VALUE=DATE:" + icsDate(date.addDays(1)));
    }
    QStringList body;
    if (!t.notes.isEmpty())
        body << t.notes;
    if (t.actionType == ActionType::Url && !t.actionValue.isEmpty())
        body << t.actionValue;
    if (!body.isEmpty())
        line("DESCRIPTION:" + escapeText(body.join("\n\n")));
    if (t.recurrence.isSet())
        line("RRULE:" + rruleText(t.recurrence));
    // Alerts count back from a moment, so an all-day event has none to offer.
    for (const Alarm &a : store.alarmsFor(t.id)) {
        if (minutes < 0 || a.deleted || a.kind == Alarm::Absolute)
            continue;
        line("BEGIN:VALARM");
        line("ACTION:DISPLAY");
        line("DESCRIPTION:" + escapeText(t.title));
        line("TRIGGER:" + triggerText(a.offset));
        line("END:VALARM");
    }
    line("END:VEVENT");
    line("END:VCALENDAR");
    return out;
}

ImportResult importIcs(Store &store, const QString &path, qint64 fallbackListId) {
    ImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    const QList<Component> todos = parseTodos(QString::fromUtf8(file.readAll()));
    if (todos.isEmpty()) {
        result.error = QStringLiteral("The selected calendar file does not contain any tasks.");
        return result;
    }

    store.begin(QStringLiteral("Import Calendar"));
    Resolver res(store);

    qint64 fallback = fallbackListId;
    if (!store.lists().contains(fallback) || store.list(fallback).deleted)
        fallback = res.listNamed(QStringLiteral("Imported"));

    struct Pending {
        Task task;
        QString uid, parentUid, listName;
        QList<Alarm> alarms;
    };
    QList<Pending> pending;
    QHash<QString, int> byUid;

    for (const Component &c : todos) {
        Pending p;
        Task &t = p.task;
        t.title = unescapeText(c.first("SUMMARY"));
        t.notes = unescapeText(c.first("DESCRIPTION"));
        p.uid = c.first("UID").trimmed();

        if (const Prop *s = c.find("DTSTART")) {
            const IcsDate d = parseIcsDate(s->value, s->params.value("VALUE").toUpper());
            t.startDate = d.date;
            t.startTime = d.minutes;
        }
        if (const Prop *s = c.find("DUE")) {
            const IcsDate d = parseIcsDate(s->value, s->params.value("VALUE").toUpper());
            t.dueDate = d.date;
            t.dueTime = d.minutes;
        }
        qint64 seconds = 0;
        const QString durationText = c.first("X-FAIRFAX-DURATION");
        if (!durationText.isEmpty())
            t.duration = qMax(0, durationText.trimmed().toInt());
        else if (parseDurationSeconds(c.first("DURATION"), &seconds) && seconds > 0)
            t.duration = int(seconds / 60);

        const int prio = c.first("PRIORITY").trimmed().toInt();
        t.priority = prio >= 1 && prio <= 4 ? Priority::High : prio == 5 ? Priority::Medium : prio >= 6 && prio <= 9 ? Priority::Low : Priority::None;

        const QString status = c.first("STATUS").trimmed().toUpper();
        t.completed = status == "COMPLETED" || status == "CANCELLED" || c.first("PERCENT-COMPLETE").trimmed() == "100" ||
                      c.find("COMPLETED") != nullptr;
        if (t.completed) {
            t.completedAt = parseIcsStamp(c.first("COMPLETED"));
            if (!t.completedAt.isValid())
                t.completedAt = QDateTime::currentDateTime();
        }
        t.created = parseIcsStamp(c.first("CREATED"));

        for (const Prop &prop : c.props) {
            if (prop.name != "CATEGORIES")
                continue;
            for (const QString &raw : splitUnescaped(prop.value, ',')) {
                const QString name = unescapeText(raw).trimmed();
                if (name.isEmpty())
                    continue;
                const qint64 id = res.tagNamed(name);
                if (!t.tagIds.contains(id))
                    t.tagIds << id;
            }
        }
        const QString type = c.first("X-FAIRFAX-TYPE").trimmed().toLower();
        t.type = type == "project" ? TaskType::Project : type == "checklist" ? TaskType::Checklist : TaskType::Task;
        t.starred = truthy(c.first("X-FAIRFAX-STARRED"));
        p.listName = unescapeText(c.first("X-FAIRFAX-LIST")).trimmed();

        for (const Prop &prop : c.props) {
            if (prop.name == "RELATED-TO" && p.parentUid.isEmpty()) {
                const QString rel = prop.params.value("RELTYPE", "PARENT").toUpper();
                if (rel == "PARENT")
                    p.parentUid = prop.value.trimmed();
            }
        }
        if (const Prop *r = c.find("RRULE")) {
            t.recurrence = parseRrule(r->value);
            if (t.recurrence.isSet() && c.first("X-FAIRFAX-REPEAT-FROM").trimmed().toUpper() == "COMPLETION")
                t.recurrence.from = Repeat::FromCompletion;
        }

        for (const Component &va : c.children) {
            if (va.kind != "VALARM")
                continue;
            const Prop *trig = va.find("TRIGGER");
            if (!trig)
                continue;
            Alarm a;
            if (trig->params.value("VALUE").toUpper() == "DATE-TIME") {
                a.kind = Alarm::Absolute;
                a.at = parseIcsStamp(trig->value);
                if (!a.at.isValid())
                    continue;
            } else {
                qint64 secs = 0;
                if (!parseDurationSeconds(trig->value, &secs))
                    continue;
                a.kind = Alarm::Relative;
                a.offset = qMax(0, int(qRound(-secs / 60.0)));
            }
            p.alarms << a;
        }
        if (!p.uid.isEmpty())
            byUid.insert(p.uid, int(pending.size()));
        pending.append(p);
    }

    // Assign ids, rebuild the hierarchy now every task is known, then lists.
    for (Pending &p : pending) {
        p.task.id = store.nextId("tasks");
        p.task.order = p.task.id;
    }
    for (Pending &p : pending) {
        const int parentIdx = byUid.value(p.parentUid, -1);
        if (parentIdx >= 0 && &pending[parentIdx] != &p)
            p.task.parentId = pending[parentIdx].task.id;
    }
    {
        QList<Task> all;
        for (const Pending &p : pending)
            all << p.task;
        breakCycles(all);
        for (int i = 0; i < pending.size(); ++i)
            pending[i].task.parentId = all[i].parentId;
    }
    QHash<qint64, int> indexById;
    for (int i = 0; i < pending.size(); ++i)
        indexById.insert(pending[i].task.id, i);
    QHash<qint64, qint64> resolvedList;
    std::function<qint64(int)> listFor = [&](int i) -> qint64 {
        Pending &p = pending[i];
        if (resolvedList.contains(p.task.id))
            return resolvedList.value(p.task.id);
        qint64 id;
        if (!p.listName.isEmpty())
            id = res.listNamed(p.listName);
        else if (p.task.parentId && indexById.contains(p.task.parentId))
            id = listFor(indexById.value(p.task.parentId));
        else
            id = fallback;
        resolvedList.insert(p.task.id, id);
        return id;
    };
    for (int i = 0; i < pending.size(); ++i) {
        pending[i].task.listId = listFor(i);
        store.putTask(pending[i].task);
        for (Alarm a : pending[i].alarms) {
            a.id = store.nextId("alarms");
            a.taskId = pending[i].task.id;
            store.putAlarm(a);
            ++result.alarms;
        }
    }
    result.tasks = int(pending.size());
    result.lists = res.listsCreated;
    result.tags = res.tagsCreated;
    if (!store.commit())
        result.error = QStringLiteral("Could not save the imported tasks.");
    return result;
}

ImportResult importTwoDo(Store &store, const QString &path) {
    ImportResult result;
    if (!QFile::exists(path)) {
        result.error = QStringLiteral("The file does not exist.");
        return result;
    }
    const QString name = QStringLiteral("fairfax-taskdb-import-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!db.open()) {
            result.error = QStringLiteral("Could not open the database: %1").arg(db.lastError().text());
        } else {
            // A non-SQLite file opens fine and fails on first use.
            QSqlQuery probe(db);
            if (!probe.exec("SELECT count(*) FROM sqlite_master"))
                result.error = QStringLiteral("This is not a the reference app database: %1").arg(probe.lastError().text());
            else
                result = importTwoDoImpl(store, db);
            probe.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

}  // namespace Exchange

// ------------------------------------------------------------- TickTick

namespace {

// RFC 4180 rows, with quoted fields that may span lines.
QList<QStringList> parseCsv(const QString &text) {
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text.at(i + 1) == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            row << field;
            field.clear();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text.at(i + 1) == '\n')
                ++i;
            row << field;
            field.clear();
            rows << row;
            row.clear();
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows << row;
    }
    return rows;
}

// TickTick writes instants in UTC ("2025-07-01T07:00:00+0000"); an all-day
// task is local midnight in its own time zone, so the day must be read there.
QDateTime tickTickInstant(const QString &text) {
    if (text.isEmpty())
        return {};
    QDateTime t = QDateTime::fromString(text.left(19), "yyyy-MM-ddTHH:mm:ss");
    if (!t.isValid())
        return {};
    t.setTimeZone(QTimeZone::utc());
    return t;
}

// "PT0S", "-P0DT15H0M0S", "-PT30M": the reminder's offset from the task's
// time, in minutes before it (positive) or after (negative).
int tickTickReminderMinutesBefore(const QString &trigger, bool *ok) {
    static const QRegularExpression re(R"(^(-)?P(?:(\d+)W)?(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?$)");
    const auto m = re.match(trigger.trimmed());
    *ok = m.hasMatch();
    if (!*ok)
        return 0;
    const int minutes = m.captured(2).toInt() * 7 * 1440 + m.captured(3).toInt() * 1440 + m.captured(4).toInt() * 60 +
                        m.captured(5).toInt() + m.captured(6).toInt() / 60;
    return m.captured(1) == "-" ? minutes : -minutes;
}

}  // namespace

namespace Exchange {

bool isTickTickBackup(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString head = QString::fromUtf8(f.read(4096));
    return head.contains("\"Folder Name\",\"List Name\",\"Title\"");
}

ImportResult importTickTick(Store &store, const QString &path) {
    ImportResult result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Can't read %1.").arg(path);
        return result;
    }
    QString text = QString::fromUtf8(f.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    const int headerAt = text.indexOf("\"Folder Name\",\"List Name\"");
    if (headerAt < 0) {
        result.error = "This doesn't look like a TickTick backup (no task header row).";
        return result;
    }
    const QList<QStringList> rows = parseCsv(text.mid(headerAt));
    if (rows.size() < 2) {
        result.error = "The TickTick backup has no tasks.";
        return result;
    }
    const QStringList header = rows.first();
    auto col = [&](const QStringList &row, const char *name) {
        const int i = header.indexOf(QString::fromLatin1(name));
        return i >= 0 && i < row.size() ? row.at(i) : QString();
    };

    // Rows without a time zone are read in the zone most of the file uses.
    QHash<QString, int> zoneCounts;
    for (int i = 1; i < rows.size(); ++i)
        if (!col(rows.at(i), "Timezone").isEmpty())
            zoneCounts[col(rows.at(i), "Timezone")]++;
    QString commonZone;
    for (auto it = zoneCounts.cbegin(); it != zoneCounts.cend(); ++it)
        if (commonZone.isEmpty() || it.value() > zoneCounts.value(commonZone))
            commonZone = it.key();

    QHash<QString, qint64> existingByUid;
    for (const auto &t : store.tasks())
        existingByUid.insert(t.uid, t.id);

    store.begin("Import from TickTick");

    // TickTick's sections ("Column Name", ordered by "Column Order") become
    // sections of the list; a list that gains sections sorts by them.
    QHash<QString, qint64> sectionIds;
    auto sectionFor = [&](qint64 listId, const QString &rawName, double order) -> qint64 {
        const QString name = rawName.trimmed();
        if (name.isEmpty() || !listId)
            return 0;
        const QString key = QString::number(listId) + "\n" + name.toLower();
        if (sectionIds.contains(key))
            return sectionIds.value(key);
        qint64 id = 0;
        for (const auto &sec : store.sectionsOf(listId))
            if (sec.name.compare(name, Qt::CaseInsensitive) == 0)
                id = sec.id;
        if (!id) {
            ListSection sec;
            sec.id = store.nextId("sections");
            sec.listId = listId;
            sec.name = name;
            sec.order = order;
            store.putSection(sec);
            id = sec.id;
            TaskList list = store.list(listId);
            if (list.sortField != SortField::Section) {
                list.sortField = SortField::Section;
                list.separated = true;
                store.putList(list);
            }
        }
        sectionIds.insert(key, id);
        return id;
    };

    auto groupFor = [&](const QString &name) -> qint64 {
        if (name.trimmed().isEmpty())
            return 0;
        for (const auto &g : store.groups())
            if (!g.deleted && g.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
                return g.id;
        ListGroup g;
        g.id = store.nextId("list_groups");
        g.name = name.trimmed();
        g.order = g.id;
        store.putGroup(g);
        return g.id;
    };
    QHash<QString, qint64> listIds;
    auto listFor = [&](const QString &folder, const QString &rawName) -> qint64 {
        const QString name = rawName.trimmed().isEmpty() ? QStringLiteral("Inbox") : rawName.trimmed();
        const QString key = folder + "\n" + name.toLower();
        if (listIds.contains(key))
            return listIds.value(key);
        qint64 id = 0;
        for (const auto &l : store.lists()) {
            if (l.deleted || l.kind == ListKind::Smart)
                continue;
            const bool inboxMatch = name.compare("Inbox", Qt::CaseInsensitive) == 0 && l.kind == ListKind::Inbox;
            if (inboxMatch || l.name.compare(name, Qt::CaseInsensitive) == 0) {
                id = l.id;
                break;
            }
        }
        if (!id) {
            TaskList l;
            l.id = store.nextId("lists");
            l.name = name;
            l.groupId = groupFor(folder);
            l.color = int(store.lists().size()) % 13;
            l.order = l.id;
            store.putList(l);
            id = l.id;
            ++result.lists;
        }
        listIds.insert(key, id);
        return id;
    };
    QHash<QString, qint64> tagIds;
    auto tagFor = [&](const QString &raw) -> qint64 {
        const QString name = QString(raw).remove(QRegularExpression("^#")).trimmed();
        if (name.isEmpty())
            return 0;
        if (tagIds.contains(name.toLower()))
            return tagIds.value(name.toLower());
        qint64 id = 0;
        for (const auto &t : store.tags())
            if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0)
                id = t.id;
        if (!id) {
            Tag t;
            t.id = store.nextId("tags");
            t.name = name;
            store.putTag(t);
            id = t.id;
            ++result.tags;
        }
        tagIds.insert(name.toLower(), id);
        return id;
    };

    QHash<QString, qint64> byTickTickId;
    QHash<qint64, QString> parentOf;
    for (int i = 1; i < rows.size(); ++i) {
        const QStringList &row = rows.at(i);
        const QString title = col(row, "Title");
        const QString tickId = col(row, "taskId");
        if (title.trimmed().isEmpty() && col(row, "Content").trimmed().isEmpty())
            continue;
        const QString uid = "TICKTICK-" + (tickId.isEmpty() ? QString::number(i) : tickId);
        if (existingByUid.contains(uid)) {
            // Imported before. Fill in a section it didn't get then (older
            // imports didn't know sections), but change nothing else.
            Task earlier = store.task(existingByUid.value(uid));
            if (earlier.id && !earlier.sectionId && !col(row, "Column Name").trimmed().isEmpty()) {
                earlier.sectionId = sectionFor(earlier.listId, col(row, "Column Name"), col(row, "Column Order").toDouble());
                store.putTask(earlier);
                ++result.updated;
            }
            continue;
        }

        Task t;
        t.id = store.nextId("tasks");
        t.uid = uid;
        t.listId = listFor(col(row, "Folder Name"), col(row, "List Name"));
        t.sectionId = sectionFor(t.listId, col(row, "Column Name"), col(row, "Column Order").toDouble());
        t.title = title.trimmed().isEmpty() ? QStringLiteral("Untitled") : title.trimmed().left(1000);
        const QString status = col(row, "Status").trimmed();

        const QTimeZone zone(col(row, "Timezone").isEmpty() ? commonZone.toUtf8() : col(row, "Timezone").toUtf8());
        const bool allDay = col(row, "Is All Day") != "false";
        auto local = [&](const QString &text) {
            const QDateTime utc = tickTickInstant(text);
            if (!utc.isValid())
                return QDateTime();
            return zone.isValid() ? utc.toTimeZone(zone) : utc.toLocalTime();
        };
        const QDateTime due = local(col(row, "Due Date"));
        const QDateTime start = local(col(row, "Start Date"));
        if (due.isValid()) {
            t.dueDate = due.date();
            if (!allDay)
                t.dueTime = due.time().hour() * 60 + due.time().minute();
        }
        // TickTick gives single-date tasks the same start and due; only a
        // real span becomes a start date.
        if (start.isValid() && (!due.isValid() || start < due)) {
            t.startDate = start.date();
            if (!allDay && start.date() != due.date())
                t.startTime = start.time().hour() * 60 + start.time().minute();
            if (!due.isValid()) {
                t.startDate = start.date();
                if (!allDay)
                    t.startTime = start.time().hour() * 60 + start.time().minute();
            }
        }
        if (!t.dueDate.isValid() && start.isValid() && !t.startDate.isValid()) {
            t.dueDate = start.date();
            if (!allDay)
                t.dueTime = start.time().hour() * 60 + start.time().minute();
        }

        // TickTick priorities: 0 none, 1 low, 3 medium, 5 high.
        const int priority = col(row, "Priority").toInt();
        t.priority = priority >= 5 ? Priority::High : priority >= 3 ? Priority::Medium : priority >= 1 ? Priority::Low : Priority::None;

        const QString repeat = col(row, "Repeat").trimmed();
        if (!repeat.isEmpty()) {
            t.recurrence = parseRrule(repeat.startsWith("RRULE:") ? repeat.mid(6) : repeat);
            if (t.recurrence.isSet() && !t.dueDate.isValid() && !t.startDate.isValid())
                t.dueDate = QDate::currentDate();
        }

        const QDateTime created = tickTickInstant(col(row, "Created Time"));
        if (created.isValid())
            t.created = created.toLocalTime();
        if (status == "2" || status == "-1") {
            t.completed = true;
            const QDateTime done = tickTickInstant(col(row, "Completed Time"));
            t.completedAt = done.isValid() ? done.toLocalTime() : (created.isValid() ? created.toLocalTime() : QDateTime::currentDateTime());
            if (status == "-1")
                t.tagIds.append(tagFor("abandoned"));  // TickTick's "Won't Do".
        }
        for (const auto &name : col(row, "Tags").split(QRegularExpression("[,，]"), Qt::SkipEmptyParts))
            if (const qint64 id = tagFor(name); id && !t.tagIds.contains(id))
                t.tagIds.append(id);
        t.order = col(row, "Order").toDouble();

        // A checklist's items are packed into its content: "▫" open, "▪" done.
        const QString content = col(row, "Content");
        QList<QPair<QString, bool>> items;
        if (col(row, "Kind") == "CHECKLIST" || col(row, "Is Check list") == "Y") {
            t.type = TaskType::Checklist;
            QString notes;
            QString current;
            int state = -1;  // -1 before the first marker (description text).
            for (const QChar c : content) {
                if (c == QChar(0x25AB) || c == QChar(0x25AA)) {
                    if (state >= 0 && !current.trimmed().isEmpty())
                        items.append({current.trimmed(), state == 1});
                    else if (state < 0)
                        notes = current.trimmed();
                    current.clear();
                    state = c == QChar(0x25AA) ? 1 : 0;
                } else {
                    current += c;
                }
            }
            if (state >= 0 && !current.trimmed().isEmpty())
                items.append({current.trimmed(), state == 1});
            else if (state < 0)
                notes = current.trimmed();
            t.notes = notes;
        } else {
            t.notes = content;
        }

        // Reminders: relative to a timed task's due time; for an all-day
        // task, relative to the start of its day, so they become fixed times.
        for (const auto &trigger : col(row, "Reminder").split(',', Qt::SkipEmptyParts)) {
            bool ok = false;
            const int before = tickTickReminderMinutesBefore(trigger, &ok);
            if (!ok || !t.dueDate.isValid() || t.completed)
                continue;
            Alarm a;
            a.id = store.nextId("alarms");
            a.taskId = t.id;
            if (allDay) {
                a.kind = Alarm::Absolute;
                a.at = QDateTime(t.dueDate, QTime(0, 0)).addSecs(-qint64(before) * 60);
            } else {
                a.offset = before;
            }
            store.putAlarm(a);
            ++result.alarms;
        }

        store.putTask(t);
        ++result.tasks;
        if (!tickId.isEmpty())
            byTickTickId.insert(tickId, t.id);
        if (!col(row, "parentId").isEmpty())
            parentOf.insert(t.id, col(row, "parentId"));

        int order = 0;
        for (const auto &[itemTitle, done] : items) {
            Task item;
            item.id = store.nextId("tasks");
            item.listId = t.listId;
            item.parentId = t.id;
            item.title = itemTitle.left(1000);
            item.completed = done || t.completed;
            if (item.completed)
                item.completedAt = t.completedAt.isValid() ? t.completedAt : QDateTime::currentDateTime();
            item.order = ++order;
            store.putTask(item);
        }
    }

    // Sub-tasks point at their parent by TickTick id; link them once every
    // task exists. A parent with sub-tasks becomes a project.
    for (auto it = parentOf.cbegin(); it != parentOf.cend(); ++it) {
        const qint64 parentId = byTickTickId.value(it.value(), 0);
        if (!parentId || parentId == it.key())
            continue;
        Task child = store.task(it.key());
        Task parent = store.task(parentId);
        child.parentId = parentId;
        child.listId = parent.listId;
        store.putTask(child);
        if (parent.type == TaskType::Task) {
            parent.type = TaskType::Project;
            store.putTask(parent);
        }
    }
    store.commit();
    return result;
}

}  // namespace Exchange


// ------------------------------------------------------------------ Things and OmniFocus

namespace {

// Finds or creates the lists, groups and tags an import refers to by name,
// so the Things and OmniFocus importers (which both work from names) share
// the bookkeeping. Everything runs inside the importer's one change.
class NameResolver {
public:
    NameResolver(Store &store, ImportResult &result) : m_store(store), m_result(result) {}

    qint64 group(const QString &raw) {
        const QString name = raw.trimmed();
        if (name.isEmpty())
            return 0;
        if (m_groups.contains(name.toLower()))
            return m_groups.value(name.toLower());
        qint64 id = 0;
        for (const auto &g : m_store.groups())
            if (!g.deleted && g.name.compare(name, Qt::CaseInsensitive) == 0)
                id = g.id;
        if (!id) {
            ListGroup g;
            g.id = m_store.nextId("list_groups");
            g.name = name;
            g.order = g.id;
            m_store.putGroup(g);
            id = g.id;
        }
        m_groups.insert(name.toLower(), id);
        return id;
    }

    // An empty name means the Inbox, or a list called Inbox when it is off.
    qint64 list(const QString &raw, qint64 groupId = 0) {
        const QString name = raw.trimmed().isEmpty() ? QStringLiteral("Inbox") : raw.trimmed();
        if (m_lists.contains(name.toLower()))
            return m_lists.value(name.toLower());
        qint64 id = 0;
        for (const auto &l : m_store.lists()) {
            if (l.deleted || l.kind == ListKind::Smart)
                continue;
            const bool inbox = name.compare("Inbox", Qt::CaseInsensitive) == 0 && l.kind == ListKind::Inbox;
            if (inbox || l.name.compare(name, Qt::CaseInsensitive) == 0) {
                id = l.id;
                break;
            }
        }
        if (!id) {
            TaskList l;
            l.id = m_store.nextId("lists");
            l.name = name;
            l.groupId = groupId;
            l.color = int(m_store.lists().size()) % 13;
            l.order = l.id;
            m_store.putList(l);
            id = l.id;
            ++m_result.lists;
        }
        m_lists.insert(name.toLower(), id);
        return id;
    }

    qint64 tag(const QString &raw) {
        const QString name = QString(raw).remove(QRegularExpression("^#")).trimmed();
        if (name.isEmpty())
            return 0;
        if (m_tags.contains(name.toLower()))
            return m_tags.value(name.toLower());
        qint64 id = 0;
        for (const auto &t : m_store.tags())
            if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0)
                id = t.id;
        if (!id) {
            Tag t;
            t.id = m_store.nextId("tags");
            t.name = name;
            t.order = t.id;
            m_store.putTag(t);
            id = t.id;
            ++m_result.tags;
        }
        m_tags.insert(name.toLower(), id);
        return id;
    }

private:
    Store &m_store;
    ImportResult &m_result;
    QHash<QString, qint64> m_groups, m_lists, m_tags;
};

// "2026-07-04", "2026-07-04T10:30:00Z" or "2026-07-04 17:00" → date and minutes.
// A time with a zone suffix is dropped: it would shift the day for no reason.
bool parseLooseDateTime(const QString &raw, QDate *date, int *minutes) {
    static const QRegularExpression re("^(\\d{4}-\\d{2}-\\d{2})(?:[T ](\\d{1,2}):(\\d{2})(?::\\d{2})?\\s*([Zz+-].*)?)?");
    const auto m = re.match(raw.trimmed());
    if (!m.hasMatch())
        return false;
    *date = QDate::fromString(m.captured(1), "yyyy-MM-dd");
    *minutes = -1;
    if (!m.captured(2).isEmpty() && m.captured(4).isEmpty())
        *minutes = m.captured(2).toInt() * 60 + m.captured(3).toInt();
    return date->isValid();
}

}  // namespace

namespace Exchange {

ImportResult importThingsJson(Store &store, const QByteArray &json) {
    ImportResult result;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = "Not valid JSON: " + parseError.errorString();
        return result;
    }
    QJsonArray items;
    if (doc.isArray()) {
        items = doc.array();
    } else {
        const QJsonObject o = doc.object();
        for (const char *key : {"items", "tasks", "todos", "to-dos"})
            if (o[QString::fromLatin1(key)].isArray()) {
                items = o[QString::fromLatin1(key)].toArray();
                break;
            }
    }
    if (items.isEmpty()) {
        result.error = "The Things export has no items.";
        return result;
    }

    QHash<QString, qint64> existingByUid;
    for (const auto &t : store.tasks())
        existingByUid.insert(t.uid, t.id);

    store.begin("Import from Things");
    NameResolver names(store, result);
    int order = 0;

    // Things' own JSON (things:///json) wraps fields in "attributes"; other
    // exporters write them flat. Both are read the same way.
    auto field = [](const QJsonObject &o, std::initializer_list<const char *> keys) {
        const QJsonObject attrs = o["attributes"].toObject();
        for (const char *k : keys) {
            const QString key = QString::fromLatin1(k);
            if (attrs.contains(key)) return attrs[key];
            if (o.contains(key)) return o[key];
        }
        return QJsonValue();
    };
    auto text = [&](const QJsonObject &o, std::initializer_list<const char *> keys) {
        const QJsonValue v = field(o, keys);
        return v.isString() ? v.toString() : QString();
    };

    std::function<void(const QJsonObject &, qint64, qint64)> addItem = [&](const QJsonObject &o, qint64 listId, qint64 parentId) {
        const QString kind = text(o, {"type"}).toLower();
        const QString title = text(o, {"title", "name"}).trimmed();
        if (title.isEmpty() || kind == "heading")
            return;
        const QString idText = text(o, {"uuid", "id"});
        const QString uid = idText.isEmpty() ? QString() : "THINGS-" + idText;
        if (!uid.isEmpty() && existingByUid.contains(uid))
            return;

        // Things files projects and to-dos under an area, which becomes the
        // list; loose to-dos with neither go to the Inbox.
        if (!listId) {
            const QString area = text(o, {"area_title", "area", "area-title"});
            listId = names.list(area);
        }

        Task t;
        t.id = store.nextId("tasks");
        t.uid = uid;
        t.listId = listId;
        t.parentId = parentId;
        t.title = title.left(1000);
        t.notes = text(o, {"notes"});
        t.order = ++order;
        QDate d;
        int minutes = -1;
        // "when" is Things' scheduling date: it hides the task until then,
        // which is what a start date does here. Keywords other than today
        // (someday, anytime) have no date to carry.
        const QString when = text(o, {"when"});
        if (parseLooseDateTime(!when.isEmpty() ? when : text(o, {"start_date", "start-date", "activation_date"}), &d, &minutes))
            t.startDate = d;
        else if (when.compare("today", Qt::CaseInsensitive) == 0 || when.compare("evening", Qt::CaseInsensitive) == 0)
            t.startDate = QDate::currentDate();
        if (parseLooseDateTime(text(o, {"deadline", "due_date", "due-date", "due"}), &d, &minutes)) {
            t.dueDate = d;
            t.dueTime = minutes;
        }
        const QString status = text(o, {"status"}).toLower();
        if (field(o, {"completed"}).toBool() || status == "completed" || status == "canceled" || status == "cancelled") {
            t.completed = true;
            QDate doneDate;
            int doneMinutes = -1;
            const QString doneText = text(o, {"completion_date", "completion-date", "stop_date", "completed_at"});
            t.completedAt = parseLooseDateTime(doneText, &doneDate, &doneMinutes)
                                ? QDateTime(doneDate, QTime(0, 0).addSecs(qMax(0, doneMinutes) * 60))
                                : QDateTime::currentDateTime();
            if (status.startsWith("cancel"))
                t.tagIds.append(names.tag("canceled"));
        }
        const QJsonValue tagValue = field(o, {"tags", "tag_names"});
        QStringList tagNames;
        if (tagValue.isArray())
            for (const auto &v : tagValue.toArray())
                tagNames << (v.isString() ? v.toString() : v.toObject()["title"].toString());
        else
            tagNames = tagValue.toString().split(QRegularExpression("\\s*,\\s*"), Qt::SkipEmptyParts);
        for (const auto &n : tagNames)
            if (const qint64 id = names.tag(n); id && !t.tagIds.contains(id))
                t.tagIds.append(id);

        const QJsonArray checklist = field(o, {"checklist-items", "checklist_items", "checklist"}).toArray();
        const QJsonArray children = field(o, {"items", "children", "todos", "to-dos"}).toArray();
        if (kind == "project" || !children.isEmpty())
            t.type = TaskType::Project;
        else if (!checklist.isEmpty())
            t.type = TaskType::Checklist;
        store.putTask(t);
        ++result.tasks;

        int itemOrder = 0;
        for (const auto &v : checklist) {
            const QJsonObject c = v.toObject();
            const QString itemTitle = (v.isString() ? v.toString() : text(c, {"title", "name"})).trimmed();
            if (itemTitle.isEmpty())
                continue;
            Task item;
            item.id = store.nextId("tasks");
            item.listId = listId;
            item.parentId = t.id;
            item.title = itemTitle.left(1000);
            item.completed = t.completed || field(c, {"completed"}).toBool() || text(c, {"status"}).toLower() == "completed";
            if (item.completed)
                item.completedAt = t.completedAt.isValid() ? t.completedAt : QDateTime::currentDateTime();
            item.order = ++itemOrder;
            store.putTask(item);
        }
        for (const auto &v : children)
            addItem(v.toObject(), listId, t.id);
    };

    for (const auto &v : items)
        addItem(v.toObject(), 0, 0);
    store.commit();
    return result;
}

ImportResult importThings(Store &store, const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ImportResult r;
        r.error = QStringLiteral("Can't read %1.").arg(path);
        return r;
    }
    return importThingsJson(store, f.readAll());
}

bool isThingsExport(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(4096).trimmed();
    return (head.startsWith('[') || head.startsWith('{')) && head.contains("\"title\"");
}

bool isTaskPaper(const QString &path) {
    if (path.endsWith(".taskpaper", Qt::CaseInsensitive))
        return true;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString head = QString::fromUtf8(f.read(4096));
    static const QRegularExpression item("^\\s*- ", QRegularExpression::MultilineOption);
    return item.match(head).hasMatch() && !head.trimmed().startsWith('[') && !head.trimmed().startsWith('{');
}

ImportResult importTaskPaperText(Store &store, const QString &text) {
    ImportResult result;
    struct Line {
        int depth = 0;
        QString body;
    };
    QList<Line> lines;
    for (QString raw : text.split('\n')) {
        if (raw.endsWith('\r'))
            raw.chop(1);
        if (raw.trimmed().isEmpty())
            continue;
        int tabs = 0, spaces = 0, i = 0;
        for (; i < raw.size() && (raw[i] == '\t' || raw[i] == ' '); ++i)
            (raw[i] == '\t' ? tabs : spaces)++;
        lines.append({tabs + spaces / 2, raw.mid(i)});
    }
    if (lines.isEmpty()) {
        result.error = "The file is empty.";
        return result;
    }

    struct Parsed {
        QString title;
        QStringList tags;
        QHash<QString, QString> attrs;
    };
    // "Title @tag @due(2026-07-04 17:00)": the @words after the title are
    // tags, or attributes when they carry a value in parentheses.
    auto parse = [](const QString &s) {
        Parsed p;
        static const QRegularExpression at("(?:^|\\s)@([A-Za-z][\\w-]*)(\\(([^)]*)\\))?");
        int firstAt = -1;
        auto it = at.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            if (firstAt < 0)
                firstAt = int(m.capturedStart());
            if (m.captured(2).isEmpty())
                p.tags << m.captured(1);
            else
                p.attrs.insert(m.captured(1).toLower(), m.captured(3));
        }
        p.title = (firstAt >= 0 ? s.left(firstAt) : s).trimmed();
        return p;
    };
    auto isItem = [](const Line &l) { return l.body.startsWith("- "); };
    auto headingTitle = [&](const Line &l) {
        QString title = parse(l.body).title;
        if (title.endsWith(':'))
            title.chop(1);
        return title.trimmed();
    };
    auto isHeading = [&](const Line &l) { return !isItem(l) && parse(l.body).title.endsWith(':'); };

    QHash<QString, qint64> existingByUid;
    for (const auto &t : store.tasks())
        existingByUid.insert(t.uid, t.id);

    store.begin("Import from OmniFocus");
    NameResolver names(store, result);

    // A heading that holds other headings is a folder (a list group); a
    // heading that holds tasks is a project, which becomes a list. Tasks
    // before any heading are Inbox items.
    QString folder;
    qint64 listId = 0;
    QList<qint64> stack;
    QList<int> stackDepths;
    qint64 lastTask = 0;
    int order = 0;

    for (int i = 0; i < lines.size(); ++i) {
        const Line &line = lines.at(i);
        if (isHeading(line)) {
            stack.clear();
            stackDepths.clear();
            lastTask = 0;
            const bool holdsProjects = i + 1 < lines.size() && isHeading(lines.at(i + 1)) && lines.at(i + 1).depth > line.depth;
            if (holdsProjects) {
                folder = headingTitle(line);
                listId = 0;
            } else {
                if (line.depth == 0)
                    folder.clear();
                listId = names.list(headingTitle(line), names.group(folder));
            }
            continue;
        }
        if (!isItem(line)) {
            if (lastTask) {
                Task t = store.task(lastTask);
                t.notes += (t.notes.isEmpty() ? QString() : QStringLiteral("\n")) + line.body.trimmed();
                store.putTask(t);
            }
            continue;
        }

        const Parsed p = parse(line.body.mid(2));
        if (p.title.isEmpty())
            continue;
        if (!listId)
            listId = names.list(QString());
        while (!stackDepths.isEmpty() && stackDepths.last() >= line.depth) {
            stackDepths.removeLast();
            stack.removeLast();
        }
        const QString idText = p.attrs.value("id");
        const QString uid = idText.isEmpty() ? QString() : "OMNIFOCUS-" + idText;
        if (!uid.isEmpty() && existingByUid.contains(uid)) {
            lastTask = 0;
            continue;
        }
        Task t;
        t.id = store.nextId("tasks");
        t.uid = uid;
        t.listId = listId;
        t.parentId = stack.isEmpty() ? 0 : stack.last();
        t.title = p.title.left(1000);
        t.order = ++order;
        QDate d;
        int minutes = -1;
        if (parseLooseDateTime(p.attrs.value("due"), &d, &minutes)) {
            t.dueDate = d;
            t.dueTime = minutes;
        }
        if (parseLooseDateTime(p.attrs.value("defer"), &d, &minutes)) {
            t.startDate = d;
            t.startTime = minutes;
        }
        if (p.attrs.contains("done") || p.tags.contains("done", Qt::CaseInsensitive)) {
            t.completed = true;
            t.completedAt = parseLooseDateTime(p.attrs.value("done"), &d, &minutes)
                                ? QDateTime(d, QTime(0, 0).addSecs(qMax(0, minutes) * 60))
                                : QDateTime::currentDateTime();
        }
        // @estimate(1h30m), @estimate(45m), @estimate(2h).
        static const QRegularExpression est("^(?:(\\d+)\\s*h)?\\s*(?:(\\d+)\\s*m)?");
        const auto em = est.match(p.attrs.value("estimate").trimmed());
        if (em.hasMatch())
            t.duration = em.captured(1).toInt() * 60 + em.captured(2).toInt();
        QStringList tagNames;
        static const QStringList ignored = {"parallel", "autodone", "done", "dropped"};
        for (const auto &tag : p.tags) {
            if (tag.compare("flagged", Qt::CaseInsensitive) == 0)
                t.starred = true;
            else if (!ignored.contains(tag.toLower()))
                tagNames << tag;
        }
        tagNames << p.attrs.value("tags").split(QRegularExpression("\\s*,\\s*"), Qt::SkipEmptyParts);
        for (const auto &n : tagNames)
            if (const qint64 id = names.tag(n); id && !t.tagIds.contains(id))
                t.tagIds.append(id);
        if (t.parentId) {
            Task parent = store.task(t.parentId);
            if (parent.type == TaskType::Task) {
                parent.type = TaskType::Project;
                store.putTask(parent);
            }
        }
        store.putTask(t);
        ++result.tasks;
        stack.append(t.id);
        stackDepths.append(line.depth);
        lastTask = t.id;
    }
    store.commit();
    if (!result.tasks)
        result.error = "No tasks found; expected an OmniFocus TaskPaper export.";
    return result;
}

ImportResult importOmniFocus(Store &store, const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ImportResult r;
        r.error = QStringLiteral("Can't read %1.").arg(path);
        return r;
    }
    return importTaskPaperText(store, QString::fromUtf8(f.readAll()));
}

}  // namespace Exchange

// ------------------------------------------------------------------ printing, mail and text

namespace {

QString htmlEscape(const QString &s) { return s.toHtmlEscaped().replace('\n', "<br/>"); }

QString dateText(const QDate &d, int minutes) {
    if (!d.isValid())
        return {};
    QString text = QLocale().toString(d, QLocale::ShortFormat);
    if (minutes >= 0)
        text += QStringLiteral(" %1").arg(QTime(0, 0).addSecs(minutes * 60).toString("HH:mm"));
    return text;
}

}  // namespace

namespace Exchange {

// RFC 2047 encoded words (=?charset?B/Q?text?=) in a header value.
QString decodeMimeWords(const QString &raw) {
    static const QRegularExpression word("=\\?([^?]+)\\?([bBqQ])\\?([^?]*)\\?=");
    QString out;
    qsizetype last = 0;
    auto it = word.globalMatch(raw);
    while (it.hasNext()) {
        const auto m = it.next();
        // Whitespace between two encoded words is not part of the text.
        const QString gap = raw.mid(last, m.capturedStart() - last);
        if (!gap.trimmed().isEmpty() || last == 0)
            out += gap;
        QByteArray bytes;
        if (m.captured(2).toLower() == "b") {
            bytes = QByteArray::fromBase64(m.captured(3).toLatin1());
        } else {
            QByteArray q = m.captured(3).toLatin1().replace('_', ' ');
            for (int i = 0; i < q.size(); ++i)
                if (q[i] == '=' && i + 2 < q.size()) {
                    bytes.append(char(q.mid(i + 1, 2).toInt(nullptr, 16)));
                    i += 2;
                } else {
                    bytes.append(q[i]);
                }
        }
        // The language suffix of RFC 2231 ("utf-8*en") is not part of the charset.
        const QByteArray charset = m.captured(1).section('*', 0, 0).toLatin1();
        QStringDecoder decoder(charset.constData());
        out += decoder.isValid() ? QString(decoder.decode(bytes)) : QString::fromUtf8(bytes);
        last = m.capturedEnd();
    }
    return out + raw.mid(last);
}

QString printHtml(const QString &title, const QVector<PrintSection> &sections, bool includeNotes) {
    QString html = "<html><body style=\"font-family: sans-serif; font-size: 10pt;\">";
    html += "<h1 style=\"font-size: 18pt; margin-bottom: 2pt;\">" + htmlEscape(title) + "</h1>";
    html += "<p style=\"color: #666666; margin-top: 0;\">" + htmlEscape(QLocale().toString(QDate::currentDate(), QLocale::LongFormat)) + "</p>";
    for (const auto &section : sections) {
        if (section.items.isEmpty())
            continue;
        if (!section.heading.isEmpty())
            html += "<h2 style=\"font-size: 12pt; margin: 12pt 0 3pt 0; border-bottom: 1px solid #999999;\">" + htmlEscape(section.heading) + "</h2>";
        html += "<table cellspacing=\"0\" cellpadding=\"2\" width=\"100%\">";
        for (const auto &item : section.items) {
            const QString box = item.done ? QStringLiteral("&#9745;") : QStringLiteral("&#9744;");
            QString name = htmlEscape(item.title);
            if (item.done)
                name = "<s style=\"color: #888888;\">" + name + "</s>";
            if (item.starred)
                name += " &#9733;";
            else if (item.priority > 0)
                name += QStringLiteral(" <span style=\"color: #888888;\">%1</span>").arg(QString(item.priority, QChar('!')));
            QString detail;
            if (!item.tags.isEmpty())
                detail += "<span style=\"color: #555555;\">" + htmlEscape(item.tags) + "</span>";
            if (includeNotes && !item.notes.trimmed().isEmpty())
                detail += (detail.isEmpty() ? "" : "<br/>") + QStringLiteral("<span style=\"color: #444444;\">") + htmlEscape(item.notes.trimmed()) + "</span>";
            html += QStringLiteral("<tr><td width=\"%1\"></td><td width=\"16\" valign=\"top\">%2</td><td valign=\"top\">%3%4</td><td align=\"right\" valign=\"top\" width=\"22%\" style=\"color: #555555;\">%5</td></tr>")
                        .arg(item.depth * 18).arg(box).arg(name).arg(detail.isEmpty() ? QString() : "<br/>" + detail).arg(htmlEscape(item.due));
        }
        html += "</table>";
    }
    html += "</body></html>";
    return html;
}

bool writePdf(const QString &path, const QString &title, const QVector<PrintSection> &sections, bool includeNotes, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    QPdfWriter writer(&file);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    writer.setTitle(title);
    writer.setCreator("Fairfax");
    QTextDocument document;
    document.setHtml(printHtml(title, sections, includeNotes));
    document.print(&writer);
    return true;
}

PrintItem printItem(const Store &store, const Task &t, int depth) {
    PrintItem item;
    item.title = t.title;
    item.depth = depth;
    item.done = t.completed;
    item.starred = t.starred;
    item.priority = t.priority;
    item.notes = t.notes;
    item.due = dateText(t.dueDate, t.dueTime);
    QStringList tags;
    for (auto id : t.tagIds) {
        const Tag tag = store.tag(id);
        if (tag.id && !tag.deleted)
            tags << "#" + tag.name;
    }
    item.tags = tags.join(' ');
    return item;
}

QString tasksAsText(const Store &store, const QList<qint64> &taskIds) {
    QString out;
    QSet<qint64> wanted(taskIds.begin(), taskIds.end());
    std::function<void(const Task &, int)> add = [&](const Task &t, int depth) {
        const QString pad(depth * 4, ' ');
        out += pad + (t.completed ? "[x] " : "[ ] ") + t.title;
        const QString due = dateText(t.dueDate, t.dueTime);
        if (!due.isEmpty())
            out += "  (due " + due + ")";
        out += '\n';
        if (!t.notes.trimmed().isEmpty()) {
            for (const auto &line : t.notes.trimmed().split('\n'))
                out += pad + "    " + line + '\n';
        }
    };
    for (auto id : taskIds) {
        const Task t = store.task(id);
        if (!t.id || t.deleted)
            continue;
        // A sub-task whose parent is also listed is shown under the parent.
        if (t.parentId && wanted.contains(t.parentId))
            continue;
        add(t, 0);
        QList<Task> kids;
        for (auto cid : taskIds) {
            const Task c = store.task(cid);
            if (c.id && c.parentId == t.id && !c.deleted)
                kids.append(c);
        }
        for (const auto &c : kids)
            add(c, 1);
    }
    return out;
}

QUrl mailtoForTasks(const Store &store, const QList<qint64> &taskIds) {
    QString subject = QStringLiteral("Tasks");
    if (taskIds.size() == 1 && store.task(taskIds.first()).id)
        subject = store.task(taskIds.first()).title;
    else if (taskIds.size() > 1)
        subject = QStringLiteral("%1 tasks").arg(taskIds.size());
    // QUrl's own query handling leaves characters mail clients read as
    // separators; percent-encode every value by hand.
    const QString body = tasksAsText(store, taskIds);
    return QUrl(QStringLiteral("mailto:?subject=%1&body=%2")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(subject)), QString::fromUtf8(QUrl::toPercentEncoding(body))));
}

Eml parseEml(const QByteArray &bytes) {
    Eml mail;
    const QString text = QString::fromUtf8(bytes);
    const qsizetype end = text.indexOf(QRegularExpression("\\r?\\n\\r?\\n"));
    const QString head = end < 0 ? text : text.left(end);
    // Unfold continuation lines, then read "Name: value" pairs.
    QString unfolded = head;
    unfolded.replace(QRegularExpression("\\r?\\n[ \\t]+"), " ");
    for (const auto &line : unfolded.split(QRegularExpression("\\r?\\n"))) {
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString name = line.left(colon).trimmed().toLower();
        const QString value = line.mid(colon + 1).trimmed();
        if (name == "subject" && mail.subject.isEmpty())
            mail.subject = decodeMimeWords(value);
        else if (name == "from" && mail.from.isEmpty())
            mail.from = decodeMimeWords(value);
        else if (name == "message-id" && mail.messageId.isEmpty()) {
            mail.messageId = value;
            mail.messageId.remove('<').remove('>');
            mail.messageId = mail.messageId.trimmed();
        }
    }
    return mail;
}

}  // namespace Exchange
