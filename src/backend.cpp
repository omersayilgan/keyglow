#include "backend.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcess>

#include "keyboard.h"

Backend::Backend(Keyboard *keyboard, const QString &statePath, QObject *parent)
    : QObject(parent), m_keyboard(keyboard), m_statePath(statePath), m_state(Lighting::load(statePath)) {
    // Dragging a slider changes the state many times a second; the keyboard
    // only needs the latest value, and the controller is slow to repaint.
    m_apply.setSingleShot(true);
    m_apply.setInterval(40);
    connect(&m_apply, &QTimer::timeout, this, &Backend::commit);

    // The daemon writes the state when Omarchy's brightness keys change the
    // level, and `keyglow set` can change anything, so follow the file.
    // Saving renames over it, which drops it from the watch; re-arm each time.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Backend::reloadFromDisk);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Backend::reloadFromDisk);
    reloadFromDisk();

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
    next.color = QColor(color.red(), color.green(), color.blue());
    next.on = true;
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

void Backend::flush() {
    if (m_apply.isActive()) {
        m_apply.stop();
        commit();
    }
}

void Backend::commit() {
    QString error;
    if (!Lighting::save(m_state, m_statePath, &error)) {
        setProblem(tr("Couldn't save the lighting: %1").arg(error));
        return;
    }
    if (!m_keyboard->apply(m_state, &error)) {
        setProblem(error);
        return;
    }
    setProblem({});
}

void Backend::reloadFromDisk() {
    const QString directory = QFileInfo(m_statePath).absolutePath();
    if (QFileInfo::exists(directory) && !m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
    if (QFileInfo::exists(m_statePath) && !m_watcher.files().contains(m_statePath))
        m_watcher.addPath(m_statePath);

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
