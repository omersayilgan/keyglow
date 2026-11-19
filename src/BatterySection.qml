import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What the light does while unplugged. The background service applies it
// when the charger comes and goes, so it works with the window closed.
ColumnLayout {
    id: section
    spacing: 10

    SectionLabel { text: qsTr("On battery") }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Repeater {
            model: [
                { name: "none", label: qsTr("Keep as is") },
                { name: "dim", label: qsTr("Dim") },
                { name: "off", label: qsTr("Turn off") },
            ]

            Button {
                id: choice
                required property var modelData
                readonly property bool current: backend.batterySaver === modelData.name

                Layout.fillWidth: true
                Layout.preferredHeight: 40
                topInset: 0
                bottomInset: 0
                focusPolicy: Qt.NoFocus
                onClicked: backend.setBatterySaver(modelData.name)

                background: Rectangle {
                    radius: theme.radius
                    color: choice.current ? theme.accent : choice.hovered ? "#1f1f24" : "#16161a"
                    border.width: 1
                    border.color: choice.current ? theme.accent : "#26262b"
                }
                contentItem: Label {
                    text: choice.modelData.label
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: choice.current
                    color: choice.current ? theme.accentForeground : Material.foreground
                    elide: Text.ElideRight
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: backend.batterySaver === "dim"
        spacing: 12

        Label { text: qsTr("At most"); opacity: 0.7 }
        Slider {
            id: dimSlider
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            from: 2
            to: 100
            stepSize: 2
            value: backend.batteryBrightness
            onMoved: backend.setBatteryBrightness(value)
        }
        Label {
            Layout.preferredWidth: 48
            horizontalAlignment: Text.AlignRight
            text: win.percent(dimSlider.value)
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        opacity: 0.55
        font.pixelSize: 12
        text: !backend.onBattery ? qsTr("Plugged in now.")
            : !backend.saving ? qsTr("On battery now.")
            : backend.batterySaver === "off" ? qsTr("On battery now, so the light is off until you plug in.")
            : qsTr("On battery now, so the light is held at %1.").arg(win.percent(Math.min(backend.brightness, backend.batteryBrightness)))
    }
}
