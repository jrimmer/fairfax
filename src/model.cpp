#include "model.h"

#include <QStringList>

namespace {

QVariant dateValue(const QDate &date) { return date.isValid() ? QVariant(date.toString(Qt::ISODate)) : QVariant(); }
QDate dateFrom(const QVariant &value) { return value.isNull() ? QDate() : QDate::fromString(value.toString(), Qt::ISODate); }
QVariant stampValue(const QDateTime &stamp) { return stamp.isValid() ? QVariant(stamp.toSecsSinceEpoch()) : QVariant(); }
QDateTime stampFrom(const QVariant &value) {
    return value.isNull() ? QDateTime() : QDateTime::fromSecsSinceEpoch(value.toLongLong());
}

QString idsToString(const QList<qint64> &ids) {
    QStringList parts;
    for (auto id : ids)
        parts << QString::number(id);
    return parts.join(',');
}

QList<qint64> idsFromString(const QString &text) {
    QList<qint64> ids;
    for (const auto &part : text.split(',', Qt::SkipEmptyParts))
        ids << part.toLongLong();
    return ids;
}

}  // namespace

QVariantMap Task::toMap() const {
    return {
        {"id", id},
        {"uid", uid},
        {"list_id", listId},
        {"parent_id", parentId},
        {"type", type},
        {"title", title},
        {"notes", notes},
        {"priority", priority},
        {"starred", starred},
        {"start_date", dateValue(startDate)},
        {"start_time", startTime},
        {"due_date", dateValue(dueDate)},
        {"due_time", dueTime},
        {"duration", duration},
        {"completed", completed},
        {"completed_at", stampValue(completedAt)},
        {"action_type", actionType},
        {"action_value", actionValue},
        {"rec_frequency", recurrence.frequency},
        {"rec_interval", recurrence.interval},
        {"rec_from", recurrence.from},
        {"rec_weekdays", recurrence.weekdays},
        {"rec_month_ordinal", recurrence.monthOrdinal},
        {"rec_month_weekday", recurrence.monthWeekday},
        {"rec_end_type", recurrence.endType},
        {"rec_end_count", recurrence.endCount},
        {"rec_end_date", dateValue(recurrence.endDate)},
        {"rec_done", recurrence.occurrencesDone},
        {"series_uid", seriesUid},
        {"sort_order", order},
        {"collapsed", collapsed},
        {"tag_ids", idsToString(tagIds)},
        {"location_ids", idsToString(locationIds)},
        {"created", stampValue(created)},
        {"modified", stampValue(modified)},
        {"deleted", deleted},
    };
}

Task Task::fromMap(const QVariantMap &m) {
    Task t;
    t.id = m.value("id").toLongLong();
    t.uid = m.value("uid").toString();
    t.listId = m.value("list_id").toLongLong();
    t.parentId = m.value("parent_id").toLongLong();
    t.type = m.value("type").toInt();
    t.title = m.value("title").toString();
    t.notes = m.value("notes").toString();
    t.priority = m.value("priority").toInt();
    t.starred = m.value("starred").toBool();
    t.startDate = dateFrom(m.value("start_date"));
    t.startTime = m.value("start_time", -1).toInt();
    t.dueDate = dateFrom(m.value("due_date"));
    t.dueTime = m.value("due_time", -1).toInt();
    t.duration = m.value("duration").toInt();
    t.completed = m.value("completed").toBool();
    t.completedAt = stampFrom(m.value("completed_at"));
    t.actionType = m.value("action_type").toInt();
    t.actionValue = m.value("action_value").toString();
    t.recurrence.frequency = m.value("rec_frequency").toInt();
    t.recurrence.interval = qMax(1, m.value("rec_interval", 1).toInt());
    t.recurrence.from = m.value("rec_from").toInt();
    t.recurrence.weekdays = m.value("rec_weekdays").toInt();
    t.recurrence.monthOrdinal = m.value("rec_month_ordinal").toInt();
    t.recurrence.monthWeekday = m.value("rec_month_weekday").toInt();
    t.recurrence.endType = m.value("rec_end_type").toInt();
    t.recurrence.endCount = m.value("rec_end_count").toInt();
    t.recurrence.endDate = dateFrom(m.value("rec_end_date"));
    t.recurrence.occurrencesDone = m.value("rec_done").toInt();
    t.seriesUid = m.value("series_uid").toString();
    t.order = m.value("sort_order").toDouble();
    t.collapsed = m.value("collapsed").toBool();
    t.tagIds = idsFromString(m.value("tag_ids").toString());
    t.locationIds = idsFromString(m.value("location_ids").toString());
    t.created = stampFrom(m.value("created"));
    t.modified = stampFrom(m.value("modified"));
    t.deleted = m.value("deleted").toBool();
    return t;
}

QVariantMap ListGroup::toMap() const {
    return {{"id", id}, {"uid", uid}, {"name", name}, {"sort_order", order},
            {"collapsed", collapsed}, {"modified", stampValue(modified)}, {"deleted", deleted}};
}

ListGroup ListGroup::fromMap(const QVariantMap &m) {
    ListGroup g;
    g.id = m.value("id").toLongLong();
    g.uid = m.value("uid").toString();
    g.name = m.value("name").toString();
    g.order = m.value("sort_order").toDouble();
    g.collapsed = m.value("collapsed").toBool();
    g.modified = stampFrom(m.value("modified"));
    g.deleted = m.value("deleted").toBool();
    return g;
}

QVariantMap TaskList::toMap() const {
    return {
        {"id", id},
        {"uid", uid},
        {"kind", kind},
        {"group_id", groupId},
        {"name", name},
        {"color", color},
        {"sort_order", order},
        {"sort_field", sortField},
        {"sort_ascending", sortAscending},
        {"separated", separated},
        {"show_in_all", showInAll},
        {"show_in_today", showInToday},
        {"show_in_done", showInDone},
        {"archived", archived},
        {"smart_query", smartQuery},
        {"smart_range", smartRange},
        {"modified", stampValue(modified)},
        {"deleted", deleted},
    };
}

TaskList TaskList::fromMap(const QVariantMap &m) {
    TaskList l;
    l.id = m.value("id").toLongLong();
    l.uid = m.value("uid").toString();
    l.kind = m.value("kind").toInt();
    l.groupId = m.value("group_id").toLongLong();
    l.name = m.value("name").toString();
    l.color = m.value("color").toInt();
    l.order = m.value("sort_order").toDouble();
    l.sortField = m.value("sort_field").toInt();
    l.sortAscending = m.value("sort_ascending", true).toBool();
    l.separated = m.value("separated", true).toBool();
    l.showInAll = m.value("show_in_all", true).toBool();
    l.showInToday = m.value("show_in_today", true).toBool();
    l.showInDone = m.value("show_in_done", true).toBool();
    l.archived = m.value("archived").toBool();
    l.smartQuery = m.value("smart_query").toString();
    l.smartRange = m.value("smart_range").toString();
    l.modified = stampFrom(m.value("modified"));
    l.deleted = m.value("deleted").toBool();
    return l;
}

QVariantMap TagGroup::toMap() const {
    return {{"id", id}, {"uid", uid}, {"name", name}, {"sort_order", order},
            {"collapsed", collapsed}, {"modified", stampValue(modified)}, {"deleted", deleted}};
}

TagGroup TagGroup::fromMap(const QVariantMap &m) {
    TagGroup g;
    g.id = m.value("id").toLongLong();
    g.uid = m.value("uid").toString();
    g.name = m.value("name").toString();
    g.order = m.value("sort_order").toDouble();
    g.collapsed = m.value("collapsed").toBool();
    g.modified = stampFrom(m.value("modified"));
    g.deleted = m.value("deleted").toBool();
    return g;
}

QVariantMap Tag::toMap() const {
    return {{"id", id}, {"uid", uid}, {"group_id", groupId}, {"name", name}, {"paused", paused},
            {"sort_order", order}, {"modified", stampValue(modified)}, {"deleted", deleted}};
}

Tag Tag::fromMap(const QVariantMap &m) {
    Tag t;
    t.id = m.value("id").toLongLong();
    t.uid = m.value("uid").toString();
    t.groupId = m.value("group_id").toLongLong();
    t.name = m.value("name").toString();
    t.paused = m.value("paused").toBool();
    t.order = m.value("sort_order").toDouble();
    t.modified = stampFrom(m.value("modified"));
    t.deleted = m.value("deleted").toBool();
    return t;
}

QVariantMap Alarm::toMap() const {
    return {{"id", id}, {"uid", uid}, {"task_id", taskId}, {"kind", kind}, {"offset_minutes", offset},
            {"at", stampValue(at)}, {"sound", sound}, {"snoozed_until", stampValue(snoozedUntil)},
            {"fired_for", stampValue(firedFor)}, {"modified", stampValue(modified)}, {"deleted", deleted}};
}

Alarm Alarm::fromMap(const QVariantMap &m) {
    Alarm a;
    a.id = m.value("id").toLongLong();
    a.uid = m.value("uid").toString();
    a.taskId = m.value("task_id").toLongLong();
    a.kind = m.value("kind").toInt();
    a.offset = m.value("offset_minutes").toInt();
    a.at = stampFrom(m.value("at"));
    a.sound = m.value("sound", true).toBool();
    a.snoozedUntil = stampFrom(m.value("snoozed_until"));
    a.firedFor = stampFrom(m.value("fired_for"));
    a.modified = stampFrom(m.value("modified"));
    a.deleted = m.value("deleted").toBool();
    return a;
}

QVariantMap Location::toMap() const {
    return {{"id", id}, {"uid", uid}, {"name", name}, {"address", address}, {"latitude", latitude},
            {"longitude", longitude}, {"paused", paused}, {"modified", stampValue(modified)}, {"deleted", deleted}};
}

Location Location::fromMap(const QVariantMap &m) {
    Location l;
    l.id = m.value("id").toLongLong();
    l.uid = m.value("uid").toString();
    l.name = m.value("name").toString();
    l.address = m.value("address").toString();
    l.latitude = m.value("latitude").toDouble();
    l.longitude = m.value("longitude").toDouble();
    l.paused = m.value("paused").toBool();
    l.modified = stampFrom(m.value("modified"));
    l.deleted = m.value("deleted").toBool();
    return l;
}

QVariantMap Attachment::toMap() const {
    return {{"id", id}, {"uid", uid}, {"task_id", taskId}, {"kind", kind}, {"name", name}, {"file_name", fileName},
            {"bytes", bytes}, {"created", stampValue(created)}, {"modified", stampValue(modified)}, {"deleted", deleted}};
}

Attachment Attachment::fromMap(const QVariantMap &m) {
    Attachment a;
    a.id = m.value("id").toLongLong();
    a.uid = m.value("uid").toString();
    a.taskId = m.value("task_id").toLongLong();
    a.kind = m.value("kind").toInt();
    a.name = m.value("name").toString();
    a.fileName = m.value("file_name").toString();
    a.bytes = m.value("bytes").toLongLong();
    a.created = stampFrom(m.value("created"));
    a.modified = stampFrom(m.value("modified"));
    a.deleted = m.value("deleted").toBool();
    return a;
}

