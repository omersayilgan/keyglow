// keyglow — colour, effect, and brightness for the RGB keyboard backlight.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "keyboard.h"
#include "lighting.h"
#include "profiles.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString("open");

    // Commands without a window run without a display, so the daemon starts
    // before Hyprland is up and works over SSH. Anything that isn't `open`
    // goes there too, so a typo gets the usage instead of a window.
    if (command != "open") {
        QCoreApplication app(argc, argv);
        app.setApplicationName("keyglow");
        Keyboard keyboard;
        return Cli::run(app.arguments().mid(1), &keyboard, Lighting::defaultStatePath(), Profiles::defaultPath());
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("keyglow");
    app.setApplicationVersion("0.3.0");

    // Matches the window to keyglow.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("keyglow");
    app.setWindowIcon(QIcon::fromTheme("keyglow"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Keyboard keyboard;
    Backend backend(&keyboard, Lighting::defaultStatePath(), Profiles::defaultPath(), &theme);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, &Backend::flush);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
