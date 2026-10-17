import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The proposal review sheet: every suggestion the agent has queued, one row
// each, with the kind and a human summary of its payload. It is keyboard
// first — y applies the highlighted proposal, n or Delete dismisses it, a
// applies all of them, Esc closes — and every control carries an accessible
// name. Nothing appears here unless the review was asked for, so the feature
// stays inert while agent.enabled is off.
Popup {
    id: root
    objectName: "proposalSheet"

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 40)
    height: Math.min(520, parent.height - 40)
    padding: 0
    modal: true
    focus: true
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }

    // A proposal's kind and payload, in one line a person can read.
    function summary(p) {
        if (!p)
            return ""
        const payload = p.payload || {}
        if (p.kind === "set-due-date") {
            const task = backend.taskInfo(Number(payload.taskId || 0))
            const title = task && task.title ? task.title : "task " + payload.taskId
            return "Set due date of “" + title + "” to " + (payload.due || "?")
        }
        if (p.kind === "query")
            return "Ask: “" + (payload.prompt || "?") + "” → " + (payload.query || "?")
        if (p.kind === "triage") {
            const task = backend.taskInfo(Number(payload.taskId || 0))
            const title = task && task.title ? task.title : "task " + payload.taskId
            const parts = []
            if (payload.list) parts.push("to “" + payload.list + "”")
            if (payload.tags && payload.tags.length) parts.push("tags " + payload.tags.join(", "))
            if (payload.due) parts.push("due " + payload.due)
            if (payload.priority !== undefined && payload.priority !== null) parts.push(priorityName(payload.priority))
            return "Triage “" + title + "”: " + (parts.length ? parts.join(", ") : "no change")
        }
        if (p.kind === "priority") {
            const task = backend.taskInfo(Number(payload.taskId || 0))
            const title = task && task.title ? task.title : "task " + payload.taskId
            return "Set priority of “" + title + "” to " + priorityName(payload.priority)
        }
        if (p.kind === "step") {
            const task = backend.taskInfo(Number(payload.taskId || 0))
            const title = task && task.title ? task.title : "task " + payload.taskId
            const step = payload.title || "?"
            const duration = payload.duration > 0 ? " — " + backend.durationText(payload.duration) : ""
            return "Add to “" + title + "”: " + step + duration
        }
        return p.kind + " " + JSON.stringify(payload)
    }

    function priorityName(value) {
        return value === 0 ? "None" : value === 1 ? "Low" : value === 2 ? "Medium" : value === 3 ? "High" : String(value)
    }

    // The list is reassigned when the queue changes, so the sheet follows an
    // apply or dismiss without a live binding to a function call.
    property var items: []
    function reload() { items = backend.proposals() }
    Connections {
        target: backend
        function onProposalsChanged() { root.reload() }
    }

    function openSheet() {
        reload()
        list.currentIndex = 0
        open()
        list.forceActiveFocus()
    }

    function current() {
        return list.currentIndex >= 0 && list.currentIndex < items.length ? items[list.currentIndex] : null
    }

    function applyCurrent() {
        const p = current()
        if (!p)
            return
        backend.applyProposal(p.id)
        reload()
        if (list.count > 0)
            list.currentIndex = Math.min(list.currentIndex, list.count - 1)
        else
            root.close()
    }

    function dismissCurrent() {
        const p = current()
        if (!p)
            return
        backend.dismissProposal(p.id)
        reload()
        if (list.count > 0)
            list.currentIndex = Math.min(list.currentIndex, list.count - 1)
        if (list.count === 0)
            root.close()
    }

    function applyAll() {
        const all = backend.proposals()
        for (const p of all)
            backend.applyProposal(p.id)
        reload()
        if (items.length === 0)
            root.close()
    }

    onOpened: list.forceActiveFocus()

    contentItem: Item {
        implicitHeight: root.height
        focus: true
        Accessible.role: Accessible.Dialog
        Accessible.name: "Suggested changes"

        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Y) { root.applyCurrent(); e.accepted = true }
            else if (e.key === Qt.Key_N || e.key === Qt.Key_Delete || e.key === Qt.Key_Backspace) { root.dismissCurrent(); e.accepted = true }
            else if (e.key === Qt.Key_A) { root.applyAll(); e.accepted = true }
            else if (e.key === Qt.Key_Escape) { root.close(); e.accepted = true }
            else if (e.key === Qt.Key_Down) { list.incrementCurrentIndex(); e.accepted = true }
            else if (e.key === Qt.Key_Up) { list.decrementCurrentIndex(); e.accepted = true }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 8

            Text {
                text: "Suggested changes"
                color: theme.foreground
                font.pixelSize: backend.fontSize + 5
                font.weight: Font.DemiBold
            }
            Text {
                objectName: "proposalHint"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: backend.fontSize - 2
                text: "Y applies, N or Delete dismisses, A applies all, Esc closes."
            }

            ListView {
                id: list
                objectName: "proposalList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.items
                focus: true
                currentIndex: 0
                keyNavigationEnabled: false
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: row.implicitHeight + 16
                    radius: 8
                    color: index === list.currentIndex ? theme.selection : "transparent"

                    Accessible.role: Accessible.ListItem
                    Accessible.name: root.summary(modelData)
                    Accessible.selected: index === list.currentIndex
                    Accessible.onPressAction: root.applyCurrent()

                    RowLayout {
                        id: row
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 10
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.foreground
                            font.pixelSize: backend.fontSize
                            text: root.summary(modelData)
                        }
                        Button {
                            objectName: "proposalApply:" + modelData.id
                            text: "Apply"
                            Accessible.name: "Apply: " + root.summary(modelData)
                            flat: true
                            onClicked: { list.currentIndex = index; root.applyCurrent() }
                        }
                        Button {
                            objectName: "proposalDismiss:" + modelData.id
                            text: "Dismiss"
                            Accessible.name: "Dismiss: " + root.summary(modelData)
                            flat: true
                            onClicked: { list.currentIndex = index; root.dismissCurrent() }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "No suggestions"
                    color: theme.faint
                    font.pixelSize: backend.fontSize + 1
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "proposalApplyAll"
                    text: "Apply All"
                    Accessible.name: "Apply all suggestions"
                    enabled: root.items.length > 0
                    onClicked: root.applyAll()
                }
                Button {
                    objectName: "proposalDismissAll"
                    text: "Dismiss All"
                    Accessible.name: "Dismiss all suggestions"
                    enabled: root.items.length > 0
                    flat: true
                    onClicked: { backend.dismissAllProposals(); root.close() }
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "proposalClose"
                    text: "Close"
                    Accessible.name: "Close the suggestions sheet"
                    flat: true
                    onClicked: root.close()
                }
            }
        }
    }
}
