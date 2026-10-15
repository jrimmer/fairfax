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
#include "shortcuts.h"
#include "store.h"
#include "views.h"

class CalendarFeed;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(RowModel *sidebar READ sidebar CONSTANT)
    // Which platform this build is for: the UI hides Linux-only controls
    // (Omarchy, Hyprland, Waybar, the fit-thirds setting and audio notes)
    // everywhere else, so nothing is shown that cannot work there.
    Q_PROPERTY(bool platformIsLinux READ platformIsLinux CONSTANT)
    Q_PROPERTY(bool platformIsMac READ platformIsMac CONSTANT)
    Q_PROPERTY(bool platformIsWindows READ platformIsWindows CONSTANT)
    Q_PROPERTY(RowModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(RowModel *tags READ tags CONSTANT)
    Q_PROPERTY(QString sourceKey READ sourceKey NOTIFY viewChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY viewChanged)
    Q_PROPERTY(QString sourceSubtitle READ sourceSubtitle NOTIFY viewChanged)
    Q_PROPERTY(int sourceColor READ sourceColor NOTIFY viewChanged)
    Q_PROPERTY(bool sourceIsFocus READ sourceIsFocus NOTIFY viewChanged)
    Q_PROPERTY(bool sourceIsMyDay READ sourceIsMyDay NOTIFY viewChanged)
    Q_PROPERTY(bool myDayEnabled READ myDayEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap myDayCapacityMap READ myDayCapacityMap NOTIFY myDayChanged)
    Q_PROPERTY(QVariantList defaultViewChoices READ defaultViewChoices NOTIFY dataChanged)
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
    Q_PROPERTY(int settingsRevision READ settingsRevision NOTIFY settingsChanged)
    Q_PROPERTY(int contentWidth READ contentWidth NOTIFY settingsChanged)
    Q_PROPERTY(bool showNotesPreview READ showNotesPreview NOTIFY settingsChanged)
    Q_PROPERTY(bool showListNames READ showListNames NOTIFY settingsChanged)
    Q_PROPERTY(bool overdueInRed READ overdueInRed NOTIFY settingsChanged)
    Q_PROPERTY(bool showTagNames READ showTagNames NOTIFY settingsChanged)
    Q_PROPERTY(bool quickLookEnabled READ quickLookEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool monthDividers READ monthDividers NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap shortcutMap READ shortcutMap NOTIFY shortcutsChanged)
    Q_PROPERTY(bool recordingShortcut READ recordingShortcut WRITE setRecordingShortcut NOTIFY recordingShortcutChanged)
    // 0 for Sunday to 6 for Saturday, as in Qt Quick's Locale.
    Q_PROPERTY(int firstDayOfWeek READ firstDayOfWeek NOTIFY settingsChanged)
    Q_PROPERTY(bool focusOn READ focusOn NOTIFY viewChanged)
    Q_PROPERTY(QStringList focusConditions READ focusConditions NOTIFY viewChanged)
    Q_PROPERTY(bool sourceIsSmart READ sourceIsSmart NOTIFY viewChanged)
    Q_PROPERTY(QString smartQuery READ smartQuery NOTIFY viewChanged)
    Q_PROPERTY(QString smartRangeText READ smartRangeText NOTIFY viewChanged)
    Q_PROPERTY(QVariantList tagFilter READ tagFilter NOTIFY viewChanged)
    Q_PROPERTY(QString dayFilter READ dayFilter NOTIFY viewChanged)
    Q_PROPERTY(QString dayFilterTo READ dayFilterTo NOTIFY viewChanged)
    Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 currentTaskId READ currentTaskId NOTIFY selectionChanged)
    Q_PROPERTY(qint64 editingTaskId READ editingTaskId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap editor READ editor NOTIFY editorChanged)
    Q_PROPERTY(QVariantList listChoices READ listChoices NOTIFY dataChanged)
    Q_PROPERTY(QVariantList traySourceChoices READ traySourceChoices NOTIFY dataChanged)
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
#if defined(Q_OS_LINUX)
    bool platformIsLinux() const { return true; }
    bool platformIsMac() const { return false; }
    bool platformIsWindows() const { return false; }
#elif defined(Q_OS_MACOS)
    bool platformIsLinux() const { return false; }
    bool platformIsMac() const { return true; }
    bool platformIsWindows() const { return false; }
#elif defined(Q_OS_WIN)
    bool platformIsLinux() const { return false; }
    bool platformIsMac() const { return false; }
    bool platformIsWindows() const { return true; }
#else
    bool platformIsLinux() const { return false; }
    bool platformIsMac() const { return false; }
    bool platformIsWindows() const { return false; }
#endif
    RowModel *tasks() { return &m_tasks; }
    RowModel *tags() { return &m_tags; }
    RowModel *places() { return &m_places; }
    bool searchAllLists() const { return m_searchAll; }
    int density() const { return m_store->setting("appearance.density", 1).toInt(); }
    int contentWidth() const { return m_store->setting("appearance.contentWidth", 980).toInt(); }
    bool showNotesPreview() const { return m_store->setting("appearance.notesPreview", false).toBool(); }
    bool showListNames() const { return m_store->setting("appearance.listNames", false).toBool(); }
    bool overdueInRed() const { return m_store->setting("appearance.overdueRed", true).toBool(); }
    bool showTagNames() const { return m_store->setting("appearance.tagNames", true).toBool(); }
    bool quickLookEnabled() const { return m_store->setting("tasks.quickLook", true).toBool(); }
    bool monthDividers() const { return m_store->setting("appearance.monthDividers", true).toBool(); }
    int firstDayOfWeek() const { return Schedule::resolveWeekStart(m_store->setting("general.weekStart", 0).toInt()) % 7; }

    QString sourceKey() const { return m_source.key(); }
    QString sourceTitle() const;
    QString sourceSubtitle() const;
    int sourceColor() const;
    bool sourceIsFocus() const { return m_source.isFocus(); }
    bool sourceIsMyDay() const { return m_source.kind == Source::MyDay; }
    bool myDayEnabled() const;
    QVariantMap myDayCapacityMap() const { return myDayCapacity(); }
    QVariantList defaultViewChoices() const;
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
    QString dayFilterTo() const;
    QString selectionSummary() const;
    int selectionCount() const { return m_selected.size(); }
    qint64 currentTaskId() const { return m_cursor; }
    qint64 editingTaskId() const { return m_editing; }
    QVariantMap editor() const { return m_editor; }
    QVariantList listChoices() const;
    // The "Quick view" choices in Settings > General: Today, Starred,
    // Scheduled, every list and Smart List, then Commands only.
    QVariantList traySourceChoices() const;
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
    // Filters to a span of days, as dragged across the mini calendar.
    Q_INVOKABLE void setDayRange(const QString &fromIso, const QString &toIso);
    Q_INVOKABLE QVariantList monthDays(int year, int month) const;

    // Selection and editing.
    Q_INVOKABLE void selectTask(qint64 id, int mode = 0);  // 0 replace, 1 extend range, 2 toggle.
    Q_INVOKABLE void moveCursor(int delta, bool extend = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void beginEdit(qint64 id = 0);
    // Opens the editor on the selected task and goes to one of its fields:
    // notes, action, location, alerts, dates or repeat (⌘E, ⌘B, ⌘G, ⌘', ⌘/, ⌘;).
    Q_INVOKABLE void editField(const QString &kind);
    Q_INVOKABLE QString takePendingField();
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
    // Moves the selection's due dates by a typed offset such as "+3d", "+1w",
    // "-2d", "+1m" or "+1y" (undated tasks count from today). False when the
    // text is not an offset.
    Q_INVOKABLE bool deferBy(const QString &offset);
    // The quick time choices of the Due Time menu and bar, from the "Due Time
    // Shortcuts" setting: a list of {label, minutes}.
    Q_INVOKABLE QVariantList dueTimeChips() const;
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
    Q_INVOKABLE void copySelectionToList(qint64 listId);
    // True while the drag in progress copies (Alt held at the press).
    Q_INVOKABLE void setDragCopy(bool on) { m_dragCopy = on; }
    // New Project / Checklist from Selection: the selected tasks become the
    // children of a new parent in the first one's list.
    Q_INVOKABLE void newFromSelection(int type);
    // Goto Project: reveals the project or checklist a sub-task belongs to.
    Q_INVOKABLE void gotoProject(qint64 id = 0);
    Q_INVOKABLE void linkFile(qint64 taskId, const QString &path);
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
    // Untag...: the tags the selection carries, and removing some of them.
    Q_INVOKABLE QVariantList selectionTags() const;
    Q_INVOKABLE void untagSelection(const QVariantList &tagIds);
    Q_INVOKABLE void performAction(qint64 id = 0);
    // The Google domain for a locale's country (google.co.uk, google.de, ...), .com when unknown.
    static QString googleDomain(const QLocale &locale);
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
    // Recolours the group and every list in it ("Color grouped lists…").
    Q_INVOKABLE void setGroupColor(qint64 groupId, int color);
    Q_INVOKABLE void setAllGroupsCollapsed(bool collapsed);
    Q_INVOKABLE QVariantList groupChoices() const;
    // New task in a given list: the sidebar's per-row [+].
    Q_INVOKABLE void newTaskInList(qint64 listId);
    // Hidden lists (Settings > Today & Lists): out of the sidebar, All,
    // Today, searches and counts until revealed, which lasts one session.
    Q_INVOKABLE void setListHidden(qint64 id, bool hidden);
    Q_INVOKABLE QVariantList hideableLists() const;
    Q_INVOKABLE bool hasHiddenLists() const;
    Q_INVOKABLE bool hiddenRevealed() const;
    Q_INVOKABLE void setHiddenRevealed(bool revealed);
    // Done's "growing number of completed tasks" warning.
    static constexpr int DoneWarningThreshold = 500;
    Q_INVOKABLE int completedCount() const;
    Q_INVOKABLE bool doneWarningShown() const;
    Q_INVOKABLE QVariantMap listInfo(qint64 id) const;
    // "Don't Sync This List": git, folder, WebDAV and Todoist sync skip it
    // and its tasks. Kept in a setting, on this machine only.
    Q_INVOKABLE void setListSyncExcluded(qint64 id, bool excluded);
    Q_INVOKABLE QVariantMap taskInfo(qint64 id) const;
    Q_INVOKABLE QVariantList findTasks(const QString &text, int limit = 12, bool exact = false) const;
    Q_INVOKABLE void revealInList(qint64 taskId);
    // @{Title} links. A link resolves to the task whose title matches exactly,
    // else case-insensitively, else by prefix; within a tier a task in the same
    // list as `fromId` wins, then the most recently changed. A task never
    // links to itself. Returns 0 when nothing matches.
    Q_INVOKABLE qint64 resolveTaskLink(const QString &title, qint64 fromId = 0) const;
    // Reveals the linked task, or says in the status line that there is none.
    Q_INVOKABLE bool openTaskLink(const QString &title, qint64 fromId = 0);
    // A task as plain text (title, then notes), as Quick Look's "Copy as Text" puts it.
    Q_INVOKABLE QString taskAsText(qint64 id) const;
    // Notes as Quick Look renders them: Markdown with clickable task links.
    Q_INVOKABLE QString renderNotes(const QString &notes) const;
    Q_INVOKABLE void copyText(const QString &text, const QString &status = QString());
    // The jump, find and quick-open bars. Jump lists every list, including
    // those in collapsed groups; find looks inside collapsed projects too;
    // Quick Open starts from what was visited lately.
    // Whether `haystack` matches `needle` in the jump and find bars: loosely
    // (a case-insensitive substring) or, with `exact`, as whole words.
    Q_INVOKABLE bool textMatches(const QString &haystack, const QString &needle, bool exact = false) const;
    Q_INVOKABLE QVariantList jumpTargets(const QString &text, bool exact = false) const;
    Q_INVOKABLE QVariantList findInView(const QString &text, int limit = 12, bool exact = false) const;
    // Quick Open on a task: selected where it stands when the current view
    // shows it (and the option is on), otherwise revealed in its own list.
    Q_INVOKABLE void openTask(qint64 taskId);
    Q_INVOKABLE void revealHere(qint64 taskId);
    Q_INVOKABLE QVariantList recentItems(int limit = 8) const;

    // Focus Filter: per-list conditions, OR-combined, toggled as a whole.
    Q_INVOKABLE void toggleFocus();
    Q_INVOKABLE void setFocusCondition(const QString &condition, bool on);

    // Smart Lists are saved searches.
    Q_INVOKABLE qint64 saveSmartList(const QString &name, const QString &query, const QString &range = {},
                                     int completedMode = 0, bool includeScheduled = false);
    Q_INVOKABLE void setSmartQuery(qint64 listId, const QString &query);
    // The pieces of a Smart List the search builder edits.
    Q_INVOKABLE QVariantMap smartListInfo(qint64 listId) const;
    // The rule builder's tree for a `type:` value (with `ok` added; false when
    // it has no row form), the value for a tree, and the property table.
    Q_INVOKABLE QVariantMap rulesFromType(const QString &typeValue) const;
    Q_INVOKABLE QString rulesToType(const QVariantMap &tree) const;
    Q_INVOKABLE QVariantList ruleProperties() const;
    Q_INVOKABLE void updateSmartList(qint64 listId, const QString &name, const QString &query, const QString &range,
                                     int completedMode, bool includeScheduled);
    // Include/Exclude Completed on a list (0 follows the global setting).
    Q_INVOKABLE void setListCompletedMode(qint64 id, int mode);
    Q_INVOKABLE void deleteAllTasks(qint64 id);

    // Quick Entry and natural-language input.
    Q_INVOKABLE QVariantMap previewEntry(const QString &text) const;
    // Turns natural-language reading of a new task's title off or on for
    // that task (the editor's "Stop detecting" chip).
    Q_INVOKABLE void setDetecting(qint64 taskId, bool on);
    Q_INVOKABLE qint64 quickAdd(const QString &text, const QString &notes, int type, qint64 listId);
    // The redesigned Quick Entry: the window keeps its own field values
    // (seeded from previewEntry().fields as you type, then adjusted with its
    // pills) and saves them exactly, without parsing the title again.
    // Keys: title, notes, type, listId, dueIso, dueMinutes, startIso,
    // startMinutes, recurrence (map, as setRecurrence), alerts (minutes
    // before), duration, tags (names), locationId, priority, starred, files,
    // actionType and actionValue. A title that is a bare URL gets a URL
    // action pointing at it unless an action is given.
    Q_INVOKABLE qint64 quickAddFields(const QVariantMap &fields);
    // Where Quick Entry starts: its list and the default due date, if any.
    Q_INVOKABLE QVariantMap quickEntryDefaults() const;
    Q_INVOKABLE qint64 quickEntryListId() const;
    // Splits pasted text for the Quick Entry title: the first non-empty line
    // is the title, the rest goes to the notes, and a bare URL is flagged so
    // it can become the task's URL action. Keys: title, notes, url.
    Q_INVOKABLE QVariantMap splitPaste(const QString &text) const;
    Q_INVOKABLE QVariantMap setting(const QString &key) const;
    // A setting read as a switch: stored settings come back as text after a
    // restart, so "false" must read as false. Bindings that use it should
    // also read settingsRevision, which changes with every setting, so they
    // re-evaluate when a setting changes.
    Q_INVOKABLE bool settingFlag(const QString &key, bool fallback) const;
    int settingsRevision() const { return m_settingsRevision; }
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

    // Keyboard shortcuts: the id -> binding map QML binds its Shortcuts to,
    // the command list the Settings pane and the overlay read, and the
    // invokables that change a binding. Every change emits shortcutsChanged.
    QVariantMap shortcutMap() const;
    bool recordingShortcut() const { return m_recordingShortcut; }
    Q_INVOKABLE QVariantList shortcutCommands() const;
    Q_INVOKABLE void setShortcut(const QString &id, const QString &sequence);
    Q_INVOKABLE void resetShortcut(const QString &id);
    Q_INVOKABLE void resetAllShortcuts();
    Q_INVOKABLE QString shortcutConflict(const QString &id, const QString &sequence) const;
    Q_INVOKABLE QString shortcutProblem(const QString &id, const QString &sequence) const;
    Q_INVOKABLE QString shortcutText(const QString &id) const;
    Q_INVOKABLE QString shortcutFor(int key, int modifiers) const;
    // The Settings pane starts and stops recording a key; while it records,
    // the application shortcuts are held back so the key being pressed is
    // seen by the pane, not acted on.
    Q_INVOKABLE void setRecordingShortcut(bool on);
    Q_INVOKABLE void requestQuickEntry() { emit quickEntryRequested(); }
    void requestQuickEntryWith(const QString &text) { emit quickEntryTextRequested(text); }
    // AppServices wires the sync hub's and the mail connector's failures here,
    // so a screen reader hears them without the sidebar being visible.
    void screenReaderAnnounce(const QString &message) { emit announce(message); }
    void screenReaderAnnounceUrgent(const QString &message) { emit announceAssertive(message); }
    QList<qint64> selectedIds() const { return QList<qint64>(m_selected.cbegin(), m_selected.cend()); }
    Q_INVOKABLE QString shareableLink(qint64 taskId) const;
    Q_INVOKABLE QString licenseText() const;

    // Backups: automatic every 24 or 48 hours, keeping the newest N, plus
    // Backup Now and restore. A restore first backs up what it replaces.
    Q_INVOKABLE bool backupNow(const QString &reason = {});
    Q_INVOKABLE QVariantList backups() const;
    Q_INVOKABLE bool restoreBackup(const QString &path);
    // Restore Selected Lists: the lists a backup holds ({uid, name, tasks}),
    // and adding chosen ones to the current database as new records in a
    // "Restored" group (undoable; the backup itself is only read).
    Q_INVOKABLE QVariantList backupLists(const QString &path) const;
    Q_INVOKABLE int restoreBackupLists(const QString &path, const QStringList &listUids);
    Q_INVOKABLE bool saveBackupCopy(const QString &path, const QString &destination);
    Q_INVOKABLE bool deleteBackup(const QString &path);
    Q_INVOKABLE QString backupFolder() const;

    // Diagnostics pane: versions, paths, counts and the latest warnings as
    // text to read or copy for a bug report. `extra` is appended as is (the
    // sync providers' statuses, which only the window knows).
    Q_INVOKABLE QString diagnosticsReport(const QString &extra = {}) const;
    Q_INVOKABLE void copyDiagnostics(const QString &extra = {});

    // Import and export (paths come from the portal file picker).
    Q_INVOKABLE void exportTo(const QString &path, const QString &format, bool selectionOnly = false);
    Q_INVOKABLE void importFrom(const QString &path, const QString &format);
    // The export chooser: `listIds` are the lists to include and `scope` is
    // 0 all, 1 not done, 2 done. Returns how many tasks were written.
    Q_INVOKABLE int exportLists(const QString &path, const QString &format, const QVariantList &listIds, int scope);
    // Add to Calendar: a one-event .ics for the task, opened with the desktop
    // calendar handler, or saved to `path` when one is given.
    Q_INVOKABLE bool addToCalendar(qint64 taskId, const QString &path = {});
    // Print / Save as PDF of the list as shown, opened afterwards in the PDF viewer.
    Q_INVOKABLE bool printView(const QString &path);
    // Email These Tasks: a mailto: with the selected tasks as plain text.
    Q_INVOKABLE void emailTasks(qint64 id = 0);
    // A temporary .ics or .txt for the selection, as a file: URL, for dragging
    // tasks out of the window.
    Q_INVOKABLE QString dragOutFile();
    // Drops of mail messages (.eml) become the task's action.
    void dropMail(qint64 taskId, const QString &path);

    // The GTD Inbox: a real list that Quick Entry fills by default.
    Q_INVOKABLE void setInboxEnabled(bool on);
    Q_INVOKABLE bool inboxEnabled() const;
    Q_INVOKABLE void requestWindow() { emit windowRequested(); }
    Q_INVOKABLE void requestNewWindow() { emit newWindowRequested(); }
    void requestPanel(const QString &panel) { emit windowRequested(); emit panelRequested(panel); }
    Q_INVOKABLE void setTagPaused(qint64 tagId, bool paused);
    // Tags can be listed alphabetically or in a manual order set by dragging.
    Q_PROPERTY(bool tagsManual READ tagsManual NOTIFY dataChanged)
    bool tagsManual() const { return m_store->setting("tags.manual", false).toBool(); }
    Q_INVOKABLE void setTagsManual(bool on);
    Q_INVOKABLE void moveTag(qint64 tagId, qint64 beforeTagId, qint64 groupId);
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
    // Cut is copy then delete; pasting what was just cut or copied restores
    // the tasks with everything they carried, not only their titles.
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE void pasteTasks();
    // Indented lines (tabs, two or more spaces) nest as sub-tasks.
    Q_INVOKABLE void pasteText(const QString &text);

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

    // My Day: the daily plan's sections, capacity line and planning actions.
    // The calendar feed is the one the app shows, injected at construction;
    // its changes refresh the capacity line, which reads the day's events.
    void setCalendar(CalendarFeed *calendar);
    Q_INVOKABLE QVariantMap myDaySections() const;
    Q_INVOKABLE QVariantMap myDayCapacity() const;
    Q_INVOKABLE void myDayAdd(const QVariantList &ids);
    Q_INVOKABLE void myDayRemove(const QVariantList &ids);
    Q_INVOKABLE void myDayHide(const QVariantList &ids);
    // Puts the task in the first free gap of its length, giving it a 30-minute
    // duration first when it has none.
    Q_INVOKABLE void myDayBlockNext(qint64 id);
    // Expands the capped Overdue or Suggestions section of My Day.
    Q_INVOKABLE void myDayExpand(const QString &section);
    // Plan-mode keys: the list's selected tasks as a plain id list, and the
    // four bare-key actions A, B, R and H. They act only while My Day is on
    // screen; elsewhere they do nothing.
    Q_INVOKABLE QVariantList selectedTaskIds() const;
    Q_INVOKABLE void myDayAddSelection();
    Q_INVOKABLE void myDayBlockSelection();
    Q_INVOKABLE void myDayRemoveSelection();
    Q_INVOKABLE void myDayHideSelection();
    // Called when My Day is opened by any route: marks today as planned so the
    // once-a-day prompt does not ask again.
    void markMyDayPlanned();
    // The first launch of the day: true when the prompt should be offered on
    // the next window show.
    Q_INVOKABLE bool myDayPromptDue() const;
    Q_INVOKABLE void myDayPromptAnswered(bool planned);

    // Alerts beyond the presets.
    Q_INVOKABLE void addAbsoluteAlarm(qint64 taskId, const QString &isoDate, int minutes);
    Q_INVOKABLE void addDaysBeforeAlarm(qint64 taskId, int days, int minutes);
    Q_INVOKABLE void setAlarmSound(qint64 alarmId, bool sound);
    Q_INVOKABLE void removeAlarm(qint64 alarmId);

    // Search everywhere rather than in the current list (the reference app's scope toggle).
    Q_INVOKABLE void toggleSearchScope();
    Q_INVOKABLE void setSearchAllLists(bool all);

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
    void shortcutsChanged();
    void recordingShortcutChanged();
    void clockChanged();
    void statusChanged();
    void doneWarningRequested();  // Done holds 500+ completed tasks and auto-delete is off.
    void revealTask(qint64 id);  // Asks the list to scroll a task into view.
    void quickEntryRequested();
    void taskCompleted(bool done);   // For the completion sound.
    void celebrate();                // The last open task in view was just completed.
    void newWindowRequested();
    void panelRequested(const QString &panel);  // "open" (Quick Open) or "settings".
    void recordingChanged();
    void myDayChanged();  // The My Day plan or capacity moved.
    void quickEntryTextRequested(const QString &text);
    void editEnding();  // The editor is about to close: it saves what is still in its fields.
    void editFieldRequested();  // The editor is open; takePendingField() says where to go.
    void windowRequested();
    // What a screen reader should say for a change it would otherwise miss:
    // a completion, a move, a delete, undo and redo, copy and paste. Sync
    // and mail failures are urgent enough to interrupt; announceAssertive
    // asks Main.qml for the assertive politeness instead of the polite one.
    void announce(const QString &message);
    void announceAssertive(const QString &message);

private:
    SortSpec sortSpec() const;
    void storeSortSpec(const SortSpec &spec);
    ScheduleSettings scheduleSettings() const;
    QVector<qint64> targetIds(qint64 id = 0) const;
    void editTasks(const QString &label, const QVector<qint64> &ids, const std::function<void(Task &)> &edit);
    void completeTask(Task task, bool done, const QDateTime &now);
    // Copies a repeating project's children (as they stand) under its completed
    // copy and resets the live ones, then the same for their own children.
    void archiveChildren(qint64 parentId, qint64 copyId);
    // Rescheduling a project sets its completed children back to active.
    void reopenChildren(const Task &project);
    void deleteTree(qint64 id);
    qint64 duplicateTree(const Task &task, qint64 parentId, qint64 listId);
    void collectTree(const Task &task, QVector<Task> &into) const;
    qint64 defaultListId() const;
    void rememberList(qint64 listId);
    qint64 tagIdForName(const QString &name, bool create);
    double nextOrder(qint64 listId, qint64 parentId) const;
    void setStatus(const QString &message);
    void applyParsed(Task &task, const ParsedEntry &parsed);
    QStringList listNames() const;
    QStringList locationNames() const;
    QString focusSettingKey() const { return "focus." + m_source.key(); }

    void rebuild();
    bool putManualSort();
    void rebuildSidebar(const ViewBuilder &views);
    void rebuildMyDayTasks(const ViewBuilder &views);
    bool doneWarningDue() const;
    void rebuildTasks(const ViewBuilder &views);
    void rebuildTags(const ViewBuilder &views);
    void rebuildEditor();
    QVariantMap taskRow(const Task &t, const TaskRow &row, const ViewBuilder &views) const;
    void saveUiState();

    Store *m_store;
    CalendarFeed *m_calendar = nullptr;
    RowModel m_sidebar;
    RowModel m_tasks;
    RowModel m_tags;
    RowModel m_places;
    bool m_searchAll = false;
    qint64 m_inheritFrom = 0;  // The Smart List a project view was opened from.
    class QProcess *m_recorder = nullptr;
    qint64 m_recordingTask = 0;
    QString m_recordingFile;
    QVector<qint64> m_history;
    QStringList m_recentSources;  // Newest first.
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
    int m_settingsRevision = 0;
    // My Day's capped sections, expanded for this session only.
    bool m_myDayOverdueExpanded = false;
    bool m_myDaySuggestionsExpanded = false;
    qint64 m_noDetectTask = 0;
    bool m_hasHidden = false;
    QVariantMap m_editor;
    QString m_pendingField;
    bool m_dragCopy = false;
    bool m_recordingShortcut = false;
    // What the last Copy or Cut put on the clipboard, to restore on paste.
    QString m_clipText;
    QVector<Task> m_clipTasks;
    // True while a Cut's own Copy runs, so only the delete is announced.
    bool m_cutting = false;
    QString m_status;
    QTimer m_clock;
    QTimer m_backupTimer;
    void maybeBackup();
    QDate m_lastDay;
};
