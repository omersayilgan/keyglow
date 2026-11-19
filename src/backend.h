#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "lighting.h"

class Keyboard;

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
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(bool serviceRunning READ serviceRunning NOTIFY serviceRunningChanged)

public:
    Backend(Keyboard *keyboard, const QString &statePath, QObject *parent = nullptr);

    QString mode() const { return Lighting::modeName(m_state.mode); }
    QStringList modes() const { return Lighting::modeNames(); }
    QColor color() const { return m_state.color; }
    int brightness() const { return Lighting::levelToPercent(m_state.brightness); }
    int speed() const { return m_state.speed; }
    bool on() const { return m_state.on; }
    bool usesColor() const { return Lighting::usesColor(m_state.mode); }
    bool animated() const { return Lighting::isAnimated(m_state.mode); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString problem() const { return m_problem; }
    bool serviceRunning() const { return m_serviceRunning; }
    Lighting::State state() const { return m_state; }

    Q_INVOKABLE void setMode(const QString &name);
    Q_INVOKABLE void setColor(const QColor &color);
    Q_INVOKABLE void setBrightness(int percent);
    Q_INVOKABLE void setSpeed(int speed);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void retry();

    // Pushes the pending change to the keyboard now instead of after the
    // short coalescing delay; tests use it, and so does quitting.
    void flush();

signals:
    void stateChanged();
    void historyChanged();
    void problemChanged();
    void serviceRunningChanged();

private:
    void change(const Lighting::State &next);
    void commit();
    void reloadFromDisk();
    void checkService();
    void setProblem(const QString &problem);

    Keyboard *m_keyboard;
    QString m_statePath;
    Lighting::State m_state;
    QList<Lighting::State> m_undo, m_redo;
    qint64 m_lastChange = 0;
    QTimer m_apply;
    QFileSystemWatcher m_watcher;
    QString m_problem;
    bool m_serviceRunning = true;
};
