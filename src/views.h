#pragma once

// Builds what the three panes show. Given the store, the selected source
// (a focus list, a list or a project) and the view options, it decides which
// tasks are visible, how they nest, which sections they fall into and how
// they sort, following the reference app's focus-list and Smart Scheduling rules.

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

#include "model.h"
#include "query.h"
#include "schedule.h"
#include "store.h"

struct Source {
    enum Kind { All = 0, Today = 1, Starred = 2, Scheduled = 3, Done = 4, List = 5, Project = 6 };
    int kind = Today;
    qint64 id = 0;

    bool isFocus() const { return kind <= Done; }
    QString key() const;
    static Source fromKey(const QString &key);
    bool operator==(const Source &) const = default;
};

struct ViewOptions {
    bool showScheduled = false;
    bool showPaused = false;
    bool hideCompleted = false;
    int completedWindowMinutes = 24 * 60;  // -1 keeps completed tasks forever, 0 hides them at once.
    QSet<qint64> tagFilter;
    QSet<qint64> locationFilter;
    QString search;  // the reference app search syntax; see query.h.
    bool soundex = false;
    QStringList focusConditions;  // Focus Filter conditions, OR-combined; empty when the filter is off.
    QDate dayFilterFrom;
    QDate dayFilterTo;
    QSet<qint64> keepVisible;  // Just-completed tasks that stay until the view changes.
    QSet<QString> collapsedSections;
    bool groupedDates = false;      // Today / Tomorrow / Next 7 Days / Future instead of one heading per day.
    bool starredAboveHigh = false;  // A Starred section above High when sorting by priority.
    // "Inherit Search in Project View": a project's children also have to
    // match the search that was active when it was opened, and the rule and
    // range of the Smart List it was opened from (both may be empty).
    bool inheritSearch = false;
    QString inheritedRule;
    QString inheritedRange;
};

struct SortSpec {
    int field = SortField::DueDate;
    bool ascending = true;
    bool separated = true;
};

struct TaskRow {
    qint64 taskId = 0;
    int depth = 0;
    bool breadcrumb = false;  // A project child shown outside its project.
    int visibleChildren = 0;
};

struct Section {
    QString key;
    QString title;
    QString tone;  // "overdue", "progress", "list" or empty.
    int color = -1;  // List palette index for "list" sections.
    int openCount = 0;
    int durationTotal = 0;
    bool collapsed = false;
    QVector<TaskRow> rows;
};

struct ListCounts {
    int overdue = 0;
    int undone = 0;
    bool dueToday = false;
};

class ViewBuilder {
public:
    ViewBuilder(const Store &store, const QDateTime &now, const ScheduleSettings &settings);

    QVector<Section> build(const Source &source, const SortSpec &sort, const ViewOptions &options) const;

    // Sidebar counters: overdue and not-done top-level items, honouring the
    // same hiding rules as the views.
    ListCounts countsForList(qint64 listId) const;
    ListCounts countsForFocus(int kind) const;

    bool isPaused(const Task &task) const;
    bool isSmartList(qint64 listId) const;
    bool inSmartRange(const Task &task, const QString &range) const;
    static bool matchesFocusCondition(const Task &task, const QString &condition, const QDateTime &now,
                                      const ScheduleSettings &settings);
    bool listVisible(qint64 listId) const;
    QString sourceTitle(const Source &source) const;
    QVector<TaskList> orderedLists() const;  // Sidebar order: ungrouped first, then by group.
    QVector<Task> children(qint64 parentId) const;  // Sorted by manual order.
    // A project or checklist's own sort (the reference app's Sort Project), or manual.
    SortSpec projectSort(qint64 parentId) const;
    // Tags as the Tags panel lists them: ungrouped first, then by group, then
    // alphabetically or, with the "tags.manual" setting, in their own order.
    // Sorting by tag uses the same order.
    static QVector<Tag> orderedTags(const Store &store);

private:
    QueryContext queryContext(const ViewOptions &options) const;
    bool matchesFocus(const Task &task, int kind) const;
    QString firstLocation(const Task &task) const;  // Alphabetically first, or empty.
    bool passesFilters(const Task &task, const Source &source, const ViewOptions &options,
                       const Query *query = nullptr) const;
    // `policy` overrides the completed-visibility setting: 0 follows it,
    // 1 shows every completed task, 2 hides them all.
    bool completedVisible(const Task &task, const Source &source, const ViewOptions &options, int policy = 0) const;
    QVector<Section> sectionize(const QVector<Task> &items, const Source &source, const SortSpec &sort,
                                const ViewOptions &options) const;
    void sortItems(QVector<Task> &items, const SortSpec &sort) const;

    const Store &m_store;
    QDateTime m_now;
    QDate m_today;
    ScheduleSettings m_settings;
    QSet<qint64> m_pausedTags;
    QSet<qint64> m_pausedLocations;
    QHash<qint64, qint64> m_tagRank;
    QHash<qint64, QVector<qint64>> m_childrenOf;
};
