import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Archived lists, each with a Restore button.
ThemedDialog {
    id: root

    property var lists: []

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: "Archived Lists"
    width: 340
    standardButtons: Dialog.Close
    onListsChanged: if (lists.length === 0) close()

    contentItem: ColumnLayout {
        spacing: 4
        Repeater {
            model: root.lists
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Rectangle { width: 7; height: 20; radius: 3.5; color: theme.listColor(modelData.color || 0) }
                Label { text: modelData.name; elide: Text.ElideRight; Layout.fillWidth: true }
                Button { text: "Restore"; onClicked: backend.setListFlag(modelData.id, "archived", false) }
            }
        }
    }
}
