// End-to-end task tests, part 1: creating tasks (Ctrl+N, the + button, Return
// for the next task, Quick Entry, projects and checklists). Each test boots the
// real window, does what a user does with the mouse and keyboard, then
// checks the saved task and what the list shows.

#include "taskkit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::tk;

class E2ETasksCreate : public QObject {
    Q_OBJECT

private slots:
    // "⌘N New Task" (keyboard-shortcuts.md). A new task starts in the list you
    // are looking at; typing the title and pressing Return saves it, and one
    // undo takes it away again.
    void ctrlNInListThenUndo() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.work)));
        const int before = app.store().tasks().size();
        app.press(Qt::Key_N, Qt::ControlModifier);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY2_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), qPrintable(app.focusDescription()), 5000);
        app.type("Plan the offsite");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Plan the offsite").id != 0, 5000);
        const Task t = app.taskTitled("Plan the offsite");
        QCOMPARE(t.listId, app.ids.work);
        QVERIFY(!t.dueDate.isValid());
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rowText("taskTitle", t.id), QString("Plan the offsite"), 5000);

        // Undo steps back through the title, then the creation, and the row
        // goes with it.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Plan the offsite").id == 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rowText("taskTitle", t.id), QString("New Task"), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, t.id), 5000);
        QCOMPARE(int(app.store().tasks().size()), before);
    }

    // A new task left without a title never happened (Escape on the empty
    // editor leaves nothing behind).
    void newTaskWithoutTitleIsDiscarded() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        const int before = app.store().tasks().size();
        app.press(Qt::Key_N, Qt::ControlModifier);
        QVERIFY(waitItem("editorCard"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().tasks().size() == before + 1 || prop("editorTitle", "activeFocus").toBool(), 5000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
        for (const Task &t : app.store().tasks())
            QVERIFY2(t.deleted || !t.title.trimmed().isEmpty(), "an untitled task was kept");
    }

    // "Context inheritance": added in Today it is due today, so it shows in
    // Today's own section.
    void ctrlNInTodayIsDueToday() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_2, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("today"), 5000);
        app.press(Qt::Key_N, Qt::ControlModifier);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Send the invoice");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Send the invoice").id != 0, 5000);
        const Task t = app.taskTitled("Send the invoice");
        QCOMPARE(t.dueDate, QDate::currentDate());
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, t.id), 5000);
        QCOMPARE(sectionOf(t.id), QString("d:" + isoOf(QDate::currentDate())));
        QTRY_COMPARE_WITH_TIMEOUT(rowText("rowDue", t.id).isEmpty(), false, 5000);
    }

    // In Starred a new task is starred (and shows its star).
    void ctrlNInStarredIsStarred() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_3, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("starred"), 5000);
        app.press(Qt::Key_N, Qt::ControlModifier);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Pick up the parcel");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Pick up the parcel").id != 0, 5000);
        const Task t = app.taskTitled("Pick up the parcel");
        QVERIFY(t.starred);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowStar", t.id), 5000);
    }

    // A Smart List holds no tasks of its own, so a task made while looking at
    // one goes to the default list (and shows in the Smart List when it matches
    // the search, as an undated task does for "type: nodue").
    void ctrlNInSmartListLandsInARealList() {
        App app;
        QVERIFY(app.ok);
        const qint64 smart = app.backend().saveSmartList("Undated", "type: nodue");
        QVERIFY(smart > 0);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("list:%1").arg(smart), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.shelf) != nullptr, 5000);
        app.press(Qt::Key_N, Qt::ControlModifier);
        QVERIFY2(waitItem("editorTitle"), qPrintable("no editor; tasks: " + app.titles() + "; untitled in list "
                 + QString::number(app.store().tasks().size())));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Made in a smart list");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Made in a smart list").id != 0, 5000);
        const Task t = app.taskTitled("Made in a smart list");
        QVERIFY2(app.store().list(t.listId).kind != ListKind::Smart, "the task was filed in the Smart List itself");
        QVERIFY(t.listId == app.ids.home || t.listId == app.ids.work);
    }

    // The + button in the toolbar (tip "New Task (Ctrl+N)") starts a new task
    // in the current list; its right-click menu offers a project and a
    // checklist too.
    void plusButtonAndItsMenu() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        QVERIFY(clickNamed("newTaskButton"));
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Clean the gutters");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Clean the gutters").id != 0, 5000);
        QCOMPARE(app.taskTitled("Clean the gutters").listId, app.ids.home);
        QCOMPARE(app.taskTitled("Clean the gutters").type, int(TaskType::Task));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);

        // Right-click for the project entry.
        QQuickItem *plus = findItem("newTaskButton");
        click(plus, Qt::RightButton);
        QVERIFY(clickMenu("New Project"));
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY2_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), qPrintable("focus is on " + app.focusDescription()), 5000);
        app.type("Garden plan");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Garden plan").id != 0, 5000);
        QCOMPARE(app.taskTitled("Garden plan").type, int(TaskType::Project));
    }

    // "Hit Return to create a new task": with the setting on, Return on a
    // selected row starts the next task straight away, so a list can be typed
    // out title after title.
    void returnCreatesNextTask() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("tasks.returnCreates", true);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        QVERIFY(waitFor([&] { return app.row(app.ids.shelf) != nullptr; }));
        click(app.row(app.ids.shelf), Qt::LeftButton, pointIn(app.row(app.ids.shelf), 0.4, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), app.ids.shelf, 5000);
        app.press(Qt::Key_Return);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("First one");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("First one").id != 0, 5000);
        // Leave the editor, then Return on the (selected) row makes another.
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
        const qint64 first = app.taskTitled("First one").id;
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), first, 5000);
        app.press(Qt::Key_Return);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Second one");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Second one").id != 0, 5000);
        QCOMPARE(app.taskTitled("Second one").listId, app.ids.home);
        QVERIFY(app.taskTitled("Second one").id != first);

        // Switched off again, Return opens the selected task for editing.
        app.backend().setSetting("tasks.returnCreates", false);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
        const int count = app.store().tasks().size();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), app.taskTitled("Second one").id, 5000);
        app.press(Qt::Key_Return);
        QVERIFY(waitItem("editorTitle"));
        QCOMPARE(prop("editorTitle", "text").toString(), QString("Second one"));
        QCOMPARE(int(app.store().tasks().size()), count);
    }
};

REGISTER_TEST(E2ETasksCreate)

#include "test_tasks.moc"
