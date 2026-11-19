#include "profiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace Profiles {

QString defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/keyglow/profiles.json";
}

QList<Profile> load(const QString &path) {
    QList<Profile> profiles;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return profiles;
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value("name").toString().trimmed();
        if (!name.isEmpty() && indexOf(profiles, name) < 0)
            profiles << Profile{name, Lighting::fromJson(object)};
    }
    return profiles;
}

bool save(const QList<Profile> &profiles, const QString &path, QString *error) {
    QJsonArray array;
    for (const Profile &profile : profiles) {
        const QJsonObject look = Lighting::toJson(profile.look);
        array.append(QJsonObject{
            {"name", profile.name},
            {"mode", look.value("mode")},
            {"color", look.value("color")},
            {"brightness", look.value("brightness")},
            {"speed", look.value("speed")},
            {"followTheme", look.value("followTheme")},
        });
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QStringList names(const QList<Profile> &profiles) {
    QStringList list;
    for (const Profile &profile : profiles)
        list << profile.name;
    return list;
}

int indexOf(const QList<Profile> &profiles, const QString &name) {
    for (int i = 0; i < profiles.size(); ++i)
        if (profiles[i].name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

void put(QList<Profile> *profiles, const QString &name, const Lighting::State &state) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;
    const int index = indexOf(*profiles, trimmed);
    if (index >= 0)
        (*profiles)[index] = Profile{trimmed, state};
    else
        profiles->append(Profile{trimmed, state});
}

bool remove(QList<Profile> *profiles, const QString &name) {
    const int index = indexOf(*profiles, name);
    if (index < 0)
        return false;
    profiles->removeAt(index);
    return true;
}

Lighting::State applied(const Lighting::State &state, const Profile &profile) {
    Lighting::State next = state;
    next.mode = profile.look.mode;
    next.color = profile.look.color;
    next.brightness = profile.look.brightness;
    next.speed = profile.look.speed;
    next.followTheme = profile.look.followTheme;
    next.on = true;
    return next;
}

bool matches(const Lighting::State &state, const Profile &profile) {
    const auto &look = profile.look;
    if (state.mode != look.mode || state.brightness != look.brightness || state.followTheme != look.followTheme)
        return false;
    // Colour and speed only count where the effect uses them, so a rainbow
    // profile still matches after the colour picker was touched.
    if (Lighting::usesColor(look.mode) && !look.followTheme && state.color.rgb() != look.color.rgb())
        return false;
    return !Lighting::isAnimated(look.mode) || state.speed == look.speed;
}

QString active(const QList<Profile> &profiles, const Lighting::State &state) {
    for (const Profile &profile : profiles)
        if (matches(state, profile))
            return profile.name;
    return {};
}

QString next(const QList<Profile> &profiles, const Lighting::State &state) {
    if (profiles.isEmpty())
        return {};
    const int current = indexOf(profiles, active(profiles, state));
    return profiles[(current + 1) % profiles.size()].name;
}

} // namespace Profiles
