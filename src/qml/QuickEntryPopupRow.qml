import QtQuick

// The keyboard half of Quick Entry's option row: Left/Right (and Up/Down)
// move, Return or Space picks, digits 1-9 pick directly, Escape closes only
// the row. Letters are handed to `typeTarget` (a text field) when there is
// one. The name contains "Popup" so the application's Escape filter lets the
// row see Escape instead of closing the window.
FocusScope {
    id: row

    property int count: 0
    property int index: 0
    property Item typeTarget
    signal activated(int i)
    signal closeRequested()

    Keys.onPressed: (e) => {
        if (e.modifiers & (Qt.ControlModifier | Qt.ShiftModifier | Qt.AltModifier) && e.key !== Qt.Key_Backtab) return
        if (e.key === Qt.Key_Escape) { e.accepted = true; row.closeRequested(); return }
        if (e.key === Qt.Key_Left || e.key === Qt.Key_Up) { if (row.count) row.index = (row.index + row.count - 1) % row.count; e.accepted = true; return }
        if (e.key === Qt.Key_Right || e.key === Qt.Key_Down) { if (row.count) row.index = (row.index + 1) % row.count; e.accepted = true; return }
        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) {
            if (row.index >= 0 && row.index < row.count) row.activated(row.index)
            e.accepted = true
            return
        }
        if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9) {
            const i = e.key - Qt.Key_1
            if (i < row.count) { row.index = i; row.activated(i) }
            e.accepted = true
            return
        }
        if (row.typeTarget && e.text.length === 1 && e.text >= " ") {
            row.typeTarget.forceActiveFocus()
            row.typeTarget.insert(row.typeTarget.length, e.text)
            e.accepted = true
        }
    }
}
