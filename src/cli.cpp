#include "cli.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "daemon.h"
#include "keyboard.h"
#include "lighting.h"

namespace {

const char *usage =
    "Usage: keyglow [command]\n"
    "\n"
    "  open                 Open the window (the default)\n"
    "  status [--json]      Show the saved lighting and whether the keyboard is there\n"
    "  set [options]        Change the lighting and save it\n"
    "      --mode <static|breathing|wave|rainbow|random|ripple>\n"
    "      --color <#rrggbb>\n"
    "      --brightness <0-100>\n"
    "      --speed <1-10>\n"
    "      --on | --off | --toggle\n"
    "      --json           Print the resulting state as JSON\n"
    "  apply                Send the saved lighting to the keyboard again\n"
    "  daemon               Keep the saved lighting on the keyboard (run by keyglow.service)\n"
    "  help                 Show this help\n";

int fail(const QString &message) {
    QTextStream(stderr) << "keyglow: " << message << Qt::endl;
    return 1;
}

QJsonObject status(const Lighting::State &state, Keyboard *keyboard) {
    QJsonObject object = Lighting::toJson(state);
    object.insert("keyboard", keyboard->present());
    object.insert("zones", int(keyboard->zones().size()));
    return object;
}

void print(const QJsonObject &object, bool json) {
    QTextStream out(stdout);
    if (json) {
        out << QJsonDocument(object).toJson(QJsonDocument::Indented);
        return;
    }
    out << "mode:       " << object.value("mode").toString() << "\n"
        << "color:      " << object.value("color").toString() << "\n"
        << "brightness: " << object.value("brightness").toInt() << "%\n"
        << "speed:      " << object.value("speed").toInt() << "\n"
        << "on:         " << (object.value("on").toBool() ? "yes" : "no") << "\n"
        << "keyboard:   " << (object.value("keyboard").toBool() ? "found" : "not found") << Qt::endl;
}

} // namespace

namespace Cli {

int run(const QStringList &arguments, Keyboard *keyboard, const QString &statePath) {
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
        return keyboard->apply(Lighting::load(statePath), &error) ? 0 : fail(error);
    }

    if (command == "daemon") {
        Daemon daemon(keyboard, statePath);
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
                state.color = QColor(color.red(), color.green(), color.blue());
                state.on = true;
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

        QString error;
        if (!Lighting::save(state, statePath, &error))
            return fail(error);
        if (!keyboard->apply(state, &error))
            return fail(error);
        if (json)
            print(status(state, keyboard), true);
        return 0;
    }

    return fail("unknown command " + command + "\n\n" + usage);
}

} // namespace Cli
