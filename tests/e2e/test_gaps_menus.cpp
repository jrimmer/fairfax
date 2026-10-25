// End-to-end tests for the task context menu's less-travelled entries, the
// reverse-sort arrow, the "+" menu, section headings and the sidebar's small
// buttons: the quick date items, the priority menus, the date/time/duration
// pickers, Choose Tags, project sorting, Goto Project and Open in List, New
// Checklist from Selection, Link File, Email These Tasks, New Sub-Task, the
// section heading menu, a list's "+", the Sync button, the Done warning and
// "Don't Sync This List".

#include <QDesktopServices>
#include <QDirIterator>
#include <QUrlQuery>

#include "misckit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::mk;

namespace {

// Catches the URLs the app asks the desktop to open (a mailto: link), so none
// is opened for real.
class MailSink : public QObject {
    Q_OBJECT
public:
    QStringList urls;
public slots:
    void opened(const QUrl &url) { urls << url.toString(); }
};

// The file chooser is a portal dialog on the desktop and the tests have no
// session bus, so a request is reported as failed; this waits for that report
// and returns what was asked for, ready for the answer to be sent by hand.
template <class Trigger>
QString askedPurpose(App &app, Trigger trigger) {
    int failures = 0;
    const auto link = QObject::connect(&app.services().filePicker, &FilePicker::failed, &app.backend(), [&] { ++failures; });
    trigger();
    const bool ok = waitUntil([&] { return failures > 0; });
    QObject::disconnect(link);
    return ok ? app.services().filePicker.lastPurpose() : QString();
}

// The task menu is taller than the popup window the platform gives it, and its
// last entries can sit out of reach of the pointer, so such an entry's action
// is run as a click on it would (the menu is then closed).
bool pickLowEntry(const QString &text) {
    QQuickItem *item = nullptr;
    if (!waitUntil([&] { return (item = findText(text, "QQuickMenuItem")) != nullptr; }))
        return false;
    QWindow *w = item->window();
    QMetaObject::invokeMethod(item, "triggered");
    QTest::keyClick(w, Qt::Key_Escape);
    return waitUntil([] { return popupsClosed(); });
}

QDate today() { return QDate::currentDate(); }

// Sections in Home, and which task sits in which.
struct SectionWorld {
    qint64 morning = 0, evening = 0;
};
SectionWorld &sw() {
    static SectionWorld w;
    return w;
}

// The default world with two sections in Home: Morning holds "Buy milk" and
// "Water the plants", Evening holds "Pay electricity bill".
Seed seedSections(Store &store) {
    Seed s = seedWorld(store);
    store.begin("Seed sections", false, true);
    auto section = [&](const QString &name, double order) {
        ListSection sec;
        sec.id = store.nextId("sections");
        sec.listId = s.home;
        sec.name = name;
        sec.order = order;
        store.putSection(sec);
        return sec.id;
    };
    sw().morning = section("Morning", 1);
    sw().evening = section("Evening", 2);
    auto file = [&](qint64 task, qint64 sec) {
        Task t = store.task(task);
        t.sectionId = sec;
        store.putTask(t);
    };
    file(s.buyMilk, sw().morning);
    file(s.plants, sw().morning);
    file(s.electricity, sw().evening);
    store.commit();
    return s;
}

// A world with a lot of completed tasks, enough to raise the Done warning.
Seed seedManyDone(Store &store) {
    Seed s = seedWorld(store);
    store.begin("Seed done", false, true);
    for (int i = 0; i < 520; ++i) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = s.home;
        t.title = QString("Finished %1").arg(i);
        t.completed = true;
        t.completedAt = QDateTime::currentDateTime().addDays(-1);
        t.order = t.id;
        store.putTask(t);
    }
    store.commit();
    return s;
}

QList<qint64> reversed(QList<qint64> v) {
    std::reverse(v.begin(), v.end());
    return v;
}

// The section headings of the open list, top to bottom, as the user reads them.
QStringList headings() { return headingTitles(); }

// Sections of Home in their stored order, by name.
QStringList storedSections(App &app) {
    QStringList out;
    for (const ListSection &s : app.store().sectionsOf(app.ids.home))
        out << s.name;
    return out;
}

// Right-clicks a section heading.
bool rightClickSection(qint64 sectionId) {
    const QString name = QString("section:sec:%1").arg(sectionId);
    if (!settle() || !tk::waitItem(name) || !tk::settled(name))
        return false;
    QQuickItem *header = findItem(name);
    click(header, Qt::RightButton, pointIn(header, 0.3, 0.7));
    return waitUntil([] { return findText("Move Section Up", "QQuickMenuItem") != nullptr; });
}

// Opens the + button's menu on the task pane with a right-click.
bool openPlusMenu() {
    if (!tk::waitItem("newTaskButton") || !tk::settled("newTaskButton"))
        return false;
    click(findItem("newTaskButton"), Qt::RightButton);
    return waitUntil([] { return findText("New Project", "QQuickMenuItem") != nullptr; });
}

// Waits for the command bar (Defer, Schedule, Due Time ...) to be up, types
// into it and confirms.
bool fillCommandBar(App &app, const QString &kind, const QString &text) {
    if (!waitUntil([&] { return commandBar(kind); }))
        return false;
    app.type(text);
    // Suggestions are computed as the text changes.
    QTest::qWait(30);
    app.press(Qt::Key_Return);
    return waitUntil([] { return commandBarClosed(); });
}

// The ListView that lays out an open menu's entries, found from one of them.
QQuickItem *menuList(QQuickItem *entry) {
    for (QQuickItem *i = entry; i; i = i->parentItem())
        if (i->inherits("QQuickListView"))
            return i;
    return nullptr;
}

// Makes the window as short as it goes, so the long menus are taller than it.
void shrinkWindow(App &app) {
    app.window->resize(1180, 480);
    QVERIFY(tk::waitFor([&] { return app.window->height() <= 480; }));
}

// Scrolls the open menu that `entry` belongs to by wheel, by hover and by a set
// position, and returns what went wrong, or nothing when it stayed where it was put.
QString scrollHolds(QQuickItem *entry) {
    QQuickItem *list = menuList(entry);
    if (!list)
        return "no menu list";
    auto contentY = [&] { return list->property("contentY").toReal(); };
    if (!(list->property("contentHeight").toReal() > list->height() + 50))
        return "the menu is not taller than the window";
    QWindow *w = entry->window();
    const QPointF at = entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2));
    const QPointF global = w->mapToGlobal(at.toPoint());
    const qreal start = contentY();
    for (int i = 0; i < 4; ++i) {
        QWheelEvent ev(at, global, QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(w, &ev);
        QTest::qWait(30);
    }
    const qreal wheeled = contentY();
    if (!(wheeled > start + 20))
        return QString("the wheel did not scroll: %1").arg(wheeled);
    QTest::qWait(1000);
    if (contentY() < wheeled - 1)
        return QString("wheel: %1 -> %2").arg(wheeled).arg(contentY());
    // The pointer then moves over an entry, as a reader's does.
    QTest::mouseMove(w, at.toPoint());
    QTest::qWait(300);
    if (contentY() < wheeled - 1)
        return QString("hover: %1 -> %2").arg(wheeled).arg(contentY());
    // And a position set directly holds as well.
    const qreal bottom = list->property("contentHeight").toReal() - list->height();
    list->setProperty("contentY", bottom);
    QTest::qWait(1000);
    if (contentY() < bottom - 1)
        return QString("set: %1 -> %2").arg(bottom).arg(contentY());
    return QString();
}

}  // namespace

class E2EGapsMenus : public QObject {
    Q_OBJECT
    MailSink m_mail;

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QDesktopServices::setUrlHandler("mailto", &m_mail, "opened");
    }
    void cleanupTestCase() { QDesktopServices::unsetUrlHandler("mailto"); }

    // ------------------------------------------------------- menu layout

    // With the model suggestions off, the entries that belong to them take no
    // room: the visible entries (and separators) follow one another with no gap,
    // and no separator opens, closes or doubles up the menu.
    void taskMenuHasNoGapWhereEntriesAreHidden() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(!app.backend().agentEnabled());
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        QQuickItem *edit = tk::steadyMenuItem("Edit");
        QVERIFY(edit);
        QQuickItem *content = edit->parentItem();
        QVERIFY(content);
        for (const char *name : {"suggestPriorities", "suggestSteps"}) {
            QQuickItem *hidden = findItem(name, false);
            QVERIFY2(hidden, name);
            QVERIFY2(!hidden->isVisible(), name);
            QCOMPARE(hidden->height(), 0.0);
        }
        QList<QQuickItem *> shown;
        for (QQuickItem *i : content->childItems())
            if (i->isVisible() && (i->inherits("QQuickMenuItem") || i->inherits("QQuickMenuSeparator")))
                shown << i;
        std::sort(shown.begin(), shown.end(), [](QQuickItem *a, QQuickItem *b) { return a->y() < b->y(); });
        QVERIFY(shown.size() > 20);
        QCOMPARE(shown.first()->y(), 0.0);
        for (int i = 1; i < shown.size(); ++i) {
            const QString at = shown[i]->property("text").toString() + " after " + shown[i - 1]->property("text").toString();
            QVERIFY2(qAbs(shown[i]->y() - (shown[i - 1]->y() + shown[i - 1]->height())) < 0.5, qPrintable(at));
            QVERIFY2(!(shown[i]->inherits("QQuickMenuSeparator") && shown[i - 1]->inherits("QQuickMenuSeparator")), qPrintable(at));
        }
        QVERIFY(!shown.first()->inherits("QQuickMenuSeparator"));
        QVERIFY(!shown.last()->inherits("QQuickMenuSeparator"));
        closeMenus(app);
    }

    // In a window shorter than the menu, scrolling the menu keeps it where it was
    // put: it does not spring back to the top.
    void taskMenuStaysScrolledInAShortWindow() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        shrinkWindow(app);
        const QString name = QString("taskRow:%1").arg(app.ids.buyMilk);
        QVERIFY(tk::waitItem(name) && tk::settled(name));
        QQuickItem *row = app.row(app.ids.buyMilk);
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        QQuickItem *edit = nullptr;
        QVERIFY(tk::waitFor([&] { return (edit = tk::steadyMenuItem("Edit", 200)) != nullptr; }));
        const QString failure = scrollHolds(edit);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        closeMenus(app);
    }

    // The same for a list's menu in the sidebar, which shares the scrolling menu.
    void listMenuStaysScrolledInAShortWindow() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        shrinkWindow(app);
        QQuickItem *row = nullptr;
        QVERIFY(tk::waitFor([&] { return (row = app.sidebarRow(lk(app.ids.home))) != nullptr; }));
        click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
        QQuickItem *entry = nullptr;
        QVERIFY(tk::waitFor([&] { return (entry = tk::steadyMenuItem("Rename…", 200)) != nullptr; }));
        const QString failure = scrollHolds(entry);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        closeMenus(app);
    }

    // ------------------------------------------------------- sort direction

    // The arrow beside the sort button reverses the order: the rows turn over, each list remembers its own
    // direction, and a second click puts the order back.
    void reverseArrowReversesRowsPerListAndKeepsAcrossRestart() {
        Keep k(seedWorld);
        QVERIFY(k.start());
        App &app = *k;
        QVERIFY(app.openSource(lk(app.ids.home)));
        // Under date headings each section holds one task and keeps its place;
        // flat, the whole column turns over.
        QVERIFY(setSeparated(app, false));
        QTRY_VERIFY_WITH_TIMEOUT(rowIds().size() == 4, 5000);
        const QList<qint64> original = rowIds();
        const bool ascending = app.backend().sortAscending();

        QVERIFY(tk::clickNamed("sortReverse"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sortAscending(), !ascending, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rowIds(), reversed(original), 5000);

        // Another list keeps its own direction (and its own order).
        QVERIFY(app.openSource(lk(app.ids.work)));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sortAscending(), ascending, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowIds().size() >= 3, 5000);
        const QList<qint64> workOrder = rowIds();
        QVERIFY(app.openSource(lk(app.ids.home)));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sortAscending(), !ascending, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rowIds(), reversed(original), 5000);

        // The direction survives a restart.
        QVERIFY(k.start());
        App &again = *k;
        QVERIFY(again.openSource(lk(again.ids.home)));
        QTRY_COMPARE_WITH_TIMEOUT(again.backend().sortAscending(), !ascending, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rowIds(), reversed(original), 5000);
        QVERIFY(again.openSource(lk(again.ids.work)));
        QTRY_COMPARE_WITH_TIMEOUT(rowIds(), workOrder, 5000);
        QVERIFY(again.openSource(lk(again.ids.home)));

        // Clicking again turns it back.
        QVERIFY(tk::clickNamed("sortReverse"));
        QTRY_COMPARE_WITH_TIMEOUT(again.backend().sortAscending(), ascending, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rowIds(), original, 5000);
    }

    // ------------------------------------------------------ quick date items

    // "Due Today", "Due Tomorrow" and "Start Today" in the right-click menu set
    // the dates of the task, and the No Due Date / No Start Date entries clear
    // them again.
    void quickDateItemsSetAndClearDates() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = app.ids.plants;   // Due tomorrow, no start date.
        QVERIFY(!app.task(id).startDate.isValid());

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickMenu("Due Today"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tk::sectionOf(id), "d:" + tk::isoOf(today()), 5000);

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickMenu("Due Tomorrow"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today().addDays(1), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tk::sectionOf(id), "d:" + tk::isoOf(today().addDays(1)), 5000);

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickMenu("Start Today"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startDate, today(), 5000);

        // Cleared through the same menu.
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "No Start Date"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).startDate.isValid(), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Defer", "No Due Date"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).dueDate.isValid(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tk::sectionOf(id), QString("nodue"), 5000);
        // And Ctrl+Z walks the last change back.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today().addDays(1), 5000);
    }

    // ------------------------------------------------------------ priority

    // The Priority submenu: None / Low / Medium / High, Increase and Decrease
    // Priority (which stop at the ends).
    void priorityMenuSetsAndStepsPriority() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = app.ids.plants;
        QCOMPARE(app.task(id).priority, 0);
        QVERIFY(!tk::badge("rowPriority", id));

        const struct { const char *entry; int level; } levels[] = {{"High", 3}, {"Medium", 2}, {"Low", 1}, {"None", 0}};
        for (const auto &l : levels) {
            QVERIFY(contextMenuOn(app, id));
            QVERIFY2(clickSub("Priority", l.entry), l.entry);
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, l.level, 5000);
            QTRY_COMPARE_WITH_TIMEOUT(tk::badge("rowPriority", id), l.level > 0, 5000);
        }
        // Increase twice (None > Low > Medium), then Decrease back down.
        for (int expect : {1, 2}) {
            QVERIFY(contextMenuOn(app, id));
            QVERIFY(clickSub("Priority", "Increase Priority"));
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, expect, 5000);
        }
        for (int expect : {1, 0, 0}) {
            QVERIFY(contextMenuOn(app, id));
            QVERIFY(clickSub("Priority", "Decrease Priority"));
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, expect, 5000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!tk::badge("rowPriority", id), 5000);
        // Increase stops at High.
        prep(app, id, [](Task &t) { t.priority = 3; });
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Priority", "Increase Priority"));
        QTest::qWait(100);
        QCOMPARE(app.task(id).priority, 3);
    }

    // The priority mark on a row opens its own menu with a right-click (a click
    // stars the task instead).
    void priorityBadgeMenuSetsPriorityAndStar() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = app.ids.plants;
        auto openBadgeMenu = [&] {
            const QString area = QString("rowStarArea:%1").arg(id);
            if (!tk::waitItem(area) || !tk::settled(area))
                return false;
            click(findItem(area), Qt::RightButton);
            return waitUntil([] { return findText("Star", "QQuickMenuItem") || findText("Unstar", "QQuickMenuItem"); });
        };
        const struct { const char *entry; int level; } levels[] = {{"High", 3}, {"Medium", 2}, {"Low", 1}, {"None", 0}};
        for (const auto &l : levels) {
            QVERIFY(openBadgeMenu());
            QVERIFY2(clickMenu(l.entry), l.entry);
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, l.level, 5000);
            QTRY_COMPARE_WITH_TIMEOUT(tk::badge("rowPriority", id), l.level > 0, 5000);
        }
        QVERIFY(openBadgeMenu());
        QVERIFY(clickMenu("Star"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).starred, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(tk::badge("rowStar", id), 5000);
        QVERIFY(openBadgeMenu());
        QVERIFY(clickMenu("Unstar"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).starred, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!tk::badge("rowStar", id), 5000);
    }

    // ------------------------------------------------ pickers behind "Other…"

    // Defer > "Pick Date or Offset", Schedule > "Pick Date", Due Time > "Other"
    // and Duration > "Other" open the command bar for that setting; what is
    // typed there lands on the task. Each is undone with its "No ..." entry.
    void otherEntriesOpenTheCommandBars() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = app.ids.plants;   // Due tomorrow.

        // A typed offset moves the due date.
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Defer", "Pick Date or Offset (+3d)…"));
        QVERIFY(fillCommandBar(app, "defer", "+3d"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today().addDays(4), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Defer", "No Due Date"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).dueDate.isValid(), 5000);

        // Schedule takes a date (a start in the future would hide the row).
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "Pick Date…"));
        QVERIFY(fillCommandBar(app, "schedule", "today"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startDate, today(), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Schedule", "No Start Date"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).startDate.isValid(), 5000);

        // Due Time takes a time of day; the task needs a due date to show it.
        prep(app, id, [](Task &t) { t.dueDate = today().addDays(1); });
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Due Time", "Other…"));
        QVERIFY(fillCommandBar(app, "time", "3pm"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueTime, 15 * 60, 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Due Time", "No Time"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueTime, -1, 5000);

        // Duration takes minutes (or "1.5h").
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Duration", "Other…"));
        QVERIFY(fillCommandBar(app, "duration", "90"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).duration, 90, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(tk::badge("rowDuration", id), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Duration", "No Duration"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).duration, 0, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!tk::badge("rowDuration", id), 5000);
    }

    // Tag > "Choose Tags…" opens the tag bar; a name typed there tags the task
    // (a new name makes a new tag), and Untag > All Tags takes them off again.
    void chooseTagsTagsTheTask() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(tk::tagNames(app, app.task(id)).isEmpty());

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Tag", "Choose Tags…"));
        QVERIFY(fillCommandBar(app, "tag", "garden"));
        QTRY_COMPARE_WITH_TIMEOUT(tk::tagNames(app, app.task(id)), (QStringList{"garden"}), 5000);
        auto tagShown = [&] { return findItem(QString("rowTag:%1:garden").arg(id)) || findItem(QString("rowTagIcon:%1").arg(id)); };
        QTRY_VERIFY_WITH_TIMEOUT(tagShown(), 5000);

        // A name that is not a tag yet creates one.
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Tag", "Choose Tags…"));
        QVERIFY(fillCommandBar(app, "tag", "errand"));
        QTRY_VERIFY_WITH_TIMEOUT(tk::tagNames(app, app.task(id)).contains("errand"), 5000);

        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Untag", "All Tags"));
        QTRY_VERIFY_WITH_TIMEOUT(tk::tagNames(app, app.task(id)).isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!tagShown(), 5000);
    }

    // ------------------------------------------------------------ projects

    // "Sort Project": the submenu sorts the children of a project; Descending
    // turns the order over. The choice is kept for that project.
    void sortProjectSubmenuAndDescending() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.work)));
        const MiscWorld &w = mw();
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.trip) != nullptr, 5000);
        // Open the project to see its children.
        if (!app.row(w.bags))
            click(findItem(QString("rowDisclosure:%1").arg(w.trip)));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.bags) && app.row(w.flights) && app.row(w.passport), 5000);
        auto childOrder = [&] {
            QList<qint64> out;
            for (qint64 id : rowIds())
                if (id == w.flights || id == w.bags || id == w.passport)
                    out << id;
            return out;
        };
        // Manual order to begin with: Flights, Bags, Passport.
        QCOMPARE(childOrder(), (QList<qint64>{w.flights, w.bags, w.passport}));

        // By title, ascending: Book flights, Check passport, Pack bags.
        const QList<qint64> byTitle{w.flights, w.passport, w.bags};
        QVERIFY(contextMenuOn(app, w.trip));
        QVERIFY(clickSub("Sort Project", "Title"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().setting(QString("sort.project:%1.field").arg(w.trip), -1).toInt(), int(SortField::Title), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(childOrder(), byTitle, 5000);

        // Descending, then the same sort again turns it over.
        QVERIFY(contextMenuOn(app, w.trip));
        QVERIFY(clickSub("Sort Project", "Descending"));
        QVERIFY(contextMenuOn(app, w.trip));
        QVERIFY(clickSub("Sort Project", "Title"));
        QTRY_COMPARE_WITH_TIMEOUT(childOrder(), reversed(byTitle), 5000);
        QVERIFY(!app.store().setting(QString("sort.project:%1.ascending").arg(w.trip), true).toBool());

        // Untick Descending and ask again: back to ascending.
        QVERIFY(contextMenuOn(app, w.trip));
        QVERIFY(clickSub("Sort Project", "Descending"));
        QVERIFY(contextMenuOn(app, w.trip));
        QVERIFY(clickSub("Sort Project", "Title"));
        QTRY_COMPARE_WITH_TIMEOUT(childOrder(), byTitle, 5000);

        // Manually restores the order the project began with.
        QVERIFY(contextMenuOn(app, w.trip));
        QVERIFY(clickSub("Sort Project", "Manually"));
        QTRY_COMPARE_WITH_TIMEOUT(childOrder(), (QList<qint64>{w.flights, w.bags, w.passport}), 5000);
    }

    // Goto Project jumps from a sub-task to its project; it is greyed out for a
    // task that has none.
    void gotoProjectSelectsTheParent() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.work)));
        const MiscWorld &w = mw();
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.trip) != nullptr, 5000);
        if (!app.row(w.bags))
            click(findItem(QString("rowDisclosure:%1").arg(w.trip)));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.bags) != nullptr, 5000);
        // A task outside any project: the entry is there but disabled.
        QVERIFY(contextMenuOn(app, app.ids.callAlex));
        QQuickItem *entry = findText("Goto Project", "QQuickMenuItem");
        QVERIFY(entry && !entry->isEnabled());
        closeMenus(app);
        // A sub-task: it moves the selection to the project.
        QVERIFY(contextMenuOn(app, w.bags));
        entry = findText("Goto Project", "QQuickMenuItem");
        QVERIFY(entry && entry->isEnabled());
        QCOMPARE(app.backend().currentTaskId(), w.bags);
        QVERIFY(clickMenu("Goto Project"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), w.trip, 5000);
    }

    // "Open in List" (in a view that is not a list, such as Today) opens the
    // task's own list with the task selected; inside a list the entry is gone.
    void openInListLeavesTodayForTheList() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("today"));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) != nullptr, 5000);
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        QVERIFY(findText("Open in List", "QQuickMenuItem"));
        QVERIFY(clickMenu("Open in List"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(app.ids.home), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), app.ids.buyMilk, 5000);
        // Now inside the list: nothing to open.
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        QVERIFY(!findText("Open in List", "QQuickMenuItem"));
        closeMenus(app);
    }

    // "New > Checklist from Selection": the picked tasks become the items of a
    // new checklist; Ctrl+Z breaks it up again.
    void checklistFromSelection() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(selectRow(app, app.ids.buyMilk));
        tk::clickWith(app.row(app.ids.plants), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        QVERIFY(contextMenuOn(app, app.ids.plants));
        QVERIFY(clickSub("New", "Checklist from Selection"));
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("New Checklist").id != 0, 5000);
        const Task list = app.taskTitled("New Checklist");
        QCOMPARE(list.type, int(TaskType::Checklist));
        QCOMPARE(app.task(app.ids.buyMilk).parentId, list.id);
        QCOMPARE(app.task(app.ids.plants).parentId, list.id);
        // The new checklist's title is open for editing; Ctrl+Return keeps the name.
        QVERIFY(tk::waitItem("editorTitle"));
        QVERIFY(tk::closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(tk::badge("rowChecklist", list.id), 5000);
        // Undo (the new checklist's title edit may be open, which takes one more).
        for (int i = 0; i < 3 && app.task(app.ids.buyMilk).parentId; ++i) {
            app.press(Qt::Key_Z, Qt::ControlModifier);
            QTest::qWait(100);
        }
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.buyMilk).parentId, qint64(0), 5000);
        QCOMPARE(app.task(app.ids.plants).parentId, qint64(0));
    }

    // ------------------------------------------------- link file and email

    // "Link File…" asks the chooser for a file; the answer becomes a link in
    // the task's notes, and Ctrl+Z takes it back out.
    void linkFileAddsALinkToTheNotes() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        QFile f(files.filePath("plan.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("the plan");
        f.close();
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = app.ids.shelf;
        QVERIFY(app.task(id).notes.isEmpty());
        QVERIFY(contextMenuOn(app, id));
        const QString purpose = askedPurpose(app, [&] { QVERIFY(pickLowEntry("Link File…")); });
        QCOMPARE(purpose, QString("link:%1").arg(id));
        QCOMPARE(app.task(id).notes, QString());   // Nothing happens until the chooser answers.
        emit app.services().filePicker.picked(f.fileName(), purpose);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).notes.contains("plan.txt"), 5000);
        QVERIFY(app.task(id).notes.contains(files.path()));
        // Undo.
        app.window->requestActivate();
        QVERIFY(selectRow(app, id));
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).notes.isEmpty(), 5000);
    }

    // "Email These Tasks…" hands a mailto: link with the selected tasks to the
    // mail client.
    void emailTheseTasksBuildsAMailtoLink() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        m_mail.urls.clear();
        QVERIFY(selectRow(app, app.ids.buyMilk));
        tk::clickWith(app.row(app.ids.plants), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        QVERIFY(contextMenuOn(app, app.ids.plants));
        QVERIFY(pickLowEntry("Email These Tasks…"));
        QTRY_COMPARE_WITH_TIMEOUT(m_mail.urls.size(), 1, 5000);
        const QUrl url(m_mail.urls.first());
        QCOMPARE(url.scheme(), QString("mailto"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue("subject", QUrl::FullyDecoded), QString("2 tasks"));
        const QString body = query.queryItemValue("body", QUrl::FullyDecoded);
        QVERIFY2(body.contains("Buy milk") && body.contains("Water the plants"), qPrintable(body));
        QVERIFY(!body.contains("Fix the shelf"));

        // One task: its title is the subject.
        m_mail.urls.clear();
        QVERIFY(selectRow(app, app.ids.shelf));
        QVERIFY(contextMenuOn(app, app.ids.shelf));
        QVERIFY(pickLowEntry("Email These Tasks…"));
        QTRY_COMPARE_WITH_TIMEOUT(m_mail.urls.size(), 1, 5000);
        QCOMPARE(QUrlQuery(QUrl(m_mail.urls.first())).queryItemValue("subject", QUrl::FullyDecoded), QString("Fix the shelf"));
    }

    // ------------------------------------------------------------ "+" menu

    // The "+" menu's New Sub-Task adds a task inside the selected one (it is
    // greyed out with nothing selected); Ctrl+Z removes it.
    void newSubTaskFromThePlusMenu() {
        App app(seedMisc);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.work)));
        const MiscWorld &w = mw();
        // Nothing selected yet.
        QVERIFY(openPlusMenu());
        QQuickItem *entry = findText("New Sub-Task", "QQuickMenuItem");
        QVERIFY(entry && !entry->isEnabled());
        closeMenus(app);

        QVERIFY(selectRow(app, w.trip));
        QVERIFY(openPlusMenu());
        entry = findText("New Sub-Task", "QQuickMenuItem");
        QVERIFY(entry && entry->isEnabled());
        const int before = app.store().tasks().size();
        QVERIFY(clickMenu("New Sub-Task"));
        QVERIFY(tk::waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(tk::prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Renew visa");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Renew visa").id != 0, 5000);
        const Task sub = app.taskTitled("Renew visa");
        QCOMPARE(sub.parentId, w.trip);
        QCOMPARE(sub.listId, app.store().task(w.trip).listId);
        QCOMPARE(sub.type, int(TaskType::Task));
        QCOMPARE(app.store().tasks().size(), before + 1);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Renew visa").id == 0, 5000);
    }

    // ----------------------------------------------------- section headings

    // The heading's right-click menu: Rename Section, Move Section Up / Down
    // and Delete Section (whose tasks stay in the list, outside any section).
    void sectionRenameMoveAndDelete() {
        App app(seedSections);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(chooseSort(app, SortField::Section));
        const SectionWorld &s = sw();
        QTRY_VERIFY2_WITH_TIMEOUT(headings().contains("MORNING") && headings().contains("EVENING"), qPrintable(headings().join(",")), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tk::sectionOf(app.ids.buyMilk), QString("sec:%1").arg(s.morning), 5000);

        // Rename.
        QVERIFY(rightClickSection(s.evening));
        QVERIFY(clickMenu("Rename Section…"));
        QTRY_VERIFY2_WITH_TIMEOUT(QString(app.focusDescription()).contains("InlineEditor"), qPrintable(app.focusDescription()), 5000);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("Night");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(storedSections(app), (QStringList{"Morning", "Night"}), 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(headings().contains("NIGHT") && !headings().contains("EVENING"), qPrintable(headings().join(",")), 5000);
        // Back to the old name.
        QVERIFY(rightClickSection(s.evening));
        QVERIFY(clickMenu("Rename Section…"));
        QTRY_VERIFY2_WITH_TIMEOUT(QString(app.focusDescription()).contains("InlineEditor"), qPrintable(app.focusDescription()), 5000);
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("Evening");
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(storedSections(app), (QStringList{"Morning", "Evening"}), 5000);

        // Move Up / Down.
        auto order = [&] {
            QStringList h = headings();
            h.removeAll("NOT IN A SECTION");
            return h;
        };
        QCOMPARE(order(), (QStringList{"MORNING", "EVENING"}));
        QVERIFY(rightClickSection(s.evening));
        QVERIFY(clickMenu("Move Section Up"));
        QTRY_COMPARE_WITH_TIMEOUT(storedSections(app), (QStringList{"Evening", "Morning"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(order(), (QStringList{"EVENING", "MORNING"}), 5000);
        QVERIFY(rightClickSection(s.evening));
        QVERIFY(clickMenu("Move Section Down"));
        QTRY_COMPARE_WITH_TIMEOUT(storedSections(app), (QStringList{"Morning", "Evening"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(order(), (QStringList{"MORNING", "EVENING"}), 5000);
        // At the bottom, Down does nothing.
        QVERIFY(rightClickSection(s.evening));
        QVERIFY(clickMenu("Move Section Down"));
        QTest::qWait(100);
        QCOMPARE(storedSections(app), (QStringList{"Morning", "Evening"}));

        // Delete: the section goes, its task stays, in "Not in a Section".
        QVERIFY(rightClickSection(s.evening));
        QVERIFY(clickMenu("Delete Section"));
        QTRY_COMPARE_WITH_TIMEOUT(storedSections(app), (QStringList{"Morning"}), 5000);
        QCOMPARE(app.task(app.ids.electricity).sectionId, qint64(0));
        QVERIFY(!app.task(app.ids.electricity).deleted);
        QTRY_VERIFY2_WITH_TIMEOUT(!headings().contains("EVENING"), qPrintable(headings().join(",")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.electricity) != nullptr, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tk::sectionOf(app.ids.electricity), QString("sec:0"), 5000);
        // Undo brings the section and its task back.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(storedSections(app), (QStringList{"Morning", "Evening"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.electricity).sectionId, s.evening, 5000);
    }

    // ------------------------------------------------------------- sidebar

    // The [+] that shows on a list's row under the pointer starts a new task in
    // that list, whichever list is open.
    void sidebarRowPlusMakesATaskInThatList() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        parkMouse(app);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sidebarAddTask"), 5000);   // Only on the row under the pointer.
        QQuickItem *row = app.sidebarRow(lk(app.ids.work));
        QVERIFY(row);
        QTest::mouseMove(app.window, pointIn(row, 0.4, 0.5));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("sidebarAddTask") != nullptr, 5000);
        QVERIFY(tk::settled("sidebarAddTask"));
        click(findItem("sidebarAddTask"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), lk(app.ids.work), 5000);
        QVERIFY(tk::waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(tk::prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Order stamps");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Order stamps").id != 0, 5000);
        QCOMPARE(app.taskTitled("Order stamps").listId, app.ids.work);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Order stamps").id == 0, 5000);
    }

    // The Sync button at the foot of the sidebar runs a sync now (as Ctrl+S
    // does): with automatic sync off, a change waits until it is clicked.
    void syncButtonSyncsNow() {
        QTemporaryDir remote;
        QVERIFY(remote.isValid());
        // Automatic sync is off from the start: with it on, the app syncs once by
        // itself a few seconds after launch, which would hide a button that does nothing.
        auto seed = [](Store &store) {
            const Seed s = seedWorld(store);
            store.setSetting("sync.auto", false);
            return s;
        };
        App app(seed, QString(), true);
        QVERIFY(app.ok);
        QVERIFY(app.services().folderSync);
        // No provider yet: no button.
        QVERIFY(!findItem("syncButton"));
        auto folderHas = [&](const QString &text) {
            QDirIterator it(remote.path(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                if (readFile(it.next()).contains(text))
                    return true;
            return false;
        };
        emit app.services().filePicker.picked(remote.path(), "sync-folder");
        QTRY_VERIFY_WITH_TIMEOUT(app.services().folderSync->configured() && !app.services().folderSync->busy() && folderHas("Buy milk"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("syncButton") != nullptr, 5000);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(selectRow(app, app.ids.shelf));
        app.press(Qt::Key_N, Qt::ControlModifier);
        QVERIFY(tk::waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(tk::prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Made before the button");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Made before the button").id != 0, 5000);
        // Nothing syncs on its own while automatic sync is off.
        QSignalSpy synced(app.services().folderSync.get(), &FolderSync::synced);
        QTest::qWait(1500);
        QVERIFY(!folderHas("Made before the button"));
        QCOMPARE(synced.count(), 0);
        QVERIFY(tk::clickNamed("syncButton"));
        QTRY_VERIFY_WITH_TIMEOUT(synced.count() >= 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(folderHas("Made before the button"), 15000);
    }

    // The "Synced … ago" label at the foot of the sidebar stays between the
    // calendar buttons and the sync button, even at the largest text size,
    // switching to its short form ("now", "2m") when the full one won't fit.
    void syncAgeLabelFitsTheFooter() {
        QTemporaryDir remote;
        QVERIFY(remote.isValid());
        auto seed = [](Store &store) {
            const Seed s = seedWorld(store);
            store.setSetting("sync.auto", false);
            store.setSetting("appearance.textSize", 8);
            return s;
        };
        App app(seed, QString(), true);
        QVERIFY(app.ok);
        emit app.services().filePicker.picked(remote.path(), "sync-folder");
        QTRY_VERIFY_WITH_TIMEOUT(app.services().folderSync->configured() && !app.services().folderSync->busy(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("syncAge") != nullptr && findItem("syncAge")->isVisible(), 5000);
        QQuickItem *label = findItem("syncAge");
        QQuickItem *calendar = findItem("calendarControls");
        QQuickItem *button = findItem("syncButton");
        QVERIFY(calendar && button);
        QTRY_VERIFY_WITH_TIMEOUT(label->property("text").toString().size() > 0, 5000);
        const QRectF l = label->mapRectToScene(QRectF(0, 0, label->width(), label->height()));
        const QRectF c = calendar->mapRectToScene(QRectF(0, 0, calendar->width(), calendar->height()));
        const QRectF b = button->mapRectToScene(QRectF(0, 0, button->width(), button->height()));
        QVERIFY2(l.left() >= c.right(), "the label starts after the calendar buttons");
        QVERIFY2(l.right() <= b.left(), "the label ends before the sync button");
        QVERIFY2(label->property("contentWidth").toReal() <= label->width() + 0.5, "the text fits without spilling");
        QVERIFY(!label->property("truncated").toBool());
    }

    // Done carries a warning mark once 500 or more completed tasks pile up and
    // auto-delete is off; clicking it opens the auto-delete setting. Turning
    // auto-delete on takes the mark away.
    void doneWarningMarkOpensTheAutoDeleteSetting() {
        App app(seedManyDone);
        QVERIFY(app.ok);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("sidebarDoneWarning") != nullptr, 5000);
        QVERIFY(!settingsWindow());
        QVERIFY(tk::settled("sidebarDoneWarning"));
        click(findItem("sidebarDoneWarning"));
        QTRY_VERIFY_WITH_TIMEOUT(settingsWindow() != nullptr && settingsWindow()->isExposed(), 5000);
        QCOMPARE(settingsWindow()->property("pane").toInt(), 2);
        // The click does not open Done.
        QVERIFY(app.backend().sourceKey() != "done");
        QQuickWindow *sett = settingsWindow();
        closeSettings(app);
        Q_UNUSED(sett);
        // Auto-delete on: the mark is gone.
        app.backend().setSetting("tasks.autoDeleteDays", 30);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("sidebarDoneWarning") == nullptr, 5000);
    }

    // The list menu's "Don't Sync This List" is a tick: on it keeps the list
    // out of sync, off it takes it back in.
    void dontSyncThisListToggles() {
        App app;
        QVERIFY(app.ok);
        auto excluded = [&] { return app.backend().listInfo(app.ids.work).value("syncExcluded").toBool(); };
        auto ticked = [&] {
            QQuickItem *row = app.sidebarRow(lk(app.ids.work));
            if (!settle() || !row)
                return QVariant();
            click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
            QQuickItem *entry = nullptr;
            if (!waitUntil([&] { return (entry = findText("Don't Sync This List", "QQuickMenuItem")) != nullptr; }))
                return QVariant();
            const bool on = entry->property("checked").toBool();
            QTest::keyClick(entry->window(), Qt::Key_Escape);
            settle();
            return QVariant(on);
        };
        QVERIFY(!excluded());
        QCOMPARE(ticked(), QVariant(false));
        QVERIFY(listMenu(app, app.ids.work, "Don't Sync This List"));
        QTRY_VERIFY_WITH_TIMEOUT(excluded(), 5000);
        QCOMPARE(ticked(), QVariant(true));
        QVERIFY(listMenu(app, app.ids.work, "Don't Sync This List"));
        QTRY_VERIFY_WITH_TIMEOUT(!excluded(), 5000);
        QCOMPARE(ticked(), QVariant(false));
    }
};

REGISTER_TEST(E2EGapsMenus)

#include "test_gaps_menus.moc"
