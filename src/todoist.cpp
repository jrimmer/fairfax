#include "todoist.h"
#include "keyring.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

#include "nlparse.h"
#include "store.h"
#include "synctree.h"

using TodoistMapping::Provider;

namespace {

const QString ETasks = QStringLiteral("tasks");
const QString ELists = QStringLiteral("lists");
const QString ETags = QStringLiteral("tags");
const QString MetaOpen = QStringLiteral("<Fairfax Meta>");
const QString MetaClose = QStringLiteral("</Fairfax Meta>");
constexpr int MaxCloses = 20;

QString newUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString keyOf(const QString &entity, qint64 id) { return entity + ':' + QString::number(id); }

QString jsonId(const QJsonValue &v) {
    if (v.isString())
        return v.toString();
    if (v.isNull() || v.isUndefined())
        return {};
    return QString::number(v.toVariant().toLongLong());
}

QVariantMap withoutModified(QVariantMap m) {
    m.remove("modified");
    return m;
}

// What counts as a change worth syncing: ordering, collapse state and
// bookkeeping stamps are local business.
QJsonObject projection(const QString &entity, QVariantMap m) {
    if (entity != ETasks) {
        QVariantMap p;
        p.insert("name", m.value("name"));
        p.insert("deleted", m.value("deleted"));
        return QJsonObject::fromVariantMap(p);
    }
    for (const char *k : {"modified", "sort_order", "collapsed", "created", "id", "location_ids"})
        m.remove(k);
    return QJsonObject::fromVariantMap(m);
}

bool differs(const QString &entity, const QVariantMap &current, const QVariantMap &snapshot) {
    return projection(entity, current) != projection(entity, snapshot);
}

QString hhmm(int minutes) { return QString("%1:%2").arg(minutes / 60, 2, 10, QChar('0')).arg(minutes % 60, 2, 10, QChar('0')); }

const char *const DayNames[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

QString ordinalWord(int n) {
    switch (n) {
    case -1: return "last";
    case 1: return "1st";
    case 2: return "2nd";
    case 3: return "3rd";
    default: return QString::number(n) + "th";
    }
}

// Strips the parts of a due string that are not the repeat rule itself.
QString ruleOnly(QString s) {
    s = s.toLower().trimmed();
    static const QRegularExpression tail(R"(\s+(starting|starts|ending|until|from|for)\b.*$)");
    static const QRegularExpression at(R"(\s+(at|@)\s+\d.*$)");
    s.remove(tail);
    s.remove(at);
    return s.simplified();
}

QString splitDescription(const QString &description, QJsonObject *meta, bool *present) {
    static const QRegularExpression re("<Fairfax Meta>(.*?)</Fairfax Meta>", QRegularExpression::DotMatchesEverythingOption);
    QString notes = description;
    *present = false;
    const auto m = re.match(description);
    if (m.hasMatch()) {
        const auto doc = QJsonDocument::fromJson(m.captured(1).toUtf8());
        if (doc.isObject()) {
            *meta = doc.object();
            *present = true;
        }
        notes.remove(m.capturedStart(), m.capturedLength());
    }
    while (notes.endsWith('\n') || notes.endsWith(' '))
        notes.chop(1);
    return notes;
}

QJsonObject recToJson(const Recurrence &r) {
    return {{"frequency", r.frequency}, {"interval", r.interval}, {"from", r.from}, {"weekdays", r.weekdays},
            {"monthOrdinal", r.monthOrdinal}, {"monthWeekday", r.monthWeekday}, {"endType", r.endType},
            {"endCount", r.endCount}, {"endDate", r.endDate.isValid() ? r.endDate.toString(Qt::ISODate) : QString()}};
}

Recurrence recFromJson(const QJsonObject &o) {
    Recurrence r;
    r.frequency = o.value("frequency").toInt();
    r.interval = qMax(1, o.value("interval").toInt(1));
    r.from = o.value("from").toInt();
    r.weekdays = o.value("weekdays").toInt();
    r.monthOrdinal = o.value("monthOrdinal").toInt();
    r.monthWeekday = o.value("monthWeekday").toInt();
    r.endType = o.value("endType").toInt();
    r.endCount = o.value("endCount").toInt();
    r.endDate = QDate::fromString(o.value("endDate").toString(), Qt::ISODate);
    return r;
}

// The description Todoist stores: notes, then the meta block with what
// Todoist has no field for.
QString descriptionFor(const Task &t) {
    QJsonObject meta;
    meta.insert("uid", t.uid);
    if (t.startDate.isValid())
        meta.insert("start_date", t.startDate.toString(Qt::ISODate));
    if (t.startTime >= 0)
        meta.insert("start_time", t.startTime);
    if (t.starred)
        meta.insert("starred", true);
    if (t.type != TaskType::Task)
        meta.insert("type", t.type);
    if (t.actionType != ActionType::None)
        meta.insert("action_type", t.actionType);
    if (!t.actionValue.isEmpty())
        meta.insert("action_value", t.actionValue);
    if (t.recurrence.isSet())
        meta.insert("rec", recToJson(t.recurrence));
    QString out = t.notes;
    while (out.endsWith('\n') || out.endsWith(' '))
        out.chop(1);
    if (!out.isEmpty())
        out += "\n\n";
    return out + MetaOpen + QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)) + MetaClose;
}

QJsonValue dueFor(const Task &t, bool allowRecurring) {
    if (!t.dueDate.isValid())
        return QJsonValue::Null;
    if (allowRecurring && t.recurrence.isSet()) {
        QString s = TodoistMapping::recurrenceString(t.recurrence);
        if (t.dueTime >= 0)
            s += " at " + hhmm(t.dueTime);
        if (t.dueDate != QDate::currentDate())
            s += " starting " + t.dueDate.toString(Qt::ISODate);
        if (t.recurrence.endType == Repeat::OnDate && t.recurrence.endDate.isValid())
            s += " until " + t.recurrence.endDate.toString(Qt::ISODate);
        return QJsonObject{{"string", s}};
    }
    QString d = t.dueDate.toString(Qt::ISODate);
    if (t.dueTime >= 0)
        d += "T" + hhmm(t.dueTime) + ":00";
    return QJsonObject{{"date", d}};
}

QJsonValue durationFor(const Task &t) {
    if (t.duration <= 0)
        return QJsonValue::Null;
    return QJsonObject{{"amount", t.duration}, {"unit", "minute"}};
}

QStringList labelNames(const Store &s, const Task &t) {
    QStringList names;
    for (qint64 id : t.tagIds) {
        const Tag tag = s.tag(id);
        if (tag.id && !tag.deleted && !tag.name.isEmpty())
            names << tag.name;
    }
    return names;
}

// ---------------------------------------------------------------------------
// Push

struct Builder {
    QJsonArray out;

    void add(const QString &type, const QJsonObject &args, const QString &entity, qint64 id, const QString &op,
             const QVariantMap &snapshot, const QString &remote = {}, const QString &temp = {},
             const QVariantMap &addSnapshot = {}) {
        QJsonObject local{{"entity", entity}, {"id", QString::number(id)}, {"key", keyOf(entity, id)}, {"op", op},
                          {"remote", remote}, {"snapshot", QJsonObject::fromVariantMap(snapshot)}};
        if (!addSnapshot.isEmpty())
            local.insert("addSnapshot", QJsonObject::fromVariantMap(addSnapshot));
        QJsonObject c{{"type", type}, {"uuid", newUuid()}, {"args", args}, {"_local", local}};
        if (!temp.isEmpty())
            c.insert("temp_id", temp);
        out.append(c);
    }
};

int depthOf(const Store &s, const Task &t) {
    int d = 0;
    qint64 p = t.parentId;
    while (p && d < 50) {
        const auto it = s.tasks().constFind(p);
        if (it == s.tasks().constEnd())
            break;
        p = it->parentId;
        ++d;
    }
    return d;
}

void buildSimple(Builder &b, const Store &s, const QString &entity, const QSet<QString> &skip) {
    const bool lists = entity == ELists;
    const auto links = s.syncLinks(Provider, entity);
    const QString addType = lists ? "project_add" : "label_add";
    const QString updType = lists ? "project_update" : "label_update";
    const QString delType = lists ? "project_delete" : "label_delete";

    QVector<qint64> ids;
    if (lists)
        for (const auto &l : s.lists())
            ids << l.id;
    else
        for (const auto &t : s.tags())
            ids << t.id;
    std::sort(ids.begin(), ids.end());

    const QSet<qint64> excluded = SyncTree::excludedListIds(s);
    for (qint64 id : std::as_const(ids)) {
        QVariantMap map;
        QString name;
        bool deleted = false;
        if (lists) {
            if (excluded.contains(id))
                continue;  // "Don't Sync This List": no project for it, no changes pushed.
            const TaskList l = s.list(id);
            if (l.kind == ListKind::Smart)
                continue;
            map = l.toMap();
            name = l.name;
            deleted = l.deleted;
            if (l.kind == ListKind::Inbox && !links.contains(id))
                continue;
            if (l.kind == ListKind::Inbox && deleted)
                continue;
        } else {
            const Tag t = s.tag(id);
            map = t.toMap();
            name = t.name;
            deleted = t.deleted;
        }
        const QString key = keyOf(entity, id);
        if (skip.contains(key))
            continue;
        const auto link = links.constFind(id);
        const QVariantMap snap = withoutModified(map);
        if (link == links.constEnd()) {
            if (!deleted && !name.trimmed().isEmpty())
                b.add(addType, {{"name", name}}, entity, id, "add", snap, {}, map.value("uid").toString());
        } else if (deleted) {
            b.add(delType, {{"id", link->remoteId}}, entity, id, "del", snap, link->remoteId);
        } else if (differs(entity, map, link->snapshot) && !(lists && s.list(id).kind == ListKind::Inbox)) {
            b.add(updType, {{"id", link->remoteId}, {"name", name}}, entity, id, "upd", snap, link->remoteId);
        }
    }
    // Linked records that no longer exist at all.
    QVector<qint64> orphans;
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        const bool exists = lists ? s.lists().contains(it.key()) : s.tags().contains(it.key());
        if (!exists)
            orphans << it.key();
    }
    std::sort(orphans.begin(), orphans.end());
    for (qint64 id : std::as_const(orphans))
        if (!skip.contains(keyOf(entity, id)))
            b.add(delType, {{"id", links.value(id).remoteId}}, entity, id, "del", {}, links.value(id).remoteId);
}

}  // namespace

QString TodoistMapping::recurrenceString(const Recurrence &r) {
    if (!r.isSet())
        return {};
    QString body;
    const int n = qMax(1, r.interval);
    switch (r.frequency) {
    case Repeat::Daily: body = n == 1 ? "day" : QString("%1 days").arg(n); break;
    case Repeat::Weekdays: body = "weekday"; break;
    case Repeat::Weekly:
        if (n == 1 && r.weekdays) {
            QStringList days;
            for (int i = 0; i < 7; ++i)
                if (r.weekdays & (1 << i))
                    days << DayNames[i];
            body = days.join(", ");
        } else {
            body = n == 1 ? "week" : QString("%1 weeks").arg(n);
        }
        break;
    case Repeat::Monthly:
        if (r.monthOrdinal != 0 && r.monthWeekday >= 1 && r.monthWeekday <= 7)
            body = ordinalWord(r.monthOrdinal) + " " + DayNames[r.monthWeekday - 1];
        else
            body = n == 1 ? "month" : QString("%1 months").arg(n);
        break;
    case Repeat::Yearly: body = n == 1 ? "year" : QString("%1 years").arg(n); break;
    default: return {};
    }
    return QString(r.from == Repeat::FromCompletion ? "every! " : "every ") + body;
}

QJsonArray TodoistMapping::buildCommands(const Store &s, const QSet<QString> &skipIn) {
    Builder b;
    QSet<QString> skip = skipIn;
    buildSimple(b, s, ELists, skip);
    buildSimple(b, s, ETags, skip);

    const auto listLinks = s.syncLinks(Provider, ELists);
    const auto taskLinks = s.syncLinks(Provider, ETasks);

    // Which tasks are about to be created, so children can name them by temp id.
    auto eligibleNew = [&](const Task &t) {
        if (t.deleted || taskLinks.contains(t.id) || t.title.trimmed().isEmpty())
            return false;
        if (t.completed && !t.seriesUid.isEmpty())
            return false;
        return true;
    };

    // Deletions first.
    QVector<qint64> gone;
    for (auto it = taskLinks.cbegin(); it != taskLinks.cend(); ++it) {
        const auto t = s.tasks().constFind(it.key());
        if (t == s.tasks().constEnd() || t->deleted)
            gone << it.key();
    }
    std::sort(gone.begin(), gone.end());
    for (qint64 id : std::as_const(gone)) {
        if (skip.contains(keyOf(ETasks, id)))
            continue;
        const auto t = s.tasks().constFind(id);
        if (t != s.tasks().constEnd() && SyncTree::excludedListIds(s).contains(t->listId))
            continue;
        b.add("item_delete", {{"id", taskLinks.value(id).remoteId}}, ETasks, id, "del",
              t == s.tasks().constEnd() ? QVariantMap() : withoutModified(t->toMap()), taskLinks.value(id).remoteId);
    }

    // Tasks of lists excluded from sync are neither created nor updated.
    const QSet<qint64> excludedLists = SyncTree::excludedListIds(s);
    QVector<Task> tasks;
    for (const auto &t : s.tasks())
        if (!t.deleted && !excludedLists.contains(t.listId))
            tasks << t;
    std::sort(tasks.begin(), tasks.end(), [&](const Task &x, const Task &y) {
        const int dx = depthOf(s, x), dy = depthOf(s, y);
        return dx != dy ? dx < dy : x.id < y.id;
    });

    // A reference to a project: its remote id, its temp id when it's being
    // created in this very batch, or nothing (Inbox). `blocked` is set when
    // the project failed to create, so its tasks wait.
    auto projectRef = [&](qint64 listId, bool *blocked) -> QString {
        *blocked = false;
        if (!listId)
            return {};
        const TaskList l = s.list(listId);
        if (!l.id || l.kind == ListKind::Inbox || l.kind == ListKind::Smart)
            return listLinks.contains(listId) ? listLinks.value(listId).remoteId : QString();
        if (listLinks.contains(listId))
            return listLinks.value(listId).remoteId;
        if (skip.contains(keyOf(ELists, listId)) || l.deleted || l.name.trimmed().isEmpty()) {
            *blocked = !l.deleted && !l.name.trimmed().isEmpty();
            return {};
        }
        return l.uid;
    };

    for (const Task &t : std::as_const(tasks)) {
        const QString key = keyOf(ETasks, t.id);
        if (skip.contains(key))
            continue;
        const QVariantMap map = t.toMap();
        const QVariantMap snapNow = withoutModified(map);
        const auto link = taskLinks.constFind(t.id);
        bool blocked = false;
        const QString project = projectRef(t.listId, &blocked);
        if (blocked) {
            skip.insert(key);
            continue;
        }
        QString parent;
        if (t.parentId) {
            const auto pl = taskLinks.constFind(t.parentId);
            if (pl != taskLinks.constEnd()) {
                parent = pl->remoteId;
            } else {
                const auto pt = s.tasks().constFind(t.parentId);
                if (pt != s.tasks().constEnd() && eligibleNew(*pt)) {
                    if (skip.contains(keyOf(ETasks, pt->id))) {
                        skip.insert(key);
                        continue;
                    }
                    parent = pt->uid;
                }
            }
        }

        if (link == taskLinks.constEnd()) {
            if (!eligibleNew(t))
                continue;
            QJsonObject args{{"content", t.title}, {"description", descriptionFor(t)}, {"priority", qBound(0, t.priority, 3) + 1}};
            if (!project.isEmpty())
                args.insert("project_id", project);
            if (!parent.isEmpty())
                args.insert("parent_id", parent);
            const auto labels = labelNames(s, t);
            if (!labels.isEmpty())
                args.insert("labels", QJsonArray::fromStringList(labels));
            const QJsonValue due = dueFor(t, !t.completed);
            if (!due.isNull())
                args.insert("due", due);
            if (t.duration > 0)
                args.insert("duration", durationFor(t));
            QVariantMap addSnap = snapNow;
            addSnap.insert("completed", false);
            addSnap.insert("completed_at", QVariant());
            b.add("item_add", args, ETasks, t.id, "add", snapNow, {}, t.uid, addSnap);
            if (t.completed)
                b.add("item_close", {{"id", t.uid}}, ETasks, t.id, "close", snapNow, {}, {}, addSnap);
            continue;
        }

        // Linked: only what changed since the snapshot.
        const QVariantMap &snap = link->snapshot;
        if (!differs(ETasks, map, snap))
            continue;
        const QString rid = link->remoteId;
        const bool wasDone = snap.value("completed").toBool();
        const int doneBefore = snap.value("rec_done").toInt();
        const bool advanced = t.recurrence.isSet() && !t.completed && t.recurrence.occurrencesDone > doneBefore;

        if (wasDone && !t.completed)
            b.add("item_uncomplete", {{"id", rid}}, ETasks, t.id, "uncomplete", snapNow, rid);

        const bool moved = map.value("list_id") != snap.value("list_id") || map.value("parent_id") != snap.value("parent_id");
        if (moved && !t.completed) {
            if (!parent.isEmpty())
                b.add("item_move", {{"id", rid}, {"parent_id", parent}}, ETasks, t.id, "move", snapNow, rid);
            else if (!project.isEmpty())
                b.add("item_move", {{"id", rid}, {"project_id", project}}, ETasks, t.id, "move", snapNow, rid);
        }

        // Content: compare without the completion fields (and without the
        // due date when the server is about to advance it itself).
        QVariantMap a = map, c = snap;
        for (const char *k : {"completed", "completed_at", "rec_done"}) {
            a.remove(k);
            c.remove(k);
        }
        if (advanced)
            for (const char *k : {"due_date", "due_time"}) {
                a.remove(k);
                c.remove(k);
            }
        if (!(wasDone && t.completed) && differs(ETasks, a, c)) {
            QJsonObject args{{"id", rid}, {"content", t.title}, {"description", descriptionFor(t)},
                             {"priority", qBound(0, t.priority, 3) + 1}, {"labels", QJsonArray::fromStringList(labelNames(s, t))},
                             {"duration", durationFor(t)}};
            if (!advanced)
                args.insert("due", dueFor(t, !t.completed));
            b.add("item_update", args, ETasks, t.id, "upd", snapNow, rid);
        }

        if (!wasDone && t.completed) {
            if (t.recurrence.isSet())  // The series ended: stop the server repeating it first.
                b.add("item_update", {{"id", rid}, {"due", dueFor(t, false)}}, ETasks, t.id, "upd", snapNow, rid);
            b.add("item_close", {{"id", rid}}, ETasks, t.id, "close", snapNow, rid);
        } else if (advanced) {
            const int n = qMin(MaxCloses, t.recurrence.occurrencesDone - doneBefore);
            for (int i = 0; i < n; ++i)
                b.add("item_close", {{"id", rid}}, ETasks, t.id, "close", snapNow, rid);
        }
    }
    return b.out;
}

QJsonArray TodoistMapping::firstBatch(const QJsonArray &commands, int max) {
    QJsonArray out;
    int i = 0;
    while (i < commands.size()) {
        const QString key = commands[i].toObject().value("_local").toObject().value("key").toString();
        int j = i;
        while (j < commands.size() && commands[j].toObject().value("_local").toObject().value("key").toString() == key)
            ++j;
        if (!out.isEmpty() && out.size() + (j - i) > max)
            break;
        for (int k = i; k < j; ++k)
            out.append(commands[k]);
        if (out.size() >= max)
            break;
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pull

namespace {

QDateTime parseStamp(const QString &s) {
    if (s.isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid()) {  // Over-long fractions.
        QString t = s;
        static const QRegularExpression frac(R"(\.(\d{3})\d+)");
        t.replace(frac, ".\\1");
        dt = QDateTime::fromString(t, Qt::ISODateWithMs);
    }
    return dt;
}

bool remoteNewer(const QDateTime &local, const QString &updatedAt) {
    const QDateTime r = parseStamp(updatedAt);
    if (!r.isValid())
        return false;
    return !local.isValid() || r > local;
}

struct Pull {
    Store &store;
    QHash<qint64, Store::SyncLink> taskLinks, listLinks, tagLinks;
    QHash<QString, qint64> taskByRemote, listByRemote, tagByRemote;
    QSet<QString> justPushed;
    QSet<qint64> excluded;  // Lists the user keeps out of sync.
    int in = 0;

    explicit Pull(Store &s) : store(s), excluded(SyncTree::excludedListIds(s)) { reload(); }

    void reload() {
        taskLinks = store.syncLinks(Provider, ETasks);
        listLinks = store.syncLinks(Provider, ELists);
        tagLinks = store.syncLinks(Provider, ETags);
        taskByRemote.clear();
        listByRemote.clear();
        tagByRemote.clear();
        for (const auto &l : taskLinks) taskByRemote.insert(l.remoteId, l.localId);
        for (const auto &l : listLinks) listByRemote.insert(l.remoteId, l.localId);
        for (const auto &l : tagLinks) tagByRemote.insert(l.remoteId, l.localId);
    }

    void link(const QString &entity, qint64 id, const QString &remote, const QVariantMap &snapshot) {
        const Store::SyncLink l{id, remote, snapshot};
        store.setSyncLink(Provider, entity, l);
        auto &links = entity == ETasks ? taskLinks : entity == ELists ? listLinks : tagLinks;
        auto &byRemote = entity == ETasks ? taskByRemote : entity == ELists ? listByRemote : tagByRemote;
        links.insert(id, l);
        byRemote.insert(remote, id);
    }

    void unlink(const QString &entity, qint64 id) {
        auto &links = entity == ETasks ? taskLinks : entity == ELists ? listLinks : tagLinks;
        auto &byRemote = entity == ETasks ? taskByRemote : entity == ELists ? listByRemote : tagByRemote;
        byRemote.remove(links.value(id).remoteId);
        links.remove(id);
        store.removeSyncLink(Provider, entity, id);
    }

    qint64 inboxId() const {
        qint64 best = 0;
        for (const auto &l : store.lists())
            if (l.kind == ListKind::Inbox && !l.deleted && (!best || l.id < best))
                best = l.id;
        return best;
    }

    void projects(const QJsonArray &incoming) {
        for (const auto &v : incoming) {
            const QJsonObject p = v.toObject();
            const QString rid = jsonId(p.value("id"));
            if (rid.isEmpty() || justPushed.contains("p" + rid))
                continue;
            const bool deleted = p.value("is_deleted").toBool();
            const QString name = p.value("name").toString();
            qint64 id = listByRemote.value(rid, 0);
            if (id && excluded.contains(id))
                continue;  // A list kept out of sync is left alone.
            bool adopt = false;
            if (!id && !deleted) {
                if (p.value("is_inbox_project").toBool()) {
                    for (const auto &l : store.lists())
                        if (l.kind == ListKind::Inbox && !l.deleted && !listLinks.contains(l.id) && !excluded.contains(l.id) && (!id || l.id < id))
                            id = l.id;
                } else {
                    for (const auto &l : store.lists())
                        if (l.kind == ListKind::Normal && !l.deleted && !listLinks.contains(l.id) && !excluded.contains(l.id) && l.name == name && (!id || l.id < id))
                            id = l.id;
                }
                adopt = id != 0;
            }
            if (!id) {
                if (deleted)
                    continue;
                TaskList l;
                l.id = store.nextId("lists");
                l.kind = p.value("is_inbox_project").toBool() ? ListKind::Inbox : ListKind::Normal;
                l.name = name;
                l.order = p.value("child_order").toDouble();
                l.modified = QDateTime::currentDateTime();
                store.putList(l);
                link(ELists, l.id, rid, withoutModified(store.list(l.id).toMap()));
                ++in;
                continue;
            }
            TaskList l = store.list(id);
            if (deleted) {
                if (!l.deleted) {
                    l.deleted = true;
                    l.modified = QDateTime::currentDateTime();
                    store.putList(l);
                    for (Task t : store.tasks())
                        if (t.listId == id && !t.deleted) {
                            t.deleted = true;
                            t.modified = QDateTime::currentDateTime();
                            store.putTask(t);
                            if (taskLinks.contains(t.id))
                                unlink(ETasks, t.id);
                        }
                    ++in;
                }
                unlink(ELists, id);
                continue;
            }
            const auto lk = listLinks.constFind(id);
            const bool locallyChanged = lk != listLinks.constEnd() && differs(ELists, l.toMap(), lk->snapshot);
            const bool wins = !locallyChanged || remoteNewer(l.modified, p.value("updated_at").toString());
            if (adopt && l.name == name) {
                link(ELists, id, rid, withoutModified(l.toMap()));
                continue;
            }
            if (l.kind != ListKind::Inbox && l.name != name && (wins && !adopt)) {
                l.name = name;
                const QDateTime u = parseStamp(p.value("updated_at").toString());
                l.modified = u.isValid() ? u : QDateTime::currentDateTime();
                store.putList(l);
                ++in;
                link(ELists, id, rid, withoutModified(store.list(id).toMap()));
            } else if (adopt) {
                link(ELists, id, rid, {});
            } else if (lk == listLinks.constEnd()) {
                link(ELists, id, rid, withoutModified(l.toMap()));
            }
        }
    }

    void labels(const QJsonArray &incoming) {
        for (const auto &v : incoming) {
            const QJsonObject o = v.toObject();
            const QString rid = jsonId(o.value("id"));
            if (rid.isEmpty() || justPushed.contains("l" + rid))
                continue;
            const bool deleted = o.value("is_deleted").toBool();
            const QString name = o.value("name").toString();
            qint64 id = tagByRemote.value(rid, 0);
            bool adopt = false;
            if (!id && !deleted) {
                for (const auto &t : store.tags())
                    if (!t.deleted && !tagLinks.contains(t.id) && t.name.compare(name, Qt::CaseInsensitive) == 0 && (!id || t.id < id))
                        id = t.id;
                adopt = id != 0;
            }
            if (!id) {
                if (deleted)
                    continue;
                Tag t;
                t.id = store.nextId("tags");
                t.name = name;
                t.order = o.value("item_order").toDouble();
                t.modified = QDateTime::currentDateTime();
                store.putTag(t);
                link(ETags, t.id, rid, withoutModified(store.tag(t.id).toMap()));
                ++in;
                continue;
            }
            Tag t = store.tag(id);
            if (deleted) {
                if (!t.deleted) {
                    t.deleted = true;
                    t.modified = QDateTime::currentDateTime();
                    store.putTag(t);
                    ++in;
                }
                unlink(ETags, id);
                continue;
            }
            const auto lk = tagLinks.constFind(id);
            const bool locallyChanged = lk != tagLinks.constEnd() && differs(ETags, t.toMap(), lk->snapshot);
            if (adopt) {
                link(ETags, id, rid, t.name == name ? withoutModified(t.toMap()) : QVariantMap());
            } else if (!locallyChanged) {
                if (t.name != name) {
                    t.name = name;
                    t.modified = QDateTime::currentDateTime();
                    store.putTag(t);
                    ++in;
                }
                link(ETags, id, rid, withoutModified(store.tag(id).toMap()));
            }
        }
    }

    qint64 tagFor(const QString &name) {
        for (const auto &t : store.tags())
            if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0)
                return t.id;
        Tag t;
        t.id = store.nextId("tags");
        t.name = name;
        t.modified = QDateTime::currentDateTime();
        store.putTag(t);
        return t.id;
    }

    // Writes Todoist's view of an item onto `t`, keeping whatever Todoist
    // can't express exactly as it was.
    void fill(Task &t, const QJsonObject &item, const QJsonObject &meta, bool metaPresent, const QString &notes) {
        t.title = item.value("content").toString();
        if (t.notes.trimmed() != notes.trimmed())
            t.notes = notes;
        if (metaPresent) {
            t.startDate = QDate::fromString(meta.value("start_date").toString(), Qt::ISODate);
            t.startTime = meta.value("start_time").toInt(-1);
            t.starred = meta.value("starred").toBool();
            t.type = meta.value("type").toInt(TaskType::Task);
            t.actionType = meta.value("action_type").toInt();
            t.actionValue = meta.value("action_value").toString();
        }
        t.priority = qBound(0, item.value("priority").toInt(1) - 1, 3);
        t.order = item.value("child_order").toDouble(t.order);

        const QJsonObject dur = item.value("duration").toObject();
        if (item.value("duration").isObject() && dur.value("amount").toInt() > 0)
            t.duration = dur.value("amount").toInt() * (dur.value("unit").toString() == "day" ? 1440 : 1);
        else
            t.duration = 0;

        const QJsonValue dueV = item.value("due");
        if (!dueV.isObject()) {
            t.dueDate = QDate();
            t.dueTime = -1;
            t.recurrence = Recurrence();
        } else {
            const QJsonObject due = dueV.toObject();
            const QString ds = due.value("date").toString();
            if (ds.contains('T')) {
                QDateTime dt = QDateTime::fromString(ds.left(19), Qt::ISODate);
                if (ds.endsWith('Z')) {
                    dt.setTimeSpec(Qt::UTC);
                    dt = dt.toLocalTime();
                }
                t.dueDate = dt.date();
                t.dueTime = dt.time().hour() * 60 + dt.time().minute();
            } else {
                t.dueDate = QDate::fromString(ds.left(10), Qt::ISODate);
                t.dueTime = -1;
            }
            if (due.value("is_recurring").toBool()) {
                const QString rule = ruleOnly(due.value("string").toString());
                const int done = t.recurrence.occurrencesDone;
                Recurrence metaRec = recFromJson(meta.value("rec").toObject());
                if (metaPresent && metaRec.isSet() && TodoistMapping::recurrenceString(metaRec) == rule) {
                    t.recurrence = metaRec;
                } else if (t.recurrence.isSet() && TodoistMapping::recurrenceString(t.recurrence) == rule) {
                    // Unchanged.
                } else {
                    // The parser doesn't know Todoist's "every!" (repeat from completion).
                    QString plain = rule;
                    const bool fromCompletion = plain.startsWith("every!");
                    if (fromCompletion)
                        plain.replace(0, 6, "every");
                    t.recurrence = NaturalLanguage::parse(plain, QDateTime::currentDateTime()).recurrence;
                    if (t.recurrence.isSet() && fromCompletion)
                        t.recurrence.from = Repeat::FromCompletion;
                }
                t.recurrence.occurrencesDone = done;
            } else {
                t.recurrence = Recurrence();
            }
        }

        const bool checked = item.value("checked").toBool();
        if (checked && !t.completed) {
            const QDateTime c = parseStamp(item.value("completed_at").toString());
            t.completedAt = c.isValid() ? c.toLocalTime() : QDateTime::currentDateTime();
        } else if (!checked) {
            t.completedAt = QDateTime();
        }
        t.completed = checked;

        const QString pid = jsonId(item.value("project_id"));
        t.listId = listByRemote.contains(pid) ? listByRemote.value(pid) : (t.listId ? t.listId : inboxId());
        const QString par = jsonId(item.value("parent_id"));
        t.parentId = par.isEmpty() ? 0 : taskByRemote.value(par, 0);

        QList<qint64> tags;
        for (const auto &n : item.value("labels").toArray())
            tags << tagFor(n.toString());
        t.tagIds = tags;
    }

    void items(const QJsonArray &incoming) {
        QSet<QString> pending;
        for (const auto &v : incoming)
            pending.insert(jsonId(v.toObject().value("id")));
        QJsonArray rest = incoming;
        while (!rest.isEmpty()) {
            QJsonArray next;
            for (const auto &v : std::as_const(rest)) {
                const QJsonObject item = v.toObject();
                const QString parent = jsonId(item.value("parent_id"));
                if (!parent.isEmpty() && pending.contains(parent) && parent != jsonId(item.value("id")))
                    next.append(v);
                else {
                    item1(item);
                    pending.remove(jsonId(item.value("id")));
                }
            }
            if (next.size() == rest.size()) {
                for (const auto &v : std::as_const(next))
                    item1(v.toObject());
                break;
            }
            rest = next;
        }
    }

    void item1(const QJsonObject &item) {
        const QString rid = jsonId(item.value("id"));
        if (rid.isEmpty() || justPushed.contains("i" + rid))
            return;
        const bool deleted = item.value("is_deleted").toBool();
        QJsonObject meta;
        bool metaPresent = false;
        const QString notes = splitDescription(item.value("description").toString(), &meta, &metaPresent);
        const QString metaUid = meta.value("uid").toString();

        qint64 id = taskByRemote.value(rid, 0);
        bool adopt = false;
        if (!id && !metaUid.isEmpty() && !deleted) {
            for (const auto &t : store.tasks())
                if (t.uid == metaUid && !t.deleted && !taskLinks.contains(t.id)) {
                    id = t.id;
                    adopt = true;
                    break;
                }
        }
        const QDateTime updated = parseStamp(item.value("updated_at").toString());

        // Tasks in a list kept out of sync are left alone, wherever they are.
        if (id && excluded.contains(store.task(id).listId))
            return;
        if (!id && excluded.contains(listByRemote.value(jsonId(item.value("project_id")), 0)))
            return;

        if (!id) {
            if (deleted)
                return;
            Task t;
            t.id = store.nextId("tasks");
            bool uidTaken = metaUid.isEmpty();
            for (const auto &o : store.tasks())
                if (o.uid == metaUid) {
                    uidTaken = true;
                    break;
                }
            t.uid = uidTaken ? Store::newUid() : metaUid;
            const QDateTime added = parseStamp(item.value("added_at").toString());
            t.created = added.isValid() ? added.toLocalTime() : QDateTime::currentDateTime();
            fill(t, item, meta, metaPresent, notes);
            t.modified = updated.isValid() ? updated.toLocalTime() : QDateTime::currentDateTime();
            store.putTask(t);
            link(ETasks, t.id, rid, withoutModified(store.task(t.id).toMap()));
            ++in;
            return;
        }

        Task local = store.task(id);
        if (local.deleted)
            return;  // Its deletion is waiting to be pushed.
        if (deleted) {
            local.deleted = true;
            local.modified = updated.isValid() ? updated.toLocalTime() : QDateTime::currentDateTime();
            store.putTask(local);
            unlink(ETasks, id);
            ++in;
            return;
        }
        Task merged = local;
        fill(merged, item, meta, metaPresent, notes);
        const bool same = projection(ETasks, merged.toMap()) == projection(ETasks, local.toMap());
        const auto lk = taskLinks.constFind(id);
        const bool locallyChanged = adopt || (lk != taskLinks.constEnd() && differs(ETasks, local.toMap(), lk->snapshot));
        if (same) {
            link(ETasks, id, rid, withoutModified(local.toMap()));
            return;
        }
        if (locallyChanged && !remoteNewer(local.modified, item.value("updated_at").toString())) {
            if (adopt)
                link(ETasks, id, rid, {});
            return;  // Local wins; it is pushed on the next round.
        }
        merged.modified = updated.isValid() ? updated.toLocalTime() : QDateTime::currentDateTime();
        store.putTask(merged);
        link(ETasks, id, rid, withoutModified(store.task(id).toMap()));
        ++in;
    }
};

}  // namespace

TodoistMapping::ApplyResult TodoistMapping::apply(Store &store, const QJsonArray &sent, const QJsonObject &resp) {
    ApplyResult result;
    const QJsonObject status = resp.value("sync_status").toObject();
    const QJsonObject mapping = resp.value("temp_id_mapping").toObject();
    Pull pull(store);

    struct Group {
        bool ok = true;
        QJsonObject last;
        QString remote;
        QString entity;
        qint64 id = 0;
        QString op;
    };
    QMap<QString, Group> groups;
    QStringList order;
    for (const auto &cv : sent) {
        const QJsonObject c = cv.toObject();
        const QJsonObject loc = c.value("_local").toObject();
        const QString key = loc.value("key").toString();
        if (!groups.contains(key))
            order << key;
        Group &g = groups[key];
        g.entity = loc.value("entity").toString();
        g.id = loc.value("id").toString().toLongLong();
        const QString type = c.value("type").toString();
        g.op = loc.value("op").toString() == "del" ? "del" : (g.op == "add" ? g.op : loc.value("op").toString());
        const QJsonValue st = status.value(c.value("uuid").toString());
        const bool del = loc.value("op").toString() == "del";
        const bool ok = (st.isString() && st.toString() == "ok") || (del && st.isObject() && st.toObject().value("http_code").toInt() == 404);
        if (!ok) {
            g.ok = false;
            if (result.errors.size() < 5)
                result.errors << QString("%1 failed: %2").arg(type, st.isObject() ? st.toObject().value("error").toString() : QStringLiteral("no status"));
            continue;
        }
        g.last = loc;
        g.remote = loc.value("remote").toString();
        if (type.endsWith("_add")) {
            g.remote = jsonId(mapping.value(c.value("temp_id").toString()));
            const bool exists = g.entity == ETasks ? store.tasks().contains(g.id)
                                : g.entity == ELists ? store.lists().contains(g.id) : store.tags().contains(g.id);
            if (!g.remote.isEmpty() && exists) {
                // Linked at once, so a later failure in this record's commands can never cause a duplicate.
                const QJsonObject add = loc.contains("addSnapshot") ? loc.value("addSnapshot").toObject() : loc.value("snapshot").toObject();
                pull.link(g.entity, g.id, g.remote, add.toVariantMap());
            }
        }
    }
    for (const QString &key : std::as_const(order)) {
        const Group &g = groups[key];
        if (!g.ok || (g.op != "del" && g.remote.isEmpty())) {
            result.failed.insert(key);
            continue;
        }
        ++result.out;
        if (g.op == "del") {
            pull.unlink(g.entity, g.id);
            continue;
        }
        const char prefix = g.entity == ETasks ? 'i' : g.entity == ELists ? 'p' : 'l';
        pull.justPushed.insert(QString(prefix) + g.remote);
        pull.link(g.entity, g.id, g.remote, g.last.value("snapshot").toObject().toVariantMap());
    }

    store.begin("Todoist sync", false, true);
    pull.projects(resp.value("projects").toArray());
    pull.labels(resp.value("labels").toArray());
    pull.items(resp.value("items").toArray());
    store.commit();
    result.in = pull.in;
    return result;
}

QString TodoistMapping::applyResponse(Store &store, const QJsonArray &sent, const QJsonObject &resp, QString *error) {
    const ApplyResult r = apply(store, sent, resp);
    if (error)
        *error = r.errors.value(0);
    return QString("%1 in, %2 out").arg(r.in).arg(r.out);
}

// ---------------------------------------------------------------------------
// TodoistSync

namespace {

const char *const SyncTokenSetting = "todoist.syncToken";

struct Secret {
    bool missing = false;
    int code = -1;
    QString out;
};

Secret runSecret(const QStringList &args, const QByteArray &input = {}) {
    Secret s;
    QProcess p;
    p.start("secret-tool", args);
    if (!p.waitForStarted(3000)) {
        s.missing = true;
        return s;
    }
    if (!input.isEmpty())
        p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(15000)) {
        p.kill();
        return s;
    }
    s.code = p.exitCode();
    s.out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    return s;
}

}  // namespace

TodoistSync::TodoistSync(Store *store, QObject *parent) : QObject(parent), m_store(store) {
    m_timer.setInterval(5 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (connected() && SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    m_timer.start();
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(20 * 1000);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (connected() && SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    connect(store, &Store::changed, this, [this] {
        if (m_applying || !m_tokenLooked || m_cachedToken.isEmpty() || !SyncTree::autoSyncEnabled(*m_store))
            return;
        m_debounce.start();
    });
}

QString TodoistSync::token() const {
    if (!m_tokenLooked) {
        m_tokenLooked = true;
        m_cachedToken = Keyring::lookup("fairfax", "todoist");
    }
    return m_cachedToken;
}

bool TodoistSync::connected() const { return !token().isEmpty(); }

QDateTime TodoistSync::lastSync() const {
    return QDateTime::fromString(m_store->setting("todoist.lastSync").toString(), Qt::ISODate);
}

void TodoistSync::setTransport(Transport transport, const QString &token) {
    m_transport = std::move(transport);
    m_cachedToken = token;
    m_tokenLooked = true;
    emit stateChanged();
}

void TodoistSync::setStatus(const QString &status) {
    m_status = status;
    emit stateChanged();
}

void TodoistSync::connectAccount(const QString &tokenText) {
    const QString t = tokenText.trimmed();
    if (t.isEmpty()) {
        setStatus("Paste your Todoist API token first");
        return;
    }
    QString error;
    if (!Keyring::store("fairfax", "todoist", t, &error)) {
        setStatus(error);
        return;
    }
    m_cachedToken = t;
    m_tokenLooked = true;
    m_store->setSetting(SyncTokenSetting, QString());
    m_store->clearSyncLinks(Provider);
    emit stateChanged();
    syncNow();
}

void TodoistSync::disconnectAccount() {
    if (!m_transport)
        Keyring::clear("fairfax", "todoist");
    m_cachedToken.clear();
    m_tokenLooked = true;
    m_store->setSetting(SyncTokenSetting, QString());
    m_store->clearSyncLinks(Provider);
    m_store->setSetting("todoist.lastSync", QString());
    m_status = "Not connected";
    emit stateChanged();
}

void TodoistSync::finish(const QString &status) {
    m_busy = false;
    m_status = status;
    emit stateChanged();
}

void TodoistSync::syncNow() {
    if (m_busy)
        return;
    if (!connected()) {
        setStatus(m_tokenLooked && !m_transport ? "Not connected to Todoist" : "Not connected");
        return;
    }
    m_busy = true;
    m_phase = 0;
    m_rounds = 0;
    m_in = m_out = 0;
    m_failed.clear();
    m_sent = QJsonArray();
    m_firstError.clear();
    m_status = "Syncing with Todoist…";
    emit stateChanged();
    step();
}

void TodoistSync::step() {
    if (++m_rounds > 60) {
        finish("Todoist sync stopped: too many rounds");
        return;
    }
    QJsonArray batch;
    if (m_phase == 1) {
        // Pull before pushing, so records another machine already created
        // are adopted instead of duplicated.
        batch = TodoistMapping::firstBatch(TodoistMapping::buildCommands(*m_store, m_failed));
        if (batch.isEmpty()) {
            QString s = QString("Synced: %1 in, %2 out").arg(m_in).arg(m_out);
            if (!m_firstError.isEmpty())
                s += " (" + m_firstError + ")";
            m_store->setSetting("todoist.lastSync", QDateTime::currentDateTime().toString(Qt::ISODate));
            finish(s);
            emit synced(s);
            return;
        }
    }
    m_sent = batch;
    QJsonArray wire;
    for (const auto &cv : batch) {
        QJsonObject c = cv.toObject();
        c.remove("_local");
        wire.append(c);
    }
    QUrlQuery form;
    form.addQueryItem("sync_token", m_store->setting(SyncTokenSetting).toString().isEmpty() ? "*" : m_store->setting(SyncTokenSetting).toString());
    form.addQueryItem("resource_types", R"(["projects","items","labels"])");
    if (!wire.isEmpty())
        form.addQueryItem("commands", QString::fromUtf8(QJsonDocument(wire).toJson(QJsonDocument::Compact)));
    send(form, [this](const QJsonObject &reply) {
        m_applying = true;
        const auto r = TodoistMapping::apply(*m_store, m_sent, reply);
        m_applying = false;
        const QString tok = reply.value("sync_token").toString();
        if (!tok.isEmpty())
            m_store->setSetting(SyncTokenSetting, tok);
        m_in += r.in;
        m_out += r.out;
        m_failed.unite(r.failed);
        if (m_firstError.isEmpty() && !r.errors.isEmpty())
            m_firstError = r.errors.first();
        m_phase = 1;
        step();
    });
}

void TodoistSync::send(const QUrlQuery &form, std::function<void(const QJsonObject &)> done) {
    m_form = form;
    m_done = std::move(done);
    m_retries = 0;
    dispatch();
}

void TodoistSync::dispatch() {
    if (m_transport) {
        handle(m_transport(m_form));
        return;
    }
    QByteArray body;
    for (const auto &item : m_form.queryItems(QUrl::FullyDecoded)) {
        if (!body.isEmpty())
            body += '&';
        body += QUrl::toPercentEncoding(item.first) + '=' + QUrl::toPercentEncoding(item.second);
    }
    QNetworkRequest req(QUrl("https://api.todoist.com/api/v1/sync"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setRawHeader("Authorization", "Bearer " + token().toUtf8());
    req.setTransferTimeout(60000);
    QNetworkReply *reply = m_network.post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (code == 0) {
            obj = QJsonObject{{"http_status", 0}, {"error", reply->errorString()}};
        } else if (code != 200) {
            obj.insert("http_status", code);
            if (!obj.contains("retry_after") && reply->hasRawHeader("Retry-After"))
                obj.insert("retry_after", reply->rawHeader("Retry-After").toInt());
            if (!obj.contains("error"))
                obj.insert("error", reply->errorString());
        }
        handle(obj);
    });
}

void TodoistSync::handle(const QJsonObject &reply) {
    const int http = reply.contains("http_status") ? reply.value("http_status").toInt() : 200;
    if (http == 200 || http == 204) {
        auto done = std::move(m_done);
        done(reply);
        return;
    }
    if (http == 401 || http == 403) {
        finish(QString("Todoist rejected the token (%1); reconnect with a new token").arg(http));
        return;
    }
    if (http == 429) {
        int wait = reply.value("retry_after").toInt(-1);
        if (wait < 0)
            wait = reply.value("error_extra").toObject().value("retry_after").toInt(1 << qMin(m_retries + 1, 6));
        wait = qBound(0, wait, 300);
        if (++m_retries > 3) {
            finish("Todoist is rate limiting requests; will try again later");
            return;
        }
        setStatus(QString("Todoist rate limit; retrying in %1 s").arg(wait));
        QTimer::singleShot(wait * 1000, this, [this] { dispatch(); });
        return;
    }
    if (http == 0) {
        finish("Todoist unreachable: " + reply.value("error").toString());
        return;
    }
    finish(QString("Todoist error %1: %2").arg(http).arg(reply.value("error").toString()));
}
