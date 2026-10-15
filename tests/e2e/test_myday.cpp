// End-to-end tests for My Day: the sidebar item and its shortcut, the Default
// View setting, the screen's sections, reason chips and capacity line, the
// caps and their expanders, dropping a row on the timeline and the compact bar
// in a narrow window. My Day is on by default; a test that wants it off turns
// the setting off before the app starts.

#include <QFile>

#include "misckit.h"
#include "settingskit.h"
#include "../testregistry.h"

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
        QCOMPARE(sectionTitles(), (QStringList{"OVERDUE", "ALREADY TODAY", "SUGGESTIONS"}));
    }

    // The capacity line names planned and free time, and changes with a task's
    // duration, using a local .ics the way the calendar tests do.
    void capacityLineCountsTheDay() {
        QTemporaryDir dir;
        App app(seedMyDay);
        QVERIFY(app.ok);
        const QString path = writeCalendar(dir.path());
        QVERIFY(!path.isEmpty());
        app.services().calendar.setClock([] { return QDateTime(QDate::currentDate(), QTime(9, 0)); });
        app.services().calendar.setSources({path});
        QVERIFY(app.openSource("myday"));
        QTRY_VERIFY_WITH_TIMEOUT(capacityText().contains("planned"), 5000);
        // With no durations the planned side is zero and nothing is missing.
        QVERIFY2(capacityText().contains("0m planned"), qPrintable(capacityText()));
        // The event takes an hour out of the work day's window (9:00-17:00).
        const QVariantMap cap = app.backend().myDayCapacityMap();
        QCOMPARE(cap.value("freeMinutes").toInt(), 7 * 60);

        // Give the due-today task 90 minutes: the planned total moves.
        Task t = app.store().task(app.ids.buyMilk);
        t.duration = 90;
        app.store().begin("duration");
        app.store().putTask(t);
        app.store().commit();
        QTRY_VERIFY_WITH_TIMEOUT(capacityText().contains("1h 30m planned"), 5000);
        QCOMPARE(app.backend().myDayCapacityMap().value("plannedMinutes").toInt(), 90);
    }

    // The capacity line follows an asynchronous calendar refresh: a second
    // event appears and the free time shrinks without leaving My Day.
    void capacityFollowsCalendarRefresh() {
        QTemporaryDir dir;
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
};

REGISTER_TEST(E2EMyDay)

#include "test_myday.moc"
