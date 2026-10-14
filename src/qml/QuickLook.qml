import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Icons.js" as Icons

// Quick Look (Space): a floating preview of the selected task. Up and Down
// keep moving through the list underneath, and the preview follows.
Popup {
    id: root

    readonly property var t: visible ? backend.taskInfo(backend.currentTaskId) : ({})
    function toggle() { if (visible) close(); else if (backend.currentTaskId) open() }

    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: parent.height * 0.12
    width: Math.min(460, parent.width - 40)
    padding: 0
    focus: true
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }

    Connections {
        target: backend
        function onSelectionChanged() { if (root.visible) root.tChanged() }
        function onDataChanged() { if (root.visible) root.tChanged() }
    }

    contentItem: Item {
        implicitHeight: body.implicitHeight + 24
        focus: true
        Accessible.role: Accessible.Dialog
        Accessible.name: "Quick Look"
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Down) { backend.moveCursor(1); e.accepted = true }
            else if (e.key === Qt.Key_Up) { backend.moveCursor(-1); e.accepted = true }
            else if (e.key === Qt.Key_Space || e.key === Qt.Key_Escape) { root.close(); e.accepted = true }
            else if (e.key === Qt.Key_W && (e.modifiers & Qt.ControlModifier)) { root.close(); e.accepted = true }
        }

        Rectangle { width: 4; height: parent.height; radius: 2; color: theme.listColor(root.t.listColor || 0) }

        ColumnLayout {
            id: body
            x: 22
            y: 14
            width: parent.width - 40
            spacing: 8

            Text {
                text: (root.t.listName || "") + (root.t.parent ? "  ›  " + root.t.parent : "")
                color: theme.listColor(root.t.listColor || 0)
                font.pixelSize: backend.fontSize - 2
                font.weight: Font.Bold
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icon { visible: root.t.type === 1; name: "project"; size: 16; color: theme.muted }
                Icon { visible: root.t.type === 2; name: "checklist"; size: 16; color: theme.muted }
                Text {
                    Layout.fillWidth: true
                    text: root.t.title || ""
                    wrapMode: Text.Wrap
                    color: root.t.overdue ? theme.overdue : theme.foreground
                    font.pixelSize: backend.fontSize + 3
                    font.weight: Font.DemiBold
                    font.strikeout: !!root.t.completed
                }
                Icon { visible: !!root.t.starred; name: "starred"; filled: true; size: 16; color: theme.star }
                // Copy the task out in the forms the reference app's Quick Look offers.
                IconButton {
                    id: copyButton
                    objectName: "quickLookCopy"
                    icon: "copy"
                    size: 15
                    tip: "Copy"
                    checked: copyMenu.opened
                    onClicked: copyMenu.popup(copyButton, 0, copyButton.height + 2)
                    Menu {
                        id: copyMenu
                        MenuItem { text: "Copy Title"; onTriggered: backend.copyText(root.t.title || "", "Title copied.") }
                        MenuItem { text: "Copy Notes"; enabled: !!root.t.notes; onTriggered: backend.copyText(root.t.notes || "", "Notes copied.") }
                        MenuItem { text: "Copy as Text"; onTriggered: backend.copyText(backend.taskAsText(root.t.id), "Task copied.") }
                        MenuItem { text: "Copy Link"; enabled: !!root.t.link; onTriggered: backend.copyText(root.t.link || "", "Link copied.") }
                    }
                }
            }
            // The task's picture as a cover banner.
            Image {
                visible: !!root.t.pictureUrl
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 150 : 0
                source: root.t.pictureUrl || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 900
                clip: true
            }
            // Completion bar for projects and checklists.
            Rectangle {
                visible: (root.t.childTotal || 0) > 0
                Layout.fillWidth: true
                implicitHeight: 4
                radius: 2
                color: theme.divider
                Rectangle {
                    width: parent.width * (root.t.childDone || 0) / Math.max(1, root.t.childTotal || 1)
                    height: parent.height
                    radius: 2
                    color: theme.start
                }
            }
            Text {
                visible: !!root.t.notes
                Layout.fillWidth: true
                text: root.t.notes ? backend.renderNotes(root.t.notes) : ""
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                color: theme.foreground
                linkColor: theme.today
                font.pixelSize: backend.fontSize - 1
                onLinkActivated: (link) => {
                    if (link.startsWith("fairfax-task:")) backend.openTaskLink(decodeURIComponent(link.substring(14)), root.t.id || 0)
                    else Qt.openUrlExternally(link)
                }
            }
            Rectangle {
                visible: (root.t.actionType || 0) > 0
                Layout.fillWidth: true
                implicitHeight: 40
                radius: 8
                color: Qt.alpha(theme.today, 0.14)
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    Icon { name: Icons.actionIcons[root.t.actionType || 0]; size: 16; color: theme.today }
                    Text { Layout.fillWidth: true; text: Icons.actionNames[root.t.actionType || 0] + "  " + (root.t.actionValue || ""); color: theme.today; elide: Text.ElideRight }
                }
                MouseArea { objectName: "quickLookAction"; anchors.fill: parent; onClicked: backend.performAction(root.t.id) }
            }
            GridLayout {
                columns: 2
                columnSpacing: 14
                rowSpacing: 4
                Text { visible: !!root.t.start; text: "Start"; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
                Text { visible: !!root.t.start; text: root.t.start || ""; color: theme.start; font.pixelSize: backend.fontSize - 1 }
                Text { visible: !!root.t.due; text: "Due"; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
                Text { visible: !!root.t.due; text: root.t.due || ""; color: root.t.overdue ? theme.overdue : theme.foreground; font.pixelSize: backend.fontSize - 1 }
                Text { visible: !!root.t.repeat; text: "Repeats"; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
                Text { visible: !!root.t.repeat; text: root.t.repeat || ""; color: theme.foreground; font.pixelSize: backend.fontSize - 1 }
                Text { visible: !!root.t.nextAlarm; text: "Alert"; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
                Text { visible: !!root.t.nextAlarm; text: root.t.nextAlarm || ""; color: theme.foreground; font.pixelSize: backend.fontSize - 1 }
                Text { visible: !!root.t.duration; text: "Duration"; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
                Text { visible: !!root.t.duration; text: root.t.duration || ""; color: theme.duration; font.pixelSize: backend.fontSize - 1 }
                Text { visible: !!root.t.hint; text: "Status"; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
                Text { visible: !!root.t.hint; text: root.t.hint || ""; color: theme.today; font.pixelSize: backend.fontSize - 1 }
                Text { visible: (root.t.tags || []).length > 0; text: "Tags"; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
                Text { visible: (root.t.tags || []).length > 0; text: (root.t.tags || []).join(", "); color: theme.foreground; font.pixelSize: backend.fontSize - 1 }
            }
            // Audio notes play in the desktop's player.
            Repeater {
                model: root.t.audio || []
                Rectangle {
                    required property var modelData
                    objectName: "quickLookAudio:" + modelData.id
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: 8
                    color: Qt.alpha(theme.duration, 0.12)
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        Icon { name: "play"; filled: true; size: 12; color: theme.duration }
                        Text { Layout.fillWidth: true; text: modelData.name || "Audio note"; color: theme.foreground; elide: Text.ElideRight; font.pixelSize: backend.fontSize - 1 }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.openAttachment(modelData.id) }
                }
            }
        }
    }
}
