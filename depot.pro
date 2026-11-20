QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = depot
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/engines.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/engines.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
