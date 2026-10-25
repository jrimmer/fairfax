#include "model.h"

#include <QJsonDocument>
#include <QJsonObject>
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
        {"section_id", sectionId},
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
        {"deleted", deleted}, {"ext", ext},
    };
}

Task Task::fromMap(const QVariantMap &m) {
    Task t;
    t.id = m.value("id").toLongLong();
    t.uid = m.value("uid").toString();
    t.listId = m.value("list_id").toLongLong();
    t.parentId = m.value("parent_id").toLongLong();
    t.sectionId = m.value("section_id").toLongLong();
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
    t.ext = m.value("ext").toString();
    return t;
}

QVariantMap ListGroup::toMap() const {
    return {{"id", id}, {"uid", uid}, {"name", name}, {"sort_order", order},
            {"collapsed", collapsed}, {"color", color}, {"modified", stampValue(modified)}, {"deleted", deleted}, {"ext", ext}};
}

ListGroup ListGroup::fromMap(const QVariantMap &m) {
    ListGroup g;
    g.id = m.value("id").toLongLong();
    g.uid = m.value("uid").toString();
    g.name = m.value("name").toString();
    g.order = m.value("sort_order").toDouble();
    g.collapsed = m.value("collapsed").toBool();
    g.color = m.value("color", -1).toInt();
    g.modified = stampFrom(m.value("modified"));
    g.deleted = m.value("deleted").toBool();
    g.ext = m.value("ext").toString();
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
        {"completed_mode", completedMode},
        {"include_scheduled", includeScheduled},
        {"mute_alerts", muteAlerts},
        {"modified", stampValue(modified)},
        {"deleted", deleted}, {"ext", ext},
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
    l.completedMode = m.value("completed_mode").toInt();
    l.includeScheduled = m.value("include_scheduled").toBool();
    l.muteAlerts = m.value("mute_alerts").toBool();
    l.modified = stampFrom(m.value("modified"));
    l.deleted = m.value("deleted").toBool();
    l.ext = m.value("ext").toString();
    return l;
}

QVariantMap TagGroup::toMap() const {
    return {{"id", id}, {"uid", uid}, {"name", name}, {"sort_order", order},
            {"collapsed", collapsed}, {"modified", stampValue(modified)}, {"deleted", deleted}, {"ext", ext}};
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
    g.ext = m.value("ext").toString();
    return g;
}

QVariantMap Tag::toMap() const {
    return {{"id", id}, {"uid", uid}, {"group_id", groupId}, {"name", name}, {"paused", paused},
            {"sort_order", order}, {"modified", stampValue(modified)}, {"deleted", deleted}, {"ext", ext}};
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
    t.ext = m.value("ext").toString();
    return t;
}

QVariantMap Alarm::toMap() const {
    return {{"id", id}, {"uid", uid}, {"task_id", taskId}, {"kind", kind}, {"offset_minutes", offset},
            {"at", stampValue(at)}, {"sound", sound}, {"snoozed_until", stampValue(snoozedUntil)},
            {"fired_for", stampValue(firedFor)}, {"modified", stampValue(modified)}, {"deleted", deleted}, {"ext", ext}};
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
    a.ext = m.value("ext").toString();
    return a;
}

QVariantMap Location::toMap() const {
    return {{"id", id}, {"uid", uid}, {"name", name}, {"address", address}, {"latitude", latitude},
            {"longitude", longitude}, {"paused", paused}, {"modified", stampValue(modified)}, {"deleted", deleted}, {"ext", ext}};
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
    l.ext = m.value("ext").toString();
    return l;
}

QVariantMap Attachment::toMap() const {
    return {{"id", id}, {"uid", uid}, {"task_id", taskId}, {"kind", kind}, {"name", name}, {"file_name", fileName},
            {"bytes", bytes}, {"created", stampValue(created)}, {"modified", stampValue(modified)}, {"deleted", deleted}, {"ext", ext}};
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
    a.ext = m.value("ext").toString();
    return a;
}

QVariantMap ListSection::toMap() const {
    return {{"id", id}, {"uid", uid}, {"list_id", listId}, {"name", name}, {"sort_order", order},
            {"modified", stampValue(modified)}, {"deleted", deleted}, {"ext", ext}};
}

ListSection ListSection::fromMap(const QVariantMap &m) {
    ListSection s;
    s.id = m.value("id").toLongLong();
    s.uid = m.value("uid").toString();
    s.listId = m.value("list_id").toLongLong();
    s.name = m.value("name").toString();
    s.order = m.value("sort_order").toDouble();
    s.modified = stampFrom(m.value("modified"));
    s.deleted = m.value("deleted").toBool();
    s.ext = m.value("ext").toString();
    return s;
}

QVariantMap Proposal::toMap() const {
    return {{"id", id}, {"kind", kind},
            {"payload", QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson(QJsonDocument::Compact))},
            {"created", stampValue(created)}};
}

Proposal Proposal::fromMap(const QVariantMap &m) {
    Proposal p;
    p.id = m.value("id").toLongLong();
    p.kind = m.value("kind").toString();
    p.payload = QJsonDocument::fromJson(m.value("payload").toString().toUtf8()).object().toVariantMap();
    p.created = stampFrom(m.value("created"));
    return p;
}

