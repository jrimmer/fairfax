QT += core gui widgets sql qml quick quickcontrols2 testlib network
linux: QT += dbus
CONFIG += c++20
TARGET = precrime_e2e
TEMPLATE = app

INCLUDEPATH += ../../src

# The whole app minus its main(), as in precrime_tests.pro, plus appsetup and
# the QML resources: the tests boot the real window and drive it with the mouse
# and keyboard.
HEADERS += ../../src/alarms.h ../../src/appsetup.h ../../src/backend.h ../../src/dateparse.h ../../src/exchange.h ../../src/filepicker.h ../../src/gitsync.h ../../src/synctree.h ../../src/foldersync.h ../../src/webdav.h ../../src/synchub.h ../../src/restorelists.h ../../src/model.h ../../src/nlparse.h ../../src/query.h ../../src/recurrence.h ../../src/rowmodel.h ../../src/schedule.h ../../src/store.h ../../src/theme.h ../../src/views.h ../../src/markdown.h ../../src/automation.h ../../src/calendar.h ../../src/sounds.h ../../src/todoist.h ../../src/keyring.h ../../src/diagnostics.h harness.h
SOURCES += ../../src/alarms.cpp ../../src/appsetup.cpp ../../src/backend.cpp ../../src/dateparse.cpp ../../src/demo.cpp ../../src/exchange.cpp ../../src/filepicker.cpp ../../src/gitsync.cpp ../../src/synctree.cpp ../../src/foldersync.cpp ../../src/webdav.cpp ../../src/synchub.cpp ../../src/restorelists.cpp ../../src/model.cpp ../../src/nlparse.cpp ../../src/query.cpp ../../src/recurrence.cpp ../../src/rowmodel.cpp ../../src/schedule.cpp ../../src/store.cpp ../../src/theme.cpp ../../src/views.cpp ../../src/markdown.cpp ../../src/automation.cpp ../../src/calendar.cpp ../../src/sounds.cpp ../../src/todoist.cpp ../../src/keyring.cpp ../../src/diagnostics.cpp
SOURCES += main.cpp test_scenarios.cpp test_layout.cpp
SOURCES += test_lists.cpp test_lists_groups.cpp test_lists_nav.cpp
HEADERS += listkit.h
SOURCES += test_tasks.cpp test_tasks_quick.cpp test_tasks_editor.cpp test_tasks_repeat.cpp test_tasks_lifecycle.cpp test_tasks_structure.cpp
HEADERS += taskkit.h

RESOURCES += ../../src/resources.qrc

# Settings end-to-end tests.
HEADERS += settingskit.h settingscases_general.h settingscases_tasks.h settingscases_lists.h
SOURCES += test_settings.cpp test_settings_lists.cpp test_settings_system.cpp
DEFINES += SRCDIR=\\\"$$PWD/../../src\\\"

# Smart List and search end-to-end tests.
HEADERS += smartkit.h
SOURCES += test_smart_presets.cpp test_smart_keywords.cpp test_smart_text.cpp test_smart_bar.cpp test_smart_builder.cpp test_smart_live.cpp test_smart_recipes.cpp test_smart_nav.cpp
