// End-to-end task tests, part 5: the life of a task in the list: completing and
// reopening (checkbox, menu, keyboard), undo and redo, delete, duplicate, the
// Defer / Schedule / Move menus, multi-select, dragging onto the sidebar, and
// how rows move between sections and into Done.

#include "taskkit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::tk;

namespace {

int liveCopies(App &app, const QString &title) {
    int n = 0;
    for (const Task &t : app.store().tasks())
        if (!t.deleted && t.title == title) ++n;
    return n;
}

bool strikeOut(qint64 id) {
    QQuickItem *t = findItem(QString("taskTitle:%1").arg(id));
    return t && t->property("font").value<QFont>().strikeOut();
}

}  // namespace

class E2ETasksLifecycle : public QObject {
    Q_OBJECT

    static QString homeKey(App &app) { return "list:" + QString::number(app.ids.home); }

private slots:
    // "The first column of every task row is a checkbox. Click it to mark
    // complete" (the-window.md): the row is struck through, Done lists it,
    // and clicking its checkbox there reopens it.
    void completeAndReopenWithCheckbox() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(!strikeOut(id));
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).completed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(strikeOut(id), 5000);

        app.press(Qt::Key_5, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("done"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
        QVERIFY(strikeOut(id));

        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).completed, 5000);
        QVERIFY(app.openSource(homeKey(app)));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
        QVERIFY(!strikeOut(id));
    }

    // The context menu: "Mark as Completed" / "Mark as Not Completed".
    void completeAndReopenFromMenu() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickMenu("Mark as Completed"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).completed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(strikeOut(id), 5000);

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickMenu("Mark as Not Completed"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).completed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!strikeOut(id), 5000);
    }

    // Keyboard: Ctrl+. completes the selected task, Ctrl+Alt+. reopens it.
    void completeAndReopenFromKeyboard() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(selectRow(app, id));
        app.press(Qt::Key_Period, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).completed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(strikeOut(id), 5000);
        app.press(Qt::Key_Period, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).completed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!strikeOut(id), 5000);
    }

    // "Edit > Undo (⌘Z)" and Redo (⌘⇧Z) (backups.md), through a run of edits.
    void undoAndRedoWalkThroughEdits() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(selectRow(app, id));
        app.press(Qt::Key_3);                       // High priority
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, 3, 5000);
        app.press(Qt::Key_S);                       // Star
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).starred, 5000);
        app.press(Qt::Key_T);                       // Due today
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, QDate::currentDate(), 5000);

        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, QDate::currentDate().addDays(1), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).starred, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowStar", id), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, 0, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowPriority", id), 5000);

        app.press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowPriority", id) || badge("rowStar", id), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).starred, 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, QDate::currentDate(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(QDate::currentDate())), 5000);
    }

    // Delete: the Delete key removes the selected task, Ctrl+Z brings it back.
    // The context menu's Delete and the editor's trash button do the same.
    void deleteWithUndo() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(selectRow(app, id));
        app.press(Qt::Key_Delete);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).deleted, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).deleted, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickMenu("Delete"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).deleted, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).deleted, 5000);

        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorDelete"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).deleted, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).deleted, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
    }

    // "Duplicate" from the menu and with Ctrl+D: a copy with the same details,
    // selected; one undo removes it.
    void duplicate() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;  // Due tomorrow, tagged "garden".
        QCOMPARE(liveCopies(app, "Water the plants"), 1);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickMenu("Duplicate"));
        QTRY_COMPARE_WITH_TIMEOUT(liveCopies(app, "Water the plants"), 2, 5000);
        qint64 copy = 0;
        for (const Task &t : app.store().tasks())
            if (!t.deleted && t.title == "Water the plants" && t.id != id) copy = t.id;
        QVERIFY(copy);
        QCOMPARE(app.task(copy).dueDate, app.task(id).dueDate);
        QCOMPARE(app.task(copy).tagIds, app.task(id).tagIds);
        QCOMPARE(app.task(copy).listId, app.task(id).listId);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, copy), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), copy, 5000);

        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(liveCopies(app, "Water the plants"), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, copy), 5000);

        QVERIFY(selectRow(app, id));
        app.press(Qt::Key_D, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(liveCopies(app, "Water the plants"), 2, 5000);
    }

    // "Defer...: pick a relative offset (Today / Tomorrow / ...)"
    // (multi-task-actions.md): every entry of the Defer submenu, then
    // "No Due Date".
    void deferMenuEntries() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        const QDate today = QDate::currentDate();
        struct Entry { const char *label; int days; };
        for (const Entry &e : {Entry{"Today", 0}, Entry{"Tomorrow", 1}, Entry{"In 2 Days", 2}, Entry{"In 3 Days", 3},
                               Entry{"Next Week", 7}, Entry{"In 2 Weeks", 14}, Entry{"In a Month", 30}}) {
            QVERIFY(contextMenuOn(app, id));
            QVERIFY2(clickSub("Defer", e.label), e.label);
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(e.days), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(today.addDays(e.days))), 5000);
        }
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Defer", "No Due Date"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).dueDate.isValid(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("nodue"), 5000);
    }

    // "Schedule" sets the start date: a future start hides the task from the
    // list into Scheduled until it begins; "No Start Date" brings it back.
    void scheduleMenuEntries() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        const QDate today = QDate::currentDate();
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "Today"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startDate, today, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);     // Starts today: still in the list.

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "In 3 Days"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startDate, today.addDays(3), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);        // Scheduled: hidden from Home.

        app.press(Qt::Key_4, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("scheduled"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "Next Week"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startDate, today.addDays(7), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "No Start Date"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).startDate.isValid(), 5000);
        QVERIFY(app.openSource(homeKey(app)));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
    }

    // "Move To" in the right-click menu moves the task to another list; the row
    // follows it there.
    void moveToListFromMenu() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Move To", "Work"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).listId, app.ids.work, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.work)));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Move To", "Home"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).listId, app.ids.home, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);
    }

    // "Selecting multiple tasks: Shift-click for a range, Ctrl-click to add or
    // remove one" (multi-task-actions.md); the count shows in the pane.
    void multiSelectWithShiftAndCtrl() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        // Home, by due date: electricity, Buy milk, plants, shelf.
        QVERIFY(selectRow(app, app.ids.buyMilk));
        QCOMPARE(app.backend().selectionCount(), 1);
        clickWith(app.row(app.ids.shelf), Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 3, 5000);   // Buy milk .. shelf
        clickWith(app.row(app.ids.plants), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);   // plants dropped
        clickWith(app.row(app.ids.electricity), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 3, 5000);   // electricity added
        // A plain click goes back to one.
        QVERIFY(selectRow(app, app.ids.plants));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 1, 5000);
    }

    // Complete, priority, tag, defer, move and delete, each applied to a whole
    // selection (Buy milk + Fix the shelf) and not to the others.
    void actionsApplyToTheWholeSelection() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 a = app.ids.buyMilk, b = app.ids.shelf, other = app.ids.plants, over = app.ids.electricity;
        auto select = [&] {
            QVERIFY(selectRow(app, a));
            clickWith(app.row(b), Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        };

        // Priority (bare key) on both.
        select();
        app.press(Qt::Key_2);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(a).priority, 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(b).priority, 2, 5000);
        QCOMPARE(app.task(other).priority, 0);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowPriority", a), 5000);
        app.press(Qt::Key_0);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(a).priority, 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(b).priority, 0, 5000);

        // Tag from the Tag submenu.
        QVERIFY(contextMenuOn(app, a));
        QVERIFY(clickSub("Tag", "garden"));
        QTRY_COMPARE_WITH_TIMEOUT(tagNames(app, app.task(a)), QStringList{"garden"}, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tagNames(app, app.task(b)), QStringList{"garden"}, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowTagIcon", a) && badge("rowTagIcon", b), 5000);
        QVERIFY(contextMenuOn(app, a));
        QVERIFY(clickSub("Untag", "All Tags"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(a).tagIds.isEmpty() && app.task(b).tagIds.isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowTagIcon", a), 5000);

        // Defer both to tomorrow.
        QVERIFY(contextMenuOn(app, a));
        QVERIFY(clickSub("Defer", "Tomorrow"));
        const QDate tomorrow = QDate::currentDate().addDays(1);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(a).dueDate, tomorrow, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(b).dueDate, tomorrow, 5000);
        QCOMPARE(app.task(over).dueDate, QDate::currentDate().addDays(-2));

        // Complete both with the keyboard, reopen with the menu.
        app.press(Qt::Key_Period, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(a).completed && app.task(b).completed, 5000);
        QVERIFY(!app.task(other).completed);
        QVERIFY(contextMenuOn(app, a));
        QVERIFY(clickMenu("Mark as Not Completed"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(a).completed && !app.task(b).completed, 5000);

        // Move both to Work.
        QVERIFY(contextMenuOn(app, a));
        QVERIFY(clickSub("Move To", "Work"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(a).listId == app.ids.work && app.task(b).listId == app.ids.work, 5000);
        QCOMPARE(app.task(other).listId, app.ids.home);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, a) && rowGone(app, b), 5000);

        // Delete both from Work.
        QVERIFY(app.openSource("list:" + QString::number(app.ids.work)));
        QVERIFY(selectRow(app, a));
        clickWith(app.row(b), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        app.press(Qt::Key_Delete);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(a).deleted && app.task(b).deleted, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, a) && rowGone(app, b), 5000);
        QVERIFY(!app.task(app.ids.report).deleted);
        // One undo brings both back.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(a).deleted && !app.task(b).deleted, 5000);
    }

    // "Drag tasks onto a list in the sidebar to move them" (the drop hint reads
    // "Drop to Move"); one undo puts the task back.
    void dragOntoSidebarList() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(waitFor([&] { return app.row(id) && app.sidebarRow("list:" + QString::number(app.ids.work)); }));
        QVERIFY(settled(QString("taskRow:%1").arg(id)));
        dragSlowly(app.row(id), app.sidebarRow("list:" + QString::number(app.ids.work)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).listId, app.ids.work, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).listId, app.ids.home, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
    }

    // "...and onto Starred to star them" (the hint reads "Drop to Star").
    void dragOntoStarred() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(waitFor([&] { return app.row(id) && app.sidebarRow("starred"); }));
        QVERIFY(settled(QString("taskRow:%1").arg(id)));
        QVERIFY(!app.task(id).starred);
        dragSlowly(app.row(id), app.sidebarRow("starred"));
        QTRY_VERIFY2_WITH_TIMEOUT(app.task(id).starred, qPrintable(QString("due %3 start %4 done %5 selected %1, hint %2, sidebar row at y %6")
            .arg(app.backend().selectionCount()).arg(app.backend().dropHint("starred")).arg(app.task(id).dueDate.toString()).arg(app.task(id).startDate.toString()).arg(app.task(id).completed).arg(centreOf(app.sidebarRow("starred")).y())), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowStar", id), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).starred, 5000);
    }

    // Rows move between Overdue, the day sections and No Due Date as the due
    // date changes (bare keys T and Y, Alt+[ to clear), and a start date moves a
    // task out to Scheduled.
    void rowsMoveBetweenSections() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.electricity;   // Overdue.
        const QDate today = QDate::currentDate();
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("overdue"), 5000);
        QVERIFY(selectRow(app, id));
        app.press(Qt::Key_T);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(today)), 5000);
        app.press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(today.addDays(1))), 5000);
        app.press(Qt::Key_BracketLeft, Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("nodue"), 5000);
        // Back in time with undo: Overdue again.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("overdue"), 5000);

        // Today shows what is due today (and overdue), Scheduled what starts later.
        app.press(Qt::Key_2, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("today"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "In 2 Weeks"));
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 5000);
        app.press(Qt::Key_4, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 5000);
    }

    // "Done shows completed tasks": completing moves a task into Done, and the
    // seeded completed task is there from the start.
    void doneListShowsCompletedTasks() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_5, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("done"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, app.ids.oldThing), 5000);
        QVERIFY(!rowVisible(app, app.ids.buyMilk));

        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, app.ids.buyMilk), 5000);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(app.ids.buyMilk)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.buyMilk).completed, 5000);
        app.press(Qt::Key_5, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, app.ids.buyMilk) && rowVisible(app, app.ids.oldThing), 5000);
        QVERIFY(!rowVisible(app, app.ids.plants));

        // Reopened from Done, it leaves Done again.
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(app.ids.oldThing)));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(app.ids.oldThing).completed, 5000);
        app.press(Qt::Key_1, Qt::ControlModifier);   // The row stays until the view changes.
        app.press(Qt::Key_5, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, app.ids.oldThing), 5000);
        QVERIFY(rowVisible(app, app.ids.buyMilk));
    }
};

REGISTER_TEST(E2ETasksLifecycle)

#include "test_tasks_lifecycle.moc"
