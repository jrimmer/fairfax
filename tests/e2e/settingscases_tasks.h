#pragma once

// Rows of the settings table for the Tasks pane. Included by settingskit.h once
// SettingCase and the helpers exist; not a header to use on its own.

namespace e2e {

inline void addTaskCases(QVector<SettingCase> &v) {
    using K = SettingCase;
        // ------------------------------------------------------------- Tasks
        {
            K c{"nl.quickEntry", "Tasks", "setting:nl.quickEntry", K::Check, true};
            // "`Collect the parcel tomorrow at 3pm`... arrive with their fields filled in."
            c.effect = SK_EFFECT {
                SK_APP
                const QString stem = unique("Zebra");
                Task t;
                quickAdd(app, stem + " tomorrow at 3pm", stem, t);
                if (changed) {
                    QCOMPARE(t.title, stem + " tomorrow at 3pm");
                    QVERIFY(!t.dueDate.isValid());
                } else {
                    QCOMPARE(t.title, stem);
                    QCOMPARE(t.dueDate, QDate::currentDate().addDays(1));
                    QCOMPARE(t.dueTime, 15 * 60);
                }
            };
            v << c;
        }
        {
            K c{"nl.editor", "Tasks", "setting:nl.editor", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                const QString stem = unique("Yak");
                Task t;
                newTaskByKeyboard(app, stem + " tomorrow", t, stem);
                if (changed) {
                    QCOMPARE(t.title, stem + " tomorrow");
                    QVERIFY(!t.dueDate.isValid());
                } else {
                    QCOMPARE(t.title, stem);
                    QCOMPARE(t.dueDate, QDate::currentDate().addDays(1));
                }
            };
            v << c;
        }
        for (int index : {0, 6}) {
            // "Show completed tasks for": Never ... Always.
            K c{"tasks.completedWindow", "Tasks", "setting:tasks.completedWindow", K::Combo};
            c.fromIndex = 3;  // 1 day
            c.toIndex = index;
            c.stored = index == 0 ? "0" : "-1";
            c.prepare = [index](Store &st, Seed &ids) {
                // Finished an hour ago is inside the default day; three days ago is not.
                editTask(st, ids.oldThing, [index](Task &t) { t.completedAt = QDateTime::currentDateTime().addSecs(index == 0 ? -3600 : -3 * 86400); });
            };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.work));
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.report) != nullptr, 5000);
                const bool shown = index == 0 ? !changed : changed;
                QTRY_COMPARE_WITH_TIMEOUT(app.row(s.ids.oldThing) != nullptr, shown, 5000);
            };
            v << c;
        }
        {
            K c{"tasks.completedRuleSubtasks", "Tasks", "setting:tasks.completedRuleSubtasks", K::Check, true};
            c.prepare = [](Store &st, Seed &ids) {
                const qint64 reno = addTask(st, ids.home, "Renovate", [](Task &t) { t.type = TaskType::Project; });
                addTask(st, ids.home, "Painted walls", [reno](Task &t) {
                    t.parentId = reno;
                    t.completed = true;
                    t.completedAt = QDateTime::currentDateTime().addDays(-3);
                });
                addTask(st, ids.home, "Buy tiles", [reno](Task &t) { t.parentId = reno; });
            };
            // "Off keeps completed project sub-tasks in view, whatever the setting above says."
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                app.backend().selectSource("project:" + QString::number(app.taskTitled("Renovate").id));
                QTRY_VERIFY_WITH_TIMEOUT(app.row(app.taskTitled("Buy tiles").id) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.row(app.taskTitled("Painted walls").id) != nullptr, changed, 5000);
            };
            v << c;
        }
        {
            K c{"tasks.defaultList", "Tasks", "setting:tasks.defaultList", K::Combo};
            c.fromIndex = 0;  // First list
            c.toIndex = 3;    // First list, Last used, Home, Work
            c.stored = "*";
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "all");
                Task t;
                newTaskByKeyboard(app, unique("Dl"), t);
                QCOMPARE(t.listId, changed ? s.ids.work : s.ids.home);
            };
            v << c;
        }
        {
            K c{"tasks.alwaysDefaultList", "Tasks", "setting:tasks.alwaysDefaultList", K::Check, false};
            c.prepare = [](Store &st, Seed &ids) { st.setSetting("tasks.defaultList", ids.work); };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                qint64 listId = 0;
                newTaskListId(app, listId);
                QCOMPARE(listId, changed ? s.ids.work : s.ids.home);
            };
            v << c;
        }
        {
            K c{"tasks.overdueUsesTime", "Tasks", "setting:tasks.overdueUsesTime", K::Check, false};
            c.prepare = [](Store &st, Seed &ids) {
                addTask(st, ids.home, "Late today", [](Task &t) { t.dueDate = QDate::currentDate(); t.dueTime = 0; });
            };
            c.effect = SK_EFFECT {
                SK_APP
                // Needs the clock past midnight to have a time that has gone.
                if (QTime::currentTime() < QTime(0, 5))
                    QSKIP("too close to midnight");
                showSource(app, "today");
                const qint64 id = app.taskTitled("Late today").id;
                QQuickItem *title = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((title = rowTitle(app, id)) != nullptr, 5000);
                Theme &theme = app.services().theme;
                if (changed)
                    QTRY_COMPARE_WITH_TIMEOUT(colourOf(title), QColor(theme.overdue()), 5000);
                else
                    QTRY_COMPARE_WITH_TIMEOUT(colourOf(title), QColor(theme.foreground()), 5000);
            };
            v << c;
        }
        {
            K c{"tasks.defaultDueOffset", "Tasks", "setting:tasks.defaultDueOffset", K::Combo};
            c.fromIndex = 0;
            c.toIndex = 2;  // Day After
            c.stored = "1";
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                Task t;
                newTaskByKeyboard(app, unique("Dd"), t);
                if (changed)
                    QCOMPARE(t.dueDate, QDate::currentDate().addDays(1));
                else
                    QVERIFY(!t.dueDate.isValid());
            };
            v << c;
        }
        {
            K c{"tasks.defaultDueQuickEntryOnly", "Tasks", "setting:tasks.defaultDueQuickEntryOnly", K::Check, false};
            c.prepare = [](Store &st, Seed &) { st.setSetting("tasks.defaultDueOffset", 0); };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                Task inMain, inQuick;
                newTaskByKeyboard(app, unique("Dm"), inMain);
                const QString stem = unique("Dq");
                quickAdd(app, stem, stem, inQuick);
                QCOMPARE(inQuick.dueDate, QDate::currentDate());
                if (changed)
                    QVERIFY(!inMain.dueDate.isValid());
                else
                    QCOMPARE(inMain.dueDate, QDate::currentDate());
            };
            v << c;
        }
        {
            K c{"tasks.defaultDueNotQuickEntry", "Tasks", "setting:tasks.defaultDueNotQuickEntry", K::Check, false};
            c.prepare = [](Store &st, Seed &) { st.setSetting("tasks.defaultDueOffset", 0); };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                Task inMain, inQuick;
                newTaskByKeyboard(app, unique("Dm"), inMain);
                const QString stem = unique("Dq");
                quickAdd(app, stem, stem, inQuick);
                QCOMPARE(inMain.dueDate, QDate::currentDate());
                if (changed)
                    QVERIFY(!inQuick.dueDate.isValid());
                else
                    QCOMPARE(inQuick.dueDate, QDate::currentDate());
            };
            v << c;
        }
        {
            K c{"tasks.todayDue", "Tasks", "setting:tasks.todayDue", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "today");
                Task t;
                newTaskByKeyboard(app, unique("Td"), t);
                if (changed)
                    QVERIFY(!t.dueDate.isValid());
                else
                    QCOMPARE(t.dueDate, QDate::currentDate());
            };
            v << c;
        }
        {
            K c{"tasks.useDefaultDueTime", "Tasks", "setting:tasks.useDefaultDueTime", K::Check, false};
            c.prepare = [](Store &st, Seed &) { st.setSetting("tasks.defaultDueTime", 14 * 60); };
            // "The time a task gets the first time you give it a due date without choosing one."
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                int time = -9;
                dueTimeFromCalendar(app, time);
                QCOMPARE(time, changed ? 14 * 60 : -1);
            };
            v << c;
        }
        {
            K c{"tasks.defaultDueTime", "Tasks", "setting:tasks.defaultDueTime", K::Combo};
            c.fromIndex = 18;  // 9:00 AM
            c.toIndex = 28;    // 2:00 PM
            c.stored = "840";
            c.prepare = [](Store &st, Seed &) { st.setSetting("tasks.useDefaultDueTime", true); };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                int time = -9;
                dueTimeFromCalendar(app, time);
                QCOMPARE(time, changed ? 14 * 60 : 9 * 60);
            };
            v << c;
        }
        {
            K c{"tasks.returnCreates", "Tasks", "setting:tasks.returnCreates", K::Check, false};
            // "Hit Return to create a new task, Ctrl+Return to edit."
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                QQuickItem *row = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((row = app.row(s.ids.buyMilk)) != nullptr, 5000);
                click(row, Qt::LeftButton, pointIn(row, 0.4, 0.5));
                QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), s.ids.buyMilk, 5000);
                app.press(Qt::Key_Return);
                QQuickItem *title = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((title = findItem("editorTitle")) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(title->property("text").toString(), changed ? QString() : QString("Buy milk"), 5000);
                closeEditorWithEscape(app);
            };
            v << c;
        }
        {
            K c{"quickopen.selectInPlace", "Tasks", "setting:quickopen.selectInPlace", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "all");
                app.press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
                QQuickItem *input = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((input = findItem("commandInput")) != nullptr, 5000);
                app.type("Water the plants");
                app.press(Qt::Key_Return);
                if (changed)
                    QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), listKey(s.ids.home), 5000);
                else
                    QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), s.ids.plants, 5000);
                QCOMPARE(app.backend().currentTaskId(), s.ids.plants);
                if (!changed)
                    QCOMPARE(app.backend().sourceKey(), QString("all"));
            };
            v << c;
        }
        {
            K c{"tasks.dueTimeChips", "Tasks", "setting:tasks.dueTimeChips", K::Text};
            c.toText = "7am, 8pm";
            c.stored = "7am, 8pm";
            // The shortcuts offered when picking a due time.
            c.effect = SK_EFFECT {
                SK_APP
                QStringList labels;
                for (const QVariant &chip : app.backend().dueTimeChips())
                    labels << chip.toMap().value("label").toString();
                if (changed)
                    QCOMPARE(labels, (QStringList{"7 AM", "8 PM"}));
                else
                    QCOMPARE(labels, (QStringList{"9 AM", "12 PM", "3 PM", "6 PM"}));
            };
            v << c;
        }
        {
            K c{"tasks.quickLook", "Tasks", "setting:tasks.quickLook", K::Check, true};
            // "Space on a task opens a zoomed-in view of it."
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                QQuickItem *row = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((row = app.row(s.ids.buyMilk)) != nullptr, 5000);
                click(row, Qt::LeftButton, pointIn(row, 0.4, 0.5));
                QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), s.ids.buyMilk, 5000);
                app.press(Qt::Key_Space);
                QObject *look = findObject("quickLook");
                QVERIFY(look);
                if (changed) {
                    QTest::qWait(300);
                    QVERIFY(!look->property("visible").toBool());
                } else {
                    QTRY_VERIFY_WITH_TIMEOUT(look->property("visible").toBool(), 5000);
                    closeQuickLook(app, look);
                }
            };
            v << c;
        }
        // ---------------------------------------------------------- My Day
        // The plan and its capacity come from backend.myDayCapacity(); the
        // work day's two ends change the window it reports.
        {
            K c{"myday.enabled", "Tasks", "setting:myday.enabled", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                const QVariantMap cap = app.backend().myDayCapacity();
                if (changed) {
                    // Off: the plan is empty and there is no window.
                    QCOMPARE(cap.value("windowMinutes").toInt(), 0);
                    QCOMPARE(cap.value("plannedMinutes").toInt(), 0);
                    QCOMPARE(cap.value("freeMinutes").toInt(), 0);
                } else {
                    const QTime now = QTime::currentTime();
                    const int nowMinute = now.hour() * 60 + now.minute();
                    const int expected = qMax(0, 17 * 60 - qMax(nowMinute, 9 * 60));
                    QVERIFY(qAbs(cap.value("windowMinutes").toInt() - expected) <= 1);
                }
            };
            v << c;
        }
        {
            K c{"myday.workStart", "Tasks", "setting:myday.workStart", K::Combo};
            c.fromIndex = 18;  // 9:00
            c.toIndex = 16;    // 8:00
            c.stored = "480";
            c.effect = SK_EFFECT {
                SK_APP
                const QVariantMap cap = app.backend().myDayCapacity();
                const QTime now = QTime::currentTime();
                const int nowMinute = now.hour() * 60 + now.minute();
                const int expected = qMax(0, 17 * 60 - qMax(nowMinute, changed ? 8 * 60 : 9 * 60));
                QVERIFY(qAbs(cap.value("windowMinutes").toInt() - expected) <= 1);
            };
            v << c;
        }
        {
            K c{"myday.workEnd", "Tasks", "setting:myday.workEnd", K::Combo};
            c.fromIndex = 34;  // 17:00
            c.toIndex = 36;    // 18:00
            c.stored = "1080";
            c.effect = SK_EFFECT {
                SK_APP
                const QVariantMap cap = app.backend().myDayCapacity();
                const QTime now = QTime::currentTime();
                const int nowMinute = now.hour() * 60 + now.minute();
                const int expected = qMax(0, (changed ? 18 * 60 : 17 * 60) - qMax(nowMinute, 9 * 60));
                QVERIFY(qAbs(cap.value("windowMinutes").toInt() - expected) <= 1);
            };
            v << c;
        }
        {
            K c{"myday.prompt", "Tasks", "setting:myday.prompt", K::Check, false};
            // The once-a-day prompt is driven by E2EMyDay::promptIsAskedOnceADay.
            c.slot = "E2EMyDay::promptIsAskedOnceADay";
            v << c;
        }
}

}  // namespace e2e
