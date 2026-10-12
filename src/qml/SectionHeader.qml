import QtQuick

// A section heading in the task list: OVERDUE in red, TODAY, MON 20 JUL …,
// with the duration total and the open count. Click folds the section;
// Ctrl-click folds or unfolds every section.
Item {
    id: root

    property var row
    property real margin: 40

    height: row.isFirst ? 34 : 50

    Text {
        id: title
        x: root.margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        text: (root.row.title || "").toUpperCase()
        color: root.row.tone === "overdue" ? theme.overdue
             : root.row.tone === "list" ? theme.listColor(root.row.color)
             : root.row.tone === "progress" ? theme.today : theme.foreground
        font.pixelSize: backend.fontSize - 1
        font.weight: Font.Bold
        font.letterSpacing: 0.6
    }
    Text {
        anchors.left: title.right
        anchors.leftMargin: 10
        anchors.baseline: title.baseline
        visible: !!root.row.durationTotal
        text: root.row.durationTotal || ""
        color: theme.duration
        font.pixelSize: backend.fontSize - 2
    }
    Icon {
        anchors.left: title.right
        anchors.leftMargin: root.row.durationTotal ? 60 : 8
        anchors.verticalCenter: title.verticalCenter
        visible: root.row.collapsed || hover.containsMouse
        name: root.row.collapsed ? "chevronRight" : "chevronDown"
        size: 10
        stroke: 2
        color: theme.faint
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: root.margin
        anchors.baseline: title.baseline
        text: root.row.count || ""
        color: root.row.tone === "overdue" ? theme.overdue : theme.faint
        font.pixelSize: backend.fontSize - 2
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onClicked: (m) => backend.toggleSection(root.row.sectionKey, m.modifiers & Qt.ControlModifier)
    }
}
