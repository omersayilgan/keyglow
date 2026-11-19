import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Matches the four LED zones by eye. The zones' LEDs aren't equally strong,
// so a mixed colour like orange drifts toward red in some zones and yellow
// in others. Matching each zone on pure red, green, and blue in turn evens
// out every LED on its own, and the correction then carries over to every
// mixed colour and effect.
Popup {
    id: panel
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.max(16, Math.round((parent.height - height) / 2))
    width: Math.min(parent.width - 32, 540)
    height: Math.min(parent.height - 32, content.implicitHeight + 48)
    modal: true
    focus: true
    padding: 24

    // Popups live on the window's overlay layer and don't inherit its
    // Material settings, so they are set again here.
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    background: Rectangle {
        radius: theme.radius
        color: "#18181b"
        border.color: "#2a2a30"
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }

    readonly property var zoneNames: [qsTr("Zone 1"), qsTr("Zone 2"), qsTr("Zone 3"), qsTr("Zone 4")]
    readonly property var channelNames: [qsTr("Red"), qsTr("Green"), qsTr("Blue")]
    readonly property var channelColors: ["#ff5a5a", "#3ddc63", "#5a8cff"]
    // Pure primaries: each lights one LED per zone, so only that channel's
    // sliders matter and the zones can be matched on brightness alone.
    readonly property var testColors: ["#ff0000", "#00ff00", "#0000ff"]
    // The channel the shown test colour lights, or -1 for any other colour.
    readonly property int testChannel: testColors.indexOf(backend.color.toString().toLowerCase())

    // Mismatched zones show best on a plain mixed colour.
    onAboutToShow: {
        if (backend.mode !== "static")
            backend.setMode("static")
        if (testChannel < 0)
            backend.setColor(testColors[0])
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 14

            Label {
                text: qsTr("Calibrate zones")
                font.pixelSize: 18
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: qsTr("Go through red, green, and blue in turn. For each, slide that colour in every zone until all four zones are equally bright; the other sliders don't change it. Moving a slider shows which zone it is.")
            }

            RowLayout {
                spacing: 10
                Label { text: qsTr("Test with"); opacity: 0.7 }
                Repeater {
                    model: panel.testColors
                    Rectangle {
                        required property string modelData
                        readonly property bool current: backend.color.toString().toLowerCase() === modelData
                        width: 28
                        height: 28
                        radius: 14
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

            Repeater {
                model: 4

                ColumnLayout {
                    id: zoneRow
                    required property int index
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: panel.zoneNames[zoneRow.index]
                        font.bold: true
                        Layout.topMargin: 6
                    }

                    Repeater {
                        model: 3

                        RowLayout {
                            id: channelRow
                            required property int index
                            Layout.fillWidth: true
                            spacing: 10
                            // The sliders the test colour doesn't light step back.
                            opacity: panel.testChannel < 0 || panel.testChannel === index ? 1 : 0.35

                            Label {
                                Layout.preferredWidth: 52
                                text: panel.channelNames[channelRow.index]
                                opacity: 0.7
                            }
                            Slider {
                                id: trimSlider
                                Layout.fillWidth: true
                                focusPolicy: Qt.NoFocus
                                Material.accent: panel.channelColors[channelRow.index]
                                from: 0
                                to: 200
                                stepSize: 1
                                value: backend.trim[zoneRow.index][channelRow.index]
                                onMoved: backend.setTrim(zoneRow.index, channelRow.index, value)
                            }
                            Label {
                                Layout.preferredWidth: 48
                                horizontalAlignment: Text.AlignRight
                                text: win.percent(trimSlider.value)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Button {
                    text: qsTr("Reset")
                    flat: true
                    enabled: backend.trimmed
                    onClicked: backend.resetTrim()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Done")
                    highlighted: true
                    onClicked: panel.close()
                }
            }
        }
    }
}
