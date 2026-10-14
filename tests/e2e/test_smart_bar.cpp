// End-to-end scenarios for the Global Search Bar (search-and-jump-bars.md,
// smart-lists.md): typing a query filters the pane, Escape and the ✕ clear it,
// the presets menu, searching in a list versus in All, and Inherit Search in
// Project View.

#include "settingskit.h"
#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

const SL Plumbers{"Fix plumber leak", "Plumber quote", "Plumber for garden", "Plumber for office"};
const SL Hi{"Rent overdue", "Fence in 6 days", "Starred urgent", "Launch project"};
const SL DueToday{"Milk today", "Dentist today", "Two day window", "Fresh window", "Daily repeat", "Repeating window"};

SL projectView(App &app) { return paneTitles(app, true); }

// Opens the project view of the task shown under this title, with the
// shortcut for it, from whatever list or search is open.
bool openProject(App &app, const QString &title) {
    if (!selectRow(app, title))
        return false;
    app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    return waitUntil([&] { return app.backend().sourceKey() == QString("project:%1").arg(idOf(title)); });
}

void setInherit(App &app, bool on) {
    QQuickWindow *sw = nullptr;
    openSettings(app, sw);
    selectPane(sw, "Today & Lists");
    setCheck("setting:search.inheritInProject", on);
    QTRY_COMPARE_WITH_TIMEOUT(app.store().setting("search.inheritInProject", false).toBool(), on, 5000);
    closeSettings(app);
}

}  // namespace

class E2ESmartBar : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;

private slots:
    void initTestCase() {
        m_app = std::make_unique<App>(seedSmart);
        QVERIFY(m_app->ok);
        QVERIFY(waitUntil([&] { return m_app->openSource("all"); }));
    }
    void cleanupTestCase() { m_app.reset(); }
    void cleanup() {
        if (!m_app)
            return;
        App &app = *m_app;
        closeMenus(app);
        closeBuilder(app);
        escapeSearch(app);
        showScheduled(app, false);
        app.store().setSetting("search.inheritInProject", false);
        if (app.backend().searchAllLists())
            app.backend().setSearchAllLists(false);
        app.openSource("all");
    }

    // "⌘F: open the Global Search Bar. Clicking the magnifying glass expands
    // and collapses it too." "Filter the task list by Smart Filter syntax."
    // Typing filters; Escape clears; the ✕ clears; the field collapses.
    void typingFiltersEscapeAndCrossClear() {
        App &app = *m_app;
        QVERIFY(!searchOpen());
        SMART_SHOWS(app, allShown());
        // Real key presses for this one.
        QVERIFY(typeSearch(app, "plumber", true));
        SMART_SHOWS(app, Plumbers);
        QCOMPARE(app.backend().search(), QString("plumber"));
        QVERIFY(searchOpen());
        QVERIFY(findItem("searchClear"));
        // Narrowing as the query grows.
        SMART_QUERY(app, "plumber garden", SL{"Plumber for garden"});
        SMART_QUERY(app, "plumber", Plumbers);
        // Escape clears the search, collapses the field and restores the rows.
        app.press(Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().search(), QString(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!searchOpen(), 5000);
        SMART_SHOWS(app, allShown());
        // The ✕ does the same.
        SMART_QUERY(app, "plumber", Plumbers);
        LIST_ITEM(cross, findItem("searchClear"));
        clickSteady(cross);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().search(), QString(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!searchOpen(), 5000);
        QVERIFY(!findItem("searchClear"));
        SMART_SHOWS(app, allShown());
        // Nothing found says so.
        SMART_QUERY(app, "zzzz", SL{});
        QVERIFY(findText(QString::fromUtf8("No tasks match \xe2\x80\x9czzzz\xe2\x80\x9d")));
    }

    // "The Global Search Bar lives in the toolbar… Click it (or press ⌘F)": a
    // click on the magnifying glass expands the field too, and it stays out
    // of the way when empty.
    void magnifierExpandsField() {
        App &app = *m_app;
        QVERIFY(!searchOpen());
        app.press(Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(searchOpen(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(searchField()->property("activeFocus").toBool(), 5000);
        // Escape puts it away again.
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!searchOpen(), 5000);
    }

    // "The scope control decides whether the search covers only the list
    // you're currently in or every list you have. ⌥⌘F (Toggle Search Scope)
    // flips it without opening the panel."
    void searchInListVersusAll() {
        App &app = *m_app;
        QVERIFY(pane(app, lk(listId("Garden"))));
        SMART_QUERY(app, "plumber", SL{"Plumber for garden"});
        // Everything in Garden, nothing from the other lists.
        QVERIFY(!app.backend().searchAllLists());
        app.press(Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().searchAllLists(), 5000);
        SMART_SHOWS(app, Plumbers);
        QCOMPARE(app.backend().sourceKey(), lk(listId("Garden")));
        app.press(Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().searchAllLists(), 5000);
        SMART_SHOWS(app, SL{"Plumber for garden"});
        // The same from the presets menu, in both directions.
        click(findItem("searchPresets"));
        QVERIFY(clickMenuEntry("Search All Lists"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().searchAllLists(), 5000);
        SMART_SHOWS(app, Plumbers);
        QVERIFY(settle());
        click(findItem("searchPresets"));
        QVERIFY(clickMenuEntry("Search This List Only"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().searchAllLists(), 5000);
        SMART_SHOWS(app, SL{"Plumber for garden"});
        // With no search the scope does not matter: the list shows its own tasks.
        QVERIFY(escapeSearch(app));
        SMART_SHOWS(app, specTitles([](const Spec &s) { return shownInAll(s) && s.list == "Garden"; }));
    }

    // "Search Options… a This List / All Lists scope control, and an Include
    // Scheduled checkbox."
    void searchOptionsScopeAndScheduled() {
        App &app = *m_app;
        QVERIFY(pane(app, lk(listId("Work"))));
        SMART_QUERY(app, "starts", SL{"Starts today"});
        app.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QVERIFY(pressBuilder("scopeAll"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().searchAllLists(), 5000);
        // "Starts in 3 due in 5" belongs to Website and is scheduled; "Starts
        // tomorrow" is scheduled too: hidden until Include is chosen.
        QTRY_COMPARE_WITH_TIMEOUT(paneTitles(app), (SL{"Starts today"}), 5000);
        QVERIFY(pressBuilder("searchScheduledInclude"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().showScheduled(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(paneTitles(app), sorted(SL{"Starts today", "Starts tomorrow", "Starts in 3 due in 5"}), 5000);
        QVERIFY(pressBuilder("searchScheduledHide"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().showScheduled(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(paneTitles(app), (SL{"Starts today"}), 5000);
        QVERIFY(pressBuilder("scopeThis"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().searchAllLists(), 5000);
        QVERIFY(closeBuilder(app));
    }

    // The presets menu: "The Presets menu inside the field is the quick one.
    // It drops ready-made queries into the field… Whatever it inserts is
    // ordinary search text, so you can edit it by hand afterwards."
    void presetsMenu_data() {
        QTest::addColumn<QString>("entry");
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("Overdue") << "Overdue" << "type: overdue" << SL{"Rent overdue", "Invoice overdue"};
        QTest::newRow("Due Today") << "Due Today" << "type: duetoday" << DueToday;
        QTest::newRow("Due This Week") << "Due This Week" << "type: duethisweek"
                                       << specTitles([](const Spec &s) {
                                              const int lo = -(QDate::currentDate().dayOfWeek() - 1);
                                              return shownInAll(s) && s.due != NoDay && s.due - s.duration / 1440 <= lo + 6 && s.due >= lo;
                                          });
        QTest::newRow("In Progress") << "In Progress" << "type: inprogress"
                                     << SL{"Two day window", "Fresh window", "Repeating window", "Began yesterday due in 2"};
        QTest::newRow("High Priority") << "High Priority" << "type: hiprio" << Hi;
        QTest::newRow("Starred") << "Starred" << "type: starred"
                                 << SL{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote", "Plumber for garden",
                                       "Plumber for office"};
        QTest::newRow("Without Due Date") << "Without Due Date" << "type: nodue"
                                          << specTitles([](const Spec &s) { return shownInAll(s) && s.due == NoDay; });
        QTest::newRow("Repeating") << "Repeating" << "type: repeat" << SL{"Weekly repeat", "Daily repeat", "Repeating window"};
        QTest::newRow("With Alerts") << "With Alerts" << "type: alarms" << SL{"Dentist today", "Alarm without date"};
        QTest::newRow("Empty Projects and Checklists") << "Empty Projects and Checklists" << "type: emptyproj OR emptychck"
                                                       << SL{"Empty project", "Finished project", "Empty checklist"};
        QTest::newRow("Recently Completed") << "Recently Completed" << "completed: 7d"
                                            << SL{"Done just now", "Launch step done", "Done yesterday", "Finished step",
                                                  "Done 3 days ago"};
    }
    void presetsMenu() {
        App &app = *m_app;
        QFETCH(QString, entry);
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        QVERIFY(settle());
        LIST_ITEM(button, findItem("searchPresets"));
        click(button);
        QVERIFY2(clickMenuEntry(entry), qPrintable(entry));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().search(), query, 5000);
        SMART_SHOWS(app, expected);
        // Ordinary text: editable by hand afterwards.
        LIST_ITEM(field, searchField());
        QVERIFY(field->property("text").toString() == query);
        QVERIFY(escapeSearch(app));
        SMART_SHOWS(app, allShown());
    }

    // "Inherit Search under Today & Lists… Off: Project View shows all of a
    // project's sub-tasks, ignoring the outside search. Your original search is
    // restored the moment you leave the project." Fairfax ships it off.
    void inheritSearchOffShowsEverySubtask() {
        App &app = *m_app;
        QCOMPARE(app.store().setting("search.inheritInProject", false).toBool(), false);
        SMART_QUERY(app, "type: hiprio OR tomorrow",
                    (Hi + SL{"Parcel tomorrow", "Launch step one", "Archive item", "Repeating window"}));
        QVERIFY(openProject(app, "Launch project"));
        SMART_SHOWS(app, SL{"Launch project"});
        QTRY_COMPARE_WITH_TIMEOUT(projectView(app), sorted(SL{"Launch project", "Launch step one", "Launch step later", "Launch step done"}), 5000);
        // Leaving the project brings the search back.
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(listId("Website")), 5000);
        QCOMPARE(app.backend().search(), QString("type: hiprio OR tomorrow"));
        QTRY_COMPARE_WITH_TIMEOUT(paneTitles(app), (SL{"Launch project"}), 5000);
    }

    // "On (default): opening a project keeps the Smart List's rule or your
    // current search applied to its sub-tasks, so you only see the sub-tasks
    // that match."
    void inheritSearchOnFiltersSubtasksOfSearch() {
        App &app = *m_app;
        setInherit(app, true);
        SMART_QUERY(app, "type: hiprio OR tomorrow",
                    (Hi + SL{"Parcel tomorrow", "Launch step one", "Archive item", "Repeating window"}));
        QVERIFY(openProject(app, "Launch project"));
        QTRY_COMPARE_WITH_TIMEOUT(projectView(app), sorted(SL{"Launch project", "Launch step one"}), 5000);
        QCOMPARE(app.backend().search(), QString("type: hiprio OR tomorrow"));
        // Back out: the full search result of the project's list returns.
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(paneTitles(app), (SL{"Launch project"}), 5000);
        // Reversed: switched off again, every sub-task shows.
        setInherit(app, false);
        QVERIFY(openProject(app, "Launch project"));
        QTRY_COMPARE_WITH_TIMEOUT(projectView(app), sorted(SL{"Launch project", "Launch step one", "Launch step later", "Launch step done"}), 5000);
    }

    // "A Smart List's rule counts as a search here even though you did not
    // type it, and that includes its date range. Sub-tasks in a project are
    // matched on their own dates, so a project opened from a Smart List such
    // as Next 5 Days shows only the sub-tasks dated within those days."
    void inheritSearchFromSmartList() {
        App &app = *m_app;
        // A Smart List whose rule the project and one of its sub-tasks meet.
        QVERIFY(typeSearch(app, "type: hiprio OR tomorrow"));
        // The button that has just appeared is clicked once it has stopped moving.
        const qint64 hot = saveSearchAsSmart(app, "type: hiprio OR tomorrow", true);
        QVERIFY(hot);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().sourceIsSmart(), 5000);
        QCOMPARE(app.backend().search(), QString());
        QTRY_VERIFY_WITH_TIMEOUT(app.row(idOf("Launch project")) != nullptr, 5000);

        setInherit(app, true);
        QVERIFY(openProject(app, "Launch project"));
        QTRY_COMPARE_WITH_TIMEOUT(projectView(app), sorted(SL{"Launch project", "Launch step one"}), 5000);
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(listId("Website")), 5000);

        // Off: the whole project, whatever the Smart List said.
        setInherit(app, false);
        QVERIFY(pane(app, lk(hot)));
        QVERIFY(openProject(app, "Launch project"));
        QTRY_COMPARE_WITH_TIMEOUT(projectView(app), sorted(SL{"Launch project", "Launch step one", "Launch step later", "Launch step done"}), 5000);
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().sourceKey() == lk(listId("Website")), 5000);
        app.backend().deleteList(hot);
    }

    // The date range of a Smart List is part of its rule: a project jumped to
    // (J) from "Next 3 days" shows only sub-tasks due within those days.
    void inheritSearchIncludesDateRange() {
        App &app = *m_app;
        setInherit(app, true);
        const qint64 soon = app.backend().saveSmartList("Soon", "lists: Website", "next:3:d");
        QVERIFY(soon);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().sourceKey() == lk(soon), 5000);
        // Only dated tasks of Website come through the range.
        QTRY_COMPARE_WITH_TIMEOUT(paneTitles(app), (SL{"Began yesterday due in 2", "Launch step one"}), 5000);
        // Jump to the project from the Smart List with the J bar.
        app.press(Qt::Key_J);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("commandInput") != nullptr, 5000);
        app.type("Launch project");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("project:%1").arg(idOf("Launch project")), 5000);
        // The project itself is undated, the range shows only its dated child
        // that falls inside it.
        QTRY_COMPARE_WITH_TIMEOUT(projectView(app), sorted(SL{"Launch project", "Launch step one"}), 5000);
        setInherit(app, false);
        QTRY_COMPARE_WITH_TIMEOUT(projectView(app), sorted(SL{"Launch project", "Launch step one", "Launch step later", "Launch step done"}), 5000);
        app.backend().deleteList(soon);
    }
};

REGISTER_TEST(E2ESmartBar)

#include "test_smart_bar.moc"
