#pragma once

#include <QObject>
#include <QTimer>

#include "lighting.h"

class Keyboard;

// Keeps the saved lighting on the keyboard. The driver repaints the keyboard
// in plain colour whenever its brightness changes (Omarchy's lock screen
// blanks and restores it, and Ctrl plus the screen brightness keys step it),
// and the controller can lose its effect over suspend, so the daemon watches
// for both and sends the effect again.
class Daemon : public QObject {
    Q_OBJECT

public:
    Daemon(Keyboard *keyboard, const QString &statePath, QObject *parent = nullptr);

    void start();
    // One poll; public so tests can step it without waiting.
    void poll();

private slots:
    void prepareForSleep(bool sleeping);

private:
    void reapply();

    Keyboard *m_keyboard;
    QString m_statePath;
    QTimer m_poll;
    int m_lastBrightness = -2;
};
