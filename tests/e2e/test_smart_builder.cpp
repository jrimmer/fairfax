// End-to-end scenarios for building Smart Lists: the Search Options sheet
// (smart-lists.md, The Rule Builder; Saving a Filter as a Smart List;
// Updating or Editing a Smart List; Completed Tasks in a Smart List), saving
// from the search bar, editing, renaming, deleting with undo, the sheet's
// options, and the Combine Today and Overdue walkthrough.
//
// Precrime's builder is made of chips rather than one row per clause: a chip
// per `type:` preset (click to include, again to exclude, again to clear),
// pickers for named tags, lists, tag groups, list groups and locations,
// relative-time fields, and groups of whole filters. "Any" and "All" are the
// two TASK TYPE modes.

#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

const SL Hi{"Rent overdue", "Fence in 6 days", "Starred urgent", "Launch project"};
const SL Med{"Milk today", "Passport in 14 days", "Shopping checklist", "Medium plain"};
const SL DueToday{"Milk today", "Dentist today", "Two day window", "Fresh window", "Daily repeat", "Repeating window"};
const SL Overdue{"Rent overdue", "Invoice overdue"};
const SL Tagged{"Rent overdue", "Invoice overdue", "Milk today", "Parcel tomorrow", "Call mum", "Email boss",
                "Fix plumber leak", "Plumber for garden", "Plumber for office", "Blocked work", "Play tagged work",
                "Call work", "Someday idea", "Deep focus"};
const SL WorkTagged{"Email boss", "Blocked work", "Play tagged work", "Call work"};
const SL Contexts{"Milk today", "Parcel tomorrow", "Call mum", "Email boss", "Blocked work", "Play tagged work", "Call work"};

SL inLists(const SL &lists) {
    return specTitles([&](const Spec &s) { return shownInAll(s) && lists.contains(s.list); });
}

QString previewText() {
    QQuickItem *p = findItem("preview");
    return p ? p->property("text").toString() : QString();
}

QDate day(int offset) { return QDate::currentDate().addDays(offset); }

}  // namespace

class E2ESmartBuilder : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;

    // Opens the builder on All, from a clean slate.
    void openFresh() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        QVERIFY(escapeSearch(app));
        QVERIFY(openBuilder(app));
        QCOMPARE(builderQuery(), QString());
    }

    // Clicks "Search" and checks what the pane lists.
    void applyAndExpect(const QString &query, const QStringList &expected) {
        App &app = *m_app;
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), query, 5000);
        QVERIFY(pressBuilder("searchButton"));
        QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
        QCOMPARE(app.backend().search(), query);
        SMART_SHOWS(app, expected);
    }

private slots:
    void init() {
        m_app = std::make_unique<App>(seedSmart);
        QVERIFY(m_app->ok);
    }
    void cleanup() { m_app.reset(); }

    // "Open the Search Options panel… click [+] to add a clause… Joined with
    // all (AND) or any (OR) at the top." Two chips, Any mode, saved under a
    // name: it appears in the sidebar's Smart Lists with its count, and opens.
    void buildSmartListByClickingChips() {
        App &app = *m_app;
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(pressBuilder("typeChip:hiprio"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio"), 5000);
        QCOMPARE(chipState("typeChip:hiprio"), 1);
        QVERIFY(pressBuilder("typeChip:medprio"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio OR medprio"), 5000);
        QCOMPARE(previewText(), QString("type: hiprio OR medprio"));
        QVERIFY(smartLists(app).isEmpty());

        const qint64 id = saveFromBuilder(app, "Prio mix");
        QVERIFY(id);
        QCOMPARE(app.store().list(id).smartQuery, QString("type: hiprio OR medprio"));
        QCOMPARE(app.store().list(id).kind, int(ListKind::Smart));
        QCOMPARE(app.backend().sourceKey(), lk(id));
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Prio mix"), 5000);
        SMART_SHOWS(app, Hi + Med);
        // The sidebar's Smart Lists section, with the right count.
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"Prio mix"}), 5000);
        QVERIFY(sidebarOrder().contains("g:-1"));
        QCOMPARE(shownName(groupHeader(-1)), QString("Smart Lists"));
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, (Hi + Med).size(), 5000);
        // The pane shows the rule it is built on.
        LIST_ITEM(rule, findItem("smartQueryField"));
        QCOMPARE(rule->property("text").toString(), QString("type: hiprio OR medprio"));

        // Reversed: selecting another list and coming back shows the same rows.
        QVERIFY(pane(app, "today"));
        QVERIFY(pane(app, lk(id)));
        SMART_SHOWS(app, Hi + Med);
    }

    // "Option … All": the other join. "Any of these (OR)" / "All of these
    // (AND)" change the same chips' query live.
    void allModeJoinsChipsWithAnd() {
        App &app = *m_app;
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(pressBuilder("typeChip:hiprio"));
        QVERIFY(pressBuilder("typeChip:due"));
        QVERIFY(pressBuilder("typeModeAnd"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio AND due"), 5000);
        // "Search" applies it to the search bar.
        applyAndExpect("type: hiprio AND due", SL{"Rent overdue", "Fence in 6 days", "Starred urgent"});
        if (QTest::currentTestFailed())
            return;
        // Back in the sheet the same rule is read back; flip to Any.
        QVERIFY(openBuilder(app));
        QCOMPARE(chipState("typeChip:hiprio"), 1);
        QCOMPARE(chipState("typeChip:due"), 1);
        QVERIFY(pressBuilder("typeModeOr"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio OR due"), 5000);
        const SL dated{"Rent overdue", "Invoice overdue", "Milk today", "Dentist today", "Parcel tomorrow", "Report in 3 days",
                       "Fence in 6 days", "Taxes in 9 days", "Passport in 14 days", "Began yesterday due in 2", "Two day window",
                       "Fresh window", "Repeating window", "Starred urgent", "Weekly repeat", "Daily repeat", "Launch step one",
                       "Launch step later", "Archive item", "Launch project"};
        applyAndExpect("type: hiprio OR due", dated);
        if (QTest::currentTestFailed())
            return;
    }

    // "Chip states: off -> include -> exclude (written as !value) -> off."
    // Removing a rule is clicking it round to off; right-click excludes
    // directly.
    void chipsCycleAndRemoveRules() {
        App &app = *m_app;
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QCOMPARE(chipState("typeChip:hiprio"), 0);
        QVERIFY(pressBuilder("typeChip:hiprio"));
        QCOMPARE(chipState("typeChip:hiprio"), 1);
        QVERIFY(pressBuilder("typeChip:hiprio"));
        QCOMPARE(chipState("typeChip:hiprio"), 2);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: !hiprio"), 5000);
        QVERIFY(pressBuilder("typeChip:hiprio"));
        QCOMPARE(chipState("typeChip:hiprio"), 0);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString(), 5000);
        QVERIFY(pressBuilder("typeChip:starred", Qt::RightButton));
        QCOMPARE(chipState("typeChip:starred"), 2);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: !starred"), 5000);
        // An include and an exclude together.
        QVERIFY(pressBuilder("typeChip:prio"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: prio AND !starred"), 5000);
        applyAndExpect("type: prio AND !starred", without(Hi + Med + SL{"Dentist today", "Weekly repeat", "Low plain"}, SL{"Starred urgent"}));
        if (QTest::currentTestFailed())
            return;
        // Clear resets the whole sheet and the search bar.
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("clearButton"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString(), 5000);
        QCOMPARE(chipState("typeChip:prio"), 0);
        QCOMPARE(app.backend().search(), QString());
        QVERIFY(closeBuilder(app));
        SMART_SHOWS(app, allShown());
    }

    // The named pickers: "Searching for … a named list, a named tag or tag
    // group … type those as query keywords (plain text, lists:, tags:,
    // taggroups:)": the sheet writes them for you.
    void pickerChips_data() {
        QTest::addColumn<QString>("tab");
        QTest::addColumn<QStringList>("include");
        QTest::addColumn<QStringList>("exclude");
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("tags") << "tags" << SL{"urgent", "waiting"} << SL{} << "tags: urgent, waiting" << Overdue;
        QTest::newRow("excluded tag") << "tags" << SL{} << SL{"work"} << "tags: !work" << without(Tagged, WorkTagged);
        QTest::newRow("tag with a space") << "tags" << SL{"deep work"} << SL{} << "tags: \"deep work\"" << SL{"Deep focus"};
        QTest::newRow("lists") << "lists" << SL{"Work"} << SL{} << "lists: Work" << inLists(SL{"Work"});
        QTest::newRow("excluded list") << "lists" << SL{} << SL{"Archive"} << "lists: !Archive"
                                       << inLists(SL{"Home", "Garden", "Work", "Website", "Inbox"});
        QTest::newRow("tag groups") << "taggroups" << SL{"Someday"} << SL{} << "taggroups: Someday" << SL{"Someday idea"};
        QTest::newRow("contexts group") << "taggroups" << SL{"Contexts"} << SL{} << "taggroups: Contexts" << Contexts;
        QTest::newRow("list groups") << "groups" << SL{"Projects"} << SL{} << "groups: Projects" << inLists(SL{"Work", "Website"});
        QTest::newRow("locations") << "locations" << SL{"Office"} << SL{} << "locations: Office"
                                   << SL{"Office desk", "Office and playground"};
    }
    void pickerChips() {
        App &app = *m_app;
        QFETCH(QString, tab);
        QFETCH(QStringList, include);
        QFETCH(QStringList, exclude);
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        Q_UNUSED(app);
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(pressBuilder("pickerTab:" + tab));
        for (const QString &name : include) {
            QVERIFY2(pressBuilder("nameChip:" + tab + ":" + name), qPrintable(name));
            QCOMPARE(chipState("nameChip:" + tab + ":" + name), 1);
        }
        for (const QString &name : exclude) {
            QVERIFY2(pressBuilder("nameChip:" + tab + ":" + name, Qt::RightButton), qPrintable(name));
            QCOMPARE(chipState("nameChip:" + tab + ":" + name), 2);
        }
        applyAndExpect(query, expected);
        if (QTest::currentTestFailed())
            return;
    }

    // "modified:, created: and completed: … a number plus a unit": the sheet's
    // WITHIN THE LAST rows, in days or weeks.
    void relativeTimeFields_data() {
        QTest::addColumn<QString>("which");
        QTest::addColumn<QString>("number");
        QTest::addColumn<QString>("unit");
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("created days") << "created" << "2" << "d" << "created: 2d" << SL{"Fresh window", "Fresh note"};
        QTest::newRow("created weeks") << "created" << "2" << "w" << "created: 2w" << SL{"Fresh window", "Fresh note", "Made 3 days ago"};
        QTest::newRow("modified days") << "modified" << "2" << "d" << "modified: 2d" << SL{"Fresh window", "Fresh note", "Edited lately"};
        QTest::newRow("completed days") << "completed" << "7" << "d" << "completed: 7d"
                                        << SL{"Done just now", "Launch step done", "Done yesterday", "Finished step", "Done 3 days ago"};
    }
    void relativeTimeFields() {
        QFETCH(QString, which);
        QFETCH(QString, number);
        QFETCH(QString, unit);
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(fillBuilder(*m_app, "dateN:" + which, number));
        if (unit == "w")
            QVERIFY(pressBuilder("dateUnit:" + which + ":w"));
        applyAndExpect(query, expected);
        if (QTest::currentTestFailed())
            return;
    }

    // Free text, a type chip and a tag together: "build the structure
    // visually, then add the text and named criteria in the field".
    void freeTextAndChipsTogether() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(fillBuilder(*m_app, "freeInput", "plumber"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("plumber"), 5000);
        QVERIFY(pressBuilder("typeChip:starred"));
        QVERIFY(pressBuilder("pickerTab:tags"));
        QVERIFY(pressBuilder("nameChip:tags:builders"));
        QVERIFY(pressBuilder("pickerTab:groups"));
        QVERIFY(pressBuilder("nameChip:groups:Home"));
        applyAndExpect("plumber type: starred tags: builders groups: Home", SL{"Fix plumber leak", "Plumber for garden"});
        if (QTest::currentTestFailed())
            return;
    }

    // "For finer expressions, hold the Option key to create a sub-filter, a
    // nested rule group": Add group, with And / Or / Not joins.
    void groupsOfWholeFilters() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(pressBuilder("addGroup"));
        QVERIFY(fillBuilder(*m_app, "groupField", "tags: urgent OR tags: waiting"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("(tags: urgent OR tags: waiting)"), 5000);
        QVERIFY(pressBuilder("typeChip:overdue"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: overdue (tags: urgent OR tags: waiting)"), 5000);
        QVERIFY(pressBuilder("groupJoin:0:not"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: overdue !(tags: urgent OR tags: waiting)"), 5000);
        applyAndExpect("type: overdue !(tags: urgent OR tags: waiting)", SL{});
        if (QTest::currentTestFailed())
            return;
        QVERIFY(openBuilder(*m_app));
        QVERIFY(pressBuilder("groupJoin:0:and"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: overdue (tags: urgent OR tags: waiting)"), 5000);
        applyAndExpect("type: overdue (tags: urgent OR tags: waiting)", Overdue);
        if (QTest::currentTestFailed())
            return;
        // Remove the group again.
        QVERIFY(openBuilder(*m_app));
        QVERIFY(pressBuilder("removeGroup:0"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: overdue"), 5000);
        QVERIFY(closeBuilder(*m_app));
    }

    // "The keyword syntax is most useful for refining a saved rule": the
    // QUERY box can be typed into, and the chips follow.
    void queryBoxEditedByHand() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        LIST_ITEM(box, findItem("preview"));
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(box->property("activeFocus").toBool(), 5000);
        m_app->type("type: hiprio OR medprio tags: urgent");
        QTRY_COMPARE_WITH_TIMEOUT(chipState("typeChip:hiprio"), 1, 5000);
        QCOMPARE(chipState("typeChip:medprio"), 1);
        QCOMPARE(chipState("typeChip:lowprio"), 0);
        QVERIFY(pressBuilder("pickerTab:tags"));
        QTRY_COMPARE_WITH_TIMEOUT(chipState("nameChip:tags:urgent"), 1, 5000);
        // Clicking a chip after hand-editing rewrites the box from the chips.
        QVERIFY(pressBuilder("typeChip:lowprio"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio OR medprio OR lowprio tags: urgent"), 5000);
        QVERIFY(closeBuilder(*m_app));
    }

    // "Once your search returns the perspective you want, click the [+] button
    // … There is no naming prompt. The Smart List is created straight away and
    // named after the query." The save button next to the typed query.
    void saveSearchBarQueryAsSmartList() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(id);
        QCOMPARE(app.store().list(id).name, QString("type: hiprio"));
        QCOMPARE(app.store().list(id).smartQuery, QString("type: hiprio"));
        QCOMPARE(app.backend().search(), QString());
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("type: hiprio"), 5000);
        SMART_SHOWS(app, Hi);
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"type: hiprio"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, Hi.size(), 5000);
        // Deleting a Smart List never deletes its tasks.
        const int tasks = app.store().tasks().size();
        QVERIFY(listMenu(app, id, "Delete Smart List"));
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, id), 5000);
        QCOMPARE(app.store().tasks().size(), tasks);
        for (const QString &t : Hi)
            QVERIFY(!app.taskTitled(t).deleted && app.taskTitled(t).id);
        // A second save of the same search makes a second list.
        QVERIFY(pane(app, "all"));
        QVERIFY(saveSearchAsSmart(app, "type: hiprio"));
        QCOMPARE(smartLists(app).size(), 1);
    }

    // "File > New > New Smart List… the + button at the bottom of the Lists
    // Panel → New Smart List": it opens the sheet over All.
    void newSmartListFromSidebarMenu() {
        App &app = *m_app;
        QVERIFY(pane(app, "today"));
        QVERIFY(plusMenu("New Smart List…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QCOMPARE(app.backend().sourceKey(), QString("all"));
        QVERIFY(pressBuilder("typeChip:starred"));
        QVERIFY(pressBuilder("typeChip:hiprio"));
        const qint64 id = saveFromBuilder(app, "Stars");
        QVERIFY(id);
        QCOMPARE(app.store().list(id).smartQuery, QString("type: starred OR hiprio"));
        SMART_SHOWS(app, (SL{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote", "Plumber for garden",
                            "Plumber for office", "Rent overdue", "Fence in 6 days", "Launch project"}));
    }

    // "To change a Smart List's rule, select the Smart List in the sidebar and
    // its rule appears in the search bar. Adjust the query (or the date filter
    // and rule builder)… the Smart List … is updated in place and the
    // perspective changes immediately." Edit Search… reads the rule back into
    // the sheet; Update writes it.
    void editSearchChangesTheRule() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(id);
        QVERIFY(listMenu(app, id, "Edit Search…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QCOMPARE(chipState("typeChip:hiprio"), 1);
        QCOMPARE(builderQuery(), QString("type: hiprio"));
        QVERIFY(pressBuilder("typeChip:medprio"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio OR medprio"), 5000);
        QVERIFY(pressBuilder("updateButton"));
        QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).smartQuery, QString("type: hiprio OR medprio"), 5000);
        QCOMPARE(smartLists(app).size(), 1);
        SMART_SHOWS(app, Hi + Med);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, (Hi + Med).size(), 5000);
        LIST_ITEM(rule, findItem("smartQueryField"));
        QCOMPARE(rule->property("text").toString(), QString("type: hiprio OR medprio"));
        // Reversed: take medprio out again.
        app.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QVERIFY(pressBuilder("typeChip:medprio"));   // Include, then exclude…
        QVERIFY(pressBuilder("typeChip:medprio"));   // …then off.
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio"), 5000);
        QVERIFY(pressBuilder("updateButton"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).smartQuery, QString("type: hiprio"), 5000);
        SMART_SHOWS(app, Hi);
    }

    // The rule shown above the tasks is editable in place; Return commits it,
    // Escape puts the saved text back.
    void editRuleInPlace() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(id);
        LIST_ITEM(rule, findItem("smartQueryField"));
        click(rule);
        QTRY_VERIFY_WITH_TIMEOUT(rule->property("activeFocus").toBool(), 5000);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("type: starred");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).smartQuery, QString("type: starred"), 5000);
        SMART_SHOWS(app, (SL{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote", "Plumber for garden",
                            "Plumber for office"}));
        // Escape drops what was typed.
        click(findItem("smartQueryField"));
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("type: nothing here");
        app.press(Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(findItem("smartQueryField")->property("text").toString(), QString("type: starred"), 5000);
        QCOMPARE(app.store().list(id).smartQuery, QString("type: starred"));
    }

    // "Save as New…" in the sheet leaves the edited list alone and creates
    // another, "If it is not [the source], the button creates a new Smart List
    // instead."
    void saveAsNewKeepsTheOriginal() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(id);
        QVERIFY(listMenu(app, id, "Edit Search…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QVERIFY(pressBuilder("typeChip:medprio"));
        const qint64 copy = saveFromBuilder(app, "Both");
        QVERIFY(copy && copy != id);
        QCOMPARE(app.store().list(id).smartQuery, QString("type: hiprio"));
        QCOMPARE(app.store().list(copy).smartQuery, QString("type: hiprio OR medprio"));
        SMART_SHOWS(app, Hi + Med);
        QVERIFY(pane(app, lk(id)));
        SMART_SHOWS(app, Hi);
        QCOMPARE(smartLists(app).size(), 2);
    }

    // Renaming: "Rename it in the Lists Panel whenever you like" (smart-lists.md,
    // Saving a Filter) by double-click, by the menu's Rename…, and in Edit List.
    void renameSmartList() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(id);
        // Double-click in the sidebar.
        doubleClick(app.sidebarRow(lk(id)), pointIn(app.sidebarRow(lk(id)), 0.4, 0.5));
        LIST_ITEM(editor, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(editor->property("activeFocus").toBool(), 5000);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("Urgent stuff");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).name, QString("Urgent stuff"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"Urgent stuff"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Urgent stuff"), 5000);
        QCOMPARE(app.store().list(id).smartQuery, QString("type: hiprio"));
        // The menu's Rename….
        QVERIFY(listMenu(app, id, "Rename…"));
        LIST_ITEM(menuEditor, findItem("sidebarListRename"));
        QTRY_VERIFY_WITH_TIMEOUT(menuEditor->property("activeFocus").toBool(), 5000);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("Prio");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).name, QString("Prio"), 5000);
        // The Edit List sheet; a Smart List has only a name and a colour there.
        QVERIFY(listMenu(app, id, "Edit List…"));
        LIST_ITEM(name, findItem("editListName"));
        QVERIFY(!findItem("editListGroup"));
        QVERIFY(!findItem("editListCompleted"));
        click(name);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("Top priority");
        QVERIFY(clickDialogButton("OK"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).name, QString("Top priority"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"Top priority"}), 5000);
        // The rule and its rows are untouched.
        QCOMPARE(app.store().list(id).smartQuery, QString("type: hiprio"));
        SMART_SHOWS(app, Hi);
    }

    // "A Smart List is just a perspective. Deleting one never deletes its
    // tasks." Delete Smart List, then undo (Ctrl+Z) brings it back, rule and
    // all; redo takes it away again.
    void deleteSmartListAndUndo() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "type: hiprio");
        QVERIFY(id);
        const int tasks = app.store().tasks().size();
        QVERIFY(listMenu(app, id, "Delete Smart List"));
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, id), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(id)), 5000);
        QCOMPARE(smartRowNames(app), SL{});
        QCOMPARE(app.store().tasks().size(), tasks);
        QVERIFY(app.backend().sourceKey() != lk(id));
        // Undo.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(listExists(app, id), 5000);
        QCOMPARE(app.store().list(id).smartQuery, QString("type: hiprio"));
        QCOMPARE(app.store().list(id).name, QString("type: hiprio"));
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{"type: hiprio"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, Hi.size(), 5000);
        QVERIFY(pane(app, lk(id)));
        SMART_SHOWS(app, Hi);
        // Redo.
        app.press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!listExists(app, id), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(id)), 5000);
    }

    // "By default a Smart List leaves completed tasks out, so the view stays on
    // what's still outstanding." The sheet's Completed switch: Default,
    // Include, Exclude.
    void completedSwitchOnSheet() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "lists: Work");
        QVERIFY(id);
        const SL open = inLists(SL{"Work"});
        // Done just now is in Work and was completed moments ago.
        SMART_SHOWS(app, open);
        QVERIFY(!paneTitles(app).contains("Done just now"));
        auto choose = [&](const QString &segment, int mode, const SL &expected) {
            QVERIFY(listMenu(app, id, "Edit Search…"));
            QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
            QVERIFY(pressBuilder(segment));
            QVERIFY(pressBuilder("updateButton"));
            QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).completedMode, mode, 5000);
            SMART_SHOWS(app, expected);
            QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, expected.size(), 5000);
        };
        choose("completedInclude", 1, open + SL{"Done just now"});
        choose("completedExclude", 2, open);
        choose("completedInclude", 1, open + SL{"Done just now"});
        choose("completedDefault", 0, open);
    }

    // "Include Scheduled brings tasks with a future start date into the
    // results, and it's remembered when you save the search as a Smart List."
    void includeScheduledSwitchOnSheet() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "lists: Work");
        QVERIFY(id);
        const SL open = inLists(SL{"Work"});
        SMART_SHOWS(app, open);
        QVERIFY(!paneTitles(app).contains("Starts tomorrow"));
        auto choose = [&](const QString &segment, bool on, const SL &expected) {
            QVERIFY(listMenu(app, id, "Edit Search…"));
            QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
            QVERIFY(pressBuilder(segment));
            QVERIFY(pressBuilder("updateButton"));
            QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).includeScheduled, on, 5000);
            SMART_SHOWS(app, expected);
        };
        choose("searchScheduledInclude", true, open + SL{"Starts tomorrow"});
        choose("searchScheduledHide", false, open);
        // Remembered: reopened, the switch reads as it was left.
        choose("searchScheduledInclude", true, open + SL{"Starts tomorrow"});
        QVERIFY(listMenu(app, id, "Edit Search…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QVERIFY(builder()->property("includeScheduled").toBool());
        QVERIFY(closeBuilder(app));
    }

    // The sheet's due-date options, saved with the rule: "Without Due Date:
    // only tasks with no due date (perfect for a Someday Smart List)", "Date
    // Range: a fixed From … To", "Dynamic Range: Next N days / weeks / months /
    // years".
    void dateRangeOptions() {
        App &app = *m_app;
        const SL dated = specTitles([](const Spec &s) { return shownInAll(s) && s.due != NoDay; });
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(pressBuilder("rangeNoDue"));
        const qint64 someday = saveFromBuilder(app, "Someday");
        QVERIFY(someday);
        QCOMPARE(app.store().list(someday).smartRange, QString("nodue"));
        QCOMPARE(app.store().list(someday).smartQuery, QString());
        SMART_SHOWS(app, without(allShown(), dated));
        LIST_ITEM(label, findText("Without due date"));
        Q_UNUSED(label);

        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("rangeNext"));
        QVERIFY(fillBuilder(app, "rangeN", "3"));
        const qint64 next3 = saveFromBuilder(app, "Next 3 Days");
        QVERIFY(next3);
        QCOMPARE(app.store().list(next3).smartRange, QString("next:3:d"));
        SMART_SHOWS(app, specTitles([](const Spec &s) { return shownInAll(s) && s.due != NoDay && s.due >= 0 && s.due < 3; }));
        QVERIFY(findText("In the next 3 days"));

        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("rangeNext"));
        QVERIFY(fillBuilder(app, "rangeN", "1"));
        QVERIFY(pressBuilder("rangeUnit:w"));
        const qint64 week = saveFromBuilder(app, "Next week");
        QVERIFY(week);
        QCOMPARE(app.store().list(week).smartRange, QString("next:1:w"));
        SMART_SHOWS(app, specTitles([](const Spec &s) { return shownInAll(s) && s.due != NoDay && s.due >= 0 && s.due < 7; }));

        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("rangeFixed"));
        QVERIFY(fillBuilder(app, "rangeFrom", day(1).toString(Qt::ISODate)));
        QVERIFY(fillBuilder(app, "rangeTo", day(3).toString(Qt::ISODate)));
        const qint64 fixed = saveFromBuilder(app, "Fixed range");
        QVERIFY(fixed);
        QCOMPARE(app.store().list(fixed).smartRange, QString("fixed:%1:%2").arg(day(1).toString(Qt::ISODate), day(3).toString(Qt::ISODate)));
        SMART_SHOWS(app, specTitles([](const Spec &s) { return shownInAll(s) && s.due != NoDay && s.due >= 1 && s.due <= 3; }));

        // Editing the range on a saved list: back to no range at all, which
        // needs a rule to stay savable.
        QVERIFY(listMenu(app, fixed, "Edit Search…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QVERIFY(pressBuilder("rangeNone"));
        QVERIFY(pressBuilder("typeChip:hiprio"));
        QVERIFY(pressBuilder("updateButton"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(fixed).smartRange, QString(), 5000);
        SMART_SHOWS(app, Hi);
    }

    // "Combine Today and Overdue": `type: duetoday OR overdue`, built with the
    // controls: two clauses, Any, no other rules. "All requires each task to
    // meet both conditions… Any includes a task when either condition matches."
    void combineTodayAndOverdueWithChips() {
        App &app = *m_app;
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(pressBuilder("typeChip:duetoday"));
        QVERIFY(pressBuilder("typeChip:overdue"));
        // Any is the default.
        applyAndExpect("type: duetoday OR overdue", DueToday + Overdue);
        if (QTest::currentTestFailed())
            return;
        // All: nothing can be both.
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("typeModeAnd"));
        applyAndExpect("type: duetoday AND overdue", SL{});
        if (QTest::currentTestFailed())
            return;
        // Back to Any, saved as a Smart List: the saved search keeps both.
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("typeModeOr"));
        const qint64 id = saveFromBuilder(app, "Today and overdue");
        QVERIFY(id);
        SMART_SHOWS(app, DueToday + Overdue);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, (DueToday + Overdue).size(), 5000);
    }

    // "To require high priority as well, edit the search field to read: type:
    // (duetoday OR overdue) AND hiprio. The parentheses group the two date
    // choices… Adding high priority as a third rule in the same Any group would
    // also include high-priority tasks due on other days."
    void combineTodayAndOverdueWithPriority() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        SMART_QUERY(app, "type: duetoday OR overdue", DueToday + Overdue);
        SMART_QUERY(app, "type: (duetoday OR overdue) AND hiprio", SL{"Rent overdue"});
        // The third rule inside the same Any group widens it instead.
        SMART_QUERY(app, "type: duetoday OR overdue OR hiprio", (DueToday + Overdue + SL{"Fence in 6 days", "Starred urgent", "Launch project"}));
        // Saved from the bar and edited, as the manual says.
        const qint64 id = saveSearchAsSmart(app, "type: duetoday OR overdue");
        QVERIFY(id);
        LIST_ITEM(rule, findItem("smartQueryField"));
        click(rule);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("type: (duetoday OR overdue) AND hiprio");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).smartQuery, QString("type: (duetoday OR overdue) AND hiprio"), 5000);
        SMART_SHOWS(app, SL{"Rent overdue"});
    }

    // "A separate date range in Search Options still narrows the results, even
    // when the rules use Any. If the range is limited to today, older overdue
    // tasks remain excluded. Click the active date-filter control again to turn
    // it off."
    void dateRangeStillNarrowsAnyRules() {
        App &app = *m_app;
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(pressBuilder("typeChip:duetoday"));
        QVERIFY(pressBuilder("typeChip:overdue"));
        QVERIFY(pressBuilder("rangeNext"));
        QVERIFY(fillBuilder(app, "rangeN", "1"));
        const qint64 id = saveFromBuilder(app, "Narrowed");
        QVERIFY(id);
        QCOMPARE(app.store().list(id).smartRange, QString("next:1:d"));
        // Overdue tasks fall outside "today only".
        SMART_SHOWS(app, specTitles([](const Spec &s) { return shownInAll(s) && s.due == 0; }));
        QVERIFY(!paneTitles(app).contains("Rent overdue"));
        // Switch the range off and update: the overdue ones return.
        QVERIFY(listMenu(app, id, "Edit Search…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QVERIFY(pressBuilder("rangeNone"));
        QVERIFY(pressBuilder("updateButton"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).smartRange, QString(), 5000);
        SMART_SHOWS(app, DueToday + Overdue);
    }

    // The manual's rule builder has a row per clause (Property / is / value,
    // with all-or-any at the top and + to add). Precrime's sheet is chip-based.
    void ruleRowsWithPropertyOperatorValue() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QEXPECT_FAIL("", "GAP: no row-per-clause builder (Property, is / is not, value; [+] to add); the sheet uses chips instead", Continue);
        QVERIFY(findItem("ruleAdd") != nullptr);
        QVERIFY(closeBuilder(*m_app));
    }
};

REGISTER_TEST(E2ESmartBuilder)

#include "test_smart_builder.moc"
