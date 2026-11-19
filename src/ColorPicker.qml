import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Presets for the usual picks, a hue slider for anything else, and a hex
// field for an exact colour. Saturation and value stay at full (the backend
// makes every colour vivid), because washed-out colours tint unevenly across
// the zones and the brightness slider already dims the keyboard.
ColumnLayout {
    id: picker

    property var presets: []
    // Hue is undefined for white and greys, so keep the last one to slide from.
    property real hue: 0

    function followHue() {
        if (backend.color.hsvHue >= 0)
            hue = backend.color.hsvHue
    }
    Component.onCompleted: followHue()
    Connections {
        target: backend
        function onStateChanged() { picker.followHue() }
    }

    spacing: 12

    SectionLabel { text: qsTr("Colour") }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Label {
            Layout.fillWidth: true
            text: qsTr("Match the Omarchy theme")
            wrapMode: Text.Wrap
        }
        Switch {
            checked: backend.followTheme
            focusPolicy: Qt.NoFocus
            onToggled: backend.setFollowTheme(checked)
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 10
        // Still usable while following: picking a colour stops following.
        opacity: backend.followTheme ? 0.45 : 1

        Repeater {
            model: picker.presets

            Rectangle {
                required property string modelData
                readonly property bool current: backend.color.toString().toLowerCase() === modelData

                width: 32
                height: 32
                radius: 16
                color: modelData
                border.width: current ? 3 : 1
                border.color: current ? theme.accent : "#3a3a40"

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.setColor(parent.modelData)
                }
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        opacity: backend.followTheme ? 0.45 : 1
        columns: 2
        columnSpacing: 12
        rowSpacing: 0

        Label { text: qsTr("Hue"); opacity: 0.7 }
        Slider {
            id: hueSlider
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            from: 0
            to: 1
            value: picker.hue
            onMoved: backend.setColor(Qt.hsva(value, 1, 1, 1))

            background: Rectangle {
                x: hueSlider.leftPadding
                y: hueSlider.topPadding + hueSlider.availableHeight / 2 - height / 2
                width: hueSlider.availableWidth
                height: 6
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#ff0000" }
                    GradientStop { position: 0.17; color: "#ffff00" }
                    GradientStop { position: 0.33; color: "#00ff00" }
                    GradientStop { position: 0.5; color: "#00ffff" }
                    GradientStop { position: 0.67; color: "#0000ff" }
                    GradientStop { position: 0.83; color: "#ff00ff" }
                    GradientStop { position: 1.0; color: "#ff0000" }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Rectangle {
            width: 40
            height: 40
            radius: Math.min(4, theme.radius)
            color: backend.color
            border.color: "#3a3a40"
        }

        TextField {
            id: hexField
            Layout.preferredWidth: 160
            placeholderText: qsTr("Hex colour")
            text: backend.color.toString().toUpperCase()
            inputMethodHints: Qt.ImhNoPredictiveText
            validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
            onEditingFinished: {
                const value = text.startsWith("#") ? text : "#" + text
                if (/^#[0-9a-fA-F]{6}$/.test(value))
                    backend.setColor(value)
                else
                    text = Qt.binding(() => backend.color.toString().toUpperCase())
                focus = false
            }
            Keys.onEscapePressed: {
                text = Qt.binding(() => backend.color.toString().toUpperCase())
                focus = false
            }
        }
        Item { Layout.fillWidth: true }
        Button {
            text: backend.trimmed ? qsTr("Zones calibrated") : qsTr("Calibrate zones…")
            flat: true
            focusPolicy: Qt.NoFocus
            font.capitalization: Font.MixedCase
            Material.roundedScale: theme.radius
            onClicked: calibration.open()
        }
    }

    CalibrationPanel {
        id: calibration
        objectName: "calibration"
    }
}
