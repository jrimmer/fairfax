#include "shortcuts.h"

#include <QKeyCombination>
#include <QKeySequence>
#include <QVariant>

#include "store.h"

namespace Shortcuts {

namespace {

const QString kListGroup = QStringLiteral("Task list (bare keys)");
const QString kEditGroup = QStringLiteral("New & edit");
const QString kViewGroup = QStringLiteral("View");
const QString kQuickGroup = QStringLiteral("Quick Entry");

QString settingKey(const QString &id) { return QStringLiteral("shortcuts.") + id; }

// Ids that were renamed or folded into another command: what a user saved
// under the old id keeps working. "Show scheduled" (Ctrl+K) became "Hide
// not-started tasks"; "Show scheduled & paused" (Ctrl+Shift+K) became a second
// binding of "Show paused".
QString legacyId(const QString &id) {
    if (id == QLatin1String("view.hideScheduled"))
        return QStringLiteral("view.showScheduled");
    if (id == QLatin1String("view.showPaused"))
        return QStringLiteral("view.showScheduledAndPaused");
    return {};
}

// The default binding list is written as the overlay and the README spell it;
// it is parsed with fromString so a typo is caught by the tests.
QStringList seqs(std::initializer_list<const char *> list) {
    QStringList out;
    for (const char *s : list)
        out << QKeySequence(QString::fromUtf8(s)).toString(QKeySequence::PortableText);
    return out;
}

QList<Command> buildCommands() {
    QList<Command> c;
    auto add = [&](const char *id, const char *group, const char *title, std::initializer_list<const char *> defaults,
                   Context context = Context::App, bool fixed = false) {
        c.append(Command{QString::fromUtf8(id), QString::fromUtf8(group), QString::fromUtf8(title), seqs(defaults), context, fixed});
    };

    // ---- Task list (bare keys)
    add("list.moveUp", "Task list (bare keys)", "Move selection up", {"Up"}, Context::List, true);
    add("list.extendUp", "Task list (bare keys)", "Extend selection up", {"Shift+Up"}, Context::List, true);
    add("list.moveDown", "Task list (bare keys)", "Move selection down", {"Down"}, Context::List, true);
    add("list.extendDown", "Task list (bare keys)", "Extend selection down", {"Shift+Down"}, Context::List, true);
    add("list.edit", "Task list (bare keys)", "Edit task", {"Return", "Enter"}, Context::List, true);
    add("list.clearSelection", "Task list (bare keys)", "Clear selection", {"Escape"}, Context::List, true);
    add("list.quickLook", "Task list (bare keys)", "Quick Look", {"Space"}, Context::List);
    add("list.delete", "Task list (bare keys)", "Delete", {"Delete", "Backspace"}, Context::List);
    add("list.selectAll", "Task list (bare keys)", "Select all", {"Ctrl+A"}, Context::List, true);
    add("list.copy", "Task list (bare keys)", "Copy tasks", {"Ctrl+C"}, Context::List, true);
    add("list.cut", "Task list (bare keys)", "Cut tasks", {"Ctrl+X"}, Context::List, true);
    add("list.paste", "Task list (bare keys)", "Paste tasks", {"Ctrl+V"}, Context::List, true);
    add("list.startToday", "Task list (bare keys)", "Start today", {"Ctrl+T"}, Context::List);
    add("list.clearDueTime", "Task list (bare keys)", "Remove due time", {"Ctrl+Shift+["}, Context::List);
    add("list.dueToday", "Task list (bare keys)", "Due today", {"T"}, Context::List);
    add("list.dueTomorrow", "Task list (bare keys)", "Due tomorrow", {"Y"}, Context::List);
    add("list.star", "Task list (bare keys)", "Star", {"S"}, Context::List);
    add("list.priorityNone", "Task list (bare keys)", "Priority none", {"0"}, Context::List);
    add("list.priorityLow", "Task list (bare keys)", "Priority low", {"1"}, Context::List);
    add("list.priorityMedium", "Task list (bare keys)", "Priority medium", {"2"}, Context::List);
    add("list.priorityHigh", "Task list (bare keys)", "Priority high", {"3"}, Context::List);
    add("list.priorityRaise", "Task list (bare keys)", "Raise priority", {"=", "+", "Shift++"}, Context::List);
    add("list.priorityLower", "Task list (bare keys)", "Lower priority", {"-"}, Context::List);
    add("list.nudgeDueDay", "Task list (bare keys)", "Due date +1 day", {"]"}, Context::List);
    add("list.nudgeDueWeek", "Task list (bare keys)", "Due date +1 week", {"Shift+}"}, Context::List);
    add("list.nudgeDueBackDay", "Task list (bare keys)", "Due date −1 day", {"["}, Context::List);
    add("list.nudgeDueBackWeek", "Task list (bare keys)", "Due date −1 week", {"Shift+{"}, Context::List);
    add("list.clearDue", "Task list (bare keys)", "Remove due date", {"Alt+["}, Context::List);
    add("list.nudgeStartDay", "Task list (bare keys)", "Start date +1 day", {"."}, Context::List);
    add("list.nudgeStartWeek", "Task list (bare keys)", "Start date +1 week", {"Shift+>"}, Context::List);
    add("list.nudgeStartBackDay", "Task list (bare keys)", "Start date −1 day", {","}, Context::List);
    add("list.nudgeStartBackWeek", "Task list (bare keys)", "Start date −1 week", {"Shift+<"}, Context::List);
    add("list.clearStart", "Task list (bare keys)", "Remove start date", {"Alt+,"}, Context::List);
    add("list.collapse", "Task list (bare keys)", "Collapse project", {"C"}, Context::List);
    add("list.performAction", "Task list (bare keys)", "Perform action", {"P"}, Context::List);
    add("list.openLinks", "Task list (bare keys)", "Open links", {"U"}, Context::List);
    add("list.abandon", "Task list (bare keys)", "Abandon (won't do)", {"W"}, Context::List);
    add("list.defer", "Task list (bare keys)", "Defer…", {"D"}, Context::List);
    add("list.schedule", "Task list (bare keys)", "Schedule…", {"K"}, Context::List);
    add("list.time", "Task list (bare keys)", "Due Time…", {"E"}, Context::List);
    add("list.duration", "Task list (bare keys)", "Duration…", {"X"}, Context::List);
    add("list.tag", "Task list (bare keys)", "Tag…", {"L"}, Context::List);
    add("list.untag", "Task list (bare keys)", "Untag…", {"Alt+L"}, Context::List);
    add("list.move", "Task list (bare keys)", "Move to list…", {"M"}, Context::List);
    add("list.jump", "Task list (bare keys)", "Jump to list", {"J"}, Context::List);
    add("list.find", "Task list (bare keys)", "Find task", {"F"}, Context::List);
    add("list.tagSearch", "Task list (bare keys)", "Find tag", {"/"}, Context::List);
    add("list.overlay", "Task list (bare keys)", "All shortcuts", {"Shift+?"}, Context::List);
    add("list.quit", "Task list (bare keys)", "Quit", {"Q"}, Context::List);
    add("list.revealNames", "Task list (bare keys)", "Reveal list names (hold)", {"Alt"}, Context::List, true);

    // ---- My Day (plan mode; the bare keys act only while My Day is open)
    add("myday.add", "My Day", "Add to My Day", {"A"}, Context::List);
    add("myday.block", "My Day", "Block out time", {"B"}, Context::List);
    add("myday.remove", "My Day", "Remove from My Day", {"R"}, Context::List);
    add("myday.hide", "My Day", "Hide for today", {"H"}, Context::List);

    // ---- New & edit
    add("task.new", "New & edit", "New task", {"Ctrl+N"});
    add("task.newProject", "New & edit", "New project", {"Ctrl+Alt+N"});
    add("task.newChecklist", "New & edit", "New checklist", {"Ctrl+Shift+Alt+N"});
    add("task.newSubtask", "New & edit", "New sub-task", {"Alt+N"});
    add("task.newList", "New & edit", "New list", {"Ctrl+Shift+N"});
    add("task.complete", "New & edit", "Complete / reopen", {"Ctrl+."});
    add("task.reopen", "New & edit", "Reopen", {"Ctrl+Alt+."});
    add("task.abandon", "New & edit", "Abandon / reopen", {"Ctrl+Alt+A"});
    add("task.duplicate", "New & edit", "Duplicate", {"Ctrl+D"});
    add("task.delete", "New & edit", "Delete", {"Ctrl+Backspace"});
    add("edit.finish", "New & edit", "Finish editing", {"Ctrl+Return"}, Context::App, true);
    add("edit.priorityNone", "New & edit", "Priority none while editing", {"Ctrl+Alt+0"});
    add("edit.priorityLow", "New & edit", "Priority low while editing", {"Ctrl+Alt+1"});
    add("edit.priorityMedium", "New & edit", "Priority medium while editing", {"Ctrl+Alt+2"});
    add("edit.priorityHigh", "New & edit", "Priority high while editing", {"Ctrl+Alt+3"});
    add("edit.star", "New & edit", "Toggle star while editing", {"Ctrl+Alt+S"});
    add("edit.notes", "New & edit", "Editor: notes", {"Ctrl+E"});
    add("edit.action", "New & edit", "Editor: action", {"Ctrl+B"});
    add("edit.location", "New & edit", "Editor: location", {"Ctrl+G"});
    add("edit.alerts", "New & edit", "Editor: alerts", {"Ctrl+'"});
    add("edit.dates", "New & edit", "Editor: dates", {"Ctrl+/"});
    add("edit.repeat", "New & edit", "Editor: repeat", {"Ctrl+;"});
    add("view.focusFilter", "New & edit", "Focus Filter", {"Ctrl+J"});
    add("task.projectFromSelection", "New & edit", "Project from selection", {"Ctrl+Alt+P"});
    add("task.checklistFromSelection", "New & edit", "Checklist from selection", {"Ctrl+Alt+C"});
    add("task.newTag", "New & edit", "New tag", {"Ctrl+Alt+T"});
    add("task.newTagGroup", "New & edit", "New tag group", {"Ctrl+Shift+Alt+T"});
    add("task.move", "New & edit", "Move…", {"Ctrl+L"});
    add("task.tag", "New & edit", "Tag…", {"Ctrl+R"});
    add("task.untag", "New & edit", "Untag…", {"Ctrl+Alt+R"});
    add("task.convertTask", "New & edit", "Convert to task", {"Ctrl+Shift+Alt+1"});
    add("task.convertProject", "New & edit", "Convert to project", {"Ctrl+Shift+Alt+2"});
    add("task.convertChecklist", "New & edit", "Convert to checklist", {"Ctrl+Shift+Alt+3"});
    add("task.moveUp", "New & edit", "Move task up", {"Ctrl+Shift+Up"});
    add("task.moveDown", "New & edit", "Move task down", {"Ctrl+Shift+Down"});
    add("view.collapseProjects", "New & edit", "Collapse all projects", {"Ctrl+Alt+Up"});
    add("view.expandProjects", "New & edit", "Expand all projects", {"Ctrl+Alt+Down"});
    add("view.collapseGroups", "New & edit", "Collapse all list groups", {"Ctrl+Shift+Alt+Up"});
    add("view.expandGroups", "New & edit", "Expand all list groups", {"Ctrl+Shift+Alt+Down"});
    add("edit.undo", "New & edit", "Undo", {"Ctrl+Z"});
    add("edit.redo", "New & edit", "Redo", {"Ctrl+Shift+Z"});

    // ---- View
    add("view.inbox", "View", "Inbox", {"Ctrl+0"});
    add("view.myDay", "View", "My Day", {"Ctrl+Shift+P"});
    add("view.all", "View", "All", {"Ctrl+1"});
    add("view.today", "View", "Today", {"Ctrl+2"});
    add("view.starred", "View", "Starred", {"Ctrl+3"});
    add("view.scheduled", "View", "Scheduled", {"Ctrl+4"});
    add("view.done", "View", "Done", {"Ctrl+5"});
    add("view.previousList", "View", "Previous list", {"Ctrl+Up"});
    add("view.nextList", "View", "Next list", {"Ctrl+Down"});
    add("view.search", "View", "Search", {"Ctrl+F"});
    add("view.quickOpen", "View", "Quick Open", {"Ctrl+Shift+O"});
    add("view.searchOptions", "View", "Search Options…", {"Ctrl+Shift+Alt+F"});
    // The plain-English ask. It takes Ctrl+Shift+E in the App context; the
    // export panel moves to the free Ctrl+Shift+D so the two never collide.
    // Both are remappable in the View group like the rest.
    add("view.askEnglish", "View", "Ask in plain English…", {"Ctrl+Shift+E"});
    add("view.searchScope", "View", "Search scope", {"Ctrl+Alt+F"});
    add("view.hideScheduled", "View", "Hide not-started tasks", {"Ctrl+K"});
    add("view.showPaused", "View", "Show paused", {"Ctrl+U", "Ctrl+Shift+K"});
    add("view.hideCompleted", "View", "Hide completed", {"Ctrl+O"});
    add("view.project", "View", "Project view", {"Ctrl+Alt+J"});
    add("view.listsPanel", "View", "Lists panel", {"Ctrl+Alt+Left"});
    add("view.tagsPanel", "View", "Tags panel", {"Ctrl+Alt+Right"});
    add("view.textLarger", "View", "Larger text", {"Ctrl+="});
    add("view.textSmaller", "View", "Smaller text", {"Ctrl+-"});
    add("view.previousTask", "View", "Previous task", {"Shift+F5"});
    add("view.fullScreen", "View", "Full screen", {"F11"});
    add("app.newWindow", "View", "New window", {"Ctrl+Alt+W"});
    add("app.savePdf", "View", "Save as PDF", {"Ctrl+P"});
    add("app.sync", "View", "Sync now", {"Ctrl+S"});
    add("app.settings", "View", "Settings", {"Ctrl+,"});
    add("app.quickEntry", "View", "Quick Entry", {"Ctrl+Shift+2"});
    add("app.export", "View", "Export tasks", {"Ctrl+Shift+D"});
    add("app.quit", "View", "Quit", {"Ctrl+Q"});

    // ---- Quick Entry
    add("quickEntry.hide", "Quick Entry", "Hide Quick Entry", {"Ctrl+W"}, Context::QuickEntry);
    add("quickEntry.saveAndOpen", "Quick Entry", "Save and open the task", {"Ctrl+Return"}, Context::QuickEntry);
    add("quickEntry.save", "Quick Entry", "Save and start the next", {"Shift+Return"}, Context::QuickEntry);

    return c;
}

bool overlaps(Context a, Context b) {
    if (a == Context::App || b == Context::App)
        return true;  // Application shortcuts fire in every window.
    return a == b;
}

bool isModifierKey(int key) {
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta
           || key == Qt::Key_AltGr;
}

// The keys a command may never take, because the list, the editor or typing
// depends on them.
const QStringList &reserved() {
    static const QStringList keys{
        QStringLiteral("Up"), QStringLiteral("Shift+Up"), QStringLiteral("Down"), QStringLiteral("Shift+Down"),
        QStringLiteral("Return"), QStringLiteral("Enter"), QStringLiteral("Escape"), QStringLiteral("Tab"),
        QStringLiteral("Ctrl+A"), QStringLiteral("Ctrl+C"), QStringLiteral("Ctrl+X"), QStringLiteral("Ctrl+V"),
        QStringLiteral("Ctrl+Return"), QStringLiteral("Alt"),
    };
    return keys;
}

}  // namespace

const QList<Command> &commands() {
    static const QList<Command> list = buildCommands();
    return list;
}

const QStringList &groups() {
    static const QStringList list = [] {
        QStringList out;
        for (const Command &c : commands())
            if (!out.contains(c.group))
                out << c.group;
        return out;
    }();
    return list;
}

const Command *command(const QString &id) {
    for (const Command &c : commands())
        if (c.id == id)
            return &c;
    return nullptr;
}

QString contextName(Context context) {
    switch (context) {
    case Context::App: return QStringLiteral("App");
    case Context::List: return QStringLiteral("List");
    case Context::QuickEntry: return QStringLiteral("QuickEntry");
    }
    return {};
}

Context contextFromName(const QString &name, bool *ok) {
    if (ok)
        *ok = true;
    if (name == QLatin1String("List"))
        return Context::List;
    if (name == QLatin1String("QuickEntry"))
        return Context::QuickEntry;
    if (name != QLatin1String("App") && ok)
        *ok = false;
    return Context::App;
}

QString sequenceFor(int key, int modifiers) {
    modifiers &= ~Qt::KeypadModifier;
    if (key == 0 || isModifierKey(key))
        return {};
    return QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key))).toString(QKeySequence::PortableText);
}

QString nativeText(const QStringList &sequences) {
    QStringList pretty;
    for (const QString &s : sequences)
        pretty << QKeySequence::fromString(s, QKeySequence::PortableText).toString(QKeySequence::NativeText);
    return pretty.join(QStringLiteral(" / "));
}

QStringList current(const Store &store, const QString &id) {
    const Command *cmd = command(id);
    if (!cmd)
        return {};
    QVariant stored = store.setting(settingKey(id));
    const QString legacy = legacyId(id);
    if ((!stored.isValid() || stored.isNull()) && !legacy.isEmpty()) {
        const QVariant old = store.setting(settingKey(legacy));
        if (old.isValid() && !old.isNull()) {
            if (id == QLatin1String("view.hideScheduled")) {
                stored = old;
            } else {
                // Show paused keeps its own default and takes whatever the
                // user had given the old combined command as its extra binding.
                QStringList out = cmd->defaults.mid(0, 1);
                for (const QKeySequence &s : QKeySequence::listFromString(old.toString(), QKeySequence::PortableText))
                    if (!s.isEmpty() && !out.contains(s.toString(QKeySequence::PortableText)))
                        out << s.toString(QKeySequence::PortableText);
                return out;
            }
        }
    }
    if (!stored.isValid() || stored.isNull())
        return cmd->defaults;  // Never changed.
    QStringList out;
    for (const QKeySequence &s : QKeySequence::listFromString(stored.toString(), QKeySequence::PortableText))
        if (!s.isEmpty())
            out << s.toString(QKeySequence::PortableText);
    return out;
}

void set(Store &store, const QString &id, const QStringList &sequences) {
    store.setSetting(settingKey(id), QKeySequence::listToString(
                                          [&] {
                                              QList<QKeySequence> out;
                                              for (const QString &s : sequences)
                                                  out << QKeySequence::fromString(s, QKeySequence::PortableText);
                                              return out;
                                          }(),
                                          QKeySequence::PortableText));
}

void reset(Store &store, const QString &id) {
    store.removeSetting(settingKey(id));
    const QString legacy = legacyId(id);
    if (!legacy.isEmpty())
        store.removeSetting(settingKey(legacy));
}

void resetAll(Store &store) {
    for (const Command &c : commands())
        reset(store, c.id);
}

QString conflict(const Store &store, const QString &id, const QString &sequence) {
    const Command *self = command(id);
    if (!self || self->fixed || sequence.isEmpty())
        return {};  // A fixed binding has no edit button, so nothing to resolve.
    for (const Command &other : commands()) {
        if (other.id == id || other.fixed)
            continue;
        if (!overlaps(self->context, other.context))
            continue;
        if (current(store, other.id).contains(sequence))
            return other.id;
    }
    return {};
}

QString validate(Context context, const QString &sequence) {
    if (sequence.isEmpty())
        return QStringLiteral("Press a key.");
    if (reserved().contains(sequence)) {
        for (const Command &c : commands())
            if (c.fixed && c.defaults.contains(sequence))
                return QStringLiteral("“%1” is fixed to “%2”.").arg(sequence, c.title);
        return QStringLiteral("“%1” is reserved.").arg(sequence);
    }
    if (context == Context::App || context == Context::QuickEntry) {
        const QKeySequence pressed = QKeySequence::fromString(sequence, QKeySequence::PortableText);
        if (pressed.isEmpty() || pressed.count() != 1)
            return QStringLiteral("Press one key combination.");
        const QKeyCombination combo = pressed[0];
        const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
        const int key = combo.key();
        const bool functionKey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
        if (!(mods & Qt::ControlModifier) && !(mods & Qt::AltModifier) && !functionKey)
            return QStringLiteral("Add Ctrl or Alt, or use a function key (F1–F35).");
    }
    return {};
}

}  // namespace Shortcuts
