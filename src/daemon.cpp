#include "daemon.h"

#include <QDBusConnection>
#include <QTextStream>

#include "keyboard.h"

Daemon::Daemon(Keyboard *keyboard, const QString &statePath, QObject *parent)
    : QObject(parent), m_keyboard(keyboard), m_statePath(statePath) {
    // Reading one small sysfs file costs next to nothing, and sysfs
    // brightness files don't report changes to inotify. A quarter second
    // keeps the driver's plain colour from showing for long after unlocking.
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, &Daemon::poll);
}

void Daemon::start() {
    QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1",
                                         "org.freedesktop.login1.Manager", "PrepareForSleep", this,
                                         SLOT(prepareForSleep(bool)));
    reapply();
    m_lastBrightness = m_keyboard->brightness();
    m_poll.start();
}

void Daemon::poll() {
    const int brightness = m_keyboard->brightness();
    if (brightness == m_lastBrightness)
        return;
    const bool reappeared = m_lastBrightness == -1 && brightness >= 0;
    m_lastBrightness = brightness;

    // The driver came back (a module reload or the keyboard resetting), so
    // start it from the saved state.
    if (reappeared) {
        reapply();
        m_lastBrightness = m_keyboard->brightness();
        return;
    }

    // Zero is the lock screen blanking or the user turning the light off;
    // either way there is nothing to repaint until it comes back.
    if (brightness <= 0)
        return;

    // A new level from outside the app is the user's choice, so remember it
    // and repaint the effect at that level.
    Lighting::State state = Lighting::load(m_statePath);
    if (state.brightness != brightness || !state.on) {
        state.brightness = brightness;
        state.on = true;
        Lighting::save(state, m_statePath);
    }
    // The driver has just repainted in its own colour. That is already right
    // for a static colour it holds, and wrong for anything else.
    if (Lighting::isAnimated(state.mode) || m_keyboard->color() != state.color)
        reapply();
}

void Daemon::prepareForSleep(bool sleeping) {
    // Give the USB controller a moment to come back before talking to it.
    if (!sleeping)
        QTimer::singleShot(1500, this, &Daemon::reapply);
}

void Daemon::reapply() {
    QString error;
    if (!m_keyboard->apply(Lighting::load(m_statePath), &error))
        QTextStream(stderr) << "keyglow: " << error << Qt::endl;
}
