#pragma once

// Rows of the settings table for the Today & Lists and Alerts panes. Included by settingskit.h once
// SettingCase and the helpers exist; not a header to use on its own.

namespace e2e {

inline void addListAndAlertCases(QVector<SettingCase> &v) {
    using K = SettingCase;
        // ----------------------------------------------------- Today & Lists
        {
            K c{"today.overdue", "Today & Lists", "setting:today.overdue", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "today");
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.buyMilk) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.row(s.ids.electricity) != nullptr, !changed, 5000);
            };
            v << c;
        }
        {
            K c{"today.starred", "Today & Lists", "setting:today.starred", K::Check, false};
            // "Include starred items in Today": a starred task with no date joins it.
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "today");
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.buyMilk) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.row(s.ids.shelf) != nullptr, changed, 5000);
            };
            v << c;
        }
        {
            K c{"today.stillWorking", "Today & Lists", "setting:today.stillWorking", K::Check, true};
            c.prepare = [](Store &st, Seed &ids) {
                addTask(st, ids.work, "Underway", [](Task &t) {
                    t.startDate = QDate::currentDate().addDays(-1);
                    t.dueDate = QDate::currentDate().addDays(5);
                });
            };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "today");
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.buyMilk) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.row(app.taskTitled("Underway").id) != nullptr, !changed, 5000);
            };
            v << c;
        }
        {
            K c{"lists.startedVisible", "Today & Lists", "setting:lists.startedVisible", K::Check, true};
            c.prepare = [](Store &st, Seed &ids) {
                addTask(st, ids.work, "Underway", [](Task &t) {
                    t.startDate = QDate::currentDate().addDays(-1);
                    t.dueDate = QDate::currentDate().addDays(5);
                });
            };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.work));
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.report) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.row(app.taskTitled("Underway").id) != nullptr, !changed, 5000);
            };
            v << c;
        }
        {
            K c{"focus.subtasksSeparately", "Today & Lists", "setting:focus.subtasksSeparately", K::Check, true};
            c.prepare = [](Store &st, Seed &ids) {
                const qint64 reno = addTask(st, ids.home, "Renovate", [](Task &t) {
                    t.type = TaskType::Project;
                    t.dueDate = QDate::currentDate();
                });
                addTask(st, ids.home, "Sand floor", [reno](Task &t) {
                    t.parentId = reno;
                    t.dueDate = QDate::currentDate();
                });
            };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "today");
                QTRY_VERIFY_WITH_TIMEOUT(app.row(app.taskTitled("Renovate").id) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.row(app.taskTitled("Sand floor").id) != nullptr, !changed, 5000);
            };
            v << c;
        }
        {
            K c{"sort.groupedDates", "Today & Lists", "setting:sort.groupedDates", K::Check, false};
            // "Today, Tomorrow, Next 7 Days and Future instead of one heading per day."
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "all");
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.report) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(textUnder(findItem("taskList"), "NEXT 7 DAYS") != nullptr, changed, 5000);
            };
            v << c;
        }
        {
            K c{"sort.starredAboveHigh", "Today & Lists", "setting:sort.starredAboveHigh", K::Check, false};
            c.prepare = [](Store &st, Seed &) { st.setSetting("sort.all.field", SortField::Priority); };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "all");
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.report) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(textUnder(findItem("taskList"), "STARRED") != nullptr, changed, 5000);
            };
            v << c;
        }
        {
            K c{"sort.listOrder", "Today & Lists", "setting:sort.listOrder", K::Check, true};
            c.prepare = [](Store &st, Seed &ids) {
                st.setSetting("sort.all.field", SortField::List);
                TaskList admin;
                admin.id = st.nextId("lists");
                admin.name = "Admin";
                admin.groupId = st.list(ids.home).groupId;
                admin.order = 3;
                st.begin("Prepare", false);
                st.putList(admin);
                st.commit();
                addTask(st, admin.id, "File taxes");
            };
            // Sorting by list: your list order, or A to Z.
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "all");
                const QStringList want = changed ? QStringList{"ADMIN", "HOME", "WORK"} : QStringList{"HOME", "WORK", "ADMIN"};
                QStringList got;
                QTRY_VERIFY2_WITH_TIMEOUT((got = sectionTitles()) == want, qPrintable(got.join(",")), 5000);
            };
            v << c;
        }
        for (int mode : {0, 1}) {
            K c{"lists.counts", "Today & Lists", "setting:lists.counts", K::Combo};
            c.fromIndex = 2;  // Overdue + Undone
            c.toIndex = mode;
            c.stored = QString::number(mode);
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                const QString home = listKey(s.ids.home);
                QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(home) != nullptr, 5000);
                const bool overdue = !changed || mode >= 1, undone = !changed || mode >= 2;
                QTRY_COMPARE_WITH_TIMEOUT(sidebarNumber("sidebarOverdue", home) > 0, overdue, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(sidebarNumber("sidebarUndone", home) > 0, undone, 5000);
            };
            v << c;
        }
        {
            K c{"lists.undoneIncludesOverdue", "Today & Lists", "setting:lists.undoneIncludesOverdue", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                const QString home = listKey(s.ids.home);
                QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(home) != nullptr, 5000);
                // Home holds four open tasks, one of them overdue.
                QTRY_COMPARE_WITH_TIMEOUT(sidebarNumber("sidebarUndone", home), changed ? 3 : 4, 5000);
                QCOMPARE(sidebarNumber("sidebarOverdue", home), 1);
            };
            v << c;
        }
        {
            K c{"search.inheritInProject", "Today & Lists", "setting:search.inheritInProject", K::Check, false};
            c.prepare = [](Store &st, Seed &ids) {
                const qint64 paint = addTask(st, ids.home, "Paint the house", [](Task &t) { t.type = TaskType::Project; });
                addTask(st, ids.home, "Paint walls", [paint](Task &t) { t.parentId = paint; });
                addTask(st, ids.home, "Buy tiles", [paint](Task &t) { t.parentId = paint; });
                TaskList smart;
                smart.id = st.nextId("lists");
                smart.kind = ListKind::Smart;
                smart.name = "Painting";
                smart.smartQuery = "paint";
                smart.order = 9;
                st.begin("Prepare", false);
                st.putList(smart);
                st.commit();
            };
            // "Opening a project from a search or Smart List shows only the sub-tasks that match it."
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                qint64 smart = 0;
                for (const TaskList &l : app.store().lists())
                    if (l.name == "Painting")
                        smart = l.id;
                showSource(app, listKey(smart));
                const qint64 project = app.taskTitled("Paint the house").id;
                QQuickItem *row = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((row = app.row(project)) != nullptr, 5000);
                // A row still sliding into place can miss the click; aim again.
                // The delegate can be rebuilt meanwhile, so look the row up again each time.
                for (int i = 0; i < 3 && app.backend().currentTaskId() != project; ++i) {
                    if (!QTest::qWaitFor([&] { return (row = app.row(project)) != nullptr; }, 2000))
                        continue;
                    click(row, Qt::LeftButton, pointIn(row, 0.4, 0.5));
                    QTest::qWaitFor([&] { return app.backend().currentTaskId() == project; }, 1500);
                }
                QCOMPARE(app.backend().currentTaskId(), project);
                app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
                QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), "project:" + QString::number(project), 5000);
                QTRY_VERIFY_WITH_TIMEOUT(app.row(app.taskTitled("Paint walls").id) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.row(app.taskTitled("Buy tiles").id) != nullptr, !changed, 5000);
            };
            v << c;
        }

        // ------------------------------------------------------------ Alerts
        {
            K c{"tasks.defaultDueTime", "Alerts", "setting:tasks.defaultDueTime", K::Combo};
            c.fromIndex = 18;  // 9:00 AM
            c.toIndex = 28;    // 2:00 PM
            c.stored = "840";
            c.prepare = [](Store &st, Seed &ids) {
                const qint64 id = addTask(st, ids.home, "Alarmed", [](Task &t) { t.dueDate = QDate::currentDate().addDays(1); });
                Alarm a;
                a.id = st.nextId("alarms");
                a.taskId = id;
                a.kind = Alarm::Relative;
                a.offset = 0;
                st.begin("Prepare", false);
                st.putAlarm(a);
                st.commit();
            };
            // "Alerts on date-only tasks fire at": Quick Look shows when the next alert goes off.
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                const qint64 id = app.taskTitled("Alarmed").id;
                QQuickItem *row = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((row = app.row(id)) != nullptr, 5000);
                click(row, Qt::LeftButton, pointIn(row, 0.4, 0.5));
                QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), id, 5000);
                app.press(Qt::Key_Space);
                QObject *look = findObject("quickLook");
                QVERIFY(look);
                QTRY_VERIFY_WITH_TIMEOUT(look->property("visible").toBool(), 5000);
                const QRegularExpression time(changed ? "2:00 PM" : "9:00 AM");
                QTRY_VERIFY_WITH_TIMEOUT(textMatching(popupSurface(look), time) != nullptr, 5000);
                closeQuickLook(app, look);
            };
            v << c;
        }
        {
            K c{"alerts.nagMinutes", "Alerts", "setting:alerts.nagMinutes", K::Combo};
            c.fromIndex = 0;
            c.toIndex = 2;  // Every 5 mins
            c.stored = "5";
            c.outside = "AlarmService repeats sounding alerts from a timer that needs a notification service";
            v << c;
        }
        for (int mode : {1, 2, 3}) {
            K c{"alerts.badge", "Alerts", "setting:alerts.badge", K::Combo};
            c.fromIndex = 0;  // Overdue + Today
            c.toIndex = mode;
            c.stored = mode == 1 ? "overdue" : mode == 2 ? "today" : "none";
            c.prepare = [](Store &st, Seed &ids) { addTask(st, ids.home, "Due now", [](Task &t) { t.dueDate = QDate::currentDate(); }); };
            // The number on the tray icon and in the Waybar module, as main() works it out:
            // one task is overdue and two are due today.
            c.effect = SK_EFFECT {
                SK_APP
                const ScheduleSettings settings = ScheduleSettings::fromStore(app.store());
                const ViewBuilder views(app.store(), QDateTime::currentDateTime(), settings);
                const QString metric = app.store().setting("alerts.badge", "both").toString();
                const int want = !changed ? 3 : mode == 1 ? 1 : mode == 2 ? 2 : 0;
                QCOMPARE(views.badgeCount(metric), want);
            };
            v << c;
        }
        {
            K c{"alerts.defaultTimed", "Alerts", "setting:alerts.defaultTimed", K::Combo};
            c.fromIndex = 0;  // None
            c.toIndex = 4;    // 15 minutes before
            c.stored = "15";
            c.effect = SK_EFFECT {
                SK_APP
                const QString stem = unique("Zebra");
                Task t;
                quickAdd(app, stem + " tomorrow at 3pm", stem, t);
                const auto alarms = app.store().alarmsFor(t.id);
                if (!changed) {
                    QCOMPARE(alarms.size(), 0);
                } else {
                    QCOMPARE(alarms.size(), 1);
                    QCOMPARE(alarms.first().kind, int(Alarm::Relative));
                    QCOMPARE(alarms.first().offset, 15);
                }
            };
            v << c;
        }
        {
            K c{"alerts.defaultDateOnly", "Alerts", "setting:alerts.defaultDateOnly", K::Combo};
            c.fromIndex = 0;  // None
            c.toIndex = 2;    // 1 day before
            c.stored = "1";
            c.effect = SK_EFFECT {
                SK_APP
                const QString stem = unique("Zebra");
                Task t;
                quickAdd(app, stem + " tomorrow", stem, t);
                const auto alarms = app.store().alarmsFor(t.id);
                if (!changed) {
                    QCOMPARE(alarms.size(), 0);
                } else {
                    QCOMPARE(alarms.size(), 1);
                    QCOMPARE(alarms.first().kind, int(Alarm::DaysBefore));
                    QCOMPARE(alarms.first().offset, 1);
                }
            };
            v << c;
        }
        {
            K c{"alerts.defaultDateOnlyTime", "Alerts", "setting:alerts.defaultDateOnlyTime", K::Combo};
            c.fromIndex = 3;  // 9:00 AM
            c.toIndex = 9;    // 3:00 PM
            c.stored = "900";
            c.prepare = [](Store &st, Seed &) { st.setSetting("alerts.defaultDateOnly", 0); };
            c.effect = SK_EFFECT {
                SK_APP
                const QString stem = unique("Zebra");
                Task t;
                quickAdd(app, stem + " tomorrow", stem, t);
                const auto alarms = app.store().alarmsFor(t.id);
                QCOMPARE(alarms.size(), 1);
                const QTime at = alarms.first().at.time();
                QCOMPARE(at.hour() * 60 + at.minute(), changed ? 15 * 60 : 9 * 60);
            };
            v << c;
        }
}

}  // namespace e2e
