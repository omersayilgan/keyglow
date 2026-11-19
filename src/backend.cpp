#include "backend.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcess>

#include "keyboard.h"
#include "theme.h"

Backend::Backend(Keyboard *keyboard, const QString &statePath, const QString &profilesPath, Theme *theme,
                 QObject *parent)
    : QObject(parent), m_keyboard(keyboard), m_statePath(statePath), m_profilesPath(profilesPath), m_theme(theme),
      m_state(Lighting::load(statePath)), m_profiles(Profiles::load(profilesPath)) {
    // Dragging a slider changes the state many times a second; the keyboard
    // only needs the latest value, and the controller is slow to repaint.
    m_apply.setSingleShot(true);
    m_apply.setInterval(40);
    connect(&m_apply, &QTimer::timeout, this, &Backend::commit);

    // The daemon writes the state when Omarchy's brightness keys change the
    // level, and `keyglow set` or `keyglow profile` can change anything, so
    // follow both files. Saving renames over a file, which drops it from the
    // watch, so it is re-armed every time.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Backend::reloadFromDisk);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Backend::reloadFromDisk);
    watch();

    // The window shows when the battery saver is holding the light down.
    m_onBattery = m_keyboard->onBattery();
    m_power.setInterval(2000);
    connect(&m_power, &QTimer::timeout, this, &Backend::refreshPower);
    m_power.start();

    if (m_theme)
        connect(m_theme, &Theme::changed, this, &Backend::followAccent);

    connect(this, &Backend::stateChanged, this, [this] {
        const QString active = activeProfile();
        if (active != m_activeProfile) {
            m_activeProfile = active;
            emit activeProfileChanged();
        }
        emit savingChanged();
    });
    connect(this, &Backend::profilesChanged, this, &Backend::stateChanged);
    m_activeProfile = activeProfile();

    setProblem(m_keyboard->problem());
    checkService();
}

void Backend::setMode(const QString &name) {
    Lighting::State next = m_state;
    if (!Lighting::modeFromName(name, &next.mode))
        return;
    next.on = true;
    change(next);
}

void Backend::setColor(const QColor &color) {
    if (!color.isValid())
        return;
    Lighting::State next = m_state;
    next.color = Lighting::vivid(QColor(color.red(), color.green(), color.blue()));
    next.on = true;
    // A colour picked by hand replaces the theme's.
    next.followTheme = false;
    // Picking a colour while an effect without one runs means the user wants
    // to see that colour, so fall back to plain colour.
    if (!Lighting::usesColor(next.mode))
        next.mode = Lighting::Mode::Static;
    change(next);
}

void Backend::setBrightness(int percent) {
    Lighting::State next = m_state;
    next.brightness = Lighting::percentToLevel(percent);
    next.on = next.brightness > 0;
    if (next.brightness == 0)
        next.brightness = m_state.brightness;
    change(next);
}

void Backend::setSpeed(int speed) {
    Lighting::State next = m_state;
    next.speed = qBound(Lighting::MinSpeed, speed, Lighting::MaxSpeed);
    change(next);
}

void Backend::setFollowTheme(bool follow) {
    Lighting::State next = m_state;
    next.followTheme = follow;
    if (follow && m_theme) {
        next.color = Lighting::vivid(QColor(m_theme->accent()));
        next.on = true;
        if (!Lighting::usesColor(next.mode))
            next.mode = Lighting::Mode::Static;
    }
    change(next);
}

void Backend::setBatterySaver(const QString &name) {
    Lighting::State next = m_state;
    if (Lighting::batterySaverFromName(name, &next.batterySaver))
        change(next);
}

void Backend::setBatteryBrightness(int percent) {
    Lighting::State next = m_state;
    next.batteryBrightness = qMax(1, Lighting::percentToLevel(percent));
    change(next);
}

QVariantList Backend::trim() const {
    QVariantList zones;
    for (const Lighting::Trim &zone : m_state.trim)
        zones.append(QVariant(QVariantList{zone[0], zone[1], zone[2]}));
    return zones;
}

void Backend::setTrim(int zone, int channel, int percent) {
    if (zone < 0 || zone >= Lighting::Zones || channel < 0 || channel > 2)
        return;
    Lighting::State next = m_state;
    next.trim[zone][channel] = qBound(Lighting::MinTrim, percent, Lighting::MaxTrim);
    change(next);
}

void Backend::resetTrim() {
    Lighting::State next = m_state;
    next.trim.fill(Lighting::NeutralTrim);
    change(next);
}

void Backend::toggle() {
    Lighting::State next = m_state;
    next.on = !next.on;
    if (next.on && next.brightness == 0)
        next.brightness = Lighting::MaxBrightness / 2;
    change(next);
}

void Backend::change(const Lighting::State &next) {
    if (next == m_state)
        return;

    // A run of quick changes, such as a slider drag, is one step to undo.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_undo.isEmpty() || now - m_lastChange > 600)
        m_undo.append(m_state);
    m_lastChange = now;
    m_redo.clear();
    if (m_undo.size() > 100)
        m_undo.removeFirst();

    m_state = next;
    emit stateChanged();
    emit historyChanged();
    m_apply.start();
}

// A change that didn't come from the user (the theme moved on), so it isn't
// a step to undo.
void Backend::replace(const Lighting::State &next) {
    if (next == m_state)
        return;
    m_state = next;
    emit stateChanged();
    m_apply.start();
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_state);
    m_state = m_undo.takeLast();
    m_lastChange = 0;
    emit stateChanged();
    emit historyChanged();
    m_apply.start();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_state);
    m_state = m_redo.takeLast();
    m_lastChange = 0;
    emit stateChanged();
    emit historyChanged();
    m_apply.start();
}

void Backend::retry() {
    setProblem(m_keyboard->problem());
    checkService();
    if (m_problem.isEmpty())
        commit();
}

bool Backend::hasProfile(const QString &name) const { return Profiles::indexOf(m_profiles, name) >= 0; }

bool Backend::saveProfile(const QString &name) {
    if (name.trimmed().isEmpty())
        return false;
    QList<Profiles::Profile> next = m_profiles;
    Profiles::put(&next, name, m_state);
    QString error;
    if (!Profiles::save(next, m_profilesPath, &error)) {
        setProblem(tr("Couldn't save the profile: %1").arg(error));
        return false;
    }
    m_profiles = next;
    emit profilesChanged();
    return true;
}

void Backend::loadProfile(const QString &name) {
    const int index = Profiles::indexOf(m_profiles, name);
    if (index < 0)
        return;
    Lighting::State next = Profiles::applied(m_state, m_profiles[index]);
    if (next.followTheme && m_theme)
        next.color = QColor(m_theme->accent());
    change(next);
}

void Backend::deleteProfile(const QString &name) {
    QList<Profiles::Profile> next = m_profiles;
    if (!Profiles::remove(&next, name))
        return;
    QString error;
    if (!Profiles::save(next, m_profilesPath, &error)) {
        setProblem(tr("Couldn't delete the profile: %1").arg(error));
        return;
    }
    m_profiles = next;
    emit profilesChanged();
}

void Backend::nextProfile() {
    const QString name = Profiles::next(m_profiles, m_state);
    if (!name.isEmpty())
        loadProfile(name);
}

void Backend::flush() {
    if (m_apply.isActive()) {
        m_apply.stop();
        commit();
    }
}

void Backend::refreshPower() {
    const bool onBattery = m_keyboard->onBattery();
    if (onBattery == m_onBattery)
        return;
    m_onBattery = onBattery;
    emit onBatteryChanged();
    emit savingChanged();
}

void Backend::commit() {
    QString error;
    if (!Lighting::save(m_state, m_statePath, &error)) {
        setProblem(tr("Couldn't save the lighting: %1").arg(error));
        return;
    }
    if (!m_keyboard->apply(Lighting::effective(m_state, m_keyboard->onBattery()), &error)) {
        setProblem(error);
        return;
    }
    setProblem({});
}

void Backend::followAccent() {
    if (!m_state.followTheme || !m_theme)
        return;
    Lighting::State next = m_state;
    next.color = Lighting::vivid(QColor(m_theme->accent()));
    replace(next);
}

void Backend::watch() {
    for (const QString &path : {m_statePath, m_profilesPath}) {
        const QString directory = QFileInfo(path).absolutePath();
        if (QFileInfo::exists(directory) && !m_watcher.directories().contains(directory))
            m_watcher.addPath(directory);
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
            m_watcher.addPath(path);
    }
}

void Backend::reloadFromDisk() {
    watch();

    const QList<Profiles::Profile> profiles = Profiles::load(m_profilesPath);
    bool same = profiles.size() == m_profiles.size();
    for (int i = 0; same && i < profiles.size(); ++i)
        same = profiles[i].name == m_profiles[i].name && profiles[i].look == m_profiles[i].look;
    if (!same) {
        m_profiles = profiles;
        emit profilesChanged();
    }

    // A change still waiting to be sent wins over what is on disk.
    if (m_apply.isActive() || !QFileInfo::exists(m_statePath))
        return;
    const Lighting::State disk = Lighting::load(m_statePath);
    if (disk != m_state) {
        m_state = disk;
        emit stateChanged();
    }
}

void Backend::checkService() {
    QProcess systemctl;
    systemctl.start("systemctl", {"--user", "is-active", "--quiet", "keyglow.service"});
    const bool running = systemctl.waitForFinished(1500) && systemctl.exitStatus() == QProcess::NormalExit &&
                         systemctl.exitCode() == 0;
    if (running != m_serviceRunning) {
        m_serviceRunning = running;
        emit serviceRunningChanged();
    }
}

void Backend::setProblem(const QString &problem) {
    if (problem == m_problem)
        return;
    m_problem = problem;
    emit problemChanged();
}
