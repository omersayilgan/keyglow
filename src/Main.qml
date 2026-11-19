import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 580
    height: 820
    minimumWidth: 480
    minimumHeight: 640
    visible: true
    title: "Keyglow"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    readonly property var presets: ["#ffffff", "#ff2d2d", "#ff8a00", "#ffd60a", "#2bd94a",
                                    "#00d5ff", "#2f6bff", "#9b4dff", "#ff3fb4"]

    function modeLabel(name) {
        switch (name) {
        case "static": return qsTr("Static")
        case "breathing": return qsTr("Breathing")
        case "wave": return qsTr("Wave")
        case "rainbow": return qsTr("Rainbow")
        case "random": return qsTr("Random")
        case "ripple": return qsTr("Ripple")
        }
        return name
    }

    function modeHint(name) {
        switch (name) {
        case "static": return qsTr("One colour")
        case "breathing": return qsTr("Slow pulse")
        case "wave": return qsTr("Colours sweep across")
        case "rainbow": return qsTr("All keys cycle hues")
        case "random": return qsTr("Zones change colour")
        case "ripple": return qsTr("Lights up as you type")
        }
        return ""
    }

    function percent(value) {
        return qsTr("%1%").arg(Number(value).toLocaleString(Qt.locale(), 'f', 0))
    }

    function nextPreset() {
        const current = backend.color.toString().toLowerCase()
        const i = presets.indexOf(current)
        backend.setColor(presets[(i + 1) % presets.length])
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.toggle() }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; onActivated: backend.setBrightness(Math.max(2, backend.brightness - 10)) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: backend.setBrightness(Math.min(100, backend.brightness + 10)) }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; onActivated: backend.setSpeed(backend.speed - 1) }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; onActivated: backend.setSpeed(backend.speed + 1) }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; onActivated: win.nextPreset() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Repeater {
        model: backend.modes
        Item {
            Shortcut {
                sequence: String(index + 1)
                context: Qt.ApplicationShortcut
                onActivated: backend.setMode(modelData)
            }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // Hyprland tiles the window to whatever space is free, so the
        // content scrolls rather than being cut off in a small tile.
        ScrollView {
            id: scroller
            anchors.fill: parent
            padding: 24
            contentWidth: availableWidth
            clip: true

        ColumnLayout {
            width: scroller.availableWidth
            spacing: 18

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Label {
                    text: "Keyglow"
                    font.pixelSize: 24
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: backend.on ? qsTr("On") : qsTr("Off")
                    opacity: 0.7
                }
                Switch {
                    checked: backend.on
                    focusPolicy: Qt.NoFocus
                    onToggled: backend.toggle()
                }
            }

            KeyboardPreview {
                Layout.fillWidth: true
                mode: backend.mode
                baseColor: backend.usesColor ? backend.color : "white"
                brightness: backend.brightness
                speed: backend.speed
                on: backend.on
            }

            SectionLabel { text: qsTr("Effect") }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                rowSpacing: 8
                columnSpacing: 8

                Repeater {
                    model: backend.modes

                    Button {
                        id: modeButton
                        required property string modelData
                        required property int index
                        readonly property bool current: backend.mode === modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        focusPolicy: Qt.NoFocus
                        onClicked: backend.setMode(modelData)

                        background: Rectangle {
                            radius: theme.radius
                            color: modeButton.current ? theme.accent
                                 : modeButton.hovered ? "#1f1f24" : "#16161a"
                            border.width: 1
                            border.color: modeButton.current ? theme.accent : "#26262b"
                        }

                        contentItem: ColumnLayout {
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: win.modeLabel(modeButton.modelData)
                                font.bold: true
                                font.capitalization: Font.MixedCase
                                horizontalAlignment: Text.AlignHCenter
                                color: modeButton.current ? theme.accentForeground : Material.foreground
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: win.modeHint(modeButton.modelData)
                                font.pixelSize: 11
                                font.capitalization: Font.MixedCase
                                horizontalAlignment: Text.AlignHCenter
                                color: modeButton.current ? theme.accentForeground : Material.foreground
                                opacity: 0.7
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            ColorPicker {
                Layout.fillWidth: true
                visible: backend.usesColor
                presets: win.presets
            }

            SectionLabel { text: qsTr("Brightness") }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Slider {
                    id: brightnessSlider
                    Layout.fillWidth: true
                    focusPolicy: Qt.NoFocus
                    from: 0
                    to: 100
                    stepSize: 2
                    value: backend.on ? backend.brightness : 0
                    onMoved: backend.setBrightness(value)
                }
                Label {
                    Layout.preferredWidth: 48
                    horizontalAlignment: Text.AlignRight
                    text: win.percent(brightnessSlider.value)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: backend.animated
                spacing: 4

                SectionLabel { text: qsTr("Speed") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label { text: qsTr("Slow"); opacity: 0.6 }
                    Slider {
                        Layout.fillWidth: true
                        focusPolicy: Qt.NoFocus
                        from: 1
                        to: 10
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: backend.speed
                        onMoved: backend.setSpeed(value)
                    }
                    Label { text: qsTr("Fast"); opacity: 0.6 }
                }
            }

            Notice {
                Layout.fillWidth: true
                visible: backend.problem !== ""
                tone: "#e5484d"
                text: backend.problem
                action: qsTr("Retry")
                onTriggered: backend.retry()
            }

            Notice {
                Layout.fillWidth: true
                visible: backend.problem === "" && !backend.serviceRunning
                tone: "#f5a524"
                text: qsTr("The keyglow service isn't running, so effects go back to plain colour after the lock screen. Turn it on with: systemctl --user enable --now keyglow")
                action: qsTr("Check again")
                onTriggered: backend.retry()
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Press ? for shortcuts")
                opacity: 0.4
                font.pixelSize: 12
            }
        }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClosed: win.helpVisible = false
        }
    }
}
