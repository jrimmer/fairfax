#include "automation.h"
#include "alarms.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

#include "dateparse.h"
#include "nlparse.h"
#include "query.h"
#include "recurrence.h"
#include "schedule.h"
#include "views.h"

namespace {

// Commands report problems by throwing this; run() turns it into the error
// response. Every handler validates before it opens a store change, so a
// throw never leaves a half-written change behind.
struct Failure {
    QString code;
    QString message;
};

[[noreturn]] void fail(const QString &code, const QString &message) { throw Failure{code, message}; }
[[noreturn]] void invalid(const QString &message) { fail("invalid_arguments", message); }

const QStringList kPriorityNames = {"none", "low", "medium", "high"};
const QStringList kTypeNames = {"normal", "project", "checklist"};
const QStringList kActionNames = {"none", "call", "message", "mail", "visit", "url", "google"};
const QStringList kFocusNames = {"all", "today", "starred", "scheduled", "done"};

constexpr int kMaxBatch = 100;
constexpr int kMaxTitle = 1000;
constexpr int kMaxSearch = 256;

QString idString(qint64 id) { return QString::number(id); }

QJsonValue dateValue(const QDate &d) { return d.isValid() ? QJsonValue(d.toString("yyyy-MM-dd")) : QJsonValue(); }
QJsonValue timeValue(int minutes) {
    return minutes >= 0 ? QJsonValue(QStringLiteral("%1:%2").arg(minutes / 60, 2, 10, QChar('0')).arg(minutes % 60, 2, 10, QChar('0')))
                        : QJsonValue();
}

QDate parseDate(const QJsonValue &v, const QString &key) {
    const QString s = v.toString();
    const QDate d = QDate::fromString(s, "yyyy-MM-dd");
    if (!v.isString() || s.size() != 10 || !d.isValid())
        invalid(QStringLiteral("%1 must be a date formatted yyyy-MM-dd.").arg(key));
    return d;
}

int parseTime(const QJsonValue &v, const QString &key) {
    static const QRegularExpression re("^([01]\\d|2[0-3]):([0-5]\\d)$");
    const auto m = re.match(v.toString());
    if (!v.isString() || !m.hasMatch())
        invalid(QStringLiteral("%1 must be a time formatted HH:mm.").arg(key));
    return m.captured(1).toInt() * 60 + m.captured(2).toInt();
}

bool parseBool(const QJsonValue &v, const QString &key) {
    if (!v.isBool())
        invalid(QStringLiteral("%1 must be a boolean.").arg(key));
    return v.toBool();
}

QString parseString(const QJsonValue &v, const QString &key) {
    if (!v.isString())
        invalid(QStringLiteral("%1 must be a string.").arg(key));
    return v.toString();
}

int parseInt(const QJsonValue &v, const QString &key, qint64 lo, qint64 hi) {
    if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble()))
        invalid(QStringLiteral("%1 must be an integer.").arg(key));
    const double d = v.toDouble();
    if (d < double(lo) || d > double(hi))
        invalid(QStringLiteral("%1 must be between %2 and %3.").arg(key).arg(lo).arg(hi));
    return int(d);
}

int parseEnum(const QJsonValue &v, const QString &key, const QStringList &names) {
    const int i = v.isString() ? names.indexOf(v.toString()) : -1;
    if (i < 0)
        invalid(QStringLiteral("%1 must be one of: %2.").arg(key, names.join(", ")));
    return i;
}

// ------------------------------------------------------------ schemas

QJsonObject typeSchema(const QString &type, const QString &description) {
    return QJsonObject{{"type", type}, {"description", description}};
}
QJsonObject str(const QString &d) { return typeSchema("string", d); }
QJsonObject integer(const QString &d) { return typeSchema("integer", d); }
QJsonObject flag(const QString &d) { return typeSchema("boolean", d); }
QJsonObject nullableStr(const QString &d) {
    return QJsonObject{{"type", QJsonArray{"string", "null"}}, {"description", d}};
}
QJsonObject enumStr(const QString &d, const QStringList &values) {
    QJsonArray a;
    for (const auto &v : values)
        a.append(v);
    return QJsonObject{{"type", "string"}, {"enum", a}, {"description", d}};
}
QJsonObject stringArray(const QString &d, int maxItems = 0) {
    QJsonObject o{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", d}};
    if (maxItems)
        o["maxItems"] = maxItems;
    return o;
}
QJsonObject idArray(const QString &d) {
    QJsonObject o = stringArray(d, kMaxBatch);
    o["minItems"] = 1;
    return o;
}
QJsonObject actionSchema() {
    return QJsonObject{{"type", "object"},
                       {"description", "The task's action. type none clears it; the others need a value (phone number, address, URL or search text)."},
                       {"properties", QJsonObject{{"type", enumStr("Action type.", kActionNames)}, {"value", str("Action target.")}}},
                       {"required", QJsonArray{"type"}},
                       {"additionalProperties", false}};
}

QJsonObject taskWriteProps(bool create) {
    QJsonObject p{
        {"notes", str("Replaces the notes. An empty string clears them; omit to leave unchanged.")},
        {"task_type", enumStr("normal, project or checklist.", kTypeNames)},
        {"priority", enumStr("Task priority.", kPriorityNames)},
        {"starred", flag("Replaces the starred state.")},
        {"due_date", nullableStr("yyyy-MM-dd. null clears the due date and due time.")},
        {"due_time", nullableStr("HH:mm. Requires a due date. null clears it.")},
        {"start_date", nullableStr("yyyy-MM-dd. null clears the start date and start time.")},
        {"start_time", nullableStr("HH:mm. Requires a start date. null clears it.")},
        {"duration_seconds", integer("Duration in seconds, 0 through 2147483647. Stored in whole minutes, rounded up.")},
        {"action", actionSchema()},
    };
    if (create) {
        p["title"] = str("Task title, up to 1000 characters.");
        p["list_id"] = str("List to create the task in. Omit to use the default destination. Use list_lists to find ids.");
        p["parent_task_id"] = str("Create the task as a sub-task of this project or checklist (a normal task is turned into a project).");
        p["tag_names"] = stringArray("Tag names; missing tags are created.");
        p["subtasks"] = stringArray("Titles of sub-tasks to create inside the new task (1 to 100). A normal task becomes a checklist.", kMaxBatch);
        p["natural_language"] = flag("Parse the title for dates, times, #tags, priority and so on, as Quick Entry does. Explicit fields win.");
    } else {
        p["title"] = str("New title.");
        p["tag_update"] = QJsonObject{
            {"type", "object"},
            {"description", "Changes tags: mode set replaces, append adds, remove removes the named tags."},
            {"properties", QJsonObject{{"mode", enumStr("How to apply tag_names.", {"set", "append", "remove"})},
                                       {"tag_names", stringArray("Tag names; missing tags are created for set and append.")}}},
            {"required", QJsonArray{"mode", "tag_names"}},
            {"additionalProperties", false}};
    }
    return p;
}

// ------------------------------------------------------------ write fields

// The task write fields of create_task and update_task, parsed and checked
// but not yet applied, so a bad argument is refused before any change opens.
struct Fields {
    std::optional<QString> title, notes;
    std::optional<bool> starred;
    std::optional<int> priority, type, duration;
    bool hasDue = false, hasDueTime = false, hasStart = false, hasStartTime = false;
    QDate due, start;
    int dueTime = -1, startTime = -1;
    std::optional<std::pair<int, QString>> action;
    bool hasTags = false;
    QString tagMode = "set";
    QStringList tagNames;
};

QStringList parseNames(const QJsonValue &v, const QString &key) {
    if (!v.isArray())
        invalid(QStringLiteral("%1 must be an array of strings.").arg(key));
    QStringList names;
    for (const auto &n : v.toArray()) {
        if (!n.isString() || n.toString().trimmed().isEmpty())
            invalid(QStringLiteral("%1 must contain only non-empty strings.").arg(key));
        names << n.toString().trimmed();
    }
    return names;
}

Fields parseFields(const QJsonObject &a) {
    Fields f;
    if (a.contains("title")) {
        const QString t = parseString(a["title"], "title").trimmed();
        if (t.isEmpty())
            invalid("title must not be empty.");
        if (t.size() > kMaxTitle)
            invalid(QStringLiteral("title must be at most %1 characters.").arg(kMaxTitle));
        f.title = t;
    }
    if (a.contains("notes"))
        f.notes = parseString(a["notes"], "notes");
    if (a.contains("starred"))
        f.starred = parseBool(a["starred"], "starred");
    if (a.contains("priority"))
        f.priority = parseEnum(a["priority"], "priority", kPriorityNames);
    if (a.contains("task_type"))
        f.type = parseEnum(a["task_type"], "task_type", kTypeNames);
    if (a.contains("duration_seconds")) {
        const double secs = a["duration_seconds"].toDouble(-1);
        if (!a["duration_seconds"].isDouble() || secs != std::floor(secs) || secs < 0 || secs > 2147483647.0)
            invalid("duration_seconds must be an integer from 0 through 2147483647.");
        f.duration = int(qMin<double>(std::ceil(secs / 60.0), 2147483647.0));
    }
    if (a.contains("due_date")) {
        f.hasDue = true;
        if (!a["due_date"].isNull())
            f.due = parseDate(a["due_date"], "due_date");
    }
    if (a.contains("due_time")) {
        f.hasDueTime = true;
        if (!a["due_time"].isNull())
            f.dueTime = parseTime(a["due_time"], "due_time");
    }
    if (a.contains("start_date")) {
        f.hasStart = true;
        if (!a["start_date"].isNull())
            f.start = parseDate(a["start_date"], "start_date");
    }
    if (a.contains("start_time")) {
        f.hasStartTime = true;
        if (!a["start_time"].isNull())
            f.startTime = parseTime(a["start_time"], "start_time");
    }
    if (a.contains("action")) {
        if (!a["action"].isObject())
            invalid("action must be an object like {\"type\": \"call\", \"value\": \"+1 555 0100\"}.");
        const QJsonObject act = a["action"].toObject();
        for (const auto &k : act.keys())
            if (k != "type" && k != "value")
                invalid(QStringLiteral("Unknown action key: %1.").arg(k));
        const int type = parseEnum(act["type"], "action.type", kActionNames);
        QString value;
        if (type != ActionType::None) {
            value = parseString(act["value"], "action.value").trimmed();
            if (value.isEmpty())
                invalid("action.value is required for this action type.");
        }
        f.action = {type, value};
    }
    if (a.contains("tag_names")) {
        f.hasTags = true;
        f.tagNames = parseNames(a["tag_names"], "tag_names");
    }
    if (a.contains("tag_update")) {
        if (!a["tag_update"].isObject())
            invalid("tag_update must be an object.");
        const QJsonObject u = a["tag_update"].toObject();
        for (const auto &k : u.keys())
            if (k != "mode" && k != "tag_names")
                invalid(QStringLiteral("Unknown tag_update key: %1.").arg(k));
        f.hasTags = true;
        f.tagMode = QStringList{"set", "append", "remove"}.value(parseEnum(u["mode"], "tag_update.mode", {"set", "append", "remove"}));
        f.tagNames = parseNames(u["tag_names"], "tag_update.tag_names");
    }
    return f;
}

// Applies the plain fields (everything except tags, which need the store) and
// checks the combinations: a time without a date is meaningless.
void applyFields(Task &t, const Fields &f) {
    if (f.title) t.title = *f.title;
    if (f.notes) t.notes = *f.notes;
    if (f.starred) t.starred = *f.starred;
    if (f.priority) t.priority = *f.priority;
    if (f.type) t.type = *f.type;
    if (f.duration) t.duration = *f.duration;
    if (f.hasDue) {
        t.dueDate = f.due;
        if (!f.due.isValid())
            t.dueTime = -1;
    }
    if (f.hasDueTime) t.dueTime = f.dueTime;
    if (f.hasStart) {
        t.startDate = f.start;
        if (!f.start.isValid())
            t.startTime = -1;
    }
    if (f.hasStartTime) t.startTime = f.startTime;
    if (f.action) {
        t.actionType = f.action->first;
        t.actionValue = f.action->second;
    }
    if (t.dueTime >= 0 && !t.dueDate.isValid())
        invalid("due_time requires a due date.");
    if (t.startTime >= 0 && !t.startDate.isValid())
        invalid("start_time requires a start date.");
}

}  // namespace

// ================================================================ Impl

struct Automation::Impl {
    Impl(Automation &automation) : a(automation), s(automation.m_store) {}

    Automation &a;
    Store &s;
    std::unique_ptr<ViewBuilder> m_views;
    QHash<QString, qint64> newTags;                  // Tags created by the running command.
    QHash<QPair<qint64, qint64>, double> orders;     // Next sibling order handed out per (list, parent).

    using Handler = QJsonObject (Impl::*)(const QJsonObject &);

    // ----- shared state

    QDateTime now() const { return a.m_now.isValid() ? a.m_now : QDateTime::currentDateTime(); }

    ScheduleSettings settings() const { return ScheduleSettings::fromStore(s); }

    ViewBuilder &views() {
        if (!m_views)
            m_views = std::make_unique<ViewBuilder>(s, now(), settings());
        return *m_views;
    }

    void reset() {
        m_views.reset();
        newTags.clear();
        orders.clear();
    }

    // ----- lookups

    static qint64 idFrom(const QJsonValue &v, const QString &key) {
        qint64 id = 0;
        if (v.isString()) {
            bool ok = false;
            id = v.toString().toLongLong(&ok);
            if (!ok) id = 0;
        } else if (v.isDouble() && v.toDouble() == std::floor(v.toDouble())) {
            id = qint64(v.toDouble());
        }
        if (id <= 0)
            invalid(QStringLiteral("%1 must be an id string returned by an earlier command.").arg(key));
        return id;
    }

    // A task is addressed by the id automation hands out or by its uid, the
    // latter so a precrime:// link's uid works anywhere a task_id does.
    Task findTask(const QJsonValue &v, const QString &key = "task_id") {
        if (!v.isString() && !v.isDouble())
            invalid(QStringLiteral("%1 is required.").arg(key));
        const QString text = v.isString() ? v.toString() : QString::number(qint64(v.toDouble()));
        bool numeric = false;
        const qint64 id = text.toLongLong(&numeric);
        if (numeric) {
            const auto it = s.tasks().constFind(id);
            if (it != s.tasks().cend() && !it->deleted)
                return *it;
        } else {
            for (const auto &t : s.tasks())
                if (!t.deleted && t.uid == text)
                    return t;
        }
        fail("not_found", QStringLiteral("No task with %1 \"%2\".").arg(key, text));
    }

    TaskList findList(const QJsonValue &v, const QString &key, bool allowSmart = false) {
        const qint64 id = idFrom(v, key);
        const auto it = s.lists().constFind(id);
        if (it == s.lists().cend() || it->deleted || (!allowSmart && it->kind == ListKind::Smart) ||
            (allowSmart && it->kind != ListKind::Smart))
            fail("not_found", QStringLiteral("No %1 with id %2.").arg(allowSmart ? "Smart List" : "list").arg(id));
        return *it;
    }

    Tag findTag(const QJsonObject &args) {
        const bool hasId = args.contains("tag_id"), hasName = args.contains("tag_name");
        if (hasId == hasName)
            invalid("Provide exactly one of tag_id or tag_name.");
        if (hasId) {
            const qint64 id = idFrom(args["tag_id"], "tag_id");
            const auto it = s.tags().constFind(id);
            if (it == s.tags().cend() || it->deleted)
                fail("not_found", QStringLiteral("No tag with id %1.").arg(id));
            return *it;
        }
        const QString name = parseString(args["tag_name"], "tag_name").trimmed();
        for (const auto &t : s.tags())
            if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0)
                return t;
        fail("not_found", QStringLiteral("No tag named \"%1\".").arg(name));
    }

    // task_id or task_ids, de-duplicated, in the order given.
    QList<Task> findTasks(const QJsonObject &args) {
        const bool one = args.contains("task_id"), many = args.contains("task_ids");
        if (one == many)
            invalid("Provide exactly one of task_id or task_ids.");
        QList<Task> out;
        QSet<qint64> seen;
        auto add = [&](const QJsonValue &v, const QString &key) {
            const Task t = findTask(v, key);
            if (!seen.contains(t.id)) {
                seen.insert(t.id);
                out.append(t);
            }
        };
        if (one) {
            add(args["task_id"], "task_id");
        } else {
            if (!args["task_ids"].isArray())
                invalid("task_ids must be an array of task ids.");
            const QJsonArray ids = args["task_ids"].toArray();
            if (ids.isEmpty() || ids.size() > kMaxBatch)
                invalid(QStringLiteral("task_ids must hold 1 through %1 ids.").arg(kMaxBatch));
            for (const auto &v : ids)
                add(v, "task_ids");
        }
        return out;
    }

    qint64 defaultListId() {
        const qint64 preferred = s.setting("tasks.defaultList", 0).toLongLong();
        if (preferred && s.lists().contains(preferred) && !s.list(preferred).deleted &&
            s.list(preferred).kind != ListKind::Smart)
            return preferred;
        const auto lists = views().orderedLists();
        return lists.isEmpty() ? 0 : lists.first().id;
    }

    QVector<Task> childrenOf(qint64 id) { return views().children(id); }

    // ----- JSON

    QJsonObject listJson(const TaskList &l) {
        QJsonObject o{{"id", idString(l.id)}, {"uid", l.uid}, {"name", l.name}, {"archived", l.archived},
                      {"kind", l.kind == ListKind::Inbox ? "inbox" : l.kind == ListKind::Smart ? "smart" : "normal"}};
        o["group_id"] = l.groupId ? QJsonValue(idString(l.groupId)) : QJsonValue();
        if (l.kind == ListKind::Smart)
            o["smart_query"] = l.smartQuery;
        return o;
    }

    QJsonObject tagJson(const Tag &t) {
        return QJsonObject{{"id", idString(t.id)}, {"uid", t.uid}, {"name", t.name}, {"paused", t.paused},
                           {"group_id", t.groupId ? QJsonValue(idString(t.groupId)) : QJsonValue()}};
    }

    QJsonObject taskJson(const Task &t) {
        const TaskList list = s.list(t.listId);
        QJsonArray tags;
        for (auto id : t.tagIds) {
            const Tag tag = s.tag(id);
            if (tag.id && !tag.deleted)
                tags.append(QJsonObject{{"id", idString(tag.id)}, {"name", tag.name}, {"paused", tag.paused}});
        }
        QJsonObject o;
        o["id"] = idString(t.id);
        o["uid"] = t.uid;
        o["title"] = t.title;
        o["notes"] = t.notes;
        o["list"] = QJsonObject{{"id", idString(t.listId)}, {"name", list.name}};
        o["parent_id"] = t.parentId ? QJsonValue(idString(t.parentId)) : QJsonValue();
        o["type"] = kTypeNames.value(t.type);
        o["priority"] = kPriorityNames.value(t.priority);
        o["starred"] = t.starred;
        o["completed"] = t.completed;
        o["completed_at"] = t.completedAt.isValid() ? QJsonValue(t.completedAt.toString(Qt::ISODate)) : QJsonValue();
        o["start_date"] = dateValue(t.startDate);
        o["start_time"] = timeValue(t.startDate.isValid() ? t.startTime : -1);
        o["due_date"] = dateValue(t.dueDate);
        o["due_time"] = timeValue(t.dueDate.isValid() ? t.dueTime : -1);
        o["duration_seconds"] = t.duration * 60;
        o["repeat"] = t.recurrence.isSet() ? QJsonValue(Recurrences::describe(t.recurrence)) : QJsonValue();
        o["tags"] = tags;
        o["action"] = QJsonObject{{"type", kActionNames.value(t.actionType)}, {"value", t.actionValue}};
        o["overdue"] = !t.completed && Schedule::isOverdue(t, now(), settings());
        o["scheduled"] = !t.completed && Schedule::isScheduled(t, now());
        o["paused"] = views().isPaused(t);
        return o;
    }

    QJsonArray taskArray(const QList<Task> &tasks) {
        QJsonArray a;
        for (const auto &t : tasks)
            a.append(taskJson(t));
        return a;
    }

    static QJsonObject collection(const QString &key, const QJsonArray &items) {
        return QJsonObject{{key, items}, {"item_count", items.size()}};
    }

    // ----- changes

    void commit() {
        if (!s.commit())
            fail("operation_failed", "The database rejected the change.");
        m_views.reset();
    }

    double nextOrder(qint64 listId, qint64 parentId) {
        const auto key = qMakePair(listId, parentId);
        if (!orders.contains(key)) {
            double order = 0;
            for (const auto &t : s.tasks())
                if (t.listId == listId && t.parentId == parentId)
                    order = qMax(order, t.order);
            orders.insert(key, order);
        }
        return ++orders[key];
    }

    // Existing tags are matched by name, ignoring case; missing ones are
    // created inside the running change.
    qint64 tagIdForName(const QString &raw) {
        QString name = raw.trimmed();
        if (name.startsWith('#'))
            name.remove(0, 1);
        if (name.isEmpty())
            return 0;
        for (const auto &t : s.tags())
            if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0)
                return t.id;
        const QString lower = name.toLower();
        if (newTags.contains(lower))
            return newTags.value(lower);
        Tag tag;
        tag.id = s.nextId("tags");
        tag.name = name;
        tag.order = tag.id;
        s.putTag(tag);
        newTags.insert(lower, tag.id);
        return tag.id;
    }

    void applyTags(Task &t, const Fields &f) {
        if (!f.hasTags)
            return;
        QList<qint64> ids;
        for (const auto &n : f.tagNames)
            if (const qint64 id = tagIdForName(n))
                ids.append(id);
        if (f.tagMode == "set") {
            t.tagIds.clear();
        } else if (f.tagMode == "remove") {
            for (auto id : ids)
                t.tagIds.removeAll(id);
            return;
        }
        for (auto id : ids)
            if (!t.tagIds.contains(id))
                t.tagIds.append(id);
    }

    // Mirrors Backend::completeTask so the app and automation agree: a
    // repeating task leaves a completed copy behind and moves on, and
    // completing a project or checklist completes what is inside it.
    void completeTree(Task t, bool done, const QDateTime &when) {
        if (t.completed == done)
            return;
        if (done && t.recurrence.isSet()) {
            if (auto next = Recurrences::advance(t, when.date())) {
                Task copy = t;
                copy.id = s.nextId("tasks");
                copy.uid.clear();
                copy.recurrence = {};
                copy.completed = true;
                copy.completedAt = when;
                if (copy.seriesUid.isEmpty())
                    copy.seriesUid = t.uid;
                s.putTask(copy);
                next->seriesUid = copy.seriesUid;
                s.putTask(*next);
                return;
            }
        }
        t.completed = done;
        t.completedAt = done ? when : QDateTime();
        s.putTask(t);
        if (done && t.type != TaskType::Task)
            for (const auto &c : childrenOf(t.id))
                if (!c.completed)
                    completeTree(c, true, when);
    }

    void deleteTree(qint64 id, QSet<qint64> &done) {
        if (done.contains(id))
            return;
        done.insert(id);
        Task t = s.task(id);
        for (const auto &c : childrenOf(id))
            deleteTree(c.id, done);
        t.deleted = true;
        s.putTask(t);
    }

    qint64 duplicateTree(const Task &source, qint64 parentId, qint64 listId) {
        Task copy = source;
        copy.id = s.nextId("tasks");
        copy.uid.clear();
        copy.created = {};
        copy.parentId = parentId;
        copy.listId = listId;
        copy.order = source.order + 0.5;
        s.putTask(copy);
        for (const auto &c : childrenOf(source.id))
            if (c.id != copy.id)
                duplicateTree(c, copy.id, listId);
        return copy.id;
    }

    // ----- discovery

    QJsonObject listLists(const QJsonObject &args) {
        const bool archived = args.contains("include_archived") && parseBool(args["include_archived"], "include_archived");
        QJsonArray out;
        for (const auto &l : views().orderedLists())
            out.append(listJson(l));
        if (archived) {
            QList<TaskList> extra;
            for (const auto &l : s.lists())
                if (!l.deleted && l.archived && l.kind != ListKind::Smart)
                    extra.append(l);
            std::sort(extra.begin(), extra.end(), [](const TaskList &x, const TaskList &y) { return x.id < y.id; });
            for (const auto &l : extra)
                out.append(listJson(l));
        }
        return collection("lists", out);
    }

    QJsonObject listListGroups(const QJsonObject &) {
        QList<ListGroup> groups;
        for (const auto &g : s.groups())
            if (!g.deleted)
                groups.append(g);
        std::sort(groups.begin(), groups.end(), [](const ListGroup &x, const ListGroup &y) { return x.order != y.order ? x.order < y.order : x.id < y.id; });
        QJsonArray out;
        for (const auto &g : groups)
            out.append(QJsonObject{{"id", idString(g.id)}, {"uid", g.uid}, {"name", g.name}, {"collapsed", g.collapsed}});
        return collection("list_groups", out);
    }

    QJsonObject listSmartLists(const QJsonObject &) {
        QList<TaskList> lists;
        for (const auto &l : s.lists())
            if (!l.deleted && !l.archived && l.kind == ListKind::Smart)
                lists.append(l);
        std::sort(lists.begin(), lists.end(), [](const TaskList &x, const TaskList &y) { return x.order != y.order ? x.order < y.order : x.id < y.id; });
        QJsonArray out;
        for (const auto &l : lists)
            out.append(listJson(l));
        return collection("smart_lists", out);
    }

    QJsonObject listTags(const QJsonObject &args) {
        qint64 group = 0;
        if (args.contains("tag_group_id")) {
            group = idFrom(args["tag_group_id"], "tag_group_id");
            if (!s.tagGroups().contains(group) || s.tagGroups().value(group).deleted)
                fail("not_found", QStringLiteral("No tag group with id %1.").arg(group));
        }
        QList<Tag> tags;
        for (const auto &t : s.tags())
            if (!t.deleted && (!group || t.groupId == group))
                tags.append(t);
        std::sort(tags.begin(), tags.end(), [](const Tag &x, const Tag &y) { return x.order != y.order ? x.order < y.order : x.id < y.id; });
        QJsonArray out;
        for (const auto &t : tags)
            out.append(tagJson(t));
        return collection("tags", out);
    }

    QJsonObject listTagGroups(const QJsonObject &) {
        QList<TagGroup> groups;
        for (const auto &g : s.tagGroups())
            if (!g.deleted)
                groups.append(g);
        std::sort(groups.begin(), groups.end(), [](const TagGroup &x, const TagGroup &y) { return x.order != y.order ? x.order < y.order : x.id < y.id; });
        QJsonArray out;
        for (const auto &g : groups)
            out.append(QJsonObject{{"id", idString(g.id)}, {"uid", g.uid}, {"name", g.name}});
        return collection("tag_groups", out);
    }

    QJsonObject listLocations(const QJsonObject &) {
        QList<Location> places;
        for (const auto &l : s.locations())
            if (!l.deleted)
                places.append(l);
        std::sort(places.begin(), places.end(), [](const Location &x, const Location &y) { return x.id < y.id; });
        QJsonArray out;
        for (const auto &l : places)
            out.append(QJsonObject{{"id", idString(l.id)}, {"uid", l.uid}, {"name", l.name}, {"address", l.address}, {"paused", l.paused}});
        return collection("locations", out);
    }

    QJsonObject defaultDestination(const QJsonObject &) {
        const qint64 id = defaultListId();
        if (!id)
            fail("not_found", "There is no list to put new tasks in; create a list first.");
        const TaskList l = s.list(id);
        return QJsonObject{{"destination", QJsonObject{{"destination_id", "list:" + idString(id)},
                                                       {"list_id", idString(id)},
                                                       {"title", l.name}}}};
    }

    // ----- reading tasks

    QJsonObject getTask(const QJsonObject &args) {
        return QJsonObject{{"task", taskJson(findTask(args["task_id"]))}};
    }

    QJsonObject listSubtasks(const QJsonObject &args) {
        const Task parent = findTask(args["task_id"]);
        const bool completed = !args.contains("include_completed") || parseBool(args["include_completed"], "include_completed");
        QList<Task> kids;
        for (const auto &c : childrenOf(parent.id))
            if (completed || !c.completed)
                kids.append(c);
        QJsonObject out = collection("tasks", taskArray(kids));
        out["parent_task_id"] = idString(parent.id);
        return out;
    }

    QJsonObject selectedTasks(const QJsonObject &) {
        if (!a.selectedTaskIds)
            fail("automation_unavailable", "Reading the selection needs the Precrime window; this process has none.");
        QList<Task> tasks;
        for (auto id : a.selectedTaskIds()) {
            const Task t = s.task(id);
            if (t.id && !t.deleted)
                tasks.append(t);
        }
        return collection("tasks", taskArray(tasks));
    }

    QJsonObject queryTasks(const QJsonObject &args) {
        const int targets = int(args.contains("list_id")) + int(args.contains("smart_list_id")) + int(args.contains("focus"));
        if (targets > 1)
            invalid("Provide at most one of list_id, smart_list_id or focus.");
        QString search;
        if (args.contains("search")) {
            search = parseString(args["search"], "search");
            if (search.size() > kMaxSearch)
                invalid(QStringLiteral("search must be at most %1 characters.").arg(kMaxSearch));
        }
        const bool includeCompleted = args.contains("include_completed") && parseBool(args["include_completed"], "include_completed");
        const bool includeArchived = args.contains("include_archived") && parseBool(args["include_archived"], "include_archived");
        const int limit = args.contains("limit") ? parseInt(args["limit"], "limit", 1, 200) : 50;
        const int offset = args.contains("offset") ? parseInt(args["offset"], "offset", 0, 1000000000) : 0;
        QDate dueBefore, startAfter, startBefore;
        if (args.contains("due_on_or_before")) dueBefore = parseDate(args["due_on_or_before"], "due_on_or_before");
        if (args.contains("start_on_or_after")) startAfter = parseDate(args["start_on_or_after"], "start_on_or_after");
        if (args.contains("start_on_or_before")) startBefore = parseDate(args["start_on_or_before"], "start_on_or_before");
        QString paused = "hide";
        if (args.contains("paused_filter"))
            paused = QStringList{"hide", "include", "only"}.value(parseEnum(args["paused_filter"], "paused_filter", {"hide", "include", "only"}));

        const QDateTime when = now();
        QVector<Task> found;

        if (args.contains("focus")) {
            const int kind = parseEnum(args["focus"], "focus", kFocusNames);
            ViewOptions o;
            o.showScheduled = true;
            o.showPaused = paused != "hide";
            o.completedWindowMinutes = -1;
            o.search = search;
            QSet<qint64> seen;
            for (const auto &section : views().build(Source{kind, 0}, SortSpec{}, o))
                for (const auto &row : section.rows)
                    if (!seen.contains(row.taskId)) {
                        seen.insert(row.taskId);
                        found.append(s.task(row.taskId));
                    }
        } else {
            qint64 listId = 0;
            Query saved;
            if (args.contains("list_id")) {
                const TaskList l = findList(args["list_id"], "list_id");
                listId = l.id;
            } else if (args.contains("smart_list_id")) {
                const TaskList l = findList(args["smart_list_id"], "smart_list_id", true);
                saved = Query::parse(l.smartQuery);
            }
            const Query typed = Query::parse(search);
            QueryContext ctx;
            ctx.store = &s;
            ctx.now = when;
            ctx.settings = settings();
            ctx.isPaused = [this](const Task &t) { return views().isPaused(t); };
            for (const auto &t : s.tasks()) {
                if (t.deleted)
                    continue;
                const auto lit = s.lists().constFind(t.listId);
                if (lit == s.lists().cend() || lit->deleted || lit->kind == ListKind::Smart)
                    continue;
                if (listId ? t.listId != listId : (lit->archived && !includeArchived))
                    continue;
                if ((!saved.isEmpty() && !saved.matches(t, ctx)) || (!typed.isEmpty() && !typed.matches(t, ctx)))
                    continue;
                found.append(t);
            }
        }

        QVector<Task> kept;
        for (const auto &t : found) {
            if (t.completed && !includeCompleted && !(args["focus"].toString() == "done"))
                continue;
            const bool isPaused = views().isPaused(t);
            if ((paused == "hide" && isPaused) || (paused == "only" && !isPaused))
                continue;
            if (dueBefore.isValid() && !(t.dueDate.isValid() && t.dueDate <= dueBefore))
                continue;
            if (startAfter.isValid() && !(t.startDate.isValid() && t.startDate >= startAfter))
                continue;
            if (startBefore.isValid() && !(t.startDate.isValid() && t.startDate <= startBefore))
                continue;
            kept.append(t);
        }
        // A stable, readable order: open before done, soonest first, undated
        // last, then higher priority and the list's own manual order.
        std::stable_sort(kept.begin(), kept.end(), [](const Task &x, const Task &y) {
            if (x.completed != y.completed) return !x.completed;
            const QDateTime kx = Schedule::dueSortKey(x), ky = Schedule::dueSortKey(y);
            if (kx.isValid() != ky.isValid()) return kx.isValid();
            if (kx != ky) return kx < ky;
            if (x.priority != y.priority) return x.priority > y.priority;
            if (x.order != y.order) return x.order < y.order;
            return x.id < y.id;
        });
        QList<Task> page;
        for (int i = offset; i < kept.size() && page.size() < limit; ++i)
            page.append(kept.at(i));
        QJsonObject out = collection("tasks", taskArray(page));
        out["total_count"] = kept.size();
        return out;
    }

    // ----- writing tasks

    QJsonObject createTask(const QJsonObject &args) {
        if (!args.contains("title"))
            invalid("title is required.");
        Fields f = parseFields(args);
        const bool natural = args.contains("natural_language") && parseBool(args["natural_language"], "natural_language");

        QStringList subtasks;
        if (args.contains("subtasks")) {
            if (args.contains("parent_task_id"))
                invalid("subtasks cannot be combined with parent_task_id; sub-tasks do not nest.");
            if (!args["subtasks"].isArray())
                invalid("subtasks must be an array of titles.");
            for (const auto &v : args["subtasks"].toArray()) {
                const QString title = v.toString().trimmed();
                if (!v.isString() || title.isEmpty() || title.size() > kMaxTitle)
                    invalid("Each subtask must be a non-empty title of at most 1000 characters.");
                subtasks << title;
            }
            if (subtasks.isEmpty() || subtasks.size() > kMaxBatch)
                invalid(QStringLiteral("subtasks must hold 1 through %1 titles.").arg(kMaxBatch));
        }

        Task t;
        Task parent;
        if (args.contains("parent_task_id")) {
            parent = findTask(args["parent_task_id"], "parent_task_id");
            if (parent.parentId)
                invalid("parent_task_id must be a top-level task; sub-tasks do not nest.");
            if (args.contains("list_id") && findList(args["list_id"], "list_id").id != parent.listId)
                invalid("list_id does not match the parent task's list.");
            if (f.type && *f.type != TaskType::Task)
                invalid("A sub-task must have task_type normal.");
            t.parentId = parent.id;
            t.listId = parent.listId;
        } else if (args.contains("list_id")) {
            t.listId = findList(args["list_id"], "list_id").id;
        } else {
            t.listId = defaultListId();
            if (!t.listId)
                fail("not_found", "There is no list to put the task in; create a list first.");
        }

        // Quick-Entry style parsing happens first so explicit fields override it.
        ParsedEntry parsed;
        if (natural) {
            QStringList names;
            for (const auto &l : s.lists())
                if (!l.deleted && !l.archived && l.kind != ListKind::Smart)
                    names << l.name;
            parsed = NaturalLanguage::parse(f.title.value_or(QString()), now(), names);
            if (!parsed.title.isEmpty())
                t.title = parsed.title.left(kMaxTitle);
            if (parsed.startDate.isValid()) { t.startDate = parsed.startDate; t.startTime = parsed.startTime; }
            if (parsed.dueDate.isValid()) { t.dueDate = parsed.dueDate; t.dueTime = parsed.dueTime; }
            else if (parsed.dueTime >= 0) { t.dueDate = now().date(); t.dueTime = parsed.dueTime; }
            if (parsed.duration > 0) t.duration = parsed.duration;
            if (parsed.priority > 0) t.priority = parsed.priority;
            if (parsed.starred) t.starred = true;
            if (parsed.recurrence.isSet()) {
                t.recurrence = parsed.recurrence;
                if (!t.dueDate.isValid() && !t.startDate.isValid())
                    t.dueDate = Recurrences::next(parsed.recurrence, now().date().addDays(-1));
            }
            if (!parsed.alertOffsets.isEmpty() && !t.dueDate.isValid())
                t.dueDate = now().date();
            if (!parsed.list.isEmpty() && !args.contains("list_id") && !args.contains("parent_task_id"))
                for (const auto &l : s.lists())
                    if (!l.deleted && !l.archived && l.kind != ListKind::Smart && l.name.compare(parsed.list, Qt::CaseInsensitive) == 0) {
                        t.listId = l.id;
                        break;
                    }
            f.title.reset();  // The parsed title stands in for the typed one.
        }
        applyFields(t, f);
        if (!parsed.tags.isEmpty()) {
            f.hasTags = true;
            f.tagNames << parsed.tags;
        }
        if (t.title.trimmed().isEmpty())
            invalid("title must not be empty.");
        if (!subtasks.isEmpty() && t.type == TaskType::Task)
            t.type = TaskType::Checklist;
        t.id = s.nextId("tasks");

        s.begin(t.type == TaskType::Project ? "New Project" : t.type == TaskType::Checklist ? "New Checklist" : "New Task");
        if (parent.id && parent.type == TaskType::Task) {
            // Giving a plain task a child makes it a project, as the UI does.
            parent.type = TaskType::Project;
            s.putTask(parent);
        }
        t.order = nextOrder(t.listId, t.parentId);
        applyTags(t, f);
        s.putTask(t);
        for (int offset : parsed.alertOffsets) {
            Alarm alarm;
            alarm.id = s.nextId("alarms");
            alarm.taskId = t.id;
            alarm.offset = offset;
            s.putAlarm(alarm);
        }
        AlarmTimes::applyDefaultReminder(s, t);
        QList<qint64> childIds;
        for (const auto &title : subtasks) {
            Task c;
            c.id = s.nextId("tasks");
            c.listId = t.listId;
            c.parentId = t.id;
            c.title = title;
            c.order = nextOrder(t.listId, t.id);
            s.putTask(c);
            childIds.append(c.id);
        }
        commit();

        QJsonObject out{{"task", taskJson(s.task(t.id))}};
        if (!childIds.isEmpty()) {
            QList<Task> kids;
            for (auto id : childIds)
                kids.append(s.task(id));
            out["subtasks"] = taskArray(kids);
        }
        return out;
    }

    QJsonObject updateTask(const QJsonObject &args) {
        Task t = findTask(args["task_id"]);
        const Fields f = parseFields(args);
        applyFields(t, f);
        s.begin("Edit Task");
        applyTags(t, f);
        s.putTask(t);
        commit();
        return QJsonObject{{"task", taskJson(s.task(t.id))}};
    }

    QJsonObject updateTaskAction(const QJsonObject &args) {
        if (!args.contains("action"))
            invalid("action is required.");
        return updateTask(args);
    }

    QJsonObject deleteTask(const QJsonObject &args) {
        const QList<Task> tasks = findTasks(args);
        QSet<qint64> done;
        s.begin(tasks.size() > 1 ? QStringLiteral("Delete %1 Tasks").arg(tasks.size()) : QStringLiteral("Delete Task"));
        for (const auto &t : tasks)
            deleteTree(t.id, done);
        commit();
        QJsonArray ids;
        for (const auto &t : tasks)
            ids.append(idString(t.id));
        QJsonObject out{{"deleted", ids}, {"item_count", ids.size()}};
        if (args.contains("task_id"))
            out["task_id"] = idString(tasks.first().id);
        return out;
    }

    QJsonObject duplicateTask(const QJsonObject &args) {
        const Task t = findTask(args["task_id"]);
        s.begin("Duplicate");
        const qint64 id = duplicateTree(t, t.parentId, t.listId);
        commit();
        return QJsonObject{{"task", taskJson(s.task(id))}};
    }

    QJsonObject moveTask(const QJsonObject &args) {
        const QList<Task> tasks = findTasks(args);
        const bool toList = args.contains("list_id"), toParent = args.contains("parent_task_id");
        if (toList == toParent)
            invalid("Provide exactly one of list_id or parent_task_id.");
        QSet<qint64> moving;
        for (const auto &t : tasks)
            moving.insert(t.id);

        TaskList list;
        Task parent;
        if (toList) {
            list = findList(args["list_id"], "list_id");
        } else {
            parent = findTask(args["parent_task_id"], "parent_task_id");
            if (parent.parentId)
                invalid("parent_task_id must be a top-level task; sub-tasks do not nest.");
            if (moving.contains(parent.id))
                invalid("A task cannot be moved into itself.");
            for (const auto &t : tasks) {
                if (t.type != TaskType::Task || !childrenOf(t.id).isEmpty())
                    invalid("Only plain tasks without sub-tasks can become sub-tasks.");
            }
        }

        s.begin(toList ? "Move to List" : "Move into Project");
        if (toParent && parent.type == TaskType::Task) {
            parent.type = TaskType::Project;
            s.putTask(parent);
        }
        std::function<void(qint64)> move = [&](qint64 id) {
            Task t = s.task(id);
            if (toParent) {
                t.parentId = parent.id;
                t.listId = parent.listId;
                t.order = nextOrder(t.listId, t.parentId);
            } else {
                t.listId = list.id;
                if (moving.contains(id)) {
                    // A moved sub-task leaves its parent unless the parent moves too.
                    if (t.parentId && !moving.contains(t.parentId))
                        t.parentId = 0;
                    t.order = nextOrder(list.id, t.parentId);
                }
            }
            s.putTask(t);
            if (!toParent)
                for (const auto &c : childrenOf(id))
                    move(c.id);
        };
        for (const auto &t : tasks)
            move(t.id);
        commit();

        QList<Task> moved;
        for (const auto &t : tasks)
            moved.append(s.task(t.id));
        QJsonObject out = collection("tasks", taskArray(moved));
        out["task"] = out["tasks"].toArray().first();
        return out;
    }

    QJsonObject setCompleted(const QJsonObject &args, bool done) {
        const QList<Task> tasks = findTasks(args);
        const QDateTime when = now();
        s.begin(done ? "Mark as Completed" : "Mark as Not Completed");
        for (const auto &t : tasks)
            completeTree(s.task(t.id), done, when);
        commit();
        QList<Task> after;
        for (const auto &t : tasks)
            after.append(s.task(t.id));
        QJsonObject out = collection("tasks", taskArray(after));
        out["task"] = out["tasks"].toArray().first();
        return out;
    }
    QJsonObject completeTask(const QJsonObject &args) { return setCompleted(args, true); }
    QJsonObject uncompleteTask(const QJsonObject &args) { return setCompleted(args, false); }

    // ----- tags

    QJsonObject setPaused(const QJsonObject &args, bool paused) {
        Tag tag = findTag(args);
        if (tag.paused != paused) {
            tag.paused = paused;
            s.begin(paused ? "Pause Tag" : "Resume Tag");
            s.putTag(tag);
            commit();
        }
        return QJsonObject{{"tag", tagJson(s.tag(tag.id))}};
    }
    QJsonObject pauseTag(const QJsonObject &args) { return setPaused(args, true); }
    QJsonObject unpauseTag(const QJsonObject &args) { return setPaused(args, false); }

    // ----- navigation

    void needSelect() {
        if (!a.selectSource)
            fail("automation_unavailable", "Navigation needs the Precrime window; this process has none.");
    }

    QJsonObject selectList(const QJsonObject &args) {
        const TaskList l = findList(args["list_id"], "list_id");
        needSelect();
        a.selectSource("list:" + idString(l.id));
        return QJsonObject{{"selected", true}, {"list_id", idString(l.id)}};
    }

    QJsonObject selectListGroup(const QJsonObject &args) {
        const qint64 id = idFrom(args["list_group_id"], "list_group_id");
        if (!s.groups().contains(id) || s.groups().value(id).deleted)
            fail("not_found", QStringLiteral("No list group with id %1.").arg(id));
        needSelect();
        a.selectSource("group:" + idString(id));
        return QJsonObject{{"selected", true}, {"list_group_id", idString(id)}};
    }

    QJsonObject selectSmartList(const QJsonObject &args) {
        const TaskList l = findList(args["smart_list_id"], "smart_list_id", true);
        needSelect();
        a.selectSource("list:" + idString(l.id));
        return QJsonObject{{"selected", true}, {"smart_list_id", idString(l.id)}};
    }

    QJsonObject selectFocus(const QJsonObject &args) {
        const QString focus = kFocusNames.value(parseEnum(args["focus"], "focus", kFocusNames));
        needSelect();
        a.selectSource(focus);
        return QJsonObject{{"selected", true}, {"collection_id", "focus:" + focus}};
    }

    QJsonObject openTaskCmd(const QJsonObject &args) {
        const Task t = findTask(args["task_id"]);
        if (!a.openTask)
            fail("automation_unavailable", "Opening a task needs the Precrime window; this process has none.");
        a.openTask(t.id);
        return QJsonObject{{"opened", true}, {"task_id", idString(t.id)}};
    }

    QJsonObject urlCommand(const QUrl &url);
};

// ================================================================ commands

namespace {

struct Command {
    QString name;
    QString description;
    QJsonObject properties;
    QStringList required;
    Automation::Impl::Handler handler;
};

QJsonObject merged(QJsonObject base, const QJsonObject &extra) {
    for (auto it = extra.begin(); it != extra.end(); ++it)
        base.insert(it.key(), it.value());
    return base;
}

const QList<Command> &commands() {
    using I = Automation::Impl;
    static const QList<Command> table = [] {
        const QJsonObject taskIds{{"task_id", str("A single task id.")}, {"task_ids", idArray("Up to 100 task ids; use instead of task_id.")}};
        QJsonObject update = merged(taskWriteProps(false), {{"task_id", str("Id (or uid) of the task to change.")}});
        QList<Command> c;
        c << Command{"list_lists", "List the task lists (including the Inbox when enabled), in sidebar order.",
                     {{"include_archived", flag("Also list archived lists. Default false.")}}, {}, &I::listLists};
        c << Command{"list_list_groups", "List the list groups that organise lists in the sidebar.", {}, {}, &I::listListGroups};
        c << Command{"list_smart_lists", "List saved Smart Lists with their search expressions.", {}, {}, &I::listSmartLists};
        c << Command{"list_tags", "List tags, including whether each is paused.",
                     {{"tag_group_id", str("Only tags in this tag group.")}}, {}, &I::listTags};
        c << Command{"list_tag_groups", "List tag groups.", {}, {}, &I::listTagGroups};
        c << Command{"list_locations", "List saved locations.", {}, {}, &I::listLocations};
        c << Command{"get_default_task_destination", "Say which list new tasks go to when no list is given.", {}, {}, &I::defaultDestination};
        c << Command{"get_task", "Get one task by task_id.", {{"task_id", str("Task id or uid.")}}, {"task_id"}, &I::getTask};
        c << Command{"query_tasks",
                     "Search and filter tasks. Use at most one of list_id, smart_list_id or focus (default: all open tasks). "
                     "search uses the reference app syntax: words, \"phrases\", OR, -word, type: hiprio, tags: work, lists: Home.",
                     {{"list_id", str("Only tasks in this list.")},
                      {"smart_list_id", str("Run this saved Smart List.")},
                      {"focus", enumStr("A focus list.", kFocusNames)},
                      {"search", str("the reference app search expression, at most 256 characters.")},
                      {"due_on_or_before", str("yyyy-MM-dd; keep tasks due on or before this date.")},
                      {"start_on_or_after", str("yyyy-MM-dd; keep tasks starting on or after this date.")},
                      {"start_on_or_before", str("yyyy-MM-dd; keep tasks starting on or before this date.")},
                      {"include_completed", flag("Include completed tasks. Default false.")},
                      {"include_archived", flag("Include tasks in archived lists. Default false.")},
                      {"paused_filter", enumStr("hide (default), include or only tasks paused by a paused tag.", {"hide", "include", "only"})},
                      {"limit", integer("Page size, 1 through 200. Default 50.")},
                      {"offset", integer("Tasks to skip. Default 0.")}},
                     {}, &I::queryTasks};
        c << Command{"list_subtasks", "List the sub-tasks of a project or checklist.",
                     {{"task_id", str("The project or checklist.")}, {"include_completed", flag("Default true.")}}, {"task_id"}, &I::listSubtasks};
        c << Command{"selected_tasks", "Return the tasks currently selected in the Precrime window.", {}, {}, &I::selectedTasks};
        c << Command{"create_task", "Create a task, project or checklist, optionally with sub-tasks. One undoable change.",
                     taskWriteProps(true), {"title"}, &I::createTask};
        c << Command{"update_task", "Change a task's fields. Omitted fields stay as they are. One undoable change.", update, {"task_id"}, &I::updateTask};
        c << Command{"update_task_action", "Set, replace or clear a task's action.",
                     {{"task_id", str("Task id or uid.")}, {"action", actionSchema()}}, {"task_id", "action"}, &I::updateTaskAction};
        c << Command{"delete_task", "Delete tasks and everything inside them. Undoable in the app.", taskIds, {}, &I::deleteTask};
        c << Command{"duplicate_task", "Duplicate a task with its sub-tasks.", {{"task_id", str("Task id or uid.")}}, {"task_id"}, &I::duplicateTask};
        c << Command{"move_task", "Move tasks to another list, or into a project or checklist. Give exactly one of list_id or parent_task_id.",
                     merged(taskIds, {{"list_id", str("Destination list.")}, {"parent_task_id", str("Destination project or checklist.")}}), {}, &I::moveTask};
        c << Command{"complete_task",
                     "Mark tasks done. A repeating task keeps a completed copy and advances to its next occurrence, so do not retry blindly.",
                     taskIds, {}, &I::completeTask};
        c << Command{"uncomplete_task", "Mark tasks not done.", taskIds, {}, &I::uncompleteTask};
        c << Command{"pause_tag", "Pause a tag, hiding every task that carries it. Idempotent.",
                     {{"tag_id", str("Tag id.")}, {"tag_name", str("Tag name; use instead of tag_id.")}}, {}, &I::pauseTag};
        c << Command{"unpause_tag", "Resume a paused tag. Idempotent.",
                     {{"tag_id", str("Tag id.")}, {"tag_name", str("Tag name; use instead of tag_id.")}}, {}, &I::unpauseTag};
        c << Command{"select_list", "Show a list in the Precrime window.", {{"list_id", str("List id.")}}, {"list_id"}, &I::selectList};
        c << Command{"select_list_group", "Show a list group in the Precrime window.", {{"list_group_id", str("List group id.")}}, {"list_group_id"}, &I::selectListGroup};
        c << Command{"select_smart_list", "Show a Smart List in the Precrime window.", {{"smart_list_id", str("Smart List id.")}}, {"smart_list_id"}, &I::selectSmartList};
        c << Command{"select_focus", "Show a focus list in the Precrime window.", {{"focus", enumStr("Focus list.", kFocusNames)}}, {"focus"}, &I::selectFocus};
        c << Command{"open_task", "Open a task in the Precrime window.", {{"task_id", str("Task id or uid.")}}, {"task_id"}, &I::openTaskCmd};
        return c;
    }();
    return table;
}

const Command *findCommand(const QString &name) {
    for (const auto &c : commands())
        if (c.name == name)
            return &c;
    return nullptr;
}

QJsonObject errorResponse(const QString &code, const QString &message) {
    return QJsonObject{{"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject inputSchema(const Command &c) {
    QJsonObject schema{{"type", "object"}, {"properties", c.properties}, {"additionalProperties", false}};
    if (!c.required.isEmpty())
        schema["required"] = QJsonArray::fromStringList(c.required);
    return schema;
}

}  // namespace

// ================================================================ Automation

Automation::Automation(Store &store) : m_store(store), d(std::make_unique<Impl>(*this)) {}
Automation::~Automation() = default;

QStringList Automation::commandNames() {
    QStringList names;
    for (const auto &c : commands())
        names << c.name;
    return names;
}

QJsonObject Automation::mcpTools() {
    QJsonArray tools;
    for (const auto &c : commands())
        tools.append(QJsonObject{{"name", c.name}, {"description", c.description}, {"inputSchema", inputSchema(c)}});
    return QJsonObject{{"tools", tools}};
}

QJsonObject Automation::run(const QString &command, const QJsonObject &args) {
    d->reset();
    try {
        const Command *c = findCommand(command);
        if (!c)
            invalid(QStringLiteral("Unknown command \"%1\". Run `precrime ctl help` for the list.").arg(command));
        for (const auto &k : args.keys())
            if (!c->properties.contains(k))
                invalid(QStringLiteral("Unknown argument \"%1\" for %2.").arg(k, command));
        for (const auto &r : c->required)
            if (!args.contains(r) || args[r].isNull())
                invalid(QStringLiteral("%1 is required.").arg(r));
        QJsonObject out = ((*d).*(c->handler))(args);
        out["ok"] = true;
        return out;
    } catch (const Failure &f) {
        return errorResponse(f.code, f.message);
    }
}

// ================================================================ CLI

namespace {

// key=value arguments are typed by the command's schema, so `title=123` stays
// the string "123" while `limit=5` becomes the number 5.
QJsonValue coerce(const QJsonObject &property, const QString &value) {
    const QJsonValue typeValue = property["type"];
    QStringList types;
    if (typeValue.isArray())
        for (const auto &t : typeValue.toArray())
            types << t.toString();
    else
        types << typeValue.toString();
    if (value == "null" && types.contains("null"))
        return QJsonValue(QJsonValue::Null);
    if (types.contains("boolean")) {
        const QString v = value.toLower();
        if (v == "true" || v == "1" || v == "yes") return true;
        if (v == "false" || v == "0" || v == "no") return false;
    } else if (types.contains("integer")) {
        bool ok = false;
        const qint64 n = value.toLongLong(&ok);
        if (ok) return double(n);
    } else if (types.contains("array")) {
        QJsonParseError e;
        const auto doc = QJsonDocument::fromJson(value.toUtf8(), &e);
        if (e.error == QJsonParseError::NoError && doc.isArray())
            return doc.array();
        QJsonArray items;
        for (const auto &p : value.split(value.contains('\n') ? '\n' : ',', Qt::SkipEmptyParts))
            items.append(p.trimmed());
        return items;
    } else if (types.contains("object")) {
        const auto doc = QJsonDocument::fromJson(value.toUtf8());
        if (doc.isObject()) return doc.object();
    }
    return value;
}

}  // namespace

int Automation::runCli(const QStringList &args, QTextStream &out, QTextStream &err) {
    if (args.isEmpty() || args.first() == "help" || args.first() == "--help" || args.first() == "-h") {
        if (args.size() > 1) {
            const Command *c = findCommand(args.at(1));
            if (!c) {
                err << "Unknown command: " << args.at(1) << "\n";
                return 1;
            }
            out << c->name << "\n  " << c->description << "\n\nArguments (JSON schema):\n"
                << QJsonDocument(inputSchema(*c)).toJson(QJsonDocument::Indented) << "\n";
            return 0;
        }
        out << "Usage: precrime ctl <command> [json-args | key=value ...]\n"
               "       precrime ctl url <precrime://x-callback-url/...>\n"
               "       precrime ctl help [command]\n\nCommands:\n";
        for (const auto &c : commands())
            out << "  " << c.name.leftJustified(30) << c.description.section('.', 0, 0) << "\n";
        return 0;
    }

    const QString command = args.first();
    QJsonObject response;
    if (command == "url") {
        if (args.size() != 2) {
            err << "Usage: precrime ctl url <url>\n";
            return 1;
        }
        response = handleUrl(QUrl(args.at(1)));
    } else {
        const Command *c = findCommand(command);
        QJsonObject params;
        const QStringList rest = args.mid(1);
        if (rest.size() == 1 && rest.first().trimmed().startsWith('{')) {
            QJsonParseError e;
            const auto doc = QJsonDocument::fromJson(rest.first().toUtf8(), &e);
            if (e.error != QJsonParseError::NoError || !doc.isObject()) {
                err << "Arguments are not valid JSON: " << e.errorString() << "\n";
                return 1;
            }
            params = doc.object();
        } else {
            for (const auto &pair : rest) {
                const int eq = pair.indexOf('=');
                if (eq <= 0) {
                    err << "Expected key=value or a JSON object, got: " << pair << "\n";
                    return 1;
                }
                const QString key = pair.left(eq);
                const QJsonObject prop = c ? c->properties.value(key).toObject() : QJsonObject();
                params[key] = coerce(prop, pair.mid(eq + 1));
            }
        }
        response = run(command, params);
    }
    out << QJsonDocument(response).toJson(QJsonDocument::Indented);
    return response["ok"].toBool() ? 0 : 1;
}

// ================================================================ URL scheme

QString Automation::shareableLink(const Task &task) {
    return QStringLiteral("precrime://x-callback-url/showTask?uid=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(task.uid)));
}

QJsonObject Automation::handleUrl(const QUrl &url) {
    d->reset();
    try {
        return d->urlCommand(url);
    } catch (const Failure &f) {
        return errorResponse(f.code, f.message);
    }
}

QJsonObject Automation::Impl::urlCommand(const QUrl &url) {
    if (url.scheme().compare("precrime", Qt::CaseInsensitive) != 0)
        invalid("Not a precrime:// URL.");
    // precrime://x-callback-url/<action>, and tolerate precrime://<action>.
    QString action = url.host().compare("x-callback-url", Qt::CaseInsensitive) == 0 ? url.path() : url.host();
    action = action.remove('/').toLower();
    const QUrlQuery q(url);
    auto param = [&](const QString &key) { return q.queryItemValue(key, QUrl::FullyDecoded); };
    auto has = [&](const QString &key) { return q.hasQueryItem(key); };
    auto truthy = [&](const QString &key) {
        const QString v = param(key).toLower();
        return v == "1" || v == "true" || v == "yes";
    };
    // x-success gets the new uid appended as `add`, as the reference app's callbacks do.
    auto finish = [&](QJsonObject result, const QString &callbackKey = QString(), const QString &callbackValue = QString()) {
        result["ok"] = true;
        result["action"] = action;
        if (has("x-success")) {
            QString target = param("x-success");
            if (!callbackKey.isEmpty())
                target += QString(target.contains('?') ? "&" : "?") + callbackKey + "=" + QString::fromUtf8(QUrl::toPercentEncoding(callbackValue));
            result["callback"] = target;
        }
        return result;
    };
    auto listByName = [&](const QString &name, bool caseSensitive) -> TaskList {
        for (const auto &l : s.lists())
            if (!l.deleted && (caseSensitive ? l.name == name : l.name.compare(name, Qt::CaseInsensitive) == 0))
                return l;
        fail("not_found", QStringLiteral("No list named \"%1\".").arg(name));
    };
    auto showSource = [&](const QString &key) {
        if (a.selectSource)
            a.selectSource(key);
        return finish(QJsonObject{{"source", key}});
    };

    if (action == "showall") return showSource("all");
    if (action == "showtoday") return showSource("today");
    if (action == "showstarred") return showSource("starred");
    if (action == "showscheduled") return showSource("scheduled");
    if (action == "launch") return finish({});

    if (action == "showlist") {
        if (!has("name"))
            invalid("showlist needs a name.");
        return showSource("list:" + idString(listByName(param("name"), true).id));
    }
    if (action == "search") {
        if (!has("text"))
            invalid("search needs text.");
        return finish(QJsonObject{{"search", param("text")}});
    }
    if (action == "showtask") {
        const Task t = findTask(param("uid"), "uid");
        if (a.openTask)
            a.openTask(t.id);
        return finish(QJsonObject{{"task_id", idString(t.id)}, {"uid", t.uid}});
    }
    if (action == "completetasks") {
        QJsonArray ids;
        for (const auto &uid : param("uids").split(',', Qt::SkipEmptyParts))
            ids.append(uid.trimmed());
        if (ids.isEmpty())
            invalid("completetasks needs uids.");
        QJsonObject r = a.run("complete_task", QJsonObject{{"task_ids", ids}});
        if (!r["ok"].toBool())
            return r;
        return finish(r);
    }
    if (action == "gettaskid") {
        const QString title = param("task");
        if (title.isEmpty())
            invalid("gettaskid needs task.");
        const qint64 listId = has("forlist") ? listByName(param("forlist"), false).id : 0;
        for (const auto &t : s.tasks())
            if (!t.deleted && t.type != TaskType::Task && (!listId || t.listId == listId) &&
                t.title.compare(title, Qt::CaseInsensitive) == 0)
                return finish(QJsonObject{{"uid", t.uid}, {"task", taskJson(t)}}, "uid", t.uid);
        fail("not_found", QStringLiteral("No project or checklist titled \"%1\".").arg(title));
    }
    if (action != "add" && action != "addnewtask")
        invalid(QStringLiteral("Unknown action \"%1\".").arg(action));

    // ---- add / addnewtask

    QJsonObject args;
    const QString type = param("type");
    if (!type.isEmpty()) {
        const int n = type.toInt();
        if (type.size() != 1 || n < 0 || n > 2)
            invalid("type must be 0, 1 or 2.");
        args["task_type"] = kTypeNames.at(n);
    }
    if (has("priority")) {
        bool ok = false;
        const int n = param("priority").toInt(&ok);
        if (!ok || n < 0 || n > 3)
            invalid("priority must be 0 through 3.");
        args["priority"] = kPriorityNames.at(n);
    }
    if (has("starred"))
        args["starred"] = truthy("starred");
    if (has("note"))
        args["notes"] = param("note");
    if (has("tags"))
        args["tag_names"] = QJsonArray::fromStringList(
            [&] { QStringList l; for (const auto &t : param("tags").split(',', Qt::SkipEmptyParts)) l << t.trimmed(); return l; }());
    if (has("subtasks")) {
        QStringList titles;
        for (const auto &t : param("subtasks").split('\n', Qt::SkipEmptyParts))
            if (!t.trimmed().isEmpty())
                titles << t.trimmed();
        if (!titles.isEmpty())
            args["subtasks"] = QJsonArray::fromStringList(titles);
    }
    if (has("action")) {
        const QString spec = param("action");
        const int colon = spec.indexOf(':');
        const QString kind = spec.left(colon).toLower() == "mail" ? "mail" : spec.left(colon).toLower();
        if (colon <= 0 || !kActionNames.contains(kind))
            invalid("action must look like call:+15551234567 (call, message, mail, visit, url or google).");
        args["action"] = QJsonObject{{"type", kind}, {"value", spec.mid(colon + 1)}};
    }

    // Dates accept yyyy-MM-dd, a day offset from today, or a phrase.
    const QDate today = now().date();
    int phraseMinutes = -1;
    auto readDate = [&](const QString &raw, const QString &key) -> QDate {
        static const QRegularExpression iso("^(\\d{4}-\\d{2}-\\d{2})(?:[ T](\\d{1,2}:\\d{2}))?$");
        static const QRegularExpression offset("^[+-]?\\d{1,5}$");
        const auto m = iso.match(raw.trimmed());
        if (m.hasMatch()) {
            const QDate d = QDate::fromString(m.captured(1), "yyyy-MM-dd");
            if (!d.isValid()) invalid(QStringLiteral("%1 is not a valid date.").arg(key));
            if (!m.captured(2).isEmpty()) {
                const int mins = DateParse::parseTime(m.captured(2), now());
                if (mins >= 0) phraseMinutes = mins;
            }
            return d;
        }
        if (offset.match(raw.trimmed()).hasMatch())
            return today.addDays(raw.trimmed().toInt());
        const DateSuggestion best = DateParse::best(raw, now());
        if (!best.date.isValid())
            invalid(QStringLiteral("Could not understand %1 \"%2\".").arg(key, raw));
        phraseMinutes = best.minutes;
        return best.date;
    };
    auto timeParam = [&](const QString &key, int fallback) {
        if (!has(key) || param(key).isEmpty())
            return fallback;
        const int m = DateParse::parseTime(param(key), now());
        if (m < 0) invalid(QStringLiteral("Could not understand %1 \"%2\".").arg(key, param(key)));
        return m;
    };
    QDate due, start;
    int dueMinutes = -1, startMinutes = -1;
    if (has("due") && !param("due").isEmpty()) {
        phraseMinutes = -1;
        due = readDate(param("due"), "due");
        dueMinutes = timeParam("duetime", phraseMinutes);
    }
    if (has("start") && !param("start").isEmpty()) {
        phraseMinutes = -1;
        start = readDate(param("start"), "start");
        startMinutes = phraseMinutes;
    }
    if (has("repeat")) {
        // the reference app's presets; the recurrence only exists on the created task, so
        // it is applied after creation below.
        const int n = param("repeat").toInt();
        if (n < 1 || n > 4) invalid("repeat must be 1 (daily), 2 (weekly), 3 (every two weeks) or 4 (monthly).");
        if (!due.isValid() && !start.isValid()) invalid("repeat needs a due or start date.");
    }
    if (due.isValid()) {
        args["due_date"] = due.toString("yyyy-MM-dd");
        if (dueMinutes >= 0) args["due_time"] = timeValue(dueMinutes);
    }
    if (start.isValid()) {
        args["start_date"] = start.toString("yyyy-MM-dd");
        if (startMinutes >= 0) args["start_time"] = timeValue(startMinutes);
    }

    const QString title = has("task") && !param("task").isEmpty() ? param("task") : QStringLiteral("New Task");

    // Quick Entry cannot take every field, so the text carries what the
    // natural-language grammar understands and the rest rides along.
    if (action == "addnewtask" || truthy("usequickentry")) {
        QString text = has("task") ? param("task") : QString();
        for (const auto &t : args["tag_names"].toArray())
            text += " #" + t.toString();
        const int prio = kPriorityNames.indexOf(args["priority"].toString());
        if (prio > 0) text += " " + QString(prio, '!');
        if (args["starred"].toBool()) text += " *";
        if (has("forlist")) text += " /" + param("forlist") + "/";
        if (due.isValid()) {
            const qint64 days = today.daysTo(due);
            text += days == 0 ? " today" : days == 1 ? " tomorrow" : " " + due.toString("yyyy-MM-dd");
            if (dueMinutes >= 0) text += " at " + timeValue(dueMinutes).toString();
        }
        QJsonObject r{{"quickEntry", true}, {"text", text.trimmed()}};
        if (has("note")) r["notes"] = param("note");
        if (has("forlist")) r["list"] = param("forlist");
        return finish(r);
    }

    args["title"] = title;
    if (has("forlist") && !param("forlist").isEmpty()) {
        const TaskList list = listByName(param("forlist"), false);
        if (list.kind == ListKind::Smart)
            invalid("Tasks cannot be added to a Smart List.");
        args["list_id"] = idString(list.id);
    }
    if (has("forparenttask")) {
        args["parent_task_id"] = idString(findTask(param("forparenttask"), "forparenttask").id);
        args.remove("list_id");
    } else if (has("forparentname")) {
        const qint64 listId = args.contains("list_id") ? args["list_id"].toString().toLongLong() : defaultListId();
        qint64 found = 0;
        for (const auto &t : s.tasks())
            if (!t.deleted && !t.parentId && t.type != TaskType::Task && t.listId == listId &&
                t.title.compare(param("forparentname"), Qt::CaseInsensitive) == 0)
                found = t.id;
        if (!found)
            fail("not_found", QStringLiteral("No project or checklist titled \"%1\" in that list.").arg(param("forparentname")));
        args["parent_task_id"] = idString(found);
        args.remove("list_id");
    }

    QJsonObject created = a.run("create_task", args);
    if (!created["ok"].toBool())
        return created;
    const qint64 id = created["task"].toObject()["id"].toString().toLongLong();

    // Repeat presets are applied as an extra step of the same logical add;
    // the add itself is already one undoable change.
    if (has("repeat")) {
        Task t = s.task(id);
        const int n = param("repeat").toInt();
        t.recurrence.frequency = n == 1 ? Repeat::Daily : n == 4 ? Repeat::Monthly : Repeat::Weekly;
        t.recurrence.interval = n == 3 ? 2 : 1;
        s.begin("New Task");
        s.putTask(t);
        s.commit();
        m_views.reset();
        created["task"] = taskJson(s.task(id));
    }
    if (truthy("edit") && a.openTask)
        a.openTask(id);
    QJsonObject r = created;
    r["uid"] = s.task(id).uid;
    if (truthy("saveinclipboard"))
        r["clipboard"] = r["uid"];
    return finish(r, "add", r["uid"].toString());
}

// ================================================================ MCP

McpServer::McpServer(Automation &automation, const QString &version) : m_automation(automation), m_version(version) {}

namespace {

QJsonObject rpcError(const QJsonValue &id, int code, const QString &message) {
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

}  // namespace

QJsonObject McpServer::handle(const QJsonObject &message) {
    const QString method = message["method"].toString();
    const bool isRequest = message.contains("id") && !message["id"].isUndefined();
    const QJsonValue id = message["id"];
    if (!isRequest) {
        // Notifications (initialized, cancelled) get no reply, known or not.
        return {};
    }
    auto reply = [&](const QJsonObject &result) { return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}; };

    if (method == "initialize") {
        return reply(QJsonObject{{"protocolVersion", "2025-06-18"},
                                 {"serverInfo", QJsonObject{{"name", "precrime"}, {"version", m_version}}},
                                 {"capabilities", QJsonObject{{"tools", QJsonObject()}}},
                                 {"instructions", "Precrime is a classic task manager. Call list_lists first to find list ids, then query_tasks, create_task and the rest."}});
    }
    if (method == "ping")
        return reply({});
    if (method == "tools/list")
        return reply(Automation::mcpTools());
    if (method == "tools/call") {
        const QJsonObject params = message["params"].toObject();
        const QString name = params["name"].toString();
        if (name.isEmpty())
            return rpcError(id, -32602, "tools/call needs a tool name.");
        if (!params["arguments"].isUndefined() && !params["arguments"].isNull() && !params["arguments"].isObject())
            return rpcError(id, -32602, "arguments must be an object.");
        if (m_reload && Automation::commandNames().contains(name))
            m_automation.reloadStore();
        const QJsonObject response = m_automation.run(name, params["arguments"].toObject());
        return reply(QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact))}}}},
            {"isError", !response["ok"].toBool()}});
    }
    return rpcError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

int McpServer::run(QTextStream &in, QTextStream &out) {
    for (QString line = in.readLine(); !line.isNull(); line = in.readLine()) {
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError e;
        const auto doc = QJsonDocument::fromJson(line.toUtf8(), &e);
        QJsonObject response;
        if (e.error != QJsonParseError::NoError || !doc.isObject())
            response = rpcError(QJsonValue(QJsonValue::Null), -32700, "Parse error");
        else
            response = handle(doc.object());
        if (response.isEmpty())
            continue;
        out << QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)) << '\n';
        out.flush();
    }
    return 0;
}
