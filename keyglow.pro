QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = keyglow
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/daemon.h \
    src/keyboard.h \
    src/lighting.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/daemon.cpp \
    src/keyboard.cpp \
    src/lighting.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
