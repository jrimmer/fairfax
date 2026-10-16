import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The plain-English ask: one text field and a Go. The model compiles the
// question into a Fairfax query, which Backend puts in the search field as if
// it had been typed — editable and runnable. A parse hint appears when the
// parser had to adjust what the model wrote, and a Save as Smart List… button
// hands the query to the Search Options sheet. Nothing is sent while
// agent.enabled is off: the entry that opens this sheet is hidden then, and
// Backend.askQuery refuses, so no request leaves the machine.
Popup {
    id: root
    objectName: "askSheet"

    // The sheet is opened by the toolbar's "Ask in plain English…" entry and
    // by the view.askEnglish shortcut, both only while the agent is on.
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: Math.max(20, parent.height * 0.12)
    width: Math.min(520, parent.width - 40)
    padding: 0
    modal: true
    focus: true
    dim: false
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }

    // Raised when Save as Smart List… is pressed; TaskPane opens the Search
    // Options sheet on the compiled query so its own save flow does the work.
    signal saveRequested()

    // What the field last asked for, kept so the result reads as a sentence.
    property string asked: ""

    function openSheet() {
        asked = ""
        input.text = ""
        open()
        x = (parent.width - width) / 2
        y = Math.max(20, parent.height * 0.12)
        input.forceActiveFocus()
    }

    function go() {
        const prompt = input.text.trim()
        if (!prompt)
            return
        asked = prompt
        backend.askQuery(prompt)
    }

    onOpened: input.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 0
        Accessible.role: Accessible.Dialog
        Accessible.name: "Ask in plain English"

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 12
            spacing: 8

            Text {
                text: "Ask in plain English"
                color: theme.foreground
                font.pixelSize: backend.fontSize + 4
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: backend.fontSize - 2
                text: "Describe the tasks you want in your own words. Fairfax asks the model for a search query, then runs it — you can edit it and save it as a Smart List."
            }

            TextField {
                id: input
                objectName: "askField"
                Layout.fillWidth: true
                Accessible.name: "Question in plain English"
                placeholderText: "What's waiting on Sam?"
                color: theme.foreground
                font.pixelSize: backend.fontSize + 1
                enabled: !backend.compiling
                background: Rectangle { radius: 8; color: theme.background; border.color: input.activeFocus ? theme.accent : theme.divider }
                Keys.onReturnPressed: root.go()
                Keys.onEnterPressed: root.go()
            }

            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "askGo"
                    text: backend.compiling ? "Asking…" : "Go"
                    Accessible.name: "Ask"
                    enabled: input.text.trim().length > 0 && !backend.compiling
                    onClicked: root.go()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.divider }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.topMargin: 12
            spacing: 8

            Text {
                visible: backend.compiling
                text: "Asking the model…"
                color: theme.muted
                font.pixelSize: backend.fontSize
            }
            Text {
                objectName: "askResult"
                Layout.fillWidth: true
                visible: !backend.compiling && root.asked !== "" && backend.compiledQuery !== ""
                wrapMode: Text.Wrap
                color: theme.foreground
                font.pixelSize: backend.fontSize
                text: "“" + root.asked + "” became:"
            }
            Text {
                Layout.fillWidth: true
                visible: !backend.compiling && root.asked !== "" && backend.compiledQuery !== ""
                wrapMode: Text.WrapAnywhere
                color: theme.today
                font.family: theme.monoFont
                font.pixelSize: backend.fontSize
                text: backend.compiledQuery
            }
            // The parse hint: one line saying what Query::parse adjusted.
            Text {
                objectName: "askHint"
                Layout.fillWidth: true
                visible: root.asked !== "" && backend.compiledHint !== ""
                wrapMode: Text.Wrap
                color: theme.priorityMedium
                font.pixelSize: backend.fontSize - 2
                text: backend.compiledHint
            }
            Text {
                objectName: "askError"
                Layout.fillWidth: true
                visible: root.asked !== "" && backend.compileError !== ""
                wrapMode: Text.Wrap
                color: theme.overdue
                font.pixelSize: backend.fontSize - 2
                text: backend.compileError
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    objectName: "askSave"
                    visible: !backend.compiling && root.asked !== "" && backend.compiledQuery !== ""
                    text: "Save as Smart List…"
                    Accessible.name: "Save as Smart List"
                    onClicked: { root.saveRequested(); root.close() }
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "askClose"
                    text: "Close"
                    Accessible.name: "Close the ask sheet"
                    flat: true
                    onClicked: root.close()
                }
            }
        }
    }
}
