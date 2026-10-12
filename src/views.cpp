#include "views.h"

#include <QLocale>

#include "query.h"
#include <algorithm>
#include <limits>

namespace {

const QLocale English(QLocale::English, QLocale::UnitedKingdom);
const char *SourceNames[] = {"all", "today", "starred", "scheduled", "done", "list", "project"};

QString dayTitle(const QDate &date, const QDate &today) {
    const qint64 days = today.daysTo(date);
    if (days == 0) return QStringLiteral("Today");
    if (days == 1) return QStringLiteral("Tomorrow");
    if (days == 2) return QStringLiteral("Day After Tomorrow");
    if (days == -1) return QStringLiteral("Yesterday");
    return English.toString(date, date.year() == today.year() ? "ddd d MMM" : "ddd d MMM yyyy");
}

QString durationTitle(int minutes) {
    if (minutes <= 0) return QStringLiteral("No Duration");
    if (minutes % (24 * 60) == 0) {
        const int d = minutes / (24 * 60);
        return QStringLiteral("%1 Day%2").arg(d).arg(d == 1 ? "" : "s");
    }
    if (minutes % 60 == 0) {
        const int h = minutes / 60;
        return QStringLiteral("%1 Hour%2").arg(h).arg(h == 1 ? "" : "s");
    }
    return QStringLiteral("%1 Minutes").arg(minutes);
}

// Sections sort by a numeric rank so "Overdue" leads and "No Due Date" trails
// regardless of the dates in between.
constexpr qint64 RankFirst = std::numeric_limits<qint64>::min() / 2;
constexpr qint64 RankLast = std::numeric_limits<qint64>::max() / 2;

struct Placement {
    QString key;
    QString title;
    QString tone;
    int color = -1;
    qint64 rank = 0;
};

}  // namespace

QString Source::key() const {
    return kind >= List ? QStringLiteral("%1:%2").arg(SourceNames[kind]).arg(id) : QString(SourceNames[kind]);
}

Source Source::fromKey(const QString &key) {
    const auto parts = key.split(':');
    for (int k = All; k <= Project; ++k) {
        if (parts.value(0) == SourceNames[k])
            return Source{k, parts.value(1).toLongLong()};
    }
    return Source{};
}

ViewBuilder::ViewBuilder(const Store &store, const QDateTime &now, const ScheduleSettings &settings)
    : m_store(store), m_now(now), m_today(now.date()), m_settings(settings) {
    for (const auto &tag : store.tags())
        if (tag.paused && !tag.deleted)
            m_pausedTags.insert(tag.id);
    const auto tags = orderedTags(store);
    for (int i = 0; i < tags.size(); ++i)
        m_tagRank.insert(tags.at(i).id, i);
    for (const auto &place : store.locations())
        if (place.paused && !place.deleted)
            m_pausedLocations.insert(place.id);
    for (const auto &task : store.tasks())
        if (!task.deleted && task.parentId)
            m_childrenOf[task.parentId].append(task.id);
}

QVector<Tag> ViewBuilder::orderedTags(const Store &store) {
    QVector<Tag> tags;
    for (const auto &t : store.tags())
        if (!t.deleted)
            tags.append(t);
    const bool manual = store.setting("tags.manual", false).toBool();
    std::sort(tags.begin(), tags.end(), [&](const Tag &a, const Tag &b) {
        const auto ga = store.tagGroups().value(a.groupId), gb = store.tagGroups().value(b.groupId);
        const bool ua = !ga.id || ga.deleted, ub = !gb.id || gb.deleted;
        if (ua != ub) return ua;  // Ungrouped tags first, under "Tags".
        if (!ua && ga.order != gb.order) return ga.order < gb.order;
        if (a.groupId != b.groupId) return a.groupId < b.groupId;
        if (manual && a.order != b.order) return a.order < b.order;
        const int byName = QString::localeAwareCompare(a.name, b.name);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    return tags;
}

bool ViewBuilder::isPaused(const Task &task) const {
    for (auto id : task.tagIds)
        if (m_pausedTags.contains(id))
            return true;
    for (auto id : task.locationIds)
        if (m_pausedLocations.contains(id))
            return true;
    // A paused project hides its children with it.
    if (task.parentId && m_store.tasks().contains(task.parentId))
        return isPaused(m_store.task(task.parentId));
    return false;
}

bool ViewBuilder::listVisible(qint64 listId) const {
    const auto it = m_store.lists().constFind(listId);
    return it != m_store.lists().cend() && !it->deleted && !it->archived && it->kind != ListKind::Smart;
}

bool ViewBuilder::inSmartRange(const Task &t, const QString &range) const {
    if (range.isEmpty())
        return true;
    const QStringList parts = range.split(':');
    if (parts.value(0) == "nodue")
        return !t.dueDate.isValid();
    if (!t.dueDate.isValid())
        return false;
    if (parts.value(0) == "next") {
        // "In the next N days/weeks/months/years", from today, overdue excluded.
        const int n = qMax(1, parts.value(1).toInt());
        const QString unit = parts.value(2);
        const QDate end = unit == "w" ? m_today.addDays(7 * n) : unit == "m" ? m_today.addMonths(n)
                        : unit == "y" ? m_today.addYears(n) : m_today.addDays(n);
        return t.dueDate >= m_today && t.dueDate < end;
    }
    if (parts.value(0) == "fixed") {
        const QDate from = QDate::fromString(parts.value(1), Qt::ISODate);
        const QDate to = QDate::fromString(parts.value(2), Qt::ISODate);
        return (!from.isValid() || t.dueDate >= from) && (!to.isValid() || t.dueDate <= to);
    }
    return true;
}

bool ViewBuilder::isSmartList(qint64 listId) const {
    const auto it = m_store.lists().constFind(listId);
    return it != m_store.lists().cend() && !it->deleted && it->kind == ListKind::Smart;
}

bool ViewBuilder::matchesFocusCondition(const Task &t, const QString &c, const QDateTime &now,
                                        const ScheduleSettings &s) {
    const QDate today = now.date();
    const auto dueWithin = [&](int days) { return t.dueDate.isValid() && t.dueDate >= today && t.dueDate < today.addDays(days); };
    if (c == "overdue") return Schedule::isOverdue(t, now, s);
    if (c == "nodue") return !t.dueDate.isValid();
    if (c == "inprogress") return Schedule::isInProgress(t, now) || (t.startDate.isValid() && t.startDate <= today && !t.completed);
    if (c == "today") return Schedule::isDueToday(t, now);
    if (c == "tomorrow") return t.dueDate == today.addDays(1);
    if (c == "thisweek") {
        const QDate first = Schedule::weekStartOf(today, s.weekStart);
        return t.dueDate.isValid() && t.dueDate >= first && t.dueDate <= first.addDays(6);
    }
    if (c == "next3") return dueWithin(3);
    if (c == "next7") return dueWithin(7);
    if (c == "next30") return dueWithin(30);
    if (c == "next6m") return t.dueDate.isValid() && t.dueDate >= today && t.dueDate < today.addMonths(6);
    if (c == "next12m") return t.dueDate.isValid() && t.dueDate >= today && t.dueDate < today.addMonths(12);
    if (c == "hiprio") return t.priority == Priority::High;
    if (c == "starred") return t.starred;
    return false;
}

QVector<Task> ViewBuilder::children(qint64 parentId) const {
    QVector<Task> result;
    for (auto id : m_childrenOf.value(parentId))
        result.append(m_store.task(id));
    std::sort(result.begin(), result.end(), [](const Task &a, const Task &b) {
        return a.order != b.order ? a.order < b.order : a.created < b.created;
    });
    return result;
}

SortSpec ViewBuilder::projectSort(qint64 parentId) const {
    const QString prefix = QStringLiteral("sort.project:%1.").arg(parentId);
    SortSpec spec;
    spec.field = m_store.setting(prefix + "field", SortField::Manual).toInt();
    spec.ascending = m_store.setting(prefix + "ascending", true).toBool();
    spec.separated = false;
    return spec;
}

QVector<TaskList> ViewBuilder::orderedLists() const {
    QVector<TaskList> lists;
    for (const auto &l : m_store.lists())
        if (!l.deleted && !l.archived && l.kind != ListKind::Smart)
            lists.append(l);
    auto groupOrder = [this](qint64 groupId) -> double {
        const auto it = m_store.groups().constFind(groupId);
        if (!groupId || it == m_store.groups().cend() || it->deleted)
            return -1e18;  // Ungrouped lists come first.
        return it->order;
    };
    std::sort(lists.begin(), lists.end(), [&](const TaskList &a, const TaskList &b) {
        if (a.kind != b.kind) return a.kind == ListKind::Inbox;
        const double ga = groupOrder(a.groupId), gb = groupOrder(b.groupId);
        if (ga != gb) return ga < gb;
        if (a.groupId != b.groupId) return a.groupId < b.groupId;
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return lists;
}

QString ViewBuilder::firstLocation(const Task &t) const {
    QString first;
    for (auto id : t.locationIds) {
        const auto it = m_store.locations().constFind(id);
        if (it == m_store.locations().cend() || it->deleted)
            continue;
        if (first.isEmpty() || QString::localeAwareCompare(it->name, first) < 0)
            first = it->name;
    }
    return first;
}

QString ViewBuilder::sourceTitle(const Source &s) const {
    switch (s.kind) {
    case Source::All: return QStringLiteral("All");
    case Source::Today: return QStringLiteral("Today");
    case Source::Starred: return QStringLiteral("Starred");
    case Source::Scheduled: return QStringLiteral("Scheduled");
    case Source::Done: return QStringLiteral("Done");
    case Source::List: return m_store.list(s.id).name;
    case Source::Project: return m_store.task(s.id).title;
    }
    return {};
}

bool ViewBuilder::matchesFocus(const Task &t, int kind) const {
    const TaskList list = m_store.list(t.listId);
    switch (kind) {
    case Source::All: return !t.completed && list.showInAll;
    case Source::Today:
        if (!Schedule::inToday(t, m_now, m_settings)) return false;
        // Starred tasks reach Today even from lists excluded from it.
        return list.showInToday || (m_settings.todayIncludesStarred && t.starred);
    case Source::Starred: return !t.completed && t.starred;
    case Source::Scheduled: return !t.completed && Schedule::isScheduled(t, m_now);
    case Source::Done: return t.completed && list.showInDone;
    }
    return false;
}

QueryContext ViewBuilder::queryContext(const ViewOptions &o) const {
    QueryContext context;
    context.store = &m_store;
    context.now = m_now;
    context.settings = m_settings;
    context.soundex = o.soundex;
    context.rangeFrom = o.dayFilterFrom;
    context.rangeTo = o.dayFilterTo.isValid() ? o.dayFilterTo : o.dayFilterFrom;
    context.isPaused = [this](const Task &task) { return isPaused(task); };
    return context;
}

bool ViewBuilder::passesFilters(const Task &t, const Source &source, const ViewOptions &o, const Query *query) const {
    if (t.deleted || !listVisible(t.listId))
        return false;
    if (!o.showPaused && isPaused(t))
        return false;
    const bool scheduledView = source.kind == Source::Scheduled || source.kind == Source::Done;
    if (!scheduledView && !o.showScheduled && Schedule::isScheduled(t, m_now) && !t.completed)
        return false;
    // "Hide started tasks unless due today" covers All and the lists; Today
    // has its own "Still Working On" setting.
    if ((source.kind == Source::All || source.kind == Source::List) && Schedule::startedHidden(t, m_now, m_settings))
        return false;
    if (!o.tagFilter.isEmpty()) {
        bool any = false;
        for (auto id : t.tagIds)
            any = any || o.tagFilter.contains(id);
        if (!any)
            return false;
    }
    if (!o.locationFilter.isEmpty()) {
        bool any = false;
        for (auto id : t.locationIds)
            any = any || o.locationFilter.contains(id);
        if (!any)
            return false;
    }
    if (o.dayFilterFrom.isValid()) {
        const QDate to = o.dayFilterTo.isValid() ? o.dayFilterTo : o.dayFilterFrom;
        if (!t.dueDate.isValid() || t.dueDate < o.dayFilterFrom || t.dueDate > to)
            return false;
    }
    if (query && !query->isEmpty() && !query->matches(t, queryContext(o)))
        return false;
    if (!o.focusConditions.isEmpty()) {
        bool any = false;
        for (const auto &c : o.focusConditions)
            any = any || matchesFocusCondition(t, c, m_now, m_settings);
        if (!any)
            return false;
    }
    return true;
}

bool ViewBuilder::completedVisible(const Task &t, const Source &source, const ViewOptions &o, int policy) const {
    if (!t.completed || source.kind == Source::Done || o.keepVisible.contains(t.id) || policy == 1)
        return true;
    if (policy == 2)
        return false;
    // Today never shows completed tasks, whatever the window says.
    if (source.kind == Source::Today || o.hideCompleted || o.completedWindowMinutes == 0)
        return false;
    if (o.completedWindowMinutes < 0)
        return true;
    return t.completedAt.isValid() && t.completedAt.secsTo(m_now) <= qint64(o.completedWindowMinutes) * 60;
}

QHash<qint64, int> ViewBuilder::listRanks(bool listOrder) const {
    auto lists = orderedLists();
    if (!listOrder) {
        std::stable_sort(lists.begin(), lists.end(), [](const TaskList &a, const TaskList &b) {
            const int byName = QString::localeAwareCompare(a.name, b.name);
            return byName != 0 ? byName < 0 : a.id < b.id;
        });
    }
    QHash<qint64, int> rank;
    for (int i = 0; i < lists.size(); ++i)
        rank.insert(lists.at(i).id, i);
    return rank;
}

void ViewBuilder::sortItems(QVector<Task> &items, const SortSpec &sort, bool listOrder) const {
    const QHash<qint64, int> listRank = listRanks(listOrder);
    auto dueKey = [](const Task &t) {
        const QDateTime k = Schedule::dueSortKey(t);
        return k.isValid() ? k.toSecsSinceEpoch() : RankLast;
    };
    auto startKey = [](const Task &t) {
        if (!t.startDate.isValid()) return RankLast;
        return QDateTime(t.startDate, QTime(0, 0)).addSecs(qMax(0, t.startTime) * 60).toSecsSinceEpoch();
    };
    // A task sorts by the first of its tags in tag order, and untagged tasks
    // go last. Locations work the same way, in name order.
    auto firstTagRank = [this](const Task &t) -> qint64 {
        qint64 best = RankLast;
        for (auto id : t.tagIds)
            best = qMin<qint64>(best, m_tagRank.value(id, RankLast));
        return best;
    };
    // Returns <0, 0, >0 on the sort's own field; ties fall through to the
    // shared tiebreakers below.
    auto primary = [&](const Task &a, const Task &b) -> qint64 {
        switch (sort.field) {
        case SortField::DueDate: return dueKey(a) - dueKey(b) ? (dueKey(a) < dueKey(b) ? -1 : 1) : 0;
        case SortField::StartDate: return startKey(a) == startKey(b) ? 0 : (startKey(a) < startKey(b) ? -1 : 1);
        case SortField::Priority: return b.priority - a.priority;
        case SortField::List: return listRank.value(a.listId) - listRank.value(b.listId);
        case SortField::Title: return QString::localeAwareCompare(a.title, b.title);
        case SortField::Creation: return a.created == b.created ? 0 : (a.created < b.created ? -1 : 1);
        case SortField::Modified: return a.modified == b.modified ? 0 : (a.modified > b.modified ? -1 : 1);
        case SortField::Duration: return a.duration - b.duration;
        case SortField::Note: {
            // Tasks without a note trail, whichever way the sort runs.
            const bool an = a.notes.trimmed().isEmpty(), bn = b.notes.trimmed().isEmpty();
            if (an != bn) return sort.ascending == an ? 1 : -1;
            return an ? 0 : QString::localeAwareCompare(a.notes.trimmed(), b.notes.trimmed());
        }
        case SortField::Location: {
            const QString la = firstLocation(a), lb = firstLocation(b);
            if (la.isEmpty() != lb.isEmpty()) return sort.ascending == la.isEmpty() ? 1 : -1;
            return la.isEmpty() ? 0 : QString::localeAwareCompare(la, lb);
        }
        case SortField::Completion: return a.completedAt == b.completedAt ? 0 : (a.completedAt > b.completedAt ? -1 : 1);
        case SortField::Tag: return firstTagRank(a) - firstTagRank(b);
        case SortField::Manual:
        case SortField::Section: return a.order == b.order ? 0 : (a.order < b.order ? -1 : 1);
        default: return 0;
        }
    };
    std::stable_sort(items.begin(), items.end(), [&](const Task &a, const Task &b) {
        if (a.completed != b.completed && sort.field != SortField::Completion)
            return !a.completed;  // Completed tasks settle to the bottom of their section.
        const qint64 p = primary(a, b);
        if (p != 0)
            return sort.ascending ? p < 0 : p > 0;
        if (a.priority != b.priority) return a.priority > b.priority;
        if (dueKey(a) != dueKey(b)) return dueKey(a) < dueKey(b);
        if (a.order != b.order) return a.order < b.order;
        return a.id < b.id;
    });
}

QVector<Section> ViewBuilder::sectionize(const QVector<Task> &items, const Source &source, const SortSpec &sort,
                                         const ViewOptions &options) const {
    const QHash<qint64, int> listRank = listRanks(options.listOrder);

    auto place = [&](const Task &t) -> Placement {
        if (!sort.separated || source.kind == Source::Project) {
            return {"all", QStringLiteral("All Tasks in %1").arg(sourceTitle(source)), {}, -1, 0};
        }
        switch (sort.field) {
        case SortField::DueDate:
            if (!t.dueDate.isValid()) return {"nodue", "No Due Date", {}, -1, RankLast};
            if (Schedule::isOverdue(t, m_now, m_settings)) return {"overdue", "Overdue", "overdue", -1, RankFirst};
            if (source.kind == Source::Today && Schedule::isInProgress(t, m_now))
                return {"progress", "In Progress", "progress", -1, RankFirst + 1};
            if (options.groupedDates) {
                const qint64 days = m_today.daysTo(t.dueDate);
                if (days <= 0) return {"g:today", "Today", {}, -1, 0};
                if (days == 1) return {"g:tomorrow", "Tomorrow", {}, -1, 1};
                if (days <= 7) return {"g:week", "Next 7 Days", {}, -1, 2};
                return {"g:future", "Future", {}, -1, 3};
            }
            return {"d:" + t.dueDate.toString(Qt::ISODate), dayTitle(t.dueDate, m_today), {}, -1, t.dueDate.toJulianDay()};
        case SortField::StartDate:
            if (!t.startDate.isValid()) return {"nostart", "No Start Date", {}, -1, RankLast};
            if (t.startDate < m_today) return {"started", "Started", {}, -1, RankFirst};
            return {"s:" + t.startDate.toString(Qt::ISODate), dayTitle(t.startDate, m_today), {}, -1, t.startDate.toJulianDay()};
        case SortField::Section: {
            // Tasks outside any section come first, then each section in order.
            if (!t.sectionId || !m_store.sections().contains(t.sectionId) || m_store.sections().value(t.sectionId).deleted)
                return {"sec:0", "Not in a Section", {}, -1, RankFirst};
            const ListSection sec = m_store.sections().value(t.sectionId);
            return {QStringLiteral("sec:%1").arg(sec.id), sec.name, {}, -1, qint64(sec.order * 1000) + 1};
        }
        case SortField::Status: {
            // the reference app's Status sort: what needs attention first.
            if (t.completed) return {"st:done", "Completed", {}, -1, 6};
            if (Schedule::isOverdue(t, m_now, m_settings)) return {"st:overdue", "Overdue", "overdue", -1, 0};
            if (Schedule::isInProgress(t, m_now)) return {"st:progress", "In Progress", "progress", -1, 1};
            if (Schedule::isDueToday(t, m_now)) return {"st:today", "Due Today", {}, -1, 2};
            if (Schedule::isScheduled(t, m_now)) return {"st:scheduled", "Scheduled", {}, -1, 4};
            if (t.dueDate.isValid()) return {"st:upcoming", "Upcoming", {}, -1, 3};
            return {"st:nodate", "No Date", {}, -1, 5};
        }
        case SortField::Priority: {
            if (options.starredAboveHigh && t.starred) return {"p-star", "Starred", {}, -1, -1};
            static const char *names[] = {"None", "Low", "Medium", "High"};
            const int p = qBound(0, t.priority, 3);
            return {QStringLiteral("p%1").arg(p), names[p], {}, -1, 3 - p};
        }
        case SortField::List: {
            const TaskList l = m_store.list(t.listId);
            return {QStringLiteral("l%1").arg(l.id), l.name, "list", l.color, listRank.value(l.id)};
        }
        case SortField::Tag: {
            QString first;
            for (auto id : t.tagIds) {
                const QString name = m_store.tag(id).name;
                if (first.isEmpty() || QString::localeAwareCompare(name, first) < 0)
                    first = name;
            }
            if (first.isEmpty()) return {"notag", "No Tags", {}, -1, RankLast};
            return {"t:" + first.toLower(), first, {}, -1, 0};
        }
        case SortField::Note:
            if (t.notes.trimmed().isEmpty()) return {"nonote", "No Note", {}, -1, RankLast};
            return {"hasnote", "Has Note", {}, -1, 0};
        case SortField::Location: {
            const QString first = firstLocation(t);
            if (first.isEmpty()) return {"noloc", "No Location", {}, -1, RankLast};
            return {"loc:" + first.toLower(), first, {}, -1, 0};
        }
        case SortField::Duration:
            return {QStringLiteral("u%1").arg(t.duration), durationTitle(t.duration), {}, -1,
                    t.duration > 0 ? t.duration : RankLast};
        case SortField::Completion: {
            const QDate day = t.completedAt.date();
            return {"c:" + day.toString(Qt::ISODate), dayTitle(day, m_today), {}, -1, -day.toJulianDay()};
        }
        default:
            return {"all", QStringLiteral("All Tasks in %1").arg(sourceTitle(source)), {}, -1, 0};
        }
    };

    // A task with several tags sits under each of them when sorting by tag.
    auto placements = [&](const Task &t) -> QVector<Placement> {
        if (sort.field != SortField::Tag || !sort.separated || source.kind == Source::Project)
            return {place(t)};
        QVector<Placement> found;
        for (auto id : t.tagIds) {
            const Tag tag = m_store.tag(id);
            if (tag.id && !tag.deleted)
                found.append({"t:" + QString::number(tag.id), tag.name, {}, -1, m_tagRank.value(tag.id)});
        }
        if (found.isEmpty()) found.append({"notag", "No Tags", {}, -1, RankLast});
        return found;
    };

    QVector<Section> sections;
    QHash<QString, int> indexOf;
    QHash<QString, qint64> rankOf;
    QHash<QString, QVector<Task>> itemsOf;
    for (const auto &t : items) {
        for (const Placement &p : placements(t)) {
            if (!indexOf.contains(p.key)) {
                indexOf.insert(p.key, sections.size());
                rankOf.insert(p.key, p.rank);
                Section s;
                s.key = p.key;
                s.title = p.title;
                s.tone = p.tone;
                s.color = p.color;
                sections.append(s);
            }
            itemsOf[p.key].append(t);
        }
    }

    const bool dated = sort.field == SortField::DueDate || sort.field == SortField::StartDate;
    std::stable_sort(sections.begin(), sections.end(), [&](const Section &a, const Section &b) {
        const qint64 ra = rankOf.value(a.key), rb = rankOf.value(b.key);
        if (ra == rb) return QString::localeAwareCompare(a.title, b.title) < 0;
        // Descending reverses the dated sections but keeps Overdue first and
        // the undated section last, as the reference app does.
        const bool pinned = ra == RankFirst || rb == RankFirst || ra == RankLast || rb == RankLast;
        if (!sort.ascending && dated && !pinned) return ra > rb;
        if (!sort.ascending && !dated) return ra > rb;
        return ra < rb;
    });

    for (auto &s : sections) {
        QVector<Task> sectionItems = itemsOf.value(s.key);
        sortItems(sectionItems, sort, options.listOrder);
        for (const auto &t : sectionItems) {
            TaskRow row;
            row.taskId = t.id;
            s.rows.append(row);
            if (!t.completed) {
                ++s.openCount;
                int childMinutes = 0;
                for (const auto &c : children(t.id))
                    if (!c.completed)
                        childMinutes += c.duration;
                // Open sub-tasks with durations stand in for the parent's own.
                s.durationTotal += childMinutes > 0 ? childMinutes : t.duration;
            }
        }
    }
    // A list sorted by duration already names the duration in each heading.
    if (sort.field == SortField::Duration && sort.separated && source.kind != Source::Project)
        for (auto &s : sections)
            s.durationTotal = 0;
    return sections;
}

QVector<Section> ViewBuilder::build(const Source &source, const SortSpec &sort, const ViewOptions &options) const {
    // A Smart List is a saved search over every list, combined with whatever
    // is typed in the search field.
    const bool smart = source.kind == Source::List && isSmartList(source.id);
    const TaskList smartList = smart ? m_store.list(source.id) : TaskList();
    const Query search = Query::parse(options.search);
    const Query saved = smart ? Query::parse(smartList.smartQuery) : Query();
    // Naming lists, groups or completed tasks in a search widens it to every
    // list, whatever is selected; a project keeps its own scope.
    const bool wide = !smart && source.kind != Source::Project && search.replacesScope();
    const bool flat = smart || wide || source.isFocus();

    ViewOptions o = options;
    if (smart && smartList.includeScheduled)
        o.showScheduled = true;
    // Sorting one of your own lists by Start Date shows its scheduled tasks,
    // in the order they start.
    if (!smart && sort.field == SortField::StartDate && (source.kind == Source::List || source.kind == Source::Project))
        o.showScheduled = true;
    // Completed tasks: a query that asks for them shows them all; otherwise
    // the list's own Include/Exclude Completed switch decides.
    int policy = 0;
    if (search.namesCompleted() || saved.namesCompleted()) {
        policy = 1;
    } else if (!wide) {
        qint64 listId = 0;
        if (source.kind == Source::List) listId = source.id;
        else if (source.kind == Source::Project) listId = m_store.task(source.id).listId;
        const int mode = listId ? m_store.list(listId).completedMode : 0;
        policy = mode == 1 ? 1 : mode == 2 ? 2 : 0;
    }

    const bool inherit = source.kind == Source::Project && o.inheritSearch;
    const Query inheritedRule = inherit ? Query::parse(o.inheritedRule) : Query();

    QVector<Task> items;
    for (const auto &t : m_store.tasks()) {
        if (t.deleted)
            continue;
        if (smart) {
            if (t.parentId && m_store.task(t.parentId).type != TaskType::Project)
                continue;
            if (!passesFilters(t, source, o, &saved) || !inSmartRange(t, smartList.smartRange))
                continue;
        } else if (wide) {
            if (t.parentId && m_store.task(t.parentId).type != TaskType::Project)
                continue;
        } else if (source.isFocus()) {
            // Checklist items and a task's own sub-tasks never surface on
            // their own; project children do.
            if (t.parentId) {
                const Task parent = m_store.task(t.parentId);
                if (parent.type != TaskType::Project || parent.deleted)
                    continue;
            }
            const bool kept = o.keepVisible.contains(t.id);
            if (!kept && !matchesFocus(t, source.kind))
                continue;
        } else if (source.kind == Source::List) {
            if (t.listId != source.id || t.parentId)
                continue;
        } else if (source.kind == Source::Project) {
            if (t.id != source.id)
                continue;
        }
        // An inherited search filters the project's children, not the project
        // the user chose to look at.
        if (!passesFilters(t, source, o, inherit ? nullptr : &search) || !completedVisible(t, source, o, policy))
            continue;
        items.append(t);
    }

    if (flat && !o.subtasksSeparately) {
        QSet<qint64> present;
        for (const auto &t : items)
            present.insert(t.id);
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [&](const Task &t) { return t.parentId && present.contains(t.parentId); }),
                    items.end());
    }

    QVector<Section> sections = sectionize(items, source, sort, o);
    if (sort.field == SortField::Section && sort.separated && source.kind == Source::List) {
        for (const auto &sec : m_store.sectionsOf(source.id)) {
            const QString key = QStringLiteral("sec:%1").arg(sec.id);
            bool present = false;
            for (const auto &s : sections)
                present = present || s.key == key;
            if (!present) {
                Section empty;
                empty.key = key;
                empty.title = sec.name;
                sections.append(empty);
            }
        }
        std::stable_sort(sections.begin(), sections.end(), [this](const Section &a, const Section &b) {
            auto rank = [this](const Section &s) {
                const qint64 id = s.key.mid(4).toLongLong();
                return id ? m_store.sections().value(id).order : -1e18;
            };
            return rank(a) < rank(b);
        });
    }

    // Expand each row with its nested children: checklist items and sub-tasks
    // everywhere, project children only where the project is their home.
    for (auto &s : sections) {
        s.collapsed = o.collapsedSections.contains(source.key() + "/" + s.key);
        QVector<TaskRow> expanded;
        for (const auto &row : s.rows) {
            const Task t = m_store.task(row.taskId);
            TaskRow top = row;
            top.breadcrumb = flat && t.parentId != 0;
            QVector<TaskRow> nested;
            const bool nestHere = t.type != TaskType::Project || !flat;
            if (nestHere) {
                QVector<Task> kids = children(t.id);
                const SortSpec childSort = projectSort(t.id);
                if (childSort.field != SortField::Manual)
                    sortItems(kids, childSort);
                for (const auto &c : kids) {
                    if (!passesFilters(c, Source{Source::List, c.listId}, o, inherit ? &search : nullptr))
                        continue;
                    if (inherit && (!inheritedRule.matches(c, queryContext(o)) || !inSmartRange(c, o.inheritedRange)))
                        continue;
                    // Completed checklist items stay with their checklist.
                    if (t.type == TaskType::Project && o.completedRuleSubtasks && !completedVisible(c, source, o, policy))
                        continue;
                    nested.append(TaskRow{c.id, 1, false, 0});
                }
            }
            top.visibleChildren = nested.size();
            expanded.append(top);
            if (!t.collapsed || source.kind == Source::Project)
                expanded += nested;
        }
        s.rows = expanded;
    }
    return sections;
}

int ViewBuilder::badgeCount(const QString &metric) const {
    if (metric == "none")
        return 0;
    const ListCounts today = countsForFocus(Source::Today);
    // Today's own count includes what is overdue when Today shows it.
    const int dueToday = m_settings.todayIncludesOverdue ? today.undone - today.overdue : today.undone;
    if (metric == "overdue")
        return today.overdue;
    if (metric == "today")
        return dueToday;
    return today.overdue + dueToday;
}

ListCounts ViewBuilder::countsForList(qint64 listId) const {
    ListCounts c;
    for (const auto &t : m_store.tasks()) {
        if (t.listId != listId || t.parentId || t.completed || t.deleted || isPaused(t))
            continue;
        if (Schedule::isScheduled(t, m_now) || Schedule::startedHidden(t, m_now, m_settings))
            continue;
        ++c.undone;
        if (Schedule::isOverdue(t, m_now, m_settings))
            ++c.overdue;
        else if (Schedule::isDueToday(t, m_now))
            c.dueToday = true;
    }
    return c;
}

ListCounts ViewBuilder::countsForFocus(int kind) const {
    ListCounts c;
    if (kind == Source::Done)
        return c;
    for (const auto &t : m_store.tasks()) {
        if (t.deleted || t.completed || !listVisible(t.listId) || isPaused(t))
            continue;
        if (t.parentId && m_store.task(t.parentId).type != TaskType::Project)
            continue;
        if (kind != Source::Scheduled && Schedule::isScheduled(t, m_now))
            continue;
        if (kind == Source::All && Schedule::startedHidden(t, m_now, m_settings))
            continue;
        if (!matchesFocus(t, kind))
            continue;
        ++c.undone;
        if (Schedule::isOverdue(t, m_now, m_settings))
            ++c.overdue;
    }
    return c;
}
