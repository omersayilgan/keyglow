import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Saved looks as chips: click to switch, × to forget, and a button to save
// what the keyboard shows now.
ColumnLayout {
    id: bar
    spacing: 10

    function openSave() {
        nameField.text = backend.activeProfile
        saveDialog.open()
    }

    SectionLabel { text: qsTr("Profiles") }

    Flow {
        Layout.fillWidth: true
        spacing: 8

        Repeater {
            model: backend.profiles

            Rectangle {
                id: chip
                required property string modelData
                readonly property bool current: backend.activeProfile === modelData

                height: 34
                width: chipRow.implicitWidth + 24
                radius: theme.radius
                color: current ? theme.accent : chipArea.containsMouse ? "#1f1f24" : "#16161a"
                border.width: 1
                border.color: current ? theme.accent : "#26262b"

                MouseArea {
                    id: chipArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.loadProfile(chip.modelData)
                }

                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 8

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.modelData
                        font.bold: chip.current
                        color: chip.current ? theme.accentForeground : Material.foreground
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "×"
                        font.pixelSize: 16
                        color: chip.current ? theme.accentForeground : Material.foreground
                        opacity: removeArea.containsMouse ? 1 : 0.45

                        MouseArea {
                            id: removeArea
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                deleteDialog.name = chip.modelData
                                deleteDialog.open()
                            }
                        }
                    }
                }
            }
        }

        Button {
            height: 34
            topInset: 0
            bottomInset: 0
            flat: true
            focusPolicy: Qt.NoFocus
            text: qsTr("+ Save current")
            font.capitalization: Font.MixedCase
            Material.roundedScale: theme.radius
            onClicked: bar.openSave()
        }
    }

    Label {
        Layout.fillWidth: true
        visible: backend.profiles.length === 0
        text: qsTr("Save the current look to switch back to it later, or bind “keyglow profile next” to a key.")
        wrapMode: Text.Wrap
        opacity: 0.5
        font.pixelSize: 12
    }

    Popup {
        id: saveDialog
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - 48, 380)
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
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.65) }
        onAboutToShow: nameField.forceActiveFocus()

        readonly property string trimmed: nameField.text.trim()

        function accept() {
            if (trimmed !== "" && backend.saveProfile(trimmed))
                close()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 14

            Label {
                text: qsTr("Save profile")
                font.pixelSize: 18
                font.bold: true
            }
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                maximumLength: 40
                onAccepted: saveDialog.accept()
            }
            Label {
                Layout.fillWidth: true
                visible: saveDialog.trimmed !== "" && backend.hasProfile(saveDialog.trimmed)
                text: qsTr("Replaces the saved profile with this name.")
                wrapMode: Text.Wrap
                opacity: 0.6
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: saveDialog.close()
                }
                Button {
                    text: qsTr("Save")
                    highlighted: true
                    enabled: saveDialog.trimmed !== ""
                    onClicked: saveDialog.accept()
                }
            }
        }
    }

    Popup {
        id: deleteDialog
        property string name
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - 48, 380)
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
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.65) }

        ColumnLayout {
            anchors.fill: parent
            spacing: 14

            Label {
                Layout.fillWidth: true
                text: qsTr("Delete “%1”?").arg(deleteDialog.name)
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("The keyboard keeps its current lighting.")
                wrapMode: Text.Wrap
                opacity: 0.6
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: deleteDialog.close()
                }
                Button {
                    text: qsTr("Delete")
                    highlighted: true
                    Material.accent: "#e5484d"
                    onClicked: {
                        backend.deleteProfile(deleteDialog.name)
                        deleteDialog.close()
                    }
                }
            }
        }
    }
}
