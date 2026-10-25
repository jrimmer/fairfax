// End-to-end tests: sorting a list. The sort menu next to the list name, the
// reverse arrow, the Separated switch, one sort per list kept across a restart,
// manual order by dragging, project sort and the view-option buttons beside it
// (the clock for scheduled tasks and the Focus Filter's sun).

#include "misckit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::mk;

class E2EMiscSort : public QObject {
    Q_OBJECT

    // Opens the Sorting list the way a user does, by clicking it.
    static bool openSorting(App &app) { return app.openSource(lk(mw().sorting)); }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // Every option of the menu, ascending and descending,
    // with and without Separated, gives exactly one order of the five tasks.
    void everySortOrdersTheRows_data() {
        QTest::addColumn<int>("field");
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("ascSeparated");
        QTest::addColumn<QString>("descSeparated");
        QTest::addColumn<QString>("ascFlat");
        QTest::addColumn<QString>("descFlat");
        // A Alpha, B Bravo, C Charlie, D Delta, E Echo (see misckit.h).
        QTest::newRow("dueDate") << int(SortField::DueDate) << "Due Date" << "EBDAC" << "EADBC" << "EBDAC" << "CADBE";
        QTest::newRow("startDate") << int(SortField::StartDate) << "Start Date" << "EACDB" << "CAEDB" << "EACDB" << "BDCAE";
        QTest::newRow("priority") << int(SortField::Priority) << "Priority" << "BDEAC" << "CEADB" << "BDEAC" << "CEADB";
        QTest::newRow("list") << int(SortField::List) << "To-do List" << "BDEAC" << "BDEAC" << "BDEAC" << "BDEAC";
        QTest::newRow("title") << int(SortField::Title) << "Title" << "ABCDE" << "EDCBA" << "ABCDE" << "EDCBA";
        QTest::newRow("tag") << int(SortField::Tag) << "Tag" << "DBEAC" << "CABED" << "DBEAC" << "CABED";
        QTest::newRow("duration") << int(SortField::Duration) << "Duration" << "EBDAC" << "CADBE" << "CEBDA" << "ADBEC";
        QTest::newRow("created") << int(SortField::Creation) << "Creation" << "ACBDE" << "EDBCA" << "ACBDE" << "EDBCA";
        QTest::newRow("modified") << int(SortField::Modified) << "Modified" << "ACDBE" << "EBDCA" << "ACDBE" << "EBDCA";
        QTest::newRow("status") << int(SortField::Status) << "Status" << "EBDAC" << "CBDAE" << "BDEAC" << "BDEAC";
        QTest::newRow("note") << int(SortField::Note) << "Note" << "BDECA" << "ACEDB" << "BDECA" << "CEDBA";
        QTest::newRow("location") << int(SortField::Location) << "Location" << "CDEAB" << "BCDEA" << "CDEAB" << "AEDCB";
        QTest::newRow("manual") << int(SortField::Manual) << "Manually" << "CEADB" << "BDAEC" << "CEADB" << "BDAEC";
    }
    void everySortOrdersTheRows() {
        QFETCH(int, field);
        QFETCH(QString, label);
        QFETCH(QString, ascSeparated);
        QFETCH(QString, descSeparated);
        QFETCH(QString, ascFlat);
        QFETCH(QString, descFlat);
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 5000);
        QVERIFY(chooseSort(app, field));
        QVERIFY2(findItem("sortButton")->property("current").toString() == label, qPrintable(findItem("sortButton")->property("current").toString()));

        struct Combo { bool separated, ascending; QString want; };
        for (const Combo &c : {Combo{true, true, ascSeparated}, Combo{true, false, descSeparated},
                               Combo{false, false, descFlat}, Combo{false, true, ascFlat}}) {
            QVERIFY(setSeparated(app, c.separated));
            QVERIFY(setAscending(app, c.ascending));
            QTRY_COMPARE_WITH_TIMEOUT(orderNow(), c.want, 5000);
            // The choice is the list's own, saved with it.
            const TaskList saved = app.store().list(mw().sorting);
            QCOMPARE(saved.sortField, field);
            QCOMPARE(saved.sortAscending, c.ascending);
            QCOMPARE(saved.separated, c.separated);
        }
    }

    // The menu offers each sort, ticks the one in force, and the
    // Done list offers its own subset.
    void menuListsTheChoicesAndTicksTheCurrentOne() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(openSortMenu());
        const QStringList expected{"Due Date", "Start Date", "Priority", "To-do List", "Title", "Tag", "Duration",
                                   "Creation", "Modified", "Status", "Note", "Location", "Manually", "Separated"};
        QTRY_VERIFY2_WITH_TIMEOUT(sortMenuTexts() == expected, qPrintable(sortMenuTexts().join(" | ")), 5000);
        QVERIFY(sortMenuItem(SortField::DueDate)->property("checked").toBool());
        QVERIFY(!sortMenuItem(SortField::Title)->property("checked").toBool());
        QVERIFY(findItem("sortSeparated")->property("checked").toBool());
        closeMenus(app);

        QVERIFY(chooseSort(app, SortField::Title));
        QVERIFY(sortTicked(SortField::Title));
        QVERIFY(!sortTicked(SortField::DueDate));
        closeMenus(app);

        app.press(Qt::Key_5, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("done"), 5000);
        QVERIFY(openSortMenu());
        const QStringList doneChoices{"Priority", "To-do List", "Modified", "Completion", "Note", "Title", "Separated"};
        QTRY_VERIFY2_WITH_TIMEOUT(sortMenuTexts() == doneChoices, qPrintable(sortMenuTexts().join(" | ")), 5000);
        QVERIFY(sortMenuItem(SortField::Completion)->property("checked").toBool());
        closeMenus(app);
    }

    // A list with sections adds "Section" to the menu, first.
    void sectionSortAppearsWithSections() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(openSortMenu());
        QVERIFY(!sortMenuItem(SortField::Section));
        closeMenus(app);
        // New Section from the + button's menu, as a user adds one.
        QQuickItem *plus = findItem("newTaskButton");
        QVERIFY(plus);
        click(plus, Qt::RightButton);
        QVERIFY(clickMenuEntry("New Section"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().sectionsOf(mw().sorting).isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().sortField() == SortField::Section, 5000);
        QVERIFY(openSortMenu());
        QVERIFY(sortMenuItem(SortField::Section));
        QTRY_VERIFY2_WITH_TIMEOUT(sortMenuTexts().value(0) == "Section", qPrintable(sortMenuTexts().join(" | ")), 5000);
        closeMenus(app);
    }

    // "Separated" groups the list under headings; off, one heading holds all.
    void separatedGroupsUnderHeadings() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        const QDate today = QDate::currentDate();
        QTRY_VERIFY_WITH_TIMEOUT(headingTitles().size() == 5, 5000);
        QCOMPARE(headingTitles().first(), QString("OVERDUE"));
        QCOMPARE(headingTitles().last(), QString("NO DUE DATE"));

        QVERIFY(setSeparated(app, false));
        QTRY_COMPARE_WITH_TIMEOUT(headingTitles(), QStringList{"ALL TASKS IN SORTING"}, 5000);
        QCOMPARE(orderNow(), QString("EBDAC"));

        QVERIFY(setSeparated(app, true));
        QTRY_COMPARE_WITH_TIMEOUT(headingTitles().size(), 5, 5000);
        // By priority the headings are the priority names.
        QVERIFY(chooseSort(app, SortField::Priority));
        QTRY_COMPARE_WITH_TIMEOUT(headingTitles(), (QStringList{"HIGH", "MEDIUM", "LOW", "NONE"}), 5000);
        // By title there is nothing to group by.
        QVERIFY(chooseSort(app, SortField::Title));
        QTRY_COMPARE_WITH_TIMEOUT(headingTitles(), QStringList{"ALL TASKS IN SORTING"}, 5000);
        Q_UNUSED(today);
    }

    // The sort belongs to each list: Home keeps its own while Sorting changes,
    // and both come back after a restart, as does a sort chosen in All.
    void sortIsPerListAndSurvivesARestart() {
        Keep keep(seedMisc);
        QVERIFY(keep.start());
        {
            App &app = *keep;
            QVERIFY(openSorting(app));
            QVERIFY(chooseSort(app, SortField::Title));
            QVERIFY(setAscending(app, false));
            QVERIFY(setSeparated(app, false));
            QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EDCBA"), 5000);

            QVERIFY(app.openSource(lk(app.ids.home)));
            QCOMPARE(app.backend().sortField(), int(SortField::DueDate));   // Not Sorting's.
            QVERIFY(app.backend().sortAscending() && app.backend().separated());
            QVERIFY(chooseSort(app, SortField::Priority));
            QVERIFY(setAscending(app, false));

            app.press(Qt::Key_1, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("all"), 5000);
            QVERIFY(chooseSort(app, SortField::Title));
        }
        QVERIFY(keep.start());
        App &app = *keep;
        QVERIFY(openSorting(app));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EDCBA"), 5000);
        QCOMPARE(app.backend().sortField(), int(SortField::Title));
        QVERIFY(!app.backend().sortAscending() && !app.backend().separated());
        QCOMPARE(findItem("sortButton")->property("current").toString(), QString("Title"));
        QVERIFY(app.openSource(lk(app.ids.home)));
        QCOMPARE(app.backend().sortField(), int(SortField::Priority));
        QVERIFY(!app.backend().sortAscending());
        QVERIFY(app.openSource("all"));
        QCOMPARE(app.backend().sortField(), int(SortField::Title));
        QVERIFY(app.backend().sortAscending());
    }

    // Manual order by dragging: a task dropped on another goes in front of it, and the list stays manual.
    void manualOrderByDragging() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(chooseSort(app, SortField::Manual));
        QVERIFY(setSeparated(app, false));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("CEADB"), 5000);
        auto dragRow = [&](qint64 from, qint64 onto) {
            QVERIFY(tk::waitItem(QString("taskRow:%1").arg(from)) && tk::settled(QString("taskRow:%1").arg(from)));
            QVERIFY(tk::waitItem(QString("taskRow:%1").arg(onto)) && tk::settled(QString("taskRow:%1").arg(onto)));
            tk::dragSlowly(app.row(from), app.row(onto));
        };
        dragRow(mw().bravo, mw().charlie);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("BCEAD"), 5000);
        QVERIFY(app.store().task(mw().bravo).order < app.store().task(mw().charlie).order);
        QCOMPARE(app.backend().sortField(), int(SortField::Manual));
        dragRow(mw().echo, mw().alpha);   // Already in front of it: stays.
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("BCEAD"), 5000);
        dragRow(mw().charlie, mw().delta);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("BEACD"), 5000);
        // Undo puts the last move back.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("BCEAD"), 5000);
    }

    // Dragging a task while the list is sorted another way makes the order manual.
    void draggingASortedListSwitchesToManual() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(openSorting(app));
        QVERIFY(chooseSort(app, SortField::Title));
        QVERIFY(setSeparated(app, false));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("ABCDE"), 5000);
        QVERIFY(tk::settled(QString("taskRow:%1").arg(mw().echo)));
        tk::dragSlowly(app.row(mw().echo), app.row(mw().alpha));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sortField(), int(SortField::Manual), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(orderNow().indexOf("E") < orderNow().indexOf("A") && orderNow().size() == 5, 5000);
        QCOMPARE(findItem("sortButton")->property("current").toString(), QString("Manually"));
    }

    // A project sorts its own tasks, with its own sort, kept across a restart.
    void projectSortIsItsOwn() {
        Keep keep(seedMisc);
        QVERIFY(keep.start());
        const QString project = QString("project:%1").arg(mw().trip);
        {
            App &app = *keep;
            QVERIFY(app.openSource(lk(app.ids.work)));
            LIST_ITEM(row, app.row(mw().trip));
            clickSteady(row);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), mw().trip, 5000);
            app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), project, 5000);
            QTRY_COMPARE_WITH_TIMEOUT(rowTitles(app, mw().trip), (QStringList{"Book flights", "Pack bags", "Check passport"}), 5000);
            QCOMPARE(app.backend().sortField(), int(SortField::Manual));

            QVERIFY(chooseSort(app, SortField::Priority));
            QTRY_COMPARE_WITH_TIMEOUT(rowTitles(app, mw().trip), (QStringList{"Pack bags", "Check passport", "Book flights"}), 5000);
            QVERIFY(chooseSort(app, SortField::Title));
            QVERIFY(setAscending(app, false));
            QTRY_COMPARE_WITH_TIMEOUT(rowTitles(app, mw().trip), (QStringList{"Pack bags", "Check passport", "Book flights"}), 5000);
            QVERIFY(setAscending(app, true));
            QTRY_COMPARE_WITH_TIMEOUT(rowTitles(app, mw().trip), (QStringList{"Book flights", "Check passport", "Pack bags"}), 5000);
            QVERIFY(chooseSort(app, SortField::Priority));
            QVERIFY(setAscending(app, false));
            QTRY_COMPARE_WITH_TIMEOUT(rowTitles(app, mw().trip), (QStringList{"Book flights", "Check passport", "Pack bags"}), 5000);
            // The list the project lives in is not affected.
            QVERIFY(app.openSource(lk(app.ids.work)));
            QCOMPARE(app.backend().sortField(), int(SortField::DueDate));
        }
        QVERIFY(keep.start());
        App &app = *keep;
        QVERIFY(app.openSource(lk(app.ids.work)));
        LIST_ITEM(row, app.row(mw().trip));
        clickSteady(row);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), mw().trip, 5000);
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), project, 5000);
        QCOMPARE(app.backend().sortField(), int(SortField::Priority));
        QVERIFY(!app.backend().sortAscending());
        QTRY_COMPARE_WITH_TIMEOUT(rowTitles(app, mw().trip), (QStringList{"Book flights", "Check passport", "Pack bags"}), 5000);
    }

    // To-do List sorting in All groups the tasks under their list's name.
    void sortByListInAllGroupsByList() {
        App app(seedMisc);
        QVERIFY(app.ok);
        app.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("all"), 5000);
        QVERIFY(chooseSort(app, SortField::List));
        QTRY_VERIFY_WITH_TIMEOUT(headingTitles().contains("HOME") && headingTitles().contains("WORK") && headingTitles().contains("SORTING"), 5000);
        const QStringList titles = headingTitles();
        QVERIFY(titles.indexOf("HOME") < titles.indexOf("WORK") && titles.indexOf("WORK") < titles.indexOf("SORTING"));
        // The Sorting tasks all sit under their heading, after Work's.
        const QList<qint64> rows = rowIds();
        QVERIFY(rows.indexOf(app.ids.report) < rows.indexOf(mw().alpha));
        QVERIFY(rows.indexOf(app.ids.buyMilk) < rows.indexOf(app.ids.report));
        QVERIFY(setAscending(app, false));
        QTRY_VERIFY_WITH_TIMEOUT(rowIds().indexOf(mw().alpha) < rowIds().indexOf(app.ids.report), 5000);
        QVERIFY(rowIds().indexOf(app.ids.report) < rowIds().indexOf(app.ids.buyMilk));
    }
};

REGISTER_TEST(E2EMiscSort)

#include "test_misc_sort.moc"
