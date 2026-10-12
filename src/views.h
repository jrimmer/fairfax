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
#include "schedule.h"
#include "store.h"

class Query;

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
    static bool matchesFocusCondition(const Task &task, const QString &condition, const QDateTime &now,
                                      const ScheduleSettings &settings);
    bool listVisible(qint64 listId) const;
    QString sourceTitle(const Source &source) const;
    QVector<TaskList> orderedLists() const;  // Sidebar order: ungrouped first, then by group.
    QVector<Task> children(qint64 parentId) const;  // Sorted by manual order.

private:
    bool matchesFocus(const Task &task, int kind) const;
    bool passesFilters(const Task &task, const Source &source, const ViewOptions &options,
                       const Query *query = nullptr) const;
    bool completedVisible(const Task &task, const Source &source, const ViewOptions &options) const;
    QVector<Section> sectionize(const QVector<Task> &items, const Source &source, const SortSpec &sort) const;
    void sortItems(QVector<Task> &items, const SortSpec &sort) const;

    const Store &m_store;
    QDateTime m_now;
    QDate m_today;
    ScheduleSettings m_settings;
    QSet<qint64> m_pausedTags;
    QSet<qint64> m_pausedLocations;
    QHash<qint64, QVector<qint64>> m_childrenOf;
};
