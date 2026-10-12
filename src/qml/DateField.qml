import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Plain

// A typeable date field with the reference app's suggestion dropdown: "tom 3pm", "in 2w",
// "18d", "fri". Up/Down pick a suggestion, Return or a click applies it, and
// leaving the field applies the best reading of what was typed.
Plain.TextField {
    id: root

    property string value
    property string iso  // The current date, for the picker.
    // Quick Entry shows date fields as boxes so it's clear they're inputs;
    // the editor card keeps them borderless until focused.
    property bool boxed: false
    property bool clearable: false
    signal cleared()
    property bool timeOnly: false
    property var suggestions: []
    property int highlighted: 0
    property bool dirty: false
    signal picked(string iso, int minutes)
    signal typed(string text)

    implicitWidth: timeOnly ? 84 : 116
    padding: 3
    leftPadding: 6
    font.pixelSize: backend.fontSize - 1
    color: theme.foreground
    placeholderTextColor: theme.faint
    selectByMouse: true
    horizontalAlignment: Text.AlignRight
    rightPadding: clearable ? 22 : 6
    background: Rectangle {
        radius: 5
        color: root.activeFocus || root.boxed ? theme.background : "transparent"
        border.color: root.activeFocus ? theme.accent : root.boxed ? theme.divider : "transparent"
        Icon {
            visible: root.clearable && !root.activeFocus
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            name: "clear"; size: 12; color: theme.faint
            MouseArea { anchors.fill: parent; anchors.margins: -3; onClicked: root.cleared() }
        }
    }

    Component.onCompleted: text = value
    onValueChanged: if (!activeFocus) text = value
    onActiveFocusChanged: {
        if (activeFocus) { selectAll(); return }
        popup.close()
        if (dirty) { dirty = false; typed(text) }
        text = value
    }
    onTextEdited: {
        dirty = true
        suggestions = timeOnly ? [] : backend.dateSuggestions(text)
        highlighted = 0
        if (suggestions.length) popup.open(); else popup.close()
    }

    function apply(i) {
        const s = suggestions[i]
        dirty = false
        popup.close()
        picked(s.date, s.minutes)
    }

    // Down with no suggestions open shows the calendar, as in the reference app.
    Keys.onDownPressed: {
        if (popup.visible) highlighted = Math.min(highlighted + 1, suggestions.length - 1)
        else if (!timeOnly) picker.openAt(root, root.iso)
    }
    Keys.onUpPressed: if (popup.visible) highlighted = Math.max(highlighted - 1, 0)
    Keys.onReturnPressed: (e) => {
        if (popup.visible && suggestions.length) apply(highlighted)
        else if (dirty) { dirty = false; typed(text) }
        e.accepted = true
    }
    Keys.onEscapePressed: (e) => {
        if (popup.visible) { popup.close(); e.accepted = true; return }
        dirty = false
        text = value
        e.accepted = false
    }

    TapHandler { enabled: !root.timeOnly; onTapped: if (!popup.visible) picker.openAt(root, root.iso) }

    DatePicker {
        id: picker
        onPicked: (iso) => { root.dirty = false; root.picked(iso, -1) }
    }

    Popup {
        id: popup
        popupType: Popup.Window
        y: root.height + 2
        x: root.width - width
        width: 210
        padding: 4
        background: Rectangle { color: theme.raised; radius: 8; border.color: theme.divider }
        contentItem: Column {
            Repeater {
                model: root.suggestions
                Rectangle {
                    required property var modelData
                    required property int index
                    width: 202
                    height: 26
                    radius: 5
                    color: index === root.highlighted ? theme.selection : "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 8
                        text: modelData.label
                        color: theme.foreground
                        font.pixelSize: backend.fontSize - 1
                    }
                    MouseArea { anchors.fill: parent; onClicked: root.apply(index) }
                }
            }
        }
    }
}
