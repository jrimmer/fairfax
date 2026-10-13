import QtQuick
import QtQuick.Controls

// "Due Date ⌄": picks the list's sort, plus the Separated switch.
Item {
    id: root
    objectName: "sortButton"
    implicitWidth: row.implicitWidth + 12
    implicitHeight: 28

    readonly property string current: {
        for (const choice of backend.sortChoices)
            if (choice.field === backend.sortField) return choice.name
        return "Sort"
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.07) : "transparent"
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        Text { text: root.current; color: theme.muted; font.pixelSize: backend.fontSize - 1 }
        Icon { name: "chevronDown"; size: 10; stroke: 2; color: theme.muted; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: menu.popup(0, root.height) }

    Menu {
        id: menu
        Repeater {
            model: backend.sortChoices
            MenuItem {
                required property var modelData
                text: modelData.name
                checkable: true
                checked: modelData.field === backend.sortField
                onTriggered: backend.setSort(modelData.field)
            }
        }
        MenuSeparator {}
        MenuItem { text: "Separated"; checkable: true; checked: backend.separated; onTriggered: backend.setSeparated(!backend.separated) }
    }
}
