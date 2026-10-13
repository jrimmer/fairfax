import QtQuick
import QtQuick.Controls

// A section heading in the task list: OVERDUE in red, TODAY, MON 20 JUL …,
// with the duration total and the open count. Click folds the section;
// Ctrl-click folds or unfolds every section.
Item {
    id: root

    property var row
    // The last real row, so bindings stay valid while the delegate is torn
    // down and its model entry is already gone.
    property var r: row || ({})
    onRowChanged: if (row) r = row
    property real margin: 40

    height: r.isFirst ? 34 : 50

    Text {
        id: title
        objectName: "sectionTitle"
        x: root.margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        visible: !root.renaming
        text: (root.r.title || "").toUpperCase()
        color: root.r.tone === "overdue" ? (backend.overdueInRed ? theme.overdue : theme.foreground)
             : root.r.tone === "list" ? theme.listColor(root.r.color)
             : root.r.tone === "progress" ? theme.today : theme.foreground
        font.pixelSize: backend.fontSize - 1
        font.weight: Font.Bold
        font.letterSpacing: 0.6
    }
    Text {
        anchors.left: title.right
        anchors.leftMargin: 10
        anchors.baseline: title.baseline
        objectName: "sectionDuration"
        visible: !!root.r.durationTotal
        text: root.r.durationTotal || ""
        color: theme.duration
        font.pixelSize: backend.fontSize - 2
    }
    Icon {
        anchors.left: title.right
        anchors.leftMargin: root.r.durationTotal ? 60 : 8
        anchors.verticalCenter: title.verticalCenter
        visible: root.r.collapsed || hover.containsMouse
        name: root.r.collapsed ? "chevronRight" : "chevronDown"
        size: 10
        stroke: 2
        color: theme.faint
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: root.margin
        anchors.baseline: title.baseline
        text: root.r.count || ""
        color: root.r.tone === "overdue" && backend.overdueInRed ? theme.overdue : theme.faint
        font.pixelSize: backend.fontSize - 2
    }
    readonly property bool isListSection: (root.r.sectionId || 0) > 0
    property bool renaming: false

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (m) => {
            if (m.button === Qt.RightButton) { if (root.isListSection) sectionMenu.popup(); return }
            backend.toggleSection(root.r.sectionKey, m.modifiers & Qt.ControlModifier)
        }
        onDoubleClicked: if (root.isListSection) root.renaming = true
    }

    // Tasks dropped on a list section's heading move into it.
    DropArea {
        anchors.fill: parent
        keys: ["precrime-tasks"]
        enabled: (root.r.sectionId || -1) >= 0
        readonly property var proxy: ApplicationWindow.window ? ApplicationWindow.window.dragProxy : null
        onEntered: if (proxy) proxy.hint = root.r.sectionId > 0 ? "Move to " + root.r.title : "Remove from Section"
        onExited: if (proxy) proxy.hint = ""
        onDropped: backend.dropOnSection(root.r.sectionKey)
        Rectangle {
            x: root.margin - 8
            width: parent.width - 2 * root.margin + 16
            anchors.bottom: parent.bottom
            height: 30
            radius: 6
            color: "transparent"
            border.width: 2
            border.color: theme.accent
            visible: parent.containsDrag
        }
    }

    InlineEditor {
        x: root.margin - 4
        width: 260
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        active: root.renaming
        initial: root.r.title || ""
        onCommitted: (text) => { backend.renameSection(root.r.sectionId, text); root.renaming = false }
        onCancelled: root.renaming = false
    }

    Menu {
        id: sectionMenu
        MenuItem { text: "Rename Section…"; onTriggered: root.renaming = true }
        MenuItem { text: "Move Section Up"; onTriggered: backend.moveSection(root.r.sectionId, -1) }
        MenuItem { text: "Move Section Down"; onTriggered: backend.moveSection(root.r.sectionId, 1) }
        MenuSeparator {}
        MenuItem { text: "Delete Section"; onTriggered: backend.deleteSection(root.r.sectionId) }
    }
}
