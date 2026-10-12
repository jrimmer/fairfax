#pragma once

// The app's state and actions, exposed to QML as `backend`. It owns the store,
// tracks what is selected and being edited, and turns every user action into
// an undoable store change. After each change it recomputes the three panes
// (sidebar, task list, tags) as row lists for the RowModels.

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "nlparse.h"
#include "rowmodel.h"
#include "schedule.h"
#include "store.h"
#include "views.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(RowModel *sidebar READ sidebar CONSTANT)
    Q_PROPERTY(RowModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(RowModel *tags READ tags CONSTANT)
    Q_PROPERTY(QString sourceKey READ sourceKey NOTIFY viewChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY viewChanged)
    Q_PROPERTY(QString sourceSubtitle READ sourceSubtitle NOTIFY viewChanged)
    Q_PROPERTY(int sourceColor READ sourceColor NOTIFY viewChanged)
    Q_PROPERTY(bool sourceIsFocus READ sourceIsFocus NOTIFY viewChanged)
    Q_PROPERTY(int sortField READ sortField NOTIFY viewChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY viewChanged)
    Q_PROPERTY(bool separated READ separated NOTIFY viewChanged)
    Q_PROPERTY(QVariantList sortChoices READ sortChoices NOTIFY viewChanged)
    Q_PROPERTY(bool showScheduled READ showScheduled WRITE setShowScheduled NOTIFY viewChanged)
    Q_PROPERTY(bool showPaused READ showPaused WRITE setShowPaused NOTIFY viewChanged)
    Q_PROPERTY(bool hideCompleted READ hideCompleted WRITE setHideCompleted NOTIFY viewChanged)
    Q_PROPERTY(bool hasHidden READ hasHidden NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(bool soundex READ soundex WRITE setSoundex NOTIFY viewChanged)
    Q_PROPERTY(bool focusOn READ focusOn NOTIFY viewChanged)
    Q_PROPERTY(QStringList focusConditions READ focusConditions NOTIFY viewChanged)
    Q_PROPERTY(bool sourceIsSmart READ sourceIsSmart NOTIFY viewChanged)
    Q_PROPERTY(QString smartQuery READ smartQuery NOTIFY viewChanged)
    Q_PROPERTY(QVariantList tagFilter READ tagFilter NOTIFY viewChanged)
    Q_PROPERTY(QString dayFilter READ dayFilter NOTIFY viewChanged)
    Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 currentTaskId READ currentTaskId NOTIFY selectionChanged)
    Q_PROPERTY(qint64 editingTaskId READ editingTaskId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap editor READ editor NOTIFY editorChanged)
    Q_PROPERTY(QVariantList listChoices READ listChoices NOTIFY dataChanged)
    Q_PROPERTY(QStringList tagNames READ tagNames NOTIFY dataChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY dataChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY dataChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY dataChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY dataChanged)
    Q_PROPERTY(int textSize READ textSize WRITE setTextSize NOTIFY settingsChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY settingsChanged)
    Q_PROPERTY(QString textSizeName READ textSizeName NOTIFY settingsChanged)
    Q_PROPERTY(QString today READ todayIso NOTIFY clockChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)

public:
    explicit Backend(Store *store, QObject *parent = nullptr);

    RowModel *sidebar() { return &m_sidebar; }
    RowModel *tasks() { return &m_tasks; }
    RowModel *tags() { return &m_tags; }

    QString sourceKey() const { return m_source.key(); }
    QString sourceTitle() const;
    QString sourceSubtitle() const;
    int sourceColor() const;
    bool sourceIsFocus() const { return m_source.isFocus(); }
    int sortField() const { return sortSpec().field; }
    bool sortAscending() const { return sortSpec().ascending; }
    bool separated() const { return sortSpec().separated; }
    QVariantList sortChoices() const;
    bool showScheduled() const { return m_options.showScheduled; }
    bool showPaused() const { return m_options.showPaused; }
    bool hideCompleted() const { return m_options.hideCompleted; }
    bool hasHidden() const { return m_hasHidden; }
    QString search() const { return m_options.search; }
    bool soundex() const { return m_options.soundex; }
    void setSoundex(bool on);
    bool focusOn() const;
    QStringList focusConditions() const;
    bool sourceIsSmart() const;
    QString smartQuery() const;
    QVariantList tagFilter() const;
    QString dayFilter() const;
    QString selectionSummary() const;
    int selectionCount() const { return m_selected.size(); }
    qint64 currentTaskId() const { return m_cursor; }
    qint64 editingTaskId() const { return m_editing; }
    QVariantMap editor() const { return m_editor; }
    QVariantList listChoices() const;
    QStringList tagNames() const;
    bool canUndo() const { return m_store->canUndo(); }
    bool canRedo() const { return m_store->canRedo(); }
    QString undoLabel() const { return m_store->undoLabel(); }
    QString redoLabel() const { return m_store->redoLabel(); }
    int textSize() const;
    void setTextSize(int size);
    int fontSize() const;
    QString textSizeName() const;
    QString todayIso() const;
    QString statusMessage() const { return m_status; }

    void setShowScheduled(bool on);
    void setShowPaused(bool on);
    void setHideCompleted(bool on);
    void setSearch(const QString &text);

    // Navigation.
    Q_INVOKABLE void selectSource(const QString &key);
    Q_INVOKABLE void selectAdjacentSource(int delta);
    Q_INVOKABLE void toggleGroupCollapsed(qint64 groupId);
    Q_INVOKABLE void toggleListExpanded(qint64 listId);
    Q_INVOKABLE void toggleSection(const QString &sectionKey, bool all = false);
    Q_INVOKABLE void setSort(int field);
    Q_INVOKABLE void toggleSortDirection();
    Q_INVOKABLE void setSeparated(bool on);
    Q_INVOKABLE void toggleTagFilter(qint64 tagId);
    Q_INVOKABLE void clearTagFilter();
    Q_INVOKABLE void setDayFilter(const QString &isoDate);
    Q_INVOKABLE QVariantList monthDays(int year, int month) const;

    // Selection and editing.
    Q_INVOKABLE void selectTask(qint64 id, int mode = 0);  // 0 replace, 1 extend range, 2 toggle.
    Q_INVOKABLE void moveCursor(int delta, bool extend = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void beginEdit(qint64 id = 0);
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE bool isSelected(qint64 id) const { return m_selected.contains(id); }

    // Creating and changing tasks. Selection-wide actions apply to every
    // selected task, as the reference app's bare-key commands do.
    Q_INVOKABLE void newTask(int type = 0, bool subtask = false);
    Q_INVOKABLE void toggleComplete(qint64 id = 0);
    Q_INVOKABLE void setCompleted(bool done);
    Q_INVOKABLE void setPriority(int priority, qint64 id = 0);
    Q_INVOKABLE void bumpPriority(int delta);
    Q_INVOKABLE void setStarred(int state, qint64 id = 0);  // 1 star, 0 unstar, -1 toggle.
    Q_INVOKABLE void nudgeDue(int days);
    Q_INVOKABLE void nudgeStart(int days);
    Q_INVOKABLE void setDueInDays(int days);
    Q_INVOKABLE void setStartInDays(int days);
    Q_INVOKABLE void clearDue();
    Q_INVOKABLE void clearStart();
    Q_INVOKABLE void deferTo(const QString &isoDate, int minutes = -2);
    Q_INVOKABLE void scheduleTo(const QString &isoDate, int minutes = -2);
    Q_INVOKABLE void setSelectionDuration(int minutes);
    Q_INVOKABLE void setSelectionDueTime(int minutes);
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicateSelection();
    Q_INVOKABLE void moveSelectionToList(qint64 listId);
    Q_INVOKABLE void moveSelection(int delta);
    Q_INVOKABLE void toggleCollapsed(qint64 id = 0);
    Q_INVOKABLE void convertSelection(int type);
    Q_INVOKABLE void tagSelection(const QString &names, bool replace);
    Q_INVOKABLE void untagSelection();
    Q_INVOKABLE void performAction(qint64 id = 0);
    Q_INVOKABLE void openLinks(qint64 id = 0);

    // Editor fields for one task.
    Q_INVOKABLE void setTitle(qint64 id, const QString &title);
    Q_INVOKABLE void setNotes(qint64 id, const QString &notes);
    Q_INVOKABLE void setTaskList(qint64 id, qint64 listId);
    Q_INVOKABLE void setDue(qint64 id, const QString &isoDate, int minutes = -2);
    Q_INVOKABLE void setStart(qint64 id, const QString &isoDate, int minutes = -2);
    Q_INVOKABLE bool setDueFromText(qint64 id, const QString &text);
    Q_INVOKABLE bool setStartFromText(qint64 id, const QString &text);
    Q_INVOKABLE bool setTimeFromText(qint64 id, bool due, const QString &text);
    Q_INVOKABLE void setDuration(qint64 id, int minutes);
    Q_INVOKABLE void addTag(qint64 id, const QString &name);
    Q_INVOKABLE void removeTag(qint64 id, qint64 tagId);
    Q_INVOKABLE void setType(qint64 id, int type);
    Q_INVOKABLE void setAction(qint64 id, int type, const QString &value);
    Q_INVOKABLE void setRepeatPreset(qint64 id, int preset);
    Q_INVOKABLE void setRepeatFromCompletion(qint64 id, bool on);
    Q_INVOKABLE void toggleAlarm(qint64 id, int minutesBefore);
    Q_INVOKABLE void clearAlarms(qint64 id);
    Q_INVOKABLE QVariantList dateSuggestions(const QString &text) const;

    // Lists, groups and tags.
    Q_INVOKABLE qint64 newList(const QString &name = {});
    Q_INVOKABLE qint64 newGroup(const QString &name = {});
    Q_INVOKABLE void renameList(qint64 id, const QString &name);
    Q_INVOKABLE void renameGroup(qint64 id, const QString &name);
    Q_INVOKABLE void setListColor(qint64 id, int color);
    Q_INVOKABLE void setListFlag(qint64 id, const QString &flag, bool on);
    Q_INVOKABLE void deleteList(qint64 id);
    Q_INVOKABLE QVariantMap listInfo(qint64 id) const;
    Q_INVOKABLE QVariantMap taskInfo(qint64 id) const;
    Q_INVOKABLE QVariantList findTasks(const QString &text, int limit = 12) const;
    Q_INVOKABLE void revealInList(qint64 taskId);

    // Focus Filter: per-list conditions, OR-combined, toggled as a whole.
    Q_INVOKABLE void toggleFocus();
    Q_INVOKABLE void setFocusCondition(const QString &condition, bool on);

    // Smart Lists are saved searches.
    Q_INVOKABLE qint64 saveSmartList(const QString &name, const QString &query);
    Q_INVOKABLE void setSmartQuery(qint64 listId, const QString &query);

    // Quick Entry and natural-language input.
    Q_INVOKABLE QVariantMap previewEntry(const QString &text) const;
    Q_INVOKABLE qint64 quickAdd(const QString &text, const QString &notes, int type, qint64 listId);
    Q_INVOKABLE qint64 quickEntryListId() const;
    Q_INVOKABLE QVariantMap setting(const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void requestQuickEntry() { emit quickEntryRequested(); }
    Q_INVOKABLE void requestWindow() { emit windowRequested(); }
    Q_INVOKABLE void setTagPaused(qint64 tagId, bool paused);
    Q_INVOKABLE qint64 newTag(const QString &name, qint64 groupId = 0);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Seeds a first-run database, and the demo data used for screenshots.
    void ensureDefaults();
    void seedDemo();

signals:
    void viewChanged();
    void selectionChanged();
    void editorChanged();
    void dataChanged();
    void settingsChanged();
    void clockChanged();
    void statusChanged();
    void revealTask(qint64 id);  // Asks the list to scroll a task into view.
    void quickEntryRequested();
    void windowRequested();

private:
    SortSpec sortSpec() const;
    void storeSortSpec(const SortSpec &spec);
    ScheduleSettings scheduleSettings() const;
    QVector<qint64> targetIds(qint64 id = 0) const;
    void editTasks(const QString &label, const QVector<qint64> &ids, const std::function<void(Task &)> &edit);
    void completeTask(Task task, bool done, const QDateTime &now);
    void deleteTree(qint64 id);
    qint64 duplicateTree(const Task &task, qint64 parentId, qint64 listId);
    qint64 defaultListId() const;
    qint64 tagIdForName(const QString &name, bool create);
    double nextOrder(qint64 listId, qint64 parentId) const;
    void setStatus(const QString &message);
    void applyParsed(Task &task, const ParsedEntry &parsed);
    QStringList listNames() const;
    QString focusSettingKey() const { return "focus." + m_source.key(); }

    void rebuild();
    void rebuildSidebar(const ViewBuilder &views);
    void rebuildTasks(const ViewBuilder &views);
    void rebuildTags(const ViewBuilder &views);
    void rebuildEditor();
    QVariantMap taskRow(const Task &t, const TaskRow &row, const ViewBuilder &views) const;
    void saveUiState();

    Store *m_store;
    RowModel m_sidebar;
    RowModel m_tasks;
    RowModel m_tags;
    Source m_source;
    ViewOptions m_options;
    QSet<qint64> m_expandedLists;
    QVector<qint64> m_visibleOrder;
    QVector<qint64> m_selected;
    qint64 m_cursor = 0;
    qint64 m_anchor = 0;
    qint64 m_editing = 0;
    qint64 m_newTask = 0;
    bool m_hasHidden = false;
    QVariantMap m_editor;
    QString m_status;
    QTimer m_clock;
    QDate m_lastDay;
};
