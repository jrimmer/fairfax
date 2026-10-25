// End-to-end scenarios for Smart Lists in navigation: where they sit in the
// sidebar, reaching them from the List Jump Bar (J) and Ctrl+Up/Down, the
// order among them, and that they survive a restart with the same rules,
// options and contents.

#include "settingskit.h"
#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

const SL Hi{"Rent overdue", "Fence in 6 days", "Starred urgent", "Launch project"};
const SL Starred{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote", "Plumber for garden", "Plumber for office"};

SL inLists(const SL &lists) {
    return specTitles([&](const Spec &s) { return shownInAll(s) && lists.contains(s.list); });
}

}  // namespace

class E2ESmartNav : public QObject {
    Q_OBJECT

private slots:
    // "Smart Lists also live in the Lists Panel" (lists-and-list-groups.md)
    // under their own heading, after the regular lists, in the order they
    // were made; clicking one opens it.
    void smartListsSitUnderTheirOwnHeading() {
        App app(seedSmart);
        QVERIFY(app.ok);
        QVERIFY(pane(app, "all"));
        const qint64 a = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(pane(app, "all"));
        const qint64 b = saveSearchAsSmart(app, "type: starred");
        QVERIFY(pane(app, "all"));
        const qint64 c = saveSearchAsSmart(app, "lists: Inbox");
        QVERIFY(a && b && c);
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"type: hiprio", "type: starred", "lists: Inbox"}), 5000);
        const QStringList order = sidebarOrder();
        // After every regular list, under the header.
        const int header = order.indexOf("g:-1");
        QVERIFY(header > order.indexOf(lk(listId("Archive"))));
        QCOMPARE(order.mid(header + 1), (SL{lk(a), lk(b), lk(c)}));
        // Each opens to its own rows.
        QVERIFY(pane(app, lk(b)));
        SMART_SHOWS(app, Starred);
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("type: starred"), 5000);
        QVERIFY(pane(app, lk(c)));
        SMART_SHOWS(app, (SL{"Starred plain", "Inbox note"}));
        QVERIFY(pane(app, lk(a)));
        SMART_SHOWS(app, Hi);
        QCOMPARE(app.backend().sourceKey(), lk(a));
    }

    // The jump bar matches Smart Lists by name along with other lists.
    void jumpBarReachesSmartLists() {
        App app(seedSmart);
        QVERIFY(app.ok);
        QVERIFY(pane(app, "all"));
        const qint64 hot = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(hot);
        QVERIFY(pane(app, "all"));
        const qint64 star = saveSearchAsSmart(app, "type: starred");
        QVERIFY(star);
        QVERIFY(pane(app, "today"));
        // J opens the bar; a part of the name filters it.
        QVERIFY(selectRowOf(app));
        app.press(Qt::Key_J);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") != nullptr, 5000);
        app.type("type: starred");
        QObject *bar = findObject("commandPopup");
        QTRY_COMPARE_WITH_TIMEOUT(bar->property("items").toList().size(), 1, 5000);
        const QVariantMap hit = bar->property("items").toList().first().toMap();
        QCOMPARE(hit.value("label").toString(), QString("type: starred"));
        QCOMPARE(hit.value("sub").toString(), QString("Smart List"));
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(star), 5000);
        SMART_SHOWS(app, Starred);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") == nullptr, 5000);
        // Jumping from one Smart List to the other works the same.
        app.press(Qt::Key_J);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") != nullptr, 5000);
        app.type("type: hiprio");
        QTRY_COMPARE_WITH_TIMEOUT(bar->property("items").toList().value(0).toMap().value("label").toString(), QString("type: hiprio"), 5000);
        QCOMPARE(bar->property("items").toList().size(), 1);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(hot), 5000);
        SMART_SHOWS(app, Hi);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") == nullptr, 5000);
        // Esc cancels the jump.
        app.press(Qt::Key_J);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") != nullptr, 5000);
        app.type("starred");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") == nullptr, 5000);
        QCOMPARE(app.backend().sourceKey(), lk(hot));
    }

    // Ctrl+Down and Ctrl+Up step through the Lists Panel in order, so they pass
    // from the last regular list into the Smart Lists and between them.
    void adjacentSourceShortcutsStepThroughSmartLists() {
        App app(seedSmart);
        QVERIFY(app.ok);
        QVERIFY(pane(app, "all"));
        const qint64 a = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(pane(app, "all"));
        const qint64 b = saveSearchAsSmart(app, "type: starred");
        QVERIFY(a && b);
        const QStringList order = sidebarOrder();
        const QString last = order.at(order.indexOf("g:-1") - 1);
        QVERIFY(pane(app, last));
        app.press(Qt::Key_Down, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(a), 5000);
        app.press(Qt::Key_Down, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(b), 5000);
        SMART_SHOWS(app, Starred);
        app.press(Qt::Key_Up, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(a), 5000);
        app.press(Qt::Key_Up, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), last, 5000);
    }

    // Reordering Smart Lists among themselves: a drag of one onto another is
    // how regular lists reorder in the Lists Panel, and a drop below the last
    // one moves it to the end.
    void smartListsCanBeReordered() {
        App app(seedSmart);
        QVERIFY(app.ok);
        QVERIFY(pane(app, "all"));
        const qint64 a = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(pane(app, "all"));
        const qint64 b = saveSearchAsSmart(app, "type: starred");
        QVERIFY(a && b);
        QCOMPARE(smartRowNames(app), (SL{"type: hiprio", "type: starred"}));
        dragOnto(app.sidebarRow(lk(b)), app.sidebarRow(lk(a)), 0.2);
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"type: starred", "type: hiprio"}), 5000);
        QVERIFY(app.store().list(b).order < app.store().list(a).order);
        QCOMPARE(app.store().list(b).kind, int(ListKind::Smart));
        // Back again, through the drop strip under the last list (it answers
        // about 13 px below where the pointer is drawn, so aim lower).
        QQuickItem *tail = findItem("sidebarTailDrop");
        QVERIFY(tail);
        dragOnto(app.sidebarRow(lk(b)), tail, 0, 20);
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"type: hiprio", "type: starred"}), 5000);
        QVERIFY(app.store().list(a).order < app.store().list(b).order);
    }

    // Smart Lists survive a restart: the same lists in the same order, their
    // rules, ranges and switches, and the same rows and counts.
    void smartListsSurviveARestart() {
        Session s;
        s.seed = seedSmart;
        QVERIFY(s.start());
        App &app = *s.app;
        QVERIFY(pane(app, "all"));
        // One saved from the search bar, then given Include Completed.
        const qint64 work = saveSearchAsSmart(app, "lists: Work");
        QVERIFY(work);
        QVERIFY(listMenu(app, work, "Edit Search…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QVERIFY(pressBuilder("completedInclude"));
        QVERIFY(pressBuilder("searchScheduledInclude"));
        QVERIFY(pressBuilder("updateButton"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().list(work).completedMode == 1 && app.store().list(work).includeScheduled, 5000);
        // One built from the sheet with a date range.
        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("rangeNext"));
        QVERIFY(fillBuilder(app, "rangeN", "3"));
        QVERIFY(addRuleRow("Priority", "is", "High") == "0");
        QVERIFY(addRuleRow("Priority", "is", "Medium") == "1");
        QVERIFY(setMatch("", true));
        const qint64 soon = saveFromBuilder(app, "Soon and important");
        QVERIFY(soon);
        // And a plain one, renamed.
        QVERIFY(pane(app, "all"));
        const qint64 star = saveSearchAsSmart(app, "type: starred");
        QVERIFY(star);
        doubleClick(app.sidebarRow(lk(star)), pointIn(app.sidebarRow(lk(star)), 0.4, 0.5));
        LIST_ITEM(editor, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 5000);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("Stars");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(star).name, QString("Stars"), 5000);

        QMap<qint64, QStringList> before;
        QMap<qint64, int> counts;
        for (qint64 id : {work, soon, star}) {
            QVERIFY(pane(app, lk(id)));
            QTRY_VERIFY_WITH_TIMEOUT(!paneTitles(app).isEmpty(), 5000);
            before[id] = paneTitles(app);
            counts[id] = badgeOf(app, lk(id)).undone;
            QVERIFY(counts[id] > 0);
        }
        QVERIFY(before[work].contains("Done just now"));
        const QStringList names = smartRowNames(app);
        QCOMPARE(names, (SL{"lists: Work", "Soon and important", "Stars"}));

        // Quit and relaunch on the same database.
        QVERIFY(s.restart());
        App &again = *s.app;
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(again), names, 4000);
        QCOMPARE(again.store().list(work).smartQuery, QString("lists: Work"));
        QCOMPARE(again.store().list(work).completedMode, 1);
        QVERIFY(again.store().list(work).includeScheduled);
        QCOMPARE(again.store().list(soon).smartRange, QString("next:3:d"));
        QCOMPARE(again.store().list(soon).smartQuery, QString("type: hiprio OR medprio"));
        QCOMPARE(again.store().list(star).name, QString("Stars"));
        for (qint64 id : {work, soon, star}) {
            QVERIFY(pane(again, lk(id)));
            QTRY_COMPARE_WITH_TIMEOUT(paneTitles(again), before[id], 4000);
            QTRY_COMPARE_WITH_TIMEOUT(badgeOf(again, lk(id)).undone, counts[id], 5000);
        }
        // The range is still shown above the rows.
        QVERIFY(pane(again, lk(soon)));
        QVERIFY(findText("In the next 3 days"));
    }

private:
    // Gives the task list keyboard focus, as a click on a row does.
    static bool selectRowOf(App &app) {
        for (const QVariant &v : paneRows(app)) {
            QQuickItem *row = app.row(v.toMap().value("taskId").toLongLong());
            if (row) {
                click(row, Qt::LeftButton, pointIn(row, 0.5, 0.5));
                return true;
            }
        }
        // Nothing due: the pane is empty, but J still works from the window.
        return true;
    }
};

REGISTER_TEST(E2ESmartNav)

#include "test_smart_nav.moc"
