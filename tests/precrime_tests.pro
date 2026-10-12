QT += core gui sql qml quick quickcontrols2 testlib network dbus
CONFIG += c++20 testcase
TARGET = precrime_tests
TEMPLATE = app

INCLUDEPATH += ../src

# Listed explicitly, like precrime.pro: every source except the app's main().
HEADERS += ../src/alarms.h ../src/backend.h ../src/dateparse.h ../src/exchange.h ../src/filepicker.h ../src/gitsync.h ../src/model.h ../src/nlparse.h ../src/query.h ../src/recurrence.h ../src/rowmodel.h ../src/schedule.h ../src/store.h ../src/theme.h ../src/views.h  testregistry.h ../src/markdown.h ../src/automation.h ../src/calendar.h ../src/sounds.h ../src/todoist.h
SOURCES += ../src/alarms.cpp ../src/backend.cpp ../src/dateparse.cpp ../src/demo.cpp ../src/exchange.cpp ../src/filepicker.cpp ../src/gitsync.cpp ../src/model.cpp ../src/nlparse.cpp ../src/query.cpp ../src/recurrence.cpp ../src/rowmodel.cpp ../src/schedule.cpp ../src/store.cpp ../src/theme.cpp ../src/views.cpp  main.cpp test_backend.cpp test_dateparse.cpp test_exchange.cpp test_gitsync.cpp test_nlparse.cpp test_query.cpp test_recurrence.cpp test_schedule.cpp test_store.cpp test_views.cpp ../src/markdown.cpp ../src/automation.cpp ../src/calendar.cpp test_markdown.cpp test_automation.cpp test_calendar.cpp ../src/sounds.cpp test_sounds.cpp ../src/todoist.cpp test_todoist.cpp
