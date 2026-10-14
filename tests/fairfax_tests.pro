QT += core gui widgets sql qml quick quickcontrols2 testlib network
linux: QT += dbus
CONFIG += c++20 testcase
TARGET = fairfax_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += MAIL_FIXTURES=\\\"$$PWD/fixtures/mail\\\"
DEFINES += FAIRFAX_ROOT=\\\"$$PWD/..\\\"

# Listed explicitly, like fairfax.pro: every source except the app's main().
HEADERS += ../src/alarms.h ../src/backend.h ../src/dateparse.h ../src/exchange.h ../src/mailtask.h ../src/imapclient.h ../src/mailfetch.h ../src/oauth.h ../src/cli.h ../src/filepicker.h ../src/gitsync.h ../src/synctree.h ../src/foldersync.h ../src/webdav.h ../src/synchub.h ../src/restorelists.h ../src/model.h ../src/nlparse.h ../src/query.h ../src/recurrence.h ../src/rowmodel.h ../src/schedule.h ../src/store.h ../src/theme.h ../src/views.h  testregistry.h ../src/markdown.h ../src/automation.h ../src/calendar.h ../src/sounds.h ../src/todoist.h ../src/keyring.h ../src/diagnostics.h ../src/searchrules.h ../src/traymenu.h ../src/shortcuts.h ../src/caldavclient.h ../src/vtodo.h ../src/caldavsync.h
SOURCES += ../src/alarms.cpp ../src/backend.cpp ../src/dateparse.cpp ../src/demo.cpp ../src/exchange.cpp ../src/mailtask.cpp ../src/imapclient.cpp ../src/mailfetch.cpp ../src/oauth.cpp ../src/cli.cpp ../src/filepicker.cpp ../src/gitsync.cpp ../src/synctree.cpp ../src/foldersync.cpp ../src/webdav.cpp ../src/synchub.cpp ../src/restorelists.cpp ../src/model.cpp ../src/nlparse.cpp ../src/query.cpp ../src/recurrence.cpp ../src/rowmodel.cpp ../src/schedule.cpp ../src/store.cpp ../src/theme.cpp ../src/views.cpp  main.cpp test_alarms.cpp test_backend.cpp test_dateparse.cpp test_exchange.cpp test_gitsync.cpp test_foldersync.cpp test_nlparse.cpp test_query.cpp test_recurrence.cpp test_schedule.cpp test_store.cpp test_views.cpp ../src/markdown.cpp ../src/automation.cpp ../src/calendar.cpp test_markdown.cpp test_mailtask.cpp test_imapclient.cpp test_mailfetch.cpp test_oauth.cpp test_cli.cpp test_automation.cpp test_calendar.cpp ../src/sounds.cpp test_sounds.cpp ../src/todoist.cpp test_todoist.cpp test_ticktick.cpp ../src/keyring.cpp ../src/diagnostics.cpp ../src/searchrules.cpp ../src/traymenu.cpp test_searchrules.cpp test_traymenu.cpp ../src/shortcuts.cpp test_shortcuts.cpp ../src/caldavclient.cpp test_caldavclient.cpp ../src/vtodo.cpp test_vtodo.cpp ../src/caldavsync.cpp test_caldavsync.cpp
