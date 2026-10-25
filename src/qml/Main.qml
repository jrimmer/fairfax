import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The main window: the three panels (lists, tasks, tags) and every
// app-wide shortcut. ⌘ maps to Ctrl because Super belongs to Hyprland; the
// bare-key commands live in TaskPane, where they are scoped to the list.
ApplicationWindow {
    id: win

    width: 1180
    height: 760
    minimumWidth: 640
    minimumHeight: 460
    visible: !startHidden
    title: backend.sourceTitle ? backend.sourceTitle + " — Fairfax" : "Fairfax"
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.raised
    Material.foreground: theme.foreground
    Material.primary: theme.accent
    font.pixelSize: backend.fontSize

    property alias dragProxy: dragProxy
    // Whether the Lists and Tags panels are shown is remembered across launches.
    property bool sidebarShown: backend.settingFlag("window.sidebarShown", true)
    property bool utilityShown: backend.settingFlag("window.utilityShown", true)
    onSidebarShownChanged: backend.setSetting("window.sidebarShown", sidebarShown)
    onUtilityShownChanged: backend.setSetting("window.utilityShown", utilityShown)
    // The lists panel tucks away in a narrow window and comes back with room,
    // unless it was hidden by hand.
    readonly property bool sidebarVisible: sidebarShown && width >= 820
    readonly property bool utilityVisible: utilityShown && width >= 1040

    function openCommand(kind) { command.openFor(kind) }
    function newTag(group) {
        if (!win.utilityVisible) win.utilityShown = true
        utility.tab = 0
        utility.promptName(group ? "newGroup" : "newTag", 0, "", 0)
    }
    // Popups restore focus to whatever they found when they close, which can
    // be the window itself; handing focus back to the list afterwards keeps
    // the bare-key commands working.
    function focusList() { Qt.callLater(pane.focusList) }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 240
            visible: win.sidebarVisible
            onHideRequested: win.sidebarShown = false
            onSettingsRequested: { settings.show(); settings.raise() }
            // A new smart list starts as a search over everything in the
            // search builder, saved from there.
            onNewSmartListRequested: { backend.selectSource("all"); pane.openSearchBuilder() }
            onEditSmartListRequested: pane.editSmartQuery()
            onDoneWarningRequested: { settings.pane = 2; settings.show(); settings.raise() }
        }

        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: theme.divider; visible: sidebar.visible }

        TaskPane {
            id: pane
            Layout.fillWidth: true
            Layout.fillHeight: true
            sidebarVisible: win.sidebarVisible
            utilityVisible: win.utilityVisible
            onToggleSidebar: win.sidebarShown = !win.sidebarVisible
            onToggleUtility: win.utilityShown = !win.utilityVisible
            onCommand: (kind) => win.openCommand(kind)
            onQuickLook: quickLook.toggle()
            onTagSearch: { if (!win.utilityVisible) win.utilityShown = true; utility.focusSearch() }
            onHelp: help.toggle()
            onProposalsRequested: proposalSheet.openSheet()
        }

        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: theme.divider; visible: utility.visible }

        UtilityPanel {
            id: utility
            Layout.fillHeight: true
            Layout.preferredWidth: 228
            visible: win.utilityVisible
        }
    }

    DragProxy { id: dragProxy; objectName: "dragProxy"; parent: Overlay.overlay }
    CommandPopup { id: command; objectName: "commandPopup"; onClosed: win.focusList() }
    QuickEntry { id: quickEntry }
    SettingsWindow { id: settings }

    // Announcements a screen reader cannot see otherwise: completions, moves,
    // deletes, undo and redo, copy and paste. Sync and mail failures are
    // assertive, since they need attention now.
    Connections {
        target: backend
        function onAnnounce(message) { win.contentItem.Accessible.announce(message, Accessible.Polite) }
        function onAnnounceAssertive(message) { win.contentItem.Accessible.announce(message, Accessible.Assertive) }
    }

    // Selecting Done with 500+ completed tasks and auto-delete off asks once
    // whether to open the setting.
    Connections {
        target: backend
        function onDoneWarningRequested() { if (win.active) doneWarning.open() }
    }
    ThemedDialog {
        id: doneWarning
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(440, parent.width - 40)
        title: "Growing number of completed tasks"
        Label {
            text: "Done holds " + backend.completedCount() + " completed tasks.\nShould they be deleted automatically after a while?\nNo leaves them as they are."
        }
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: { settings.pane = 2; settings.show(); settings.raise() }
        onClosed: win.focusList()
    }
    ExportDialog { id: exportDialog; objectName: "exportDialog"; owner: "main" }

    // The once-a-day offer to plan, shown the first time the window appears
    // that day. Both answers mark the day planned (backend.myDayPromptAnswered),
    // so it is asked once; Return opens My Day, Esc declines. With the window
    // starting hidden in the tray nothing pops up until it is shown.
    ThemedDialog {
        id: myDayPrompt
        objectName: "myDayPrompt"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(420, parent.width - 40)
        title: "Plan your day?"
        Label {
            text: "My Day gathers what is overdue, what is already due today and what suits the day.\nOpen My Day to plan it, or leave it for now."
            wrapMode: Text.Wrap
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        // Return opens My Day and Escape declines, whatever activation the
        // platform gives a dialog button. Enabled only while the prompt is up.
        Shortcut { sequences: ["Return", "Enter"]; enabled: myDayPrompt.visible; onActivated: myDayPrompt.accept() }
        Shortcut { sequences: ["Escape"]; enabled: myDayPrompt.visible; onActivated: myDayPrompt.reject() }
        Component.onCompleted: {
            standardButton(Dialog.Ok).text = "Open My Day"
            standardButton(Dialog.Cancel).text = "Not today"
        }
        onAccepted: backend.myDayPromptAnswered(true)
        onRejected: backend.myDayPromptAnswered(false)
        onClosed: win.focusList()
    }
    // The first show of the day is a plain launch, where visible is already
    // true and no visibleChanged fires; onVisibleChanged covers a window that
    // starts in the tray and is shown later, and onCompleted the normal case.
    Component.onCompleted: Qt.callLater(win.maybePlanMyDay)
    // When the day rolls over while the app stays open, the next time the
    // window is shown (or the minute timer ticks with it visible) asks again.
    function maybePlanMyDay() {
        if (!win.visible || startHidden) return
        if (!backend.myDayPromptDue()) return
        if (myDayPrompt.visible) return
        myDayPrompt.open()
    }
    onVisibleChanged: if (visible) maybePlanMyDay()
    Connections {
        target: backend
        function onClockChanged() { win.maybePlanMyDay() }
    }

    // Files picked for a task's attachments.
    Connections {
        target: filePicker
        function onPicked(path, purpose) {
            if (purpose.startsWith("attach:"))
                backend.dropFiles(Number(purpose.slice(7)), [path])
            else if (purpose.startsWith("event:"))
                backend.addToCalendar(Number(purpose.slice(6)), path)
            else if (purpose === "print-pdf")
                backend.printView(path)
            else if (purpose === "export-selection")
                backend.exportTo(path, path.endsWith(".ics") ? "ics" : "csv", true)
            else if (purpose.startsWith("link:"))
                backend.linkFile(Number(purpose.slice(5)), path)
        }
    }

    Connections {
        target: backend
        function onQuickEntryRequested() { quickEntry.summon() }
        function onQuickEntryTextRequested(text) { quickEntry.summon(); quickEntry.prefill(text) }
        // A triage or priority call finished: show the review sheet so the
        // proposals are walked one by one.
        function onReviewRequested() { proposalSheet.openSheet() }
        function onPanelRequested(panel) {
            if (panel === "open") win.openCommand("open")
            else if (panel === "settings") { settings.show(); settings.raise(); settings.requestActivate() }
        }
        function onWindowRequested() { win.show(); win.raise(); win.requestActivate() }
    }
    QuickLook { id: quickLook; objectName: "quickLook"; onClosed: win.focusList() }
    ProposalSheet { id: proposalSheet }
    ShortcutOverlay { id: help; objectName: "shortcutOverlay"; onClosed: win.focusList() }

    // Errors and confirmations, e.g. "Undo Change Due Date".
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        radius: height / 2
        color: theme.raised
        border.color: theme.divider
        height: toastText.implicitHeight + 14
        width: toastText.implicitWidth + 28
        opacity: 0
        visible: opacity > 0
        Text { id: toastText; anchors.centerIn: parent; color: theme.foreground; font.pixelSize: backend.fontSize - 1 }
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Timer { id: toastTimer; interval: 2200; onTriggered: toast.opacity = 0 }
        Connections {
            target: backend
            function onStatusChanged() {
                if (!backend.statusMessage) return
                toastText.text = backend.statusMessage
                toast.opacity = 1
                toastTimer.restart()
            }
        }
    }

    // A database that won't open is said plainly, not papered over.
    Rectangle {
        anchors.fill: parent
        visible: storeError !== ""
        color: theme.background
        Column {
            anchors.centerIn: parent
            spacing: 8
            Text { text: "Fairfax can't open its database"; color: theme.foreground; font.bold: true; font.pixelSize: 18 }
            Text { text: storeError; color: theme.muted }
        }
    }

    component AppShortcut: Shortcut { context: Qt.ApplicationShortcut }

    AppShortcut { sequences: backend.shortcutMap["task.new"] || []; enabled: !backend.recordingShortcut; onActivated: backend.newTask(0) }
    AppShortcut { sequences: backend.shortcutMap["task.newProject"] || []; enabled: !backend.recordingShortcut; onActivated: backend.newTask(1) }
    AppShortcut { sequences: backend.shortcutMap["task.newChecklist"] || []; enabled: !backend.recordingShortcut; onActivated: backend.newTask(2) }
    AppShortcut { sequences: backend.shortcutMap["task.newSubtask"] || []; enabled: !backend.recordingShortcut; onActivated: backend.newTask(0, true) }
    AppShortcut { sequences: backend.shortcutMap["task.newList"] || []; enabled: !backend.recordingShortcut; onActivated: { backend.newList(); sidebar.renameSelected() } }
    AppShortcut { sequences: backend.shortcutMap["view.inbox"] || []; enabled: !backend.recordingShortcut; onActivated: sidebar.selectInbox() }
    AppShortcut { sequences: backend.shortcutMap["view.myDay"] || []; enabled: !backend.recordingShortcut && backend.myDayEnabled; onActivated: backend.selectSource("myday") }
    AppShortcut { sequences: backend.shortcutMap["view.all"] || []; enabled: !backend.recordingShortcut; onActivated: backend.selectSource("all") }
    AppShortcut { sequences: backend.shortcutMap["view.today"] || []; enabled: !backend.recordingShortcut; onActivated: backend.selectSource("today") }
    AppShortcut { sequences: backend.shortcutMap["view.starred"] || []; enabled: !backend.recordingShortcut; onActivated: backend.selectSource("starred") }
    AppShortcut { sequences: backend.shortcutMap["view.scheduled"] || []; enabled: !backend.recordingShortcut; onActivated: backend.selectSource("scheduled") }
    AppShortcut { sequences: backend.shortcutMap["view.done"] || []; enabled: !backend.recordingShortcut; onActivated: backend.selectSource("done") }
    AppShortcut { sequences: backend.shortcutMap["view.previousList"] || []; enabled: !backend.recordingShortcut; onActivated: backend.selectAdjacentSource(-1) }
    AppShortcut { sequences: backend.shortcutMap["view.nextList"] || []; enabled: !backend.recordingShortcut; onActivated: backend.selectAdjacentSource(1) }
    AppShortcut { sequences: backend.shortcutMap["task.moveUp"] || []; enabled: !backend.recordingShortcut; onActivated: backend.moveSelection(-1) }
    AppShortcut { sequences: backend.shortcutMap["view.collapseProjects"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setAllCollapsed(true) }
    AppShortcut { sequences: backend.shortcutMap["view.collapseGroups"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setAllGroupsCollapsed(true) }
    AppShortcut { sequences: backend.shortcutMap["view.expandGroups"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setAllGroupsCollapsed(false) }
    AppShortcut { sequences: backend.shortcutMap["task.moveDown"] || []; enabled: !backend.recordingShortcut; onActivated: backend.moveSelection(1) }
    AppShortcut { sequences: backend.shortcutMap["view.expandProjects"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setAllCollapsed(false) }
    AppShortcut { sequences: backend.shortcutMap["task.complete"] || []; enabled: !backend.recordingShortcut; onActivated: backend.toggleComplete() }
    AppShortcut { sequences: backend.shortcutMap["task.reopen"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setCompleted(false) }
    AppShortcut { sequences: backend.shortcutMap["task.duplicate"] || []; enabled: !backend.recordingShortcut; onActivated: backend.duplicateSelection() }
    AppShortcut { sequences: backend.shortcutMap["task.delete"] || []; enabled: !backend.recordingShortcut; onActivated: backend.deleteSelection() }
    AppShortcut { sequences: backend.shortcutMap["view.showScheduled"] || []; enabled: !backend.recordingShortcut; onActivated: backend.showScheduled = !backend.showScheduled }
    AppShortcut { sequences: backend.shortcutMap["view.showPaused"] || []; enabled: !backend.recordingShortcut; onActivated: backend.showPaused = !backend.showPaused }
    AppShortcut {
        sequences: backend.shortcutMap["view.showScheduledAndPaused"] || []; enabled: !backend.recordingShortcut
        onActivated: { const on = !(backend.showScheduled && backend.showPaused); backend.showScheduled = on; backend.showPaused = on }
    }
    AppShortcut { sequences: backend.shortcutMap["view.hideCompleted"] || []; enabled: !backend.recordingShortcut; onActivated: backend.hideCompleted = !backend.hideCompleted }
    AppShortcut { sequences: backend.shortcutMap["view.search"] || []; enabled: !backend.recordingShortcut; onActivated: pane.focusSearch() }
    AppShortcut { sequences: backend.shortcutMap["view.quickOpen"] || []; enabled: !backend.recordingShortcut; onActivated: win.openCommand("open") }
    AppShortcut { sequences: backend.shortcutMap["edit.undo"] || []; enabled: !backend.recordingShortcut; onActivated: backend.undo() }
    AppShortcut { sequences: backend.shortcutMap["edit.redo"] || []; enabled: !backend.recordingShortcut; onActivated: backend.redo() }
    AppShortcut { sequences: backend.shortcutMap["view.textLarger"] || []; enabled: !backend.recordingShortcut; onActivated: backend.textSize = backend.textSize + 1 }
    AppShortcut { sequences: backend.shortcutMap["view.textSmaller"] || []; enabled: !backend.recordingShortcut; onActivated: backend.textSize = backend.textSize - 1 }
    AppShortcut { sequences: backend.shortcutMap["view.listsPanel"] || []; enabled: !backend.recordingShortcut; onActivated: win.sidebarShown = !win.sidebarVisible }
    AppShortcut { sequences: backend.shortcutMap["view.tagsPanel"] || []; enabled: !backend.recordingShortcut; onActivated: win.utilityShown = !win.utilityVisible }
    AppShortcut { sequences: backend.shortcutMap["view.project"] || []; enabled: !backend.recordingShortcut; onActivated: pane.toggleProjectView() }
    AppShortcut { sequences: backend.shortcutMap["edit.finish"] || []; enabled: !backend.recordingShortcut; onActivated: { backend.endEdit(); win.focusList() } }
    AppShortcut { sequences: backend.shortcutMap["edit.priorityNone"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setPriority(0) }
    AppShortcut { sequences: backend.shortcutMap["edit.priorityLow"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setPriority(1) }
    AppShortcut { sequences: backend.shortcutMap["edit.priorityMedium"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setPriority(2) }
    AppShortcut { sequences: backend.shortcutMap["edit.priorityHigh"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setPriority(3) }
    AppShortcut { sequences: backend.shortcutMap["edit.star"] || []; enabled: !backend.recordingShortcut; onActivated: backend.setStarred(-1) }
    AppShortcut { sequences: backend.shortcutMap["task.convertTask"] || []; enabled: !backend.recordingShortcut; onActivated: backend.convertSelection(0) }
    AppShortcut { sequences: backend.shortcutMap["task.convertProject"] || []; enabled: !backend.recordingShortcut; onActivated: backend.convertSelection(1) }
    AppShortcut { sequences: backend.shortcutMap["task.convertChecklist"] || []; enabled: !backend.recordingShortcut; onActivated: backend.convertSelection(2) }
    AppShortcut { sequences: backend.shortcutMap["task.move"] || []; enabled: !backend.recordingShortcut; onActivated: win.openCommand("move") }
    AppShortcut { sequences: backend.shortcutMap["task.tag"] || []; enabled: !backend.recordingShortcut; onActivated: win.openCommand("tag") }
    // The editor quick-jumps: open the editor on the task, at one field.
    AppShortcut { sequences: backend.shortcutMap["edit.notes"] || []; enabled: !backend.recordingShortcut; onActivated: backend.editField("notes") }
    AppShortcut { sequences: backend.shortcutMap["edit.action"] || []; enabled: !backend.recordingShortcut; onActivated: backend.editField("action") }
    AppShortcut { sequences: backend.shortcutMap["edit.location"] || []; enabled: !backend.recordingShortcut; onActivated: backend.editField("location") }
    AppShortcut { sequences: backend.shortcutMap["edit.alerts"] || []; enabled: !backend.recordingShortcut; onActivated: backend.editField("alerts") }
    AppShortcut { sequences: backend.shortcutMap["edit.dates"] || []; enabled: !backend.recordingShortcut; onActivated: backend.editField("dates") }
    AppShortcut { sequences: backend.shortcutMap["edit.repeat"] || []; enabled: !backend.recordingShortcut; onActivated: backend.editField("repeat") }
    AppShortcut { sequences: backend.shortcutMap["view.focusFilter"] || []; enabled: !backend.recordingShortcut; onActivated: backend.toggleFocus() }
    AppShortcut { sequences: backend.shortcutMap["task.untag"] || []; enabled: !backend.recordingShortcut; onActivated: backend.untagSelection() }
    // Ctrl+T already starts tasks today, so tags take Alt with it.
    AppShortcut { sequences: backend.shortcutMap["task.newTag"] || []; enabled: !backend.recordingShortcut; onActivated: win.newTag(false) }
    AppShortcut { sequences: backend.shortcutMap["task.newTagGroup"] || []; enabled: !backend.recordingShortcut; onActivated: win.newTag(true) }
    AppShortcut { sequences: backend.shortcutMap["task.projectFromSelection"] || []; enabled: !backend.recordingShortcut; onActivated: backend.newFromSelection(1) }
    AppShortcut { sequences: backend.shortcutMap["task.checklistFromSelection"] || []; enabled: !backend.recordingShortcut; onActivated: backend.newFromSelection(2) }
    AppShortcut { sequences: backend.shortcutMap["app.quit"] || []; enabled: !backend.recordingShortcut; onActivated: Qt.quit() }
    AppShortcut { sequences: backend.shortcutMap["app.quickEntry"] || []; enabled: !backend.recordingShortcut; onActivated: quickEntry.summon() }
    AppShortcut { sequences: backend.shortcutMap["view.previousTask"] || []; enabled: !backend.recordingShortcut; onActivated: backend.showLastSelected() }
    AppShortcut { sequences: backend.shortcutMap["view.searchScope"] || []; enabled: !backend.recordingShortcut; onActivated: backend.toggleSearchScope() }
    AppShortcut { sequences: backend.shortcutMap["app.newWindow"] || []; enabled: !backend.recordingShortcut; onActivated: backend.requestNewWindow() }
    AppShortcut { sequences: backend.shortcutMap["app.savePdf"] || []; enabled: !backend.recordingShortcut; onActivated: filePicker.save("Save this list as a PDF", (backend.sourceTitle || "Tasks") + ".pdf", "print-pdf") }
    // Full screen; Ctrl+Alt+F is taken by the search scope.
    AppShortcut { sequences: backend.shortcutMap["view.fullScreen"] || []; enabled: !backend.recordingShortcut; onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    AppShortcut {
        sequences: backend.shortcutMap["app.sync"] || []; enabled: !backend.recordingShortcut
        onActivated: {
            if (syncHub) syncHub.syncAll()
        }
    }
    AppShortcut { sequences: backend.shortcutMap["view.searchOptions"] || []; enabled: !backend.recordingShortcut; onActivated: pane.openSearchBuilder() }
    // The plain-English ask, only while the agent is on; with it off the key
    // does nothing at all.
    AppShortcut { sequences: backend.shortcutMap["view.askEnglish"] || []; enabled: !backend.recordingShortcut && backend.agentEnabled; onActivated: pane.openAskSheet() }
    AppShortcut { sequences: backend.shortcutMap["app.settings"] || []; enabled: !backend.recordingShortcut; onActivated: { settings.show(); settings.raise() } }
    AppShortcut { sequences: backend.shortcutMap["app.export"] || []; enabled: !backend.recordingShortcut; onActivated: exportDialog.show() }
}
