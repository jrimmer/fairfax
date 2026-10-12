#include "exchange.h"

#include <QFile>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTimeZone>
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
            l.smartQuery = smart;
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

    // Tasks. Ids are assigned up front so parents resolve by uid.
    QList<Task> tasks;
    QHash<QString, qint64> taskByUid;
    QHash<qint64, QString> parentUidOf;
    if (!rows("tasks", "COALESCE(isdeleted,0)=0", "displayorder", [&](const Row &r) {
            Task t;
            t.id = store.nextId("tasks");
            t.listId = listByUid.contains(r.str("calendaruid")) ? listByUid.value(r.str("calendaruid")) : fallbackList();
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
    line("PRODID:-//Precrime//EN");
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
            line("X-PRECRIME-LIST:" + escapeText(store.list(t.listId).name));
        line(QString("X-PRECRIME-TYPE:") + (t.type == TaskType::Project ? "project" : t.type == TaskType::Checklist ? "checklist" : "task"));
        if (t.starred)
            line("X-PRECRIME-STARRED:1");
        if (t.duration > 0)
            line(QStringLiteral("X-PRECRIME-DURATION:%1").arg(t.duration));
        if (t.parentId && included.contains(t.parentId) && store.tasks().contains(t.parentId))
            line("RELATED-TO;RELTYPE=PARENT:" + store.task(t.parentId).uid);
        if (t.recurrence.isSet()) {
            line("RRULE:" + rruleText(t.recurrence));
            if (t.recurrence.from == Repeat::FromCompletion)
                line("X-PRECRIME-REPEAT-FROM:COMPLETION");
        }
        for (const Alarm &a : store.alarmsFor(t.id)) {
            if (a.deleted)
                continue;
            line("BEGIN:VALARM");
            line("ACTION:DISPLAY");
            line("DESCRIPTION:" + escapeText(t.title));
            if (a.kind == Alarm::Absolute && a.at.isValid())
                line("TRIGGER;VALUE=DATE-TIME:" + icsUtc(a.at));
            else
                line("TRIGGER:" + triggerText(a.offset));
            line("END:VALARM");
        }
        line("END:VTODO");
    }
    line("END:VCALENDAR");
    return writeFile(path, out, error);
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
        const QString durationText = c.first("X-PRECRIME-DURATION");
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
        const QString type = c.first("X-PRECRIME-TYPE").trimmed().toLower();
        t.type = type == "project" ? TaskType::Project : type == "checklist" ? TaskType::Checklist : TaskType::Task;
        t.starred = truthy(c.first("X-PRECRIME-STARRED"));
        p.listName = unescapeText(c.first("X-PRECRIME-LIST")).trimmed();

        for (const Prop &prop : c.props) {
            if (prop.name == "RELATED-TO" && p.parentUid.isEmpty()) {
                const QString rel = prop.params.value("RELTYPE", "PARENT").toUpper();
                if (rel == "PARENT")
                    p.parentUid = prop.value.trimmed();
            }
        }
        if (const Prop *r = c.find("RRULE")) {
            t.recurrence = parseRrule(r->value);
            if (t.recurrence.isSet() && c.first("X-PRECRIME-REPEAT-FROM").trimmed().toUpper() == "COMPLETION")
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
    const QString name = QStringLiteral("precrime-taskdb-import-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
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
