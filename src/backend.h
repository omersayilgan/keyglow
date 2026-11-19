#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "lighting.h"
#include "profiles.h"

class Keyboard;
class Theme;

// The window's state, exposed to QML as `backend`. Every change is saved
// first and then sent to the keyboard, so the background daemon always reads
// the state the keyboard is about to show.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode NOTIFY stateChanged)
    Q_PROPERTY(QStringList modes READ modes CONSTANT)
    Q_PROPERTY(QColor color READ color NOTIFY stateChanged)
    Q_PROPERTY(int brightness READ brightness NOTIFY stateChanged)
    Q_PROPERTY(int speed READ speed NOTIFY stateChanged)
    Q_PROPERTY(bool on READ on NOTIFY stateChanged)
    Q_PROPERTY(bool usesColor READ usesColor NOTIFY stateChanged)
    Q_PROPERTY(bool animated READ animated NOTIFY stateChanged)
    Q_PROPERTY(bool followTheme READ followTheme NOTIFY stateChanged)
    Q_PROPERTY(QString batterySaver READ batterySaver NOTIFY stateChanged)
    Q_PROPERTY(int batteryBrightness READ batteryBrightness NOTIFY stateChanged)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY onBatteryChanged)
    Q_PROPERTY(QVariantList trim READ trim NOTIFY stateChanged)
    Q_PROPERTY(bool trimmed READ trimmed NOTIFY stateChanged)
    // How much the whole keyboard is dimmed, in percent, to keep up with a
    // zone trimmed past its LEDs' full output; 0 when none is.
    Q_PROPERTY(int trimDimming READ trimDimming NOTIFY stateChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(bool serviceRunning READ serviceRunning NOTIFY serviceRunningChanged)

public:
    Backend(Keyboard *keyboard, const QString &statePath, const QString &profilesPath, Theme *theme = nullptr,
            QObject *parent = nullptr);

    QString mode() const { return Lighting::modeName(m_state.mode); }
    QStringList modes() const { return Lighting::modeNames(); }
    QColor color() const { return m_state.color; }
    int brightness() const { return Lighting::levelToPercent(m_state.brightness); }
    int speed() const { return m_state.speed; }
    bool on() const { return m_state.on; }
    bool usesColor() const { return Lighting::usesColor(m_state.mode); }
    bool animated() const { return Lighting::isAnimated(m_state.mode); }
    bool followTheme() const { return m_state.followTheme; }
    QString batterySaver() const { return Lighting::batterySaverName(m_state.batterySaver); }
    int batteryBrightness() const { return Lighting::levelToPercent(m_state.batteryBrightness); }
    bool onBattery() const { return m_onBattery; }
    QVariantList trim() const;
    bool trimmed() const { return !Lighting::isNeutral(m_state.trim); }
    int trimDimming() const { return qRound((1.0 - Lighting::trimScale(m_state.trim)) * 100); }
    Q_INVOKABLE int fullTrim(int channel) const { return Lighting::fullTrim(channel); }
    // Whether the battery saver is holding the light down right now.
    bool saving() const { return m_onBattery && m_state.batterySaver != Lighting::BatterySaver::None && m_state.on; }
    QStringList profiles() const { return Profiles::names(m_profiles); }
    QString activeProfile() const { return Profiles::active(m_profiles, m_state); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString problem() const { return m_problem; }
    bool serviceRunning() const { return m_serviceRunning; }
    Lighting::State state() const { return m_state; }

    Q_INVOKABLE void setMode(const QString &name);
    Q_INVOKABLE void setColor(const QColor &color);
    Q_INVOKABLE void setBrightness(int percent);
    Q_INVOKABLE void setSpeed(int speed);
    Q_INVOKABLE void setFollowTheme(bool follow);
    Q_INVOKABLE void setBatterySaver(const QString &name);
    Q_INVOKABLE void setBatteryBrightness(int percent);
    Q_INVOKABLE void setTrim(int zone, int channel, int percent);
    Q_INVOKABLE void resetTrim();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void retry();

    Q_INVOKABLE bool hasProfile(const QString &name) const;
    Q_INVOKABLE bool saveProfile(const QString &name);
    Q_INVOKABLE void loadProfile(const QString &name);
    Q_INVOKABLE void deleteProfile(const QString &name);
    Q_INVOKABLE void nextProfile();

    // Pushes the pending change to the keyboard now instead of after the
    // short coalescing delay; tests use it, and so does quitting.
    void flush();
    // Re-reads the charger; the window polls it while it is open.
    void refreshPower();

signals:
    void stateChanged();
    void historyChanged();
    void problemChanged();
    void serviceRunningChanged();
    void onBatteryChanged();
    void savingChanged();
    void profilesChanged();
    void activeProfileChanged();

private:
    void change(const Lighting::State &next);
    void replace(const Lighting::State &next);
    void commit();
    void reloadFromDisk();
    void watch();
    void followAccent();
    void checkService();
    void setProblem(const QString &problem);

    Keyboard *m_keyboard;
    QString m_statePath;
    QString m_profilesPath;
    Theme *m_theme;
    Lighting::State m_state;
    QList<Profiles::Profile> m_profiles;
    QList<Lighting::State> m_undo, m_redo;
    qint64 m_lastChange = 0;
    QTimer m_apply;
    QTimer m_power;
    QFileSystemWatcher m_watcher;
    QString m_problem;
    QString m_activeProfile;
    bool m_onBattery = false;
    bool m_serviceRunning = true;
};
