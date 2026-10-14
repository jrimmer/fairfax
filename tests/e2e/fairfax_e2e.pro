QT += core gui widgets sql qml quick quickcontrols2 testlib qmltest network
linux: QT += dbus
CONFIG += c++20
TARGET = fairfax_e2e
TEMPLATE = app

INCLUDEPATH += ../../src

# The whole app minus its main(), as in fairfax_tests.pro, plus appsetup and
# the QML resources: the tests boot the real window and drive it with the mouse
# and keyboard.
HEADERS += ../../src/alarms.h ../../src/appsetup.h ../../src/backend.h ../../src/dateparse.h ../../src/exchange.h ../../src/filepicker.h ../../src/gitsync.h ../../src/synctree.h ../../src/foldersync.h ../../src/webdav.h ../../src/synchub.h ../../src/restorelists.h ../../src/model.h ../../src/nlparse.h ../../src/query.h ../../src/recurrence.h ../../src/rowmodel.h ../../src/schedule.h ../../src/store.h ../../src/theme.h ../../src/views.h ../../src/markdown.h ../../src/automation.h ../../src/calendar.h ../../src/myday.h ../../src/sounds.h ../../src/todoist.h ../../src/keyring.h ../../src/mailtask.h ../../src/imapclient.h ../../src/mailfetch.h ../../src/oauth.h ../../src/diagnostics.h ../../src/traymenu.h ../../src/tray.h ../../src/searchrules.h harness.h ../../src/shortcuts.h ../../src/vtodo.h ../../src/caldavclient.h ../../src/caldavsync.h
SOURCES += ../../src/alarms.cpp ../../src/appsetup.cpp ../../src/backend.cpp ../../src/dateparse.cpp ../../src/demo.cpp ../../src/exchange.cpp ../../src/filepicker.cpp ../../src/gitsync.cpp ../../src/synctree.cpp ../../src/foldersync.cpp ../../src/webdav.cpp ../../src/synchub.cpp ../../src/restorelists.cpp ../../src/model.cpp ../../src/nlparse.cpp ../../src/query.cpp ../../src/recurrence.cpp ../../src/rowmodel.cpp ../../src/schedule.cpp ../../src/store.cpp ../../src/theme.cpp ../../src/views.cpp ../../src/markdown.cpp ../../src/automation.cpp ../../src/calendar.cpp ../../src/myday.cpp ../../src/sounds.cpp ../../src/todoist.cpp ../../src/keyring.cpp ../../src/mailtask.cpp ../../src/imapclient.cpp ../../src/mailfetch.cpp ../../src/oauth.cpp ../../src/diagnostics.cpp ../../src/traymenu.cpp ../../src/tray.cpp ../../src/searchrules.cpp ../../src/shortcuts.cpp ../../src/vtodo.cpp ../../src/caldavclient.cpp ../../src/caldavsync.cpp
SOURCES += main.cpp test_scenarios.cpp test_layout.cpp
SOURCES += test_lists.cpp test_lists_groups.cpp test_lists_nav.cpp
HEADERS += listkit.h
SOURCES += test_tasks.cpp test_tasks_quick.cpp test_tasks_editor.cpp test_tasks_repeat.cpp test_tasks_lifecycle.cpp test_tasks_structure.cpp
HEADERS += taskkit.h

RESOURCES += ../../src/resources.qrc

# Settings end-to-end tests.
HEADERS += settingskit.h settingscases_general.h settingscases_tasks.h settingscases_lists.h settingscases_tray.h
SOURCES += test_settings.cpp test_settings_lists.cpp test_settings_system.cpp
DEFINES += SRCDIR=\\\"$$PWD/../../src\\\"

# Smart List and search end-to-end tests.
HEADERS += smartkit.h
SOURCES += test_smart_presets.cpp test_smart_keywords.cpp test_smart_text.cpp test_smart_bar.cpp test_smart_builder.cpp test_smart_rules.cpp test_smart_live.cpp test_smart_recipes.cpp test_smart_nav.cpp

# Keyboard, sort, tags, import/export and window-feature end-to-end tests.
HEADERS += misckit.h
SOURCES += test_misc_sort.cpp test_misc_keys.cpp test_misc_tags.cpp test_misc_io.cpp test_misc_window.cpp

# Gap tests: editor, Quick Look, notes, attachments, alerts and Settings leftovers.
HEADERS += gapskit2.h
SOURCES += test_gaps_editor.cpp test_gaps_notes.cpp test_gaps_settings.cpp

# Gap-filling end-to-end tests: menus, sections and sidebar buttons.
SOURCES += test_gaps_menus.cpp

# Screen reader guard and shared component naming.
SOURCES += test_accessibility.cpp

# Mail to Fairfax end-to-end tests: Settings > Mail and the connector against a fake IMAP server.
SOURCES += test_shortcuts.cpp
SOURCES += test_mail.cpp test_mail_oauth.cpp
HEADERS += mailkit.h ../fakeimap.h ../loopback.h

# CalDAV sync end-to-end tests: Settings > Sync and the provider against a fake CalDAV server.
SOURCES += test_caldav.cpp
HEADERS += ../fakecaldav.h

# The Credential Manager (src/keyring.cpp).
win32: LIBS += -ladvapi32
