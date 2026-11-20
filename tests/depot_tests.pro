QT += core gui quick quickcontrols2 testlib network
CONFIG += c++17 testcase
TARGET = depot_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += FAKES=\\\"$$PWD/fakes\\\"

HEADERS += ../src/backend.h ../src/engines.h ../src/theme.h ../src/translator.h
SOURCES += depot_tests.cpp ../src/backend.cpp ../src/engines.cpp ../src/theme.cpp ../src/translator.cpp
