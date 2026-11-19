#include "lighting.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtMath>

namespace Lighting {

namespace {

struct ModeInfo {
    Mode mode;
    const char *name;
    quint8 effect;
};

// Effect codes confirmed on the keyboard itself. Random (0x04) and ripple
// (0x06) answer but don't show properly on rev 0.02, and marquee, raindrop,
// aurora, and fireworks, listed for other ITE 8291 revisions, do nothing.
constexpr ModeInfo modes[] = {
    {Mode::Static, "static", 0x01},
    {Mode::Breathing, "breathing", 0x02},
    {Mode::Wave, "wave", 0x03},
    {Mode::Rainbow, "rainbow", 0x05},
};

const ModeInfo &info(Mode mode) {
    for (const auto &m : modes)
        if (m.mode == mode)
            return m;
    return modes[0];
}

QByteArray packet(std::initializer_list<int> bytes) {
    QByteArray data;
    for (int b : bytes)
        data.append(char(quint8(b)));
    return data;
}

} // namespace

bool State::operator==(const State &other) const {
    return mode == other.mode && color.rgb() == other.color.rgb() && brightness == other.brightness &&
           speed == other.speed && on == other.on && followTheme == other.followTheme &&
           batterySaver == other.batterySaver && batteryBrightness == other.batteryBrightness &&
           trim == other.trim;
}

QString modeName(Mode mode) { return QString::fromLatin1(info(mode).name); }

bool modeFromName(const QString &name, Mode *mode) {
    for (const auto &m : modes) {
        if (name.compare(QLatin1String(m.name), Qt::CaseInsensitive) == 0) {
            *mode = m.mode;
            return true;
        }
    }
    return false;
}

QString batterySaverName(BatterySaver saver) {
    switch (saver) {
    case BatterySaver::Dim: return "dim";
    case BatterySaver::Off: return "off";
    default: return "none";
    }
}

bool batterySaverFromName(const QString &name, BatterySaver *saver) {
    for (BatterySaver s : {BatterySaver::None, BatterySaver::Dim, BatterySaver::Off}) {
        if (name.compare(batterySaverName(s), Qt::CaseInsensitive) == 0) {
            *saver = s;
            return true;
        }
    }
    return false;
}

State effective(const State &state, bool onBattery) {
    State shown = state;
    if (!onBattery)
        return shown;
    if (state.batterySaver == BatterySaver::Off)
        shown.on = false;
    else if (state.batterySaver == BatterySaver::Dim)
        shown.brightness = qMin(state.brightness, qMax(1, state.batteryBrightness));
    return shown;
}

QStringList modeNames() {
    QStringList names;
    for (const auto &m : modes)
        names << QString::fromLatin1(m.name);
    return names;
}

bool usesColor(Mode mode) { return mode == Mode::Static || mode == Mode::Breathing; }

bool isAnimated(Mode mode) { return mode != Mode::Static; }

QColor whiteBalanced(const QColor &color) {
    return QColor(color.red(), color.green() * 126 / 255, color.blue() * 120 / 255);
}

QColor vivid(const QColor &color) {
    if (!color.isValid())
        return color;
    const int hue = color.hsvHue();
    if (hue < 0)
        return QColor(255, 255, 255);
    // Rebuilt from 8-bit channels, so it compares equal to the same hex.
    const QColor full = QColor::fromHsv(hue, 255, 255);
    return QColor(full.red(), full.green(), full.blue());
}

QColor calibrated(const QColor &color, const Trim &trim) {
    const QColor balanced = whiteBalanced(color);
    auto scale = [](int value, int percent) { return qBound(0, value * percent / 100, 255); };
    return QColor(scale(balanced.red(), trim[0]), scale(balanced.green(), trim[1]), scale(balanced.blue(), trim[2]));
}

bool isNeutral(const std::array<Trim, Zones> &trim) {
    for (const Trim &zone : trim)
        if (zone != NeutralTrim)
            return false;
    return true;
}

// The controller counts speed the other way round (checked on the keyboard:
// a higher byte is a slower animation), so the app's slow-to-fast scale is
// flipped here.
quint8 speedByte(int speed) {
    return quint8(MaxSpeed + MinSpeed - qBound(MinSpeed, speed, MaxSpeed));
}

QList<QByteArray> effectPackets(const State &state) {
    QList<QByteArray> packets;

    // Colour effects get the one colour in every slot. The others get a
    // spread of hues, which the wave draws from.
    static const QColor spread[Zones] = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255),
                                         QColor(255, 0, 255)};
    for (int slot = 0; slot < Zones; ++slot) {
        // Slot n paints zone n, so each slot gets its zone's trim.
        const QColor color = calibrated(usesColor(state.mode) ? state.color : spread[slot], state.trim[slot]);
        packets << packet({0x14, 0x00, slot + 1, color.red(), color.green(), color.blue(), 0x00, 0x00});
    }

    // The last byte would save the effect to the controller's own memory;
    // it stays 0 so the keyboard always comes back to what the driver sets.
    const int level = state.on ? qBound(0, state.brightness, MaxBrightness) : 0;
    const int direction = state.mode == Mode::Wave ? 0x01 : 0x00;
    // Static has no speed, and other values in that byte pick multi-colour
    // static patterns, so it gets the 0x03 the driver itself sends.
    const int speed = state.mode == Mode::Static ? 0x03 : speedByte(state.speed);
    packets << packet({0x08, 0x02, info(state.mode).effect, speed, level, 0x08, direction, 0x00});
    return packets;
}

QJsonObject toJson(const State &state) {
    return QJsonObject{
        {"mode", modeName(state.mode)},
        {"color", state.color.name()},
        {"brightness", levelToPercent(state.brightness)},
        {"speed", state.speed},
        {"on", state.on},
        {"followTheme", state.followTheme},
        {"batterySaver", batterySaverName(state.batterySaver)},
        {"batteryBrightness", levelToPercent(state.batteryBrightness)},
        {"zoneTrim", [&] {
             QJsonArray zones;
             for (const Trim &zone : state.trim)
                 zones.append(QJsonArray{zone[0], zone[1], zone[2]});
             return zones;
         }()},
    };
}

State fromJson(const QJsonObject &object) {
    State state;
    Mode mode;
    if (modeFromName(object.value("mode").toString(), &mode))
        state.mode = mode;
    const QColor color(object.value("color").toString());
    if (color.isValid())
        state.color = vivid(color);
    if (object.value("brightness").isDouble())
        state.brightness = percentToLevel(object.value("brightness").toInt());
    if (object.value("speed").isDouble())
        state.speed = qBound(MinSpeed, object.value("speed").toInt(), MaxSpeed);
    if (object.value("on").isBool())
        state.on = object.value("on").toBool();
    state.followTheme = object.value("followTheme").toBool(false);
    BatterySaver saver;
    if (batterySaverFromName(object.value("batterySaver").toString(), &saver))
        state.batterySaver = saver;
    if (object.value("batteryBrightness").isDouble())
        state.batteryBrightness = percentToLevel(object.value("batteryBrightness").toInt());
    const QJsonArray zones = object.value("zoneTrim").toArray();
    for (int zone = 0; zone < Zones && zone < zones.size(); ++zone) {
        const QJsonArray channels = zones[zone].toArray();
        if (channels.size() != 3)
            continue;
        for (int channel = 0; channel < 3; ++channel)
            state.trim[zone][channel] = qBound(MinTrim, channels[channel].toInt(100), MaxTrim);
    }
    return state;
}

QString defaultStatePath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/keyglow/state.json";
}

State load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return fromJson(QJsonDocument::fromJson(file.readAll()).object());
}

// Written beside the target and renamed over it, so a reader never sees half
// a file.
bool save(const State &state, const QString &path, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson(state)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

int percentToLevel(int percent) { return qRound(qBound(0, percent, 100) * MaxBrightness / 100.0); }

int levelToPercent(int level) { return qRound(qBound(0, level, MaxBrightness) * 100.0 / MaxBrightness); }

} // namespace Lighting
