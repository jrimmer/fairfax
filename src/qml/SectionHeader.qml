import QtQuick

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
        x: root.margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        text: (root.r.title || "").toUpperCase()
        color: root.r.tone === "overdue" ? theme.overdue
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
        color: root.r.tone === "overdue" ? theme.overdue : theme.faint
        font.pixelSize: backend.fontSize - 2
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onClicked: (m) => backend.toggleSection(root.r.sectionKey, m.modifiers & Qt.ControlModifier)
    }
}
