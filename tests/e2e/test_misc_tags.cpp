// End-to-end tests: the utility panel. The Tags tab (list and counts, filtering,
// rename, delete, tag groups, pause and resume, the tag search bar, sorting,
// dragging tags and tasks) and the Locations tab (add, edit, delete, filter,
// pause, drop a task to set it).

#include "misckit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::mk;

namespace {

QString tagName(qint64 id) { return "tagRow:" + QString::number(id); }

qint64 tagIdNamed(App &app, const QString &name) {
    for (const Tag &t : app.store().tags())
        if (!t.deleted && t.name == name)
            return t.id;
    return 0;
}

qint64 groupIdNamed(App &app, const QString &name) {
    for (const TagGroup &g : app.store().tagGroups())
        if (!g.deleted && g.name == name)
            return g.id;
    return 0;
}

qint64 placeIdNamed(App &app, const QString &name) {
    for (const Location &l : app.store().locations())
        if (!l.deleted && l.name == name)
            return l.id;
    return 0;
}

// What the Tags tab lists, top to bottom: group headings as "[Name]", tags by name.
QStringList panelRows(App &app) {
    QList<QPair<qreal, QString>> found;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            const QString n = i->objectName();
            if (!(n.startsWith("tagRow:") || n.startsWith("tagGroup:")) || i->height() <= 0)
                return false;
            const qreal y = i->mapToScene(QPointF(0, 0)).y();
            if (n.startsWith("tagRow:")) {
                found.append({y, app.store().tag(n.mid(7).toLongLong()).name});
            } else {
                const qint64 gid = n.mid(9).toLongLong();
                found.append({y, "[" + (gid ? app.store().tagGroups().value(gid).name : QString("Tags")) + "]"});
            }
            return false;
        });
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList out;
    for (const auto &f : found)
        out << f.second;
    return out;
}

// The number badge on a tag row; 0 when the row shows none.
int countOf(const QString &rowName) {
    QQuickItem *row = findItem(rowName);
    QQuickItem *c = row ? findIn(row, [](QQuickItem *i) { return i->objectName() == "tagCount"; }) : nullptr;
    return c ? c->property("text").toInt() : 0;
}

int placeCountOf(qint64 id) {
    QQuickItem *row = findItem("placeRow:" + QString::number(id));
    QQuickItem *c = row ? findIn(row, [](QQuickItem *i) { return i->objectName() == "placeCount"; }) : nullptr;
    return c ? c->property("text").toInt() : 0;
}

// Right-clicks a row of the panel and picks a menu entry. The panel rebuilds its
// rows after each change, and a menu opened on a row that is then replaced goes
// with it, so the click is tried again.
bool rowRightClick(const QString &name, const QString &entry) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        QQuickItem *row = nullptr;
        if (!tk::waitFor([&] { return (row = findItem(name)) != nullptr; }) || !tk::settled(name))
            return false;
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        if (waitUntil([&] { return menuEntry(entry) != nullptr; }, 800))
            return clickMenuEntry(entry);
        QTest::keyClick(row->window(), Qt::Key_Escape);   // Whatever opened, if anything.
        waitUntil([] { return popupsClosed(); }, 500);
    }
    return false;
}

bool rowRightClickSub(const QString &name, const QString &submenu, const QString &entry) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        QQuickItem *row = nullptr;
        if (!tk::waitFor([&] { return (row = findItem(name)) != nullptr; }) || !tk::settled(name))
            return false;
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        if (waitUntil([&] { return menuEntry(submenu) != nullptr; }, 800))
            return tk::clickSub(submenu, entry);
        qWarning().noquote() << "no" << submenu << "after a right-click on" << name << "; menus show" << menuEntries().join(" | ");
        QTest::keyClick(row->window(), Qt::Key_Escape);
        waitUntil([] { return popupsClosed(); }, 500);
    }
    return false;
}

// Types into the name dialog (New / Rename for tags, groups and places) and accepts it.
bool nameDialog(const QString &field, const QString &text) {
    QQuickItem *f = nullptr;
    if (!tk::waitFor([&] { return (f = findItem(field)) != nullptr && f->property("activeFocus").toBool(); }))
        return false;
    QWindow *w = f->window();
    QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
    typeInto(w, text);
    return true;
}

bool dialogGone(const QString &field) { return tk::waitFor([&] { return findItem(field) == nullptr; }); }

}  // namespace

class E2EMiscTags : public QObject {
    Q_OBJECT

    // The task pane shows tag names only when it is wider than 760 pixels; with both
    // panels open the test window leaves it narrower, so the window is made wider.
    static bool openSorting(App &app) {
        app.window->resize(1500, 760);
        return app.openSource(lk(mw().sorting));
    }
    static QStringList chips(App &app, qint64 task) {
        QStringList out;
        for (const QString &n : tk::tagNames(app, app.task(task)))
            if (findItem(QString("rowTag:%1:%2").arg(task).arg(n))) out << n;
        return out;
    }
    // Clicks a tag row in the panel, the way a user filters.
    static bool clickTag(App &app, const QString &name) {
        return tk::clickNamed(tagName(tagIdNamed(app, name)));
    }

private slots:
    // "Open the right utility panel and switch to Tags to see your tags, each
    // with a count of tasks" (tags.md): alphabetical, grouped under TAGS.
    void panelListsTagsWithCounts() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "mango", "zebra"}), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().mango)), 2, 3000);
        QCOMPARE(countOf(tagName(mw().apple)), 1);
        QCOMPARE(countOf(tagName(mw().zebra)), 1);
        QCOMPARE(countOf(tagName(mw().gardenTag)), 1);
        // The list shows each task's tags as chips on its row.
        QTRY_VERIFY_WITH_TIMEOUT(chips(app, mw().bravo) == QStringList{"mango"}, 3000);
    }

    // The counts follow the tasks: completing, tagging, deleting and undo.
    void countsUpdateLive() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().mango)), 2, 3000);
        // Completing a tagged task takes it off the count.
        QVERIFY(selectRow(app, mw().bravo));
        app.press(Qt::Key_Period, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().mango)), 1, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().mango)), 2, 3000);
        // Tagging a task with the l key adds one.
        QVERIFY(selectRow(app, mw().charlie));
        app.press(Qt::Key_L);
        QTRY_VERIFY_WITH_TIMEOUT(tk::waitFor([&] { return findObject("commandPopup") && findObject("commandPopup")->property("visible").toBool(); }), 3000);
        app.type("mango");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().mango)), 3, 3000);
        // Deleting a task takes it off too.
        QVERIFY(selectRow(app, mw().charlie));
        app.press(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().mango)), 2, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().mango)), 3, 3000);
        // A new tag from the panel's own button shows up with no count.
        QVERIFY(tk::clickNamed("utilityAdd"));
        QVERIFY(clickMenuEntry("New Tag"));
        QVERIFY(nameDialog("tagNameField", "fresh"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(tagIdNamed(app, "fresh") != 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem(tagName(tagIdNamed(app, "fresh"))) != nullptr, 3000);
        QCOMPARE(countOf(tagName(tagIdNamed(app, "fresh"))), 0);
        QCOMPARE(panelRows(app), (QStringList{"[Tags]", "apple", "fresh", "garden", "mango", "zebra"}));
    }

    // "Filter by another tag to add it to the filter; ... repeat on a filtering
    // tag to take it back out." The clear button empties the filter at once.
    void clickingATagFiltersAndClickingAgainClears() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
        QVERIFY(setSeparated(app, false));
        QVERIFY(!findItem("tagFilterClear"));
        QVERIFY(clickTag(app, "mango"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EB"), 3000);   // Echo (overdue), Bravo
        QCOMPARE(app.backend().tagFilter(), QVariantList{mw().mango});
        QVERIFY(findItem("tagFilterClear"));
        // A second tag adds to the filter.
        QVERIFY(clickTag(app, "zebra"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBA"), 3000);
        QCOMPARE(app.backend().tagFilter().size(), 2);
        // Clicking a filtering tag takes it back out.
        QVERIFY(clickTag(app, "mango"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("A"), 3000);
        QVERIFY(clickTag(app, "zebra"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
        QVERIFY(app.backend().tagFilter().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("tagFilterClear"), 3000);
        // And the clear button takes them all out.
        QVERIFY(clickTag(app, "apple"));
        QVERIFY(clickTag(app, "mango"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBD"), 3000);
        QVERIFY(tk::clickNamed("tagFilterClear"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
        QVERIFY(app.backend().tagFilter().isEmpty());
        // The filter reaches every view, not only the list.
        QVERIFY(clickTag(app, "garden"));
        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(rowIds() == QList<qint64>{app.ids.plants}, 3000);
        QVERIFY(clickTag(app, "garden"));
        QTRY_VERIFY_WITH_TIMEOUT(rowIds().size() > 3, 3000);
    }

    // "Right-click → Rename...: the change applies to every task using that tag."
    void renameATagAndTasksFollow() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QTRY_VERIFY_WITH_TIMEOUT(chips(app, mw().bravo) == QStringList{"mango"}, 3000);
        QVERIFY(rowRightClick(tagName(mw().mango), "Rename…"));
        QQuickItem *field = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((field = findItem("tagNameField")) != nullptr, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(field->property("text").toString(), QString("mango"), 3000);   // Prefilled.
        QVERIFY(nameDialog("tagNameField", "papaya"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().tag(mw().mango).name, QString("papaya"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(chips(app, mw().bravo) == QStringList{"papaya"} && chips(app, mw().echo) == QStringList{"papaya"}, 3000);
        QCOMPARE(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "papaya", "zebra"}));
        QCOMPARE(app.task(mw().bravo).tagIds, QList<qint64>{mw().mango});   // Same tag, new name.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().tag(mw().mango).name, QString("mango"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(chips(app, mw().bravo) == QStringList{"mango"}, 3000);

        // Cancel leaves the name as it was.
        QVERIFY(rowRightClick(tagName(mw().mango), "Rename…"));
        QVERIFY(nameDialog("tagNameField", "nothing"));
        QVERIFY(clickDialogButton("Cancel"));
        QVERIFY(dialogGone("tagNameField"));
        QCOMPARE(app.store().tag(mw().mango).name, QString("mango"));
    }

    // "A rename to a name that's already taken by another tag is rejected, so
    // tags always stay unique" (tags.md).
    void renameToATakenNameIsRejected() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(rowRightClick(tagName(mw().mango), "Rename…"));
        QVERIFY(nameDialog("tagNameField", "zebra"));
        QVERIFY(clickDialogButton("OK"));
        QVERIFY(dialogGone("tagNameField"));
        QTest::qWait(100);
        QCOMPARE(app.store().tag(mw().mango).name, QString("mango"));
        int zebras = 0;
        for (const Tag &t : app.store().tags())
            zebras += !t.deleted && t.name == "zebra";
        QCOMPARE(zebras, 1);
    }

    // Delete Tag asks first, takes the tag off every task, and undo brings it back.
    void deleteATag() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QTRY_VERIFY_WITH_TIMEOUT(chips(app, mw().bravo) == QStringList{"mango"}, 3000);
        QVERIFY(rowRightClick(tagName(mw().mango), "Delete Tag"));
        QTRY_VERIFY_WITH_TIMEOUT(findText("Delete Tag") != nullptr, 3000);
        QVERIFY(clickDialogButton("Cancel"));
        QTRY_VERIFY_WITH_TIMEOUT(findText("Delete the tag \"mango\"? It is removed from every task.") == nullptr, 3000);
        QVERIFY(!app.store().tag(mw().mango).deleted);

        QVERIFY(rowRightClick(tagName(mw().mango), "Delete Tag"));
        QTRY_VERIFY_WITH_TIMEOUT(findText("Delete the tag \"mango\"? It is removed from every task.") != nullptr, 3000);
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().tag(mw().mango).deleted, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "zebra"}), 3000);
        QVERIFY(app.task(mw().bravo).tagIds.isEmpty() && app.task(mw().echo).tagIds.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(chips(app, mw().bravo).isEmpty(), 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().tag(mw().mango).deleted, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(chips(app, mw().bravo) == QStringList{"mango"} && chips(app, mw().echo) == QStringList{"mango"}, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "mango", "zebra"}), 3000);
    }

    // Tag groups: created from the panel's + menu, tags move in and out by the
    // right-click menu, a group collapses and expands, and it is renamed and
    // deleted (its tags stay).
    void tagGroupsCreateMoveCollapse() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(tk::clickNamed("utilityAdd"));
        QVERIFY(clickMenuEntry("New Tag Group"));
        QVERIFY(nameDialog("tagNameField", "Contexts"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(groupIdNamed(app, "Contexts") != 0, 3000);
        const qint64 group = groupIdNamed(app, "Contexts");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("tagGroup:" + QString::number(group)) != nullptr, 3000);

        // Move two tags in by the menu.
        QVERIFY(rowRightClickSub(tagName(mw().apple), "Move to Group", "Contexts"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().tag(mw().apple).groupId, group, 3000);
        QVERIFY(rowRightClickSub(tagName(mw().zebra), "Move to Group", "Contexts"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().tag(mw().zebra).groupId, group, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "garden", "mango", "[Contexts]", "apple", "zebra"}), 3000);

        // Clicking the heading folds the group, clicking again unfolds it.
        const QString header = "tagGroup:" + QString::number(group);
        QVERIFY(tk::clickNamed(header));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "garden", "mango", "[Contexts]"}), 3000);
        QVERIFY(tk::clickNamed(header));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "garden", "mango", "[Contexts]", "apple", "zebra"}), 3000);
        // So does the menu's Collapse / Expand.
        QVERIFY(rowRightClick(header, "Collapse"));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "garden", "mango", "[Contexts]"}), 3000);
        QVERIFY(rowRightClick(header, "Expand"));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app).size(), 6, 3000);

        // Out again: Move to Group > No Group.
        QVERIFY(rowRightClickSub(tagName(mw().zebra), "Move to Group", "No Group"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().tag(mw().zebra).groupId, qint64(0), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "garden", "mango", "zebra", "[Contexts]", "apple"}), 3000);

        // Rename the group, add a tag in it, and delete it: its tags stay.
        QVERIFY(rowRightClick(header, "Rename…"));
        QVERIFY(nameDialog("tagNameField", "Places"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().tagGroups().value(group).name, QString("Places"), 3000);
        QVERIFY(rowRightClick(header, "New Tag in Group"));
        QVERIFY(nameDialog("tagNameField", "attic"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(tagIdNamed(app, "attic") != 0, 3000);
        QCOMPARE(app.store().tag(tagIdNamed(app, "attic")).groupId, group);
        QVERIFY(rowRightClick(header, "Delete Group"));
        QTRY_VERIFY_WITH_TIMEOUT(findText("Delete the group \"Places\"? Its tags are kept.") != nullptr, 3000);
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().tagGroups().value(group).deleted, 3000);
        QVERIFY(!app.store().tag(tagIdNamed(app, "attic")).deleted);
        QCOMPARE(app.store().tag(tagIdNamed(app, "attic")).groupId, qint64(0));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "attic", "garden", "mango", "zebra"}), 3000);
    }

    // Dragging a tag onto a group heading puts it in the group; dragging it
    // onto another tag puts it before that tag and switches to manual order.
    void dragTagsIntoGroupsAndIntoOrder() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        const qint64 group = app.backend().newTagGroup("Contexts");
        QTRY_VERIFY_WITH_TIMEOUT(findItem("tagGroup:" + QString::number(group)) != nullptr, 3000);
        QVERIFY(tk::settled(tagName(mw().mango)));
        tk::dragSlowly(findItem(tagName(mw().mango)), findItem("tagGroup:" + QString::number(group)));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().tag(mw().mango).groupId, group, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "zebra", "[Contexts]", "mango"}), 3000);

        // Drag zebra before apple: manual order, with the others where they were.
        QVERIFY(tk::settled(tagName(mw().zebra)));
        tk::dragSlowly(findItem(tagName(mw().zebra)), findItem(tagName(mw().apple)));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app).mid(0, 3), (QStringList{"[Tags]", "zebra", "apple"}), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("tagsManualToggle")->property("checked").toBool(), 3000);
    }

    // "Tag Sorting ... flips between Alphabetically and Manually", kept across a restart.
    void tagSortingToggleAndPersist() {
        Keep keep(seedMisc);
        QVERIFY(keep.start());
        {
            App &app = *keep;
            QVERIFY(openSorting(app));
            QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "mango", "zebra"}), 3000);
            QVERIFY(!findItem("tagsManualToggle")->property("checked").toBool());
            QVERIFY(tk::clickNamed("tagsManualToggle"));
            // Manual follows the order the tags were made in: garden, zebra, mango, apple.
            QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "garden", "zebra", "mango", "apple"}), 3000);
            QVERIFY(findItem("tagsManualToggle")->property("checked").toBool());
        }
        QVERIFY(keep.start());
        App &app = *keep;
        QVERIFY(openSorting(app));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "garden", "zebra", "mango", "apple"}), 3000);
        QVERIFY(tk::clickNamed("tagsManualToggle"));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "mango", "zebra"}), 3000);
    }

    // The eye chip next to the sort chip limits the panel to tags in use.
    void usedTagsOnlyButton() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        app.backend().newTag("spare", 0);
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "mango", "spare", "zebra"}), 3000);
        QVERIFY(tk::clickNamed("tagUsedOnly"));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "mango", "zebra"}), 3000);
        QVERIFY(tk::clickNamed("tagUsedOnly"));
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app).size(), 6, 3000);
    }

    // "Pausing a tag: every task that carries that tag is hidden from view";
    // "Task > Show Paused" shows them without resuming; Resume brings them back.
    void pauseAndResumeATag() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(setSeparated(app, false));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBDAC"), 3000);
        QVERIFY(rowRightClick(tagName(mw().mango), "Pause…"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().tag(mw().mango).paused, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("DAC"), 3000);   // Bravo and Echo are hidden.
        // Also from All, Today and search.
        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(mw().bravo) == nullptr && app.row(mw().echo) == nullptr && app.row(mw().alpha) != nullptr, 3000);
        QVERIFY(openSorting(app));
        // Show Paused (Ctrl+U) lets them back in without resuming.
        app.press(Qt::Key_U, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBDAC"), 3000);
        QVERIFY(app.store().tag(mw().mango).paused);
        app.press(Qt::Key_U, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("DAC"), 3000);
        // So does the eye in the panel's footer.
        QVERIFY(tk::clickNamed("tagShowPaused"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBDAC"), 3000);
        QVERIFY(tk::clickNamed("tagShowPaused"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("DAC"), 3000);
        // Resume...
        QVERIFY(rowRightClick(tagName(mw().mango), "Resume…"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().tag(mw().mango).paused, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBDAC"), 3000);
        // Pausing is one undo step.
        QVERIFY(rowRightClick(tagName(mw().mango), "Pause…"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("DAC"), 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBDAC"), 3000);
    }

    // "/" opens the tag search bar: typing narrows the panel to tags starting
    // with the letters, Return filters by the first match, Escape closes the bar.
    void tagSearchBar() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(setSeparated(app, false));
        QVERIFY(selectRow(app, mw().alpha));
        QVERIFY(!findItem("tagSearch"));
        app.press(Qt::Key_Slash);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("tagSearch") && findItem("tagSearch")->property("activeFocus").toBool(), 3000);
        app.type("ma");
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), QStringList{"mango"}, 3000);
        app.press(Qt::Key_Return);   // Filters by the first match.
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EB"), 3000);
        QCOMPARE(app.backend().tagFilter(), QVariantList{mw().mango});
        app.press(Qt::Key_Return);   // And again takes it off.
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
        // Another search: no match leaves the panel empty; Escape closes the bar and shows all.
        replaceText(app, "zz");
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), QStringList{}, 3000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("tagSearch"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(panelRows(app), (QStringList{"[Tags]", "apple", "garden", "mango", "zebra"}), 3000);
    }

    // "Drag a tag from the panel onto a task in the list" tags it; "drag a
    // selection of tasks onto a tag" tags them all; dropped on a group, the group's tags.
    void dragTagsOntoTasksAndTasksOntoTags() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(setSeparated(app, false));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
        // Tag onto task.
        QVERIFY(tk::settled(tagName(mw().apple)) && tk::settled(QString("taskRow:%1").arg(mw().bravo)));
        QVERIFY2(dragShowingPill(findItem(tagName(mw().apple)), app.row(mw().bravo), "tagDragProxy"), "no pill followed the pointer");
        QTRY_VERIFY_WITH_TIMEOUT(tk::tagNames(app, app.task(mw().bravo)) == QStringList({"apple", "mango"}), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().apple)), 2, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(tk::tagNames(app, app.task(mw().bravo)) == QStringList({"mango"}), 3000);

        // Task onto tag.
        QVERIFY(selectRow(app, mw().charlie));
        QVERIFY(tk::settled(tagName(mw().zebra)));
        tk::dragSlowly(app.row(mw().charlie), findItem(tagName(mw().zebra)));
        QTRY_VERIFY_WITH_TIMEOUT(tk::tagNames(app, app.task(mw().charlie)) == QStringList({"zebra"}), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(tagName(mw().zebra)), 2, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(mw().charlie).tagIds.isEmpty(), 3000);

        // Task onto a group heading: every tag in the group.
        const qint64 group = app.backend().newTagGroup("Pair");
        app.backend().setTagGroup(mw().apple, group);
        app.backend().setTagGroup(mw().zebra, group);
        const QString header = "tagGroup:" + QString::number(group);
        QTRY_VERIFY_WITH_TIMEOUT(findItem(header) != nullptr && findItem(tagName(mw().apple)) != nullptr, 3000);
        QVERIFY(selectRow(app, mw().charlie));
        QVERIFY(tk::settled(header));
        tk::dragSlowly(app.row(mw().charlie), findItem(header));
        QTRY_VERIFY_WITH_TIMEOUT(tk::tagNames(app, app.task(mw().charlie)) == QStringList({"apple", "zebra"}), 3000);
    }

    // The Locations tab: add, edit, delete, filter, pause and drop a task on one.
    void locationsTab() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(tk::clickNamed("utilityTabLocations"));
        QTRY_VERIFY_WITH_TIMEOUT(findText("No locations yet") != nullptr, 3000);
        QVERIFY(!findItem("tagSearch"));

        // Add one with a name and an address.
        QVERIFY(tk::clickNamed("utilityAdd"));
        QVERIFY(nameDialog("locationNameField", "Hardware store"));
        QTest::keyClick(findItem("locationNameField")->window(), Qt::Key_Return);   // On to the address.
        QTRY_VERIFY_WITH_TIMEOUT(findItem("locationAddressField")->property("activeFocus").toBool(), 3000);
        typeInto(findItem("locationAddressField")->window(), "12 Main St");
        QVERIFY(clickDialogButton("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(placeIdNamed(app, "Hardware store") != 0, 3000);
        const qint64 shop = placeIdNamed(app, "Hardware store");
        QCOMPARE(app.store().locations().value(shop).address, QString("12 Main St"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("placeRow:" + QString::number(shop)) != nullptr, 3000);
        QVERIFY(findText("12 Main St") != nullptr);
        QVERIFY(findText("No locations yet") == nullptr);
        const QString row = "placeRow:" + QString::number(shop);

        // Edit: the dialog starts with what is there.
        QVERIFY(rowRightClick(row, "Edit…"));
        QQuickItem *name = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((name = findItem("locationNameField")) != nullptr, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(name->property("text").toString(), QString("Hardware store"), 3000);
        QCOMPARE(findItem("locationAddressField")->property("text").toString(), QString("12 Main St"));
        QVERIFY(nameDialog("locationNameField", "Garden centre"));
        QVERIFY(clickDialogButton("OK"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().locations().value(shop).name, QString("Garden centre"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Garden centre") != nullptr && findText("Hardware store") == nullptr, 3000);

        // Assign it by dragging a task onto it; undo takes it off again. (A click on the
        // list's empty space puts the keyboard back in the list after the dialog.)
        QQuickItem *list = findItem("taskList");
        click(list, Qt::LeftButton, pointIn(list, 0.5, 0.97));
        qInfo() << "rows" << rowIds() << "focus" << app.focusDescription();
        QVERIFY(selectRow(app, app.ids.shelf));
        QVERIFY(tk::settled(row));
        tk::dragSlowly(app.row(app.ids.shelf), findItem(row));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.shelf).locationIds == QList<qint64>{shop}, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(placeCountOf(shop), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(tk::badge("rowLocation", app.ids.shelf), 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.shelf).locationIds.isEmpty(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(placeCountOf(shop), 0, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.shelf).locationIds == QList<qint64>{shop}, 3000);

        // Filter: only its tasks; clicking again shows all.
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) != nullptr, 3000);
        QVERIFY(tk::clickNamed(row));
        QTRY_VERIFY_WITH_TIMEOUT(rowIds() == QList<qint64>{app.ids.shelf}, 3000);
        QVERIFY(tk::clickNamed(row));
        QTRY_VERIFY_WITH_TIMEOUT(rowIds().size() == 4, 3000);

        // Pause hides its tasks; Resume brings them back.
        QVERIFY(rowRightClick(row, "Pause"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().locations().value(shop).paused, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.shelf) == nullptr && app.row(app.ids.buyMilk) != nullptr, 3000);
        QVERIFY(rowRightClick(row, "Resume"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().locations().value(shop).paused && app.row(app.ids.shelf) != nullptr, 3000);

        // Delete removes it from the task too, and the tab says there are none.
        QVERIFY(rowRightClick(row, "Delete"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().locations().value(shop).deleted, 3000);
        QVERIFY(app.task(app.ids.shelf).locationIds.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(findItem(row) == nullptr && findText("No locations yet") != nullptr, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().locations().value(shop).deleted && findItem(row) != nullptr, 3000);
        // Back to the Tags tab.
        QVERIFY(tk::clickNamed("utilityTabTags"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem(tagName(app.store().tags().isEmpty() ? 0 : app.store().tags().begin().key())) != nullptr, 3000);
    }
};

REGISTER_TEST(E2EMiscTags)

#include "test_misc_tags.moc"
