import QtQuick
import QtQuick.Controls

// "Due Date ⌄": picks the list's sort, plus the Separated switch.
Item {
    id: root
    objectName: "sortButton"
    implicitWidth: row.implicitWidth + 12
    implicitHeight: 28
    activeFocusOnTab: true

    readonly property string current: {
        for (const choice of backend.sortChoices)
            if (choice.field === backend.sortField) return choice.name
        return "Sort"
    }

    Accessible.role: Accessible.Button
    Accessible.name: "Sort by " + current
    Accessible.onPressAction: menu.popup(0, root.height)

    Keys.onSpacePressed: menu.popup(0, root.height)
    Keys.onReturnPressed: menu.popup(0, root.height)
    Keys.onEnterPressed: menu.popup(0, root.height)

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.07) : "transparent"
        border.width: root.activeFocus ? 2 : 0
        border.color: theme.accent
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        Text { text: root.current; color: theme.muted; font.pixelSize: backend.fontSize - 1 }
        Icon { name: "chevronDown"; size: 10; stroke: 2; color: theme.muted; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: menu.popup(0, root.height) }

    // The choices differ between the Done list, Smart Lists and lists with sections.
    // A Repeater whose model changes after the menu exists puts its new entries after
    // the static ones, so an Instantiator places each one at its own index instead.
    ScrollingMenu {
        id: menu
        MenuSeparator {}
        MenuItem {
            objectName: "sortSeparated"
            text: "Separated"
            checkable: true
            checked: backend.separated
            onTriggered: backend.setSeparated(!backend.separated)
        }
    }
    Instantiator {
        model: backend.sortChoices
        delegate: MenuItem {
            required property var modelData
            objectName: "sortChoice:" + modelData.field
            text: modelData.name
            checkable: true
            checked: modelData.field === backend.sortField
            onTriggered: backend.setSort(modelData.field)
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
