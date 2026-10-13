// End-to-end scenarios for lists: creating, renaming, deleting, colouring,
// archiving, hiding, the per-list options in Edit List, and the counts on the
// sidebar rows. Each change is made through the sidebar and then reversed.

#include "listkit.h"
#include "../testregistry.h"
#include "alarms.h"

using namespace e2e;

namespace {

bool nearColour(const QColor &a, const QColor &b, int tolerance = 3) {
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance
           && qAbs(a.blue() - b.blue()) <= tolerance;
}

}  // namespace

class E2ELists : public QObject {
    Q_OBJECT

private slots:
    // "`[+]` button at the bottom of the Lists Panel." and "Rename...: edit the
    // name in place." (lists-and-list-groups.md) The new list opens with its
    // name ready to type over; Escape keeps the default name.
    void newListFromPlusButton() {
        App app(seedLists);
        QVERIFY(app.ok);
        QVERIFY(plusMenu("New List"));
        LIST_ITEM(editor, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 3000);
        QCOMPARE(editor->property("text").toString(), QString("New List"));
        app.type("Garage");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(listNamed(app, "Garage") != 0, 3000);
        const qint64 id = listNamed(app, "Garage");
        QCOMPARE(liveListsNamed(app, "New List"), 0);
        QCOMPARE(liveList(app, id).groupId, qint64(0));  // Opened from Today: no group to join.
        QCOMPARE(app.backend().sourceKey(), lk(id));
        LIST_ITEM(row, app.sidebarRow(lk(id)));
        QCOMPARE(shownName(row), QString("Garage"));
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Garage"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarListRename"), 3000);

        // Reverse: delete it again.
        QVERIFY(listMenu(app, id, "Delete List…"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(id)), 3000);
        QVERIFY(!findText("Garage"));

        // Escape while naming leaves the default name.
        QVERIFY(plusMenu("New List"));
        LIST_ITEM(second, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(second->property("activeFocus").toBool(), 3000);
        app.type("Abandoned");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarListRename"), 3000);
        const qint64 kept = listNamed(app, "New List");
        QVERIFY(kept != 0);
        QCOMPARE(liveListsNamed(app, "Abandoned"), 0);
        QVERIFY(app.sidebarRow(lk(kept)));
        QVERIFY(listMenu(app, kept, "Delete List…"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, kept), 3000);
    }

    // "File > New > New List" (Ctrl+Shift+N here); a list made while another
    // is open joins its group. Undo (Ctrl+Z) takes the new list back.
    void newListShortcutJoinsOpenGroup() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        QVERIFY(pane(app, lk(app.ids.home)));
        app.press(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        LIST_ITEM(editor, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 3000);
        app.type("Pantry");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(listNamed(app, "Pantry") != 0, 3000);
        const qint64 id = listNamed(app, "Pantry");
        QCOMPARE(liveList(app, id).groupId, w.personal);
        QCOMPARE(listsIn(app, w.personal).last(), id);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(id)) != nullptr, 3000);
        // It shows up under the Personal header, after Work.
        const QStringList order = sidebarOrder();
        QVERIFY(order.indexOf(lk(app.ids.work)) < order.indexOf(lk(id)));
        QVERIFY(order.indexOf("g:" + QString::number(w.personal)) < order.indexOf(lk(id)));

        app.press(Qt::Key_Z, Qt::ControlModifier);  // The name.
        QTRY_VERIFY2_WITH_TIMEOUT(listNamed(app, "Pantry") == 0, qPrintable("focus is on " + app.focusDescription() + " visible=" + QString::number(app.window->activeFocusItem()->isVisible()) + " active=" + app.window->activeFocusItem()->property("active").toString()), 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);  // The list.
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(id)), 3000);
    }

    // "Rename...: edit the name in place." Double-clicking a row does it too.
    void renameByDoubleClick() {
        App app(seedLists);
        QVERIFY(app.ok);
        auto rename = [&](const QString &name) {
            LIST_ITEM(row, app.sidebarRow(lk(app.ids.home)));
            doubleClick(row, pointIn(row, 0.4, 0.5));
            LIST_ITEM(editor, findItem("sidebarListRename"));
            QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 3000);
            app.type(name);
        };
        rename("Household");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).name, QString("Household"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(shownName(app.sidebarRow(lk(app.ids.home))), QString("Household"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Household"), 3000);

        // Escape abandons the edit.
        rename("Nope");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarListRename"), 3000);
        QCOMPARE(liveList(app, app.ids.home).name, QString("Household"));
        QCOMPARE(shownName(app.sidebarRow(lk(app.ids.home))), QString("Household"));

        // An empty name is not accepted.
        LIST_ITEM(row, app.sidebarRow(lk(app.ids.home)));
        doubleClick(row, pointIn(row, 0.4, 0.5));
        LIST_ITEM(editor, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 3000);
        app.press(Qt::Key_Backspace);
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarListRename"), 3000);
        QCOMPARE(liveList(app, app.ids.home).name, QString("Household"));

        // Reverse.
        rename("Home");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).name, QString("Home"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(shownName(app.sidebarRow(lk(app.ids.home))), QString("Home"), 3000);
    }

    // "You can edit any of these later from Task > Edit List with the list
    // selected, or by right-clicking the list." (lists-and-list-groups.md)
    void renameInEditList() {
        App app(seedLists);
        QVERIFY(app.ok);
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(name, findItem("editListName"));
        QCOMPARE(name->property("text").toString(), QString("Home"));
        replaceText(app, "Household");
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).name, QString("Household"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(shownName(app.sidebarRow(lk(app.ids.home))), QString("Household"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editListName"), 3000);

        // Cancel throws the edit away.
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(again, findItem("editListName"));
        QCOMPARE(again->property("text").toString(), QString("Household"));
        replaceText(app, "Discarded");
        QVERIFY(clickDialogButtonIn("Edit List", "Cancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editListName"), 3000);
        QCOMPARE(liveList(app, app.ids.home).name, QString("Household"));

        // Reverse, accepting with Return.
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(third, findItem("editListName"));
        QTRY_VERIFY_WITH_TIMEOUT(third->property("activeFocus").toBool(), 3000);
        replaceText(app, "Home");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).name, QString("Home"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editListName"), 3000);
    }

    // "Delete: removes the list and everything in it." and "Edit > Undo"
    // (lists-and-list-groups.md). The confirmation can be cancelled; its tasks
    // leave every view, and Ctrl+Z brings the list and its tasks back.
    void deleteListWithTasksAndUndo() {
        App app(seedLists);
        QVERIFY(app.ok);
        QVERIFY(pane(app, lk(app.ids.work)));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.report), 3000);

        QVERIFY(listMenu(app, app.ids.work, "Delete List…"));
        QVERIFY(clickDialogButtonIn("Delete “Work”?", "Cancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Delete “Work”?"), 3000);
        QVERIFY(listExists(app, app.ids.work));
        QVERIFY(!app.task(app.ids.report).deleted);

        QVERIFY(listMenu(app, app.ids.work, "Delete List…"));
        QVERIFY(clickDialogButtonIn("Delete “Work”?", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, app.ids.work), 3000);
        for (qint64 id : {app.ids.report, app.ids.studio, app.ids.callAlex, app.ids.oldThing})
            QVERIFY2(app.task(id).deleted, "task of the deleted list survived");
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(app.ids.work)), 3000);
        QVERIFY(!findText("Work"));
        // The open list went away, so the view moved on.
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("today"), 3000);
        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);
        QVERIFY(!shows(app, app.ids.report) && !shows(app, app.ids.studio));

        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(listExists(app, app.ids.work), 3000);
        QVERIFY(!app.task(app.ids.report).deleted && !app.task(app.ids.studio).deleted);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(app.ids.work)) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.report) && shows(app, app.ids.studio), 3000);
        QCOMPARE(liveList(app, app.ids.work).groupId, world().personal);
    }

    // "Delete all Tasks: empties the list but keeps the list itself, its color,
    // and its settings. the reference app asks for confirmation first." (lists-and-list-groups.md)
    void deleteAllTasksKeepsList() {
        App app(seedLists);
        QVERIFY(app.ok);
        QVERIFY(pane(app, lk(app.ids.home)));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);
        const int color = liveList(app, app.ids.home).color;

        QVERIFY(listMenu(app, app.ids.home, "Delete all Tasks…"));
        QVERIFY(clickDialogButtonIn("Delete all tasks in “Home”?", "Cancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Delete all tasks in “Home”?"), 3000);
        QVERIFY(!app.task(app.ids.buyMilk).deleted);

        QVERIFY(listMenu(app, app.ids.home, "Delete all Tasks…"));
        QVERIFY(clickDialogButtonIn("Delete all tasks in “Home”?", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.buyMilk).deleted, 3000);
        for (qint64 id : {app.ids.electricity, app.ids.plants, app.ids.shelf})
            QVERIFY(app.task(id).deleted);
        QVERIFY(listExists(app, app.ids.home));
        QCOMPARE(liveList(app, app.ids.home).color, color);
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.buyMilk), 3000);
        QVERIFY(app.sidebarRow(lk(app.ids.home)));
        QTRY_VERIFY_WITH_TIMEOUT(!badgeOf(app, lk(app.ids.home)).undoneShown, 3000);
        QCOMPARE(app.backend().sourceKey(), lk(app.ids.home));

        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(app.ids.buyMilk).deleted, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk) && shows(app, app.ids.shelf), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(app.ids.home)).undone, 4, 3000);
    }

    // "Color ▸: pick from a curated palette of swatches" (lists-and-list-groups.md).
    // The band beside the list takes the colour, and the menu ticks it.
    void colourFromPalette() {
        App app(seedLists);
        QVERIFY(app.ok);
        const int original = liveList(app, app.ids.home).color;
        const QColor before = bandColour(app, app.ids.home);
        QVERIFY(before.isValid());
        QCOMPARE(before, bandColour(app, app.ids.home));

        QVERIFY(listSubmenu(app, app.ids.home, "Color", "Red"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).color, 3, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(bandColour(app, app.ids.home) != before, 3000);
        const QColor red = bandColour(app, app.ids.home);
        // Work is Green (2); painting it Red too gives it the same band.
        QVERIFY(bandColour(app, app.ids.work) != red);
        QVERIFY(listSubmenu(app, app.ids.work, "Color", "Red"));
        QTRY_COMPARE_WITH_TIMEOUT(bandColour(app, app.ids.work), red, 3000);

        // The menu ticks the current colour.
        QQuickItem *homeRow = app.sidebarRow(lk(app.ids.home));
        QVERIFY(settle());
        click(homeRow, Qt::RightButton, pointIn(homeRow, 0.4, 0.5));
        LIST_ITEM(color, menuEntry("Color"));
        clickSteady(color);
        LIST_ITEM(redEntry, menuEntry("Red"));
        QVERIFY(redEntry->property("checked").toBool());
        QVERIFY(!menuEntry("Blue")->property("checked").toBool());
        closeMenus(app);

        // Reverse both.
        QVERIFY(listSubmenu(app, app.ids.home, "Color", "Blue"));
        QVERIFY(listSubmenu(app, app.ids.work, "Color", "Green"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).color, original, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.work).color, 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(bandColour(app, app.ids.home), before, 3000);
    }

    // "or choose Custom to open the macOS color picker for any shade you like."
    // (lists-and-list-groups.md) Any hex colour typed in the picker sticks.
    void colourCustomFromMenu() {
        App app(seedLists);
        QVERIFY(app.ok);
        const QColor before = bandColour(app, app.ids.home);
        QVERIFY(listSubmenu(app, app.ids.home, "Color", "Custom…"));
        LIST_ITEM(hex, findItem("colorHex"));
        click(hex);
        replaceText(app, "#FF8800");
        QVERIFY(clickDialogButtonIn("Custom Color", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(ListColor::isCustom(liveList(app, app.ids.home).color), 3000);
        QVERIFY(nearColour(QColor::fromRgb(ListColor::rgb(liveList(app, app.ids.home).color)), QColor(0xFF, 0x88, 0x00)));
        QTRY_VERIFY_WITH_TIMEOUT(nearColour(bandColour(app, app.ids.home), QColor(0xFF, 0x88, 0x00)), 3000);

        // The menu now ticks Custom…, and a palette colour takes it back.
        QQuickItem *row = app.sidebarRow(lk(app.ids.home));
        QVERIFY(settle());
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        clickSteady(menuEntry("Color"));
        LIST_ITEM(custom, menuEntry("Custom…"));
        QVERIFY(custom->property("checked").toBool());
        closeMenus(app);
        QVERIFY(listSubmenu(app, app.ids.home, "Color", "Blue"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).color, 0, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(bandColour(app, app.ids.home), before, 3000);
    }

    // The Edit List sheet: a swatch row ending in a custom colour.
    void colourInEditList() {
        App app(seedLists);
        QVERIFY(app.ok);
        const QColor before = bandColour(app, app.ids.home);
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(swatch, findItem("editListSwatch:7"));
        click(swatch);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).color, 7, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(bandColour(app, app.ids.home) != before, 3000);

        // A custom shade from the sheet.
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(custom, findItem("editListCustom"));
        click(custom);
        LIST_ITEM(hex, findItem("colorHex"));
        click(hex);
        replaceText(app, "#2299CC");
        QVERIFY(clickDialogButtonIn("Custom Color", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("colorHex"), 3000);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(ListColor::isCustom(liveList(app, app.ids.home).color), 3000);
        QVERIFY(nearColour(QColor::fromRgb(ListColor::rgb(liveList(app, app.ids.home).color)), QColor(0x22, 0x99, 0xCC)));
        QTRY_VERIFY_WITH_TIMEOUT(nearColour(bandColour(app, app.ids.home), QColor(0x22, 0x99, 0xCC)), 3000);

        // Reverse.
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(blue, findItem("editListSwatch:0"));
        click(blue);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.home).color, 0, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(bandColour(app, app.ids.home), before, 3000);
    }

    // "Exclude Completed keeps the list's finished tasks out of view (the item
    // reads Include Completed once it's on)." (lists-and-list-groups.md) The
    // Edit List sheet has the same switch with three states.
    void completedModeHidesAndShowsCompleted() {
        App app(seedLists);
        QVERIFY(app.ok);
        QVERIFY(pane(app, lk(app.ids.work)));
        // Finished a moment ago, so it is still on the list by default.
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.oldThing) && shows(app, app.ids.report), 3000);

        // The menu entry flips with the state.
        QVERIFY(listMenu(app, app.ids.work, "Exclude Completed"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.work).completedMode, 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.oldThing), 3000);
        QVERIFY(shows(app, app.ids.report));
        QQuickItem *row = app.sidebarRow(lk(app.ids.work));
        QVERIFY(settle());
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        LIST_ITEM(flipped, menuEntry("Include Completed"));
        QVERIFY(!menuEntry("Exclude Completed"));
        clickSteady(flipped);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.work).completedMode, 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.oldThing), 3000);
        QVERIFY(settle());
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        LIST_ITEM(back, menuEntry("Exclude Completed"));
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!menuEntry("Exclude Completed"), 3000);

        // Through the sheet: three states.
        QVERIFY(listMenu(app, app.ids.work, "Edit List…"));
        LIST_ITEM(combo, findItem("editListCompleted"));
        QCOMPARE(combo->property("currentIndex").toInt(), 1);  // Remembers "Always include".
        QVERIFY(pickCombo("editListCompleted", "Always exclude"));
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.work).completedMode, 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.oldThing), 3000);

        // Back to the default, which shows what finished within the window...
        QVERIFY(listMenu(app, app.ids.work, "Edit List…"));
        QVERIFY(pickCombo("editListCompleted", "Follow the default"));
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, app.ids.work).completedMode, 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.oldThing), 3000);

        // ...but not what finished long ago, unless the list always includes it.
        Task old = app.store().task(app.ids.oldThing);
        old.completedAt = QDateTime::currentDateTime().addDays(-3);
        app.store().begin("Age", false);
        app.store().putTask(old);
        app.store().commit();
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.oldThing), 3000);
        QVERIFY(listMenu(app, app.ids.work, "Edit List…"));
        QVERIFY(pickCombo("editListCompleted", "Always include"));
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.oldThing), 3000);
        QVERIFY(listMenu(app, app.ids.work, "Edit List…"));
        QVERIFY(pickCombo("editListCompleted", "Follow the default"));
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.oldThing), 3000);
        QCOMPARE(liveList(app, app.ids.work).completedMode, 0);
    }

    // "You can independently exclude a list from: All, Today, Done. ... The
    // list and its tasks are still visible when you click into the list
    // directly" (lists-and-list-groups.md). Both the menu and Edit List do it.
    void focusListExclusions() {
        App app(seedLists);
        QVERIFY(app.ok);
        auto openFocus = [&](Qt::Key key, const char *source) {
            app.press(key, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString(source), 3000);
        };

        // All, from the menu.
        openFocus(Qt::Key_1, "all");
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk) && shows(app, app.ids.report), 3000);
        QVERIFY(listMenu(app, app.ids.home, "Exclude from 'All'"));
        QTRY_VERIFY_WITH_TIMEOUT(!liveList(app, app.ids.home).showInAll, 3000);
        openFocus(Qt::Key_1, "all");
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.buyMilk) && !shows(app, app.ids.plants), 3000);
        QVERIFY(shows(app, app.ids.report));
        // The list row carries the visibility indicator, and opens its tasks.
        QVERIFY(childNamed(app.sidebarRow(lk(app.ids.home)), "sidebarExcluded") != nullptr);
        QVERIFY(pane(app, lk(app.ids.home)));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);
        // The menu now offers the way back.
        QQuickItem *row = app.sidebarRow(lk(app.ids.home));
        QVERIFY(settle());
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        LIST_ITEM(include, menuEntry("Include in 'All'"));
        QVERIFY(!menuEntry("Exclude from 'All'"));
        clickSteady(include);
        QTRY_VERIFY_WITH_TIMEOUT(liveList(app, app.ids.home).showInAll, 3000);
        openFocus(Qt::Key_1, "all");
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!childNamed(app.sidebarRow(lk(app.ids.home)), "sidebarExcluded"), 3000);

        // Today, from the sheet.
        openFocus(Qt::Key_2, "today");
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(todayBox, findItem("editListToday"));
        QVERIFY(todayBox->property("checked").toBool());
        click(todayBox);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!liveList(app, app.ids.home).showInToday, 3000);
        QVERIFY(liveList(app, app.ids.home).showInAll);
        openFocus(Qt::Key_2, "today");
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.buyMilk), 3000);
        openFocus(Qt::Key_1, "all");
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);  // Still in All.
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        LIST_ITEM(todayAgain, findItem("editListToday"));
        QVERIFY(!todayAgain->property("checked").toBool());
        click(todayAgain);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(liveList(app, app.ids.home).showInToday, 3000);
        openFocus(Qt::Key_2, "today");
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);

        // Done, from the sheet.
        openFocus(Qt::Key_5, "done");
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.oldThing), 3000);
        QVERIFY(listMenu(app, app.ids.work, "Edit List…"));
        LIST_ITEM(doneBox, findItem("editListDone"));
        click(doneBox);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!liveList(app, app.ids.work).showInDone, 3000);
        openFocus(Qt::Key_5, "done");
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, app.ids.oldThing), 3000);
        QVERIFY(listMenu(app, app.ids.work, "Include in 'Done'"));
        QTRY_VERIFY_WITH_TIMEOUT(liveList(app, app.ids.work).showInDone, 3000);
        openFocus(Qt::Key_5, "done");
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.oldThing), 3000);
    }

    // "Alerts for this list's tasks stay silent": the alarm scheduler has
    // nothing pending while the list is muted, and fires again once unmuted.
    void muteAlertsSilencesTheScheduler() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        const QDateTime now = QDateTime::currentDateTime();
        QVERIFY(AlarmTimes::hasPending(app.store(), now));  // "Sort cables" is due today with an alert.
        QVERIFY(!liveList(app, w.misc).muteAlerts);

        // Through the sheet.
        QVERIFY(listMenu(app, w.misc, "Edit List…"));
        LIST_ITEM(mute, findItem("editListMute"));
        QVERIFY(!mute->property("checked").toBool());
        click(mute);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(liveList(app, w.misc).muteAlerts, 3000);
        QVERIFY(!AlarmTimes::hasPending(app.store(), now));
        QVERIFY(listMenu(app, w.misc, "Edit List…"));
        LIST_ITEM(muteAgain, findItem("editListMute"));
        QVERIFY(muteAgain->property("checked").toBool());
        click(muteAgain);
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!liveList(app, w.misc).muteAlerts, 3000);
        QVERIFY(AlarmTimes::hasPending(app.store(), now));

        // Through the menu, which shows a tick while it is on.
        QVERIFY(listMenu(app, w.misc, "Mute Alerts"));
        QTRY_VERIFY_WITH_TIMEOUT(liveList(app, w.misc).muteAlerts, 3000);
        QVERIFY(!AlarmTimes::hasPending(app.store(), now));
        QQuickItem *row = app.sidebarRow(lk(w.misc));
        QVERIFY(settle());
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        LIST_ITEM(entry, menuEntry("Mute Alerts"));
        QVERIFY(entry->property("checked").toBool());
        clickSteady(entry);
        QTRY_VERIFY_WITH_TIMEOUT(!liveList(app, w.misc).muteAlerts, 3000);
        QVERIFY(AlarmTimes::hasPending(app.store(), now));

        // Muting another list leaves this list's alert alone.
        QVERIFY(listMenu(app, app.ids.home, "Mute Alerts"));
        QTRY_VERIFY_WITH_TIMEOUT(liveList(app, app.ids.home).muteAlerts, 3000);
        QVERIFY(AlarmTimes::hasPending(app.store(), now));
        QVERIFY(listMenu(app, app.ids.home, "Mute Alerts"));
        QTRY_VERIFY_WITH_TIMEOUT(!liveList(app, app.ids.home).muteAlerts, 3000);
    }

    // "Archive List: ... Archived lists and their tasks stay out of active
    // views" and "restored at any time" (lists-and-list-groups.md).
    void archiveAndRestore() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        QVERIFY(app.sidebarRow(lk(w.errands)));
        QVERIFY(!findItem("sidebarArchived"));
        QVERIFY(pane(app, "all"));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.stamps), 3000);

        QVERIFY(listMenu(app, w.errands, "Archive"));
        QTRY_VERIFY_WITH_TIMEOUT(liveList(app, w.errands).archived, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(w.errands)), 3000);
        // The list was open, so the view moved on; the archived tasks are not in All.
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("today"), 3000);
        QVERIFY(pane(app, "all"));
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, w.stamps) && shows(app, app.ids.buyMilk), 3000);
        LIST_ITEM(archived, findItem("sidebarArchived"));
        QVERIFY(findText("Archived"));

        click(archived);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Archived Lists") != nullptr, 3000);
        QVERIFY(findText("Errands"));
        LIST_ITEM(restore, findItem("archiveRestore:" + QString::number(w.errands)));
        click(restore);
        QTRY_VERIFY_WITH_TIMEOUT(!liveList(app, w.errands).archived, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(w.errands)) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarArchived"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Archived Lists"), 3000);
        QVERIFY(pane(app, "all"));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.stamps), 3000);
        QCOMPARE(liveList(app, w.errands).groupId, w.projects);
    }

    // "Hide lists" (Settings > Today & Lists): hidden lists vanish from the
    // sidebar, All and Today until revealed; Show Hidden Lists brings them
    // back for the session and the setting undoes it for good.
    void hideRevealAndUnhide() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        auto hidden = [&] { return app.store().setting("lists.hidden").toString().contains(liveList(app, w.misc).uid); };
        QVERIFY(pane(app, lk(w.misc)));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.cables), 3000);

        QVERIFY(listMenu(app, w.misc, "Hide List"));
        QTRY_VERIFY_WITH_TIMEOUT(hidden(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(w.misc)), 3000);
        QVERIFY(!findText("Misc"));
        // The open list was hidden, so the view moved to Today, which lacks its tasks.
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("today"), 3000);
        QVERIFY(!shows(app, w.cables));
        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.buyMilk), 3000);
        QVERIFY(!shows(app, w.cables));
        QVERIFY(!app.backend().jumpTargets("Misc").size());

        // Reveal for this session, then hide again.
        QVERIFY(plusMenu("Show Hidden Lists"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().hiddenRevealed(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(w.misc)) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.cables), 3000);
        QVERIFY(hidden());
        QVERIFY(plusMenu("Show Hidden Lists"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(w.misc)), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!shows(app, w.cables), 3000);

        // Unhide for good, in Settings.
        app.press(Qt::Key_Comma, Qt::ControlModifier);
        QQuickWindow *settings = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((settings = windowTitled("Settings")) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(settings->isExposed(), 3000);
        settings->requestActivate();
        LIST_ITEM(paneEntry, findText("Today & Lists"));
        click(paneEntry);
        // Ticking a list's box hides it; the box is rebuilt after every change, so
        // look it up afresh and wait for it to show the state we expect.
        auto tick = [&](bool wasHidden) {
            for (int attempt = 0; attempt < 3; ++attempt) {
                QQuickItem *box = nullptr;
                if (!waitUntil([&] {
                        box = findText("Misc", "QQuickAbstractButton");
                        return box && box->property("modelData").toMap().value("hidden").toBool() == wasHidden;
                    }))
                    return false;
                scrollIntoView(box);
                clickSteady(box);
                if (waitUntil([&] { return hidden() != wasHidden; }, 1500))
                    return true;
            }
            return false;
        };
        QVERIFY(tick(true));
        QTRY_VERIFY_WITH_TIMEOUT(!hidden(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(w.misc)) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.cables), 3000);
        // And hiding from Settings works the other way.
        QVERIFY(tick(false));
        QTRY_VERIFY_WITH_TIMEOUT(hidden(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(w.misc)), 3000);
        QVERIFY(tick(true));
        QTRY_VERIFY_WITH_TIMEOUT(!hidden(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(w.misc)) != nullptr, 3000);
    }

    // "Overdue count: to the left of the list name, in red... Undone count: to
    // the right of a list name" and "These counts are live" (lists-and-list-groups.md).
    void listCountsFollowTasks() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        const QString home = lk(app.ids.home);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, home).undone, 4, 3000);  // Four open tasks, one overdue.
        QCOMPARE(badgeOf(app, home).overdue, 1);
        QVERIFY(badgeOf(app, home).overdueShown && badgeOf(app, home).undoneShown);
        QCOMPARE(badgeOf(app, lk(w.errands)).overdue, 1);
        QCOMPARE(badgeOf(app, lk(w.errands)).undone, 2);
        QCOMPARE(badgeOf(app, lk(app.ids.work)).overdue, 0);
        QVERIFY(!badgeOf(app, lk(app.ids.work)).overdueShown);  // No red number without late tasks.
        const int allBefore = badgeOf(app, "all").undone;

        // Completing the overdue task: the red number goes, the total drops.
        QVERIFY(pane(app, home));
        LIST_ITEM(box, findItem(QString("taskCheck:%1").arg(app.ids.electricity)));
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.electricity).completed, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, home).undone, 3, 3000);
        QCOMPARE(badgeOf(app, home).overdue, 0);
        QVERIFY(!badgeOf(app, home).overdueShown);
        QCOMPARE(badgeOf(app, "all").undone, allBefore - 1);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(app.ids.electricity).completed, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, home).undone, 4, 3000);
        QCOMPARE(badgeOf(app, home).overdue, 1);
        QVERIFY(badgeOf(app, home).overdueShown);

        // A new task in the list counts at once; so does one due yesterday.
        app.press(Qt::Key_N, Qt::ControlModifier);
        LIST_ITEM(title, findItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(title->property("activeFocus").toBool(), 3000);
        app.type("Fix the tap");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Fix the tap").id != 0, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, home).undone, 5, 3000);
        QCOMPARE(badgeOf(app, "all").undone, allBefore + 1);
        // Undo takes the task back out (once for its title, once for the task).
        for (int i = 0; i < 3 && badgeOf(app, home).undone != 4; ++i) {
            app.press(Qt::Key_Z, Qt::ControlModifier);
            waitUntil([&] { return badgeOf(app, home).undone == 4; }, 800);
        }
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, home).undone, 4, 3000);
        QCOMPARE(app.taskTitled("Fix the tap").id, qint64(0));

        // Tasks made overdue by the data add to the red number.
        Task plants = app.store().task(app.ids.plants);
        plants.dueDate = QDate::currentDate().addDays(-1);
        app.store().begin("Late", false);
        app.store().putTask(plants);
        app.store().commit();
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, home).overdue, 2, 3000);
        QCOMPARE(badgeOf(app, home).undone, 4);
    }

    // "Smart Lists also live in the Lists Panel" under their own heading. They
    // are not part of the groups: they cannot be dragged into one, hidden or
    // archived, but can be renamed in place and opened like a list.
    void smartListsSection() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        const QString smart = lk(w.urgent);
        LIST_ITEM(header, groupHeader(-1));
        QCOMPARE(shownName(header), QString("Smart Lists"));
        const QStringList order = sidebarOrder();
        QVERIFY(order.indexOf("g:-1") > order.indexOf(lk(w.errands)));
        QCOMPARE(order.indexOf(smart), order.indexOf("g:-1") + 1);
        QCOMPARE(shownName(app.sidebarRow(smart)), QString("Urgent"));

        // Opening one shows what it finds.
        QVERIFY(pane(app, smart));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.plants) && !shows(app, app.ids.buyMilk), 3000);

        // Not draggable into a group or among the lists.
        const QStringList before = sidebarOrder();
        dragOnto(app.sidebarRow(smart), app.sidebarRow(lk(app.ids.home)));
        QTest::qWait(150);
        dragOnto(app.sidebarRow(smart), groupHeader(w.projects));
        QTest::qWait(150);
        dragOnto(app.sidebarRow(lk(app.ids.home)), app.sidebarRow(smart));
        QTest::qWait(150);
        QCOMPARE(liveList(app, w.urgent).groupId, qint64(0));
        QCOMPARE(liveList(app, app.ids.home).groupId, w.personal);
        QCOMPARE(sidebarOrder(), before);

        // The menu has no Hide or Archive, and hiding is not on offer in Settings.
        parkMouse(app);
        QQuickItem *row = app.sidebarRow(smart);
        QVERIFY(settle());
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        LIST_ITEM(del, menuEntry("Delete Smart List"));
        QVERIFY(!menuEntry("Hide List") && !menuEntry("Archive"));
        QVERIFY(menuEntry("Edit Search…") && menuEntry("Rename…"));
        app.press(Qt::Key_Escape);
        for (const QVariant &v : app.backend().hideableLists())
            QVERIFY(v.toMap().value("id").toLongLong() != w.urgent);

        // Rename in place, and back.
        for (const QString &name : {QString("Important"), QString("Urgent")}) {
            doubleClick(app.sidebarRow(smart), pointIn(app.sidebarRow(smart), 0.4, 0.5));
            LIST_ITEM(editor, findItem("sidebarListRename"));
            QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 3000);
            app.type(name);
            app.press(Qt::Key_Return);
            QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.urgent).name, name, 3000);
            QTRY_COMPARE_WITH_TIMEOUT(shownName(app.sidebarRow(smart)), name, 3000);
        }
    }
};

REGISTER_TEST(E2ELists)

#include "test_lists.moc"
