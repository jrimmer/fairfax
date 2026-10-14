import QtQuick
import QtQuick.Shapes

// The arrow-shaped list badge the reference app puts before a project's name and in the
// editor's list ribbon.
Item {
    id: root

    property string text
    property color color: theme.accent
    property bool chevrons: false
    property bool pressedDown: false  // Lighter while its menu is open.
    // Overrides `text` for the accessible name.
    property string accessibleName: ""
    signal clicked()

    implicitWidth: label.implicitWidth + 30 + (chevrons ? 16 : 0)
    implicitHeight: label.implicitHeight + 8
    activeFocusOnTab: true

    Accessible.role: Accessible.StaticText
    Accessible.name: accessibleName !== "" ? accessibleName : root.text
    Accessible.onPressAction: root.clicked()

    Keys.onSpacePressed: root.clicked()
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: root.pressedDown ? Qt.lighter(root.color, 1.25) : root.color
            strokeColor: "transparent"
            startX: 4; startY: 0
            PathLine { x: root.width - 10; y: 0 }
            PathLine { x: root.width; y: root.height / 2 }
            PathLine { x: root.width - 10; y: root.height }
            PathLine { x: 4; y: root.height }
            PathArc { x: 0; y: root.height - 4; radiusX: 4; radiusY: 4 }
            PathLine { x: 0; y: 4 }
            PathArc { x: 4; y: 0; radiusX: 4; radiusY: 4 }
        }
    }
    Text {
        id: label
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: "white"
        font.pixelSize: backend.fontSize - 1
        font.weight: Font.DemiBold
    }
    Icon {
        visible: root.chevrons
        anchors.left: label.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        name: "updown"
        size: 11
        stroke: 2
        color: "white"
    }
    MouseArea { anchors.fill: parent; onClicked: root.clicked() }
    // A focus ring only for keyboard focus, so the mouse look is unchanged.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: root.activeFocus
        border.width: 2
        border.color: theme.accent
    }
}
