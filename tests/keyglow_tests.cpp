#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "daemon.h"
#include "keyboard.h"
#include "lighting.h"
#include "profiles.h"
#include "theme.h"
#include "translator.h"

using Lighting::Mode;
using Lighting::State;

// A keyboard laid out like the driver's sysfs tree in a temp directory, with
// the feature reports recorded instead of sent to a device.
class FakeKeyboard : public Keyboard {
public:
    // The power supplies live beside the LEDs, so the charger is fake too.
    explicit FakeKeyboard(const QString &root) : Keyboard(root, root + "/../power") {}
    QList<QList<QByteArray>> sent;

protected:
    bool sendFeatures(const QList<QByteArray> &packets, QString *) override {
        sent << packets;
        return true;
    }
};

class KeyglowTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QDir root(m_dir->path());
        for (const QString zone : {"rgb:kbd_backlight", "rgb:kbd_backlight_1", "rgb:kbd_backlight_2", "rgb:kbd_backlight_3"}) {
            root.mkpath("leds/" + zone + "/device/hidraw/hidraw7");
            write(ledFile(zone, "brightness"), "0\n");
            write(ledFile(zone, "multi_intensity"), "255 255 255\n");
        }
        root.mkpath("leds/input4::capslock");
        root.mkpath("power/AC0");
        root.mkpath("power/BAT0");
        write(m_dir->filePath("power/AC0/type"), "Mains\n");
        write(m_dir->filePath("power/BAT0/type"), "Battery\n");
        plug(true);
    }

    void effectiveAppliesTheBatterySaver()
    {
        State state;
        state.brightness = 40;
        state.batterySaver = Lighting::BatterySaver::Dim;
        state.batteryBrightness = 10;
        QCOMPARE(Lighting::effective(state, false).brightness, 40);
        QCOMPARE(Lighting::effective(state, true).brightness, 10);
        state.brightness = 5;
        QCOMPARE(Lighting::effective(state, true).brightness, 5);
        state.batterySaver = Lighting::BatterySaver::Off;
        QVERIFY(!Lighting::effective(state, true).on);
        QVERIFY(Lighting::effective(state, false).on);
    }

    void keyboardSeesTheCharger()
    {
        FakeKeyboard keyboard(leds());
        QVERIFY(!keyboard.onBattery());
        plug(false);
        QVERIFY(keyboard.onBattery());

        // A desktop has no mains supply at all, and is never on battery.
        QTemporaryDir none;
        QVERIFY(!Keyboard(leds(), none.path()).onBattery());
    }

    void newSettingsRoundTrip()
    {
        State state;
        state.followTheme = true;
        state.batterySaver = Lighting::BatterySaver::Dim;
        state.batteryBrightness = 15;
        const QString path = m_dir->filePath("state.json");
        QVERIFY(Lighting::save(state, path));
        QCOMPARE(Lighting::load(path), state);
    }

    void profilesReplaceByNameAndRoundTrip()
    {
        QList<Profiles::Profile> profiles;
        State night;
        night.color = QColor("#ff0000");
        night.brightness = 5;
        Profiles::put(&profiles, "Night", night);
        State gaming;
        gaming.mode = Mode::Wave;
        gaming.speed = 9;
        Profiles::put(&profiles, "Gaming", gaming);
        night.brightness = 8;
        Profiles::put(&profiles, "night", night);
        QCOMPARE(Profiles::names(profiles), QStringList({"night", "Gaming"}));

        const QString path = m_dir->filePath("profiles.json");
        QVERIFY(Profiles::save(profiles, path));
        const auto loaded = Profiles::load(path);
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded[0].look.brightness, 8);
        QCOMPARE(loaded[1].look.mode, Mode::Wave);
        QVERIFY(Profiles::remove(&profiles, "NIGHT"));
        QCOMPARE(Profiles::names(profiles), QStringList({"Gaming"}));
    }

    void profilesMatchOnlyWhatTheEffectUses()
    {
        State rainbow;
        rainbow.mode = Mode::Rainbow;
        rainbow.speed = 4;
        QList<Profiles::Profile> profiles{{"Party", rainbow}};

        State current = rainbow;
        current.color = QColor("#123456");
        current.batterySaver = Lighting::BatterySaver::Off;
        QCOMPARE(Profiles::active(profiles, current), QString("Party"));
        current.speed = 5;
        QVERIFY(Profiles::active(profiles, current).isEmpty());
    }

    void nextProfileWraps()
    {
        State a, b;
        b.mode = Mode::Wave;
        QList<Profiles::Profile> profiles{{"A", a}, {"B", b}};
        State other;
        other.brightness = 3;
        QCOMPARE(Profiles::next(profiles, other), QString("A"));
        QCOMPARE(Profiles::next(profiles, a), QString("B"));
        QCOMPARE(Profiles::next(profiles, b), QString("A"));
    }

    void effectPacketsCarryPaletteAndEffect()
    {
        State state;
        state.mode = Mode::Breathing;
        state.color = QColor(255, 255, 255);
        state.brightness = 40;
        state.speed = 10;

        const auto packets = Lighting::effectPackets(state);
        QCOMPARE(packets.size(), 5);
        // White is white-balanced the way the driver does it.
        QCOMPARE(packets[0], bytes({0x14, 0x00, 0x01, 255, 126, 120, 0x00, 0x00}));
        QCOMPARE(packets[3], bytes({0x14, 0x00, 0x04, 255, 126, 120, 0x00, 0x00}));
        // Breathing, fastest, level 40, palette colour, never saved to the controller.
        QCOMPARE(packets[4], bytes({0x08, 0x02, 0x02, 0x01, 40, 0x08, 0x00, 0x00}));
    }

    void eachZoneGetsItsOwnTrim()
    {
        State state;
        state.color = QColor("#ff8a00");
        state.trim[1] = {100, 80, 100};
        state.trim[3] = {90, 120, 100};
        const auto packets = Lighting::effectPackets(state);
        // #ff8a00 white-balances to ff 44 00.
        QCOMPARE(packets[0], bytes({0x14, 0x00, 0x01, 0xff, 0x44, 0x00, 0x00, 0x00}));
        QCOMPARE(packets[1], bytes({0x14, 0x00, 0x02, 0xff, 0x36, 0x00, 0x00, 0x00}));
        QCOMPARE(packets[3], bytes({0x14, 0x00, 0x04, 0xe5, 0x51, 0x00, 0x00, 0x00}));
        // Never past full.
        QCOMPARE(Lighting::calibrated(QColor(255, 255, 255), {200, 100, 100}).red(), 255);
    }

    void colorsAreVivid()
    {
        QCOMPARE(Lighting::vivid(QColor("#7daea3")), QColor("#00ffc4"));
        QCOMPARE(Lighting::vivid(QColor("#ff0000")), QColor("#ff0000"));
        // Greys have no hue, so they become white.
        QCOMPARE(Lighting::vivid(QColor("#6e6e6e")), QColor("#ffffff"));
    }

    void trimRoundTripsAndKeepsStaticOffTheDriver()
    {
        State state;
        state.color = QColor(255, 0, 128);
        state.trim[2] = {100, 90, 110};
        const QString path = m_dir->filePath("state.json");
        QVERIFY(Lighting::save(state, path));
        QCOMPARE(Lighting::load(path).trim, state.trim);

        // The driver can only hold one colour for every zone.
        FakeKeyboard keyboard(leds());
        QVERIFY(keyboard.apply(state));
        QCOMPARE(keyboard.sent.size(), 1);
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "multi_intensity")), QByteArray("255 255 255"));
    }

    void profilesLeaveTheTrimAlone()
    {
        State state;
        state.trim[0] = {100, 70, 100};
        Profiles::Profile profile{"Plain", State()};
        QCOMPARE(Profiles::applied(state, profile).trim, state.trim);
    }

    void staticIgnoresTheSpeedSlider()
    {
        // Any other value in the speed byte picks a multi-colour static
        // pattern on this controller.
        State state;
        for (int speed : {1, 5, 9, 10}) {
            state.speed = speed;
            QCOMPARE(Lighting::effectPackets(state).last()[3], char(0x03));
        }
    }

    void retiredEffectsFallBackToStatic()
    {
        const QString path = m_dir->filePath("state.json");
        write(path, "{\"mode\":\"ripple\",\"color\":\"#ff3fb4\"}");
        QCOMPARE(Lighting::load(path).mode, Mode::Static);
        // A washed-out colour saved by an older version comes back vivid.
        QCOMPARE(Lighting::load(path).color, QColor("#ff009d"));
    }

    void effectsWithoutColourUseASpreadOfHues()
    {
        State state;
        state.mode = Mode::Wave;
        state.color = QColor(10, 20, 30);
        const auto packets = Lighting::effectPackets(state);
        QCOMPARE(packets[0], bytes({0x14, 0x00, 0x01, 255, 0, 0, 0x00, 0x00}));
        QCOMPARE(packets[4][2], char(0x03));
        QCOMPARE(packets[4][6], char(0x01));
    }

    void effectOffSendsZeroBrightness()
    {
        State state;
        state.mode = Mode::Rainbow;
        state.on = false;
        QCOMPARE(Lighting::effectPackets(state).last()[4], char(0));
    }

    void speedRunsSlowToFast()
    {
        QCOMPARE(Lighting::speedByte(1), quint8(10));
        QCOMPARE(Lighting::speedByte(10), quint8(1));
        QCOMPARE(Lighting::speedByte(99), quint8(1));
    }

    void percentMapsToTheControllerScale()
    {
        QCOMPARE(Lighting::percentToLevel(100), 50);
        QCOMPARE(Lighting::percentToLevel(50), 25);
        QCOMPARE(Lighting::percentToLevel(-5), 0);
        QCOMPARE(Lighting::levelToPercent(25), 50);
    }

    void stateRoundTripsThroughDisk()
    {
        State state;
        state.mode = Mode::Breathing;
        state.color = QColor("#6f00ff");
        state.brightness = 30;
        state.speed = 7;
        state.on = false;
        const QString path = m_dir->filePath("config/keyglow/state.json");
        QVERIFY(Lighting::save(state, path));
        QCOMPARE(Lighting::load(path), state);
    }

    void brokenStateFallsBackToDefaults()
    {
        const QString path = m_dir->filePath("state.json");
        QCOMPARE(Lighting::load(path), State());
        write(path, "{\"mode\":\"disco\",\"color\":\"nope\",\"speed\":42}");
        State loaded = Lighting::load(path);
        QCOMPARE(loaded.mode, Mode::Static);
        QCOMPARE(loaded.color, QColor(255, 255, 255));
        QCOMPARE(loaded.speed, Lighting::MaxSpeed);
    }

    void keyboardListsZonesInDriverOrder()
    {
        FakeKeyboard keyboard(leds());
        QCOMPARE(keyboard.zones(), QStringList({"rgb:kbd_backlight", "rgb:kbd_backlight_1", "rgb:kbd_backlight_2",
                                                "rgb:kbd_backlight_3"}));
        QCOMPARE(keyboard.hidrawPath(), QString("/dev/hidraw7"));
        QVERIFY(keyboard.problem().isEmpty());
    }

    void missingDriverIsReported()
    {
        QTemporaryDir empty;
        FakeKeyboard keyboard(empty.path());
        QVERIFY(!keyboard.present());
        QVERIFY(!keyboard.problem().isEmpty());
        QString error;
        QVERIFY(!keyboard.apply(State(), &error));
        QVERIFY(!error.isEmpty());
    }

    void staticColourGoesThroughTheDriver()
    {
        FakeKeyboard keyboard(leds());
        State state;
        state.color = QColor(255, 0, 128);
        state.brightness = 20;
        QVERIFY(keyboard.apply(state));
        QVERIFY(keyboard.sent.isEmpty());
        for (const QString zone : keyboard.zones()) {
            QCOMPARE(read(ledFile(zone, "multi_intensity")), QByteArray("255 0 128"));
            QCOMPARE(read(ledFile(zone, "brightness")), QByteArray("20"));
        }
    }

    void staticColourFallsBackToTheController()
    {
        FakeKeyboard keyboard(leds());
        for (const QString zone : keyboard.zones())
            QVERIFY(QFile::setPermissions(ledFile(zone, "multi_intensity"), QFile::ReadOwner));
        State state;
        state.color = QColor(0, 255, 0);
        QVERIFY(keyboard.apply(state));
        QCOMPARE(keyboard.sent.size(), 1);
        QCOMPARE(keyboard.sent[0][0], bytes({0x14, 0x00, 0x01, 0, 126, 0, 0x00, 0x00}));
        QCOMPARE(keyboard.sent[0].last()[2], char(0x01));
    }

    void daemonRepaintsAStaticColourTheDriverDoesntHold()
    {
        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        State state;
        state.color = QColor(255, 0, 0);
        QVERIFY(Lighting::save(state, path));
        Daemon daemon(&keyboard, path);
        daemon.start();
        keyboard.sent.clear();

        // The lock restores brightness and the driver repaints in white.
        write(ledFile("rgb:kbd_backlight", "multi_intensity"), "255 255 255");
        write(ledFile("rgb:kbd_backlight", "brightness"), "30");
        daemon.poll();
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "multi_intensity")), QByteArray("255 0 0"));
    }

    void effectSetsBrightnessThenSendsTheEffect()
    {
        FakeKeyboard keyboard(leds());
        State state;
        state.mode = Mode::Wave;
        state.brightness = 35;
        QVERIFY(keyboard.apply(state));
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "brightness")), QByteArray("35"));
        QCOMPARE(keyboard.sent.size(), 1);
        QCOMPARE(keyboard.sent[0].last()[4], char(35));
    }

    void offDarkensEveryZone()
    {
        FakeKeyboard keyboard(leds());
        write(ledFile("rgb:kbd_backlight", "brightness"), "30");
        State state;
        state.mode = Mode::Rainbow;
        state.on = false;
        QVERIFY(keyboard.apply(state));
        QVERIFY(keyboard.sent.isEmpty());
        QCOMPARE(read(ledFile("rgb:kbd_backlight_3", "brightness")), QByteArray("0"));
    }

    void backendSavesAndApplies()
    {
        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        Backend backend(&keyboard, path, m_dir->filePath("profiles.json"));
        backend.setMode("breathing");
        backend.setColor(QColor("#2f6bff"));
        backend.flush();

        const State saved = Lighting::load(path);
        QCOMPARE(saved.mode, Mode::Breathing);
        QCOMPARE(saved.color, QColor("#004cff"));   // made vivid
        QCOMPARE(keyboard.sent.size(), 1);
        QVERIFY(backend.problem().isEmpty());
    }

    void pickingAColourLeavesColourlessEffects()
    {
        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"), m_dir->filePath("profiles.json"));
        backend.setMode("rainbow");
        backend.setColor(QColor("#ff8a00"));
        QCOMPARE(backend.mode(), QString("static"));
    }

    void brightnessZeroTurnsOffButRemembersTheLevel()
    {
        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"), m_dir->filePath("profiles.json"));
        backend.setBrightness(60);
        backend.setBrightness(0);
        QVERIFY(!backend.on());
        QCOMPARE(backend.brightness(), 60);
        backend.toggle();
        QVERIFY(backend.on());
        QCOMPARE(backend.brightness(), 60);
    }

    void undoAndRedoStepThroughChanges()
    {
        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"), m_dir->filePath("profiles.json"));
        backend.setMode("wave");
        QTest::qWait(650);
        backend.setMode("rainbow");
        QVERIFY(backend.canUndo());
        backend.undo();
        QCOMPARE(backend.mode(), QString("wave"));
        backend.undo();
        QCOMPARE(backend.mode(), QString("static"));
        backend.redo();
        QCOMPARE(backend.mode(), QString("wave"));
    }

    void quickChangesUndoAsOne()
    {
        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"), m_dir->filePath("profiles.json"));
        for (int percent = 10; percent <= 90; percent += 10)
            backend.setBrightness(percent);
        backend.undo();
        QVERIFY(!backend.canUndo());
        QCOMPARE(backend.brightness(), Lighting::levelToPercent(State().brightness));
    }

    void daemonRepaintsEffectsAfterOutsideBrightness()
    {
        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        State state;
        state.mode = Mode::Wave;
        state.brightness = 20;
        QVERIFY(Lighting::save(state, path));

        Daemon daemon(&keyboard, path);
        daemon.start();
        QCOMPARE(keyboard.sent.size(), 1);

        // Omarchy's lock blanks the keyboard: nothing to repaint.
        write(ledFile("rgb:kbd_backlight", "brightness"), "0");
        daemon.poll();
        QCOMPARE(keyboard.sent.size(), 1);

        // Ctrl + brightness key, or the lock restoring a new level.
        write(ledFile("rgb:kbd_backlight", "brightness"), "45");
        daemon.poll();
        QCOMPARE(keyboard.sent.size(), 2);
        QCOMPARE(keyboard.sent.last().last()[4], char(45));
        QCOMPARE(Lighting::load(path).brightness, 45);
    }

    void daemonLeavesStaticColourToTheDriver()
    {
        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        QVERIFY(Lighting::save(State(), path));
        Daemon daemon(&keyboard, path);
        daemon.start();
        write(ledFile("rgb:kbd_backlight", "brightness"), "10");
        daemon.poll();
        QVERIFY(keyboard.sent.isEmpty());
        QCOMPARE(Lighting::load(path).brightness, 10);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void backendSavesLoadsAndDeletesProfiles()
    {
        FakeKeyboard keyboard(leds());
        const QString profiles = m_dir->filePath("profiles.json");
        Backend backend(&keyboard, m_dir->filePath("state.json"), profiles);
        backend.setMode("breathing");
        backend.setColor(QColor("#ff0000"));
        QVERIFY(backend.saveProfile("Night"));
        QCOMPARE(backend.activeProfile(), QString("Night"));

        backend.setMode("rainbow");
        QVERIFY(backend.activeProfile().isEmpty());
        backend.loadProfile("night");
        QCOMPARE(backend.mode(), QString("breathing"));
        QCOMPARE(backend.activeProfile(), QString("Night"));
        QCOMPARE(Profiles::names(Profiles::load(profiles)), QStringList({"Night"}));

        backend.deleteProfile("Night");
        QVERIFY(backend.profiles().isEmpty());
        QVERIFY(Profiles::load(profiles).isEmpty());
    }

    void backendFollowsTheThemeAccent()
    {
        QTemporaryDir themeDir;
        QDir(themeDir.path()).mkpath("theme");
        write(themeDir.filePath("theme/colors.toml"), "accent = \"#112233\"\n");
        Theme theme(themeDir.path());

        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"), m_dir->filePath("profiles.json"), &theme);
        backend.setMode("rainbow");
        backend.setFollowTheme(true);
        QCOMPARE(backend.color(), QColor("#0080ff"));   // the accent, made vivid
        QCOMPARE(backend.mode(), QString("static"));

        write(themeDir.filePath("theme/colors.toml"), "accent = \"#aa7744\"\n");
        QTRY_COMPARE(backend.color(), QColor("#ff8000"));

        // A colour picked by hand stops following.
        backend.setColor(QColor("#ff0000"));
        QVERIFY(!backend.followTheme());
    }

    void backendDimsOnBattery()
    {
        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"), m_dir->filePath("profiles.json"));
        plug(false);
        backend.setBatterySaver("dim");
        backend.setBatteryBrightness(20);
        backend.setBrightness(80);
        backend.flush();
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "brightness")), QByteArray("10"));
        QCOMPARE(backend.brightness(), 80);
    }

    void daemonDimsOnUnplugAndRestores()
    {
        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        State state;
        state.brightness = 40;
        state.batterySaver = Lighting::BatterySaver::Dim;
        state.batteryBrightness = 10;
        QVERIFY(Lighting::save(state, path));
        Daemon daemon(&keyboard, path);
        daemon.start();
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "brightness")), QByteArray("40"));

        plug(false);
        daemon.poll();
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "brightness")), QByteArray("10"));
        // The saved level is untouched, so plugging in brings it back.
        QCOMPARE(Lighting::load(path).brightness, 40);
        plug(true);
        daemon.poll();
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "brightness")), QByteArray("40"));
    }

    void daemonTurnsOffOnBattery()
    {
        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        State state;
        state.mode = Mode::Wave;
        state.batterySaver = Lighting::BatterySaver::Off;
        QVERIFY(Lighting::save(state, path));
        Daemon daemon(&keyboard, path);
        daemon.start();
        plug(false);
        daemon.poll();
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "brightness")), QByteArray("0"));
        QVERIFY(Lighting::load(path).on);
    }

    void handPickedLevelOnBatteryLastsUntilThePowerChanges()
    {
        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        State state;
        state.mode = Mode::Wave;
        state.brightness = 40;
        state.batterySaver = Lighting::BatterySaver::Dim;
        state.batteryBrightness = 10;
        QVERIFY(Lighting::save(state, path));
        plug(false);
        Daemon daemon(&keyboard, path);
        daemon.start();
        keyboard.sent.clear();

        // Ctrl + brightness up while dimmed: honoured, not saved, and the
        // wave is repainted at that level.
        write(ledFile("rgb:kbd_backlight", "brightness"), "25");
        daemon.poll();
        QCOMPARE(Lighting::load(path).brightness, 40);
        QCOMPARE(keyboard.sent.last().last()[4], char(25));

        plug(true);
        daemon.poll();
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "brightness")), QByteArray("40"));
    }

    void daemonTakesTheNewAccent()
    {
        QTemporaryDir themeDir;
        QDir(themeDir.path()).mkpath("theme");
        write(themeDir.filePath("theme/colors.toml"), "accent = \"#00ff00\"\n");
        Theme theme(themeDir.path());

        FakeKeyboard keyboard(leds());
        const QString path = m_dir->filePath("state.json");
        State state;
        state.followTheme = true;
        QVERIFY(Lighting::save(state, path));
        Daemon daemon(&keyboard, path, &theme);
        daemon.start();
        QCOMPARE(Lighting::load(path).color, QColor("#00ff00"));
        QCOMPARE(read(ledFile("rgb:kbd_backlight", "multi_intensity")), QByteArray("0 255 0"));

        write(themeDir.filePath("theme/colors.toml"), "accent = \"#0000ff\"\n");
        QTRY_COMPARE(read(ledFile("rgb:kbd_backlight", "multi_intensity")), QByteArray("0 0 255"));
    }

private:
    void plug(bool online) { write(m_dir->filePath("power/AC0/online"), online ? "1\n" : "0\n"); }

    QString leds() const { return m_dir->filePath("leds"); }
    QString ledFile(const QString &zone, const QString &name) const { return leds() + "/" + zone + "/" + name; }

    static QByteArray bytes(std::initializer_list<int> values)
    {
        QByteArray data;
        for (int v : values)
            data.append(char(quint8(v)));
        return data;
    }

    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(contents);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_MAIN(KeyglowTests)
#include "keyglow_tests.moc"
