import QtQuick
import QtQuick.Controls

// Renames a list or group in place: Return commits, Escape cancels.
TextField {
    id: root

    property bool active: false
    property string initial
    signal committed(string text)
    signal cancelled()

    visible: active
    padding: 2
    leftPadding: 4
    font.pixelSize: backend.fontSize
    color: theme.foreground
    selectByMouse: true
    background: Rectangle { color: theme.background; border.color: theme.accent; radius: 4 }

    onActiveChanged: if (active) { text = initial; forceActiveFocus(); selectAll() }
    Keys.onReturnPressed: committed(text)
    Keys.onEnterPressed: committed(text)
    Keys.onEscapePressed: cancelled()
    onActiveFocusChanged: if (!activeFocus && active) committed(text)
}
