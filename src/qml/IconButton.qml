import QtQuick
import QtQuick.Controls

// A flat toolbar button showing one icon, with a tooltip. It joins the tab
// order and answers Space and Return, so a screen reader can reach and press
// it; its accessible name is the tooltip unless one is given.
Item {
    id: root

    property string icon
    property string tip
    // Overrides `tip` for the accessible name, for a button whose tooltip is
    // not what should be read aloud.
    property string accessibleName: ""
    property real size: 17
    property color color: theme.muted
    property bool checked: false
    property color checkedColor: theme.accent
    signal clicked()

    implicitWidth: size + 12
    implicitHeight: size + 12
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName !== "" ? accessibleName : tip
    Accessible.onPressAction: root.clicked()

    Keys.onSpacePressed: root.clicked()
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: mouse.pressed ? theme.mix(theme.background, theme.foreground, 0.14)
             : mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.07) : "transparent"
        // A focus ring only for keyboard focus, so the mouse look is unchanged.
        objectName: root.objectName !== "" ? root.objectName + "Ring" : "iconButtonRing"
        visible: root.activeFocus
        border.width: root.activeFocus ? 2 : 0
        border.color: theme.accent
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
