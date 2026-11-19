import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: overlay
    signal closed

    color: Qt.rgba(0, 0, 0, 0.7)

    readonly property var shortcuts: [
        ["1 – 6", qsTr("Pick an effect")],
        [qsTr("Space"), qsTr("Turn the light on or off")],
        ["← →", qsTr("Brightness down or up")],
        ["[ ]", qsTr("Slower or faster")],
        ["C", qsTr("Next preset colour")],
        ["T", qsTr("Match the Omarchy theme on or off")],
        ["P", qsTr("Next profile")],
        ["Ctrl+S", qsTr("Save the current look as a profile")],
        ["Ctrl+Z", qsTr("Undo")],
        ["Ctrl+Shift+Z", qsTr("Redo")],
        ["?", qsTr("Show or hide shortcuts")],
        ["Q", qsTr("Quit")],
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.closed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        height: list.implicitHeight + 48
        radius: theme.radius
        color: "#18181b"
        border.color: "#2a2a30"

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 24
            spacing: 10

            Label {
                text: qsTr("Shortcuts")
                font.pixelSize: 18
                font.bold: true
                Layout.bottomMargin: 6
            }

            Repeater {
                model: overlay.shortcuts
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 16
                    Rectangle {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 26
                        radius: Math.min(4, theme.radius)
                        color: "#26262b"
                        Label {
                            anchors.centerIn: parent
                            text: modelData[0]
                            font.family: "monospace"
                            font.pixelSize: 12
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData[1]
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
