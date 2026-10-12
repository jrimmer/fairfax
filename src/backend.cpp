#include "backend.h"

#include <QDesktopServices>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

#include "dateparse.h"
#include "recurrence.h"

namespace {

const QLocale English(QLocale::English, QLocale::UnitedKingdom);

const QStringList TaskRoles{
    "kind", "taskId", "depth", "title", "notesPreview", "type", "childCount", "childOpen", "completed",
    "overdue", "starred", "priority", "repeating", "tags", "actionType", "durationText", "dueText", "dueTone",
    "startText", "listName", "listColor", "breadcrumb", "collapsed", "hasChildren", "selected", "current",
    "editing", "dimmed", "progress", "hasNotes", "sectionKey", "tone", "count", "durationTotal", "color",
    "isFirst",
};
const QStringList SidebarRoles{"kind", "sourceKey", "title", "icon", "overdue", "undone", "color", "dot",
                               "selected", "depth", "expandable", "expanded", "collapsed", "excluded",
                               "groupId", "listId", "taskId"};
const QStringList TagRoles{"kind", "tagId", "groupId", "title", "count", "paused", "filtered", "collapsed"};

QDate isoDate(const QString &text) { return QDate::fromString(text, Qt::ISODate); }

QString focusKey(int kind) { return Source{kind, 0}.key(); }

}  // namespace

Backend::Backend(Store *store, QObject *parent)
    : QObject(parent), m_store(store), m_sidebar(SidebarRoles), m_tasks(TaskRoles), m_tags(TagRoles) {
    m_source = Source::fromKey(m_store->setting("ui.source", "today").toString());
    for (const auto &key : m_store->setting("ui.collapsedSections").toString().split('|', Qt::SkipEmptyParts))
        m_options.collapsedSections.insert(key);
    for (const auto &id : m_store->setting("ui.expandedLists").toString().split(',', Qt::SkipEmptyParts))
        m_expandedLists.insert(id.toLongLong());
    m_options.completedWindowMinutes = m_store->setting("tasks.completedWindow", 24 * 60).toInt();

    connect(m_store, &Store::changed, this, [this] {
        rebuild();
        emit dataChanged();
    });

    // Overdue and scheduled states move with the clock, so the views are
    // recomputed every minute; crossing midnight also resets "just completed".
    m_lastDay = QDate::currentDate();
    m_clock.setInterval(60 * 1000);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        if (QDate::currentDate() != m_lastDay) {
            m_lastDay = QDate::currentDate();
            m_options.keepVisible.clear();
            emit clockChanged();
        }
        rebuild();
    });
    m_clock.start();
}

// ---------------------------------------------------------------- settings

ScheduleSettings Backend::scheduleSettings() const {
    ScheduleSettings s;
    s.todayIncludesOverdue = m_store->setting("today.overdue", true).toBool();
    s.todayIncludesStarred = m_store->setting("today.starred", false).toBool();
    s.todayIncludesStillWorking = m_store->setting("today.stillWorking", true).toBool();
    s.overdueUsesDueTime = m_store->setting("tasks.overdueUsesTime", false).toBool();
    s.friendlyDates = m_store->setting("appearance.friendlyDates", true).toBool();
    return s;
}

SortSpec Backend::sortSpec() const {
    SortSpec spec;
    if (m_source.kind == Source::List) {
        const TaskList l = m_store->list(m_source.id);
        return {l.sortField, l.sortAscending, l.separated};
    }
    if (m_source.kind == Source::Project) {
        spec.field = SortField::Manual;
        spec.separated = false;
        return spec;
    }
    const QString prefix = "sort." + m_source.key() + ".";
    const int fallback = m_source.kind == Source::Scheduled ? SortField::StartDate
                         : m_source.kind == Source::Done    ? SortField::Completion
                                                             : SortField::DueDate;
    spec.field = m_store->setting(prefix + "field", fallback).toInt();
    spec.ascending = m_store->setting(prefix + "ascending", true).toBool();
    spec.separated = m_store->setting(prefix + "separated", true).toBool();
    return spec;
}

void Backend::storeSortSpec(const SortSpec &spec) {
    if (m_source.kind == Source::List) {
        TaskList l = m_store->list(m_source.id);
        l.sortField = spec.field;
        l.sortAscending = spec.ascending;
        l.separated = spec.separated;
        m_store->begin("Change Sort");
        m_store->putList(l);
        m_store->commit();
        return;
    }
    const QString prefix = "sort." + m_source.key() + ".";
    m_store->setSetting(prefix + "field", spec.field);
    m_store->setSetting(prefix + "ascending", spec.ascending);
    m_store->setSetting(prefix + "separated", spec.separated);
    rebuild();
}

// the reference app's nine named text sizes, Tiny to Giant, with Normal the default.
int Backend::textSize() const { return m_store->setting("appearance.textSize", 2).toInt(); }

int Backend::fontSize() const {
    static const int sizes[] = {12, 13, 14, 15, 16, 17, 19, 21, 24};
    return sizes[qBound(0, textSize(), 8)];
}

QString Backend::textSizeName() const {
    static const char *names[] = {"Tiny", "Small", "Normal", "Big", "Large", "Larger", "Huge", "Massive", "Giant"};
    return names[qBound(0, textSize(), 8)];
}

void Backend::setTextSize(int size) {
    size = qBound(0, size, 8);
    if (size == textSize())
        return;
    m_store->setSetting("appearance.textSize", size);
    emit settingsChanged();
}

QString Backend::todayIso() const { return QDate::currentDate().toString(Qt::ISODate); }

void Backend::setStatus(const QString &message) {
    m_status = message;
    emit statusChanged();
}

// ------------------------------------------------------------ view headers

QString Backend::sourceTitle() const {
    ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
    return views.sourceTitle(m_source);
}

QString Backend::sourceSubtitle() const {
    if (m_source.kind == Source::Today)
        return English.toString(QDate::currentDate(), "ddd, d MMMM");
    if (m_source.kind == Source::Project)
        return m_store->list(m_store->task(m_source.id).listId).name;
    return {};
}

int Backend::sourceColor() const {
    if (m_source.kind == Source::List)
        return m_store->list(m_source.id).color;
    if (m_source.kind == Source::Project)
        return m_store->list(m_store->task(m_source.id).listId).color;
    return -1;
}

QVariantList Backend::sortChoices() const {
    // The Done list and Smart Lists offer their own subsets, as in the reference app.
    QList<QPair<int, QString>> all{
        {SortField::DueDate, "Due Date"}, {SortField::StartDate, "Start Date"}, {SortField::Priority, "Priority"},
        {SortField::List, "To-do List"},  {SortField::Title, "Title"},          {SortField::Tag, "Tag"},
        {SortField::Duration, "Duration"}, {SortField::Creation, "Creation"},    {SortField::Modified, "Modified"},
        {SortField::Manual, "Manually"},
    };
    if (m_source.kind == Source::Done)
        all = {{SortField::Priority, "Priority"}, {SortField::List, "To-do List"}, {SortField::Modified, "Modified"},
               {SortField::Completion, "Completion"}, {SortField::Title, "Title"}};
    QVariantList result;
    for (const auto &[field, name] : all)
        result.append(QVariantMap{{"field", field}, {"name", name}});
    return result;
}

QVariantList Backend::tagFilter() const {
    QVariantList ids;
    for (auto id : m_options.tagFilter)
        ids.append(id);
    return ids;
}

QString Backend::dayFilter() const {
    return m_options.dayFilterFrom.isValid() ? m_options.dayFilterFrom.toString(Qt::ISODate) : QString();
}

QString Backend::selectionSummary() const {
    if (m_selected.size() < 2)
        return {};
    int minutes = 0;
    for (auto id : m_selected) {
        const Task t = m_store->task(id);
        if (!t.completed)
            minutes += t.duration;
    }
    QString text = QStringLiteral("%1 tasks selected").arg(m_selected.size());
    if (minutes > 0)
        text += QStringLiteral(" · ") + Schedule::durationTotal(minutes);
    return text;
}

QVariantList Backend::listChoices() const {
    ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
    QVariantList result;
    for (const auto &l : views.orderedLists()) {
        const auto g = m_store->groups().value(l.groupId);
        result.append(QVariantMap{{"id", l.id}, {"name", l.name}, {"color", l.color},
                                  {"group", g.deleted ? QString() : g.name}});
    }
    return result;
}

QStringList Backend::tagNames() const {
    QStringList names;
    for (const auto &t : m_store->tags())
        if (!t.deleted)
            names << t.name;
    names.sort(Qt::CaseInsensitive);
    return names;
}

// -------------------------------------------------------------- navigation

void Backend::selectSource(const QString &key) {
    const Source next = Source::fromKey(key);
    if (next == m_source)
        return;
    endEdit();
    m_source = next;
    m_options.keepVisible.clear();
    m_options.dayFilterFrom = {};
    m_options.dayFilterTo = {};
    m_selected.clear();
    m_cursor = m_anchor = 0;
    saveUiState();
    rebuild();
    emit viewChanged();
    emit selectionChanged();
}

void Backend::selectAdjacentSource(int delta) {
    QStringList keys;
    for (const auto &row : m_sidebar.rows())
        if (!row.value("sourceKey").toString().isEmpty())
            keys << row.value("sourceKey").toString();
    const int i = keys.indexOf(m_source.key());
    if (keys.isEmpty())
        return;
    selectSource(keys.at(qBound(0, (i < 0 ? 0 : i + delta), int(keys.size() - 1))));
}

void Backend::toggleGroupCollapsed(qint64 groupId) {
    ListGroup g = m_store->groups().value(groupId);
    if (!g.id)
        return;
    g.collapsed = !g.collapsed;
    m_store->begin(g.collapsed ? "Collapse Group" : "Expand Group");
    m_store->putGroup(g);
    m_store->commit();
}

void Backend::toggleListExpanded(qint64 listId) {
    if (!m_expandedLists.remove(listId))
        m_expandedLists.insert(listId);
    saveUiState();
    rebuild();
}

void Backend::toggleSection(const QString &sectionKey, bool all) {
    const QString prefix = m_source.key() + "/";
    if (all) {
        // ⌘-click folds or unfolds every section, following the clicked one.
        const bool collapse = !m_options.collapsedSections.contains(prefix + sectionKey);
        for (const auto &row : m_tasks.rows()) {
            if (row.value("kind") != "section")
                continue;
            const QString key = prefix + row.value("sectionKey").toString();
            if (collapse) m_options.collapsedSections.insert(key); else m_options.collapsedSections.remove(key);
        }
    } else if (!m_options.collapsedSections.remove(prefix + sectionKey)) {
        m_options.collapsedSections.insert(prefix + sectionKey);
    }
    saveUiState();
    rebuild();
}

void Backend::setSort(int field) {
    SortSpec spec = sortSpec();
    spec.field = field;
    storeSortSpec(spec);
    emit viewChanged();
}

void Backend::toggleSortDirection() {
    SortSpec spec = sortSpec();
    spec.ascending = !spec.ascending;
    storeSortSpec(spec);
    emit viewChanged();
}

void Backend::setSeparated(bool on) {
    SortSpec spec = sortSpec();
    spec.separated = on;
    storeSortSpec(spec);
    emit viewChanged();
}

void Backend::setShowScheduled(bool on) {
    m_options.showScheduled = on;
    rebuild();
    emit viewChanged();
}

void Backend::setShowPaused(bool on) {
    m_options.showPaused = on;
    rebuild();
    emit viewChanged();
}

void Backend::setHideCompleted(bool on) {
    m_options.hideCompleted = on;
    rebuild();
    emit viewChanged();
}

void Backend::setSearch(const QString &text) {
    if (text == m_options.search)
        return;
    m_options.search = text;
    rebuild();
    emit viewChanged();
}

void Backend::toggleTagFilter(qint64 tagId) {
    if (!m_options.tagFilter.remove(tagId))
        m_options.tagFilter.insert(tagId);
    rebuild();
    emit viewChanged();
}

void Backend::clearTagFilter() {
    m_options.tagFilter.clear();
    rebuild();
    emit viewChanged();
}

void Backend::setDayFilter(const QString &iso) {
    const QDate day = isoDate(iso);
    // Clicking the filtered day again clears the filter.
    m_options.dayFilterFrom = (day.isValid() && day != m_options.dayFilterFrom) ? day : QDate();
    m_options.dayFilterTo = m_options.dayFilterFrom;
    rebuild();
    emit viewChanged();
}

QVariantList Backend::monthDays(int year, int month) const {
    // Busy-day counts for the mini calendar: tasks starting or due each day,
    // plus the next occurrences of repeating tasks within the month.
    const QDate first(year, month, 1);
    QHash<QDate, int> counts;
    for (const auto &t : m_store->tasks()) {
        if (t.deleted || t.completed)
            continue;
        if (t.dueDate.isValid()) counts[t.dueDate]++;
        if (t.startDate.isValid() && t.startDate != t.dueDate) counts[t.startDate]++;
        if (t.recurrence.isSet() && t.dueDate.isValid()) {
            QDate d = t.dueDate;
            for (int guard = 0; guard < 62; ++guard) {
                d = Recurrences::next(t.recurrence, d);
                if (!d.isValid() || d > first.addMonths(1)) break;
                if (d >= first) counts[d]++;
            }
        }
    }
    QVariantList days;
    for (QDate d = first; d.month() == month; d = d.addDays(1))
        days.append(counts.value(d));
    return days;
}

// ----------------------------------------------------- selection & editing

void Backend::selectTask(qint64 id, int mode) {
    if (!m_store->tasks().contains(id))
        return;
    if (m_editing && m_editing != id)
        endEdit();
    if (mode == 1 && m_anchor) {
        const int a = m_visibleOrder.indexOf(m_anchor), b = m_visibleOrder.indexOf(id);
        if (a >= 0 && b >= 0) {
            m_selected.clear();
            for (int i = qMin(a, b); i <= qMax(a, b); ++i)
                m_selected.append(m_visibleOrder.at(i));
        }
    } else if (mode == 2) {
        if (!m_selected.removeOne(id))
            m_selected.append(id);
        m_anchor = id;
    } else {
        m_selected = {id};
        m_anchor = id;
    }
    m_cursor = id;
    rebuild();
    emit selectionChanged();
}

void Backend::moveCursor(int delta, bool extend) {
    if (m_visibleOrder.isEmpty())
        return;
    int i = m_visibleOrder.indexOf(m_cursor);
    i = i < 0 ? (delta > 0 ? 0 : m_visibleOrder.size() - 1) : qBound(0, i + delta, int(m_visibleOrder.size() - 1));
    const qint64 id = m_visibleOrder.at(i);
    selectTask(id, extend ? 1 : 0);
    emit revealTask(id);
}

void Backend::selectAll() {
    m_selected = m_visibleOrder;
    rebuild();
    emit selectionChanged();
}

void Backend::clearSelection() {
    endEdit();
    m_selected.clear();
    m_cursor = 0;
    rebuild();
    emit selectionChanged();
}

void Backend::beginEdit(qint64 id) {
    if (!id)
        id = m_cursor;
    if (!m_store->tasks().contains(id))
        return;
    if (m_editing && m_editing != id)
        endEdit();
    m_selected = {id};
    m_cursor = m_anchor = id;
    m_editing = id;
    rebuild();
    emit selectionChanged();
}

void Backend::endEdit() {
    if (!m_editing)
        return;
    const qint64 id = m_editing;
    m_editing = 0;
    // A new task left without a title never happened.
    if (id == m_newTask && m_store->task(id).title.trimmed().isEmpty()) {
        m_selected.removeOne(id);
        if (m_cursor == id)
            m_cursor = 0;
        m_store->discard("tasks", id);
    }
    m_newTask = 0;
    rebuild();
    emit selectionChanged();
}

QVector<qint64> Backend::targetIds(qint64 id) const {
    if (id)
        return {id};
    if (!m_selected.isEmpty())
        return m_selected;
    return m_cursor ? QVector<qint64>{m_cursor} : QVector<qint64>{};
}

void Backend::editTasks(const QString &label, const QVector<qint64> &ids, const std::function<void(Task &)> &edit) {
    if (ids.isEmpty())
        return;
    m_store->begin(label);
    for (auto id : ids) {
        if (!m_store->tasks().contains(id))
            continue;
        Task t = m_store->task(id);
        edit(t);
        m_store->putTask(t);
    }
    m_store->commit();
}

// ------------------------------------------------------------ task actions

qint64 Backend::defaultListId() const {
    const qint64 preferred = m_store->setting("tasks.defaultList", 0).toLongLong();
    if (preferred && m_store->lists().contains(preferred) && !m_store->list(preferred).deleted)
        return preferred;
    ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
    const auto lists = views.orderedLists();
    return lists.isEmpty() ? 0 : lists.first().id;
}

double Backend::nextOrder(qint64 listId, qint64 parentId) const {
    double order = 0;
    for (const auto &t : m_store->tasks())
        if (t.listId == listId && t.parentId == parentId)
            order = qMax(order, t.order);
    return order + 1;
}

void Backend::newTask(int type, bool subtask) {
    endEdit();
    Task t;
    t.id = m_store->nextId("tasks");
    t.type = type;
    const Task current = m_store->task(m_cursor);
    if (subtask && current.id) {
        // A sub-task belongs to the selected project or checklist, or to the
        // selected task's parent when a sub-task is selected.
        const Task parent = current.parentId ? m_store->task(current.parentId) : current;
        t.parentId = parent.id;
        t.listId = parent.listId;
        t.type = TaskType::Task;
    } else if (m_source.kind == Source::List) {
        t.listId = m_source.id;
    } else if (m_source.kind == Source::Project) {
        t.listId = m_store->task(m_source.id).listId;
        t.parentId = m_source.id;
    } else {
        t.listId = defaultListId();
    }
    if (!t.listId) {
        setStatus("Create a list first: there is nowhere to put the task.");
        return;
    }
    // Context inheritance: added in Today it's due today; in Starred, starred.
    if (!t.parentId || m_store->task(t.parentId).type == TaskType::Project) {
        if (m_source.kind == Source::Today)
            t.dueDate = QDate::currentDate();
        if (m_source.kind == Source::Starred)
            t.starred = true;
    }
    t.order = nextOrder(t.listId, t.parentId);
    m_store->begin(type == TaskType::Project ? "New Project" : type == TaskType::Checklist ? "New Checklist" : "New Task");
    m_store->putTask(t);
    m_store->commit();
    if (t.parentId) {
        Task parent = m_store->task(t.parentId);
        if (parent.collapsed) {
            parent.collapsed = false;
            m_store->begin("Expand");
            m_store->putTask(parent);
            m_store->commit();
        }
    }
    m_newTask = t.id;
    m_options.keepVisible.insert(t.id);
    beginEdit(t.id);
    emit revealTask(t.id);
}

void Backend::completeTask(Task t, bool done, const QDateTime &now) {
    if (t.completed == done)
        return;
    if (done && t.recurrence.isSet()) {
        // Keep a completed copy for the record and move the task itself on to
        // its next occurrence; the last occurrence simply completes.
        if (auto next = Recurrences::advance(t, now.date())) {
            Task copy = t;
            copy.id = m_store->nextId("tasks");
            copy.uid.clear();
            copy.recurrence = {};
            copy.completed = true;
            copy.completedAt = now;
            if (copy.seriesUid.isEmpty())
                copy.seriesUid = t.uid;
            m_store->putTask(copy);
            next->seriesUid = copy.seriesUid;
            m_store->putTask(*next);
            return;
        }
    }
    t.completed = done;
    t.completedAt = done ? now : QDateTime();
    m_store->putTask(t);
    // Completing a project or checklist completes what's inside it.
    if (done && t.type != TaskType::Task) {
        ViewBuilder views(*m_store, now, scheduleSettings());
        for (const auto &c : views.children(t.id))
            if (!c.completed)
                completeTask(c, true, now);
    }
}

void Backend::toggleComplete(qint64 id) {
    const auto ids = targetIds(id);
    if (ids.isEmpty())
        return;
    // Mixed selections complete; an all-complete selection reopens.
    bool allDone = true;
    for (auto i : ids)
        allDone = allDone && m_store->task(i).completed;
    const QDateTime now = QDateTime::currentDateTime();
    m_store->begin(allDone ? "Mark as Not Completed" : "Mark as Completed");
    for (auto i : ids) {
        m_options.keepVisible.insert(i);
        completeTask(m_store->task(i), !allDone, now);
    }
    m_store->commit();
}

void Backend::setCompleted(bool done) {
    const QDateTime now = QDateTime::currentDateTime();
    m_store->begin(done ? "Mark as Completed" : "Mark as Not Completed");
    for (auto i : targetIds()) {
        m_options.keepVisible.insert(i);
        completeTask(m_store->task(i), done, now);
    }
    m_store->commit();
}

void Backend::setPriority(int priority, qint64 id) {
    editTasks("Change Priority", targetIds(id), [&](Task &t) { t.priority = qBound(0, priority, 3); });
}

void Backend::bumpPriority(int delta) {
    editTasks(delta > 0 ? "Increase Priority" : "Decrease Priority", targetIds(),
              [&](Task &t) { t.priority = qBound(0, t.priority + delta, 3); });
}

void Backend::setStarred(int state, qint64 id) {
    const auto ids = targetIds(id);
    for (auto i : ids)
        m_options.keepVisible.insert(i);
    editTasks(state == 0 ? "Unstar" : "Star", ids, [&](Task &t) { t.starred = state < 0 ? !t.starred : state == 1; });
}

void Backend::nudgeDue(int days) {
    const QDate today = QDate::currentDate();
    editTasks("Change Due Date", targetIds(), [&](Task &t) {
        if (t.parentId && m_store->task(t.parentId).type == TaskType::Checklist) return;
        t.dueDate = t.dueDate.isValid() ? t.dueDate.addDays(days) : today.addDays(days > 0 ? days - 1 : days + 1);
    });
}

void Backend::nudgeStart(int days) {
    const QDate today = QDate::currentDate();
    editTasks("Change Start Date", targetIds(), [&](Task &t) {
        if (t.parentId && m_store->task(t.parentId).type == TaskType::Checklist) return;
        t.startDate = t.startDate.isValid() ? t.startDate.addDays(days) : today.addDays(days > 0 ? days - 1 : days + 1);
    });
}

void Backend::setDueInDays(int days) {
    const QDate due = QDate::currentDate().addDays(days);
    for (auto i : targetIds())
        m_options.keepVisible.insert(i);
    editTasks("Change Due Date", targetIds(), [&](Task &t) { t.dueDate = due; });
}

void Backend::setStartInDays(int days) {
    const QDate start = QDate::currentDate().addDays(days);
    editTasks("Change Start Date", targetIds(), [&](Task &t) { t.startDate = start; });
}

void Backend::clearDue() {
    editTasks("Remove Due Date", targetIds(), [](Task &t) {
        t.dueDate = {};
        t.dueTime = -1;
    });
}

void Backend::clearStart() {
    editTasks("Remove Start Date", targetIds(), [](Task &t) {
        t.startDate = {};
        t.startTime = -1;
    });
}

void Backend::deferTo(const QString &iso, int minutes) {
    for (auto i : targetIds())
        setDue(i, iso, minutes);
}

void Backend::scheduleTo(const QString &iso, int minutes) {
    for (auto i : targetIds())
        setStart(i, iso, minutes);
}

void Backend::setSelectionDuration(int minutes) {
    editTasks("Change Duration", targetIds(), [&](Task &t) { t.duration = qMax(0, minutes); });
}

void Backend::setSelectionDueTime(int minutes) {
    editTasks("Change Due Time", targetIds(), [&](Task &t) {
        if (!t.dueDate.isValid() && minutes >= 0)
            t.dueDate = QDate::currentDate();
        t.dueTime = minutes;
    });
}

void Backend::deleteTree(qint64 id) {
    Task t = m_store->task(id);
    ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
    for (const auto &c : views.children(id))
        deleteTree(c.id);
    t.deleted = true;
    m_store->putTask(t);
}

void Backend::deleteSelection() {
    const auto ids = targetIds();
    if (ids.isEmpty())
        return;
    // Move the cursor to the neighbour so the keyboard keeps its place.
    int i = m_visibleOrder.indexOf(ids.last());
    qint64 next = 0;
    for (int j = i + 1; j < m_visibleOrder.size() && !next; ++j)
        if (!ids.contains(m_visibleOrder.at(j))) next = m_visibleOrder.at(j);
    for (int j = i - 1; j >= 0 && !next; --j)
        if (!ids.contains(m_visibleOrder.at(j))) next = m_visibleOrder.at(j);
    m_editing = 0;
    m_store->begin(ids.size() > 1 ? QStringLiteral("Delete %1 Tasks").arg(ids.size()) : QStringLiteral("Delete Task"));
    for (auto id : ids)
        deleteTree(id);
    m_store->commit();
    m_selected = next ? QVector<qint64>{next} : QVector<qint64>{};
    m_cursor = m_anchor = next;
    rebuild();
    emit selectionChanged();
}

qint64 Backend::duplicateTree(const Task &source, qint64 parentId, qint64 listId) {
    Task copy = source;
    copy.id = m_store->nextId("tasks");
    copy.uid.clear();
    copy.created = {};
    copy.parentId = parentId;
    copy.listId = listId;
    copy.order = source.order + 0.5;
    m_store->putTask(copy);
    ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
    for (const auto &c : views.children(source.id))
        if (c.id != copy.id)
            duplicateTree(c, copy.id, listId);
    return copy.id;
}

void Backend::duplicateSelection() {
    const auto ids = targetIds();
    if (ids.isEmpty())
        return;
    QVector<qint64> copies;
    m_store->begin("Duplicate");
    for (auto id : ids) {
        const Task t = m_store->task(id);
        copies.append(duplicateTree(t, t.parentId, t.listId));
    }
    m_store->commit();
    for (auto id : copies)
        m_options.keepVisible.insert(id);
    m_selected = copies;
    m_cursor = m_anchor = copies.last();
    rebuild();
    emit selectionChanged();
}

void Backend::moveSelectionToList(qint64 listId) {
    if (!m_store->lists().contains(listId))
        return;
    const auto ids = targetIds();
    m_store->begin("Move to List");
    std::function<void(qint64)> move = [&](qint64 id) {
        Task t = m_store->task(id);
        t.listId = listId;
        if (ids.contains(id)) {
            // A moved sub-task leaves its parent unless the parent moves too.
            if (t.parentId && !ids.contains(t.parentId))
                t.parentId = 0;
            t.order = nextOrder(listId, t.parentId);
        }
        m_store->putTask(t);
        ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
        for (const auto &c : views.children(id))
            move(c.id);
    };
    for (auto id : ids)
        move(id);
    m_store->commit();
}

void Backend::moveSelection(int delta) {
    // ⌃⌘↑/↓: manual reordering among siblings, which also switches the list
    // to manual order the way dragging does in the reference app.
    if (m_cursor == 0)
        return;
    const Task t = m_store->task(m_cursor);
    ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
    QVector<Task> siblings;
    if (t.parentId) {
        siblings = views.children(t.parentId);
    } else {
        for (const auto &s : m_store->tasks())
            if (!s.deleted && s.listId == t.listId && !s.parentId)
                siblings.append(s);
        std::sort(siblings.begin(), siblings.end(), [](const Task &a, const Task &b) {
            return a.order != b.order ? a.order < b.order : a.id < b.id;
        });
    }
    int i = -1;
    for (int j = 0; j < siblings.size(); ++j)
        if (siblings.at(j).id == t.id) i = j;
    const int target = i + delta;
    if (i < 0 || target < 0 || target >= siblings.size())
        return;
    siblings.move(i, target);
    m_store->begin("Move Task");
    for (int j = 0; j < siblings.size(); ++j) {
        if (siblings.at(j).order != j + 1) {
            Task s = siblings.at(j);
            s.order = j + 1;
            m_store->putTask(s);
        }
    }
    m_store->commit();
    if (m_source.kind == Source::List && sortSpec().field != SortField::Manual)
        setSort(SortField::Manual);
}

void Backend::toggleCollapsed(qint64 id) {
    for (auto i : targetIds(id)) {
        Task t = m_store->task(i);
        if (t.parentId)
            t = m_store->task(t.parentId);
        t.collapsed = !t.collapsed;
        m_store->begin(t.collapsed ? "Collapse" : "Expand");
        m_store->putTask(t);
        m_store->commit();
    }
}

void Backend::convertSelection(int type) {
    editTasks("Convert", targetIds(), [&](Task &t) {
        if (!t.parentId || m_store->task(t.parentId).type == TaskType::Project)
            t.type = type;
    });
}

qint64 Backend::tagIdForName(const QString &raw, bool create) {
    const QString name = raw.trimmed().remove(QRegularExpression("^#"));
    if (name.isEmpty())
        return 0;
    for (const auto &t : m_store->tags())
        if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0)
            return t.id;
    if (!create)
        return 0;
    Tag tag;
    tag.id = m_store->nextId("tags");
    tag.name = name;
    tag.order = tag.id;
    m_store->putTag(tag);
    return tag.id;
}

void Backend::tagSelection(const QString &names, bool replace) {
    const auto ids = targetIds();
    m_store->begin("Tag");
    QList<qint64> tagIds;
    for (const auto &name : names.split(',', Qt::SkipEmptyParts))
        if (const qint64 id = tagIdForName(name, true))
            tagIds.append(id);
    for (auto id : ids) {
        Task t = m_store->task(id);
        if (replace)
            t.tagIds.clear();
        for (auto tagId : tagIds)
            if (!t.tagIds.contains(tagId))
                t.tagIds.append(tagId);
        m_store->putTask(t);
    }
    m_store->commit();
}

void Backend::untagSelection() {
    editTasks("Untag", targetIds(), [](Task &t) { t.tagIds.clear(); });
}

void Backend::performAction(qint64 id) {
    const Task t = m_store->task(id ? id : m_cursor);
    if (t.actionType == ActionType::None || t.actionValue.isEmpty()) {
        setStatus("This task has no action.");
        return;
    }
    const QString v = QString::fromUtf8(QUrl::toPercentEncoding(t.actionValue));
    QUrl url;
    switch (t.actionType) {
    case ActionType::Call: url = QUrl("tel:" + t.actionValue); break;
    case ActionType::Message: url = QUrl("sms:" + t.actionValue); break;
    case ActionType::Mail: url = QUrl("mailto:" + t.actionValue); break;
    case ActionType::Visit: url = QUrl("https://www.openstreetmap.org/search?query=" + v); break;
    case ActionType::Url: url = QUrl::fromUserInput(t.actionValue); break;
    case ActionType::Google: url = QUrl("https://www.google.com/search?q=" + v); break;
    }
    if (!QDesktopServices::openUrl(url))
        setStatus("Nothing on this system opens " + url.scheme() + ": links.");
}

void Backend::openLinks(qint64 id) {
    const Task t = m_store->task(id ? id : m_cursor);
    static const QRegularExpression links(R"((https?://[^\s<>()]+|www\.[^\s<>()]+))");
    int opened = 0;
    for (auto it = links.globalMatch(t.notes + " " + t.title); it.hasNext();) {
        QDesktopServices::openUrl(QUrl::fromUserInput(it.next().captured(1)));
        ++opened;
    }
    if (!opened)
        setStatus("No links in this task.");
}

// ----------------------------------------------------------- editor fields

void Backend::setTitle(qint64 id, const QString &title) {
    Task t = m_store->task(id);
    // Titles cap at 1,000 characters; anything longer moves into the notes.
    QString head = title.left(1000);
    if (!t.id || t.title == head)
        return;
    m_store->begin("Change Title");
    t.title = head;
    if (title.size() > 1000)
        t.notes = title.mid(1000) + (t.notes.isEmpty() ? "" : "\n" + t.notes);
    m_store->putTask(t);
    m_store->commit();
}

void Backend::setNotes(qint64 id, const QString &notes) {
    Task t = m_store->task(id);
    if (!t.id || t.notes == notes)
        return;
    m_store->begin("Change Notes");
    t.notes = notes;
    m_store->putTask(t);
    m_store->commit();
}

void Backend::setTaskList(qint64 id, qint64 listId) {
    const auto saved = m_selected;
    m_selected = {id};
    moveSelectionToList(listId);
    m_selected = saved;
}

void Backend::setDue(qint64 id, const QString &iso, int minutes) {
    Task t = m_store->task(id);
    if (!t.id)
        return;
    const QDate date = isoDate(iso);
    m_store->begin("Change Due Date");
    t.dueDate = date;
    if (!date.isValid())
        t.dueTime = -1;
    else if (minutes != -2)
        t.dueTime = minutes;
    m_store->putTask(t);
    // Rescheduling a project sets its finished children back to active.
    if (t.type == TaskType::Project) {
        ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
        for (auto c : views.children(t.id)) {
            if (c.completed) {
                c.completed = false;
                c.completedAt = {};
                m_store->putTask(c);
            }
        }
    }
    m_store->commit();
    m_options.keepVisible.insert(id);
}

void Backend::setStart(qint64 id, const QString &iso, int minutes) {
    Task t = m_store->task(id);
    if (!t.id)
        return;
    const QDate date = isoDate(iso);
    m_store->begin("Change Start Date");
    t.startDate = date;
    if (!date.isValid())
        t.startTime = -1;
    else if (minutes != -2)
        t.startTime = minutes;
    m_store->putTask(t);
    m_store->commit();
    m_options.keepVisible.insert(id);
}

bool Backend::setDueFromText(qint64 id, const QString &text) {
    if (text.trimmed().isEmpty()) {
        setDue(id, {}, -1);
        return true;
    }
    const auto s = DateParse::best(text, QDateTime::currentDateTime());
    if (!s.date.isValid() && s.minutes < 0)
        return false;
    const QDate date = s.date.isValid() ? s.date : (m_store->task(id).dueDate.isValid() ? m_store->task(id).dueDate : QDate::currentDate());
    setDue(id, date.toString(Qt::ISODate), s.minutes >= 0 ? s.minutes : -2);
    return true;
}

bool Backend::setStartFromText(qint64 id, const QString &text) {
    if (text.trimmed().isEmpty()) {
        setStart(id, {}, -1);
        return true;
    }
    const auto s = DateParse::best(text, QDateTime::currentDateTime());
    if (!s.date.isValid() && s.minutes < 0)
        return false;
    const QDate date = s.date.isValid() ? s.date : (m_store->task(id).startDate.isValid() ? m_store->task(id).startDate : QDate::currentDate());
    setStart(id, date.toString(Qt::ISODate), s.minutes >= 0 ? s.minutes : -2);
    return true;
}

bool Backend::setTimeFromText(qint64 id, bool due, const QString &text) {
    Task t = m_store->task(id);
    if (!t.id)
        return false;
    const int minutes = text.trimmed().isEmpty() ? -1 : DateParse::parseTime(text, QDateTime::currentDateTime());
    if (minutes < 0 && !text.trimmed().isEmpty())
        return false;
    m_store->begin(due ? "Change Due Time" : "Change Start Time");
    if (due) {
        if (!t.dueDate.isValid() && minutes >= 0) t.dueDate = QDate::currentDate();
        t.dueTime = minutes;
    } else {
        if (!t.startDate.isValid() && minutes >= 0) t.startDate = QDate::currentDate();
        t.startTime = minutes;
    }
    m_store->putTask(t);
    m_store->commit();
    return true;
}

void Backend::setDuration(qint64 id, int minutes) {
    editTasks("Change Duration", {id}, [&](Task &t) { t.duration = qMax(0, minutes); });
}

void Backend::addTag(qint64 id, const QString &name) {
    Task t = m_store->task(id);
    if (!t.id || name.trimmed().isEmpty())
        return;
    m_store->begin("Add Tag");
    const qint64 tagId = tagIdForName(name, true);
    if (!t.tagIds.contains(tagId))
        t.tagIds.append(tagId);
    m_store->putTask(t);
    m_store->commit();
}

void Backend::removeTag(qint64 id, qint64 tagId) {
    editTasks("Remove Tag", {id}, [&](Task &t) { t.tagIds.removeAll(tagId); });
}

void Backend::setType(qint64 id, int type) {
    editTasks("Convert", {id}, [&](Task &t) { t.type = type; });
}

void Backend::setAction(qint64 id, int type, const QString &value) {
    editTasks("Change Action", {id}, [&](Task &t) {
        t.actionType = type;
        t.actionValue = type == ActionType::None ? QString() : value;
    });
}

void Backend::setRepeatPreset(qint64 id, int preset) {
    // the reference app's ⌥1–⌥6 presets: Daily, Weekly, Every 2 weeks, Monthly, Quarterly,
    // Yearly; 0 turns repeating off.
    editTasks("Change Recurrence", {id}, [&](Task &t) {
        Recurrence r;
        r.from = t.recurrence.from;
        switch (preset) {
        case 1: r.frequency = Repeat::Daily; break;
        case 2: r.frequency = Repeat::Weekly; break;
        case 3: r.frequency = Repeat::Weekly; r.interval = 2; break;
        case 4: r.frequency = Repeat::Monthly; break;
        case 5: r.frequency = Repeat::Monthly; r.interval = 3; break;
        case 6: r.frequency = Repeat::Yearly; break;
        default: r = {}; break;
        }
        if (r.isSet() && !t.dueDate.isValid() && !t.startDate.isValid())
            t.dueDate = QDate::currentDate();  // A repeat needs a date to count from.
        t.recurrence = r;
    });
}

void Backend::setRepeatFromCompletion(qint64 id, bool on) {
    editTasks("Change Recurrence", {id}, [&](Task &t) { t.recurrence.from = on ? Repeat::FromCompletion : Repeat::FromDue; });
}

QVariantList Backend::dateSuggestions(const QString &text) const {
    QVariantList result;
    for (const auto &s : DateParse::suggest(text, QDateTime::currentDateTime()))
        result.append(QVariantMap{{"label", s.label}, {"date", s.date.isValid() ? s.date.toString(Qt::ISODate) : QString()},
                                  {"minutes", s.minutes}});
    return result;
}

// ------------------------------------------------------ lists, groups, tags

qint64 Backend::newList(const QString &name) {
    TaskList l;
    l.id = m_store->nextId("lists");
    l.name = name.isEmpty() ? QStringLiteral("New List") : name;
    // A new list joins the group of the list you're in, as the reference app does.
    if (m_source.kind == Source::List)
        l.groupId = m_store->list(m_source.id).groupId;
    l.order = l.id;
    l.color = int(m_store->lists().size()) % 13;
    m_store->begin("New List");
    m_store->putList(l);
    m_store->commit();
    selectSource(Source{Source::List, l.id}.key());
    return l.id;
}

qint64 Backend::newGroup(const QString &name) {
    ListGroup g;
    g.id = m_store->nextId("list_groups");
    g.name = name.isEmpty() ? QStringLiteral("New Group") : name;
    g.order = g.id;
    m_store->begin("New List Group");
    m_store->putGroup(g);
    m_store->commit();
    return g.id;
}

void Backend::renameList(qint64 id, const QString &name) {
    TaskList l = m_store->list(id);
    if (!l.id || name.trimmed().isEmpty() || l.name == name.trimmed())
        return;
    l.name = name.trimmed().left(200);
    m_store->begin("Rename List");
    m_store->putList(l);
    m_store->commit();
    emit viewChanged();
}

void Backend::renameGroup(qint64 id, const QString &name) {
    ListGroup g = m_store->groups().value(id);
    if (!g.id || name.trimmed().isEmpty())
        return;
    g.name = name.trimmed();
    m_store->begin("Rename Group");
    m_store->putGroup(g);
    m_store->commit();
}

void Backend::setListColor(qint64 id, int color) {
    TaskList l = m_store->list(id);
    if (!l.id)
        return;
    l.color = color;
    m_store->begin("Change List Color");
    m_store->putList(l);
    m_store->commit();
    emit viewChanged();
}

void Backend::setListFlag(qint64 id, const QString &flag, bool on) {
    TaskList l = m_store->list(id);
    if (!l.id)
        return;
    if (flag == "all") l.showInAll = on;
    else if (flag == "today") l.showInToday = on;
    else if (flag == "done") l.showInDone = on;
    else if (flag == "archived") l.archived = on;
    m_store->begin("Change List");
    m_store->putList(l);
    m_store->commit();
}

void Backend::deleteList(qint64 id) {
    TaskList l = m_store->list(id);
    if (!l.id)
        return;
    m_store->begin("Delete List");
    for (const auto &t : m_store->tasks()) {
        if (t.listId == id && !t.deleted) {
            Task copy = t;
            copy.deleted = true;
            m_store->putTask(copy);
        }
    }
    l.deleted = true;
    m_store->putList(l);
    m_store->commit();
    if (m_source.kind == Source::List && m_source.id == id)
        selectSource("today");
}

QVariantMap Backend::listInfo(qint64 id) const {
    const TaskList l = m_store->list(id);
    return {{"id", l.id}, {"name", l.name}, {"color", l.color}, {"showInAll", l.showInAll},
            {"showInToday", l.showInToday}, {"showInDone", l.showInDone}, {"archived", l.archived}};
}

QVariantMap Backend::taskInfo(qint64 id) const {
    // Everything Quick Look shows about a task.
    const Task t = m_store->task(id);
    if (!t.id)
        return {};
    const QDateTime now = QDateTime::currentDateTime();
    const ScheduleSettings s = scheduleSettings();
    const TaskList list = m_store->list(t.listId);
    QStringList tags;
    for (auto tagId : t.tagIds)
        if (m_store->tag(tagId).id)
            tags << m_store->tag(tagId).name;
    ViewBuilder views(*m_store, now, s);
    int total = 0, done = 0;
    for (const auto &c : views.children(t.id)) {
        ++total;
        if (c.completed) ++done;
    }
    return {
        {"id", t.id}, {"title", t.title}, {"notes", t.notes}, {"type", t.type}, {"listName", list.name},
        {"listId", t.listId}, {"parentId", t.parentId},
        {"listColor", list.color}, {"parent", t.parentId ? m_store->task(t.parentId).title : QString()},
        {"priority", t.priority}, {"starred", t.starred}, {"completed", t.completed},
        {"start", t.startDate.isValid() ? Schedule::friendlyDateTime(t.startDate, t.startTime, now.date(), false) : QString()},
        {"due", t.dueDate.isValid() ? Schedule::friendlyDateTime(t.dueDate, t.dueTime, now.date(), false) : QString()},
        {"overdue", Schedule::isOverdue(t, now, s)}, {"hint", Schedule::dueHint(t, now, s)},
        {"repeat", Recurrences::describe(t.recurrence)}, {"duration", Schedule::durationBadge(t.duration)},
        {"tags", tags}, {"actionType", t.actionType}, {"actionValue", t.actionValue},
        {"childTotal", total}, {"childDone", done},
    };
}

QVariantList Backend::findTasks(const QString &text, int limit) const {
    // Quick Open: title matches across every list, prefix matches first.
    QVector<Task> hits;
    for (const auto &t : m_store->tasks())
        if (!t.deleted && !t.title.isEmpty() && t.title.contains(text, Qt::CaseInsensitive) && m_store->lists().contains(t.listId)
            && !m_store->list(t.listId).deleted)
            hits.append(t);
    std::sort(hits.begin(), hits.end(), [&](const Task &a, const Task &b) {
        const bool pa = a.title.startsWith(text, Qt::CaseInsensitive), pb = b.title.startsWith(text, Qt::CaseInsensitive);
        if (pa != pb) return pa;
        if (a.completed != b.completed) return !a.completed;
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    QVariantList result;
    for (const auto &t : hits.mid(0, limit)) {
        const TaskList l = m_store->list(t.listId);
        result.append(QVariantMap{{"id", t.id}, {"title", t.title}, {"listName", l.name}, {"listColor", l.color},
                                  {"completed", t.completed}});
    }
    return result;
}

void Backend::revealInList(qint64 taskId) {
    // Switches to the task's home list, out of any filter, and selects it.
    const Task t = m_store->task(taskId);
    if (!t.id)
        return;
    m_options.search.clear();
    m_options.tagFilter.clear();
    if (t.completed)
        m_options.keepVisible.insert(t.id);
    if (Schedule::isScheduled(t, QDateTime::currentDateTime()))
        m_options.showScheduled = true;
    selectSource(Source{Source::List, t.listId}.key());
    if (t.parentId) {
        Task parent = m_store->task(t.parentId);
        if (parent.collapsed) {
            parent.collapsed = false;
            m_store->begin("Expand");
            m_store->putTask(parent);
            m_store->commit();
        }
    }
    rebuild();
    emit viewChanged();
    selectTask(taskId);
    emit revealTask(taskId);
}

void Backend::setTagPaused(qint64 tagId, bool paused) {
    Tag t = m_store->tag(tagId);
    if (!t.id)
        return;
    t.paused = paused;
    m_store->begin(paused ? "Pause Tag" : "Resume Tag");
    m_store->putTag(t);
    m_store->commit();
}

qint64 Backend::newTag(const QString &name, qint64 groupId) {
    m_store->begin("New Tag");
    const qint64 id = tagIdForName(name, true);
    Tag t = m_store->tag(id);
    if (groupId && t.groupId != groupId) {
        t.groupId = groupId;
        m_store->putTag(t);
    }
    m_store->commit();
    return id;
}

void Backend::undo() {
    m_editing = 0;
    m_newTask = 0;
    if (!m_store->undo())
        return;
    setStatus("Undo " + m_store->redoLabel());
}

void Backend::redo() {
    if (!m_store->redo())
        return;
    setStatus("Redo " + m_store->undoLabel());
}

// -------------------------------------------------------------- rebuilding

void Backend::saveUiState() {
    m_store->setSetting("ui.source", m_source.key());
    m_store->setSetting("ui.collapsedSections", QStringList(m_options.collapsedSections.cbegin(),
                                                            m_options.collapsedSections.cend()).join('|'));
    QStringList expanded;
    for (auto id : m_expandedLists)
        expanded << QString::number(id);
    m_store->setSetting("ui.expandedLists", expanded.join(','));
}

void Backend::rebuild() {
    // A source that vanished (list deleted, project removed) falls back to Today.
    if ((m_source.kind == Source::List && (!m_store->lists().contains(m_source.id) || m_store->list(m_source.id).deleted)) ||
        (m_source.kind == Source::Project && (!m_store->tasks().contains(m_source.id) || m_store->task(m_source.id).deleted))) {
        m_source = Source{};
        emit viewChanged();
    }
    if (m_editing && (!m_store->tasks().contains(m_editing) || m_store->task(m_editing).deleted))
        m_editing = 0;

    const ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
    rebuildSidebar(views);
    rebuildTasks(views);
    rebuildTags(views);
    rebuildEditor();
}

void Backend::rebuildSidebar(const ViewBuilder &views) {
    QVector<QVariantMap> rows;
    static const char *focusIcons[] = {"all", "today", "starred", "scheduled", "done"};
    static const char *focusTitles[] = {"All", "Today", "Starred", "Scheduled", "Done"};
    const auto lists = views.orderedLists();

    // The GTD Inbox, when present, sits in its own group above the focus lists.
    for (const auto &l : lists) {
        if (l.kind != ListKind::Inbox)
            continue;
        const auto c = views.countsForList(l.id);
        const QString key = Source{Source::List, l.id}.key();
        rows.append({{"key", "inbox"}, {"kind", "focus"}, {"sourceKey", key}, {"title", "Inbox"}, {"icon", "inbox"},
                     {"overdue", c.overdue}, {"undone", c.undone}, {"selected", m_source.key() == key}, {"listId", l.id}});
        rows.append({{"key", "gap:inbox"}, {"kind", "gap"}});
    }
    for (int k = Source::All; k <= Source::Done; ++k) {
        const auto c = views.countsForFocus(k);
        const bool showOverdue = k == Source::All || k == Source::Today;
        rows.append({{"key", focusKey(k)}, {"kind", "focus"}, {"sourceKey", focusKey(k)}, {"title", focusTitles[k]},
                     {"icon", focusIcons[k]}, {"overdue", showOverdue ? c.overdue : 0}, {"undone", c.undone},
                     {"selected", m_source.key() == focusKey(k)},
                     {"dot", k == Source::Today && c.overdue ? "overdue" : ""}});
    }

    qint64 lastGroup = -1;
    for (const auto &l : lists) {
        if (l.kind == ListKind::Inbox)
            continue;
        const auto g = m_store->groups().value(l.groupId);
        const qint64 groupId = (g.id && !g.deleted) ? g.id : 0;
        if (groupId != lastGroup) {
            lastGroup = groupId;
            if (groupId) {
                rows.append({{"key", QStringLiteral("g:%1").arg(groupId)}, {"kind", "header"}, {"title", g.name},
                             {"groupId", groupId}, {"collapsed", g.collapsed}});
            } else {
                rows.append({{"key", "gap:ungrouped"}, {"kind", "gap"}});
            }
        }
        if (groupId && g.collapsed)
            continue;
        const auto c = views.countsForList(l.id);
        const QString key = Source{Source::List, l.id}.key();
        QVector<Task> projects;
        for (const auto &t : m_store->tasks())
            if (t.listId == l.id && !t.parentId && !t.deleted && !t.completed && t.type != TaskType::Task)
                projects.append(t);
        std::sort(projects.begin(), projects.end(), [](const Task &a, const Task &b) { return a.order < b.order; });
        const bool expanded = m_expandedLists.contains(l.id);
        rows.append({{"key", key}, {"kind", "list"}, {"sourceKey", key}, {"title", l.name}, {"overdue", c.overdue},
                     {"undone", c.undone}, {"color", l.color}, {"selected", m_source.key() == key},
                     {"dot", c.overdue ? "overdue" : c.dueToday ? "today" : ""}, {"listId", l.id},
                     {"groupId", groupId}, {"expandable", !projects.isEmpty()}, {"expanded", expanded},
                     {"excluded", !l.showInAll || !l.showInToday}});
        if (expanded) {
            for (const auto &p : projects) {
                const QString pkey = Source{Source::Project, p.id}.key();
                rows.append({{"key", pkey}, {"kind", "project"}, {"sourceKey", pkey}, {"title", p.title},
                             {"icon", p.type == TaskType::Project ? "project" : "checklist"}, {"color", l.color},
                             {"selected", m_source.key() == pkey}, {"depth", 1}, {"taskId", p.id},
                             {"listId", l.id}, {"excluded", views.isPaused(p)}});
            }
        }
    }
    m_sidebar.setRows(rows);
}

QVariantMap Backend::taskRow(const Task &t, const TaskRow &row, const ViewBuilder &views) const {
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    const ScheduleSettings s = scheduleSettings();
    const TaskList list = m_store->list(t.listId);

    QStringList tags;
    for (auto id : t.tagIds) {
        const Tag tag = m_store->tag(id);
        if (tag.id && !tag.deleted)
            tags << tag.name;
    }
    int childCount = 0, childOpen = 0;
    for (const auto &c : views.children(t.id)) {
        ++childCount;
        if (!c.completed) ++childOpen;
    }
    const bool overdue = Schedule::isOverdue(t, now, s);
    QString dueTone;
    if (t.dueDate.isValid())
        dueTone = overdue ? "overdue" : t.dueDate == today ? "today" : "future";
    const bool futureStart = t.startDate.isValid() && (t.startDate > today || Schedule::isScheduled(t, now));
    const Task parent = t.parentId ? m_store->task(t.parentId) : Task();
    const QString firstLine = t.notes.section('\n', 0, 0).trimmed();

    return {
        {"key", QStringLiteral("t:%1").arg(t.id)},
        {"kind", "task"},
        {"taskId", t.id},
        {"depth", row.depth},
        {"title", t.title},
        {"notesPreview", firstLine.left(200)},
        {"hasNotes", !t.notes.trimmed().isEmpty()},
        {"type", t.type},
        {"childCount", childCount},
        {"childOpen", childOpen},
        {"completed", t.completed},
        {"overdue", overdue},
        {"starred", t.starred},
        {"priority", t.priority},
        {"repeating", t.recurrence.isSet()},
        {"tags", tags},
        {"actionType", t.actionType},
        {"durationText", Schedule::durationBadge(t.duration)},
        {"dueText", Schedule::friendlyDateTime(t.dueDate, t.dueTime, today, s.friendlyDates)},
        {"dueTone", dueTone},
        {"startText", futureStart ? Schedule::friendlyDateTime(t.startDate, t.startTime, today, s.friendlyDates) : QString()},
        {"listName", list.name},
        {"listColor", list.color},
        {"breadcrumb", row.breadcrumb ? parent.title : QString()},
        {"collapsed", t.collapsed},
        {"hasChildren", childCount > 0},
        {"selected", m_selected.contains(t.id)},
        {"current", m_cursor == t.id},
        {"editing", m_editing == t.id},
        // Hidden-by-default tasks shown on request read as dimmed, except in
        // Scheduled, where being scheduled is the point.
        {"dimmed", views.isPaused(t) || (Schedule::isScheduled(t, now) && m_source.kind != Source::Scheduled)},
        {"progress", childCount ? (childCount - childOpen) * 100 / childCount : 0},
    };
}

void Backend::rebuildTasks(const ViewBuilder &views) {
    const auto sections = views.build(m_source, sortSpec(), m_options);
    QVector<QVariantMap> rows;
    QVector<qint64> order;
    QSet<QString> seen;
    bool first = true;
    for (const auto &section : sections) {
        rows.append({{"key", "s:" + section.key}, {"kind", "section"}, {"sectionKey", section.key},
                     {"title", section.title}, {"tone", section.tone}, {"color", section.color},
                     {"count", section.openCount}, {"durationTotal", Schedule::durationTotal(section.durationTotal)},
                     {"collapsed", section.collapsed}, {"isFirst", first}});
        first = false;
        if (section.collapsed)
            continue;
        for (const auto &row : section.rows) {
            QVariantMap map = taskRow(m_store->task(row.taskId), row, views);
            // A task can appear in two sections (a project and its child in
            // different date groups); keys stay unique per occurrence.
            QString key = map.value("key").toString();
            while (seen.contains(key))
                key += "'";
            seen.insert(key);
            map["key"] = key;
            rows.append(map);
            if (!order.contains(row.taskId))
                order.append(row.taskId);
        }
    }
    m_visibleOrder = order;
    m_tasks.setRows(rows);

    // Whether the sort bar should offer its reveal toggle: something here is
    // hidden by a future start date or a paused tag.
    ViewOptions probe = m_options;
    probe.showScheduled = true;
    probe.showPaused = true;
    int all = 0, now = 0;
    for (const auto &s : views.build(m_source, sortSpec(), probe)) all += s.rows.size();
    for (const auto &s : sections) now += s.rows.size();
    const bool hidden = all != now || m_options.showScheduled || m_options.showPaused;
    if (hidden != m_hasHidden) {
        m_hasHidden = hidden;
        emit viewChanged();
    }
}

void Backend::rebuildTags(const ViewBuilder &views) {
    QHash<qint64, int> counts;
    for (const auto &t : m_store->tasks()) {
        if (t.deleted || t.completed || !views.listVisible(t.listId))
            continue;
        for (auto id : t.tagIds)
            counts[id]++;
    }
    QVector<Tag> tags;
    for (const auto &t : m_store->tags())
        if (!t.deleted)
            tags.append(t);
    auto groupName = [this](qint64 id) {
        const auto g = m_store->tagGroups().value(id);
        return g.deleted ? QString() : g.name;
    };
    std::sort(tags.begin(), tags.end(), [&](const Tag &a, const Tag &b) {
        const auto ga = m_store->tagGroups().value(a.groupId), gb = m_store->tagGroups().value(b.groupId);
        const bool ua = !ga.id || ga.deleted, ub = !gb.id || gb.deleted;
        if (ua != ub) return ua;  // Ungrouped tags first, under "Tags".
        if (!ua && ga.order != gb.order) return ga.order < gb.order;
        if (a.groupId != b.groupId) return a.groupId < b.groupId;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    QVector<QVariantMap> rows;
    qint64 lastGroup = -1;
    for (const auto &t : tags) {
        const qint64 gid = groupName(t.groupId).isEmpty() ? 0 : t.groupId;
        if (gid != lastGroup) {
            lastGroup = gid;
            rows.append({{"key", QStringLiteral("g:%1").arg(gid)}, {"kind", "header"},
                         {"title", gid ? groupName(gid) : QStringLiteral("Tags")}, {"groupId", gid}});
        }
        rows.append({{"key", QStringLiteral("t:%1").arg(t.id)}, {"kind", "tag"}, {"tagId", t.id}, {"groupId", gid},
                     {"title", t.name}, {"count", counts.value(t.id)}, {"paused", t.paused},
                     {"filtered", m_options.tagFilter.contains(t.id)}});
    }
    m_tags.setRows(rows);
}

void Backend::rebuildEditor() {
    QVariantMap editor;
    if (m_editing && m_store->tasks().contains(m_editing)) {
        const Task t = m_store->task(m_editing);
        const QDateTime now = QDateTime::currentDateTime();
        const QDate today = now.date();
        const TaskList list = m_store->list(t.listId);
        QVariantList tags;
        for (auto id : t.tagIds) {
            const Tag tag = m_store->tag(id);
            if (tag.id && !tag.deleted)
                tags.append(QVariantMap{{"id", tag.id}, {"name", tag.name}});
        }
        const Task parent = t.parentId ? m_store->task(t.parentId) : Task();
        editor = {
            {"id", t.id},
            {"title", t.title},
            {"notes", t.notes},
            {"type", t.type},
            {"listId", t.listId},
            {"listName", list.name},
            {"listColor", list.color},
            {"priority", t.priority},
            {"starred", t.starred},
            {"tags", tags},
            {"startDate", t.startDate.isValid() ? English.toString(t.startDate, "ddd, d MMM") : QString()},
            {"startTime", Schedule::formatTime(t.startTime)},
            {"dueDate", t.dueDate.isValid() ? English.toString(t.dueDate, "ddd, d MMM") : QString()},
            {"dueTime", Schedule::formatTime(t.dueTime)},
            {"hasStart", t.startDate.isValid()},
            {"hasDue", t.dueDate.isValid()},
            {"duration", t.duration},
            {"durationText", Schedule::durationBadge(t.duration)},
            {"repeat", Recurrences::describe(t.recurrence)},
            {"repeatPreset", t.recurrence.isSet() ? 1 : 0},
            {"repeatFromCompletion", t.recurrence.from == Repeat::FromCompletion},
            {"actionType", t.actionType},
            {"actionValue", t.actionValue},
            {"hint", Schedule::dueHint(t, now, scheduleSettings())},
            {"created", "Created: " + Schedule::friendlyDate(t.created.date(), today) + " at " +
                            Schedule::formatTime(t.created.time().hour() * 60 + t.created.time().minute())},
            {"isChecklistItem", parent.id && parent.type == TaskType::Checklist},
            {"isNew", t.id == m_newTask},
        };
    }
    if (editor != m_editor) {
        m_editor = editor;
        emit editorChanged();
    }
}

// ------------------------------------------------------------------ seeding

void Backend::ensureDefaults() {
    if (!m_store->lists().isEmpty())
        return;
    m_store->begin("Defaults");
    ListGroup personal{m_store->nextId("list_groups"), {}, "Personal", 1};
    m_store->putGroup(personal);
    TaskList home;
    home.id = m_store->nextId("lists");
    home.name = "Home";
    home.groupId = personal.id;
    home.color = 0;
    home.order = 1;
    m_store->putList(home);
    TaskList work;
    work.id = m_store->nextId("lists");
    work.name = "Work";
    work.groupId = personal.id;
    work.color = 2;
    work.order = 2;
    m_store->putList(work);
    m_store->commit();
    rebuild();
}
