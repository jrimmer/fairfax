import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The main window: the reference app's three panels (lists, tasks, tags) and every
// app-wide shortcut. ⌘ maps to Ctrl because Super belongs to Hyprland; the
// bare-key commands live in TaskPane, where the reference app scopes them to the list.
ApplicationWindow {
    id: win

    width: 1180
    height: 760
    minimumWidth: 640
    minimumHeight: 460
    visible: !startHidden
    title: backend.sourceTitle ? backend.sourceTitle + " — Precrime" : "Precrime"
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.raised
    Material.foreground: theme.foreground
    Material.primary: theme.accent
    font.pixelSize: backend.fontSize

    property alias dragProxy: dragProxy
    property bool sidebarShown: true
    property bool utilityShown: true
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
        }

        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: theme.divider; visible: utility.visible }

        UtilityPanel {
            id: utility
            Layout.fillHeight: true
            Layout.preferredWidth: 228
            visible: win.utilityVisible
        }
    }

    DragProxy { id: dragProxy; parent: Overlay.overlay }
    CommandPopup { id: command; onClosed: win.focusList() }
    QuickEntry { id: quickEntry }
    SettingsWindow { id: settings }

    // Selecting Done with 500+ completed tasks and auto-delete off asks once
    // whether to open the setting, as the reference app does.
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
    ExportDialog { id: exportDialog; owner: "main" }

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
        function onPanelRequested(panel) {
            if (panel === "open") win.openCommand("open")
            else if (panel === "settings") { settings.show(); settings.raise(); settings.requestActivate() }
        }
        function onWindowRequested() { win.show(); win.raise(); win.requestActivate() }
    }
    QuickLook { id: quickLook; onClosed: win.focusList() }
    ShortcutOverlay { id: help; onClosed: win.focusList() }

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
            Text { text: "Precrime can't open its database"; color: theme.foreground; font.bold: true; font.pixelSize: 18 }
            Text { text: storeError; color: theme.muted }
        }
    }

    component AppShortcut: Shortcut { context: Qt.ApplicationShortcut }

    AppShortcut { sequence: "Ctrl+N"; onActivated: backend.newTask(0) }
    AppShortcut { sequence: "Ctrl+Alt+N"; onActivated: backend.newTask(1) }
    AppShortcut { sequence: "Ctrl+Shift+Alt+N"; onActivated: backend.newTask(2) }
    AppShortcut { sequence: "Alt+N"; onActivated: backend.newTask(0, true) }
    AppShortcut { sequence: "Ctrl+Shift+N"; onActivated: { backend.newList(); sidebar.renameSelected() } }
    AppShortcut { sequence: "Ctrl+0"; onActivated: sidebar.selectInbox() }
    AppShortcut { sequence: "Ctrl+1"; onActivated: backend.selectSource("all") }
    AppShortcut { sequence: "Ctrl+2"; onActivated: backend.selectSource("today") }
    AppShortcut { sequence: "Ctrl+3"; onActivated: backend.selectSource("starred") }
    AppShortcut { sequence: "Ctrl+4"; onActivated: backend.selectSource("scheduled") }
    AppShortcut { sequence: "Ctrl+5"; onActivated: backend.selectSource("done") }
    AppShortcut { sequence: "Ctrl+Up"; onActivated: backend.selectAdjacentSource(-1) }
    AppShortcut { sequence: "Ctrl+Down"; onActivated: backend.selectAdjacentSource(1) }
    AppShortcut { sequence: "Ctrl+Shift+Up"; onActivated: backend.moveSelection(-1) }
    AppShortcut { sequence: "Ctrl+Alt+Up"; onActivated: backend.setAllCollapsed(true) }
    AppShortcut { sequence: "Ctrl+Shift+Alt+Up"; onActivated: backend.setAllGroupsCollapsed(true) }
    AppShortcut { sequence: "Ctrl+Shift+Alt+Down"; onActivated: backend.setAllGroupsCollapsed(false) }
    AppShortcut { sequence: "Ctrl+Shift+Down"; onActivated: backend.moveSelection(1) }
    AppShortcut { sequence: "Ctrl+Alt+Down"; onActivated: backend.setAllCollapsed(false) }
    AppShortcut { sequence: "Ctrl+."; onActivated: backend.toggleComplete() }
    AppShortcut { sequence: "Ctrl+Alt+."; onActivated: backend.setCompleted(false) }
    AppShortcut { sequence: "Ctrl+D"; onActivated: backend.duplicateSelection() }
    AppShortcut { sequence: "Ctrl+Backspace"; onActivated: backend.deleteSelection() }
    AppShortcut { sequence: "Ctrl+K"; onActivated: backend.showScheduled = !backend.showScheduled }
    AppShortcut { sequence: "Ctrl+U"; onActivated: backend.showPaused = !backend.showPaused }
    AppShortcut {
        sequence: "Ctrl+Shift+K"
        onActivated: { const on = !(backend.showScheduled && backend.showPaused); backend.showScheduled = on; backend.showPaused = on }
    }
    AppShortcut { sequence: "Ctrl+O"; onActivated: backend.hideCompleted = !backend.hideCompleted }
    AppShortcut { sequence: "Ctrl+F"; onActivated: pane.focusSearch() }
    AppShortcut { sequence: "Ctrl+Shift+O"; onActivated: win.openCommand("open") }
    AppShortcut { sequence: "Ctrl+Z"; onActivated: backend.undo() }
    AppShortcut { sequence: "Ctrl+Shift+Z"; onActivated: backend.redo() }
    AppShortcut { sequence: "Ctrl+="; onActivated: backend.textSize = backend.textSize + 1 }
    AppShortcut { sequence: "Ctrl+-"; onActivated: backend.textSize = backend.textSize - 1 }
    AppShortcut { sequence: "Ctrl+Alt+Left"; onActivated: win.sidebarShown = !win.sidebarVisible }
    AppShortcut { sequence: "Ctrl+Alt+Right"; onActivated: win.utilityShown = !win.utilityVisible }
    AppShortcut { sequence: "Ctrl+Alt+J"; onActivated: pane.toggleProjectView() }
    AppShortcut { sequence: "Ctrl+Return"; onActivated: { backend.endEdit(); win.focusList() } }
    AppShortcut { sequence: "Ctrl+Alt+0"; onActivated: backend.setPriority(0) }
    AppShortcut { sequence: "Ctrl+Alt+1"; onActivated: backend.setPriority(1) }
    AppShortcut { sequence: "Ctrl+Alt+2"; onActivated: backend.setPriority(2) }
    AppShortcut { sequence: "Ctrl+Alt+3"; onActivated: backend.setPriority(3) }
    AppShortcut { sequence: "Ctrl+Alt+S"; onActivated: backend.setStarred(-1) }
    AppShortcut { sequence: "Ctrl+Shift+Alt+1"; onActivated: backend.convertSelection(0) }
    AppShortcut { sequence: "Ctrl+Shift+Alt+2"; onActivated: backend.convertSelection(1) }
    AppShortcut { sequence: "Ctrl+Shift+Alt+3"; onActivated: backend.convertSelection(2) }
    AppShortcut { sequence: "Ctrl+L"; onActivated: win.openCommand("move") }
    AppShortcut { sequence: "Ctrl+R"; onActivated: win.openCommand("tag") }
    // The editor quick-jumps: open the editor on the task, at one field.
    AppShortcut { sequence: "Ctrl+E"; onActivated: backend.editField("notes") }
    AppShortcut { sequence: "Ctrl+B"; onActivated: backend.editField("action") }
    AppShortcut { sequence: "Ctrl+G"; onActivated: backend.editField("location") }
    AppShortcut { sequence: "Ctrl+'"; onActivated: backend.editField("alerts") }
    AppShortcut { sequence: "Ctrl+/"; onActivated: backend.editField("dates") }
    AppShortcut { sequence: "Ctrl+;"; onActivated: backend.editField("repeat") }
    AppShortcut { sequence: "Ctrl+J"; onActivated: backend.toggleFocus() }
    AppShortcut { sequence: "Ctrl+Alt+R"; onActivated: backend.untagSelection() }
    // Ctrl+T already starts tasks today, so tags take Alt with it.
    AppShortcut { sequence: "Ctrl+Alt+T"; onActivated: win.newTag(false) }
    AppShortcut { sequence: "Ctrl+Shift+Alt+T"; onActivated: win.newTag(true) }
    AppShortcut { sequence: "Ctrl+Alt+P"; onActivated: backend.newFromSelection(1) }
    AppShortcut { sequence: "Ctrl+Alt+C"; onActivated: backend.newFromSelection(2) }
    AppShortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
    AppShortcut { sequence: "Ctrl+Shift+2"; onActivated: quickEntry.summon() }
    AppShortcut { sequence: "Shift+F5"; onActivated: backend.showLastSelected() }
    AppShortcut { sequence: "Ctrl+Alt+F"; onActivated: backend.toggleSearchScope() }
    AppShortcut { sequence: "Ctrl+Alt+W"; onActivated: backend.requestNewWindow() }
    AppShortcut { sequence: "Ctrl+P"; onActivated: filePicker.save("Save this list as a PDF", (backend.sourceTitle || "Tasks") + ".pdf", "print-pdf") }
    // the reference app's Ctrl+Cmd+F; Ctrl+Alt+F is taken by the search scope.
    AppShortcut { sequence: "F11"; onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    AppShortcut {
        sequence: "Ctrl+S"
        onActivated: {
            if (syncHub) syncHub.syncAll()
        }
    }
    AppShortcut { sequence: "Ctrl+Shift+Alt+F"; onActivated: pane.openSearchBuilder() }
    AppShortcut { sequence: "Ctrl+,"; onActivated: { settings.show(); settings.raise() } }
    AppShortcut { sequence: "Ctrl+Shift+E"; onActivated: exportDialog.show() }
}
