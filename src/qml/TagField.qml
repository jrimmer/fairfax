import QtQuick
import QtQuick.Controls

// The editor's tokenised tag field: chips in the order they were added,
// with autocomplete from existing tags. Return or Tab (or a comma) turns
// what you typed into a tag; Backspace on an empty field removes the last.
Flow {
    id: root

    property int taskId: 0
    property var tags: []
    property var matches: []
    spacing: 5

    Icon { name: "tag"; size: 15; color: theme.faint; height: 22 }

    Repeater {
        model: root.tags
        Rectangle {
            required property var modelData
            height: 22
            width: chip.implicitWidth + 30
            radius: 11
            color: theme.mix(theme.surface, theme.foreground, 0.28)
            Text { id: chip; x: 9; anchors.verticalCenter: parent.verticalCenter; text: modelData.name; color: theme.background; font.pixelSize: backend.fontSize - 2; font.weight: Font.DemiBold }
            Icon {
                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                name: "close"; size: 9; stroke: 2.4; color: theme.background
                MouseArea { anchors.fill: parent; anchors.margins: -3; onClicked: backend.removeTag(root.taskId, modelData.id) }
            }
        }
    }

    TextInput {
        id: input
        width: Math.max(80, contentWidth + 10)
        height: 22
        verticalAlignment: TextInput.AlignVCenter
        color: theme.foreground
        selectionColor: theme.selection
        font.pixelSize: backend.fontSize - 1
        Text { visible: !input.text && !input.activeFocus; text: root.tags.length ? "" : "Tags"; color: theme.faint; font: input.font; anchors.verticalCenter: parent.verticalCenter }

        function accept(name) {
            if (name.trim()) backend.addTag(root.taskId, name.trim())
            text = ""
            popup.close()
        }
        onTextEdited: {
            if (text.endsWith(",")) { accept(text.slice(0, -1)); return }
            const q = text.toLowerCase()
            root.matches = q ? backend.tagNames.filter(n => n.toLowerCase().startsWith(q)).slice(0, 6) : []
            if (root.matches.length) popup.open(); else popup.close()
        }
        Keys.onReturnPressed: accept(root.matches.length && popup.visible ? root.matches[0] : text)
        Keys.onTabPressed: (e) => { if (text) { accept(text); e.accepted = true } else e.accepted = false }
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Backspace && !text && root.tags.length) {
                backend.removeTag(root.taskId, root.tags[root.tags.length - 1].id)
                e.accepted = true
            }
        }
        onActiveFocusChanged: if (!activeFocus && text) accept(text)

        Popup {
            id: popup
            y: input.height + 2
            padding: 4
            background: Rectangle { color: theme.raised; radius: 8; border.color: theme.divider }
            contentItem: Column {
                Repeater {
                    model: root.matches
                    Text {
                        required property string modelData
                        text: modelData
                        color: theme.foreground
                        padding: 5
                        font.pixelSize: backend.fontSize - 1
                        MouseArea { anchors.fill: parent; onClicked: input.accept(modelData) }
                    }
                }
            }
        }
    }
}
