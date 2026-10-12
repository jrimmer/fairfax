import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A small HSV colour picker for custom list colours: a saturation/value
// square, a hue strip and a hex field. Call pick() with the starting colour;
// `picked` fires with the chosen one.
Dialog {
    id: root

    signal picked(color value)

    property real hue: 0
    property real sat: 1
    property real val: 1
    readonly property color current: Qt.hsva(hue, sat, val, 1)

    function pick(start) {
        const c = Qt.color(start)
        hue = Math.max(0, c.hsvHue)
        sat = c.hsvSaturation
        val = c.hsvValue
        hexInput.text = String(current).toUpperCase()
        open()
    }

    function setFrom(text) {
        const c = Qt.color(text)
        hue = Math.max(0, c.hsvHue)
        sat = c.hsvSaturation
        val = c.hsvValue
    }

    onCurrentChanged: if (!hexInput.activeFocus) hexInput.text = String(current).toUpperCase()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: "Custom Color"
    width: 300
    standardButtons: Dialog.Cancel | Dialog.Ok
    onAccepted: picked(current)

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            spacing: 10
            // Saturation across, value down.
            Rectangle {
                id: square
                Layout.preferredWidth: 200
                Layout.preferredHeight: 160
                radius: 4
                color: Qt.hsva(root.hue, 1, 1, 1)
                clip: true
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "white" }
                        GradientStop { position: 1; color: "transparent" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: "black" }
                    }
                }
                Rectangle {
                    x: root.sat * square.width - 6
                    y: (1 - root.val) * square.height - 6
                    width: 12; height: 12; radius: 6
                    color: "transparent"
                    border.width: 2
                    border.color: root.val > 0.5 && root.sat < 0.5 ? "black" : "white"
                }
                MouseArea {
                    anchors.fill: parent
                    function set(m) {
                        root.sat = Math.max(0, Math.min(1, m.x / width))
                        root.val = 1 - Math.max(0, Math.min(1, m.y / height))
                    }
                    onPressed: (m) => set(m)
                    onPositionChanged: (m) => set(m)
                }
            }
            Rectangle {
                id: strip
                Layout.preferredWidth: 22
                Layout.preferredHeight: 160
                radius: 4
                gradient: Gradient {
                    GradientStop { position: 0.00; color: "#FF0000" }
                    GradientStop { position: 0.17; color: "#FFFF00" }
                    GradientStop { position: 0.33; color: "#00FF00" }
                    GradientStop { position: 0.50; color: "#00FFFF" }
                    GradientStop { position: 0.67; color: "#0000FF" }
                    GradientStop { position: 0.83; color: "#FF00FF" }
                    GradientStop { position: 1.00; color: "#FF0000" }
                }
                Rectangle {
                    y: root.hue * strip.height - 2
                    width: parent.width; height: 4
                    color: "transparent"
                    border.width: 2
                    border.color: "white"
                }
                MouseArea {
                    anchors.fill: parent
                    function set(m) { root.hue = Math.max(0, Math.min(0.999, m.y / height)) }
                    onPressed: (m) => set(m)
                    onPositionChanged: (m) => set(m)
                }
            }
        }

        RowLayout {
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 14
                color: root.current
                border.width: 1
                border.color: theme.divider
            }
            TextField {
                id: hexInput
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                maximumLength: 7
                font.family: theme.monoFont
                onTextEdited: if (/^#[0-9a-fA-F]{6}$/.test(text)) root.setFrom(text)
            }
        }
    }
}
