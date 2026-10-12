QT += core gui widgets sql qml quick quickcontrols2 network dbus

CONFIG += c++20 release
TARGET = precrime
TEMPLATE = app

# Listed explicitly so a half-written file in src/ never breaks the build.
HEADERS += src/alarms.h src/backend.h src/dateparse.h src/exchange.h src/filepicker.h src/gitsync.h src/model.h src/nlparse.h src/query.h src/recurrence.h src/rowmodel.h src/schedule.h src/store.h src/theme.h src/views.h src/markdown.h src/automation.h src/calendar.h src/sounds.h src/selftest.h src/todoist.h src/cli.h src/tray.h
SOURCES += src/alarms.cpp src/backend.cpp src/dateparse.cpp src/demo.cpp src/exchange.cpp src/filepicker.cpp src/gitsync.cpp src/main.cpp src/model.cpp src/nlparse.cpp src/query.cpp src/recurrence.cpp src/rowmodel.cpp src/schedule.cpp src/store.cpp src/theme.cpp src/views.cpp src/markdown.cpp src/automation.cpp src/calendar.cpp src/sounds.cpp src/selftest.cpp src/todoist.cpp src/cli.cpp src/tray.cpp

RESOURCES += src/resources.qrc
