#include "daemon.h"

#include <QColor>
#include <QDBusConnection>
#include <QTextStream>

#include "keyboard.h"
#include "theme.h"

Daemon::Daemon(Keyboard *keyboard, const QString &statePath, Theme *theme, QObject *parent)
    : QObject(parent), m_keyboard(keyboard), m_statePath(statePath), m_theme(theme) {
    // Reading two small sysfs files costs next to nothing, and sysfs
    // brightness files don't report changes to inotify. A quarter second
    // keeps the driver's plain colour from showing for long after unlocking.
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, &Daemon::poll);
    if (m_theme)
        connect(m_theme, &Theme::changed, this, &Daemon::themeChanged);
}

void Daemon::start() {
    QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1",
                                         "org.freedesktop.login1.Manager", "PrepareForSleep", this,
                                         SLOT(prepareForSleep(bool)));
    m_onBattery = m_keyboard->onBattery();
    themeChanged();
    reapply();
    m_poll.start();
}

Lighting::State Daemon::shown(const Lighting::State &state) const {
    Lighting::State result = Lighting::effective(state, m_onBattery);
    if (m_onBattery && m_batteryOverride > 0) {
        result.on = true;
        result.brightness = m_batteryOverride;
    }
    return result;
}

void Daemon::poll() {
    const bool onBattery = m_keyboard->onBattery();
    if (onBattery != m_onBattery) {
        m_onBattery = onBattery;
        m_batteryOverride = -1;
        reapply();
        return;
    }

    const int brightness = m_keyboard->brightness();
    if (brightness == m_lastBrightness)
        return;
    const bool reappeared = m_lastBrightness == -1 && brightness >= 0;
    m_lastBrightness = brightness;

    // The driver came back (a module reload or the keyboard resetting), so
    // start it from the saved state.
    if (reappeared) {
        reapply();
        return;
    }

    // Zero is the lock screen blanking or the user turning the light off;
    // either way there is nothing to repaint until it comes back.
    if (brightness <= 0)
        return;

    Lighting::State state = Lighting::load(m_statePath);
    const Lighting::State expected = shown(state);

    // Any other level came from outside the app, so it is the user's choice.
    // On battery under the saver it holds until the power changes; otherwise
    // it becomes the saved level.
    if (!expected.on || brightness != expected.brightness) {
        if (m_onBattery && state.batterySaver != Lighting::BatterySaver::None) {
            m_batteryOverride = brightness;
        } else {
            state.brightness = brightness;
            state.on = true;
            Lighting::save(state, m_statePath);
        }
    }

    // The driver has just repainted in its own colour. That is already right
    // for a static colour it holds, and wrong for anything else.
    if (Lighting::isAnimated(state.mode) || m_keyboard->color() != state.color)
        reapply();
}

void Daemon::themeChanged() {
    if (!m_theme)
        return;
    Lighting::State state = Lighting::load(m_statePath);
    const QColor accent(m_theme->accent());
    if (!state.followTheme || !accent.isValid() || state.color.rgb() == accent.rgb())
        return;
    state.color = accent;
    Lighting::save(state, m_statePath);
    reapply();
}

void Daemon::prepareForSleep(bool sleeping) {
    // Give the USB controller a moment to come back before talking to it.
    if (!sleeping)
        QTimer::singleShot(1500, this, &Daemon::reapply);
}

void Daemon::reapply() {
    QString error;
    if (!m_keyboard->apply(shown(Lighting::load(m_statePath)), &error))
        QTextStream(stderr) << "keyglow: " << error << Qt::endl;
    // What was just set is not a change from outside.
    m_lastBrightness = m_keyboard->brightness();
}
