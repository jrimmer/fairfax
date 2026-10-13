import QtQuick
import QtQuick.Controls

// The always-present search field in the toolbar. It filters the current
// list as you type; Escape clears it and returns to the list.
Item {
    id: root

    // Opened by the user (Ctrl+F, the magnifier); a search that is set some
    // other way, such as from the presets menu, shows the field too.
    property bool opened: false
    readonly property bool expanded: opened || backend.search !== ""
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
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: { root.opened = true; input.forceActiveFocus() } }
    }
    TextInput {
        id: input
        objectName: "searchField"
        visible: root.expanded
        anchors.left: parent.left
        anchors.leftMargin: 30
        anchors.right: parent.right
        anchors.rightMargin: input.text !== "" ? 26 : 8
        anchors.verticalCenter: parent.verticalCenter
        color: theme.foreground
        selectionColor: theme.selection
        font.pixelSize: backend.fontSize - 1
        clip: true
        text: backend.search
        onTextEdited: backend.search = text
        Keys.onEscapePressed: { backend.search = ""; root.opened = false; root.finished() }
        Keys.onReturnPressed: root.finished()
        Keys.onDownPressed: root.finished()
        onActiveFocusChanged: if (!activeFocus && text === "") root.opened = false
        // A search cleared while the field is not being typed in (saving it as a Smart
        // List does) leaves nothing to show: the empty field would otherwise stay open.
        onTextChanged: if (text === "" && !activeFocus) root.opened = false
        Text {
            visible: !input.text
            text: "Search"
            color: theme.faint
            font: input.font
        }
    }
    // The ✕ at the field's end clears the search, as Escape does.
    Icon {
        objectName: "searchClear"
        visible: root.expanded && input.text !== ""
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        name: "close"
        size: 12
        color: theme.muted
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: { backend.search = ""; root.opened = false; root.finished() } }
    }
}
