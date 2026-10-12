import QtQuick

// One labelled field pill of Quick Entry: icon (or a colour dot), the current
// value in words (muted when unset), an accent outline while its option row
// is open or it has keyboard focus. Tab reaches it; Return, Space or Down open it.
Rectangle {
    id: pill

    property string icon
    property string text
    property bool set: false
    property bool open: false
    property bool filledIcon: false
    property color tint: theme.foreground
    property color valueColor: theme.foreground
    property color dot: "transparent"
    property bool hasDot: false
    signal clicked()

    activeFocusOnTab: true
    implicitHeight: 28
    implicitWidth: content.implicitWidth + 22
    radius: height / 2
    color: theme.mix(theme.raised, theme.foreground, hover.hovered ? 0.12 : 0.07)
    border.width: 1
    border.color: open || activeFocus ? theme.accent : "transparent"

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 6
        Icon {
            visible: pill.icon !== "" && !pill.hasDot
            anchors.verticalCenter: parent.verticalCenter
            name: pill.icon
            size: 14
            filled: pill.filledIcon
            color: pill.set ? pill.tint : theme.faint
        }
        Rectangle {
            visible: pill.hasDot
            anchors.verticalCenter: parent.verticalCenter
            width: 10; height: 10; radius: 5
            color: pill.dot
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 190)
            elide: Text.ElideRight
            text: pill.text
            color: pill.set ? pill.valueColor : theme.faint
            font.pixelSize: backend.fontSize - 1
        }
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: pill.clicked() }
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space || e.key === Qt.Key_Down) {
            if (e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) return
            pill.clicked()
            e.accepted = true
        }
    }
}
