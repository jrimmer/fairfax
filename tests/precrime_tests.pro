QT += core gui sql qml quick quickcontrols2 testlib dbus
CONFIG += c++20 testcase
TARGET = precrime_tests
TEMPLATE = app

INCLUDEPATH += ../src

# Every source in src/ except the app's own main(), plus every test file here.
HEADERS += $$files(../src/*.h) $$files(*.h)
SOURCES += $$files(../src/*.cpp) $$files(*.cpp)
SOURCES -= ../src/main.cpp
