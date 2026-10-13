import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, on `?`. the reference app's ⌘ is Ctrl here, since Super belongs to
// Hyprland; the bare keys work while the task list has focus.
Popup {
    id: root
    function toggle() { if (visible) close(); else open() }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(920, parent.width - 40)
    height: Math.min(640, parent.height - 40)
    modal: true
    focus: true
    padding: 22
    background: Rectangle { color: theme.raised; radius: 14; border.color: theme.divider }

    readonly property var groups: [
        { title: "Task list (bare keys)", keys: [
            ["↑ ↓", "Move selection (Shift extends)"], ["Return", "Edit task"], ["Space", "Quick Look"],
            ["t / y", "Due today / tomorrow"], ["Ctrl+T", "Start today"], ["d / k", "Defer / Schedule…"],
            ["e / x", "Due time / Duration…"], ["] [", "Due date ±1 day (Shift: week)"], [". ,", "Start date ±1 day (Shift: week)"],
            ["Alt+[ / Alt+,", "Remove due / start date"], ["0–3", "Priority none … high"], ["= / -", "Raise / lower priority"],
            ["s / Ctrl+Alt+S", "Star / toggle star"], ["l / Alt+L", "Tag… / Untag…"], ["m", "Move to list…"], ["c", "Collapse project"],
            ["p / u", "Perform action / open links"], ["j / f / /", "Jump to list / find task / find tag"],
            ["Delete", "Delete"], ["?", "This overlay"], ["q", "Quit"] ] },
        { title: "New & edit", keys: [
            ["Ctrl+N", "New task"], ["Ctrl+Alt+N", "New project"], ["Ctrl+Shift+Alt+N", "New checklist"],
            ["Alt+N", "New sub-task"], ["Ctrl+Shift+N", "New list"], ["Ctrl+.", "Complete / reopen"],
            ["Ctrl+D", "Duplicate"], ["Ctrl+Return / Esc", "Finish editing"], ["Ctrl+Alt+0–3", "Priority while editing"],
            ["Ctrl+Alt+S", "Toggle star while editing"], ["Ctrl+E B G ' / ;", "Editor: notes, action, location, alerts, dates, repeat"], ["Ctrl+J", "Focus Filter"], ["Ctrl+Alt+P / C", "Project / checklist from selection"], ["Ctrl+Alt+T", "New tag (+Shift: group)"], ["Ctrl+L / Ctrl+R", "Move… / Tag…"],
            ["Ctrl+Shift+Alt+1–3", "Convert to task / project / checklist"],
            ["Ctrl+Shift+↑ ↓", "Move task up / down"], ["Ctrl+Alt+↑ ↓", "Collapse / expand all projects"], ["Ctrl+Shift+Alt+↑ ↓", "Collapse / expand all list groups"], ["Ctrl+Z / Ctrl+Shift+Z", "Undo / redo"] ] },
        { title: "View", keys: [
            ["Ctrl+1–5", "All, Today, Starred, Scheduled, Done"], ["Ctrl+0", "Inbox"], ["Ctrl+↑ ↓", "Previous / next list"],
            ["Ctrl+F", "Search"], ["Ctrl+Shift+O", "Quick Open"], ["Ctrl+K", "Show scheduled"], ["Ctrl+U", "Show paused"],
            ["Ctrl+Shift+K", "Show scheduled & paused"], ["Ctrl+O", "Hide completed"], ["Ctrl+Alt+J", "Project view"],
            ["Ctrl+Alt+← →", "Lists / Tags panels"], ["Ctrl+= / Ctrl+-", "Text size"], ["Ctrl+C / X / V", "Copy / cut / paste tasks"], ["Shift+F5", "Previous task"], ["Ctrl+Alt+F", "Search scope"], ["Ctrl+Alt+W", "New window"], ["F11", "Full screen"], ["Ctrl+P", "Save as PDF"], ["Ctrl+S", "Sync now"], ["Ctrl+,", "Settings"],
            ["Ctrl+Shift+2", "Quick Entry"], ["Ctrl+Shift+E", "Export tasks"], ["Ctrl+Q", "Quit"] ] },
    ]

    contentItem: Flickable {
        contentHeight: grid.implicitHeight
        clip: true
        focus: true
        Keys.onPressed: (e) => { root.close(); e.accepted = true }
        GridLayout {
            id: grid
            width: parent.width
            columns: width > 760 ? 3 : 1
            columnSpacing: 28
            rowSpacing: 18
            Repeater {
                model: root.groups
                ColumnLayout {
                    required property var modelData
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: modelData.title.toUpperCase(); color: theme.accent; font.pixelSize: backend.fontSize - 3; font.weight: Font.Bold; font.letterSpacing: 0.6; bottomPadding: 4 }
                    Repeater {
                        model: modelData.keys
                        RowLayout {
                            required property var modelData
                            spacing: 10
                            Text { Layout.preferredWidth: 130; text: modelData[0]; color: theme.foreground; font.family: theme.monoFont; font.pixelSize: backend.fontSize - 3 }
                            Text { Layout.fillWidth: true; text: modelData[1]; color: theme.muted; font.pixelSize: backend.fontSize - 2; wrapMode: Text.Wrap }
                        }
                    }
                }
            }
        }
    }
}
