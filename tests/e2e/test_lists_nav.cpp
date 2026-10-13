// End-to-end scenarios for moving around the lists: the focus lists and user
// lists, their shortcuts, the list jump bar, Quick Open, the list that comes
// back after a restart, and opening a project as a list.

#include "listkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

QStringList labelsOf(QObject *popup) {
    QStringList out;
    for (const QVariant &v : popup->property("items").toList())
        out << v.toMap().value("label").toString();
    return out;
}

QObject *commandPopup() {
    QObject *p = findObject("commandPopup");
    return p && p->property("opened").toBool() ? p : nullptr;
}

}  // namespace

class E2EListNavigation : public QObject {
    Q_OBJECT

private slots:
    // "Focus Lists... All, Today, Starred, Scheduled, Done" and "Use the
    // keyboard shortcuts ⌘0–⌘5 to jump between Focus Lists."
    // (lists-and-list-groups.md) Clicking a row and pressing its shortcut land
    // on the same tasks.
    void focusListsShowTheirTasks() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        const Seed &s = app.ids;
        struct Case {
            const char *key;
            Qt::Key shortcut;
            const char *title;
            QList<qint64> shown, hidden;
        };
        const QList<Case> cases = {
            {"all", Qt::Key_1, "All", {s.buyMilk, s.electricity, s.report, w.stamps, w.cables, w.triage}, {s.oldThing, w.trip}},
            {"today", Qt::Key_2, "Today", {s.buyMilk, w.cables}, {s.report, s.studio, s.shelf, w.parcel}},
            {"starred", Qt::Key_3, "Starred", {s.shelf}, {s.buyMilk, s.report}},
            {"scheduled", Qt::Key_4, "Scheduled", {w.trip}, {s.buyMilk, s.report, s.shelf}},
            {"done", Qt::Key_5, "Done", {s.oldThing}, {s.buyMilk, s.report, w.trip}},
        };
        for (const Case &c : cases) {
            for (int how = 0; how < 2; ++how) {
                // Start each from a list, so the change is real.
                if (how == 0) {
                    QVERIFY2(pane(app, lk(s.home)), "could not open Home");
                    QVERIFY(pane(app, c.key));
                } else {
                    QVERIFY2(pane(app, lk(w.errands)), "could not open Errands");
                    app.press(c.shortcut, Qt::ControlModifier);
                }
                QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString(c.key), 3000);
                QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString(c.title), 3000);
                for (qint64 id : c.shown)
                    QTRY_VERIFY2_WITH_TIMEOUT(shows(app, id), qPrintable(QString("%1 should show %2").arg(c.title, app.task(id).title)), 3000);
                for (qint64 id : c.hidden)
                    QVERIFY2(!shows(app, id), qPrintable(QString("%1 should not show %2").arg(c.title, app.task(id).title)));
            }
        }

        // Ctrl+0 is the Inbox, which sits above the other focus lists.
        app.press(Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(w.inbox), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.triage) && !shows(app, s.buyMilk), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Inbox"), 3000);
        QVERIFY(sidebarOrder().indexOf(lk(w.inbox)) < sidebarOrder().indexOf("all"));
    }

    // "Lists... hold your tasks": each of your lists shows its own tasks and
    // none of another's, under its own name.
    void userListsShowTheirOwnTasks() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        const Seed &s = app.ids;
        struct Case {
            qint64 list;
            const char *title;
            QList<qint64> shown, hidden;
        };
        const QList<Case> cases = {
            {s.home, "Home", {s.buyMilk, s.electricity, s.plants, s.shelf}, {s.report, w.stamps, w.cables}},
            {s.work, "Work", {s.report, s.studio, s.callAlex, w.launch}, {s.buyMilk, w.stamps}},
            {w.errands, "Errands", {w.stamps, w.parcel}, {s.buyMilk, s.report, w.cables}},
            {w.misc, "Misc", {w.cables}, {s.buyMilk, w.stamps, w.triage}},
        };
        for (const Case &c : cases) {
            QVERIFY(pane(app, lk(c.list)));
            QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString(c.title), 3000);
            for (qint64 id : c.shown)
                QTRY_VERIFY2_WITH_TIMEOUT(shows(app, id), qPrintable(QString("%1 should show %2").arg(c.title, app.task(id).title)), 3000);
            for (qint64 id : c.hidden)
                QVERIFY2(!shows(app, id), qPrintable(QString("%1 should not show %2").arg(c.title, app.task(id).title)));
        }
        // A scheduled task stays out of its list until it starts.
        QVERIFY(!shows(app, w.trip));
        // Completed ones show in the list (within the window) as well as in Done.
        QVERIFY(pane(app, lk(s.work)));
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, s.oldThing), 3000);
    }

    // Ctrl+Up / Ctrl+Down walk through the sidebar one list at a time.
    void upAndDownWalkTheSidebar() {
        App app(seedLists);
        QVERIFY(app.ok);
        QStringList keys;
        for (const QString &k : sidebarOrder())
            if (!k.startsWith("g:"))
                keys << k;
        QVERIFY(keys.size() > 8);
        QVERIFY(pane(app, keys.first()));
        for (int i = 1; i < keys.size(); ++i) {
            app.press(Qt::Key_Down, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), keys.at(i), 3000);
        }
        app.press(Qt::Key_Down, Qt::ControlModifier);  // Stops at the end.
        QCOMPARE(app.backend().sourceKey(), keys.last());
        for (int i = keys.size() - 2; i >= 0; --i) {
            app.press(Qt::Key_Up, Qt::ControlModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), keys.at(i), 3000);
        }
    }

    // "List Jump Bar: Jump to a List by Name... Regular lists, Smart Lists,
    // built-in Focus lists, and even the projects and checklists... all match
    // by name." (search-and-jump-bars.md) J opens it, clicking the list's name
    // does too, Return goes, Escape leaves.
    void listJumpBar() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        QVERIFY(pane(app, lk(app.ids.home)));
        // Bare J with a task selected.
        LIST_ITEM(row, app.row(app.ids.buyMilk));
        click(row, Qt::LeftButton, pointIn(row, 0.4, 0.5));
        app.press(Qt::Key_J);
        QObject *popup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        QCOMPARE(popup->property("kind").toString(), QString("jump"));
        LIST_ITEM(input, findItem("commandInput"));
        QTRY_VERIFY_WITH_TIMEOUT(input->property("activeFocus").toBool(), 3000);
        app.type("err");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Errands"), 3000);
        QVERIFY(!labelsOf(popup).contains("Work"));
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(w.errands), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!commandPopup(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.stamps), 3000);

        // Escape leaves the view alone.
        app.press(Qt::Key_Escape);
        QVERIFY(clickPaneTitle());  // The list's name opens the bar too.
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        QCOMPARE(popup->property("kind").toString(), QString("jump"));
        app.type("work");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Work"), 3000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!commandPopup(), 3000);
        QCOMPARE(app.backend().sourceKey(), lk(w.errands));

        // Focus lists, Smart Lists and projects are targets too.
        QVERIFY(clickPaneTitle());
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        app.type("star");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Starred"), 3000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("starred"), 3000);
        QVERIFY(clickPaneTitle());
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        app.type("urg");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Urgent"), 3000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(w.urgent), 3000);
        QVERIFY(clickPaneTitle());
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        app.type("launch");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Launch"), 3000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("project:%1").arg(w.launch), 3000);

        // A list inside a collapsed group is still found.
        QVERIFY(pane(app, "today"));
        app.backend().toggleGroupCollapsed(w.projects);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(lk(w.errands)), 3000);
        QVERIFY(clickPaneTitle());
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        app.type("errands");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Errands"), 3000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(w.errands), 3000);
    }

    // "Quick Open (⇧⌘O): type a few letters and jump to any task, list,
    // project, or sub-task anywhere in the app, with your recently used tasks
    // and lists offered the moment it opens." (search-and-jump-bars.md)
    void quickOpen() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        QVERIFY(pane(app, lk(w.errands)));
        QVERIFY(pane(app, lk(w.misc)));
        QVERIFY(pane(app, "today"));

        app.press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QObject *popup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        QCOMPARE(popup->property("kind").toString(), QString("open"));
        // Where you have been lately, most recent first.
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Misc") && labelsOf(popup).contains("Errands"), 3000);
        QVERIFY(labelsOf(popup).indexOf("Misc") < labelsOf(popup).indexOf("Errands"));
        app.type("errand");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).value(0) == "Errands", 3000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(w.errands), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!commandPopup(), 3000);

        // A list by name from anywhere, then a task: it opens in its own list.
        QVERIFY(pane(app, "done"));
        app.press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        app.type("work");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).value(0) == "Work", 3000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(app.ids.work), 3000);

        app.press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        app.type("buy stamps");
        QTRY_VERIFY_WITH_TIMEOUT(labelsOf(popup).contains("Buy stamps"), 3000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(w.errands), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.stamps), 3000);

        // Escape closes it and nothing moves.
        app.press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT((popup = commandPopup()) != nullptr, 3000);
        app.type("home");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!commandPopup(), 3000);
        QCOMPARE(app.backend().sourceKey(), lk(w.errands));
    }

    // The list you were looking at is the one that opens after a restart.
    void lastSelectedListIsRestored() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString db = dir.filePath("last.db");
        const auto &w = world();
        qint64 errands = 0;
        {
            App app(seedLists, db);
            QVERIFY(app.ok);
            errands = w.errands;
            QCOMPARE(app.backend().sourceKey(), QString("today"));  // The first run opens Today.
            QVERIFY(pane(app, lk(errands)));
        }
        {
            App app(nullptr, db);
            QVERIFY(app.ok);
            QCOMPARE(app.backend().sourceKey(), lk(errands));
            QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Errands"), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.stamps) && shows(app, w.parcel), 3000);
            QVERIFY(!shows(app, app.task(w.cables).id));
            QVERIFY(pane(app, "starred"));
        }
        {
            App app(nullptr, db);
            QVERIFY(app.ok);
            QCOMPARE(app.backend().sourceKey(), QString("starred"));
            QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Starred"), 3000);
        }
    }

    // "Disclosure Triangle: Jump to a Project or Checklist... Click it and the
    // panel reveals every project and checklist inside that list. Click any of
    // them to highlight it" (lists-and-list-groups.md), and the project view:
    // the title becomes the project's, with the list as an arrow badge that
    // takes you back (Ctrl+Alt+J does both ways).
    void projectViewAndBack() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString db = dir.filePath("project.db");
        const auto &w = world();
        qint64 work = 0, launch = 0;
        {
            App app(seedLists, db);
            QVERIFY(app.ok);
            work = app.ids.work;
            launch = w.launch;
            const QString project = QString("project:%1").arg(launch);
            QVERIFY(!app.sidebarRow(project));
            QVERIFY(!findItem("sidebarExpand:" + QString::number(app.ids.home)));  // No projects, no triangle.

            LIST_ITEM(triangle, findItem("sidebarExpand:" + QString::number(work)));
            click(triangle);
            LIST_ITEM(projectRow, app.sidebarRow(project));
            QCOMPARE(shownName(projectRow), QString("Launch"));
            QVERIFY(sidebarOrder().indexOf(project) == sidebarOrder().indexOf(lk(work)) + 1);

            click(projectRow);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), project, 3000);
            QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Launch"), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(shows(app, w.announce), 3000);
            QVERIFY(!shows(app, app.ids.report));  // Only the project's own tasks.
            LIST_ITEM(badge, findItem("projectBadge"));
            QCOMPARE(badge->property("text").toString(), QString("Work"));

            // The badge goes back to the list.
            click(badge);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(work), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(shows(app, app.ids.report) && !findItem("projectBadge"), 3000);

            // Ctrl+Alt+J on the selected project goes in, and again comes out.
            LIST_ITEM(launchRow, app.row(launch));
            clickSteady(launchRow);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), launch, 3000);
            app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), project, 3000);
            app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(work), 3000);
        }
        {
            // The revealed projects stay revealed after a restart; folding hides them.
            App app(nullptr, db);
            QVERIFY(app.ok);
            const QString project = QString("project:%1").arg(launch);
            QTRY_VERIFY_WITH_TIMEOUT(app.sidebarRow(project) != nullptr, 3000);
            click(findItem("sidebarExpand:" + QString::number(work)));
            QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(project), 3000);
        }
        {
            App app(nullptr, db);
            QVERIFY(app.ok);
            QVERIFY(!app.sidebarRow(QString("project:%1").arg(launch)));
            QVERIFY(findItem("sidebarExpand:" + QString::number(work)));
        }
    }
};

REGISTER_TEST(E2EListNavigation)

#include "test_lists_nav.moc"
