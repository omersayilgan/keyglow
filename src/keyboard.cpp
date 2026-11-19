#include "keyboard.h"

#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

QString tr(const char *text) { return QCoreApplication::translate("Keyboard", text); }

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

} // namespace

Keyboard::Keyboard(const QString &ledRoot, const QString &powerRoot) : m_root(ledRoot), m_powerRoot(powerRoot) {}

bool Keyboard::onBattery() const {
    bool sawMains = false;
    const QStringList supplies = QDir(m_powerRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    for (const QString &supply : supplies) {
        if (readFile(m_powerRoot + "/" + supply + "/type") != "Mains")
            continue;
        sawMains = true;
        if (readFile(m_powerRoot + "/" + supply + "/online") == "1")
            return false;
    }
    return sawMains;
}

// The driver names the zones rgb:kbd_backlight, then _1, _2, _3 as each one
// collides with the first, so the plain name is always zone one.
QStringList Keyboard::zones() const {
    QStringList zones = QDir(m_root).entryList({"rgb:kbd_backlight*"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    std::sort(zones.begin(), zones.end(), [](const QString &a, const QString &b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return zones;
}

QString Keyboard::hidrawPath() const {
    const QStringList list = zones();
    if (list.isEmpty())
        return {};
    const QStringList nodes = QDir(m_root + "/" + list.first() + "/device/hidraw")
                                  .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    return nodes.isEmpty() ? QString() : "/dev/" + nodes.first();
}

int Keyboard::brightness() const {
    const QStringList list = zones();
    if (list.isEmpty())
        return -1;
    bool ok = false;
    const int value = readFile(m_root + "/" + list.first() + "/brightness").toInt(&ok);
    return ok ? value : -1;
}

QColor Keyboard::color() const {
    const QStringList list = zones();
    if (list.isEmpty())
        return {};
    const auto parts = readFile(m_root + "/" + list.first() + "/multi_intensity").split(' ');
    return parts.size() == 3 ? QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt()) : QColor();
}

bool Keyboard::writeZoneFile(const QString &zone, const QString &name, const QByteArray &value, QString *error) {
    QFile file(m_root + "/" + zone + "/" + name);
    if (file.open(QIODevice::WriteOnly) && file.write(value) == value.size())
        return true;

    // Brightness can also go through logind, which lets the active session
    // set it without any permission on the file, as brightnessctl does.
    if (name == "brightness" && m_root == QLatin1String("/sys/class/leds")) {
        QDBusInterface session("org.freedesktop.login1", "/org/freedesktop/login1/session/auto",
                               "org.freedesktop.login1.Session", QDBusConnection::systemBus());
        QDBusReply<void> reply = session.call("SetBrightness", "leds", zone, value.toUInt());
        if (reply.isValid())
            return true;
    }

    if (error)
        *error = tr("Couldn't write %1 for %2: %3").arg(name, zone, file.errorString());
    return false;
}

bool Keyboard::setBrightness(int level, QString *error) {
    const QByteArray value = QByteArray::number(qBound(0, level, Lighting::MaxBrightness));
    for (const QString &zone : zones())
        if (!writeZoneFile(zone, "brightness", value, error))
            return false;
    return true;
}

bool Keyboard::setColor(const QColor &color, QString *error) {
    const QByteArray value = QByteArray::number(color.red()) + ' ' + QByteArray::number(color.green()) + ' ' +
                             QByteArray::number(color.blue());
    for (const QString &zone : zones())
        if (!writeZoneFile(zone, "multi_intensity", value, error))
            return false;
    return true;
}

bool Keyboard::sendFeatures(const QList<QByteArray> &packets, QString *error) {
    const QString path = hidrawPath();
    if (path.isEmpty()) {
        if (error)
            *error = tr("The keyboard controller has no hidraw device.");
        return false;
    }
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        if (error)
            *error = errno == EACCES ? tr("No permission to drive %1. Log out and back in after installing keyglow.").arg(path)
                                     : tr("Couldn't open %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    for (const QByteArray &packet : packets) {
        QByteArray buffer = packet;
        if (::ioctl(fd, HIDIOCSFEATURE(buffer.size()), buffer.data()) < 0) {
            if (error)
                *error = tr("The keyboard refused a command: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
            ::close(fd);
            return false;
        }
    }
    ::close(fd);
    return true;
}

// Static colour and brightness go through the driver, which keeps them
// across lock, suspend, and Omarchy's brightness keys on its own. An effect
// has to be sent to the controller after that, because any write the driver
// makes switches the keyboard back to static colour.
bool Keyboard::apply(const Lighting::State &state, QString *error) {
    if (!present()) {
        if (error)
            *error = problem();
        return false;
    }

    if (!state.on)
        return brightness() == 0 || setBrightness(0, error);

    if (brightness() != state.brightness && !setBrightness(state.brightness, error))
        return false;

    // Handing the colour to the driver keeps it across everything the driver
    // repaints for, but its colour files are root's unless the user opened
    // them up. Otherwise static colour is sent like an effect, the same
    // command the driver itself sends, and the daemon repaints it.
    // The driver has one colour for all zones, so a trimmed keyboard always
    // goes through the controller.
    if (state.mode == Lighting::Mode::Static && Lighting::isNeutral(state.trim) && setColor(state.color))
        return true;
    return sendFeatures(Lighting::effectPackets(state), error);
}

QString Keyboard::problem() const {
    if (present())
        return {};
    return tr("No RGB keyboard found. Install tuxedo-drivers-nocompatcheck-dkms and load the ite_8291 module.");
}
