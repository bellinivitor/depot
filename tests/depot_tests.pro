QT += core gui quick quickcontrols2 testlib network
CONFIG += c++17 testcase
TARGET = depot_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += FAKES=\\\"$$PWD/fakes\\\"

HEADERS += ../src/backend.h ../src/engines.h ../src/mcp.h ../src/operations.h ../src/theme.h ../src/translator.h
SOURCES += depot_tests.cpp ../src/backend.cpp ../src/engines.cpp ../src/mcp.cpp ../src/operations.cpp ../src/theme.cpp ../src/translator.cpp
