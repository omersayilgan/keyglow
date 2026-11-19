#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include "daemon.h"
#include "keyboard.h"
#include "lighting.h"
#include "profiles.h"
#include "theme.h"

namespace {

const char *usage =
    "Usage: keyglow [command]\n"
    "\n"
    "  open                   Open the window (the default)\n"
    "  status [--json]        Show the saved lighting and whether the keyboard is there\n"
    "  set [options]          Change the lighting and save it\n"
    "      --mode <static|breathing|wave|rainbow>\n"
    "      --color <#rrggbb>\n"
    "      --brightness <0-100>\n"
    "      --speed <1-10>\n"
    "      --on | --off | --toggle\n"
    "      --follow-theme <on|off>      Use the Omarchy theme's accent as the colour\n"
    "      --battery-saver <none|dim|off>\n"
    "      --battery-brightness <1-100> The most the dim saver allows\n"
    "      --zone-trim <zone>:<r>,<g>,<b>  Correct one zone (1-4), in percent\n"
    "      --reset-trim       Remove every zone correction\n"
    "      --json             Print the resulting state as JSON\n"
    "  profile list [--json]  List saved profiles; * marks the one showing\n"
    "  profile save <name>    Save the current look under a name\n"
    "  profile load <name>    Switch to a saved look\n"
    "  profile next           Switch to the next saved look (for a keybinding)\n"
    "  profile delete <name>  Forget a saved look\n"
    "  apply                  Send the saved lighting to the keyboard again\n"
    "  daemon                 Keep the saved lighting on the keyboard (run by keyglow.service)\n"
    "  help                   Show this help\n";

int fail(const QString &message) {
    QTextStream(stderr) << "keyglow: " << message << Qt::endl;
    return 1;
}

QString accent() {
    return Theme::readAccent(QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml");
}

QJsonObject status(const Lighting::State &state, Keyboard *keyboard) {
    QJsonObject object = Lighting::toJson(state);
    object.insert("keyboard", keyboard->present());
    object.insert("zones", int(keyboard->zones().size()));
    object.insert("onBattery", keyboard->onBattery());
    return object;
}

void print(const QJsonObject &object, bool json) {
    QTextStream out(stdout);
    if (json) {
        out << QJsonDocument(object).toJson(QJsonDocument::Indented);
        return;
    }
    out << "mode:          " << object.value("mode").toString() << "\n"
        << "color:         " << object.value("color").toString()
        << (object.value("followTheme").toBool() ? " (theme accent)" : "") << "\n"
        << "brightness:    " << object.value("brightness").toInt() << "%\n"
        << "speed:         " << object.value("speed").toInt() << "\n"
        << "on:            " << (object.value("on").toBool() ? "yes" : "no") << "\n"
        << "battery saver: " << object.value("batterySaver").toString();
    if (object.value("batterySaver").toString() == "dim")
        out << " to " << object.value("batteryBrightness").toInt() << "%";
    out << (object.value("onBattery").toBool() ? " (on battery now)" : "") << "\n"
        << "keyboard:      " << (object.value("keyboard").toBool() ? "found" : "not found") << Qt::endl;
}

// Saves the state, then shows it the way the battery saver says to.
int commit(const Lighting::State &state, Keyboard *keyboard, const QString &statePath) {
    QString error;
    if (!Lighting::save(state, statePath, &error))
        return fail(error);
    if (!keyboard->apply(Lighting::effective(state, keyboard->onBattery()), &error))
        return fail(error);
    return 0;
}

int profile(const QStringList &arguments, Keyboard *keyboard, const QString &statePath, const QString &profilesPath) {
    const QString action = arguments.value(1);
    const QString name = arguments.mid(2).filter(QRegularExpression("^(?!--)")).join(' ').trimmed();
    QList<Profiles::Profile> profiles = Profiles::load(profilesPath);
    Lighting::State state = Lighting::load(statePath);

    if (action == "list" || action.isEmpty()) {
        const QString active = Profiles::active(profiles, state);
        if (arguments.contains("--json")) {
            QJsonArray array;
            for (const auto &p : profiles) {
                QJsonObject object = Lighting::toJson(p.look);
                object.insert("name", p.name);
                object.insert("active", p.name == active);
                array.append(object);
            }
            QTextStream(stdout) << QJsonDocument(array).toJson(QJsonDocument::Indented);
        } else {
            QTextStream out(stdout);
            for (const auto &p : profiles)
                out << (p.name == active ? "* " : "  ") << p.name << "\n";
        }
        return 0;
    }

    QString error;
    if (action == "save") {
        if (name.isEmpty())
            return fail("profile save takes a name");
        Profiles::put(&profiles, name, state);
        return Profiles::save(profiles, profilesPath, &error) ? 0 : fail(error);
    }

    if (action == "delete") {
        if (!Profiles::remove(&profiles, name))
            return fail("no profile called " + name);
        return Profiles::save(profiles, profilesPath, &error) ? 0 : fail(error);
    }

    if (action == "load" || action == "next") {
        const QString target = action == "next" ? Profiles::next(profiles, state) : name;
        const int index = Profiles::indexOf(profiles, target);
        if (index < 0)
            return fail(profiles.isEmpty() ? QString("no profiles saved yet") : "no profile called " + target);
        state = Profiles::applied(state, profiles[index]);
        if (state.followTheme)
            state.color = Lighting::vivid(QColor(accent()));
        const int result = commit(state, keyboard, statePath);
        if (result == 0 && action == "next")
            QTextStream(stdout) << profiles[index].name << Qt::endl;
        return result;
    }

    return fail("unknown profile action " + action + "\n\n" + usage);
}

} // namespace

namespace Cli {

int run(const QStringList &arguments, Keyboard *keyboard, const QString &statePath, const QString &profilesPath) {
    const QString command = arguments.value(0);

    if (command == "help" || command == "--help" || command == "-h") {
        QTextStream(stdout) << usage;
        return 0;
    }

    if (command == "status") {
        print(status(Lighting::load(statePath), keyboard), arguments.contains("--json"));
        return 0;
    }

    if (command == "apply") {
        QString error;
        return keyboard->apply(Lighting::effective(Lighting::load(statePath), keyboard->onBattery()), &error)
                   ? 0
                   : fail(error);
    }

    if (command == "profile")
        return profile(arguments, keyboard, statePath, profilesPath);

    if (command == "daemon") {
        // A theme directory passed in keeps the theme from polling hyprctl
        // for window rounding, which the daemon has no use for.
        Theme theme(QDir::homePath() + "/.local/state/omarchy/current");
        Daemon daemon(keyboard, statePath, &theme);
        daemon.start();
        return QCoreApplication::exec();
    }

    if (command == "set") {
        Lighting::State state = Lighting::load(statePath);
        bool json = false;
        for (int i = 1; i < arguments.size(); ++i) {
            const QString option = arguments[i];
            auto value = [&](QString *out) {
                if (i + 1 >= arguments.size())
                    return false;
                *out = arguments[++i];
                return true;
            };
            QString text;
            if (option == "--json") {
                json = true;
            } else if (option == "--on") {
                state.on = true;
            } else if (option == "--off") {
                state.on = false;
            } else if (option == "--toggle") {
                state.on = !state.on;
            } else if (option == "--mode") {
                if (!value(&text) || !Lighting::modeFromName(text, &state.mode))
                    return fail("--mode takes one of: " + Lighting::modeNames().join(", "));
                state.on = true;
            } else if (option == "--color") {
                if (!value(&text) || !QColor::isValidColorName(text))
                    return fail("--color takes a colour like #ff8800");
                const QColor color(text);
                state.color = Lighting::vivid(color);
                state.followTheme = false;
                state.on = true;
            } else if (option == "--follow-theme") {
                if (!value(&text) || (text != "on" && text != "off"))
                    return fail("--follow-theme takes on or off");
                state.followTheme = text == "on";
                if (state.followTheme)
                    state.color = Lighting::vivid(QColor(accent()));
            } else if (option == "--battery-saver") {
                if (!value(&text) || !Lighting::batterySaverFromName(text, &state.batterySaver))
                    return fail("--battery-saver takes none, dim, or off");
            } else if (option == "--battery-brightness") {
                bool ok = false;
                const int percent = value(&text) ? text.toInt(&ok) : -1;
                if (!ok || percent < 1 || percent > 100)
                    return fail("--battery-brightness takes 1 to 100");
                state.batteryBrightness = qMax(1, Lighting::percentToLevel(percent));
            } else if (option == "--zone-trim") {
                static const QRegularExpression pattern("^([1-4]):(\\d{1,3}),(\\d{1,3}),(\\d{1,3})$");
                const auto match = value(&text) ? pattern.match(text) : QRegularExpressionMatch();
                if (!match.hasMatch())
                    return fail("--zone-trim takes zone:r,g,b, like 2:100,85,100");
                const int zone = match.captured(1).toInt() - 1;
                for (int channel = 0; channel < 3; ++channel)
                    state.trim[zone][channel] =
                        qBound(Lighting::MinTrim, match.captured(channel + 2).toInt(), Lighting::MaxTrim);
            } else if (option == "--reset-trim") {
                state.trim.fill(Lighting::NeutralTrim);
            } else if (option == "--brightness") {
                bool ok = false;
                const int percent = value(&text) ? text.toInt(&ok) : -1;
                if (!ok || percent < 0 || percent > 100)
                    return fail("--brightness takes 0 to 100");
                if (percent == 0) {
                    state.on = false;
                } else {
                    state.brightness = Lighting::percentToLevel(percent);
                    state.on = true;
                }
            } else if (option == "--speed") {
                bool ok = false;
                const int speed = value(&text) ? text.toInt(&ok) : -1;
                if (!ok || speed < Lighting::MinSpeed || speed > Lighting::MaxSpeed)
                    return fail("--speed takes 1 to 10");
                state.speed = speed;
            } else {
                return fail("unknown option " + option + "\n\n" + usage);
            }
        }

        const int result = commit(state, keyboard, statePath);
        if (result == 0 && json)
            print(status(state, keyboard), true);
        return result;
    }

    return fail("unknown command " + command + "\n\n" + usage);
}

} // namespace Cli
