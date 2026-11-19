#pragma once

#include <QObject>
#include <QTimer>

#include "lighting.h"

class Keyboard;
class Theme;

// Keeps the saved lighting on the keyboard while the window is closed.
//
// The driver repaints the keyboard in plain colour whenever its brightness
// changes (Omarchy's lock screen blanks and restores it, and Ctrl plus the
// screen brightness keys step it), and the controller can lose its effect
// over suspend, so the daemon watches for both and sends the effect again.
// It also applies the battery saver when the charger comes and goes, and
// takes the new accent when the Omarchy theme changes.
class Daemon : public QObject {
    Q_OBJECT

public:
    Daemon(Keyboard *keyboard, const QString &statePath, Theme *theme = nullptr, QObject *parent = nullptr);

    void start();
    // One poll; public so tests can step it without waiting.
    void poll();
    void themeChanged();

private slots:
    void prepareForSleep(bool sleeping);

private:
    Lighting::State shown(const Lighting::State &state) const;
    void reapply();

    Keyboard *m_keyboard;
    QString m_statePath;
    Theme *m_theme;
    QTimer m_poll;
    int m_lastBrightness = -2;
    bool m_onBattery = false;
    // A level the user picked by hand while the battery saver holds the
    // light down; it lasts until the charger is plugged in or pulled again.
    int m_batteryOverride = -1;
};
