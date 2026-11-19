QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = keyglow_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/daemon.h ../src/keyboard.h ../src/lighting.h ../src/theme.h ../src/translator.h
SOURCES += keyglow_tests.cpp ../src/backend.cpp ../src/daemon.cpp ../src/keyboard.cpp ../src/lighting.cpp \
           ../src/theme.cpp ../src/translator.cpp
