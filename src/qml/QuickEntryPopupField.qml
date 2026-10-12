import QtQuick
import QtQuick.Controls.Basic as Plain

// A small text field for Quick Entry's option row. The type's name contains
// "Popup" on purpose: the application-wide Escape filter leaves such items
// alone, so Escape here closes only the option row, not the window.
Plain.TextField {
    id: field

    signal escaped()

    implicitHeight: 26
    implicitWidth: 150
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    color: theme.foreground
    placeholderTextColor: theme.faint
    font.pixelSize: backend.fontSize - 1
    selectByMouse: true
    background: Rectangle {
        radius: height / 2
        color: theme.background
        border.color: field.activeFocus ? theme.accent : theme.divider
    }
    Keys.onEscapePressed: (e) => { e.accepted = true; field.escaped() }
}
