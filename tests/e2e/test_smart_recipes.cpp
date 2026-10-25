// End-to-end scenarios for the manual's Recipe Library (smart-lists.md): each
// recipe is typed into the Global Search Bar over All, confirmed to return what
// the fixture says it should, and saved as a real Smart List with the save
// button; the "Save-from-menu" recipes are built with the Search Options sheet.
// Each saved list shows in the sidebar with the right count and opens with the
// same rows.

#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

const SL Hi{"Rent overdue", "Fence in 6 days", "Starred urgent", "Launch project"};
const SL Med{"Milk today", "Passport in 14 days", "Shopping checklist", "Medium plain"};
const SL Low{"Dentist today", "Weekly repeat", "Low plain"};
const SL Tagged{"Rent overdue", "Invoice overdue", "Milk today", "Parcel tomorrow", "Call mum", "Email boss",
                "Fix plumber leak", "Plumber for garden", "Plumber for office", "Blocked work", "Play tagged work",
                "Call work", "Someday idea", "Deep focus"};
const SL Contexts{"Milk today", "Parcel tomorrow", "Call mum", "Email boss", "Blocked work", "Play tagged work", "Call work"};
const SL Located{"Milk today", "Parcel tomorrow", "Visit shop", "Office desk", "Office and playground"};
const SL DueToday{"Milk today", "Dentist today", "Two day window", "Fresh window", "Daily repeat", "Repeating window"};
const SL DueTomorrow{"Parcel tomorrow", "Launch step one", "Archive item", "Repeating window"};
const SL Overdue{"Rent overdue", "Invoice overdue"};
const SL Starred{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote", "Plumber for garden", "Plumber for office"};
const SL Repeating{"Weekly repeat", "Daily repeat", "Repeating window"};
const SL Dated{"Rent overdue", "Invoice overdue", "Milk today", "Dentist today", "Parcel tomorrow", "Report in 3 days",
               "Fence in 6 days", "Taxes in 9 days", "Passport in 14 days", "Began yesterday due in 2", "Two day window",
               "Fresh window", "Repeating window", "Starred urgent", "Weekly repeat", "Daily repeat", "Launch step one",
               "Launch step later", "Archive item"};

SL inLists(const SL &lists) {
    return specTitles([&](const Spec &s) { return shownInAll(s) && lists.contains(s.list); });
}

bool dueWindowInWeek(const Spec &s) {
    if (s.due == NoDay)
        return false;
    const int lo = -(QDate::currentDate().dayOfWeek() - 1), hi = lo + 6;
    return s.due - s.duration / 1440 <= hi && s.due >= lo;
}

}  // namespace

class E2ESmartRecipes : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;

private slots:
    void initTestCase() {
        m_app = std::make_unique<App>(seedSmart);
        QVERIFY(m_app->ok);
    }
    void cleanupTestCase() { m_app.reset(); }
    void cleanup() {
        if (!m_app)
            return;
        // Smart Lists made by a recipe go again, so each row starts from the
        // same sidebar.
        for (const TaskList &l : smartLists(*m_app))
            m_app->backend().deleteList(l.id);
        closeBuilder(*m_app);
        escapeSearch(*m_app);
    }

    // Every query recipe: confirm in the search bar, then save with the button.
    void queryRecipes_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        // Priority.
        QTest::newRow("Anything high priority") << "type: hiprio" << Hi;
        QTest::newRow("High or medium priority") << "type: hiprio OR medprio" << (Hi + Med);
        QTest::newRow("High priority, due this week") << "type: hiprio AND duethisweek"
                                                      << specTitles([](const Spec &s) { return shownInAll(s) && s.prio == 3 && dueWindowInWeek(s); });
        QTest::newRow("Prioritized but undated") << "type: prio AND nodue"
                                                 << SL{"Launch project", "Shopping checklist", "Low plain", "Medium plain"};
        QTest::newRow("Needs a priority") << "type: noprio AND !repeat"
                                          << without(without(allShown(), Hi + Med + Low), SL{"Daily repeat", "Repeating window"});
        // Dates and scheduling.
        QTest::newRow("Due today") << "type: duetoday" << DueToday;
        QTest::newRow("Anything happening today") << "type: anytoday" << (DueToday + SL{"Starts today"});
        QTest::newRow("Due tomorrow") << "type: duetomorrow" << DueTomorrow;
        QTest::newRow("Due this week") << "type: duethisweek"
                                       << specTitles([](const Spec &s) { return shownInAll(s) && dueWindowInWeek(s); });
        QTest::newRow("Everything this week") << "type: anythisweek"
                                              << specTitles([](const Spec &s) {
                                                     return shownInAll(s) && (dueWindowInWeek(s) || (s.start != NoDay && inThisWeek(s.start)));
                                                 });
        QTest::newRow("Overdue") << "type: overdue" << Overdue;
        QTest::newRow("Overdue or due today") << "type: overdue OR duetoday" << (Overdue + DueToday);
        QTest::newRow("Starts in the future") << "type: scheduled" << SL{"Starts tomorrow", "Starts in 3 due in 5"};
        QTest::newRow("In progress right now") << "type: inprogress"
                                               << SL{"Two day window", "Fresh window", "Repeating window", "Began yesterday due in 2"};
        QTest::newRow("No due date at all") << "type: nodue" << without(allShown(), Dated);
        // Tags and locations.
        QTest::newRow("Tagged waiting") << "tags: waiting" << SL{"Invoice overdue"};
        QTest::newRow("Not tagged waiting, untagged too") << "tags: ~ OR !waiting" << without(allShown(), SL{"Invoice overdue"});
        QTest::newRow("Has a tag, but not waiting") << "tags: !waiting" << without(Tagged, SL{"Invoice overdue"});
        QTest::newRow("Tagged home or errands") << "tags: home, errands" << SL{"Milk today", "Parcel tomorrow"};
        QTest::newRow("Work tag minus blocked tag") << "tags: work AND !blocked" << SL{"Email boss", "Play tagged work", "Call work"};
        QTest::newRow("Contexts group minus phone") << "taggroups: Contexts tags: !phone" << without(Contexts, SL{"Call mum"});
        QTest::newRow("Has at least one tag") << "type: anytag" << Tagged;
        QTest::newRow("Completely untagged") << "type: notag" << without(allShown(), Tagged);
        QTest::newRow("Has a location") << "type: anylocation" << Located;
        QTest::newRow("Near a map location, via Visit") << "type: anylocation AND visit" << SL{"Visit shop"};
        // Lists and groups.
        QTest::newRow("Work or Home lists") << "lists: Work, Home" << inLists(SL{"Work", "Home"});
        QTest::newRow("Work and Home, due today") << "lists: Work, Home type: duetoday" << DueToday;
        QTest::newRow("Everything except Archive") << "lists: !Archive" << inLists(SL{"Home", "Garden", "Work", "Website", "Inbox"});
        QTest::newRow("Everything inside Projects group") << "groups: Projects" << inLists(SL{"Work", "Website"});
        QTest::newRow("Projects group, high priority") << "groups: Projects type: hiprio" << SL{"Starred urgent", "Launch project"};
        // Status and structure.
        QTest::newRow("Starred") << "type: starred" << Starred;
        QTest::newRow("Recurring tasks") << "type: repeat" << Repeating;
        QTest::newRow("Projects only") << "type: proj" << SL{"Launch project", "Empty project", "Finished project"};
        QTest::newRow("Empty projects") << "type: emptyproj" << SL{"Empty project", "Finished project"};
        QTest::newRow("Projects or checklists, high priority") << "type: (proj OR chck) AND hiprio" << SL{"Launch project"};
        QTest::newRow("Has an attachment") << "type: (audio OR pict)" << SL{"Voice memo job", "Picture job"};
        QTest::newRow("Captured from email, this month") << "type: capturedmail created: 30d" << SL{};
        QTest::newRow("Stalled") << "type: inprogress AND !repeat created: >14d" << SL{"Two day window", "Began yesterday due in 2"};
        QTest::newRow("Waiting, or anything urgent") << "tags: waiting OR type: hiprio" << (Hi + SL{"Invoice overdue"});
        QTest::newRow("Recently created") << "created: 2d" << SL{"Fresh window", "Fresh note"};
        QTest::newRow("Recently completed") << "completed: 7d"
                                            << SL{"Done just now", "Launch step done", "Done yesterday", "Finished step", "Done 3 days ago"};
        // Combine Today and Overdue.
        QTest::newRow("Today and overdue") << "type: duetoday OR overdue" << (DueToday + Overdue);
        QTest::newRow("Today and overdue, high priority") << "type: (duetoday OR overdue) AND hiprio" << SL{"Rent overdue"};
    }
    void queryRecipes() {
        App &app = *m_app;
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        QVERIFY(pane(app, "all"));
        // Typed into the search bar first; the button saves what is there.
        QVERIFY(typeSearch(app, query));
        const qint64 id = saveSearchAsSmart(app, query, true);
        QVERIFY2(id, qPrintable(query));
        QCOMPARE(app.store().list(id).smartQuery, query);
        QCOMPARE(app.store().list(id).kind, int(ListKind::Smart));
        QCOMPARE(app.backend().search(), QString());
        SMART_SHOWS(app, expected);
        QTRY_COMPARE_WITH_TIMEOUT(smartRowNames(app), (SL{query}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, expected.size(), 5000);
    }

    // "Save-from-menu recipes": Someday (Without Due Date), Next 3 Days
    // (Dynamic Range 3 days), Quick Wins (`type: duration`) and Review Inbox
    // (`lists: Inbox`), built with the Search Options sheet and saved.
    void menuRecipeSomeday() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("rangeNoDue"));
        const qint64 id = saveFromBuilder(app, "Someday");
        QVERIFY(id);
        QCOMPARE(app.store().list(id).smartRange, QString("nodue"));
        SMART_SHOWS(app, without(allShown(), Dated));
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, without(allShown(), Dated).size(), 5000);
    }

    void menuRecipeNext3Days() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("rangeNext"));
        QVERIFY(fillBuilder(app, "rangeN", "3"));
        const qint64 id = saveFromBuilder(app, "Next 3 Days");
        QVERIFY(id);
        QCOMPARE(app.store().list(id).smartRange, QString("next:3:d"));
        const SL expected = specTitles([](const Spec &s) { return shownInAll(s) && s.due != NoDay && s.due >= 0 && s.due < 3; });
        SMART_SHOWS(app, expected);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, expected.size(), 5000);
    }

    void menuRecipeQuickWins() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(addRuleRow("Duration", "has") == "0");
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: duration"), 5000);
        const qint64 id = saveFromBuilder(app, "Quick Wins");
        QVERIFY(id);
        const SL expected{"Quick win", "Two day window", "Fresh window", "Repeating window"};
        SMART_SHOWS(app, expected);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, expected.size(), 5000);
    }

    void menuRecipeReviewInbox() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        QVERIFY(openBuilder(app));
        QVERIFY(pressBuilder("pickerTab:lists"));
        QVERIFY(pressBuilder("nameChip:lists:Inbox"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("lists: Inbox"), 5000);
        const qint64 id = saveFromBuilder(app, "Review Inbox");
        QVERIFY(id);
        // "Completed tasks are left out by default": an Inbox task completed
        // meanwhile does not show.
        const SL expected{"Starred plain", "Inbox note"};
        SMART_SHOWS(app, expected);
        QVERIFY(selectRow(app, "Inbox note"));
        app.press(Qt::Key_Period, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Inbox note").completed, 5000);
        // The count drops at once; the row stays until the view is left.
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, 1, 5000);
        SMART_SHOWS(app, expected);
        QVERIFY(pane(app, "all"));
        QVERIFY(pane(app, lk(id)));
        SMART_SHOWS(app, SL{"Starred plain"});
        app.press(Qt::Key_Z, Qt::ControlModifier);
        SMART_SHOWS(app, expected);
    }
};

REGISTER_TEST(E2ESmartRecipes)

#include "test_smart_recipes.moc"
