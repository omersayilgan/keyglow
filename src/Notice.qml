import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A problem the user can act on, in the window rather than a log.
Rectangle {
    id: notice

    property color tone: "#e5484d"
    property string text
    property string action
    signal triggered

    implicitHeight: row.implicitHeight + 24
    radius: theme.radius
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.12)
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.5)

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: notice.text
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Button {
            visible: notice.action !== ""
            text: notice.action
            flat: true
            focusPolicy: Qt.NoFocus
            Material.roundedScale: theme.radius
            onClicked: notice.triggered()
        }
    }
}
