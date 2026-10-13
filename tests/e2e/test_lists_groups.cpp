// End-to-end scenarios for list groups: creating, renaming, colouring,
// deleting, collapsing (and keeping that across a restart), and ordering
// groups and lists by dragging.

#include "listkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

QString gk(qint64 groupId) { return "g:" + QString::number(groupId); }

bool before(const QStringList &order, const QString &a, const QString &b) {
    return order.indexOf(a) >= 0 && order.indexOf(b) >= 0 && order.indexOf(a) < order.indexOf(b);
}

// Renames through the group's inline editor, which must already be open.
void typeGroupName(App &app, const QString &name) {
    QQuickItem *editor = nullptr;
    QVERIFY(waitUntil([&] { return (editor = findItem("sidebarGroupRename")) != nullptr; }));
    QVERIFY(waitUntil([&] { return editor->property("activeFocus").toBool(); }));
    app.type(name);
}

}  // namespace

class E2EListGroups : public QObject {
    Q_OBJECT

private slots:
    // "Create one with File > New > New List Group" (lists-and-list-groups.md).
    // The new group appears in the sidebar ready to be named; Escape keeps
    // "New Group"; deleting it again leaves nothing behind.
    void newGroupFromPlusButton() {
        App app(seedLists);
        QVERIFY(app.ok);
        const int groups = groupOrder(app).size();
        QVERIFY(plusMenu("New List Group"));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroupsNamed(app, "New Group") == 1, 3000);
        const qint64 id = groupNamed(app, "New Group");
        QCOMPARE(groupOrder(app).size(), groups + 1);
        QCOMPARE(groupOrder(app).last(), id);
        // An empty group still shows, so there is something to drop lists on.
        QTRY_VERIFY_WITH_TIMEOUT(groupHeader(id) != nullptr, 3000);
        typeGroupName(app, "Hobbies");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(liveGroup(app, id).name, QString("Hobbies"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(shownName(groupHeader(id)), QString("Hobbies"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarGroupRename"), 3000);
        QCOMPARE(liveGroupsNamed(app, "New Group"), 0);

        // Reverse: delete it.
        QVERIFY(groupMenu(id, "Delete Group"));
        QVERIFY(clickDialogButtonIn("Delete group “Hobbies”?", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, id).id == 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!groupHeader(id), 3000);
        QCOMPARE(groupOrder(app).size(), groups);

        // Escape keeps the default name.
        QVERIFY(plusMenu("New List Group"));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroupsNamed(app, "New Group") == 1, 3000);
        const qint64 kept = groupNamed(app, "New Group");
        typeGroupName(app, "Discarded");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarGroupRename"), 3000);
        QCOMPARE(liveGroup(app, kept).name, QString("New Group"));
        QCOMPARE(liveGroupsNamed(app, "Discarded"), 0);
    }

    // "Right-clicking a List Group gives you a shorter menu: Rename..., Color
    // grouped lists..., and Delete." (lists-and-list-groups.md) Double-clicking
    // the header renames too.
    void renameGroup() {
        App app(seedLists);
        QVERIFY(app.ok);
        const qint64 id = world().personal;
        LIST_ITEM(header, groupHeader(id));
        doubleClick(header, pointIn(header, 0.3, 0.6));
        LIST_ITEM(editor, findItem("sidebarGroupRename"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 3000);
        QCOMPARE(editor->property("text").toString(), QString("Personal"));
        app.type("Family");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(liveGroup(app, id).name, QString("Family"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(shownName(groupHeader(id)), QString("Family"), 3000);
        QCOMPARE(liveList(app, app.ids.home).groupId, id);  // The lists stay in.

        // From the menu; Escape abandons it.
        QVERIFY(groupMenu(id, "Rename…"));
        typeGroupName(app, "Nope");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarGroupRename"), 3000);
        QCOMPARE(liveGroup(app, id).name, QString("Family"));
        QCOMPARE(shownName(groupHeader(id)), QString("Family"));

        // Reverse.
        QVERIFY(groupMenu(id, "Rename…"));
        typeGroupName(app, "Personal");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(liveGroup(app, id).name, QString("Personal"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(shownName(groupHeader(id)), QString("Personal"), 3000);
    }

    // "New List in Group" is on the group's menu as well.
    void newListInGroupFromMenu() {
        App app(seedLists);
        QVERIFY(app.ok);
        const qint64 id = world().projects;
        QVERIFY(groupMenu(id, "New List in Group"));
        LIST_ITEM(editor, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 3000);
        app.type("Appointments");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(listNamed(app, "Appointments") != 0, 3000);
        const qint64 list = listNamed(app, "Appointments");
        QCOMPARE(liveList(app, list).groupId, id);
        QCOMPARE(listsIn(app, id), (QList<qint64>{world().errands, list}));
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), lk(world().errands), lk(list)), 3000);
        // Reverse.
        QVERIFY(listMenu(app, list, "Delete List…"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, list), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(list)), 3000);
    }

    // "Delete" on a group: its lists are kept and move out of the group
    // (the confirmation says so); Ctrl+Z puts the group and its lists back.
    void deleteGroupKeepsItsLists() {
        App app(seedLists);
        QVERIFY(app.ok);
        const qint64 id = world().personal;
        const QStringList order = sidebarOrder();
        QVERIFY(order.contains(gk(id)));

        QVERIFY(groupMenu(id, "Delete Group"));
        QTRY_VERIFY_WITH_TIMEOUT(findText("Its lists are kept and move out of the group.") != nullptr, 3000);
        QVERIFY(clickDialogButtonIn("Delete group “Personal”?", "Cancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!findText("Delete group “Personal”?"), 3000);
        QVERIFY(liveGroup(app, id).id != 0);
        QCOMPARE(liveList(app, app.ids.home).groupId, id);

        QVERIFY(groupMenu(id, "Delete Group"));
        QVERIFY(clickDialogButtonIn("Delete group “Personal”?", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, id).id == 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!groupHeader(id), 3000);
        QVERIFY(!findText("Personal"));
        QVERIFY(listExists(app, app.ids.home) && listExists(app, app.ids.work));
        QCOMPARE(liveList(app, app.ids.home).groupId, qint64(0));
        QCOMPARE(liveList(app, app.ids.work).groupId, qint64(0));
        QVERIFY(!app.task(app.ids.buyMilk).deleted && !app.task(app.ids.report).deleted);
        QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(app.ids.home)) && app.sidebarRow(lk(app.ids.work)), 3000);
        // They sit with the other top-level lists now: above Projects.
        QVERIFY(before(sidebarOrder(), lk(app.ids.work), gk(world().projects)));

        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, id).id != 0, 3000);
        QCOMPARE(liveList(app, app.ids.home).groupId, id);
        QCOMPARE(liveList(app, app.ids.work).groupId, id);
        QTRY_VERIFY_WITH_TIMEOUT(groupHeader(id) != nullptr, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(id), lk(app.ids.home)), 3000);
        QCOMPARE(shownName(groupHeader(id)), QString("Personal"));
    }

    // "Color grouped lists... (recolors every list inside the group in one
    // step)" and "List groups can be color-coded too." (lists-and-list-groups.md)
    void colourGroup() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        const qint64 id = w.personal;
        const QColor home = bandColour(app, app.ids.home), work = bandColour(app, app.ids.work);
        const QColor errands = bandColour(app, w.errands);
        QVERIFY(home != work);
        QCOMPARE(liveGroup(app, id).color, -1);
        QVERIFY(!childNamed(groupHeader(id), "sidebarGroupDot"));  // No dot until it has a colour.

        QVERIFY(groupSubmenu(id, "Color Grouped Lists", "Red"));
        QTRY_COMPARE_WITH_TIMEOUT(liveGroup(app, id).color, 3, 3000);
        QCOMPARE(liveList(app, app.ids.home).color, 3);
        QCOMPARE(liveList(app, app.ids.work).color, 3);
        QTRY_VERIFY_WITH_TIMEOUT(bandColour(app, app.ids.home) == bandColour(app, app.ids.work), 3000);
        QVERIFY(bandColour(app, app.ids.home) != home);
        QCOMPARE(bandColour(app, w.errands), errands);  // Another group is untouched.
        LIST_ITEM(dot, childNamed(groupHeader(id), "sidebarGroupDot"));
        QCOMPARE(dot->property("color").value<QColor>(), bandColour(app, app.ids.home));

        // Reverse with Ctrl+Z: the group loses its colour, the lists theirs.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(liveGroup(app, id).color, -1, 3000);
        QCOMPARE(liveList(app, app.ids.home).color, 0);
        QCOMPARE(liveList(app, app.ids.work).color, 2);
        QTRY_COMPARE_WITH_TIMEOUT(bandColour(app, app.ids.home), home, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(bandColour(app, app.ids.work), work, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!childNamed(groupHeader(id), "sidebarGroupDot"), 3000);

        // A custom shade colours the group and its lists the same way.
        QVERIFY(groupSubmenu(id, "Color Grouped Lists", "Custom…"));
        LIST_ITEM(hex, findItem("colorHex"));
        click(hex);
        replaceText(app, "#33AA55");
        QVERIFY(clickDialogButtonIn("Custom Color", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(ListColor::isCustom(liveGroup(app, id).color), 3000);
        QCOMPARE(liveList(app, app.ids.home).color, liveGroup(app, id).color);
        QCOMPARE(liveList(app, app.ids.work).color, liveGroup(app, id).color);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(bandColour(app, app.ids.work).green() - 0xAA) <= 3, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(liveGroup(app, id).color, -1, 3000);
        QCOMPARE(liveList(app, app.ids.work).color, 2);
    }

    // "collapse the group when you don't need to see what's inside" and
    // "Expand all List Groups / Collapse all List Groups: see the View menu."
    // (lists-and-list-groups.md)
    void collapseAndExpandGroups() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        auto shown = [&](qint64 list) { return app.sidebarRow(lk(list)) != nullptr; };
        QVERIFY(shown(app.ids.home) && shown(w.errands));

        // Clicking the header folds and unfolds it.
        click(groupHeader(w.personal), Qt::LeftButton, pointIn(groupHeader(w.personal), 0.3, 0.6));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, w.personal).collapsed, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!shown(app.ids.home) && !shown(app.ids.work), 3000);
        QVERIFY(shown(w.errands) && groupHeader(w.personal));
        click(groupHeader(w.personal), Qt::LeftButton, pointIn(groupHeader(w.personal), 0.3, 0.6));
        QTRY_VERIFY_WITH_TIMEOUT(!liveGroup(app, w.personal).collapsed, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shown(app.ids.home) && shown(app.ids.work), 3000);

        // The group's menu says which way it goes.
        QVERIFY(groupMenu(w.projects, "Collapse"));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, w.projects).collapsed && !shown(w.errands), 3000);
        QQuickItem *header = groupHeader(w.projects);
        click(header, Qt::RightButton, pointIn(header, 0.4, 0.5));
        LIST_ITEM(expand, menuEntry("Expand"));
        QVERIFY(!menuEntry("Collapse"));
        click(expand);
        QTRY_VERIFY_WITH_TIMEOUT(!liveGroup(app, w.projects).collapsed && shown(w.errands), 3000);

        // All at once: the + menu, then the shortcuts.
        QVERIFY(plusMenu("Collapse All List Groups"));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, w.personal).collapsed && liveGroup(app, w.projects).collapsed, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!shown(app.ids.home) && !shown(w.errands), 3000);
        QVERIFY(plusMenu("Expand All List Groups"));
        QTRY_VERIFY_WITH_TIMEOUT(!liveGroup(app, w.personal).collapsed && !liveGroup(app, w.projects).collapsed, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shown(app.ids.home) && shown(w.errands), 3000);
        app.press(Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, w.personal).collapsed && liveGroup(app, w.projects).collapsed, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!shown(app.ids.home) && !shown(w.errands), 3000);
        app.press(Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!liveGroup(app, w.personal).collapsed && !liveGroup(app, w.projects).collapsed, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shown(app.ids.home) && shown(w.errands), 3000);
    }

    // Collapsed groups stay collapsed after a restart, and expanding sticks too.
    void collapsedStateSurvivesRestart() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString db = dir.filePath("restart.db");
        const auto &w = world();
        qint64 home = 0, work = 0;
        {
            App app(seedLists, db);
            QVERIFY(app.ok);
            home = app.ids.home;
            work = app.ids.work;
            click(groupHeader(w.personal), Qt::LeftButton, pointIn(groupHeader(w.personal), 0.3, 0.6));
            QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, w.personal).collapsed, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(home)), 3000);
        }
        const qint64 personal = w.personal, projects = w.projects, errands = w.errands;
        {
            App app(nullptr, db);
            QVERIFY(app.ok);
            QVERIFY(liveGroup(app, personal).collapsed);
            QTRY_VERIFY_WITH_TIMEOUT(groupHeader(personal) != nullptr, 3000);
            QVERIFY(!app.sidebarRow(lk(home)) && !app.sidebarRow(lk(work)));
            QVERIFY(app.sidebarRow(lk(errands)));  // The other group was left open.
            QCOMPARE(shownName(groupHeader(personal)), QString("Personal"));
            // Expand it and fold the other one instead.
            click(groupHeader(personal), Qt::LeftButton, pointIn(groupHeader(personal), 0.3, 0.6));
            QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(home)) != nullptr, 3000);
            click(groupHeader(projects), Qt::LeftButton, pointIn(groupHeader(projects), 0.3, 0.6));
            QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(errands)), 3000);
        }
        {
            App app(nullptr, db);
            QVERIFY(app.ok);
            QVERIFY(!liveGroup(app, personal).collapsed && liveGroup(app, projects).collapsed);
            QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(home)) != nullptr, 3000);
            QVERIFY(!app.sidebarRow(lk(errands)));
        }
    }

    // "Drag a list up or down to reorder." (lists-and-list-groups.md) Dropping
    // a list on another puts it before that one; the order survives a restart.
    void reorderListsByDragging() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString db = dir.filePath("order.db");
        const auto &w = world();
        qint64 home = 0, work = 0;
        {
            App app(seedLists, db);
            QVERIFY(app.ok);
            home = app.ids.home;
            work = app.ids.work;
            QCOMPARE(listsIn(app, w.personal), (QList<qint64>{home, work}));
            LIST_ITEM(workRow, app.sidebarRow(lk(work)));
            dragOnto(workRow, app.sidebarRow(lk(home)), 0.3);
            QTRY_COMPARE_WITH_TIMEOUT(listsIn(app, w.personal), (QList<qint64>{work, home}), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), lk(work), lk(home)), 3000);
            QCOMPARE(liveList(app, work).groupId, w.personal);
            // Dropping on itself changes nothing.
            dragOnto(app.sidebarRow(lk(work)), app.sidebarRow(lk(work)));
            QTest::qWait(100);
            QCOMPARE(listsIn(app, w.personal), (QList<qint64>{work, home}));
        }
        {
            App app(nullptr, db);
            QVERIFY(app.ok);
            QCOMPARE(listsIn(app, w.personal), (QList<qint64>{work, home}));
            QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(lk(home)) && app.sidebarRow(lk(work)), 3000);
            QVERIFY(before(sidebarOrder(), lk(work), lk(home)));
            // Reverse: drag Home back above Work.
            dragOnto(app.sidebarRow(lk(home)), app.sidebarRow(lk(work)), 0.3);
            QTRY_COMPARE_WITH_TIMEOUT(listsIn(app, w.personal), (QList<qint64>{home, work}), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), lk(home), lk(work)), 3000);
        }
    }

    // "Drag a list onto a List Group to move it inside." (lists-and-list-groups.md)
    // Into a group by its header, between groups by dropping on one of its
    // lists, and out to the top level; each undone by dragging back.
    void moveListsBetweenGroupsByDragging() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();

        // Misc (top level) onto the Projects header: last in that group.
        QVERIFY(listsIn(app, 0).contains(w.misc));
        dragOnto(app.sidebarRow(lk(w.misc)), groupHeader(w.projects));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.misc).groupId, w.projects, 3000);
        QCOMPARE(listsIn(app, w.projects), (QList<qint64>{w.errands, w.misc}));
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(w.projects), lk(w.misc)), 3000);
        QVERIFY(before(sidebarOrder(), lk(w.errands), lk(w.misc)));

        // Errands onto Home: into Personal, ahead of Home.
        dragOnto(app.sidebarRow(lk(w.errands)), app.sidebarRow(lk(app.ids.home)), 0.3);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.errands).groupId, w.personal, 3000);
        QCOMPARE(listsIn(app, w.personal), (QList<qint64>{w.errands, app.ids.home, app.ids.work}));
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(w.personal), lk(w.errands)), 3000);
        QVERIFY(before(sidebarOrder(), lk(w.errands), lk(app.ids.home)));
        QCOMPARE(listsIn(app, w.projects), (QList<qint64>{w.misc}));

        // Out to the top level: dropping below the last row, in the strip the
        // sidebar opens for it while dragging, ungroups the list. (The strip
        // answers about 13 px below where the pointer is drawn, so aim lower.)
        LIST_ITEM(tail, findItem("sidebarTailDrop"));
        dragOnto(app.sidebarRow(lk(w.errands)), tail, 0, 20);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.errands).groupId, qint64(0), 3000);
        QVERIFY(listsIn(app, 0).contains(w.errands));
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), lk(w.errands), gk(w.personal)), 3000);

        // Reverse everything: Errands back into Projects, Misc back to the top.
        dragOnto(app.sidebarRow(lk(w.errands)), groupHeader(w.projects));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.errands).groupId, w.projects, 3000);
        dragOnto(app.sidebarRow(lk(w.misc)), tail, 0, 20);
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.misc).groupId, qint64(0), 3000);
        QCOMPARE(listsIn(app, w.projects), (QList<qint64>{w.errands}));
        QCOMPARE(listsIn(app, w.personal), (QList<qint64>{app.ids.home, app.ids.work}));
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), lk(w.misc), gk(w.personal)), 3000);
    }

    // The same moves from the menu: Edit List has a Group drop-down.
    void moveListsBetweenGroupsInEditList() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        QVERIFY(listMenu(app, w.misc, "Edit List…"));
        LIST_ITEM(group, findItem("editListGroup"));
        QCOMPARE(group->property("currentIndex").toInt(), 0);  // "None".
        QVERIFY(pickCombo("editListGroup", "Personal"));
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.misc).groupId, w.personal, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(w.personal), lk(w.misc)), 3000);
        QVERIFY(before(sidebarOrder(), lk(w.misc), gk(w.projects)));

        // Between groups.
        QVERIFY(listMenu(app, w.misc, "Edit List…"));
        QVERIFY(pickCombo("editListGroup", "Projects"));
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.misc).groupId, w.projects, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(w.projects), lk(w.misc)), 3000);

        // And out again.
        QVERIFY(listMenu(app, w.misc, "Edit List…"));
        QVERIFY(pickCombo("editListGroup", "None"));
        QVERIFY(clickDialogButtonIn("Edit List", "OK"));
        QTRY_COMPARE_WITH_TIMEOUT(liveList(app, w.misc).groupId, qint64(0), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), lk(w.misc), gk(w.personal)), 3000);
        QCOMPARE(listsIn(app, w.personal), (QList<qint64>{app.ids.home, app.ids.work}));
        QCOMPARE(listsIn(app, w.projects), (QList<qint64>{w.errands}));
    }

    // Groups are drag-and-droppable too: a group dropped on another goes
    // ahead of it, and the order survives a restart.
    void reorderGroupsByDragging() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString db = dir.filePath("groups.db");
        const auto &w = world();
        {
            App app(seedLists, db);
            QVERIFY(app.ok);
            QCOMPARE(groupOrder(app), (QList<qint64>{w.personal, w.projects}));
            QVERIFY(before(sidebarOrder(), gk(w.personal), gk(w.projects)));
            dragOnto(groupHeader(w.projects), groupHeader(w.personal));
            QTRY_COMPARE_WITH_TIMEOUT(groupOrder(app), (QList<qint64>{w.projects, w.personal}), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(w.projects), gk(w.personal)), 3000);
            // The lists went with their groups.
            QVERIFY(before(sidebarOrder(), lk(w.errands), gk(w.personal)));
            QVERIFY(before(sidebarOrder(), gk(w.personal), lk(app.ids.home)));
            QCOMPARE(liveList(app, w.errands).groupId, w.projects);
            QCOMPARE(liveList(app, app.ids.home).groupId, w.personal);
        }
        {
            App app(nullptr, db);
            QVERIFY(app.ok);
            QCOMPARE(groupOrder(app), (QList<qint64>{w.projects, w.personal}));
            QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(w.projects), gk(w.personal)), 3000);
            // Reverse.
            dragOnto(groupHeader(w.personal), groupHeader(w.projects));
            QTRY_COMPARE_WITH_TIMEOUT(groupOrder(app), (QList<qint64>{w.personal, w.projects}), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(before(sidebarOrder(), gk(w.personal), gk(w.projects)), 3000);
        }
    }
};

REGISTER_TEST(E2EListGroups)

#include "test_lists_groups.moc"
