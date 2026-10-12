#include "restorelists.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

#include "store.h"

namespace RestoreLists {

QVector<Info> listsIn(const Store &backup) {
    QVector<Info> out;
    QVector<TaskList> lists;
    for (const auto &l : backup.lists())
        if (!l.deleted && l.kind != ListKind::Smart)
            lists << l;
    std::sort(lists.begin(), lists.end(), [](const TaskList &a, const TaskList &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    for (const auto &l : std::as_const(lists)) {
        int tasks = 0;
        for (const auto &t : backup.tasks())
            if (t.listId == l.id && !t.deleted)
                ++tasks;
        out.append({l.uid, l.name, tasks});
    }
    return out;
}

Result merge(Store &into, const Store &backup, const QSet<QString> &listUids, const QString &attachmentsFolder) {
    Result result;
    QVector<TaskList> chosen;
    for (const auto &l : backup.lists())
        if (!l.deleted && l.kind != ListKind::Smart && listUids.contains(l.uid))
            chosen << l;
    if (chosen.isEmpty())
        return result;
    std::sort(chosen.begin(), chosen.end(), [](const TaskList &a, const TaskList &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });

    into.begin("Restore Lists");

    // The "Restored" group, reused when an earlier restore made it.
    qint64 groupId = 0;
    for (const auto &g : into.groups())
        if (!g.deleted && g.name == "Restored" && (!groupId || g.id < groupId))
            groupId = g.id;
    if (!groupId) {
        ListGroup g;
        g.id = into.nextId("list_groups");
        g.name = "Restored";
        double top = 0;
        for (const auto &o : into.groups())
            top = qMax(top, o.order);
        g.order = top + 1;
        into.putGroup(g);
        groupId = g.id;
    }

    double nextOrder = 0;
    for (const auto &l : into.lists())
        nextOrder = qMax(nextOrder, l.order);

    // Tags and locations are shared, so match them by name before making new ones.
    QHash<qint64, qint64> tagMap, locationMap;
    auto mapTag = [&](qint64 oldId) -> qint64 {
        if (tagMap.contains(oldId))
            return tagMap.value(oldId);
        const Tag old = backup.tag(oldId);
        if (!old.id || old.deleted)
            return tagMap[oldId] = 0;
        for (const auto &t : into.tags())
            if (!t.deleted && t.name.compare(old.name, Qt::CaseInsensitive) == 0)
                return tagMap[oldId] = t.id;
        Tag t;
        t.id = into.nextId("tags");
        t.name = old.name;
        t.paused = old.paused;
        t.order = old.order;
        into.putTag(t);
        return tagMap[oldId] = t.id;
    };
    auto mapLocation = [&](qint64 oldId) -> qint64 {
        if (locationMap.contains(oldId))
            return locationMap.value(oldId);
        Location old;
        for (const auto &l : backup.locations())
            if (l.id == oldId)
                old = l;
        if (!old.id || old.deleted)
            return locationMap[oldId] = 0;
        for (const auto &l : into.locations())
            if (!l.deleted && l.name.compare(old.name, Qt::CaseInsensitive) == 0)
                return locationMap[oldId] = l.id;
        Location l = old;
        l.id = into.nextId("locations");
        l.uid.clear();
        into.putLocation(l);
        return locationMap[oldId] = l.id;
    };

    QHash<QString, QString> seriesMap;
    for (const auto &oldList : std::as_const(chosen)) {
        TaskList list = oldList;
        list.id = into.nextId("lists");
        list.uid.clear();
        list.kind = ListKind::Normal;  // A restored Inbox must not become a second Inbox.
        list.groupId = groupId;
        list.order = ++nextOrder;
        list.archived = false;
        into.putList(list);
        ++result.lists;

        QHash<qint64, qint64> sectionMap;
        for (const auto &s : backup.sectionsOf(oldList.id)) {
            if (s.deleted)
                continue;
            ListSection copy = s;
            copy.id = into.nextId("sections");
            copy.uid.clear();
            copy.listId = list.id;
            into.putSection(copy);
            sectionMap.insert(s.id, copy.id);
        }

        // Hand out every task's new id first so parents can be named in any order.
        QVector<Task> tasks;
        QHash<qint64, qint64> taskMap;
        for (const auto &t : backup.tasks())
            if (t.listId == oldList.id && !t.deleted)
                tasks << t;
        std::sort(tasks.begin(), tasks.end(), [](const Task &a, const Task &b) { return a.id < b.id; });
        for (const auto &t : std::as_const(tasks))
            taskMap.insert(t.id, into.nextId("tasks"));

        for (const auto &old : std::as_const(tasks)) {
            Task t = old;
            t.id = taskMap.value(old.id);
            t.uid.clear();
            t.listId = list.id;
            t.parentId = taskMap.value(old.parentId, 0);
            t.sectionId = sectionMap.value(old.sectionId, 0);
            if (!old.seriesUid.isEmpty()) {
                if (!seriesMap.contains(old.seriesUid))
                    seriesMap.insert(old.seriesUid, Store::newUid());
                t.seriesUid = seriesMap.value(old.seriesUid);
            }
            t.tagIds.clear();
            for (qint64 id : old.tagIds)
                if (const qint64 mapped = mapTag(id))
                    t.tagIds << mapped;
            t.locationIds.clear();
            for (qint64 id : old.locationIds)
                if (const qint64 mapped = mapLocation(id))
                    t.locationIds << mapped;
            into.putTask(t);
            ++result.tasks;

            for (const auto &a : backup.alarmsFor(old.id)) {
                if (a.deleted)
                    continue;
                Alarm copy = a;
                copy.id = into.nextId("alarms");
                copy.uid.clear();
                copy.taskId = t.id;
                into.putAlarm(copy);
            }
            for (const auto &a : backup.attachmentsFor(old.id)) {
                if (a.deleted)
                    continue;
                Attachment copy = a;
                copy.id = into.nextId("attachments");
                copy.uid = Store::newUid();
                copy.taskId = t.id;
                const QString source = attachmentsFolder + "/" + a.fileName;
                if (!a.fileName.isEmpty() && QFile::exists(source)) {
                    copy.fileName = copy.uid + "." + QFileInfo(a.fileName).suffix();
                    QDir().mkpath(into.attachmentsFolder());
                    QFile::copy(source, into.attachmentsFolder() + "/" + copy.fileName);
                }
                into.putAttachment(copy);
            }
        }
    }
    into.commit();
    return result;
}

}  // namespace RestoreLists
