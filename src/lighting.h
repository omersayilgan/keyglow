#pragma once

#include <array>

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// The lighting state and the ITE 8291 (rev 0.02, four-zone) protocol, kept
// free of any device access so it can be tested on its own.
namespace Lighting {

enum class Mode { Static, Breathing, Wave, Rainbow };

// What happens to the light while the laptop runs on battery.
enum class BatterySaver { None, Dim, Off };

// The controller's own brightness scale; the driver exposes the same range.
constexpr int MaxBrightness = 50;
constexpr int MinSpeed = 1;
constexpr int MaxSpeed = 10;
constexpr int Zones = 4;

// Per-zone correction, in percent of red, green, and blue. The four LED
// zones aren't matched: a mixed colour like orange drifts toward red in a
// zone with weak green LEDs and toward yellow in one with strong ones.
using Trim = std::array<int, 3>;
constexpr int MinTrim = 0;
constexpr int MaxTrim = 200;
constexpr Trim NeutralTrim{100, 100, 100};

struct State {
    Mode mode = Mode::Static;
    QColor color = QColor(255, 255, 255);
    int brightness = 25;   // 0..MaxBrightness
    int speed = 5;         // MinSpeed (slow) .. MaxSpeed (fast)
    bool on = true;
    // Take the Omarchy theme's accent as the colour, and keep taking it.
    bool followTheme = false;
    BatterySaver batterySaver = BatterySaver::None;
    int batteryBrightness = 10;   // the most a Dim saver allows, 0..MaxBrightness
    std::array<Trim, Zones> trim{NeutralTrim, NeutralTrim, NeutralTrim, NeutralTrim};

    bool operator==(const State &other) const;
    bool operator!=(const State &other) const { return !(*this == other); }
};

QString modeName(Mode mode);
QString batterySaverName(BatterySaver saver);
bool batterySaverFromName(const QString &name, BatterySaver *saver);
bool modeFromName(const QString &name, Mode *mode);
QStringList modeNames();

// Static and breathing show the chosen colour; the others bring their own
// palette.
bool usesColor(Mode mode);
bool isAnimated(Mode mode);

// The driver applies this white balance to every colour it sends, so the
// colours the app sends straight to the controller are scaled the same way
// and an effect matches the static colour it was picked from.
QColor whiteBalanced(const QColor &color);
// Every colour the keyboard shows is at full saturation and value: washed-out
// colours light all three LEDs, so the zones' mismatch shows as a tint, and
// a dim one only wastes range the brightness setting already covers. Greys,
// which have no hue, become white.
QColor vivid(const QColor &color);
// The colour to send to one zone: white-balanced, then trimmed for that zone.
QColor calibrated(const QColor &color, const Trim &trim);
bool isNeutral(const std::array<Trim, Zones> &trim);

// The state the keyboard should show right now: the saved one, with the
// battery saver applied while unplugged.
State effective(const State &state, bool onBattery);

// Feature reports that start an effect: four palette slots, then the effect.
QList<QByteArray> effectPackets(const State &state);
quint8 speedByte(int speed);

QJsonObject toJson(const State &state);
State fromJson(const QJsonObject &object);

QString defaultStatePath();
State load(const QString &path);
bool save(const State &state, const QString &path, QString *error = nullptr);

int percentToLevel(int percent);
int levelToPercent(int level);

} // namespace Lighting
