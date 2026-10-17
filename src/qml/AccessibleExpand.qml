import QtQuick

// Tells a screen reader whether `target` can expand and whether it is
// expanded. Accessible.expandable and Accessible.expanded arrived in Qt 6.11,
// and older Qt refuses to load a file that names them, so they are set from
// script, and only when the running Qt has them. On older Qt the accessible
// name, which already says "collapsed" or "expanded", carries the state alone.
QtObject {
    required property Item target
    property bool expandable: false
    property bool expanded: false

    function apply() {
        const ax = target.Accessible
        if (!("expanded" in ax))
            return
        ax.expandable = expandable
        ax.expanded = expanded
    }

    onExpandableChanged: apply()
    onExpandedChanged: apply()
    Component.onCompleted: apply()
}
