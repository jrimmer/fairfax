QT += core gui sql qml quick quickcontrols2 network dbus

CONFIG += c++20 release
TARGET = precrime
TEMPLATE = app

HEADERS += $$files(src/*.h)
SOURCES += $$files(src/*.cpp)

RESOURCES += src/resources.qrc
