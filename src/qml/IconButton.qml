import QtQuick
import QtQuick.Controls

// A flat toolbar button showing one icon, with a tooltip.
Item {
    id: root

    property string icon
    property string tip
    property real size: 17
    property color color: theme.muted
    property bool checked: false
    property color checkedColor: theme.accent
    signal clicked()

    implicitWidth: size + 12
    implicitHeight: size + 12

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: mouse.pressed ? theme.mix(theme.background, theme.foreground, 0.14)
             : mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.07) : "transparent"
    }
    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: root.size
        color: root.checked ? root.checkedColor : root.color
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
    ToolTip.visible: tip !== "" && mouse.containsMouse
    ToolTip.delay: 600
    ToolTip.text: tip
}
