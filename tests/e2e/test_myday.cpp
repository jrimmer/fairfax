// End-to-end tests for My Day: the sidebar item and its shortcut, the Default
// View setting, the screen's sections, reason chips and capacity line, the
// caps and their expanders, dropping a row on the timeline and the compact bar
// in a narrow window. My Day is on by default; a test that wants it off turns
// the setting off before the app starts.

#include <QAccessible>
#include "alarms.h"

#include "automation.h"
#include "misckit.h"
#include "settingskit.h"
#include "../testregistry.h"
#include "myday.h"

using namespace e2e;
using namespace e2e::mk;

namespace {

// The default world plus a task that starts today and one with an absolute
// alert today, so the Suggestions section has each reason to show.
struct MyDayWorld {
    qint64 startsToday = 0, alerted = 0, alarm = 0;
};
MyDayWorld &myWorld() {
    static MyDayWorld w;
    return w;
}

Seed seedMyDay(Store &store) {
    const Seed s = seedWorld(store);
    MyDayWorld w;
    const QDate today = QDate::currentDate();
    store.begin("Seed My Day", false);
    auto task = [&](qint64 listId, const QString &title, int dueDays) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = listId;
        t.title = title;
        if (dueDays != INT_MIN)
            t.dueDate = today.addDays(dueDays);
        t.order = t.id;
        store.putTask(t);
        return t.id;
    };
    // A start date later today keeps it out of "Already today" (it is still
    // scheduled), so the reason is what shows it.
    w.startsToday = task(s.home, "Start the day", INT_MIN);
    Task start = store.task(w.startsToday);
    start.startDate = today;
    start.startTime = 23 * 60;
    store.putTask(start);

    w.alerted = task(s.work, "Alert me", INT_MIN);
    Task alerted = store.task(w.alerted);
    alerted.startDate = today.addDays(5);
    store.putTask(alerted);
    Alarm a;
    a.id = store.nextId("alarms");
    a.taskId = w.alerted;
    a.kind = Alarm::Absolute;
    a.at = QDateTime(today, QTime(14, 0));
    store.putAlarm(a);
    w.alarm = a.id;

    store.commit();
    myWorld() = w;
    return s;
}

// A world with more than seven overdue tasks, to prove the cap.
Seed seedManyOverdue(Store &store) {
    Seed s = seedWorld(store);
    const QDate today = QDate::currentDate();
    store.begin("Many overdue", false);
    for (int i = 0; i < 9; ++i) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = s.home;
        t.title = QStringLiteral("Overdue %1").arg(i);
        t.dueDate = today.addDays(-1 - i);
        t.order = t.id;
        store.putTask(t);
    }
    store.commit();
    return s;
}

Seed seedMyDayOff(Store &store) {
    Seed s = seedWorld(store);
    store.setSetting("myday.enabled", false);
    return s;
}

// A world with one list and no tasks at all: every My Day section is empty.
Seed seedBareWorld(Store &store) {
    Seed s;
    store.begin("Bare", false);
    TaskList l;
    l.id = store.nextId("lists");
    l.name = "Home";
    l.order = 1;
    store.putList(l);
    s.home = l.id;
    store.commit();
    return s;
}

// One task due today with no duration: Already today, and the capacity line
// shows because a task without a duration is still something to measure.
qint64 &onePlannedTask() {
    static qint64 id = 0;
    return id;
}
Seed seedOnePlanned(Store &store) {
    Seed s = seedBareWorld(store);
    store.begin("One planned", false);
    Task t;
    t.id = store.nextId("tasks");
    t.listId = s.home;
    t.title = "Today only";
    t.dueDate = QDate::currentDate();
    t.order = t.id;
    store.putTask(t);
    store.commit();
    onePlannedTask() = t.id;
    return s;
}

// The prompt on, and no day planned yet: the first launch of the day asks.
// The launch view is Today so the test can tell the prompt's answer apart from
// a fresh database's My Day default.
Seed seedMyDayPromptOn(Store &store) {
    Seed s = seedMyDay(store);
    store.setSetting("myday.prompt", true);
    store.setSetting("myday.lastPlanned", QString());
    store.setSetting("ui.defaultView", "today");
    return s;
}

// The prompt on, but today is already marked planned: it stays quiet.
Seed seedMyDayPromptPlanned(Store &store) {
    Seed s = seedMyDayPromptOn(store);
    store.setSetting("myday.lastPlanned", QDate::currentDate().toString(Qt::ISODate));
    return s;
}

// A few dated tasks for the plan keys: two suggestions (starred, and one due
// in two days) and one already due today.
struct PlanWorld {
    qint64 suggestionStar = 0, suggestionLater = 0, alreadyToday = 0;
};
PlanWorld &planWorld() {
    static PlanWorld w;
    return w;
}
Seed seedPlanKeys(Store &store) {
    const Seed s = seedWorld(store);
    const QDate today = QDate::currentDate();
    PlanWorld w;
    store.begin("Seed plan keys", false);
    auto task = [&](const QString &title, const std::function<void(Task &)> &fn) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = s.home;
        t.title = title;
        t.order = t.id;
        fn(t);
        store.putTask(t);
        return t.id;
    };
    w.suggestionStar = task("Starred suggestion", [](Task &t) { t.starred = true; });
    w.suggestionLater = task("Due in two days", [&](Task &t) { t.dueDate = today.addDays(2); });
    w.alreadyToday = task("Already today", [&](Task &t) { t.dueDate = today; });
    store.commit();
    planWorld() = w;
    return s;
}

// Three undated, unstarred tasks with known durations for the B key.
struct BlockWorld {
    qint64 first = 0, second = 0, third = 0;
};
BlockWorld &blockWorld() {
    static BlockWorld w;
    return w;
}
Seed seedBlockKeys(Store &store) {
    const Seed s = seedWorld(store);
    BlockWorld w;
    const QDate today = QDate::currentDate();
    store.begin("Seed block keys", false);
    auto task = [&](const QString &title, int duration) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = s.home;
        t.title = title;
        t.duration = duration;
        t.dueDate = today;   // Already today, so it is on screen to be selected.
        t.order = t.id;
        store.putTask(t);
        return t.id;
    };
    w.first = task("Block one", 60);
    w.second = task("Block two", 45);
    w.third = task("Block three", 0);   // No duration: B gives it 30 minutes.
    store.commit();
    blockWorld() = w;
    return s;
}

// The shutdown review's world: two open tasks still today (one 90 minutes,
// one without a duration), one completed today for 45 minutes, one completed
// yesterday and one abandoned today (an hour), which must not count anywhere.
struct ShutdownWorld {
    qint64 openLong = 0, openUndated = 0, doneToday = 0, doneYesterday = 0, abandonedToday = 0;
};
ShutdownWorld &shutdownWorld() {
    static ShutdownWorld w;
    return w;
}

Seed seedShutdown(Store &store) {
    const Seed s = seedWorld(store);
    // Launch on Today so the test can tell the review's opening apart from the
    // fresh-database My Day default.
    store.setSetting("ui.defaultView", "today");
    ShutdownWorld w;
    const QDate today = QDate::currentDate();
    store.begin("Seed shutdown", false);
    auto task = [&](const QString &title, const std::function<void(Task &)> &fn) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = s.home;
        t.title = title;
        t.order = t.id;
        fn(t);
        store.putTask(t);
        return t.id;
    };
    w.openLong = task("Still on", [&](Task &t) { t.dueDate = today; t.duration = 90; });
    w.openUndated = task("Undated today", [&](Task &t) { t.dueDate = today; });
    w.doneToday = task("Finished today", [&](Task &t) {
        t.dueDate = today;
        t.duration = 45;
        t.completed = true;
        t.completedAt = QDateTime(today, QTime(12, 0));
    });
    w.doneYesterday = task("Finished yesterday", [&](Task &t) {
        t.dueDate = today.addDays(-1);
        t.duration = 120;
        t.completed = true;
        t.completedAt = QDateTime(today.addDays(-1), QTime(17, 0));
    });
    w.abandonedToday = task("Dropped today", [&](Task &t) {
        t.dueDate = today;
        t.duration = 60;
        t.completed = true;
        t.abandoned = true;
        t.completedAt = QDateTime(today, QTime(13, 0));
    });
    store.commit();
    shutdownWorld() = w;
    return s;
}

// An .ics with one event today, 10:00-11:00, as the calendar e2e tests write.
QString writeCalendar(const QString &dir, const QString &name = "myday.ics", int endHour = 11) {
    const QString path = dir + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    const QString today = QDate::currentDate().toString("yyyyMMdd");
    const QString end = QString("%1%2").arg(endHour, 2, 10, QLatin1Char('0')).arg("0000");
    QString text = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//e2e//EN\r\n";
    text += QString("BEGIN:VEVENT\r\nUID:a@e2e\r\nDTSTAMP:20270101T000000Z\r\nDTSTART:%1T100000\r\nDTEND:%1T%2\r\nSUMMARY:Standup\r\nEND:VEVENT\r\n")
                .arg(today, end);
    text += "END:VCALENDAR\r\n";
    f.write(text.toUtf8());
    return path;
}

// Pins My Day's clock for the life of a test, as the calendar tests pin
// theirs, so "today" and the work-day window do not move under the test.
struct ClockPin {
    explicit ClockPin(const QDateTime &at) { MyDay::setClock([at] { return at; }); }
    ~ClockPin() { MyDay::setClock({}); }
};

// The joined text under the capacity RowLayout.
QString capacityText() {
    QQuickItem *cap = findItem("myDayCapacity");
    if (!cap)
        return {};
    QStringList parts;
    findIn(cap, [&](QQuickItem *i) {
        if (i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0)
            parts << i->property("text").toString();
        return false;
    });
    return parts.join(' ');
}

// A visible item's baseline in scene coordinates: a Text lays its baseline
// baselineOffset down from its top, and a RowLayout that reports one does the
// same, so three differently-sized items can be compared.
qreal baselineSceneY(QQuickItem *item) {
    return item->mapToScene(QPointF(0, item->baselineOffset())).y();
}

// The text of the pane's centred empty state, or empty when it is not shown.
QString emptyStateText() {
    QQuickItem *t = findItem("paneEmptyState");
    return t ? t->property("text").toString() : QString();
}

// Scrolls the task list to a task, creating its delegate if it is far below.
void revealTaskInList(App &app, qint64 id) {
    QQuickItem *list = findItem("taskList");
    if (!list)
        return;
    const int i = app.backend().tasks()->indexOfKey(QString("t:%1").arg(id));
    if (i < 0)
        return;
    QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, i), Q_ARG(int, 3));  // Contain
    QTest::qWait(50);
}

// The reason chip's text for a task, or empty when there is no chip.
QString reasonOf(qint64 id) {
    QQuickItem *chip = findItem(QString("myDayReason:%1").arg(id));
    if (!chip)
        return {};
    QQuickItem *text = findIn(chip, [](QQuickItem *i) {
        return i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0;
    });
    return text ? text->property("text").toString() : QString();
}

}  // namespace

class E2EMyDay : public QObject {
    Q_OBJECT

private slots:
    // With My Day off there is no sidebar item, and its shortcut does nothing.
    void offHidesTheItemAndTheShortcut() {
        Session s;
        s.seed = seedMyDayOff;
        QVERIFY(s.start());
        App &app = *s;
        QVERIFY(!findItem("sidebarRow:myday"));
        // The shortcut does nothing: the source does not move.
        const QString before = app.backend().sourceKey();
        app.press(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(300);
        QCOMPARE(app.backend().sourceKey(), before);
        QVERIFY(!app.backend().sourceIsMyDay());
        // The Default View combo has no My Day entry either.
        for (const QVariant &v : app.backend().defaultViewChoices())
            QVERIFY(v.toMap().value("id").toString() != "myday");
    }

    // The My Day item is the first view in the sidebar, above All.
    void sidebarItemIsFirstAboveAll() {
        App app(seedMyDay);
        QVERIFY(app.ok);
        const QStringList order = sidebarOrder();
        QVERIFY2(!order.isEmpty(), "no sidebar rows");
        QCOMPARE(order.first(), QString("myday"));
        QVERIFY(order.indexOf("myday") < order.indexOf("all"));
        QCOMPARE(shownName(app.sidebarRow("myday")), QString("My Day"));
    }

    // With it on, the sidebar item and Ctrl+Shift+P both open My Day mode, and
    // choosing another source leaves it.
    void itemAndShortcutOpenMyDayAndAnotherLeaves() {
        App app(seedMyDay);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QCOMPARE(app.backend().sourceKey(), QString("myday"));
        QVERIFY(app.backend().sourceIsMyDay());
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("My Day"), 5000);
        // The header shows the date beside the title, as Today's does.
        QQuickItem *date = findItem("paneDate");
        QVERIFY(date);
        QVERIFY(date->property("visible").toBool());
        QCOMPARE(date->property("text").toString(), app.backend().sourceSubtitle());

        QVERIFY(app.openSource("all"));
        QVERIFY(!app.backend().sourceIsMyDay());
        app.press(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("myday"), 5000);
        QVERIFY(app.backend().sourceIsMyDay());

        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(!app.backend().sourceIsMyDay());
    }

    // A fresh database opens on My Day; the harness only seeds "last" for tests
    // that predate the feature, so this one opts out.
    void freshStoreOpensOnMyDay() {
        App app(nullptr, QString(), false, false);
        QVERIFY(app.ok);
        QCOMPARE(app.backend().sourceKey(), QString("myday"));
        QCOMPARE(paneTitle(), QString("My Day"));
    }

    // With My Day off, a fresh database opens on Today.
    void myDayOffOpensOnToday() {
        Session s;
        s.seed = seedMyDayOff;
        s.lastView = false;
        QVERIFY(s.start());
        QCOMPARE(s->backend().sourceKey(), QString("today"));
    }

    // A remembered My Day view with My Day switched off opens on Today: the
    // feature being off leaves the launch as it was before My Day existed.
    void lastMyDayWithFeatureOffOpensToday() {
        Session s;
        s.seed = seedMyDay;
        QVERIFY(s.start());
        QVERIFY(s->openSource("myday"));
        s->backend().setSetting("ui.defaultView", "last");
        QVERIFY(s.restart());
        QCOMPARE(s->backend().sourceKey(), QString("myday"));
        // Now switch My Day off and relaunch: today, not a disabled feature.
        QVERIFY(s.start());
        s->backend().setSetting("myday.enabled", false);
        QVERIFY(s.restart());
        QCOMPARE(s->backend().sourceKey(), QString("today"));
        QVERIFY(!s->backend().sourceIsMyDay());
    }

    // The Default view combo in Settings > General is the real control: it
    // changes the choice the next launch opens on.
    void defaultViewComboChangesTheLaunchView() {
        Session s;
        s.lastView = false;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openSettings(*s, sw);
        selectPane(sw, "General");
        // My Day is first while it is on; Today sits two below it.
        chooseCombo("setting:ui.defaultView", 2);
        QCOMPARE(s->backend().setting("ui.defaultView").value("value").toString(), QString("today"));
        closeSettings(*s);
        QVERIFY(s.restart());
        QCOMPARE(s->backend().sourceKey(), QString("today"));

        // A chosen list is in the combo by name, and the next launch opens it.
        QVERIFY(s->openSource("myday"));
        openSettings(*s, sw);
        selectPane(sw, "General");
        QQuickItem *combo = findItem("setting:ui.defaultView");
        QVERIFY(combo);
        int home = -1;
        for (int i = 0; i < combo->property("count").toInt() && home < 0; ++i) {
            QString label;
            QMetaObject::invokeMethod(combo, "textAt", Q_RETURN_ARG(QString, label), Q_ARG(int, i));
            if (label == "Home")
                home = i;
        }
        QVERIFY2(home >= 0, "no Home entry in the Default view combo");
        chooseCombo("setting:ui.defaultView", home);
        closeSettings(*s);
        QVERIFY(s.restart());
        QCOMPARE(s->backend().sourceKey(), lk(s.ids.home));
    }

    // "last" restores the view the app was last on.
    void defaultViewLastRestoresTheSource() {
        Session s;
        s.seed = seedMyDay;
        QVERIFY(s.start());
        QVERIFY(s->openSource("starred"));
        s->backend().setSetting("ui.defaultView", "last");
        QVERIFY(s.restart());
        QCOMPARE(s->backend().sourceKey(), QString("starred"));
    }

    // A chosen list opens on it at launch.
    void defaultViewOpensAChosenList() {
        Session s;
        s.seed = seedMyDay;
        s.lastView = false;
        QVERIFY(s.start());
        s->backend().setSetting("ui.defaultView", lk(s.ids.home));
        QVERIFY(s.restart());
        QCOMPARE(s->backend().sourceKey(), lk(s.ids.home));
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Home"), 5000);
    }

    // A chosen list that has since been deleted falls back to Today.
    void deletedListFallsBackToToday() {
        Session s;
        s.lastView = false;
        s.seed = [](Store &st) {
            Seed ids = seedWorld(st);
            st.setSetting("ui.defaultView", lk(ids.home));
            TaskList home = st.list(ids.home);
            home.deleted = true;
            st.begin("delete", false);
            st.putList(home);
            st.commit();
            return ids;
        };
        QVERIFY(s.start());
        QCOMPARE(s->backend().sourceKey(), QString("today"));
    }

    // The sections show the right tasks, each with its reason chip in Overdue
    // and Suggestions.
    void sectionsShowTheirTasksAndReasons() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedMyDay);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        const Seed &s = app.ids;
        const MyDayWorld &w = myWorld();
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(s.electricity)));
        // Overdue, first, with its days-late reason.
        QCOMPARE(reasonOf(s.electricity), QString("2 days overdue"));
        // Already today: due today, no reason chip.
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(s.buyMilk)));
        QCOMPARE(reasonOf(s.buyMilk), QString());
        // Suggestions: starred, starts today, alert today.
        revealTaskInList(app, s.shelf);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(s.shelf) != nullptr, 5000);
        QCOMPARE(reasonOf(s.shelf), QString("Starred"));
        revealTaskInList(app, w.startsToday);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.startsToday) != nullptr, 5000);
        QCOMPARE(reasonOf(w.startsToday), QString("Starts today"));
        revealTaskInList(app, w.alerted);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.alerted) != nullptr, 5000);
        QCOMPARE(reasonOf(w.alerted), QString("Alert 14:00"));
        // The section headings, in order.
        QCOMPARE(sectionTitles(), (QStringList{"OVERDUE", "TODAY", "COULD DO TODAY"}));
    }

    // An empty section emits no heading row, and a completely empty day shows
    // the pane's own empty-state message instead of three bare headings.
    void emptySectionsAreHiddenAndTheDayExplainsItself() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedBareWorld);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QCOMPARE(app.backend().tasks()->rowCount(), 0);
        // No section headings at all, and the message names the day.
        QCOMPARE(sectionTitles(), QStringList{});
        QTRY_COMPARE_WITH_TIMEOUT(emptyStateText(), QString("Nothing planned for today"), 5000);
        // An empty day has nothing to measure.
        QVERIFY(!findItem("myDayCapacity"));
        // A typed search that matches nothing keeps the existing wording.
        app.backend().setSearch("zzz");
        QTRY_COMPARE_WITH_TIMEOUT(emptyStateText(), QString("No tasks match “zzz”"), 5000);
        app.backend().setSearch(QString());
        QTRY_COMPARE_WITH_TIMEOUT(emptyStateText(), QString("Nothing planned for today"), 5000);
    }

    // A task in the plan brings its section heading and the capacity line back;
    // hiding it (H) takes both away again.
    void onePlannedTaskShowsTheSectionAndCapacityAgain() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedOnePlanned);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QTRY_COMPARE_WITH_TIMEOUT(sectionTitles(), QStringList{"TODAY"}, 5000);
        QQuickItem *cap = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((cap = findItem("myDayCapacity")) != nullptr, 5000);
        QVERIFY(cap->property("visible").toBool());
        // It reads the task without a duration, not a zero capacity line.
        QVERIFY(capacityText().contains("without a duration"));
        // Hide it: the section and the capacity line go, the empty state returns.
        QVERIFY(tk::selectRow(app, onePlannedTask()));
        app.press(Qt::Key_H);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(onePlannedTask()) == nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(sectionTitles().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("myDayCapacity"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(emptyStateText(), QString("Nothing planned for today"), 5000);
    }

    // On an empty day the capacity line hides (planned and without-duration are
    // both zero); the moment a duration lands in the plan it shows again.
    void capacityZeroStateHidesAndReturns() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedBareWorld);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QCOMPARE(app.backend().myDayCapacityMap().value("plannedMinutes").toInt(), 0);
        QCOMPARE(app.backend().myDayCapacityMap().value("withoutDuration").toInt(), 0);
        QVERIFY(!findItem("myDayCapacity"));
        // Add a due-today task with a duration: the line appears.
        Task t;
        t.id = app.store().nextId("tasks");
        t.listId = app.ids.home;
        t.title = "Half an hour";
        t.dueDate = today;
        t.duration = 30;
        t.order = t.id;
        app.store().begin("add");
        app.store().putTask(t);
        app.store().commit();
        QQuickItem *cap = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((cap = findItem("myDayCapacity")) != nullptr, 5000);
        QVERIFY(cap->property("visible").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(capacityText().contains("30m of tasks"), 5000);
    }

    // The title and the date share one baseline, and the Wrap up day link ends
    // the capacity line below, on that line's baseline and inside the pane, at
    // the default window and at a narrow one.
    void headerFitsAtEveryWidth() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedOnePlanned);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("myDayCapacity") != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("myDayReview") != nullptr, 5000);
        auto rect = [](QQuickItem *i) { return i->mapRectToScene(QRectF(0, 0, i->width(), i->height())); };
        auto assertFits = [&](const char *where) {
            QTest::qWait(300);
            QQuickItem *title = findItem("paneTitle");
            QQuickItem *date = findItem("paneDate");
            QQuickItem *review = findItem("myDayReview");
            QQuickItem *cap = findItem("myDayCapacity");
            QQuickItem *line = findItem("myDayLine");
            QVERIFY2(title && date && review && cap && line, where);
            const QRectF t = rect(title), r = rect(review), c = rect(cap), l = rect(line);
            QVERIFY2(qAbs(baselineSceneY(title) - baselineSceneY(date)) < 1.5, where);
            QVERIFY2(qAbs(baselineSceneY(review) - baselineSceneY(cap)) < 1.5, where);
            QVERIFY2(r.left() >= c.right() - 0.5, qPrintable(QString("%1: link at %2, numbers end at %3").arg(where).arg(r.left()).arg(c.right())));
            QVERIFY2(r.top() >= t.bottom() - 8, where);
            QVERIFY2(r.right() <= l.right() + 0.5, qPrintable(QString("%1: link ends at %2, pane at %3").arg(where).arg(r.right()).arg(l.right())));
            QVERIFY2(c.top() >= t.bottom() - 8, qPrintable(QString("%1: capacity line not below the title").arg(where)));
            QVERIFY2(c.right() <= l.right() + 0.5, qPrintable(QString("%1: capacity line runs past the pane").arg(where)));
        };
        assertFits("default window");
        app.window->resize(900, 760);
        assertFits("900 wide");
    }

    // On Today the title and the date share a baseline too.
    void todayTitleAndDateShareABaseline() {
        App app(seedOnePlanned);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("today"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("paneDate") != nullptr && findItem("paneDate")->isVisible(), 5000);
        for (const QSize &size : {QSize(0, 0), QSize(900, 760)}) {
            if (size.width() > 0)
                app.window->resize(size);
            QTest::qWait(300);
            QQuickItem *title = findItem("paneTitle");
            QQuickItem *date = findItem("paneDate");
            QVERIFY(title && date);
            QVERIFY2(qAbs(baselineSceneY(title) - baselineSceneY(date)) < 1.5,
                     qPrintable(QString("%1 vs %2").arg(baselineSceneY(title)).arg(baselineSceneY(date))));
        }
    }

    // Wrap up day and Back to plan are muted links in the numbers line: that
    // line's size and colour, a pointing hand, an accessible name, and they work
    // by mouse and by keyboard. With nothing planned the link still shows.
    void reviewLinksAreMutedAndWorkByMouseAndKeyboard() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedOnePlanned);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QQuickItem *review = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((review = findItem("myDayReview")) != nullptr, 5000);
        QQuickItem *cap = findItem("myDayCapacity");
        QQuickItem *line = findItem("myDayLine");
        QVERIFY(cap && line);
        QVERIFY(line->isAncestorOf(review));
        // The line's own muted colour and size (the first text is "0m of tasks", unwarned).
        QQuickItem *firstText = cap->childItems().first();
        const QColor muted = firstText->property("color").value<QColor>();
        QCOMPARE(review->property("color").value<QColor>(), muted);
        QCOMPARE(review->property("font").value<QFont>().pixelSize(), firstText->property("font").value<QFont>().pixelSize());
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(review);
        QVERIFY(iface);
        QCOMPARE(iface->text(QAccessible::Name), QString("Wrap up day"));
        QCOMPARE(iface->role(), QAccessible::Button);
        QVERIFY(review->property("activeFocusOnTab").toBool());
        // By mouse.
        QVERIFY(tk::clickNamed("myDayReview"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
        QQuickItem *finish = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((finish = findItem("myDayFinish")) != nullptr && finish->isVisible(), 5000);
        QVERIFY(line->isAncestorOf(finish));
        QVERIFY(findItem("myDaySummary")->isVisible());
        QCOMPARE(finish->property("color").value<QColor>(), muted);
        QVERIFY(tk::clickNamed("myDayFinish"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().myDayShutdown(), 5000);
        // By keyboard: focus the link, then Space and Return.
        QTRY_VERIFY_WITH_TIMEOUT((review = findItem("myDayReview")) != nullptr && review->isVisible(), 5000);
        review->forceActiveFocus();
        QVERIFY(review->hasActiveFocus());
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT((finish = findItem("myDayFinish")) != nullptr && finish->isVisible(), 5000);
        finish->forceActiveFocus();
        QVERIFY(finish->hasActiveFocus());
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().myDayShutdown(), 5000);
    }

    // With nothing planned there are no numbers, and the Wrap up day link stands
    // alone in the line.
    void reviewLinkShowsWithoutNumbers() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedBareWorld);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QQuickItem *review = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((review = findItem("myDayReview")) != nullptr && review->isVisible(), 5000);
        QVERIFY(findItem("myDayCapacity") == nullptr || !findItem("myDayCapacity")->isVisible());
        QQuickItem *line = findItem("myDayLine");
        QVERIFY(line && line->isVisible() && line->isAncestorOf(review));
        QVERIFY(tk::clickNamed("myDayReview"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
    }

    // The right pane's Day title only shows with calendar sources; without any
    // an empty My Day leaves no floating "Day" behind.
    void utilityDayTitleFollowsCalendarSources() {
        QTemporaryDir dir;
        App app(seedBareWorld);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        // No sources: neither the title nor the timeline.
        QTRY_VERIFY_WITH_TIMEOUT(findItem("utilityDayTitle") == nullptr, 5000);
        QVERIFY(findItem("myDayTimeline") == nullptr);
        // A local .ics, as the calendar tests write: both come back.
        const QString path = writeCalendar(dir.path());
        QVERIFY(!path.isEmpty());
        app.services().calendar.setSources({path});
        QTRY_VERIFY_WITH_TIMEOUT(findItem("utilityDayTitle") != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("myDayTimeline") != nullptr, 5000);
        QCOMPARE(findItem("utilityDayTitle")->property("text").toString(), QString("Day"));
    }

    // The capacity line names planned and free time, and changes with a task's
    // duration, using a local .ics the way the calendar tests do.
    void capacityLineCountsTheDay() {
        QTemporaryDir dir;
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedMyDay);
        QVERIFY(app.ok);
        const QString path = writeCalendar(dir.path());
        QVERIFY(!path.isEmpty());
        app.services().calendar.setClock([] { return QDateTime(QDate::currentDate(), QTime(9, 0)); });
        app.services().calendar.setSources({path});
        QVERIFY(app.openSource("myday"));
        QTRY_VERIFY_WITH_TIMEOUT(capacityText().contains("of tasks"), 5000);
        // With no durations the planned side is zero and nothing is missing.
        QVERIFY2(capacityText().contains("0m of tasks"), qPrintable(capacityText()));
        // The event takes an hour out of the work day's window (9:00-17:00).
        const QVariantMap cap = app.backend().myDayCapacityMap();
        QCOMPARE(cap.value("freeMinutes").toInt(), 7 * 60);

        // Give the due-today task 90 minutes: the planned total moves.
        Task t = app.store().task(app.ids.buyMilk);
        t.duration = 90;
        app.store().begin("duration");
        app.store().putTask(t);
        app.store().commit();
        QTRY_VERIFY_WITH_TIMEOUT(capacityText().contains("1h 30m of tasks"), 5000);
        QCOMPARE(app.backend().myDayCapacityMap().value("plannedMinutes").toInt(), 90);
    }

    // The capacity line follows an asynchronous calendar refresh: a second
    // event appears and the free time shrinks without leaving My Day.
    void capacityFollowsCalendarRefresh() {
        QTemporaryDir dir;
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedMyDay);
        QVERIFY(app.ok);
        QVERIFY(!writeCalendar(dir.path(), "one.ics").isEmpty());
        app.services().calendar.setClock([] { return QDateTime(QDate::currentDate(), QTime(9, 0)); });
        app.services().calendar.setSources({dir.path() + "/one.ics"});
        QVERIFY(app.openSource("myday"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().myDayCapacityMap().value("freeMinutes").toInt(), 7 * 60, 5000);
        // A two-hour event replaces the one-hour one.
        QVERIFY(!writeCalendar(dir.path(), "two.ics", 12).isEmpty());
        app.services().calendar.setSources({dir.path() + "/two.ics"});
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().myDayCapacityMap().value("freeMinutes").toInt(), 6 * 60, 5000);
    }

    // Overdue is capped at seven with an "n more" row that expands it, and
    // Suggestions has its "Show more".
    void overdueCapExpands() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedManyOverdue);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QQuickItem *more = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((more = findItem("myDayMore:overdue")) != nullptr, 5000);
        const QString moreText = findIn(more, [](QQuickItem *i) {
            return i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0;
        })->property("text").toString();
        // The world has ten overdue (nine seeded plus electricity).
        QVERIFY2(moreText.endsWith("more"), qPrintable(moreText));
        QCOMPARE(moreText, QString("3 more"));
        click(more);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("myDayMore:overdue"), 5000);
        // Every overdue task is now on screen.
        QVERIFY(app.row(app.ids.electricity));
    }

    void suggestionsShowMore() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        // A world whose suggestions run past fifteen: twenty starred tasks.
        App app([](Store &store) {
            Seed s = seedWorld(store);
            store.begin("many suggestions", false);
            for (int i = 0; i < 20; ++i) {
                Task t;
                t.id = store.nextId("tasks");
                t.listId = s.home;
                t.title = QStringLiteral("Star %1").arg(i);
                t.starred = true;
                t.order = t.id;
                store.putTask(t);
            }
            store.commit();
            return s;
        });
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QQuickItem *more = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((more = findItem("myDayMore:suggestions")) != nullptr, 5000);
        QVERIFY(tk::settled("myDayMore:suggestions"));
        const QString text = findIn(more, [](QQuickItem *i) {
            return i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0;
        })->property("text").toString();
        QCOMPARE(text, QString("Show more"));
        // Scroll the list to its end so the row is inside the viewport, then click.
        QQuickItem *list = findItem("taskList");
        QVERIFY(list);
        const int idx = app.backend().tasks()->indexOfKey("more:suggestions");
        QVERIFY(idx >= 0);
        QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, idx), Q_ARG(int, 3));
        QTRY_VERIFY_WITH_TIMEOUT((more = findItem("myDayMore:suggestions")) != nullptr, 5000);
        QTest::qWait(100);
        const QPoint at = centreOf(more);
        QTest::mouseMove(app.window, at);
        QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("myDayMore:suggestions"), 5000);
    }

    // Dropping a task row on the My Day timeline time-blocks it: it is due
    // today with a due time and the drop's duration.
    void dropOnTimelineTimeBlocks() {
        QTemporaryDir dir;
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(8, 0)));
        App app(seedMyDay);
        QVERIFY(app.ok);
        const QString path = writeCalendar(dir.path());
        QVERIFY(!path.isEmpty());
        app.services().calendar.setClock([] { return QDateTime(QDate::currentDate(), QTime(8, 0)); });
        app.services().calendar.setSources({path});
        QVERIFY(app.openSource("myday"));
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(app.ids.shelf)));
        QVERIFY(tk::selectRow(app, app.ids.shelf));
        QQuickItem *standup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((standup = findItem("dayBarGridEvent:Standup")) != nullptr, 5000);
        QVERIFY(tk::settled("dayBarGridEvent:Standup"));
        // A spot in the free time after the stand-up: about 12:30.
        QQuickItem *dropPoint = new QQuickItem(standup);
        dropPoint->setPosition(QPointF(standup->width() / 2, standup->height() + 100));
        QVERIFY2(dragShowingPill(app.row(app.ids.shelf), dropPoint, "dragProxy"), "no pill followed the pointer");
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.shelf).dueTime >= 0, 5000);
        const Task t = app.task(app.ids.shelf);
        QCOMPARE(t.dueDate, QDate::currentDate());
        QCOMPARE(t.duration, 30);
    }

    // A narrow window shows the compact Day Bar above the list instead of the
    // expanded timeline.
    void narrowWindowShowsTheCompactBar() {
        QTemporaryDir dir;
        App app(seedMyDay);
        QVERIFY(app.ok);
        const QString path = writeCalendar(dir.path());
        QVERIFY(!path.isEmpty());
        app.services().calendar.setSources({path});
        QVERIFY(app.openSource("myday"));
        // Wide: the expanded timeline is in the right pane, no compact bar.
        QVERIFY(tk::waitItem("myDayTimeline"));
        QVERIFY2(tk::waitFor([] { return findItem("dayBar") == nullptr; }), "the wide window still shows the compact bar");
        // Narrow: the utility panel hides and the compact bar appears above the list.
        QVERIFY(tk::settled("taskList"));
        app.window->resize(820, 700);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("dayBar") != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("myDayTimeline"), 5000);
        // Back to wide, and it swaps again.
        app.window->resize(1180, 760);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("myDayTimeline") != nullptr, 5000);
        QVERIFY(!findItem("dayBar"));
    }

    // The capacity line follows a work-hours change at once, without waiting
    // for a store change or the minute timer.
    void capacityFollowsAWorkWindowSettingChange() {
        QTemporaryDir dir;
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedMyDay);
        QVERIFY(app.ok);
        QVERIFY(!writeCalendar(dir.path()).isEmpty());
        app.services().calendar.setClock([] { return QDateTime(QDate::currentDate(), QTime(9, 0)); });
        app.services().calendar.setSources({dir.path() + "/myday.ics"});
        QVERIFY(app.openSource("myday"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().myDayCapacityMap().value("freeMinutes").toInt(), 7 * 60, 5000);
        // The window starts at 10:00 now: the 10:00-11:00 event still takes an
        // hour, but an hour less of the day is free.
        app.backend().setSetting("myday.workStart", 10 * 60);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().myDayCapacityMap().value("freeMinutes").toInt(), 6 * 60, 5000);
    }

    // A typed search filters every section in My Day, as it does in a list.
    void searchFiltersTheSections() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedMyDay);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(app.ids.buyMilk)));
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(app.ids.electricity)));
        app.backend().setSearch("milk");
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.buyMilk) != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.electricity) == nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.shelf) == nullptr, 5000);
        app.backend().setSearch(QString());
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.electricity) != nullptr, 5000);
    }

    // My Day orders its own sections, so it shows no sort controls, and a
    // tag search cannot sit over the timeline.
    void myDayShowsNoSortControls() {
        App app(seedMyDay);
        QVERIFY(app.ok);
        // Start a tag search on a list first, then enter My Day.
        QVERIFY(app.openSource("all"));
        QVERIFY(tk::waitItem("sortButton"));
        app.press(Qt::Key_Slash);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("tagSearch") != nullptr, 5000);
        QVERIFY(app.openSource("myday"));
        QCOMPARE(findItem("sortButton"), nullptr);
        QCOMPARE(findItem("sortReverse"), nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("tagSearch") == nullptr, 5000);
        // Leaving My Day brings them back.
        QVERIFY(app.openSource("all"));
        QVERIFY(tk::waitItem("sortButton"));
    }

    // A moves a suggestion to Already today. A task whose deadline is later
    // keeps it and gains start = today instead.
    void addKeyMovesASuggestionToToday() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedPlanKeys);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        const PlanWorld &w = planWorld();
        revealTaskInList(app, w.suggestionStar);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.suggestionStar) != nullptr, 5000);
        QVERIFY(tk::selectRow(app, w.suggestionStar));
        app.press(Qt::Key_A);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(w.suggestionStar).dueDate, today, 5000);
        // It is now in Already today, with no reason chip.
        QTRY_COMPARE_WITH_TIMEOUT(reasonOf(w.suggestionStar), QString(), 5000);

        // The later-deadline task keeps its deadline and starts today.
        revealTaskInList(app, w.suggestionLater);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.suggestionLater) != nullptr, 5000);
        QVERIFY(tk::selectRow(app, w.suggestionLater));
        app.press(Qt::Key_A);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(w.suggestionLater).startDate, today, 5000);
        QCOMPARE(app.task(w.suggestionLater).dueDate, today.addDays(2));

        // Ctrl+Z undoes each, last change first.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(w.suggestionLater).startDate.isValid(), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(w.suggestionStar).dueDate != today, 5000);
    }

    // B blocks three selected tasks into consecutive free slots around a
    // seeded calendar event; a task with no duration gets 30 minutes.
    void blockKeyPacksThreeTasksIntoConsecutiveSlots() {
        QTemporaryDir dir;
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedBlockKeys);
        QVERIFY(app.ok);
        const QString path = writeCalendar(dir.path());
        QVERIFY(!path.isEmpty());
        app.services().calendar.setClock([today] { return QDateTime(today, QTime(9, 0)); });
        app.services().calendar.setSources({path});
        QVERIFY(app.openSource("myday"));
        const BlockWorld &w = blockWorld();
        // Select all three in order: one, two, three.
        revealTaskInList(app, w.first);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.first) != nullptr, 5000);
        QVERIFY(tk::selectRow(app, w.first));
        tk::clickWith(app.row(w.second), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        tk::clickWith(app.row(w.third), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 3, 5000);
        app.press(Qt::Key_B);
        // The 60-minute block fills 9:00-10:00 (the event is 10:00-11:00), the
        // 45-minute one starts at 11:00, and the 30-minute default at 11:45:
        // each task goes into the next free slot, in selection order.
        auto blocked = [&](qint64 id) { return app.task(id).dueTime >= 0 && app.task(id).dueDate == today; };
        QTRY_VERIFY_WITH_TIMEOUT(blocked(w.first), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(blocked(w.second), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(blocked(w.third), 5000);
        QCOMPARE(app.task(w.first).dueTime, 10 * 60);
        QCOMPARE(app.task(w.first).duration, 60);
        QCOMPARE(app.task(w.second).dueTime, 11 * 60 + 45);
        QCOMPARE(app.task(w.second).duration, 45);
        QCOMPARE(app.task(w.third).duration, 30);
        QCOMPARE(app.task(w.third).dueTime, 12 * 60 + 15);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!blocked(w.third), 5000);
    }

    // R moves a today task to tomorrow, and says so in the status line.
    void removeKeyPushesTodayToTomorrow() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedPlanKeys);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        const qint64 id = planWorld().alreadyToday;
        revealTaskInList(app, id);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) != nullptr, 5000);
        QVERIFY(tk::selectRow(app, id));
        app.press(Qt::Key_R);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().statusMessage(), QString("Moved to tomorrow"), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today, 5000);
    }

    // H hides a suggestion for today; the next date brings it back.
    void hideKeyHidesForTodayOnly() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedPlanKeys);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        const qint64 id = planWorld().suggestionStar;
        revealTaskInList(app, id);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) != nullptr, 5000);
        QVERIFY(tk::selectRow(app, id));
        app.press(Qt::Key_H);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) == nullptr, 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) != nullptr, 5000);

        // Hide again, then move the pinned clock to tomorrow: it is back.
        QVERIFY(tk::selectRow(app, id));
        app.press(Qt::Key_H);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) == nullptr, 5000);
        MyDay::setClock([today] { return QDateTime(today.addDays(1), QTime(9, 0)); });
        app.backend().selectSource("today");
        app.backend().selectSource("myday");
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) != nullptr, 5000);
    }

    // The plan keys do nothing outside My Day: A, B, R and H leave a list alone.
    void planKeysDoNothingOutsideMyDay() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedPlanKeys);
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = planWorld().suggestionStar;
        QVERIFY(tk::selectRow(app, id));
        const Task before = app.task(id);
        for (Qt::Key k : {Qt::Key_A, Qt::Key_B, Qt::Key_R, Qt::Key_H}) {
            app.press(k);
            QTest::qWait(50);
            const Task after = app.task(id);
            QCOMPARE(after.dueDate, before.dueDate);
            QCOMPARE(after.startDate, before.startDate);
            QCOMPARE(after.dueTime, before.dueTime);
            QCOMPARE(after.duration, before.duration);
            QVERIFY(app.row(id) != nullptr);
        }
        QVERIFY(app.store().setting("myday.hidden").toString().isEmpty());
    }

    // The prompt shows once on the first show of the day when it is enabled.
    // Esc and Return both mark the day planned; Return opens plan mode.
    void promptIsAskedOnceADay() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        Session s;
        s.seed = seedMyDayPromptOn;
        s.lastView = false;
        QVERIFY(s.start());
        App &app = *s;
        QObject *prompt = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((prompt = findObject("myDayPrompt")) != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(prompt->property("visible").toBool(), 5000);
        // Return opens My Day and marks the day planned.
        QTest::keyClick(app.window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!prompt->property("visible").toBool(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("myday"), 5000);
        QCOMPARE(MyDay::lastPlanned(app.store()), today);

        // A relaunch the same day is quiet.
        QVERIFY(s.restart());
        QVERIFY(findObject("myDayPrompt") != nullptr);
        QTest::qWait(300);
        QVERIFY(!findObject("myDayPrompt")->property("visible").toBool());
    }

    // Esc declines the prompt and still marks the day planned, without opening
    // My Day. With the setting off, or the day already planned, it never asks.
    void promptEscapeAndTheOffCases() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        {
            Session s;
            s.seed = seedMyDayPromptOn;
            s.lastView = false;
            QVERIFY(s.start());
            App &app = *s;
            QObject *prompt = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((prompt = findObject("myDayPrompt")) != nullptr, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(prompt->property("visible").toBool(), 5000);
            const QString before = app.backend().sourceKey();
            QTest::keyClick(app.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!prompt->property("visible").toBool(), 5000);
            QCOMPARE(MyDay::lastPlanned(app.store()), today);
            QCOMPARE(app.backend().sourceKey(), before);
            QVERIFY(!app.backend().sourceIsMyDay());
        }
        {
            // Prompt off: never shown.
            Session s;
            s.seed = [](Store &st) {
                Seed ids = seedMyDay(st);
                st.setSetting("myday.prompt", false);
                return ids;
            };
            s.lastView = false;
            QVERIFY(s.start());
            QVERIFY(findObject("myDayPrompt") != nullptr);
            QTest::qWait(300);
            QVERIFY(!findObject("myDayPrompt")->property("visible").toBool());
            QVERIFY(!MyDay::lastPlanned(s->store()).isValid());
        }
        {
            // Already planned today: never shown.
            Session s;
            s.seed = seedMyDayPromptPlanned;
            s.lastView = false;
            QVERIFY(s.start());
            QVERIFY(findObject("myDayPrompt") != nullptr);
            QTest::qWait(300);
            QVERIFY(!findObject("myDayPrompt")->property("visible").toBool());
        }
    }

    // ------------------------------------------------------ shutdown review

    void shutdownModeOpensAfterWorkEndAndListsOnlyUnfinished() {
        const QDate today = QDate::currentDate();
        // Half past five: the work day is over.
        ClockPin pin(QDateTime(today, QTime(17, 30)));
        App app(seedShutdown, QString(), false, false);
        QVERIFY(app.ok);
        // After the work day's end the shortcut opens the review, not the plan.
        QVERIFY(app.backend().sourceKey() != QString("myday"));
        app.press(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("myday"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
        QCOMPARE(paneTitle(), QString("My Day"));
        const ShutdownWorld &w = shutdownWorld();
        // The review lists only the unfinished today tasks; the completed ones
        // and the overdue of other days are not in it.
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.openLong) != nullptr, 5000);
        QVERIFY(app.row(w.openUndated) != nullptr);
        QVERIFY(app.row(w.doneToday) == nullptr);
        QVERIFY(app.row(w.doneYesterday) == nullptr);
        QVERIFY(app.row(w.abandonedToday) == nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(sectionTitles(), (QStringList{"NOT DONE YET"}), 5000);
        // The summary line replaces the capacity line.
        QQuickItem *summary = findItem("myDaySummary");
        QVERIFY(summary);
        QVERIFY(summary->property("visible").toBool());
        QCOMPARE(findItem("myDayCapacity"), nullptr);
        QStringList parts;
        findIn(summary, [&](QQuickItem *i) {
            if (i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0)
                parts << i->property("text").toString();
            return false;
        });
        // The task abandoned today is neither counted nor timed as done.
        QCOMPARE(parts.join(' '), QString("Done today: 2 tasks · 45m of 1h 30m"));
        // Finish (Return) leaves the review for the plan.
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().myDayShutdown(), 5000);
        QVERIFY(!sectionTitles().contains("NOT DONE YET"));
        QVERIFY(sectionTitles().contains("TODAY"));
        QVERIFY(findItem("myDaySummary") == nullptr);
        QQuickItem *capacity = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((capacity = findItem("myDayCapacity")) != nullptr, 5000);
        QVERIFY(capacity->property("visible").toBool());
    }

    // The header wording: Wrap up day / Back to plan, the capacity line while
    // the work day lasts and once it is over, and the summary's singular.
    void headerWording() {
        const QDate today = QDate::currentDate();
        {
            ClockPin pin(QDateTime(today, QTime(9, 0)));
            App app(seedOnePlanned);
            QVERIFY(app.ok);
            QVERIFY(app.openSource("myday"));
            QTRY_VERIFY_WITH_TIMEOUT(findItem("myDayReview") != nullptr, 5000);
            QCOMPARE(findItem("myDayReview")->property("text").toString(), QString("Wrap up day"));
            QTRY_VERIFY_WITH_TIMEOUT(capacityText().contains("left in work day"), 5000);
            QVERIFY2(capacityText().startsWith("0m of tasks · ") && capacityText().endsWith(" left in work day · 1 without a duration"),
                     qPrintable(capacityText()));
            QVERIFY(tk::clickNamed("myDayReview"));
            QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(findItem("myDayFinish") != nullptr, 5000);
            QCOMPARE(findItem("myDayFinish")->property("text").toString(), QString("Back to plan"));
            QQuickItem *summary = findItem("myDaySummary");
            QVERIFY(summary);
            auto summaryText = [&] {
                QStringList parts;
                findIn(summary, [&](QQuickItem *i) {
                    if (i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0)
                        parts << i->property("text").toString();
                    return false;
                });
                return parts.join(' ');
            };
            // No task done yet: zero is plural.
            QTRY_COMPARE_WITH_TIMEOUT(summaryText(), QString("Done today: 0 tasks · 0m of 0m"), 5000);
            Task t = app.store().task(onePlannedTask());
            t.completed = true;
            t.completedAt = QDateTime(today, QTime(9, 0));
            t.duration = 20;
            app.store().begin("done");
            app.store().putTask(t);
            app.store().commit();
            QTRY_COMPARE_WITH_TIMEOUT(summaryText(), QString("Done today: 1 task · 20m of 0m"), 5000);
        }
        {
            // Half past five: no window is left.
            ClockPin pin(QDateTime(today, QTime(17, 30)));
            App app(seedOnePlanned, QString(), false, false);
            QVERIFY(app.ok);
            QVERIFY(app.openSource("myday"));
            QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
            QVERIFY(tk::clickNamed("myDayFinish"));
            QTRY_VERIFY_WITH_TIMEOUT(!app.backend().myDayShutdown(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(capacityText(), QString("0m of tasks · work day over · 1 without a duration"), 5000);
        }
    }

    // "Wrap up day" opens the review at any hour; before workEnd the shortcut
    // still opens the plan.
    void reviewTodayButtonOpensTheReviewBeforeWorkEnd() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedPlanKeys);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("myday"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().myDayShutdown(), 5000);
        QVERIFY(tk::clickNamed("myDayReview"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
        QVERIFY(sectionTitles().contains("NOT DONE YET"));
        QVERIFY(findItem("myDayReview") == nullptr);
        // Leaving My Day and coming back before workEnd is the plan again.
        QVERIFY(app.openSource("all"));
        QVERIFY(app.openSource("myday"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().myDayShutdown(), 5000);
    }

    // In the review Y moves to tomorrow, D defers through the sheet, and R
    // sends the task back to tomorrow as "remove from My Day" does; Ctrl+Z
    // undoes each.
    void reviewKeysMoveDeferAndRemoveWithUndo() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(9, 0)));
        App app(seedPlanKeys);
        QVERIFY(app.ok);
        const qint64 id = planWorld().alreadyToday;
        QVERIFY(app.openSource("myday"));
        QVERIFY(tk::clickNamed("myDayReview"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
        revealTaskInList(app, id);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) != nullptr, 5000);
        QVERIFY(tk::selectRow(app, id));

        // Y: tomorrow, the existing due-tomorrow key.
        app.press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) == nullptr, 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today, 5000);

        // D: the defer sheet's chips pick a date; the task leaves the day.
        QVERIFY(tk::selectRow(app, id));
        app.press(Qt::Key_D);
        QObject *popup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((popup = findObject("commandPopup")) != nullptr, 5000);
        QVERIFY(popup->property("visible").toBool());
        QCOMPARE(popup->property("kind").toString(), QString("defer"));
        // Click the chip once it has settled; the popup's chips are a Flow
        // that only places them after the popup opens.
        QVERIFY(tk::clickNamed("commandChip:2 Days"));
        // The popup must be gone before the next click: its chips stay where
        // the list's rows are, and a click meant for a row would hit one.
        QTRY_VERIFY_WITH_TIMEOUT(!popup->property("visible").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(id) == nullptr, 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today, 5000);

        // R: back to its list by pushing today to tomorrow, with the status
        // line the plan's R gives.
        QVERIFY(tk::selectRow(app, id));
        app.press(Qt::Key_R);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().statusMessage(), QString("Moved to tomorrow"), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today, 5000);
    }

    // In the review W abandons, beside Y for tomorrow: the task is closed
    // without being done, so "Done today" does not grow, and Ctrl+Z reopens it.
    void reviewAbandonKeyClosesWithoutCountingAsDone() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(17, 30)));
        App app(seedShutdown, QString(), false, false);
        QVERIFY(app.ok);
        app.press(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().myDayShutdown(), 5000);
        const ShutdownWorld &w = shutdownWorld();
        QTRY_VERIFY_WITH_TIMEOUT(app.row(w.openUndated) != nullptr && app.row(w.openLong) != nullptr, 5000);
        QCOMPARE(app.backend().myDaySummary().value("completedCount").toInt(), 2);

        QVERIFY(tk::selectRow(app, w.openUndated));
        app.press(Qt::Key_W);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(w.openUndated).abandoned, 5000);
        QVERIFY(app.task(w.openUndated).completed);
        QCOMPARE(app.backend().myDaySummary().value("completedCount").toInt(), 2);
        QCOMPARE(app.backend().myDaySummary().value("doneMinutes").toInt(), 45);
        // Y still moves the other one to tomorrow.
        QVERIFY(tk::selectRow(app, w.openLong));
        app.press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(w.openLong).dueDate, today.addDays(1), 5000);
        // Both undo, newest first.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(w.openLong).dueDate, today, 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(w.openUndated).completed && !app.task(w.openUndated).abandoned, 5000);
    }

    // The end-of-day reminder fires once after workEnd when the setting is on,
    // and never when it is off: through the alarm service's test hook.
    void reminderFiresOnceAtWorkEnd() {
        const QDate today = QDate::currentDate();
        ClockPin pin(QDateTime(today, QTime(17, 30)));
        {
            App app(seedMyDay);
            QVERIFY(app.ok);
            app.store().setSetting("myday.shutdownPrompt", true);
            int fired = 0;
            QStringList titles;
            AlarmService service(&app.store());
            QObject::connect(&service, &AlarmService::notified, [&](const QString &title, const QString &, qint64) {
                ++fired;
                titles << title;
            });
            service.checkMyDayReview();
            QCOMPARE(fired, 1);
            QCOMPARE(titles, QStringList{"Review your day"});
            QCOMPARE(MyDay::lastShutdownPrompted(app.store()), today);
            // The second tick the same day stays quiet: once a day.
            service.checkMyDayReview();
            QCOMPARE(fired, 1);
        }
        {
            // The setting off: no reminder, no bookkeeping.
            App app(seedMyDay);
            QVERIFY(app.ok);
            int fired = 0;
            AlarmService service(&app.store());
            QObject::connect(&service, &AlarmService::notified, [&](const QString &, const QString &, qint64) { ++fired; });
            service.checkMyDayReview();
            QCOMPARE(fired, 0);
            QVERIFY(!MyDay::lastShutdownPrompted(app.store()).isValid());
        }
    }

    // `fairfax ctl myday` opens a running window in My Day mode; the automation
    // command behind it selects the source.
    void ctlMyDayOpensMyDayMode() {
        App app(seedMyDay);
        QVERIFY(app.ok);
        QVERIFY(app.openSource("all"));
        QStringList keys;
        Automation automation(app.store());
        automation.selectSource = [&](const QString &k) { keys << k; };
        QVERIFY(automation.run("myday", {})["ok"].toBool());
        QCOMPARE(keys, QStringList{"myday"});
        QVERIFY(Automation::commandNames().contains("myday"));
    }
};

REGISTER_TEST(E2EMyDay)

#include "test_myday.moc"
