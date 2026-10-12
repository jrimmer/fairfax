#include "backend.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

#include "dateparse.h"
#include "alarms.h"
#include "automation.h"
#include "exchange.h"
#include "query.h"
#include "recurrence.h"

namespace {

const QLocale English(QLocale::English, QLocale::UnitedKingdom);

const QStringList TaskRoles{
    "sectionId", "hasAlarm", "hasLocation", "hasAttachment", "pictureUrl", "kind", "taskId", "depth", "title", "notesPreview", "type", "childCount", "childOpen", "completed",
    "overdue", "starred", "priority", "repeating", "tags", "actionType", "durationText", "dueText", "dueTone",
    "startText", "listName", "listColor", "breadcrumb", "collapsed", "hasChildren", "selected", "current",
    "editing", "dimmed", "progress", "hasNotes", "sectionKey", "tone", "count", "durationTotal", "color",
    "isFirst",
};
const QStringList SidebarRoles{"kind", "sourceKey", "title", "icon", "overdue", "undone", "color", "dot",
                               "selected", "depth", "expandable", "expanded", "collapsed", "excluded",
                               "groupId", "listId", "taskId"};
const QStringList TagRoles{"kind", "tagId", "groupId", "title", "count", "paused", "filtered", "collapsed"};
const QStringList PlaceRoles{"kind", "locationId", "title", "address", "count", "paused", "filtered"};

QDate isoDate(const QString &text) { return QDate::fromString(text, Qt::ISODate); }

QString focusKey(int kind) { return Source{kind, 0}.key(); }

}  // namespace

Backend::Backend(Store *store, QObject *parent)
    : QObject(parent), m_store(store), m_sidebar(SidebarRoles), m_tasks(TaskRoles), m_tags(TagRoles), m_places(PlaceRoles) {
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

    m_backupTimer.setInterval(60 * 60 * 1000);
    connect(&m_backupTimer, &QTimer::timeout, this, &Backend::maybeBackup);
    m_backupTimer.start();
    QTimer::singleShot(5000, this, &Backend::maybeBackup);

    // Build the panes from what's already in the database straight away;
    // otherwise they'd stay empty until the first change or clock tick.
    rebuild();
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
        ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
        return views.projectSort(m_source.id);
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
    if (m_source.kind == Source::Project) {
        setProjectSort(m_source.id, spec.field, spec.ascending);
        return;
    }
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
        {SortField::Status, "Status"},     {SortField::Manual, "Manually"},
    };
    if (m_source.kind == Source::Done)
        all = {{SortField::Priority, "Priority"}, {SortField::List, "To-do List"}, {SortField::Modified, "Modified"},
               {SortField::Completion, "Completion"}, {SortField::Title, "Title"}};
    if (sourceIsSmart())
        all.removeIf([](const auto &choice) { return choice.first == SortField::Manual; });
    if (m_source.kind == Source::List && !m_store->sectionsOf(m_source.id).isEmpty())
        all.prepend({SortField::Section, "Section"});
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

void Backend::setProjectSort(qint64 projectId, int field, bool ascending) {
    const QString prefix = QStringLiteral("sort.project:%1.").arg(projectId);
    m_store->setSetting(prefix + "field", field);
    m_store->setSetting(prefix + "ascending", ascending);
    rebuild();
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

void Backend::setDayRange(const QString &fromIso, const QString &toIso) {
    QDate from = isoDate(fromIso), to = isoDate(toIso);
    if (!from.isValid() || !to.isValid())
        return setDayFilter(fromIso);
    if (to < from)
        std::swap(from, to);
    m_options.dayFilterFrom = from;
    m_options.dayFilterTo = to;
    rebuild();
    emit viewChanged();
}

QString Backend::dayFilterTo() const {
    return m_options.dayFilterTo.isValid() ? m_options.dayFilterTo.toString(Qt::ISODate) : QString();
}

QVariantList Backend::monthDays(int year, int month) const {
    // Busy-day counts for the mini calendar. Each task counts once, on its
    // due day or, failing that, its start day, as in the reference app; repeating tasks
    // also mark their next occurrences within the month.
    const QDate first(year, month, 1);
    QHash<QDate, int> counts;
    for (const auto &t : m_store->tasks()) {
        if (t.deleted || t.completed)
            continue;
        if (t.dueDate.isValid()) counts[t.dueDate]++;
        else if (t.startDate.isValid()) counts[t.startDate]++;
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
    m_history.removeAll(id);
    m_history.append(id);
    if (m_history.size() > 50)
        m_history.removeFirst();
    m_historyStep = 0;
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
    } else if (id == m_newTask && m_store->task(id).dueDate.isValid()) {
        // A new task that inherited a due date (from Today, say) gets the
        // default reminder once it has a title.
        m_store->begin("Default Reminder", false);
        AlarmTimes::applyDefaultReminder(*m_store, m_store->task(id));
        m_store->commit();
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
        // In a sectioned list a new task joins the section you're in.
        if (sortSpec().field == SortField::Section && current.id && current.listId == t.listId)
            t.sectionId = current.parentId ? m_store->task(current.parentId).sectionId : current.sectionId;
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
        if (m_source.kind == Source::Today && m_store->setting("tasks.todayDue", true).toBool())
            t.dueDate = QDate::currentDate();
        if (m_source.kind == Source::Starred)
            t.starred = true;
        // "Default due date for new tasks": days from today, -1 when off.
        const int offset = m_store->setting("tasks.defaultDueOffset", -1).toInt();
        if (!t.dueDate.isValid() && offset >= 0 && !m_store->setting("tasks.defaultDueQuickEntryOnly", false).toBool())
            t.dueDate = offset == 30 ? QDate::currentDate().addMonths(1) : QDate::currentDate().addDays(offset);
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
    emit taskCompleted(!allDone);
    if (!allDone) {
        bool anyOpen = false;
        for (const auto &row : m_tasks.rows())
            anyOpen = anyOpen || (row.value("kind") == "task" && !row.value("completed").toBool());
        if (!anyOpen)
            emit celebrate();
    }
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
    editTasks("Change Due Date", targetIds(), [&](Task &t) {
        t.dueDate = due;
        AlarmTimes::applyDefaultReminder(*m_store, t);
    });
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
        if (t.listId != listId)
            t.sectionId = 0;
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

QString Backend::dropHint(const QString &key) const {
    const Source target = Source::fromKey(key);
    switch (target.kind) {
    case Source::Today: return "Drop to Do Today";
    case Source::Starred: return "Drop to Star";
    case Source::Done: return "Drop to Complete";
    case Source::Scheduled: return "Drop to Start Tomorrow";
    case Source::List: return m_store->list(target.id).kind == ListKind::Smart ? QString() : "Drop to Move";
    case Source::Project: return "Drop into Project";
    default: return {};
    }
}

void Backend::dropOnSource(const QString &key) {
    const Source target = Source::fromKey(key);
    const auto ids = targetIds();
    if (ids.isEmpty())
        return;
    switch (target.kind) {
    case Source::Today: setDueInDays(0); break;
    case Source::Starred: setStarred(1); break;
    case Source::Done: setCompleted(true); break;
    case Source::Scheduled: setStartInDays(1); break;
    case Source::List:
        if (m_store->list(target.id).kind != ListKind::Smart)
            moveSelectionToList(target.id);
        break;
    case Source::Project: {
        const Task project = m_store->task(target.id);
        m_store->begin("Move into Project");
        for (auto id : ids) {
            if (id == project.id)
                continue;
            Task t = m_store->task(id);
            t.listId = project.listId;
            t.parentId = project.id;
            t.order = nextOrder(project.listId, project.id);
            m_store->putTask(t);
        }
        m_store->commit();
        break;
    }
    default: break;
    }
}

void Backend::dropOnDate(const QString &iso) {
    deferTo(iso);
}

void Backend::dropBefore(qint64 taskId) {
    // Dragging within a list puts it in manual order, as in the reference app.
    const Task anchor = m_store->task(taskId);
    auto ids = targetIds();
    ids.removeAll(taskId);
    if (!anchor.id || ids.isEmpty())
        return;
    QVector<Task> siblings;
    for (const auto &s : m_store->tasks())
        if (!s.deleted && s.listId == anchor.listId && s.parentId == anchor.parentId && !ids.contains(s.id))
            siblings.append(s);
    std::sort(siblings.begin(), siblings.end(), [](const Task &a, const Task &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    QVector<Task> ordered;
    for (const auto &s : siblings) {
        if (s.id == anchor.id)
            for (auto id : ids)
                ordered.append(m_store->task(id));
        ordered.append(s);
    }
    m_store->begin("Move Task");
    for (int i = 0; i < ordered.size(); ++i) {
        Task t = ordered.at(i);
        t.order = i + 1;
        t.listId = anchor.listId;
        t.parentId = anchor.parentId;
        m_store->putTask(t);
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
    // A new task's title is read for dates, tags, repeats and the rest, which
    // then come out of the title, as the reference app's natural-language input does.
    if (id == m_newTask && m_store->setting("nl.editor", true).toBool())
        applyParsed(t, NaturalLanguage::parse(head, QDateTime::currentDateTime(), listNames(), locationNames()));
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
    const bool hadDue = t.dueDate.isValid();
    t.dueDate = date;
    if (!date.isValid())
        t.dueTime = -1;
    else if (minutes != -2)
        t.dueTime = minutes;
    else if (!hadDue && m_store->setting("tasks.useDefaultDueTime", false).toBool())
        t.dueTime = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();  // "Default due time".
    m_store->putTask(t);
    if (date.isValid())
        AlarmTimes::applyDefaultReminder(*m_store, t);
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
    if (due && t.dueDate.isValid())
        AlarmTimes::applyDefaultReminder(*m_store, t);
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

namespace {

QVariantMap recurrenceToMap(const Recurrence &r) {
    return {{"frequency", r.frequency}, {"interval", r.interval}, {"from", r.from}, {"weekdays", r.weekdays},
            {"monthOrdinal", r.monthOrdinal}, {"monthWeekday", r.monthWeekday}, {"endType", r.endType},
            {"endCount", r.endCount}, {"endDate", r.endDate.isValid() ? r.endDate.toString(Qt::ISODate) : QString()}};
}

Recurrence recurrenceFromMap(const QVariantMap &m) {
    Recurrence r;
    r.frequency = m.value("frequency").toInt();
    r.interval = qBound(1, m.value("interval", 1).toInt(), 999);
    r.from = m.value("from").toInt();
    r.weekdays = m.value("weekdays").toInt();
    r.monthOrdinal = m.value("monthOrdinal").toInt();
    r.monthWeekday = m.value("monthWeekday").toInt();
    r.endType = m.value("endType").toInt();
    r.endCount = qBound(0, m.value("endCount").toInt(), 99);
    r.endDate = QDate::fromString(m.value("endDate").toString(), Qt::ISODate);
    return r;
}

}  // namespace

void Backend::setRecurrence(qint64 id, const QVariantMap &rule) {
    const Recurrence r = recurrenceFromMap(rule);
    editTasks("Change Recurrence", {id}, [&](Task &t) {
        Recurrence next = r;
        next.occurrencesDone = t.recurrence.occurrencesDone;
        if (next.isSet() && !t.dueDate.isValid() && !t.startDate.isValid())
            t.dueDate = Recurrences::next(next, QDate::currentDate().addDays(-1));  // A repeat needs a date.
        t.recurrence = next;
    });
}

QString Backend::describeRecurrence(const QVariantMap &rule) const { return Recurrences::describe(recurrenceFromMap(rule)); }

void Backend::toggleAlarm(qint64 id, int minutesBefore) {
    // the reference app's alarm presets: Message with Sound, relative to the due moment.
    // Choosing a preset that's already set removes it.
    Task t = m_store->task(id);
    if (!t.id)
        return;
    m_store->begin("Change Alerts");
    bool removed = false;
    for (auto a : m_store->alarmsFor(id)) {
        if (a.kind == Alarm::Relative && a.offset == minutesBefore) {
            a.deleted = true;
            m_store->putAlarm(a);
            removed = true;
        }
    }
    if (!removed) {
        if (m_store->alarmsFor(id).size() >= 10) {
            m_store->commit();
            setStatus("A task can have at most 10 alerts.");
            return;
        }
        Alarm a;
        a.id = m_store->nextId("alarms");
        a.taskId = id;
        a.offset = minutesBefore;
        m_store->putAlarm(a);
        if (!t.dueDate.isValid()) {
            t.dueDate = QDate::currentDate();  // An alert needs a deadline to count from.
            m_store->putTask(t);
        }
    }
    m_store->commit();
}

void Backend::clearAlarms(qint64 id) {
    m_store->begin("Remove Alerts");
    for (auto a : m_store->alarmsFor(id)) {
        a.deleted = true;
        m_store->putAlarm(a);
    }
    m_store->commit();
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
    else if (flag == "muteAlerts") l.muteAlerts = on;
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
            {"showInToday", l.showInToday}, {"showInDone", l.showInDone}, {"archived", l.archived},
            {"muteAlerts", l.muteAlerts}};
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
        {"pictureUrl", [&] {
             for (const auto &a : m_store->attachmentsFor(t.id))
                 if (a.kind == Attachment::Image)
                     return QUrl::fromLocalFile(m_store->attachmentsFolder() + "/" + a.fileName).toString();
             return QString();
         }()},
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



// ---------------------------------------------------------------- sections

qint64 Backend::newSection(qint64 listId, const QString &name) {
    TaskList list = m_store->list(listId);
    if (!list.id || list.kind == ListKind::Smart)
        return 0;
    ListSection sec;
    sec.id = m_store->nextId("sections");
    sec.listId = listId;
    sec.name = name.trimmed().isEmpty() ? QStringLiteral("New Section") : name.trimmed().left(200);
    double order = 0;
    for (const auto &other : m_store->sectionsOf(listId))
        order = qMax(order, other.order);
    sec.order = order + 1;
    m_store->begin("New Section");
    m_store->putSection(sec);
    // The first section switches the list to sorting by section, where the
    // headings show.
    if (list.sortField != SortField::Section && m_store->sectionsOf(listId).size() == 1) {
        list.sortField = SortField::Section;
        list.separated = true;
        m_store->putList(list);
    }
    m_store->commit();
    emit viewChanged();
    return sec.id;
}

void Backend::renameSection(qint64 sectionId, const QString &name) {
    ListSection sec = m_store->sections().value(sectionId);
    if (!sec.id || name.trimmed().isEmpty() || sec.name == name.trimmed())
        return;
    sec.name = name.trimmed().left(200);
    m_store->begin("Rename Section");
    m_store->putSection(sec);
    m_store->commit();
}

void Backend::deleteSection(qint64 sectionId) {
    // The section goes; its tasks stay in the list, outside any section.
    ListSection sec = m_store->sections().value(sectionId);
    if (!sec.id)
        return;
    m_store->begin("Delete Section");
    for (auto t : m_store->tasks()) {
        if (t.sectionId == sectionId) {
            t.sectionId = 0;
            m_store->putTask(t);
        }
    }
    sec.deleted = true;
    m_store->putSection(sec);
    m_store->commit();
}

void Backend::moveSection(qint64 sectionId, int delta) {
    const ListSection moving = m_store->sections().value(sectionId);
    if (!moving.id)
        return;
    QVector<ListSection> all = m_store->sectionsOf(moving.listId);
    int i = -1;
    for (int j = 0; j < all.size(); ++j)
        if (all.at(j).id == sectionId) i = j;
    const int target = i + delta;
    if (i < 0 || target < 0 || target >= all.size())
        return;
    all.move(i, target);
    m_store->begin("Move Section");
    for (int j = 0; j < all.size(); ++j) {
        ListSection sec = all.at(j);
        sec.order = j + 1;
        m_store->putSection(sec);
    }
    m_store->commit();
}

void Backend::moveSelectionToSection(qint64 sectionId) {
    const ListSection sec = m_store->sections().value(sectionId);
    const auto ids = targetIds();
    if (ids.isEmpty())
        return;
    m_store->begin(sectionId ? "Move to Section" : "Remove from Section");
    for (auto id : ids) {
        Task t = m_store->task(id);
        if (sectionId && t.listId != sec.listId) {
            // Moving into another list's section moves the task there too.
            t.listId = sec.listId;
            t.parentId = 0;
        }
        t.sectionId = sectionId;
        t.order = nextOrder(t.listId, t.parentId);
        m_store->putTask(t);
    }
    m_store->commit();
}

void Backend::dropOnSection(const QString &sectionKey) {
    if (sectionKey.startsWith("sec:"))
        moveSelectionToSection(sectionKey.mid(4).toLongLong());
}

QVariantList Backend::sectionChoices(qint64 listId) const {
    QVariantList result;
    for (const auto &sec : m_store->sectionsOf(listId))
        result.append(QVariantMap{{"id", sec.id}, {"name", sec.name}});
    return result;
}

// ------------------------------------------------- sidebar & tags panel

void Backend::moveList(qint64 listId, qint64 beforeListId, qint64 groupId) {
    // Dropped before another list (or at the end of a group when 0): the
    // whole group is renumbered so manual order stays dense.
    TaskList moving = m_store->list(listId);
    if (!moving.id || moving.kind == ListKind::Inbox)
        return;
    if (beforeListId)
        groupId = m_store->list(beforeListId).groupId;
    QVector<TaskList> members;
    for (const auto &l : m_store->lists())
        if (!l.deleted && !l.archived && l.kind == ListKind::Normal && l.groupId == groupId && l.id != listId)
            members.append(l);
    std::sort(members.begin(), members.end(), [](const TaskList &a, const TaskList &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    int at = members.size();
    for (int i = 0; i < members.size(); ++i)
        if (members.at(i).id == beforeListId) at = i;
    moving.groupId = groupId;
    members.insert(at, moving);
    m_store->begin("Move List");
    for (int i = 0; i < members.size(); ++i) {
        TaskList l = members.at(i);
        if (l.order != i + 1 || l.id == listId) {
            l.order = i + 1;
            m_store->putList(l);
        }
    }
    m_store->commit();
}

void Backend::moveGroup(qint64 groupId, qint64 beforeGroupId) {
    QVector<ListGroup> groups;
    for (const auto &g : m_store->groups())
        if (!g.deleted && g.id != groupId)
            groups.append(g);
    std::sort(groups.begin(), groups.end(), [](const ListGroup &a, const ListGroup &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    int at = groups.size();
    for (int i = 0; i < groups.size(); ++i)
        if (groups.at(i).id == beforeGroupId) at = i;
    groups.insert(at, m_store->groups().value(groupId));
    m_store->begin("Move Group");
    for (int i = 0; i < groups.size(); ++i) {
        ListGroup g = groups.at(i);
        g.order = i + 1;
        m_store->putGroup(g);
    }
    m_store->commit();
}

void Backend::deleteGroup(qint64 groupId) {
    // Deleting a group keeps its lists; they move out to the top level.
    ListGroup g = m_store->groups().value(groupId);
    if (!g.id)
        return;
    m_store->begin("Delete Group");
    for (auto l : m_store->lists()) {
        if (l.groupId == groupId) {
            l.groupId = 0;
            m_store->putList(l);
        }
    }
    g.deleted = true;
    m_store->putGroup(g);
    m_store->commit();
}

QVariantList Backend::archivedLists() const {
    QVariantList result;
    for (const auto &l : m_store->lists())
        if (!l.deleted && l.archived)
            result.append(QVariantMap{{"id", l.id}, {"name", l.name}, {"color", l.color}});
    return result;
}

void Backend::renameTag(qint64 tagId, const QString &name) {
    Tag t = m_store->tag(tagId);
    const QString clean = name.trimmed().remove(QRegularExpression("^#"));
    if (!t.id || clean.isEmpty() || clean == t.name)
        return;
    t.name = clean;
    m_store->begin("Rename Tag");
    m_store->putTag(t);
    m_store->commit();
}

void Backend::deleteTag(qint64 tagId) {
    Tag tag = m_store->tag(tagId);
    if (!tag.id)
        return;
    m_store->begin("Delete Tag");
    for (auto t : m_store->tasks()) {
        if (t.tagIds.removeAll(tagId))
            m_store->putTask(t);
    }
    tag.deleted = true;
    m_store->putTag(tag);
    m_store->commit();
    m_options.tagFilter.remove(tagId);
}

void Backend::setTagGroup(qint64 tagId, qint64 groupId) {
    Tag t = m_store->tag(tagId);
    if (!t.id || t.groupId == groupId)
        return;
    t.groupId = groupId;
    m_store->begin("Move Tag");
    m_store->putTag(t);
    m_store->commit();
}

qint64 Backend::newTagGroup(const QString &name) {
    TagGroup g;
    g.id = m_store->nextId("tag_groups");
    g.name = name.trimmed().isEmpty() ? QStringLiteral("New Group") : name.trimmed();
    g.order = g.id;
    m_store->begin("New Tag Group");
    m_store->putTagGroup(g);
    m_store->commit();
    return g.id;
}

void Backend::renameTagGroup(qint64 groupId, const QString &name) {
    TagGroup g = m_store->tagGroups().value(groupId);
    if (!g.id || name.trimmed().isEmpty())
        return;
    g.name = name.trimmed();
    m_store->begin("Rename Tag Group");
    m_store->putTagGroup(g);
    m_store->commit();
}

void Backend::deleteTagGroup(qint64 groupId) {
    TagGroup g = m_store->tagGroups().value(groupId);
    if (!g.id)
        return;
    m_store->begin("Delete Tag Group");
    for (auto t : m_store->tags()) {
        if (t.groupId == groupId) {
            t.groupId = 0;
            m_store->putTag(t);
        }
    }
    g.deleted = true;
    m_store->putTagGroup(g);
    m_store->commit();
}

QVariantList Backend::tagGroupChoices() const {
    QVariantList result{QVariantMap{{"id", 0}, {"name", "No Group"}}};
    for (const auto &g : m_store->tagGroups())
        if (!g.deleted)
            result.append(QVariantMap{{"id", g.id}, {"name", g.name}});
    return result;
}

void Backend::tagTask(qint64 taskId, qint64 tagId) {
    editTasks("Tag", {taskId}, [&](Task &t) {
        if (!t.tagIds.contains(tagId))
            t.tagIds.append(tagId);
    });
}

void Backend::dropOnTag(qint64 tagId) {
    editTasks("Tag", targetIds(), [&](Task &t) {
        if (!t.tagIds.contains(tagId))
            t.tagIds.append(tagId);
    });
}

void Backend::dropOnTagGroup(qint64 groupId) {
    // Dropping tasks on a tag group applies every tag in it.
    QList<qint64> members;
    for (const auto &tag : m_store->tags())
        if (!tag.deleted && tag.groupId == groupId)
            members.append(tag.id);
    editTasks("Tag", targetIds(), [&](Task &t) {
        for (auto id : members)
            if (!t.tagIds.contains(id))
                t.tagIds.append(id);
    });
}

// ------------------------------------------------------------- locations

qint64 Backend::newLocation(const QString &name, const QString &address) {
    if (name.trimmed().isEmpty())
        return 0;
    Location l;
    l.id = m_store->nextId("locations");
    l.name = name.trimmed();
    l.address = address.trimmed();
    m_store->begin("New Location");
    m_store->putLocation(l);
    m_store->commit();
    return l.id;
}

void Backend::updateLocation(qint64 id, const QString &name, const QString &address) {
    Location l = m_store->locations().value(id);
    if (!l.id || name.trimmed().isEmpty())
        return;
    l.name = name.trimmed();
    l.address = address.trimmed();
    m_store->begin("Edit Location");
    m_store->putLocation(l);
    m_store->commit();
}

void Backend::deleteLocation(qint64 id) {
    Location l = m_store->locations().value(id);
    if (!l.id)
        return;
    m_store->begin("Delete Location");
    for (auto t : m_store->tasks())
        if (t.locationIds.removeAll(id))
            m_store->putTask(t);
    l.deleted = true;
    m_store->putLocation(l);
    m_store->commit();
    m_options.locationFilter.remove(id);
}

void Backend::setLocationPaused(qint64 id, bool paused) {
    Location l = m_store->locations().value(id);
    if (!l.id)
        return;
    l.paused = paused;
    m_store->begin(paused ? "Pause Location" : "Resume Location");
    m_store->putLocation(l);
    m_store->commit();
}

void Backend::toggleTaskLocation(qint64 taskId, qint64 locationId) {
    editTasks("Change Location", {taskId}, [&](Task &t) {
        if (!t.locationIds.removeAll(locationId))
            t.locationIds.append(locationId);
    });
}

void Backend::dropOnLocation(qint64 locationId) {
    editTasks("Change Location", targetIds(), [&](Task &t) {
        if (!t.locationIds.contains(locationId))
            t.locationIds.append(locationId);
    });
}

void Backend::toggleLocationFilter(qint64 locationId) {
    if (!m_options.locationFilter.remove(locationId))
        m_options.locationFilter.insert(locationId);
    rebuild();
    emit viewChanged();
}

QVariantList Backend::locationChoices() const {
    QVariantList result;
    for (const auto &l : m_store->locations())
        if (!l.deleted)
            result.append(QVariantMap{{"id", l.id}, {"name", l.name}, {"address", l.address}});
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0;
    });
    return result;
}

void Backend::rebuildPlaces(const ViewBuilder &views) {
    QHash<qint64, int> counts;
    for (const auto &t : m_store->tasks()) {
        if (t.deleted || t.completed || !views.listVisible(t.listId))
            continue;
        for (auto id : t.locationIds)
            counts[id]++;
    }
    QVector<QVariantMap> rows;
    for (const auto &choice : locationChoices()) {
        const auto m = choice.toMap();
        const qint64 id = m.value("id").toLongLong();
        const Location l = m_store->locations().value(id);
        rows.append({{"key", QStringLiteral("l:%1").arg(id)}, {"kind", "location"}, {"locationId", id},
                     {"title", l.name}, {"address", l.address}, {"count", counts.value(id)}, {"paused", l.paused},
                     {"filtered", m_options.locationFilter.contains(id)}});
    }
    m_places.setRows(rows);
}

// ---------------------------------------------- sub-tasks, history, files

qint64 Backend::addSubtask(qint64 parentId, const QString &title, qint64 afterId) {
    const Task parent = m_store->task(parentId);
    if (!parent.id || title.trimmed().isEmpty())
        return 0;
    Task t;
    t.id = m_store->nextId("tasks");
    t.listId = parent.listId;
    t.parentId = parent.id;
    t.title = title.trimmed().left(1000);
    t.order = nextOrder(parent.listId, parent.id);
    if (afterId) {
        // Return on a sub-task's title puts the new one right after it.
        const Task after = m_store->task(afterId);
        double next = after.order + 1;
        for (const auto &c : m_store->tasks())
            if (c.parentId == parent.id && !c.deleted && c.order > after.order)
                next = qMin(next, c.order);
        t.order = (after.order + next) / 2;
    }
    m_store->begin("New Sub-Task");
    m_store->putTask(t);
    m_store->commit();
    return t.id;
}

void Backend::setAllCollapsed(bool collapsed) {
    // ⌥⌘↑/↓: fold or unfold every project and checklist in view.
    QVector<qint64> parents;
    for (const auto &row : m_tasks.rows())
        if (row.value("hasChildren").toBool() && row.value("collapsed").toBool() != collapsed)
            parents.append(row.value("taskId").toLongLong());
    editTasks(collapsed ? "Collapse All" : "Expand All", parents, [&](Task &t) { t.collapsed = collapsed; });
}

void Backend::showLastSelected() {
    // Walks backward through recently selected tasks, switching lists as
    // needed, and wraps around at the oldest.
    if (m_history.size() < 2)
        return;
    m_historyStep = (m_historyStep + 1) % m_history.size();
    const qint64 id = m_history.at(m_history.size() - 1 - m_historyStep);
    const QVector<qint64> saved = m_history;
    const int step = m_historyStep;
    if (m_visibleOrder.contains(id)) {
        selectTask(id);
        emit revealTask(id);
    } else {
        revealInList(id);
    }
    m_history = saved;  // Walking the history doesn't rewrite it.
    m_historyStep = step;
}

void Backend::copySelection() {
    // Plain text a person can paste anywhere: titles, with notes and
    // sub-tasks indented under them.
    QStringList lines;
    std::function<void(const Task &, int)> add = [&](const Task &t, int depth) {
        lines << QString(depth * 2, ' ') + (t.completed ? "✓ " : "") + t.title;
        for (const auto &line : t.notes.split('\n', Qt::SkipEmptyParts))
            lines << QString(depth * 2 + 4, ' ') + line;
        ViewBuilder views(*m_store, QDateTime::currentDateTime(), scheduleSettings());
        for (const auto &c : views.children(t.id))
            add(c, depth + 1);
    };
    for (auto id : targetIds())
        add(m_store->task(id), 0);
    if (lines.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(lines.join('\n'));
    setStatus(targetIds().size() == 1 ? "Copied" : QStringLiteral("Copied %1 tasks").arg(targetIds().size()));
}

void Backend::pasteTasks() {
    // Each non-empty line of pasted text becomes a task in the current list,
    // read with natural-language input like a typed title.
    const QString text = QGuiApplication::clipboard()->text();
    QStringList lines;
    for (const auto &line : text.split('\n'))
        if (!line.trimmed().isEmpty())
            lines << line.trimmed().remove(QRegularExpression("^([-*•✓]|\\d+[.)])\\s+"));
    if (lines.isEmpty())
        return;
    qint64 listId = m_source.kind == Source::List ? m_source.id
                  : m_source.kind == Source::Project ? m_store->task(m_source.id).listId : defaultListId();
    if (!listId || m_store->list(listId).kind == ListKind::Smart)
        listId = defaultListId();
    const qint64 parentId = m_source.kind == Source::Project ? m_source.id : 0;
    QVector<qint64> created;
    m_store->begin(lines.size() > 1 ? QStringLiteral("Paste %1 Tasks").arg(lines.size()) : QStringLiteral("Paste Task"));
    for (const auto &line : lines.mid(0, 500)) {
        Task t;
        t.id = m_store->nextId("tasks");
        t.listId = listId;
        t.parentId = parentId;
        t.title = line.left(1000);
        t.order = nextOrder(listId, parentId);
        if (m_source.kind == Source::Today) t.dueDate = QDate::currentDate();
        if (m_source.kind == Source::Starred) t.starred = true;
        if (m_store->setting("nl.editor", true).toBool())
            applyParsed(t, NaturalLanguage::parse(line, QDateTime::currentDateTime(), listNames(), locationNames()));
        m_store->putTask(t);
        AlarmTimes::applyDefaultReminder(*m_store, t);
        created.append(t.id);
    }
    m_store->commit();
    for (auto id : created)
        m_options.keepVisible.insert(id);
    m_selected = created;
    m_cursor = m_anchor = created.last();
    rebuild();
    emit selectionChanged();
}

void Backend::dropFiles(qint64 taskId, const QVariantList &urls) {
    // A picture becomes the task's picture (one per task, the first wins);
    // other files are linked in the notes. Dropped on empty space, the files
    // make a new task named after the first.
    // Accepts file URLs (drag and drop) or plain paths (the file picker),
    // which keeps Windows drive paths intact.
    QStringList paths;
    for (const auto &u : urls) {
        const QString text = u.typeId() == QMetaType::QUrl ? u.toUrl().toString() : u.toString();
        if (text.startsWith("file:")) {
            const QUrl url(text);
            if (url.isLocalFile())
                paths << url.toLocalFile();
        } else if (!text.isEmpty() && QFileInfo::exists(text)) {
            paths << text;
        }
    }
    if (paths.isEmpty())
        return;
    m_store->begin("Add Files");
    Task t = m_store->task(taskId);
    if (!t.id) {
        t.id = m_store->nextId("tasks");
        t.listId = m_source.kind == Source::List && m_store->list(m_source.id).kind != ListKind::Smart ? m_source.id : defaultListId();
        t.parentId = m_source.kind == Source::Project ? m_source.id : 0;
        if (m_source.kind == Source::Project)
            t.listId = m_store->task(m_source.id).listId;
        t.title = QFileInfo(paths.first()).completeBaseName();
        t.order = nextOrder(t.listId, t.parentId);
    }
    bool hasPicture = false;
    for (const auto &a : m_store->attachmentsFor(t.id))
        hasPicture = hasPicture || a.kind == Attachment::Image;
    const QMimeDatabase mimes;
    for (const auto &path : paths) {
        const QFileInfo info(path);
        const bool image = mimes.mimeTypeForFile(info).name().startsWith("image/");
        const bool audio = mimes.mimeTypeForFile(info).name().startsWith("audio/");
        if ((image && !hasPicture) || audio) {
            Attachment a;
            a.id = m_store->nextId("attachments");
            a.uid = Store::newUid();
            a.taskId = t.id;
            a.kind = image ? Attachment::Image : Attachment::Audio;
            a.name = info.fileName();
            a.fileName = a.uid + (info.suffix().isEmpty() ? QString() : "." + info.suffix().toLower());
            a.bytes = info.size();
            QDir().mkpath(m_store->attachmentsFolder());
            if (!QFile::copy(path, m_store->attachmentsFolder() + "/" + a.fileName)) {
                setStatus("Couldn't copy " + info.fileName());
                continue;
            }
            m_store->putAttachment(a);
            hasPicture = hasPicture || image;
        } else {
            const QString link = QUrl::fromLocalFile(info.absoluteFilePath()).toString();
            t.notes += (t.notes.isEmpty() || t.notes.endsWith('\n') ? "" : "\n") + QStringLiteral("[%1](%2)").arg(info.fileName(), link);
        }
    }
    m_store->putTask(t);
    m_store->commit();
    m_options.keepVisible.insert(t.id);
    selectTask(t.id);
}

void Backend::removeAttachment(qint64 attachmentId) {
    Attachment a = m_store->attachments().value(attachmentId);
    if (!a.id)
        return;
    // The file stays on disk so undo can bring the attachment back; backups
    // and the next cleanup take care of orphans.
    a.deleted = true;
    m_store->begin("Remove Attachment");
    m_store->putAttachment(a);
    m_store->commit();
}

void Backend::openAttachment(qint64 attachmentId) {
    const Attachment a = m_store->attachments().value(attachmentId);
    if (a.id)
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_store->attachmentsFolder() + "/" + a.fileName));
}

void Backend::toggleRecording(qint64 taskId) {
    if (m_recorder) {
        // Stopping: pw-record finishes the file on SIGINT-like termination.
        m_recorder->terminate();
        if (!m_recorder->waitForFinished(3000))
            m_recorder->kill();
        m_recorder->deleteLater();
        m_recorder = nullptr;
        const qint64 task = m_recordingTask;
        m_recordingTask = 0;
        emit recordingChanged();
        const QFileInfo info(m_recordingFile);
        if (!info.exists() || info.size() < 1024) {
            setStatus("Nothing was recorded.");
            QFile::remove(m_recordingFile);
            return;
        }
        Attachment a;
        a.id = m_store->nextId("attachments");
        a.uid = info.completeBaseName();
        a.taskId = task;
        a.kind = Attachment::Audio;
        a.name = "Audio note " + QDateTime::currentDateTime().toString("d MMM, h:mm AP") + ".flac";
        a.fileName = info.fileName();
        a.bytes = info.size();
        m_store->begin("Record Audio Note");
        m_store->putAttachment(a);
        m_store->commit();
        return;
    }
    const QString recorder = QStandardPaths::findExecutable("pw-record");
    if (recorder.isEmpty()) {
        setStatus("Recording audio notes needs PipeWire's pw-record, available on Linux.");
        return;
    }
    QDir().mkpath(m_store->attachmentsFolder());
    m_recordingFile = m_store->attachmentsFolder() + "/" + Store::newUid() + ".flac";
    m_recorder = new QProcess(this);
    m_recorder->start(recorder, {m_recordingFile});
    if (!m_recorder->waitForStarted(2000)) {
        setStatus("Couldn't start recording: " + m_recorder->errorString());
        m_recorder->deleteLater();
        m_recorder = nullptr;
        return;
    }
    m_recordingTask = taskId;
    emit recordingChanged();
}

void Backend::timeBlock(const QString &isoDate, int startMinute, int durationMinutes) {
    // the reference app's time blocking: dropped on the day bar, the task is due when the
    // block ends and takes the block's length, so its effective start is
    // where it was dropped.
    const QDate day = isoDate.isEmpty() ? QDate::currentDate() : QDate::fromString(isoDate, Qt::ISODate);
    if (!day.isValid())
        return;
    const int end = qMin(24 * 60 - 1, startMinute + qMax(5, durationMinutes));
    for (auto i : targetIds())
        m_options.keepVisible.insert(i);
    editTasks("Time Block", targetIds(), [&](Task &t) {
        t.dueDate = day;
        t.dueTime = end;
        t.duration = end - startMinute;
    });
}

void Backend::addAbsoluteAlarm(qint64 taskId, const QString &isoDate, int minutes) {
    const QDate day = QDate::fromString(isoDate, Qt::ISODate);
    if (!m_store->task(taskId).id || !day.isValid() || minutes < 0)
        return;
    Alarm a;
    a.id = m_store->nextId("alarms");
    a.taskId = taskId;
    a.kind = Alarm::Absolute;
    a.at = QDateTime(day, QTime(0, 0).addSecs(minutes * 60));
    m_store->begin("Add Alert");
    m_store->putAlarm(a);
    m_store->commit();
}

void Backend::addDaysBeforeAlarm(qint64 taskId, int days, int minutes) {
    // "N days before at HH:MM", counted from the due date (or the start date).
    Task t = m_store->task(taskId);
    if (!t.id || days < 0 || days > 365 || minutes < 0 || minutes >= 24 * 60)
        return;
    if (m_store->alarmsFor(taskId).size() >= 10) {
        setStatus("A task can have at most 10 alerts.");
        return;
    }
    Alarm a;
    a.id = m_store->nextId("alarms");
    a.taskId = taskId;
    a.kind = Alarm::DaysBefore;
    a.offset = days;
    a.at = AlarmTimes::clockTime(minutes);
    m_store->begin("Add Alert");
    m_store->putAlarm(a);
    if (!t.dueDate.isValid() && !t.startDate.isValid()) {
        t.dueDate = QDate::currentDate();  // An alert needs a date to count from.
        m_store->putTask(t);
    }
    m_store->commit();
}

void Backend::setAlarmSound(qint64 alarmId, bool sound) {
    // the reference app's per-alert style: "Message with Sound" or just "Message".
    Alarm a = m_store->alarms().value(alarmId);
    if (!a.id || a.sound == sound)
        return;
    a.sound = sound;
    m_store->begin("Change Alert");
    m_store->putAlarm(a);
    m_store->commit();
}

void Backend::removeAlarm(qint64 alarmId) {
    Alarm a = m_store->alarms().value(alarmId);
    if (!a.id)
        return;
    a.deleted = true;
    m_store->begin("Remove Alert");
    m_store->putAlarm(a);
    m_store->commit();
}

QString Backend::licenseText() const {
    QFile file(":/LICENSE");
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString Backend::shareableLink(qint64 taskId) const { return Automation::shareableLink(m_store->task(taskId)); }

void Backend::toggleSearchScope() {
    m_searchAll = !m_searchAll;
    rebuild();
    emit viewChanged();
    setStatus(m_searchAll ? "Searching all lists" : "Searching this list");
}

// ----------------------------------------------------- focus & smart lists

bool Backend::focusOn() const { return m_store->setting(focusSettingKey() + ".on", false).toBool(); }

QStringList Backend::focusConditions() const {
    return m_store->setting(focusSettingKey() + ".conditions").toString().split(',', Qt::SkipEmptyParts);
}

void Backend::toggleFocus() {
    // With nothing ticked yet, turning the filter on starts from Today, as a
    // single click on the reference app's sun button does.
    if (!focusOn() && focusConditions().isEmpty())
        m_store->setSetting(focusSettingKey() + ".conditions", "overdue,today");
    m_store->setSetting(focusSettingKey() + ".on", !focusOn());
    rebuild();
    emit viewChanged();
}

void Backend::setFocusCondition(const QString &condition, bool on) {
    QStringList conditions = focusConditions();
    conditions.removeAll(condition);
    if (on)
        conditions.append(condition);
    m_store->setSetting(focusSettingKey() + ".conditions", conditions.join(','));
    m_store->setSetting(focusSettingKey() + ".on", !conditions.isEmpty());
    rebuild();
    emit viewChanged();
}

bool Backend::sourceIsSmart() const {
    return m_source.kind == Source::List && m_store->list(m_source.id).kind == ListKind::Smart;
}

QString Backend::smartQuery() const { return sourceIsSmart() ? m_store->list(m_source.id).smartQuery : QString(); }

QString Backend::smartRangeText() const {
    if (!sourceIsSmart())
        return {};
    const QStringList parts = m_store->list(m_source.id).smartRange.split(':');
    if (parts.value(0) == "nodue")
        return QStringLiteral("Without due date");
    if (parts.value(0) == "next") {
        const int n = parts.value(1).toInt();
        static const QHash<QString, QString> units{{"d", "day"}, {"w", "week"}, {"m", "month"}, {"y", "year"}};
        return QStringLiteral("In the next %1 %2%3").arg(n).arg(units.value(parts.value(2), "day")).arg(n == 1 ? "" : "s");
    }
    if (parts.value(0) == "fixed")
        return QStringLiteral("Due %1 – %2").arg(parts.value(1), parts.value(2));
    return {};
}

void Backend::setSoundex(bool on) {
    m_options.soundex = on;
    rebuild();
    emit viewChanged();
}

qint64 Backend::saveSmartList(const QString &name, const QString &query) {
    if (query.trimmed().isEmpty())
        return 0;
    TaskList l;
    l.id = m_store->nextId("lists");
    l.kind = ListKind::Smart;
    l.name = name.trimmed().isEmpty() ? query.trimmed() : name.trimmed();
    l.smartQuery = query.trimmed();
    l.order = l.id;
    l.sortField = SortField::DueDate;
    m_store->begin("New Smart List");
    m_store->putList(l);
    m_store->commit();
    m_options.search.clear();
    selectSource(Source{Source::List, l.id}.key());
    return l.id;
}

void Backend::setSmartQuery(qint64 listId, const QString &query) {
    TaskList l = m_store->list(listId);
    if (!l.id || l.kind != ListKind::Smart || l.smartQuery == query)
        return;
    l.smartQuery = query;
    m_store->begin("Edit Smart List");
    m_store->putList(l);
    m_store->commit();
    emit viewChanged();
}


// ------------------------------------------------------- backups & files

QString Backend::backupFolder() const {
    return QFileInfo(m_store->path()).absolutePath() + "/backups";
}

void Backend::completeAfterAutoDelete() {
    // "Auto-delete completed tasks": completed tasks older than N days are
    // removed for good (soft-deleted, so sync carries the deletion).
    const int days = m_store->setting("tasks.autoDeleteDays", 0).toInt();
    if (days <= 0)
        return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
    const QDateTime childCutoff = m_store->setting("tasks.autoDeleteDelaySubtasks", false).toBool() ? cutoff.addDays(-21) : cutoff;
    QVector<Task> old;
    for (const auto &t : m_store->tasks())
        if (!t.deleted && t.completed && t.completedAt.isValid() && t.completedAt < (t.parentId ? childCutoff : cutoff))
            old.append(t);
    if (old.isEmpty())
        return;
    m_store->begin("Auto-Delete Completed Tasks", false);
    for (auto t : old) {
        t.deleted = true;
        m_store->putTask(t);
    }
    m_store->commit();
}

void Backend::maybeBackup() {
    completeAfterAutoDelete();
    // "Back up every 24 / 48 hours", 0 meaning off.
    const int hours = m_store->setting("backup.intervalHours", 24).toInt();
    if (hours <= 0 || m_store->path().isEmpty())
        return;
    const QDateTime last = QDateTime::fromSecsSinceEpoch(m_store->setting("backup.last", 0).toLongLong());
    if (last.secsTo(QDateTime::currentDateTime()) >= qint64(hours) * 3600)
        backupNow();
}

bool Backend::backupNow(const QString &reason) {
    const QString name = QStringLiteral("Precrime_(%1)%2.db")
                             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HH_mm_ss"),
                                  reason.isEmpty() ? QString() : "_" + reason);
    QString error;
    if (!m_store->backupTo(backupFolder() + "/" + name, &error)) {
        setStatus("Backup failed: " + error);
        return false;
    }
    m_store->setSetting("backup.last", QDateTime::currentSecsSinceEpoch());
    // Keep at most N automatic backups; safety backups taken before a
    // restore or import are pruned the same way.
    const int keep = qMax(1, m_store->setting("backup.keep", 10).toInt());
    QDir dir(backupFolder());
    const auto files = dir.entryInfoList({"Precrime_*.db"}, QDir::Files, QDir::Time);
    for (int i = keep; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
    if (reason.isEmpty())
        setStatus("Backed up to " + name);
    return true;
}

QVariantList Backend::backups() const {
    QVariantList result;
    QDir dir(backupFolder());
    for (const auto &f : dir.entryInfoList({"Precrime_*.db"}, QDir::Files, QDir::Time))
        result.append(QVariantMap{{"path", f.absoluteFilePath()}, {"name", f.fileName()},
                                  {"date", English.toString(f.lastModified(), "ddd d MMM yyyy, h:mm AP")},
                                  {"size", QString::number(f.size() / 1024) + " KB"}});
    return result;
}

bool Backend::restoreBackup(const QString &path) {
    endEdit();
    if (!backupNow("before-restore"))
        return false;
    QString error;
    if (!m_store->replaceWith(path, &error)) {
        setStatus(error);
        return false;
    }
    m_selected.clear();
    m_cursor = 0;
    m_source = Source{};
    rebuild();
    emit viewChanged();
    emit selectionChanged();
    setStatus("Restored " + QFileInfo(path).fileName());
    return true;
}

void Backend::exportTo(const QString &path, const QString &format, bool selectionOnly) {
    QList<qint64> ids;
    if (selectionOnly)
        for (auto id : m_selected)
            ids.append(id);
    QString error;
    const bool ok = format == "ics" ? Exchange::exportIcs(*m_store, path, ids, &error)
                                    : Exchange::exportCsv(*m_store, path, ids, &error);
    setStatus(ok ? "Exported to " + QFileInfo(path).fileName() : "Export failed: " + error);
}

void Backend::importFrom(const QString &path, const QString &format) {
    backupNow("before-import");
    const ImportResult r = format == "taskapp"        ? Exchange::importTwoDo(*m_store, path)
                         : format == "ticktick" ? Exchange::importTickTick(*m_store, path)
                                                : Exchange::importIcs(*m_store, path, defaultListId());
    if (!r.error.isEmpty()) {
        setStatus("Import failed: " + r.error);
        return;
    }
    setStatus(QStringLiteral("Imported %1 tasks in %2 lists (Ctrl+Z undoes it)").arg(r.tasks).arg(r.lists));
}

bool Backend::inboxEnabled() const {
    for (const auto &l : m_store->lists())
        if (!l.deleted && l.kind == ListKind::Inbox)
            return true;
    return false;
}

void Backend::setInboxEnabled(bool on) {
    if (on == inboxEnabled())
        return;
    if (on) {
        TaskList inbox;
        inbox.id = m_store->nextId("lists");
        inbox.kind = ListKind::Inbox;
        inbox.name = "Inbox";
        inbox.color = 5;
        m_store->begin("Turn On Inbox");
        m_store->putList(inbox);
        m_store->commit();
        return;
    }
    for (const auto &l : m_store->lists())
        if (!l.deleted && l.kind == ListKind::Inbox)
            deleteList(l.id);
}

// ---------------------------------------------------------- quick entry

QStringList Backend::locationNames() const {
    QStringList names;
    for (const auto &l : m_store->locations())
        if (!l.deleted)
            names << l.name;
    return names;
}

QStringList Backend::listNames() const {
    QStringList names;
    for (const auto &l : m_store->lists())
        if (!l.deleted && !l.archived && l.kind != ListKind::Smart)
            names << l.name;
    return names;
}

QVariantMap Backend::setting(const QString &key) const { return {{"value", m_store->setting(key)}}; }

void Backend::setSetting(const QString &key, const QVariant &value) {
    m_store->setSetting(key, value);
    m_options.completedWindowMinutes = m_store->setting("tasks.completedWindow", 24 * 60).toInt();
    rebuild();
    emit settingsChanged();
    emit viewChanged();
}

qint64 Backend::quickEntryListId() const {
    // Quick Entry lands in the Inbox when there is one, else the default list.
    for (const auto &l : m_store->lists())
        if (!l.deleted && l.kind == ListKind::Inbox)
            return l.id;
    const qint64 chosen = m_store->setting("tasks.quickEntryList", 0).toLongLong();
    if (chosen && m_store->lists().contains(chosen) && !m_store->list(chosen).deleted)
        return chosen;
    return defaultListId();
}

QVariantMap Backend::previewEntry(const QString &text) const {
    const ParsedEntry p = NaturalLanguage::parse(text, QDateTime::currentDateTime(), listNames(), locationNames());
    const QDate today = QDate::currentDate();
    QVariantList chips;
    auto chip = [&](const QString &kind, const QString &label) { chips.append(QVariantMap{{"kind", kind}, {"label", label}}); };
    if (p.startDate.isValid())
        chip("start", "Start " + Schedule::friendlyDateTime(p.startDate, p.startTime, today));
    if (p.dueDate.isValid())
        chip("due", "Due " + Schedule::friendlyDateTime(p.dueDate, p.dueTime, today));
    else if (p.dueTime >= 0)
        chip("due", "Due " + Schedule::formatTime(p.dueTime));
    if (p.recurrence.isSet())
        chip("repeat", Recurrences::describe(p.recurrence));
    if (p.duration > 0)
        chip("duration", Schedule::durationBadge(p.duration));
    if (p.priority > 0)
        chip("priority", QStringList{"", "Low", "Medium", "High"}.at(p.priority) + " priority");
    if (p.starred)
        chip("star", "Starred");
    for (const auto &tag : p.tags)
        chip("tag", "#" + tag);
    if (!p.list.isEmpty())
        chip("list", p.list);
    if (!p.location.isEmpty())
        chip("location", "@ " + p.location);
    for (int offset : p.alertOffsets)
        chip("alert", "Alert " + Schedule::durationBadge(offset) + " before");
    QVariantList spans;
    for (const auto &span : p.spans)
        spans.append(QVariantMap{{"start", span.start}, {"length", span.length}, {"kind", span.kind}});
    qint64 listId = 0;
    for (const auto &l : m_store->lists())
        if (!p.list.isEmpty() && !l.deleted && l.kind != ListKind::Smart && l.name.compare(p.list, Qt::CaseInsensitive) == 0)
            listId = l.id;
    qint64 locationId = 0;
    for (const auto &l : m_store->locations())
        if (!p.location.isEmpty() && !l.deleted && l.name.compare(p.location, Qt::CaseInsensitive) == 0)
            locationId = l.id;
    QVariantList alerts;
    for (int offset : p.alertOffsets)
        alerts.append(offset);
    QDate dueDate = p.dueDate;
    if (!dueDate.isValid() && p.dueTime >= 0)
        dueDate = QDate::currentDate();
    if (p.recurrence.isSet() && !dueDate.isValid() && !p.startDate.isValid())
        dueDate = Recurrences::next(p.recurrence, QDate::currentDate().addDays(-1));
    const QVariantMap fields{
        {"title", p.title},
        {"dueIso", dueDate.isValid() ? dueDate.toString(Qt::ISODate) : QString()},
        {"dueMinutes", p.dueTime},
        {"startIso", p.startDate.isValid() ? p.startDate.toString(Qt::ISODate) : QString()},
        {"startMinutes", p.startTime},
        {"recurrence", recurrenceToMap(p.recurrence)},
        {"alerts", alerts},
        {"duration", p.duration},
        {"tags", p.tags},
        {"listId", listId},
        {"locationId", locationId},
        {"priority", qMax(0, p.priority)},
        {"starred", p.starred},
    };
    return {{"title", p.title}, {"chips", chips}, {"spans", spans}, {"fields", fields}};
}

void Backend::applyParsed(Task &t, const ParsedEntry &p) {
    if (!p.title.isEmpty())
        t.title = p.title.left(1000);
    if (p.startDate.isValid()) {
        t.startDate = p.startDate;
        t.startTime = p.startTime;
    }
    if (p.dueDate.isValid()) {
        t.dueDate = p.dueDate;
        t.dueTime = p.dueTime;
    } else if (p.dueTime >= 0) {
        t.dueDate = QDate::currentDate();
        t.dueTime = p.dueTime;
    }
    if (p.duration > 0)
        t.duration = p.duration;
    if (p.priority > 0)
        t.priority = p.priority;
    if (p.starred)
        t.starred = true;
    if (p.recurrence.isSet()) {
        t.recurrence = p.recurrence;
        // "every Monday" with no date starts at the first occurrence from today.
        if (!t.dueDate.isValid() && !t.startDate.isValid())
            t.dueDate = Recurrences::next(p.recurrence, QDate::currentDate().addDays(-1));
    }
    for (const auto &tag : p.tags) {
        const qint64 tagId = tagIdForName(tag, true);
        if (tagId && !t.tagIds.contains(tagId))
            t.tagIds.append(tagId);
    }
    if (!p.location.isEmpty()) {
        for (const auto &l : m_store->locations())
            if (!l.deleted && l.name.compare(p.location, Qt::CaseInsensitive) == 0 && !t.locationIds.contains(l.id))
                t.locationIds.append(l.id);
    }
    for (int offset : p.alertOffsets) {
        Alarm a;
        a.id = m_store->nextId("alarms");
        a.taskId = t.id;
        a.offset = offset;
        m_store->putAlarm(a);
        if (!t.dueDate.isValid())
            t.dueDate = QDate::currentDate();
    }
    if (!p.list.isEmpty()) {
        for (const auto &l : m_store->lists()) {
            if (!l.deleted && l.kind != ListKind::Smart && l.name.compare(p.list, Qt::CaseInsensitive) == 0) {
                if (t.listId != l.id) {
                    t.listId = l.id;
                    t.parentId = 0;
                    t.order = nextOrder(l.id, 0);
                }
                break;
            }
        }
    }
}

QVariantMap Backend::quickEntryDefaults() const {
    QString due;
    const int offset = m_store->setting("tasks.defaultDueOffset", -1).toInt();
    if (offset >= 0 && !m_store->setting("tasks.defaultDueNotQuickEntry", false).toBool())
        due = (offset == 30 ? QDate::currentDate().addMonths(1) : QDate::currentDate().addDays(offset)).toString(Qt::ISODate);
    return {{"listId", quickEntryListId()}, {"dueIso", due}};
}

qint64 Backend::quickAddFields(const QVariantMap &f) {
    const QString title = f.value("title").toString().trimmed();
    if (title.isEmpty())
        return 0;
    Task t;
    t.id = m_store->nextId("tasks");
    t.type = qBound(0, f.value("type").toInt(), 2);
    const qint64 listId = f.value("listId").toLongLong();
    t.listId = listId && m_store->lists().contains(listId) && m_store->list(listId).kind != ListKind::Smart ? listId
                                                                                                          : quickEntryListId();
    if (!t.listId) {
        setStatus("Create a list first: there is nowhere to put the task.");
        return 0;
    }
    t.title = title.left(1000);
    t.notes = f.value("notes").toString();
    t.order = nextOrder(t.listId, 0);
    t.dueDate = QDate::fromString(f.value("dueIso").toString(), Qt::ISODate);
    t.dueTime = t.dueDate.isValid() ? f.value("dueMinutes", -1).toInt() : -1;
    t.startDate = QDate::fromString(f.value("startIso").toString(), Qt::ISODate);
    t.startTime = t.startDate.isValid() ? f.value("startMinutes", -1).toInt() : -1;
    if (f.contains("recurrence"))
        t.recurrence = recurrenceFromMap(f.value("recurrence").toMap());
    if (t.recurrence.isSet() && !t.dueDate.isValid() && !t.startDate.isValid())
        t.dueDate = Recurrences::next(t.recurrence, QDate::currentDate().addDays(-1));
    t.duration = qMax(0, f.value("duration").toInt());
    t.priority = qBound(0, f.value("priority").toInt(), 3);
    t.starred = f.value("starred").toBool();
    m_store->begin("Quick Entry");
    for (const auto &name : f.value("tags").toStringList())
        if (const qint64 id = tagIdForName(name, true); id && !t.tagIds.contains(id))
            t.tagIds.append(id);
    if (const qint64 place = f.value("locationId").toLongLong(); place && m_store->locations().contains(place))
        t.locationIds.append(place);
    for (const auto &v : f.value("alerts").toList()) {
        Alarm a;
        a.id = m_store->nextId("alarms");
        a.taskId = t.id;
        a.offset = v.toInt();
        m_store->putAlarm(a);
        if (!t.dueDate.isValid())
            t.dueDate = QDate::currentDate();  // An alert counts from a deadline.
    }
    m_store->putTask(t);
    AlarmTimes::applyDefaultReminder(*m_store, t);
    m_store->commit();
    const QVariantList files = f.value("files").toList();
    if (!files.isEmpty())
        dropFiles(t.id, files);
    m_options.keepVisible.insert(t.id);
    setStatus("Added “" + t.title + "” to " + m_store->list(t.listId).name);
    return t.id;
}

qint64 Backend::quickAdd(const QString &text, const QString &notes, int type, qint64 listId) {
    if (text.trimmed().isEmpty())
        return 0;
    Task t;
    t.id = m_store->nextId("tasks");
    t.type = type;
    t.listId = listId && m_store->lists().contains(listId) ? listId : quickEntryListId();
    if (!t.listId) {
        setStatus("Create a list first: there is nowhere to put the task.");
        return 0;
    }
    t.title = text.trimmed().left(1000);
    t.notes = notes;
    t.order = nextOrder(t.listId, 0);
    m_store->begin("Quick Entry");
    if (m_store->setting("nl.quickEntry", true).toBool())
        applyParsed(t, NaturalLanguage::parse(text, QDateTime::currentDateTime(), listNames(), locationNames()));
    const int offset = m_store->setting("tasks.defaultDueOffset", -1).toInt();
    if (!t.dueDate.isValid() && offset >= 0 && !m_store->setting("tasks.defaultDueNotQuickEntry", false).toBool())
        t.dueDate = offset == 30 ? QDate::currentDate().addMonths(1) : QDate::currentDate().addDays(offset);
    m_store->putTask(t);
    AlarmTimes::applyDefaultReminder(*m_store, t);
    m_store->commit();
    m_options.keepVisible.insert(t.id);
    setStatus("Added “" + t.title + "” to " + m_store->list(t.listId).name);
    return t.id;
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
    rebuildPlaces(views);
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

    // Smart Lists follow under their own heading; each counts what it matches.
    QVector<TaskList> smart;
    for (const auto &l : m_store->lists())
        if (!l.deleted && !l.archived && l.kind == ListKind::Smart)
            smart.append(l);
    std::sort(smart.begin(), smart.end(), [](const TaskList &a, const TaskList &b) { return a.order < b.order; });
    if (!smart.isEmpty())
        rows.append({{"key", "h:smart"}, {"kind", "header"}, {"title", "Smart Lists"}, {"groupId", -1}, {"collapsed", false}});
    for (const auto &l : smart) {
        const Source source{Source::List, l.id};
        ViewOptions probe;
        probe.completedWindowMinutes = 0;
        int count = 0;
        for (const auto &section : views.build(source, SortSpec{SortField::Title, true, false}, probe))
            for (const auto &row : section.rows)
                count += row.depth == 0;
        rows.append({{"key", source.key()}, {"kind", "smart"}, {"sourceKey", source.key()}, {"title", l.name},
                     {"icon", "search"}, {"undone", count}, {"selected", m_source.key() == source.key()},
                     {"listId", l.id}});
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
        {"hasAlarm", !m_store->alarmsFor(t.id).isEmpty()},
        {"hasLocation", !t.locationIds.isEmpty()},
        {"hasAttachment", !m_store->attachmentsFor(t.id).isEmpty()},
    };
}

void Backend::rebuildTasks(const ViewBuilder &views) {
    m_options.focusConditions = focusOn() ? focusConditions() : QStringList();
    m_options.groupedDates = m_store->setting("sort.groupedDates", false).toBool();
    m_options.starredAboveHigh = m_store->setting("sort.starredAboveHigh", false).toBool();
    // With the scope set to all lists, a search looks everywhere.
    const Source viewed = (m_searchAll && !m_options.search.isEmpty()) ? Source{Source::All, 0} : m_source;
    const auto sections = views.build(viewed, sortSpec(), m_options);
    QVector<QVariantMap> rows;
    QVector<qint64> order;
    QSet<QString> seen;
    bool first = true;
    for (const auto &section : sections) {
        rows.append({{"key", "s:" + section.key}, {"kind", "section"}, {"sectionKey", section.key},
                     {"title", section.title}, {"tone", section.tone}, {"color", section.color},
                     {"count", section.openCount}, {"durationTotal", Schedule::durationTotal(section.durationTotal)},
                     {"collapsed", section.collapsed}, {"isFirst", first},
                     {"sectionId", section.key.startsWith("sec:") ? section.key.mid(4).toLongLong() : -1}});
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
            {"recurrence", recurrenceToMap(t.recurrence)},
            {"startIso", t.startDate.isValid() ? t.startDate.toString(Qt::ISODate) : QString()},
            {"dueIso", t.dueDate.isValid() ? t.dueDate.toString(Qt::ISODate) : QString()},
            {"actionType", t.actionType},
            {"actionValue", t.actionValue},
            {"hint", Schedule::dueHint(t, now, scheduleSettings())},
            {"created", "Created: " + Schedule::friendlyDate(t.created.date(), today) + " at " +
                            Schedule::formatTime(t.created.time().hour() * 60 + t.created.time().minute())},
            {"isChecklistItem", parent.id && parent.type == TaskType::Checklist},
            {"isNew", t.id == m_newTask},
            {"subtasks", [&] {
                 QVariantList items;
                 ViewBuilder views(*m_store, now, scheduleSettings());
                 for (const auto &c : views.children(t.id))
                     items.append(QVariantMap{{"id", c.id}, {"title", c.title}, {"completed", c.completed}});
                 return items;
             }()},
            {"attachments", [&] {
                 QVariantList items;
                 for (const auto &a : m_store->attachmentsFor(t.id))
                     items.append(QVariantMap{{"id", a.id}, {"kind", a.kind}, {"name", a.name},
                                              {"url", QUrl::fromLocalFile(m_store->attachmentsFolder() + "/" + a.fileName).toString()}});
                 return items;
             }()},
            {"alarms", [&] {
                 QVariantList items;
                 for (const auto &a : m_store->alarmsFor(t.id)) {
                     QString label;
                     if (a.kind == Alarm::Absolute)
                         label = Schedule::friendlyDateTime(a.at.date(), a.at.time().hour() * 60 + a.at.time().minute(), today, false);
                     else if (a.kind == Alarm::DaysBefore)
                         label = (a.offset == 0 ? QStringLiteral("On the day") : a.offset == 1 ? QStringLiteral("Day before")
                                                                               : QStringLiteral("%1 days before").arg(a.offset)) +
                                 " at " + Schedule::formatTime(a.at.time().hour() * 60 + a.at.time().minute());
                     else if (a.offset == 0)
                         label = "At due time";
                     else
                         label = Schedule::durationBadge(a.offset) + " before";
                     items.append(QVariantMap{{"id", a.id}, {"label", label}, {"absolute", a.kind == Alarm::Absolute}, {"sound", a.sound}});
                 }
                 return items;
             }()},
            {"locations", [&] {
                 QVariantList items;
                 for (auto id : t.locationIds) {
                     const Location l = m_store->locations().value(id);
                     if (l.id && !l.deleted)
                         items.append(QVariantMap{{"id", l.id}, {"name", l.name}, {"address", l.address}});
                 }
                 return items;
             }()},
            {"link", Automation::shareableLink(t)},
            {"alarmOffsets", [&] {
                 QVariantList offsets;
                 for (const auto &a : m_store->alarmsFor(t.id))
                     if (a.kind == Alarm::Relative) offsets.append(a.offset);
                 return offsets;
             }()},
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
