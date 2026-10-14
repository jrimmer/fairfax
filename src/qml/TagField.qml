import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The editor's tokenised tag field, boxed as in Quick Entry: chips in the
// order they were added, then where you type, with autocomplete from
// existing tags. Return or Tab (or a comma) turns what you typed into a tag;
// Backspace on an empty field removes the last.
RowLayout {
    id: root

    property int taskId: 0
    property var tags: []
    property var matches: []
    // What a screen reader calls the place you type a tag.
    property string accessibleName: "Tags"
    // Tag matching ignores case and accents, so "cafe" finds "Café".
    function fold(s) { return String(s).normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase() }
    spacing: 10

    Icon {
        Layout.preferredWidth: 16
        Layout.preferredHeight: 16
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 6
        name: "tag"; size: 16; color: theme.faint
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: flow.implicitHeight + 8
        radius: 6
        color: theme.background
        border.color: input.activeFocus ? theme.accent : theme.divider
        MouseArea { anchors.fill: parent; onClicked: input.forceActiveFocus() }

        Flow {
            id: flow
            x: 6; y: 4
            width: parent.width - 12
            spacing: 5

            Repeater {
                model: root.tags
                Rectangle {
                    required property var modelData
                    objectName: root.objectName + "Chip:" + modelData.name
                    height: 22
                    width: chip.implicitWidth + 30
                    radius: 11
                    color: theme.mix(theme.surface, theme.foreground, 0.28)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.name
                    Text { id: chip; x: 9; anchors.verticalCenter: parent.verticalCenter; text: modelData.name; color: theme.background; font.pixelSize: backend.fontSize - 2; font.weight: Font.DemiBold }
                    Icon {
                        objectName: root.objectName + "Remove:" + modelData.name
                        anchors.right: parent.right
                        anchors.rightMargin: 5
                        anchors.verticalCenter: parent.verticalCenter
                        name: "close"; size: 9; stroke: 2.4; color: theme.background
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: "Remove tag " + modelData.name
                        Accessible.onPressAction: backend.removeTag(root.taskId, modelData.id)
                        Keys.onSpacePressed: backend.removeTag(root.taskId, modelData.id)
                        Keys.onReturnPressed: backend.removeTag(root.taskId, modelData.id)
                        Keys.onEnterPressed: backend.removeTag(root.taskId, modelData.id)
                        MouseArea { anchors.fill: parent; anchors.margins: -3; onClicked: backend.removeTag(root.taskId, modelData.id) }
                    }
                }
            }

            TextInput {
                id: input
                objectName: root.objectName + "Input"
                Accessible.name: root.accessibleName
                width: Math.max(70, contentWidth + 10)
                height: 22
                verticalAlignment: TextInput.AlignVCenter
                color: theme.foreground
                selectionColor: theme.selection
                font.pixelSize: backend.fontSize - 1
                clip: true
                Text { visible: !input.text; text: root.tags.length ? "Add another…" : "Add tags…"; color: theme.faint; font: input.font; anchors.verticalCenter: parent.verticalCenter }

                function accept(name) {
                    if (name.trim()) backend.addTag(root.taskId, name.trim())
                    text = ""
                    root.matches = []
                    popup.close()
                }
                onTextEdited: {
                    if (text.endsWith(",")) { accept(text.slice(0, -1)); return }
                    const q = text.toLowerCase()
                    const have = root.tags.map(t => t.name)
                    root.matches = q ? backend.tagNames.filter(n => root.fold(n).startsWith(root.fold(q)) && !have.includes(n)).slice(0, 6) : []
                    if (root.matches.length) popup.open(); else popup.close()
                }
                Keys.onReturnPressed: accept(root.matches.length && popup.visible ? root.matches[0] : text)
                Keys.onTabPressed: (e) => { if (text.trim()) { accept(root.matches.length && popup.visible ? root.matches[0] : text); e.accepted = true } else e.accepted = false }
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Backspace && !text && root.tags.length) {
                        backend.removeTag(root.taskId, root.tags[root.tags.length - 1].id)
                        e.accepted = true
                    }
                }
                Keys.onEscapePressed: (e) => { if (popup.visible) { popup.close(); e.accepted = true } else e.accepted = false }
                onActiveFocusChanged: if (!activeFocus && text.trim()) accept(text)

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
                                objectName: root.objectName + "Match:" + modelData
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
    }
}
