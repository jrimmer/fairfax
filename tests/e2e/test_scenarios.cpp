// End-to-end scenarios: each boots the real window on a fresh temporary
// database and does what the the reference app manual says a user does, with the mouse and
// keyboard, then checks the result in the store or on screen.

#include "harness.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

QString isoOf(const QDate &d) { return d.toString(Qt::ISODate); }

// Waits for a visible item to exist (rows, popups, menu entries appear after
// an event-loop turn or two).
#define TRY_ITEM(var, expr) \
    QQuickItem *var = nullptr; \
    QTRY_VERIFY_WITH_TIMEOUT((var = (expr)) != nullptr, 3000)

}  // namespace

class E2EScenarios : public QObject {
    Q_OBJECT

private slots:
    // "Opening the Editor: Double-click a task in the Task List. Select a task
    // and press Return." (task-editor.md) The editor must show the task's own
    // title, and leaving it with Return or Escape must not touch the task.
    void doubleClickOpensEditorWithTitle() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        for (Qt::Key leave : {Qt::Key_Return, Qt::Key_Escape}) {
            TRY_ITEM(row, app.row(app.ids.buyMilk));
            doubleClick(row, pointIn(row, 0.4, 0.5));
            TRY_ITEM(title, findItem("editorTitle"));
            // The title must be there as soon as the editor is, not blank
            // until something else refreshes it.
            QCOMPARE(title->property("text").toString(), QString("Buy milk"));
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().editor().value("title").toString(), QString("Buy milk"), 2000);
            app.press(leave);
            QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 3000);
            QCOMPARE(app.task(app.ids.buyMilk).title, QString("Buy milk"));
        }

        // Editing the title through the editor sticks.
        TRY_ITEM(row, app.row(app.ids.buyMilk));
        doubleClick(row, pointIn(row, 0.4, 0.5));
        TRY_ITEM(title, findItem("editorTitle"));
        QCOMPARE(title->property("text").toString(), QString("Buy milk"));
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("Buy oat milk");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.buyMilk).title, QString("Buy oat milk"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 3000);
        QVERIFY(app.row(app.ids.buyMilk));
    }

    // "⌘N New Task" (keyboard-shortcuts.md). A new task starts in the list you
    // are looking at; in Today it is due today.
    void newTaskWithShortcut() {
        App app;
        QVERIFY(app.ok);
        const QString key = "list:" + QString::number(app.ids.work);
        QVERIFY(app.openSource(key));
        app.press(Qt::Key_N, Qt::ControlModifier);
        TRY_ITEM(title, findItem("editorTitle"));
        QEXPECT_FAIL("", "BUG: Ctrl+N opens the new task's editor but leaves keyboard focus on the list row loader, so typing goes nowhere", Continue);
        QTRY_VERIFY2_WITH_TIMEOUT(title->property("activeFocus").toBool(), qPrintable("focus is on " + app.focusDescription()), 3000);
        click(title);  // What a user does when typing does nothing.
        app.type("Plan the offsite");
        app.press(Qt::Key_Return);
        QTRY_VERIFY2_WITH_TIMEOUT(app.taskTitled("Plan the offsite").id != 0, qPrintable(app.titles()), 3000);
        QCOMPARE(app.taskTitled("Plan the offsite").listId, app.ids.work);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 3000);

        app.press(Qt::Key_2, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("today"), 2000);
        app.press(Qt::Key_N, Qt::ControlModifier);
        TRY_ITEM(todayTitle, findItem("editorTitle"));
        QEXPECT_FAIL("", "BUG: Ctrl+N opens the new task's editor but leaves keyboard focus on the list row loader (Today)", Continue);
        QTRY_VERIFY2_WITH_TIMEOUT(todayTitle->property("activeFocus").toBool(), qPrintable("focus is on " + app.focusDescription()), 3000);
        click(todayTitle);
        app.type("Send the invoice");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Send the invoice").id != 0, 3000);
        QCOMPARE(app.taskTitled("Send the invoice").dueDate, QDate::currentDate());
    }

    // "The first column of every task row is a checkbox. Click it to mark
    // complete." (the-window.md) and "Edit > Undo (⌘Z)" (backups.md).
    void checkboxCompletesAndUndoRestores() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        TRY_ITEM(box, findItem(QString("taskCheck:%1").arg(app.ids.plants)));
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.plants).completed, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(app.ids.plants).completed, 3000);
    }

    // "`Collect the parcel tomorrow at 3pm #errands`... arrive with their
    // fields filled in." (quick-entry.md) Natural language in the title sets
    // the due date and time, tag and priority.
    void quickEntryNaturalLanguage() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_2, Qt::ShiftModifier | Qt::ControlModifier);
        QQuickWindow *quick = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((quick = windowTitled("Quick Entry")) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(quick->isExposed(), 3000);
        quick->requestActivate();
        TRY_ITEM(field, findItem("quickTitle"));
        click(field);
        typeInto(quick, "Call Sam tomorrow at 3pm #calls !!");
        QTest::keyClick(quick, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Call Sam").id != 0, 3000);
        const Task t = app.taskTitled("Call Sam");
        QCOMPARE(t.dueDate, QDate::currentDate().addDays(1));
        QCOMPARE(t.dueTime, 15 * 60);
        QCOMPARE(t.priority, int(Priority::Medium));
        QStringList tags;
        for (qint64 id : t.tagIds)
            tags << app.store().tag(id).name;
        QCOMPARE(tags, QStringList{"calls"});
        QTRY_VERIFY_WITH_TIMEOUT(!windowTitled("Quick Entry"), 3000);
    }

    // "You can also switch styles from the main window... the calendar button,
    // the one that shows and hides the Mini Calendar... a Display style: menu"
    // and "Shade Weekends" (mini-calendar.md). Every choice shows at once.
    void settingsApplyLive() {
        App app;
        QVERIFY(app.ok);
        TRY_ITEM(calendar, findItem("miniCalendar"));

        // Hide and show the calendar.
        click(findItem("calendarToggle"));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("miniCalendar"), 2000);
        click(findItem("calendarToggle"));
        TRY_ITEM(shown, findItem("miniCalendar"));
        calendar = shown;

        auto pick = [&](const QString &entry) {
            click(findItem("calendarStyle"));
            QQuickItem *item = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((item = findText(entry, "QQuickMenuItem")) != nullptr, 3000);
            click(item);
        };
        auto weekendShaded = [&] {
            QQuickItem *cal = findItem("miniCalendar");
            if (!cal) return false;
            bool any = false;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *i) {
                if (i->objectName() == "calWeekendShade" && i->isVisible()) any = true;
                for (QQuickItem *c : i->childItems()) walk(c);
            };
            walk(cal);
            return any;
        };
        auto dotsShown = [&] { return findItem("calDots") != nullptr; };

        QVERIFY(!weekendShaded());
        pick("Shade Weekends");
        QTRY_VERIFY_WITH_TIMEOUT(weekendShaded(), 2000);
        pick("Shade Weekends");
        QTRY_VERIFY_WITH_TIMEOUT(!weekendShaded(), 2000);

        pick("Dots");
        QTRY_COMPARE_WITH_TIMEOUT(findItem("miniCalendar")->property("style").toString(), QString("dots"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(dotsShown(), 2000);
        pick("None");
        QTRY_COMPARE_WITH_TIMEOUT(findItem("miniCalendar")->property("style").toString(), QString("none"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!dotsShown(), 2000);
        pick("Heat Map");
        QTRY_COMPARE_WITH_TIMEOUT(findItem("miniCalendar")->property("style").toString(), QString("heat"), 2000);
        QVERIFY(!dotsShown());
    }

    // "Click a day to filter the current task list to just the tasks for that
    // date. Click the same day again to clear the filter." and "Click and drag
    // across several days to filter the task list to that range."
    // (mini-calendar.md)
    void miniCalendarFiltersDays() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_1, Qt::ControlModifier);  // All
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("all"), 2000);
        const QDate today = QDate::currentDate();
        auto day = [&](int offset) { return findItem("calDay:" + isoOf(today.addDays(offset))); };
        TRY_ITEM(row, app.row(app.ids.report));  // Due in two days.
        QVERIFY(app.row(app.ids.studio));

        TRY_ITEM(tomorrow, day(1));
        click(tomorrow);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().dayFilter(), isoOf(today.addDays(1)), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.plants) && app.row(app.ids.callAlex), 2000);  // Both due tomorrow.
        QTRY_VERIFY_WITH_TIMEOUT(!app.row(app.ids.report) && !app.row(app.ids.studio) && !app.row(app.ids.buyMilk), 2000);

        click(day(1));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().dayFilter(), QString(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.report) && app.row(app.ids.buyMilk), 2000);

        drag(day(2), day(3));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().dayFilter(), isoOf(today.addDays(2)), 2000);
        QCOMPARE(app.backend().dayFilterTo(), isoOf(today.addDays(3)));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.report) && app.row(app.ids.studio), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.row(app.ids.plants) && !app.row(app.ids.buyMilk), 2000);
    }

    // "Focus Lists" and lists in the Lists Panel, and "⌘1 All, ⌘2 Today,
    // ⌘3 Starred, ⌘4 Scheduled, ⌘5 Done" (keyboard-shortcuts.md): choosing one
    // changes the task pane's title and rows.
    void sidebarSwitchesLists() {
        App app;
        QVERIFY(app.ok);
        auto title = [&]() -> QString {
            QQuickItem *t = findItem("paneTitle");
            return t ? t->property("text").toString() : QString();
        };

        QVERIFY(app.openSource("list:" + QString::number(app.ids.work)));
        QTRY_COMPARE_WITH_TIMEOUT(title(), QString("Work"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.report) && !app.row(app.ids.buyMilk), 2000);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        QTRY_COMPARE_WITH_TIMEOUT(title(), QString("Home"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) && !app.row(app.ids.report), 2000);

        QVERIFY(app.openSource("today"));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) && !app.row(app.ids.plants), 2000);
        QVERIFY(app.openSource("starred"));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.shelf) && !app.row(app.ids.buyMilk), 2000);

        // Ctrl+1..5.
        struct Case { Qt::Key key; const char *source; };
        for (const Case &c : {Case{Qt::Key_1, "all"}, Case{Qt::Key_2, "today"}, Case{Qt::Key_3, "starred"},
                              Case{Qt::Key_4, "scheduled"}, Case{Qt::Key_5, "done"}}) {
            app.press(c.key, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString(c.source), 2000);
            QTRY_COMPARE_WITH_TIMEOUT(title(), app.backend().sourceTitle(), 2000);
        }
        // Done holds the one completed task and nothing open.
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.oldThing) && !app.row(app.ids.buyMilk), 2000);
        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) && app.row(app.ids.report) && !app.row(app.ids.oldThing), 2000);
    }

    // "⌘F: open the Global Search Bar... Filter the task list" (search-and-jump-bars.md).
    // Typing filters the rows; Escape clears the search.
    void searchFiltersAndEscapeClears() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) && app.row(app.ids.report), 2000);
        app.press(Qt::Key_F, Qt::ControlModifier);
        TRY_ITEM(field, findItem("searchField"));
        QTRY_VERIFY_WITH_TIMEOUT(field->property("activeFocus").toBool(), 2000);
        app.type("milk");
        // Each keystroke refilters the list, and the list takes focus back on
        // every view change, so only the first letter reaches the field (and
        // the rest run as bare-key list commands).
        QEXPECT_FAIL("", "BUG: search field loses focus to the list after the first typed character", Abort);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().search(), QString("milk"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) && !app.row(app.ids.report) && !app.row(app.ids.plants), 2000);
        app.press(Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().search(), QString(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.report) && app.row(app.ids.plants), 2000);
    }

    // "Defer...: pick a relative offset (Today / Tomorrow / ...)"
    // (multi-task-actions.md): right-click a task, Defer, Tomorrow.
    void contextMenuDefersToTomorrow() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        TRY_ITEM(row, app.row(app.ids.electricity));  // Overdue by two days.
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        TRY_ITEM(defer, findText("Defer", "QQuickMenuItem"));
        Q_UNUSED(defer);
        // The submenu opens on hover or click, after a short delay; a click
        // that lands while it is opening can close it again, so look first.
        QQuickItem *tomorrow = nullptr;
        for (int attempt = 0; attempt < 4 && !tomorrow; ++attempt) {
            if (QQuickItem *entry = findText("Defer", "QQuickMenuItem"); entry && entry->window()) {
                if (attempt == 0)
                    QTest::mouseMove(entry->window(), centreOf(entry));  // Hovering opens it.
                else
                    click(entry);
            }
            QTest::qWait(700);
            tomorrow = findText("Tomorrow", "QQuickMenuItem");
        }
        if (!tomorrow) {
            QStringList seen;
            for (QQuickWindow *w : quickWindows())
                findIn(w->contentItem(), [&](QQuickItem *i) {
                    if (i->inherits("QQuickMenuItem")) seen << i->property("text").toString();
                    return false;
                });
            QFAIL(qPrintable("Defer submenu did not open; visible menu entries: " + seen.join(" | ")));
        }
        click(tomorrow);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.electricity).dueDate, QDate::currentDate().addDays(1), 3000);
    }

    // "Right-clicking a list... Rename...: edit the name in place."
    // (lists-and-list-groups.md)
    void renameListFromContextMenu() {
        App app;
        QVERIFY(app.ok);
        TRY_ITEM(homeRow, app.sidebarRow("list:" + QString::number(app.ids.home)));
        click(homeRow, Qt::RightButton);
        TRY_ITEM(rename, findText("Rename…", "QQuickMenuItem"));
        click(rename);
        // The name becomes an editable field with everything selected.
        QTRY_VERIFY_WITH_TIMEOUT(app.window->activeFocusItem()
                                     && app.window->activeFocusItem()->property("text").toString() == "Home", 3000);
        app.type("Household");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(app.ids.home).name, QString("Household"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Household") != nullptr, 3000);
        QVERIFY(!findText("Home"));
    }
};

REGISTER_TEST(E2EScenarios)

#include "test_scenarios.moc"
