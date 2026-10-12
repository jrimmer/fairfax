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
    Q_PROPERTY(bool searchAllLists READ searchAllLists NOTIFY viewChanged)
    Q_PROPERTY(RowModel *places READ places CONSTANT)
    Q_PROPERTY(int density READ density NOTIFY settingsChanged)
    Q_PROPERTY(int contentWidth READ contentWidth NOTIFY settingsChanged)
    Q_PROPERTY(bool showNotesPreview READ showNotesPreview NOTIFY settingsChanged)
    Q_PROPERTY(bool showListNames READ showListNames NOTIFY settingsChanged)
    Q_PROPERTY(bool focusOn READ focusOn NOTIFY viewChanged)
    Q_PROPERTY(QStringList focusConditions READ focusConditions NOTIFY viewChanged)
    Q_PROPERTY(bool sourceIsSmart READ sourceIsSmart NOTIFY viewChanged)
    Q_PROPERTY(QString smartQuery READ smartQuery NOTIFY viewChanged)
    Q_PROPERTY(QString smartRangeText READ smartRangeText NOTIFY viewChanged)
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
    RowModel *places() { return &m_places; }
    bool searchAllLists() const { return m_searchAll; }
    int density() const { return m_store->setting("appearance.density", 1).toInt(); }
    int contentWidth() const { return m_store->setting("appearance.contentWidth", 980).toInt(); }
    bool showNotesPreview() const { return m_store->setting("appearance.notesPreview", false).toBool(); }
    bool showListNames() const { return m_store->setting("appearance.listNames", false).toBool(); }

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
    QString smartRangeText() const;
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
    Q_INVOKABLE void setProjectSort(qint64 projectId, int field, bool ascending = true);
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
    // Drag and drop of the selection onto a sidebar entry, a calendar day, or
    // before another task (manual reordering).
    Q_INVOKABLE void dropOnSource(const QString &sourceKey);
    Q_INVOKABLE void dropOnDate(const QString &isoDate);
    Q_INVOKABLE void dropBefore(qint64 taskId);
    Q_INVOKABLE QString dropHint(const QString &sourceKey) const;
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
    Q_INVOKABLE void setRecurrence(qint64 id, const QVariantMap &rule);
    Q_INVOKABLE QString describeRecurrence(const QVariantMap &rule) const;
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
    void requestQuickEntryWith(const QString &text) { emit quickEntryTextRequested(text); }
    QList<qint64> selectedIds() const { return QList<qint64>(m_selected.cbegin(), m_selected.cend()); }
    Q_INVOKABLE QString shareableLink(qint64 taskId) const;

    // Backups: automatic every 24 or 48 hours, keeping the newest N, plus
    // Backup Now and restore. A restore first backs up what it replaces.
    Q_INVOKABLE bool backupNow(const QString &reason = {});
    Q_INVOKABLE QVariantList backups() const;
    Q_INVOKABLE bool restoreBackup(const QString &path);
    Q_INVOKABLE QString backupFolder() const;

    // Import and export (paths come from the portal file picker).
    Q_INVOKABLE void exportTo(const QString &path, const QString &format, bool selectionOnly = false);
    Q_INVOKABLE void importFrom(const QString &path, const QString &format);

    // The GTD Inbox: a real list that Quick Entry fills by default.
    Q_INVOKABLE void setInboxEnabled(bool on);
    Q_INVOKABLE bool inboxEnabled() const;
    Q_INVOKABLE void requestWindow() { emit windowRequested(); }
    Q_INVOKABLE void requestNewWindow() { emit newWindowRequested(); }
    Q_INVOKABLE void setTagPaused(qint64 tagId, bool paused);
    Q_INVOKABLE qint64 newTag(const QString &name, qint64 groupId = 0);

    // Sidebar arrangement and the archive.
    Q_INVOKABLE void moveList(qint64 listId, qint64 beforeListId, qint64 groupId);
    Q_INVOKABLE void moveGroup(qint64 groupId, qint64 beforeGroupId);
    Q_INVOKABLE void deleteGroup(qint64 groupId);
    Q_INVOKABLE QVariantList archivedLists() const;

    // Sections within a list (the reference app's task sections).
    Q_INVOKABLE qint64 newSection(qint64 listId, const QString &name);
    Q_INVOKABLE void renameSection(qint64 sectionId, const QString &name);
    Q_INVOKABLE void deleteSection(qint64 sectionId);
    Q_INVOKABLE void moveSection(qint64 sectionId, int delta);
    Q_INVOKABLE void moveSelectionToSection(qint64 sectionId);
    Q_INVOKABLE void dropOnSection(const QString &sectionKey);
    Q_INVOKABLE QVariantList sectionChoices(qint64 listId) const;

    // Tags panel management.
    Q_INVOKABLE void renameTag(qint64 tagId, const QString &name);
    Q_INVOKABLE void deleteTag(qint64 tagId);
    Q_INVOKABLE void setTagGroup(qint64 tagId, qint64 groupId);
    Q_INVOKABLE qint64 newTagGroup(const QString &name);
    Q_INVOKABLE void renameTagGroup(qint64 groupId, const QString &name);
    Q_INVOKABLE void deleteTagGroup(qint64 groupId);
    Q_INVOKABLE QVariantList tagGroupChoices() const;
    Q_INVOKABLE void tagTask(qint64 taskId, qint64 tagId);      // A tag dropped on a task.
    Q_INVOKABLE void dropOnTag(qint64 tagId);                  // Tasks dropped on a tag (or 0 = tag group members).
    Q_INVOKABLE void dropOnTagGroup(qint64 groupId);

    // Locations.
    Q_INVOKABLE qint64 newLocation(const QString &name, const QString &address);
    Q_INVOKABLE void updateLocation(qint64 id, const QString &name, const QString &address);
    Q_INVOKABLE void deleteLocation(qint64 id);
    Q_INVOKABLE void setLocationPaused(qint64 id, bool paused);
    Q_INVOKABLE void toggleTaskLocation(qint64 taskId, qint64 locationId);
    Q_INVOKABLE void dropOnLocation(qint64 locationId);
    Q_INVOKABLE void toggleLocationFilter(qint64 locationId);
    Q_INVOKABLE QVariantList locationChoices() const;

    // Sub-tasks edited inside a task's editor.
    Q_INVOKABLE qint64 addSubtask(qint64 parentId, const QString &title, qint64 afterId = 0);
    Q_INVOKABLE void setAllCollapsed(bool collapsed);

    // Show Last Selected Task (Shift+F5): walks back through recent selections.
    Q_INVOKABLE void showLastSelected();

    // Clipboard: copy selected tasks as text, paste lines as new tasks.
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void pasteTasks();

    // Files dropped on a task (or on empty space, making a new task).
    Q_INVOKABLE void dropFiles(qint64 taskId, const QVariantList &urls);
    Q_INVOKABLE void removeAttachment(qint64 attachmentId);
    Q_INVOKABLE void openAttachment(qint64 attachmentId);
    // Audio notes recorded with PipeWire's pw-record, Omarchy's audio stack.
    Q_INVOKABLE void toggleRecording(qint64 taskId);
    Q_PROPERTY(qint64 recordingTaskId READ recordingTaskId NOTIFY recordingChanged)
    qint64 recordingTaskId() const { return m_recordingTask; }

    // Calendar Day Bar time blocking: the selection is due at that slot.
    Q_INVOKABLE void timeBlock(const QString &isoDate, int startMinute, int durationMinutes);

    // Alerts beyond the presets.
    Q_INVOKABLE void addAbsoluteAlarm(qint64 taskId, const QString &isoDate, int minutes);
    Q_INVOKABLE void removeAlarm(qint64 alarmId);

    // Search everywhere rather than in the current list (the reference app's scope toggle).
    Q_INVOKABLE void toggleSearchScope();

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
    void taskCompleted(bool done);   // For the completion sound.
    void celebrate();                // The last open task in view was just completed.
    void newWindowRequested();
    void recordingChanged();
    void quickEntryTextRequested(const QString &text);
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
    QStringList locationNames() const;
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
    RowModel m_places;
    bool m_searchAll = false;
    class QProcess *m_recorder = nullptr;
    qint64 m_recordingTask = 0;
    QString m_recordingFile;
    QVector<qint64> m_history;
    int m_historyStep = 0;
    void rebuildPlaces(const ViewBuilder &views);
    void completeAfterAutoDelete();
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
    QTimer m_backupTimer;
    void maybeBackup();
    QDate m_lastDay;
};
