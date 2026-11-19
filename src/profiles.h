#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "lighting.h"

// Named lighting looks ("Gaming", "Night") the user saves and switches
// between. A profile keeps what the light looks like: effect, colour,
// brightness, speed, and whether the colour follows the theme. Power and
// battery settings stay with the machine, not the look.
namespace Profiles {

struct Profile {
    QString name;
    Lighting::State look;
};

QString defaultPath();
QList<Profile> load(const QString &path);
bool save(const QList<Profile> &profiles, const QString &path, QString *error = nullptr);

QStringList names(const QList<Profile> &profiles);
int indexOf(const QList<Profile> &profiles, const QString &name);

// Adds the profile, or replaces the one with the same name (any case).
void put(QList<Profile> *profiles, const QString &name, const Lighting::State &state);
bool remove(QList<Profile> *profiles, const QString &name);

// The state with the profile's look applied and the light on.
Lighting::State applied(const Lighting::State &state, const Profile &profile);
// Whether the state currently shows this profile's look.
bool matches(const Lighting::State &state, const Profile &profile);

// The first profile the state matches, or empty.
QString active(const QList<Profile> &profiles, const Lighting::State &state);
// The profile after the active one, wrapping; the first when none is active.
QString next(const QList<Profile> &profiles, const Lighting::State &state);

} // namespace Profiles
