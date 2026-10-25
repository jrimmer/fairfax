import QtQuick

// A quiet text link for the end of a muted line: the line's colour, underlined
// and accented on hover or keyboard focus.
Text {
    id: root

    signal clicked()

    color: hover.hovered || root.activeFocus ? theme.accent : theme.muted
    font.underline: hover.hovered || root.activeFocus
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: root.text
    Accessible.onPressAction: root.clicked()

    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
