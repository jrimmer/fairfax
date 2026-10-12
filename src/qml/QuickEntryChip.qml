import QtQuick

// A choice in Quick Entry's option row, or one segment of its type switch.
// `current` marks the keyboard position inside the row; `on` the chosen value.
Rectangle {
    id: chip

    property string text
    property string digit
    property bool on: false
    property bool current: false
    property bool focusable: false
    property color tint: theme.accent
    property color dot: "transparent"
    property bool hasDot: false
    signal clicked()

    activeFocusOnTab: focusable
    implicitHeight: 26
    implicitWidth: row.implicitWidth + 18
    radius: height / 2
    color: on ? Qt.alpha(tint, 0.22) : theme.mix(theme.raised, theme.foreground, hover.hovered ? 0.12 : 0.07)
    border.width: 1
    border.color: current || activeFocus ? theme.accent : (on ? Qt.alpha(tint, 0.6) : "transparent")

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Text {
            visible: chip.digit !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: chip.digit
            color: theme.faint
            font.pixelSize: backend.fontSize - 3
        }
        Rectangle {
            visible: chip.hasDot
            anchors.verticalCenter: parent.verticalCenter
            width: 9; height: 9; radius: 4.5
            color: chip.dot
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            color: chip.on ? theme.foreground : theme.muted
            font.pixelSize: backend.fontSize - 1
            font.weight: chip.on ? Font.DemiBold : Font.Normal
        }
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: chip.clicked() }
    Keys.onPressed: (e) => {
        if (chip.focusable && (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space)) {
            if (e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) return
            chip.clicked()
            e.accepted = true
        }
    }
}
