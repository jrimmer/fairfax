import QtQuick
import QtQuick.Controls

// The priority bars in the editor's title row; click to choose a level.
Item {
    id: root
    property int value: 0
    signal picked(int priority)
    implicitWidth: 18
    implicitHeight: 18

    Column {
        anchors.centerIn: parent
        spacing: 2.5
        Repeater {
            model: 3
            Rectangle {
                required property int index
                width: 14
                height: 2
                radius: 1
                color: root.value === 3 ? theme.priorityHigh : root.value === 2 ? theme.priorityMedium
                     : root.value === 1 ? theme.priorityLow : theme.faint
                opacity: root.value === 0 ? 0.6 : root.value === 1 && index > 0 ? 0.25 : root.value === 2 && index > 1 ? 0.25 : 1
            }
        }
    }
    MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: menu.popup() }
    Menu {
        id: menu
        Repeater {
            model: ["None", "Low", "Medium", "High"]
            MenuItem {
                required property int index
                required property string modelData
                text: modelData
                checkable: true
                checked: root.value === index
                onTriggered: root.picked(index)
            }
        }
    }
}
