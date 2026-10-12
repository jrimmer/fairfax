QT += core gui widgets sql qml quick quickcontrols2 network
# Desktop notifications and the file-chooser portal use D-Bus on Linux;
# macOS and Windows use the tray and native dialogs instead.
linux: QT += dbus

CONFIG += c++20 release
TARGET = precrime
TEMPLATE = app

# Listed explicitly so a half-written file in src/ never breaks the build.
HEADERS += src/alarms.h src/backend.h src/dateparse.h src/exchange.h src/filepicker.h src/gitsync.h src/synctree.h src/foldersync.h src/webdav.h src/synchub.h src/restorelists.h src/model.h src/nlparse.h src/query.h src/recurrence.h src/rowmodel.h src/schedule.h src/store.h src/theme.h src/views.h src/markdown.h src/automation.h src/calendar.h src/sounds.h src/selftest.h src/todoist.h src/cli.h src/tray.h src/keyring.h src/hyprland.h
SOURCES += src/alarms.cpp src/backend.cpp src/dateparse.cpp src/demo.cpp src/exchange.cpp src/filepicker.cpp src/gitsync.cpp src/synctree.cpp src/foldersync.cpp src/webdav.cpp src/synchub.cpp src/restorelists.cpp src/main.cpp src/model.cpp src/nlparse.cpp src/query.cpp src/recurrence.cpp src/rowmodel.cpp src/schedule.cpp src/store.cpp src/theme.cpp src/views.cpp src/markdown.cpp src/automation.cpp src/calendar.cpp src/sounds.cpp src/selftest.cpp src/todoist.cpp src/cli.cpp src/tray.cpp src/keyring.cpp src/hyprland.cpp

RESOURCES += src/resources.qrc

# macOS: a proper .app bundle named Precrime; Windows: no console window.
macx {
    TARGET = Precrime
}
win32: CONFIG += windows

