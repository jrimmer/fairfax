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
    visible: true
    title: backend.sourceTitle ? backend.sourceTitle + " — Precrime" : "Precrime"
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.raised
    Material.foreground: theme.foreground
    Material.primary: theme.accent
    font.pixelSize: backend.fontSize

    property bool sidebarShown: true
    property bool utilityShown: true
    // The lists panel tucks away in a narrow window and comes back with room,
    // unless it was hidden by hand.
    readonly property bool sidebarVisible: sidebarShown && width >= 820
    readonly property bool utilityVisible: utilityShown && width >= 1040

    function openCommand(kind) { command.openFor(kind) }
    function focusList() { pane.focusList() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 240
            visible: win.sidebarVisible
            onHideRequested: win.sidebarShown = false
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

    CommandPopup { id: command; onClosed: win.focusList() }
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
    AppShortcut { sequence: "Ctrl+Alt+Up"; onActivated: backend.moveSelection(-1) }
    AppShortcut { sequence: "Ctrl+Alt+Down"; onActivated: backend.moveSelection(1) }
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
    AppShortcut { sequence: "Ctrl+/"; onActivated: win.openCommand("defer") }
    AppShortcut { sequence: "Ctrl+;"; onActivated: win.openCommand("repeat") }
    AppShortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
}
