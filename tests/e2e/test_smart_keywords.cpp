// End-to-end scenarios for the named keywords of the search language
// (smart-lists.md): tags:, taggroups:, locations:, groups:, lists:,
// modified:, created:, completed:, and how sections combine. Each query is
// typed into the toolbar's search field over the fixture world and the exact
// rows the pane lists are checked.

#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

const SL Tagged{"Rent overdue", "Invoice overdue", "Milk today", "Parcel tomorrow", "Call mum", "Email boss",
                "Fix plumber leak", "Plumber for garden", "Plumber for office", "Blocked work", "Play tagged work",
                "Call work", "Someday idea", "Deep focus"};
const SL WorkTagged{"Email boss", "Blocked work", "Play tagged work", "Call work"};
const SL Contexts{"Milk today", "Parcel tomorrow", "Call mum", "Email boss", "Blocked work", "Play tagged work", "Call work"};
const SL Located{"Milk today", "Parcel tomorrow", "Visit shop", "Office desk", "Office and playground"};
const SL Plumbers{"Fix plumber leak", "Plumber quote", "Plumber for garden", "Plumber for office"};
const SL Hi{"Rent overdue", "Fence in 6 days", "Starred urgent", "Launch project"};
const SL Starred{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote", "Plumber for garden", "Plumber for office"};

// What the pane shows (open tasks) in lists of this name.
SL inLists(const SL &lists) {
    return specTitles([&](const Spec &s) { return shownInAll(s) && lists.contains(s.list); });
}

const SL HomeGroup{"Home", "Garden"};
const SL ProjectsGroup{"Work", "Website"};

}  // namespace

class E2ESmartKeywords : public QObject {
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
        if (m_app) {
            hideScheduled(*m_app, false);
            if (m_app->backend().sourceKey() != "all") {
                escapeSearch(*m_app);
                m_app->openSource("all");
            }
        }
    }

    // "tags: home, work  # implicit OR"; "tags: (home OR work) AND !play";
    // "tags: ?  # any tag set"; "tags: ~  # no tag set"; "tags: !work  # has a
    // tag, but not work"; "tags: ~ OR !work  # not tagged work, including
    // untagged tasks" (smart-lists.md, Filter by Tag).
    void tagsKeyword_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("one tag") << "tags: urgent" << SL{"Rent overdue"};
        QTest::newRow("another tag") << "tags: waiting" << SL{"Invoice overdue"};
        QTest::newRow("comma is or") << "tags: home, errands" << SL{"Milk today", "Parcel tomorrow"};
        QTest::newRow("explicit or") << "tags: home OR errands" << SL{"Milk today", "Parcel tomorrow"};
        QTest::newRow("three tags") << "tags: home, errands, urgent" << SL{"Milk today", "Parcel tomorrow", "Rent overdue"};
        QTest::newRow("shared tag") << "tags: work" << WorkTagged;
        QTest::newRow("and not") << "tags: work AND !blocked" << SL{"Email boss", "Play tagged work", "Call work"};
        QTest::newRow("brackets") << "tags: (home OR work) AND !play"
                                  << SL{"Milk today", "Email boss", "Blocked work", "Call work"};
        QTest::newRow("two needed") << "tags: (call OR mail) AND work" << SL{"Call work", "Email boss"};
        QTest::newRow("any tag") << "tags: ?" << Tagged;
        QTest::newRow("no tag") << "tags: ~" << without(allShown(), Tagged);
        QTest::newRow("tagged but not work") << "tags: !work" << without(Tagged, WorkTagged);
        QTest::newRow("not work, untagged too") << "tags: ~ OR !work" << without(allShown(), WorkTagged);
        QTest::newRow("tag with a space") << "tags: deep work" << SL{"Deep focus"};
        QTest::newRow("quoted tag") << "tags: \"deep work\"" << SL{"Deep focus"};
        QTest::newRow("case is ignored") << "tags: URGENT" << SL{"Rent overdue"};
        QTest::newRow("wildcard") << "tags: ur*" << SL{"Rent overdue"};
        QTest::newRow("unknown tag") << "tags: nosuchtag" << SL{};
        QTest::newRow("paused tag stays hidden") << "tags: onhold" << SL{};
    }
    void tagsKeyword() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // taggroups: matches the tags of the named groups. Like a tags: exclusion,
    // excluding a group (taggroups: !Someday) only matches tasks with at least
    // one tag.
    void tagGroupsKeyword_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("a group") << "taggroups: Contexts" << Contexts;
        QTest::newRow("lowercase") << "taggroups: contexts" << Contexts;
        QTest::newRow("other group") << "taggroups: Someday" << SL{"Someday idea"};
        QTest::newRow("two groups") << "taggroups: Contexts, Someday" << (Contexts + SL{"Someday idea"});
        QTest::newRow("exclude a group") << "taggroups: !Someday" << without(Tagged, SL{"Someday idea"});
        QTest::newRow("exclude contexts") << "taggroups: !Contexts" << without(Tagged, Contexts);
        QTest::newRow("group minus a tag") << "taggroups: Contexts tags: !phone" << without(Contexts, SL{"Call mum"});
        QTest::newRow("unknown group") << "taggroups: Nothing" << SL{};
    }
    void tagGroupsKeyword() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // "locations: home, drycleaners  # = locations: home OR drycleaners";
    // "locations: (home OR work) AND !playground"; "? / ~"; "locations: !home
    // only matches tasks that have a location set" (Filter by Location).
    void locationsKeyword_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("one") << "locations: home" << SL{"Milk today"};
        QTest::newRow("comma is or") << "locations: home, drycleaners" << SL{"Milk today", "Parcel tomorrow", "Visit shop"};
        QTest::newRow("brackets") << "locations: (home OR office) AND !playground" << SL{"Milk today", "Office desk"};
        QTest::newRow("both needed") << "locations: office AND playground" << SL{"Office and playground"};
        QTest::newRow("any") << "locations: ?" << Located;
        QTest::newRow("none") << "locations: ~" << without(allShown(), Located);
        QTest::newRow("not home") << "locations: !home" << SL{"Parcel tomorrow", "Visit shop", "Office desk", "Office and playground"};
        QTest::newRow("not home, unlocated too") << "locations: ~ OR !home" << without(allShown(), SL{"Milk today"});
        QTest::newRow("case") << "locations: PLAYGROUND" << SL{"Office and playground"};
        QTest::newRow("unknown") << "locations: moon" << SL{};
    }
    void locationsKeyword() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // groups: takes one or more list groups, and !name excludes a group.
    void groupsKeyword_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("one group") << "groups: projects" << inLists(ProjectsGroup);
        QTest::newRow("capitalised") << "groups: Projects" << inLists(ProjectsGroup);
        QTest::newRow("either group") << "groups: home, projects" << inLists(HomeGroup + ProjectsGroup);
        QTest::newRow("exclude") << "groups: !home" << inLists(ProjectsGroup + SL{"Archive", "Inbox"});
        QTest::newRow("group and priority") << "groups: Projects type: hiprio" << SL{"Starred urgent", "Launch project"};
        QTest::newRow("unknown group") << "groups: nothing" << SL{};
    }
    void groupsKeyword() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // lists: takes one or more lists, and !name excludes a list.
    void listsKeyword_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("two lists") << "lists: Work, Home" << inLists(SL{"Work", "Home"});
        QTest::newRow("one list") << "lists: Work" << inLists(SL{"Work"});
        QTest::newRow("case") << "lists: work" << inLists(SL{"Work"});
        QTest::newRow("quoted") << "lists: \"Work\"" << inLists(SL{"Work"});
        QTest::newRow("exclude") << "lists: !Archive" << inLists(SL{"Home", "Garden", "Work", "Website", "Inbox"});
        QTest::newRow("inbox") << "lists: Inbox" << SL{"Starred plain", "Inbox note"};
        QTest::newRow("archive keeps its open task") << "lists: Archive" << SL{"Archive item"};
        QTest::newRow("lists and due") << "lists: Work, Home type: duetoday"
                                       << SL{"Milk today", "Dentist today", "Two day window", "Fresh window", "Daily repeat",
                                             "Repeating window"};
        QTest::newRow("unknown list") << "lists: Nowhere" << SL{};
    }
    void listsKeyword() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // lists:, groups: and completed: replace the scope: a resolved filter
    // searches every list, whatever list is selected. The other keywords stay
    // inside the list being searched.
    void listsGroupsCompletedReplaceScope() {
        App &app = *m_app;
        QVERIFY(pane(app, lk(listId("Garden"))));
        SMART_QUERY(app, "type: hiprio", SL{"Fence in 6 days"});
        SMART_QUERY(app, "tags: urgent", SL{});
        SMART_QUERY(app, "lists: Work", inLists(SL{"Work"}));
        SMART_QUERY(app, "groups: projects", inLists(ProjectsGroup));
        SMART_QUERY(app, "completed: 7d", (SL{"Done just now", "Launch step done", "Done yesterday", "Finished step", "Done 3 days ago"}));
        QVERIFY(escapeSearch(app));
        // Reversed: with the search gone the list is back to its own tasks.
        SMART_SHOWS(app, inLists(SL{"Garden"}));
    }

    // created: and modified: take a relative time: a number and a unit (mins,
    // h, d, w, m, y), long spellings (3 days, 2 weeks, 1 month) and fractions
    // (1.5h, 0.5d). A bare value or a < prefix means within the last such span;
    // a > prefix means longer ago than that.
    void createdAndModified_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        const SL freshTwo{"Fresh window", "Fresh note"};
        const SL recent = freshTwo + SL{"Made 3 days ago"};
        QTest::newRow("created days") << "created: 2d" << freshTwo;
        QTest::newRow("created hours") << "created: 3h" << freshTwo;
        QTest::newRow("created mins") << "created: 90mins" << SL{"Fresh note"};
        QTest::newRow("created fractional hours") << "created: 1.5h" << SL{"Fresh note"};
        QTest::newRow("created fractional days") << "created: 0.5d" << freshTwo;
        QTest::newRow("created long spelling") << "created: 4 days" << recent;
        QTest::newRow("created less than a week") << "created: <1w" << recent;
        QTest::newRow("created weeks") << "created: 2 weeks" << recent;
        QTest::newRow("created a month") << "created: 1m" << (recent + SL{"Edited lately"});
        QTest::newRow("created one month long") << "created: 1 month" << (recent + SL{"Edited lately"});
        QTest::newRow("created a year") << "created: 1y" << allShown();
        QTest::newRow("created older than two weeks") << "created: >14d" << without(allShown(), recent);
        QTest::newRow("created older, weeks") << "created: >2w" << without(allShown(), recent);
        QTest::newRow("modified days") << "modified: 2d" << (freshTwo + SL{"Edited lately"});
        QTest::newRow("modified hours") << "modified: 6h" << (freshTwo + SL{"Edited lately"});
        QTest::newRow("modified narrower") << "modified: 3h" << freshTwo;
        QTest::newRow("modified long hours") << "modified: 4 hours" << freshTwo;
        QTest::newRow("modified less than a week") << "modified: <1w" << (recent + SL{"Edited lately"});
        QTest::newRow("modified older") << "modified: >2d" << without(allShown(), freshTwo + SL{"Edited lately"});
    }
    void createdAndModified() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // completed: widens the search to completed tasks and to every list, so
    // nothing has to be unhidden first.
    void completedKeyword_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        const SL today{"Done just now", "Launch step done"};
        const SL yesterday{"Done yesterday", "Finished step"};
        QTest::newRow("last two hours") << "completed: 2h" << today;
        QTest::newRow("last two days") << "completed: 2d" << (today + yesterday);
        QTest::newRow("last week") << "completed: 7d" << (today + yesterday + SL{"Done 3 days ago"});
        QTest::newRow("last year") << "completed: 1y" << (today + yesterday + SL{"Done 3 days ago", "Done 40 days ago"});
        QTest::newRow("older than a month") << "completed: >30d" << SL{"Done 40 days ago"};
        QTest::newRow("older than two days") << "completed: >2d" << SL{"Done 3 days ago", "Done 40 days ago"};
        QTest::newRow("less than") << "completed: <3h" << today;
    }
    void completedKeyword() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // Keywords are ANDed together: in "type: overdue tags: home OR school",
    // the OR stays inside tags: and does not spill into type:.
    void keywordsAreAnded_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("or stays inside tags") << "type: overdue tags: urgent OR waiting"
                                              << SL{"Rent overdue", "Invoice overdue"};
        QTest::newRow("or does not spill") << "type: duetoday tags: home OR errands" << SL{"Milk today"};
        QTest::newRow("and of two sections") << "type: overdue tags: urgent" << SL{"Rent overdue"};
        QTest::newRow("empty intersection") << "type: overdue tags: errands" << SL{};
        QTest::newRow("any order") << "tags: errands type: tomorrow" << SL{"Parcel tomorrow"};
        QTest::newRow("three sections") << "tags: urgent type: prio OR starred groups: home" << SL{"Rent overdue"};
        QTest::newRow("or inside type, and across") << "type: hiprio OR medprio lists: Garden"
                                                    << SL{"Fence in 6 days", "Passport in 14 days", "Medium plain"};
        QTest::newRow("manual's mixed example")
            << "plumber type: starred tags: builders groups: home" << SL{"Fix plumber leak", "Plumber for garden"};
        QTest::newRow("negations across sections")
            << "tags: !work type: (hiprio AND thisweek) OR starred groups: !work"
            << (SL{"Fix plumber leak", "Plumber for garden", "Plumber for office"}
                + (inThisWeek(-3) ? SL{"Rent overdue"} : SL{}));
    }
    void keywordsAreAnded() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // A standalone OR before a keyword makes that section an alternative:
    // "plumber type: hiprio OR tags: urgent OR completed: 1d" gives plumber text
    // with high priority, or urgent-tagged tasks, or tasks completed within a day.
    void orBranches_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("two branches") << "type: overdue OR tags: urgent" << SL{"Rent overdue", "Invoice overdue"};
        QTest::newRow("starred or urgent") << "type: starred OR tags: urgent" << (Starred + SL{"Rent overdue"});
        QTest::newRow("branch unlike the first") << "tags: waiting OR type: hiprio" << (Hi + SL{"Invoice overdue"});
        QTest::newRow("manual's three branches")
            << "plumber type: hiprio OR tags: urgent OR completed: 2h" << SL{"Rent overdue", "Done just now", "Launch step done"};
        QTest::newRow("text joins the and group") << "plumber OR type: hiprio" << (Plumbers + Hi);
        QTest::newRow("group is anded before branching") << "type: starred tags: builders OR type: hiprio"
                                                         << (SL{"Fix plumber leak", "Plumber for garden", "Plumber for office"} + Hi);
        QTest::newRow("leading or is not a branch") << "OR type: hiprio" << Hi;
        QTest::newRow("or inside one section stays local") << "type: hiprio OR medprio" << (Hi + SL{"Milk today", "Passport in 14 days",
                                                                                                     "Shopping checklist", "Medium plain"});
    }
    void orBranches() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // "tags: ... AND/OR with brackets" across whole sections:
    // "(tags: a OR tags: b)" groups whole filters.
    void groupedFilters() {
        App &app = *m_app;
        SMART_QUERY(app, "(tags: urgent OR tags: waiting)", (SL{"Rent overdue", "Invoice overdue"}));
        SMART_QUERY(app, "(tags: urgent OR tags: waiting) type: overdue", (SL{"Rent overdue", "Invoice overdue"}));
        SMART_QUERY(app, "!(tags: urgent OR tags: waiting) type: overdue", SL{});
        SMART_QUERY(app, "type: hiprio !(tags: urgent)", (SL{"Fence in 6 days", "Starred urgent", "Launch project"}));
    }

    // "Real power comes from combining keywords. They can appear in any
    // order": the same keywords in two orders give the same rows.
    void keywordOrderDoesNotMatter() {
        App &app = *m_app;
        const SL expected{"Fix plumber leak", "Plumber for garden"};
        SMART_QUERY(app, "plumber type: starred tags: builders groups: home", expected);
        SMART_QUERY(app, "plumber groups: home tags: builders type: starred", expected);
        SMART_QUERY(app, "plumber tags: builders groups: home type: starred", expected);
    }
};

REGISTER_TEST(E2ESmartKeywords)

#include "test_smart_keywords.moc"
