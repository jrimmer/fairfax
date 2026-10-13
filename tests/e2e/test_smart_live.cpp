// End-to-end scenarios for live updates: "its rule keeps applying as your
// tasks change, so the list is always up to date" (smart-lists.md, Saving a
// Filter as a Smart List). With a Smart List open, a task is changed through
// the keyboard, the way a user does; the list loses it, undo gives it back,
// and a new task typed into the list with the property joins it, all without
// selecting the list again. The sidebar's count follows each step.

#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

const SL Hi{"Rent overdue", "Fence in 6 days", "Starred urgent", "Launch project"};
const SL DueToday{"Milk today", "Dentist today", "Two day window", "Fresh window", "Daily repeat", "Repeating window"};
const SL Starred{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote", "Plumber for garden", "Plumber for office"};

SL inLists(const SL &lists) {
    return specTitles([&](const Spec &s) { return shownInAll(s) && lists.contains(s.list); });
}

SL plus(SL base, const QString &title) {
    base << title;
    return base;
}

}  // namespace

class E2ESmartLive : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;
    qint64 m_list = 0;

    // Saves the query as a Smart List from the search bar; it opens.
    void openSmart(const QString &query, const QStringList &expected) {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        m_list = saveSearchAsSmart(app, query);
        QVERIFY(m_list);
        showsAndCounts(expected);
        if (QTest::currentTestFailed())
            return;
    }

    // The pane lists exactly these rows and the sidebar row counts them.
    void showsAndCounts(const QStringList &expected) {
        App &app = *m_app;
        SMART_SHOWS(app, expected);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(m_list)).undone, expected.size(), 3000);
        QCOMPARE(app.backend().sourceKey(), lk(m_list));
    }

    void undo() { m_app->press(Qt::Key_Z, Qt::ControlModifier); }

    // Ctrl+N, a title, Return: a new task typed into the open Smart List.
    void typeNewTask(const QString &title) {
        App &app = *m_app;
        app.press(Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().editingTaskId() != 0, 3000);
        app.type(title);
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().editingTaskId() == 0, 3000);
    }

private slots:
    void init() {
        m_app = std::make_unique<App>(seedSmart);
        QVERIFY(m_app->ok);
    }
    void cleanup() { m_app.reset(); }

    // Untag (Alt+L) takes the task out of `tags: urgent`; undo returns it; a
    // task typed with #urgent joins.
    void tagAndUntag() {
        App &app = *m_app;
        openSmart("tags: urgent", SL{"Rent overdue"});
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Rent overdue"));
        app.press(Qt::Key_L, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") != nullptr, 3000);
        app.type("urgent");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Rent overdue").tagIds.isEmpty(), 3000);
        showsAndCounts(SL{});
        if (QTest::currentTestFailed())
            return;
        // Undo.
        undo();
        QTRY_VERIFY_WITH_TIMEOUT(!app.taskTitled("Rent overdue").tagIds.isEmpty(), 3000);
        showsAndCounts(SL{"Rent overdue"});
        if (QTest::currentTestFailed())
            return;
        // A new task tagged in its title.
        typeNewTask("Buy tape #urgent");
        if (QTest::currentTestFailed())
            return;
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Buy tape").id != 0, 3000);
        showsAndCounts(SL{"Rent overdue", "Buy tape"});
        if (QTest::currentTestFailed())
            return;
        // And the whole tag removed with Ctrl+Alt+R.
        QVERIFY(selectRow(app, "Buy tape"));
        app.press(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Buy tape").tagIds.isEmpty(), 3000);
        showsAndCounts(SL{"Rent overdue"});
        if (QTest::currentTestFailed())
            return;
    }

    // Tagging from the tag bar changes a list that excludes the tag: `tags:
    // ~` is the untagged tasks, and tagging one (Ctrl+R) takes it out.
    void taggingRemovesFromUntaggedList() {
        App &app = *m_app;
        openSmart("tags: ~ lists: Garden", inLists(SL{"Garden"}));
        if (QTest::currentTestFailed())
            return;
        QVERIFY(paneTitles(app).contains("Taxes in 9 days"));
        QVERIFY(selectRow(app, "Taxes in 9 days"));
        app.press(Qt::Key_R, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") != nullptr, 3000);
        app.type("urgent");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!app.taskTitled("Taxes in 9 days").tagIds.isEmpty(), 3000);
        showsAndCounts(without(inLists(SL{"Garden"}), SL{"Taxes in 9 days"}));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Taxes in 9 days").tagIds.isEmpty(), 3000);
        showsAndCounts(inLists(SL{"Garden"}));
        if (QTest::currentTestFailed())
            return;
    }

    // Changing priority: `type: hiprio`. Key 1 lowers one task out of it; undo
    // returns it; "!!!" in a new title joins.
    void priorityChange() {
        App &app = *m_app;
        openSmart("type: hiprio", Hi);
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Fence in 6 days"));
        app.press(Qt::Key_1);
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Fence in 6 days").priority, 1, 3000);
        showsAndCounts(without(Hi, SL{"Fence in 6 days"}));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Fence in 6 days").priority, 3, 3000);
        showsAndCounts(Hi);
        if (QTest::currentTestFailed())
            return;
        typeNewTask("Call bank !!!");
        if (QTest::currentTestFailed())
            return;
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Call bank").id != 0, 3000);
        QCOMPARE(app.taskTitled("Call bank").priority, 3);
        showsAndCounts(plus(Hi, "Call bank"));
        if (QTest::currentTestFailed())
            return;
        // Priority 0 (key 0) clears it again.
        QVERIFY(selectRow(app, "Call bank"));
        app.press(Qt::Key_0);
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Call bank").priority, 0, 3000);
        showsAndCounts(Hi);
        if (QTest::currentTestFailed())
            return;
    }

    // Changing a date: `type: duetoday`. The ] key moves the due date a day on;
    // undo returns it; a new task typed with "today" joins; Y moves it to
    // tomorrow.
    void dateChange() {
        App &app = *m_app;
        openSmart("type: duetoday", DueToday);
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Milk today"));
        app.press(Qt::Key_BracketRight);
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Milk today").dueDate, QDate::currentDate().addDays(1), 3000);
        showsAndCounts(without(DueToday, SL{"Milk today"}));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Milk today").dueDate, QDate::currentDate(), 3000);
        showsAndCounts(DueToday);
        if (QTest::currentTestFailed())
            return;
        typeNewTask("Pay bill today");
        if (QTest::currentTestFailed())
            return;
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Pay bill").id != 0, 3000);
        QCOMPARE(app.taskTitled("Pay bill").dueDate, QDate::currentDate());
        showsAndCounts(plus(DueToday, "Pay bill"));
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Pay bill"));
        app.press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Pay bill").dueDate, QDate::currentDate().addDays(1), 3000);
        showsAndCounts(DueToday);
        if (QTest::currentTestFailed())
            return;
    }

    // Moving to another list: `lists: Work`. M and a list name move a task
    // out; undo returns it; "/Work/" in a new title joins.
    void moveList() {
        App &app = *m_app;
        const SL work = inLists(SL{"Work"});
        openSmart("lists: Work", work);
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Report in 3 days"));
        app.press(Qt::Key_M);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") != nullptr, 3000);
        app.type("Garden");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Report in 3 days").listId, listId("Garden"), 3000);
        showsAndCounts(without(work, SL{"Report in 3 days"}));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_COMPARE_WITH_TIMEOUT(app.taskTitled("Report in 3 days").listId, listId("Work"), 3000);
        showsAndCounts(work);
        if (QTest::currentTestFailed())
            return;
        typeNewTask("Send mail /Work/");
        if (QTest::currentTestFailed())
            return;
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Send mail").id != 0, 3000);
        QCOMPARE(app.taskTitled("Send mail").listId, listId("Work"));
        showsAndCounts(plus(work, "Send mail"));
        if (QTest::currentTestFailed())
            return;
    }

    // Starring: `type: starred`. Ctrl+Alt+S removes the star; undo returns it;
    // "*" in a new title joins.
    void starAndUnstar() {
        App &app = *m_app;
        openSmart("type: starred", Starred);
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Starred plain"));
        app.press(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.taskTitled("Starred plain").starred, 3000);
        showsAndCounts(without(Starred, SL{"Starred plain"}));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Starred plain").starred, 3000);
        showsAndCounts(Starred);
        if (QTest::currentTestFailed())
            return;
        typeNewTask("Buy milk *");
        if (QTest::currentTestFailed())
            return;
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Buy milk").id != 0, 3000);
        QVERIFY(app.taskTitled("Buy milk").starred);
        showsAndCounts(plus(Starred, "Buy milk"));
        if (QTest::currentTestFailed())
            return;
    }

    // Completing: `lists: Work` leaves completed tasks out, so completing a
    // task (Ctrl+.) takes it out; undo returns it. Done tasks do not appear
    // while the list leaves them out.
    void completeAndUncomplete() {
        App &app = *m_app;
        const SL work = inLists(SL{"Work"});
        openSmart("lists: Work", work);
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Report in 3 days"));
        app.press(Qt::Key_Period, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Report in 3 days").completed, 3000);
        // The count drops at once; the row stays in place, struck through, until
        // the view is left, as a just-completed task does everywhere.
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(m_list)).undone, work.size() - 1, 3000);
        SMART_SHOWS(app, work);
        QVERIFY(pane(app, "all"));
        QVERIFY(pane(app, lk(m_list)));
        showsAndCounts(without(work, SL{"Report in 3 days"}));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_VERIFY_WITH_TIMEOUT(!app.taskTitled("Report in 3 days").completed, 3000);
        showsAndCounts(work);
        if (QTest::currentTestFailed())
            return;
    }

    // Completing a project's last open sub-task makes it an empty project:
    // `type: emptyproj` gains the project while the list stays open.
    void completingTheLastSubtaskEmptiesAProject() {
        App &app = *m_app;
        const SL empties{"Empty project", "Finished project"};
        openSmart("type: emptyproj", empties);
        if (QTest::currentTestFailed())
            return;
        // The sub-tasks of "Launch project" are not in this list; complete them
        // through the project's own view and come back.
        QVERIFY(pane(app, lk(listId("Website"))));
        QVERIFY(selectRow(app, "Launch project"));
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().sourceKey().startsWith("project:"), 3000);
        for (const QString &title : {QString("Launch step one"), QString("Launch step later")}) {
            QVERIFY(selectRow(app, title));
            app.press(Qt::Key_Period, Qt::ControlModifier);
            QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled(title).completed, 3000);
        }
        QVERIFY(pane(app, lk(m_list)));
        showsAndCounts(plus(empties, "Launch project"));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_VERIFY_WITH_TIMEOUT(!app.taskTitled("Launch step later").completed, 3000);
        showsAndCounts(empties);
        if (QTest::currentTestFailed())
            return;
    }

    // Deleting: Delete takes a task out of every list it was in; undo brings
    // it back into the open Smart List.
    void deleteAndUndo() {
        App &app = *m_app;
        const SL work = inLists(SL{"Work"});
        openSmart("lists: Work", work);
        if (QTest::currentTestFailed())
            return;
        QVERIFY(selectRow(app, "Report in 3 days"));
        app.press(Qt::Key_Delete);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Report in 3 days").id == 0, 3000);
        showsAndCounts(without(work, SL{"Report in 3 days"}));
        if (QTest::currentTestFailed())
            return;
        undo();
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Report in 3 days").id != 0, 3000);
        showsAndCounts(work);
        if (QTest::currentTestFailed())
            return;
    }

    // The counts on the other Smart Lists follow too: one change, two open
    // lists' badges.
    void everyListCountFollowsAChange() {
        App &app = *m_app;
        openSmart("type: hiprio", Hi);
        if (QTest::currentTestFailed())
            return;
        const qint64 hiprio = m_list;
        QVERIFY(pane(app, "all"));
        const qint64 work = saveSearchAsSmart(app, "lists: Work");
        QVERIFY(work);
        QVERIFY(pane(app, lk(hiprio)));
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(work)).undone, inLists(SL{"Work"}).size(), 3000);
        QVERIFY(selectRow(app, "Starred urgent"));   // High priority, in Work.
        app.press(Qt::Key_1);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(hiprio)).undone, Hi.size() - 1, 3000);
        QCOMPARE(badgeOf(app, lk(work)).undone, inLists(SL{"Work"}).size());
        QVERIFY(selectRow(app, "Fence in 6 days"));
        app.press(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(hiprio)).undone, Hi.size() - 2, 3000);
        undo();
        undo();
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(hiprio)).undone, Hi.size(), 3000);
    }
};

REGISTER_TEST(E2ESmartLive)

#include "test_smart_live.moc"
