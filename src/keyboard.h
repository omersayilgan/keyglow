#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

#include "lighting.h"

// The keyboard as the ite_8291 driver exposes it: four rgb:kbd_backlight LED
// zones in sysfs for brightness and static colour, and the controller's
// hidraw node for the effects the driver has no interface for.
class Keyboard {
public:
    explicit Keyboard(const QString &ledRoot = QStringLiteral("/sys/class/leds"),
                      const QString &powerRoot = QStringLiteral("/sys/class/power_supply"));
    virtual ~Keyboard() = default;

    QStringList zones() const;
    bool present() const { return !zones().isEmpty(); }
    QString hidrawPath() const;

    // -1 when the zone can't be read.
    int brightness() const;
    // True when there is a mains supply and it is unplugged. Machines
    // without one (desktops) are never on battery.
    bool onBattery() const;
    QColor color() const;

    bool setBrightness(int level, QString *error = nullptr);
    bool setColor(const QColor &color, QString *error = nullptr);
    bool apply(const Lighting::State &state, QString *error = nullptr);

    // A sentence for the window when the keyboard can't be driven, or empty.
    QString problem() const;

protected:
    virtual bool sendFeatures(const QList<QByteArray> &packets, QString *error);
    virtual bool writeZoneFile(const QString &zone, const QString &file, const QByteArray &value, QString *error);

private:
    QString m_root;
    QString m_powerRoot;
};
