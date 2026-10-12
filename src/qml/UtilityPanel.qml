import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The right-hand Utility panel: tags grouped under their tag groups, with
// counts. Clicking a tag filters the task list; right-click pauses it, which
// hides every task carrying it, everywhere.
Rectangle {
    id: root
    color: theme.background

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 6
            Item { Layout.fillWidth: true }
            IconButton { icon: "tag"; checked: true; tip: "Tags" }
            IconButton { icon: "pin"; tip: "Locations (coming later)"; opacity: 0.4 }
            Item { Layout.fillWidth: true }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.tags
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: row
                required property var model
                width: ListView.view.width
                height: model.kind === "header" ? 36 : 28

                Text {
                    visible: row.model.kind === "header"
                    x: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    text: (row.model.title || "").toUpperCase()
                    color: theme.muted
                    font.pixelSize: backend.fontSize - 3
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                }

                Rectangle {
                    visible: row.model.kind === "tag"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: 6
                    color: row.model.filtered ? theme.selection : mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.05) : "transparent"
                }
                Icon {
                    visible: row.model.kind === "tag"
                    x: 20
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.model.paused ? "pause" : "tag"
                    size: 14
                    color: row.model.filtered ? theme.accent : theme.faint
                }
                Text {
                    visible: row.model.kind === "tag"
                    x: 42
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 100
                    text: row.model.title || ""
                    elide: Text.ElideRight
                    color: row.model.paused ? theme.faint : theme.foreground
                    font.pixelSize: backend.fontSize - 1
                    font.italic: !!row.model.paused
                }
                Rectangle {
                    visible: row.model.kind === "tag" && (row.model.count || 0) > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(18, countText.implicitWidth + 10)
                    height: 18
                    radius: 9
                    color: theme.mix(theme.background, theme.foreground, 0.13)
                    Text { id: countText; anchors.centerIn: parent; text: row.model.count || ""; color: theme.muted; font.pixelSize: backend.fontSize - 4; font.bold: true }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    enabled: row.model.kind === "tag"
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton) tagMenu.popup()
                        else backend.toggleTagFilter(row.model.tagId)
                    }
                }
                Menu {
                    id: tagMenu
                    MenuItem { text: row.model.paused ? "Resume…" : "Pause…"; onTriggered: backend.setTagPaused(row.model.tagId, !row.model.paused) }
                    MenuItem { text: "Filter by Tag"; onTriggered: backend.toggleTagFilter(row.model.tagId) }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: 38
            Rectangle { width: parent.width; height: 1; color: theme.divider }
            IconButton {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                icon: "plus"
                tip: "New Tag"
                onClicked: newTag.open()
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: backend.tagFilter.length > 0
                icon: "close"
                tip: "Clear Tag Filter"
                onClicked: backend.clearTagFilter()
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: backend.tagFilter.length > 0 ? 44 : 10
                anchors.verticalCenter: parent.verticalCenter
                icon: backend.showPaused ? "eye" : "eyeOff"
                checked: backend.showPaused
                tip: backend.showPaused ? "Hide Paused (Ctrl+U)" : "Show Paused (Ctrl+U)"
                onClicked: backend.showPaused = !backend.showPaused
            }
        }
    }

    Dialog {
        id: newTag
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "New Tag"
        standardButtons: Dialog.Cancel | Dialog.Ok
        TextField { id: tagName; width: 260; placeholderText: "Tag name"; onAccepted: newTag.accept() }
        onOpened: { tagName.text = ""; tagName.forceActiveFocus() }
        onAccepted: backend.newTag(tagName.text)
    }
}
