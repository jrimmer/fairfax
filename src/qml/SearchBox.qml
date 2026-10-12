import QtQuick
import QtQuick.Controls

// The always-present search field in the toolbar. It filters the current
// list as you type; Escape clears it and returns to the list.
Item {
    id: root

    property bool expanded: backend.search !== ""
    property alias field: input
    signal finished()

    implicitWidth: expanded ? 240 : 30
    implicitHeight: 30
    Behavior on implicitWidth { NumberAnimation { duration: 120 } }

    Rectangle {
        anchors.fill: parent
        radius: 7
        color: root.expanded ? theme.mix(theme.background, theme.foreground, 0.06) : "transparent"
        border.color: input.activeFocus ? theme.accent : "transparent"
    }
    Icon {
        x: 7
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: 16
        color: theme.muted
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: { root.expanded = true; input.forceActiveFocus() } }
    }
    TextInput {
        id: input
        objectName: "searchField"
        visible: root.expanded
        anchors.left: parent.left
        anchors.leftMargin: 30
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        color: theme.foreground
        selectionColor: theme.selection
        font.pixelSize: backend.fontSize - 1
        clip: true
        text: backend.search
        onTextEdited: backend.search = text
        Keys.onEscapePressed: { backend.search = ""; root.expanded = false; root.finished() }
        Keys.onReturnPressed: root.finished()
        Keys.onDownPressed: root.finished()
        onActiveFocusChanged: if (!activeFocus && text === "") root.expanded = false
        Text {
            visible: !input.text
            text: "Search"
            color: theme.faint
            font: input.font
        }
    }
}
