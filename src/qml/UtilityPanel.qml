import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The right-hand Utility panel with two tabs: Tags (grouped, collapsible,
// searchable, draggable) and Locations (saved places). Clicking filters the
// task list; right-click pauses, which hides matching tasks everywhere.
//
// Dragging a tag row onto a task: drag key "precrime-tag", mimeData
// {"text/x-precrime-tag": "<tagId>"} (a string); drop on a DropArea with
// keys ["precrime-tag"] and read drop.getDataAsString("text/x-precrime-tag").
Rectangle {
    id: root
    color: theme.background

    property int tab: 0                 // 0 Tags, 1 Locations
    property var collapsed: ({})        // tag group id -> true
    property bool usedOnly: false
    property bool searchActive: false
    readonly property string query: searchActive ? search.text.trim().toLowerCase() : ""
    readonly property var dragProxy: ApplicationWindow.window ? ApplicationWindow.window.dragProxy : null

    function focusSearch() {
        tab = 0
        searchActive = true
        search.forceActiveFocus()
        search.selectAll()
    }
    function closeSearch() {
        searchActive = false
        search.text = ""
    }
    function toggleCollapsed(id) {
        const c = Object.assign({}, collapsed)
        if (c[id]) delete c[id]; else c[id] = true
        collapsed = c
    }
    function tagShown(m) {
        if (m.kind === "header") return query === ""
        if (query !== "") return (m.title || "").toLowerCase().startsWith(query)
        if (usedOnly && (m.count || 0) === 0) return false
        return !collapsed[m.groupId]
    }
    function toggleFirstMatch() {
        for (let i = 0; i < backend.tags.count; ++i) {
            const m = backend.tags.get(i)
            if (m.kind === "tag" && (m.title || "").toLowerCase().startsWith(query)) {
                backend.toggleTagFilter(m.tagId)
                return
            }
        }
    }

    // Name prompt shared by New/Rename for tags and groups.
    // mode: newTag | renameTag | newGroup | renameGroup
    function promptName(mode, id, initial, groupId) {
        nameDialog.mode = mode
        nameDialog.targetId = id
        nameDialog.groupId = groupId || 0
        nameDialog.initial = initial || ""
        nameDialog.open()
    }
    function editLocation(row) {
        locDialog.locId = row ? row.locationId : 0
        locDialog.initialName = row ? row.title : ""
        locDialog.initialAddress = row ? (row.address || "") : ""
        locDialog.open()
    }

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
            IconButton { icon: "tag"; checked: root.tab === 0; tip: "Tags"; onClicked: root.tab = 0 }
            IconButton { icon: "pin"; checked: root.tab === 1; tip: "Locations"; onClicked: root.tab = 1 }
            Item { Layout.fillWidth: true }
        }

        // Tag Search.
        TextField {
            id: search
            visible: root.searchActive && root.tab === 0
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 6
            placeholderText: "Search tags"
            font.pixelSize: backend.fontSize - 1
            Keys.onReturnPressed: root.toggleFirstMatch()
            Keys.onEnterPressed: root.toggleFirstMatch()
            Keys.onEscapePressed: root.closeSearch()
        }

        // ------------------------------------------------------------ tags
        ListView {
            id: tagList
            visible: root.tab === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.tags
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: row
                required property var model
                readonly property bool isHeader: model.kind === "header"
                readonly property bool shown: root.tagShown(model)
                readonly property bool isGroup: isHeader && (model.groupId || 0) > 0
                width: ListView.view.width
                visible: shown
                height: !shown ? 0 : isHeader ? 36 : 28

                // Header.
                Text {
                    visible: row.isHeader
                    x: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    text: (row.model.title || "").toUpperCase()
                    color: theme.muted
                    font.pixelSize: backend.fontSize - 3
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                }
                Icon {
                    visible: row.isGroup
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    name: root.collapsed[row.model.groupId] ? "chevronRight" : "chevronDown"
                    size: 11
                    color: theme.faint
                }

                // Tag row.
                Rectangle {
                    visible: !row.isHeader
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: 6
                    color: row.model.filtered ? theme.selection : mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.05) : "transparent"
                }
                Icon {
                    visible: !row.isHeader
                    x: 20
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.model.paused ? "pause" : "tag"
                    size: 14
                    color: row.model.filtered ? theme.accent : theme.faint
                }
                Text {
                    visible: !row.isHeader
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
                    visible: !row.isHeader && (row.model.count || 0) > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(18, countText.implicitWidth + 10)
                    height: 18
                    radius: 9
                    color: theme.mix(theme.background, theme.foreground, 0.13)
                    Text { id: countText; anchors.centerIn: parent; text: row.model.count || ""; color: theme.muted; font.pixelSize: backend.fontSize - 4; font.bold: true }
                }

                // Tasks dropped here tag themselves.
                DropArea {
                    id: drop
                    anchors.fill: parent
                    keys: ["precrime-tasks"]
                    enabled: row.shown && (!row.isHeader || row.isGroup)
                    onEntered: if (root.dragProxy) root.dragProxy.hint = "Drop to Tag"
                    onExited: if (root.dragProxy) root.dragProxy.hint = ""
                    onDropped: row.isHeader ? backend.dropOnTagGroup(row.model.groupId) : backend.dropOnTag(row.model.tagId)
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        radius: 6
                        color: "transparent"
                        border.width: 2
                        border.color: theme.accent
                        visible: drop.containsDrag
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    enabled: row.shown && (!row.isHeader || row.isGroup)
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    drag.target: row.isHeader ? undefined : tagDrag
                    drag.threshold: 10
                    drag.onActiveChanged: {
                        if (drag.active) tagDrag.begin(row.model.title, row.model.tagId)
                        else tagDrag.end()
                    }
                    onPressed: (m) => { if (!row.isHeader) tagDrag.place(mapToItem(null, m.x, m.y)) }
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton) {
                            if (row.isHeader) groupMenu.popup(); else tagMenu.popup()
                        } else if (row.isHeader) {
                            root.toggleCollapsed(row.model.groupId)
                        } else {
                            backend.toggleTagFilter(row.model.tagId)
                        }
                    }
                }

                Menu {
                    id: tagMenu
                    MenuItem { text: row.model.paused ? "Resume…" : "Pause…"; onTriggered: backend.setTagPaused(row.model.tagId, !row.model.paused) }
                    MenuItem { text: "Rename…"; onTriggered: root.promptName("renameTag", row.model.tagId, row.model.title) }
                    Menu {
                        id: moveMenu
                        title: "Move to Group"
                        onAboutToShow: {
                            while (count > 0) takeItem(0)
                            const choices = backend.tagGroupChoices()
                            for (const c of choices) {
                                const item = moveItem.createObject(null, { text: c.name, groupId: c.id, checked: c.id === row.model.groupId })
                                addItem(item)
                            }
                        }
                    }
                    MenuSeparator {}
                    MenuItem { text: "Delete Tag"; onTriggered: { confirm.kind = "tag"; confirm.targetId = row.model.tagId; confirm.name = row.model.title; confirm.open() } }
                }
                Component {
                    id: moveItem
                    MenuItem {
                        property int groupId: 0
                        checkable: true
                        onTriggered: backend.setTagGroup(row.model.tagId, groupId)
                    }
                }
                Menu {
                    id: groupMenu
                    MenuItem { text: root.collapsed[row.model.groupId] ? "Expand" : "Collapse"; onTriggered: root.toggleCollapsed(row.model.groupId) }
                    MenuItem { text: "Rename…"; onTriggered: root.promptName("renameGroup", row.model.groupId, row.model.title) }
                    MenuItem { text: "New Tag in Group"; onTriggered: root.promptName("newTag", 0, "", row.model.groupId) }
                    MenuSeparator {}
                    MenuItem { text: "Delete Group"; onTriggered: { confirm.kind = "group"; confirm.targetId = row.model.groupId; confirm.name = row.model.title; confirm.open() } }
                }
            }
        }

        // ------------------------------------------------------- locations
        ListView {
            id: placeList
            visible: root.tab === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.places
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Text {
                visible: placeList.count === 0
                anchors.centerIn: parent
                text: "No locations yet"
                color: theme.faint
                font.pixelSize: backend.fontSize - 1
            }

            delegate: Item {
                id: prow
                required property var model
                width: ListView.view.width
                height: 44

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 1
                    anchors.bottomMargin: 1
                    radius: 6
                    color: prow.model.filtered ? theme.selection : pmouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.05) : "transparent"
                }
                Icon {
                    x: 20
                    anchors.verticalCenter: parent.verticalCenter
                    name: prow.model.paused ? "pause" : "pin"
                    size: 14
                    color: prow.model.filtered ? theme.accent : theme.faint
                }
                Column {
                    x: 42
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 100
                    opacity: prow.model.paused ? 0.5 : 1
                    Text {
                        width: parent.width
                        text: prow.model.title || ""
                        elide: Text.ElideRight
                        color: theme.foreground
                        font.pixelSize: backend.fontSize - 1
                    }
                    Text {
                        visible: !!prow.model.address
                        width: parent.width
                        text: prow.model.address || ""
                        elide: Text.ElideRight
                        color: theme.muted
                        font.pixelSize: backend.fontSize - 3
                    }
                }
                Rectangle {
                    visible: (prow.model.count || 0) > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(18, pcount.implicitWidth + 10)
                    height: 18
                    radius: 9
                    color: theme.mix(theme.background, theme.foreground, 0.13)
                    Text { id: pcount; anchors.centerIn: parent; text: prow.model.count || ""; color: theme.muted; font.pixelSize: backend.fontSize - 4; font.bold: true }
                }
                DropArea {
                    id: pdrop
                    anchors.fill: parent
                    keys: ["precrime-tasks"]
                    onEntered: if (root.dragProxy) root.dragProxy.hint = "Drop to Set Location"
                    onExited: if (root.dragProxy) root.dragProxy.hint = ""
                    onDropped: backend.dropOnLocation(prow.model.locationId)
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        radius: 6
                        color: "transparent"
                        border.width: 2
                        border.color: theme.accent
                        visible: pdrop.containsDrag
                    }
                }
                MouseArea {
                    id: pmouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton) placeMenu.popup()
                        else backend.toggleLocationFilter(prow.model.locationId)
                    }
                }
                Menu {
                    id: placeMenu
                    MenuItem { text: prow.model.paused ? "Resume" : "Pause"; onTriggered: backend.setLocationPaused(prow.model.locationId, !prow.model.paused) }
                    MenuItem { text: "Edit…"; onTriggered: root.editLocation(prow.model) }
                    MenuSeparator {}
                    MenuItem { text: "Delete"; onTriggered: backend.deleteLocation(prow.model.locationId) }
                }
            }
        }

        // ---------------------------------------------------------- footer
        Item {
            Layout.fillWidth: true
            implicitHeight: 38
            Rectangle { width: parent.width; height: 1; color: theme.divider }
            IconButton {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                icon: "plus"
                tip: root.tab === 0 ? "New Tag or Tag Group" : "New Location"
                onClicked: root.tab === 0 ? addMenu.popup() : root.editLocation(null)
                Menu {
                    id: addMenu
                    MenuItem { text: "New Tag"; onTriggered: root.promptName("newTag", 0, "", 0) }
                    MenuItem { text: "New Tag Group"; onTriggered: root.promptName("newGroup", 0, "") }
                }
            }
            IconButton {
                id: clearBtn
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: backend.tagFilter.length > 0 && root.tab === 0
                icon: "close"
                tip: "Clear Tag Filter"
                onClicked: backend.clearTagFilter()
            }
            IconButton {
                id: pausedBtn
                anchors.right: parent.right
                anchors.rightMargin: clearBtn.visible ? 44 : 10
                anchors.verticalCenter: parent.verticalCenter
                icon: backend.showPaused ? "eye" : "eyeOff"
                checked: backend.showPaused
                tip: backend.showPaused ? "Hide Paused (Ctrl+U)" : "Show Paused (Ctrl+U)"
                onClicked: backend.showPaused = !backend.showPaused
            }
            IconButton {
                anchors.right: pausedBtn.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                visible: root.tab === 0
                icon: "list"
                checked: root.usedOnly
                tip: root.usedOnly ? "Show All Tags" : "Show Used Tags Only"
                onClicked: root.usedOnly = !root.usedOnly
            }
        }
    }

    // What follows the pointer while dragging a tag onto a task.
    Rectangle {
        id: tagDrag
        parent: Overlay.overlay
        visible: Drag.active
        z: 1000
        width: dragLabel.implicitWidth + 22
        height: 26
        radius: 13
        color: theme.accent
        opacity: 0.94
        Drag.keys: ["precrime-tag"]
        Drag.hotSpot.x: 10
        Drag.hotSpot.y: 13
        function place(p) {
            if (Drag.active) return
            x = p.x - 10
            y = p.y - 13
        }
        function begin(title, tagId) {
            dragLabel.text = title
            Drag.mimeData = { "text/x-precrime-tag": String(tagId) }
            Drag.active = true
        }
        function end() {
            if (!Drag.active) return
            Drag.drop()
            Drag.active = false
        }
        Text {
            id: dragLabel
            anchors.centerIn: parent
            color: theme.accentForeground
            font.pixelSize: backend.fontSize - 2
            font.weight: Font.DemiBold
        }
    }

    Dialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: nameDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        property string mode: "newTag"
        property int targetId: 0
        property int groupId: 0
        property string initial: ""
        title: mode === "newTag" ? "New Tag" : mode === "renameTag" ? "Rename Tag" : mode === "newGroup" ? "New Tag Group" : "Rename Tag Group"
        standardButtons: Dialog.Cancel | Dialog.Ok
        TextField { id: nameField; width: 260; placeholderText: "Name"; onAccepted: nameDialog.accept() }
        onOpened: { nameField.text = initial; nameField.forceActiveFocus(); nameField.selectAll() }
        onAccepted: {
            const n = nameField.text.trim()
            if (!n) return
            if (mode === "newTag") backend.newTag(n, groupId)
            else if (mode === "renameTag") backend.renameTag(targetId, n)
            else if (mode === "newGroup") backend.newTagGroup(n)
            else backend.renameTagGroup(targetId, n)
        }
    }

    Dialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: confirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        property string kind: "tag"
        property int targetId: 0
        property string name: ""
        title: kind === "tag" ? "Delete Tag" : "Delete Tag Group"
        standardButtons: Dialog.Cancel | Dialog.Ok
        Text {
            width: 280
            wrapMode: Text.WordWrap
            color: theme.foreground
            text: confirm.kind === "tag" ? "Delete the tag \"" + confirm.name + "\"? It is removed from every task."
                                         : "Delete the group \"" + confirm.name + "\"? Its tags are kept."
        }
        onAccepted: confirm.kind === "tag" ? backend.deleteTag(targetId) : backend.deleteTagGroup(targetId)
    }

    Dialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: locDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        property int locId: 0
        property string initialName: ""
        property string initialAddress: ""
        title: locId ? "Edit Location" : "New Location"
        standardButtons: Dialog.Cancel | Dialog.Ok
        ColumnLayout {
            spacing: 8
            TextField { id: locName; Layout.preferredWidth: 280; placeholderText: "Name"; onAccepted: locAddress.forceActiveFocus() }
            TextField { id: locAddress; Layout.preferredWidth: 280; placeholderText: "Address"; onAccepted: locDialog.accept() }
        }
        onOpened: { locName.text = initialName; locAddress.text = initialAddress; locName.forceActiveFocus() }
        onAccepted: {
            if (!locName.text.trim()) return
            if (locId) backend.updateLocation(locId, locName.text, locAddress.text)
            else backend.newLocation(locName.text, locAddress.text)
        }
    }
}
