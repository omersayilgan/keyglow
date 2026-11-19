#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// The lighting state and the ITE 8291 (rev 0.02, four-zone) protocol, kept
// free of any device access so it can be tested on its own.
namespace Lighting {

enum class Mode { Static, Breathing, Wave, Rainbow, Random, Ripple };

// The controller's own brightness scale; the driver exposes the same range.
constexpr int MaxBrightness = 50;
constexpr int MinSpeed = 1;
constexpr int MaxSpeed = 10;
constexpr int Zones = 4;

struct State {
    Mode mode = Mode::Static;
    QColor color = QColor(255, 255, 255);
    int brightness = 25;   // 0..MaxBrightness
    int speed = 5;         // MinSpeed (slow) .. MaxSpeed (fast)
    bool on = true;

    bool operator==(const State &other) const;
    bool operator!=(const State &other) const { return !(*this == other); }
};

QString modeName(Mode mode);
bool modeFromName(const QString &name, Mode *mode);
QStringList modeNames();

// Static, breathing, and ripple show the chosen colour; the others bring
// their own palette.
bool usesColor(Mode mode);
bool isAnimated(Mode mode);

// The driver applies this white balance to every colour it sends, so the
// colours the app sends straight to the controller are scaled the same way
// and an effect matches the static colour it was picked from.
QColor whiteBalanced(const QColor &color);

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
