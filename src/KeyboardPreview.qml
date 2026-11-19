import QtQuick

// A drawing of the keyboard with its four zones lit the way the selected
// effect lights them. It approximates the controller's animation; the real
// timing comes from the keyboard itself.
Rectangle {
    id: preview

    property string mode: "static"
    property color baseColor: "white"
    property int brightness: 50
    property int speed: 5
    property bool on: true

    readonly property int columns: 16
    readonly property int rows: 5
    property real t: 0

    color: "#141417"
    border.color: "#26262b"
    border.width: 1
    radius: theme.radius
    implicitHeight: 150

    NumberAnimation on t {
        from: 0
        to: 1
        loops: Animation.Infinite
        running: preview.on && preview.mode !== "static" && preview.visible
        // Speed 1 is a slow drift, 10 a quick pulse.
        duration: 9000 - (preview.speed - 1) * 850
    }

    function fract(x) { return x - Math.floor(x) }

    function zoneColor(zone) {
        if (!on)
            return Qt.rgba(0, 0, 0, 0)
        const level = 0.2 + 0.8 * brightness / 100
        const c = baseColor
        switch (mode) {
        case "breathing": {
            const k = level * (0.12 + 0.88 * (0.5 - 0.5 * Math.cos(2 * Math.PI * t)))
            return Qt.rgba(c.r * k, c.g * k, c.b * k, 1)
        }
        case "wave":
            return Qt.hsva(fract(t - zone / 4), 1, level, 1)
        case "rainbow":
            return Qt.hsva(t, 1, level, 1)
        default:
            return Qt.rgba(c.r * level, c.g * level, c.b * level, 1)
        }
    }

    Grid {
        id: keys
        anchors.fill: parent
        anchors.margins: 14
        columns: preview.columns
        rows: preview.rows
        spacing: 5

        readonly property real keyWidth: (width - spacing * (columns - 1)) / columns
        readonly property real keyHeight: (height - spacing * (rows - 1)) / rows

        Repeater {
            model: preview.columns * preview.rows

            Rectangle {
                readonly property int zone: Math.floor((index % preview.columns) / (preview.columns / 4))
                readonly property color glow: preview.zoneColor(zone)
                // Read t so every key repaints as the effect moves.
                readonly property real tick: preview.t

                width: keys.keyWidth
                height: keys.keyHeight
                radius: Math.min(4, theme.radius)
                color: Qt.rgba(0.11 + glow.r * 0.55, 0.11 + glow.g * 0.55, 0.12 + glow.b * 0.55, 1)
                border.width: 1
                border.color: Qt.rgba(glow.r, glow.g, glow.b, preview.on ? 0.9 : 0)
            }
        }
    }
}
