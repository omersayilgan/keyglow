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
#include "theme.h"
#include "translator.h"

using Lighting::Mode;
using Lighting::State;

// A keyboard laid out like the driver's sysfs tree in a temp directory, with
// the feature reports recorded instead of sent to a device.
class FakeKeyboard : public Keyboard {
public:
    explicit FakeKeyboard(const QString &root) : Keyboard(root) {}
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
        state.mode = Mode::Ripple;
        state.color = QColor("#9b4dff");
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
        Backend backend(&keyboard, path);
        backend.setMode("breathing");
        backend.setColor(QColor("#2f6bff"));
        backend.flush();

        const State saved = Lighting::load(path);
        QCOMPARE(saved.mode, Mode::Breathing);
        QCOMPARE(saved.color, QColor("#2f6bff"));
        QCOMPARE(keyboard.sent.size(), 1);
        QVERIFY(backend.problem().isEmpty());
    }

    void pickingAColourLeavesColourlessEffects()
    {
        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"));
        backend.setMode("rainbow");
        backend.setColor(QColor("#ff8a00"));
        QCOMPARE(backend.mode(), QString("static"));
    }

    void brightnessZeroTurnsOffButRemembersTheLevel()
    {
        FakeKeyboard keyboard(leds());
        Backend backend(&keyboard, m_dir->filePath("state.json"));
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
        Backend backend(&keyboard, m_dir->filePath("state.json"));
        backend.setMode("wave");
        QTest::qWait(650);
        backend.setMode("ripple");
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
        Backend backend(&keyboard, m_dir->filePath("state.json"));
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

private:
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
