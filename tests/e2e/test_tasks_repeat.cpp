// End-to-end task tests, part 4: repeat rules built in the Repeat popup
// (presets, custom weekly, monthly, after completion, ends, Remove Repeat),
// what completing a repeating task does, and alerts (presets, several at
// once, days-before, at a date and time, remove one, Remove All).

#include "taskkit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::tk;

namespace {

// A world in which Buy milk (due today, in Home) carries the given rule, set
// up directly because the test is about completing it, not about building it.
std::function<Seed(Store &)> milkWith(const std::function<void(Task &)> &edit) {
    return [edit](Store &store) {
        Seed ids = seedWorld(store);
        store.begin("Seed", false);
        Task t = store.task(ids.buyMilk);
        edit(t);
        store.putTask(t);
        store.commit();
        return ids;
    };
}

QVariantMap ruleOf(App &app) { return app.backend().editor().value("recurrence").toMap(); }

bool openRepeat(App &app, qint64 id) {
    if (!openEditor(app, id)) return false;
    if (!clickNamed("editorRepeat")) return false;
    return waitItem("repeatFreq:Daily") != nullptr;
}

int liveCopies(App &app, const QString &title) {
    int n = 0;
    for (const Task &t : app.store().tasks())
        if (!t.deleted && t.title == title) ++n;
    return n;
}

// Clicks the up arrow of a number box in the Repeat popup.
void bumpBox(const QString &name) {
    QQuickItem *box = findItem(name);
    QVERIFY2(box, qPrintable(name));
    click(box, Qt::LeftButton, pointIn(box, 0.93, 0.5));
}

}  // namespace

class E2ETasksRepeat : public QObject {
    Q_OBJECT

    static QString homeKey(App &app) { return "list:" + QString::number(app.ids.home); }

private slots:
    // "Six presets cover the common schedules... Daily, Weekly, Every 2 weeks,
    // Monthly, Quarterly, Yearly" and "Remove Repeat" (repeating-tasks.md); the
    // row shows the repeat arrow while the rule is set and drops it after.
    void presetsAndRemoveRepeat() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openRepeat(app, id));
        struct Preset { const char *name; int freq, interval; };
        for (const Preset &p : {Preset{"Daily", 1, 1}, Preset{"Weekly", 2, 1}, Preset{"Every 2 weeks", 2, 2},
                                Preset{"Monthly", 3, 1}, Preset{"Quarterly", 3, 3}, Preset{"Yearly", 4, 1}}) {
            QVERIFY(clickNamed(QString("repeatPreset:") + p.name));
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.frequency, p.freq, 3000);
            QCOMPARE(app.task(id).recurrence.interval, p.interval);
            QVERIFY(!prop("repeatSummary", "text").toString().isEmpty());
            QVERIFY(prop("repeatSummary", "text").toString() != "Never");
            // Remove Repeat clears it, so the next preset chip is on offer.
            QVERIFY(clickNamed("repeatRemove"));
            QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).recurrence.isSet(), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(prop("repeatSummary", "text").toString(), QString("Never"), 3000);
        }
        QVERIFY(clickNamed("repeatPreset:Weekly"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.frequency, 2, 3000);
        app.press(Qt::Key_Escape);                      // Close the popup.
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("repeatFreq:Daily"), 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowRepeat", id), 3000);

        QVERIFY(openRepeat(app, id));
        QVERIFY(clickNamed("repeatRemove"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).recurrence.isSet(), 3000);
        app.press(Qt::Key_Escape);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowRepeat", id), 3000);
    }

    // "Alt+1...6" chips and the Repeat shortcut: Ctrl+; opens the popup on the
    // selected task and Alt+3 starts "Every 2 weeks".
    void repeatShortcutAndAltChips() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(waitFor([&] { return app.row(id) != nullptr; }));
        click(app.row(id), Qt::LeftButton, pointIn(app.row(id), 0.4, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), id, 2000);
        app.press(Qt::Key_Semicolon, Qt::ControlModifier);
        QVERIFY2(waitItem("editorCard"), "Ctrl+; did not open the editor");
        QVERIFY(waitItem("repeatFreq:Daily"));
        app.press(Qt::Key_3, Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.frequency, 2, 3000);
        QCOMPARE(app.task(id).recurrence.interval, 2);
    }

    // "On some days of the week: tick any combination of Mon through Sun",
    // "every N weeks", "after completion" and "Ends: After a number of
    // occurrences" (repeating-tasks.md).
    void customWeeklyRule() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openRepeat(app, id));
        QVERIFY(clickNamed("repeatFreq:Weekly"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.frequency, 2, 3000);
        // Weekly has just added the weekday row; until it is laid out, its
        // toggles sit at stale positions and a click hits the wrong day.
        QVERIFY(waitItem("repeatWeekday:0"));
        settleLayout(findItem("repeatWeekday:0"));
        // Exactly Mon, Wed, Fri: switch on what is missing, then off what is extra.
        const int want = 1 | 4 | 16;
        for (int pass = 0; pass < 2; ++pass)
            for (int day = 0; day < 7; ++day) {
                const bool on = (ruleOf(app).value("weekdays").toInt() >> day) & 1;
                const bool wanted = (want >> day) & 1;
                if (pass == 0 && wanted && !on) click(findItem(QString("repeatWeekday:%1").arg(day)));
                if (pass == 1 && !wanted && on) click(findItem(QString("repeatWeekday:%1").arg(day)));
                QTRY_COMPARE_WITH_TIMEOUT(((app.task(id).recurrence.weekdays >> day) & 1) != 0,
                                          (pass == 0 ? (on || wanted) : wanted), 3000);
            }
        QCOMPARE(app.task(id).recurrence.weekdays, want);
        // Every 3 weeks.
        bumpBox("repeatInterval");
        bumpBox("repeatInterval");
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.interval, 3, 3000);
        // After completion.
        click(findItem("repeatAfterCompletion"), Qt::LeftButton, pointIn(findItem("repeatAfterCompletion"), 0.04, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.from, int(Repeat::FromCompletion), 3000);
        // End after 4 times.
        click(findItem("repeatEndAfter"), Qt::LeftButton, pointIn(findItem("repeatEndAfter"), 0.04, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.endType, int(Repeat::AfterCount), 3000);
        for (int i = 0; i < 3; ++i) bumpBox("repeatEndCount");
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.endCount, 4, 3000);
        QCOMPARE(app.task(id).recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(app.task(id).recurrence.interval, 3);
        QCOMPARE(app.task(id).recurrence.weekdays, want);

        // Reverse each in turn: from the due date, never ending.
        click(findItem("repeatAfterCompletion"), Qt::LeftButton, pointIn(findItem("repeatAfterCompletion"), 0.04, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.from, int(Repeat::FromDue), 3000);
        click(findItem("repeatEndNever"), Qt::LeftButton, pointIn(findItem("repeatEndNever"), 0.04, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.endType, int(Repeat::Never), 3000);
        QVERIFY(clickNamed("repeatRemove"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).recurrence.isSet(), 3000);
    }

    // Monthly: on the same date, or "the 1st, 2nd... or Last of a chosen
    // weekday", and "End on date" picked from the calendar.
    void monthlyAndEndOnDate() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openRepeat(app, id));
        QVERIFY(clickNamed("repeatFreq:Monthly"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.frequency, 3, 3000);
        QCOMPARE(app.task(id).recurrence.monthOrdinal, 0);

        // "The Nth weekday": starts from the task's own weekday.
        click(findItem("repeatMonthNth"), Qt::LeftButton, pointIn(findItem("repeatMonthNth"), 0.04, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.monthOrdinal, 1, 3000);
        QCOMPARE(app.task(id).recurrence.monthWeekday, QDate::currentDate().dayOfWeek());
        click(findItem("repeatOrdinal"));
        QVERIFY(waitFor([] { return findText("Last") != nullptr; }));
        click(findText("Last"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.monthOrdinal, -1, 3000);
        // Back to the fixed date.
        click(findItem("repeatMonthDay"), Qt::LeftButton, pointIn(findItem("repeatMonthDay"), 0.04, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.monthOrdinal, 0, 3000);

        // Let the ordinal drop-down finish closing, or it takes the next click.
        QVERIFY(waitFor([] {
            QObject *popup = findItem("repeatOrdinal")->property("popup").value<QObject *>();
            return popup && !popup->property("visible").toBool();
        }));

        // End on a date chosen in the calendar.
        // The calendar opens on this month; the 10th of next month is one click on.
        const QDate nextMonth = QDate::currentDate().addMonths(1);
        const QDate end(nextMonth.year(), nextMonth.month(), 10);
        QVERIFY(clickNamed("repeatEndDate"));
        QVERIFY(waitItem("pickerNext"));
        click(findItem("pickerNext"));
        QQuickItem *cell = waitItem("pickerDay:" + isoOf(end));
        QVERIFY(cell);
        click(cell);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.endType, int(Repeat::OnDate), 3000);
        QCOMPARE(app.task(id).recurrence.endDate, end);
        // The calendar must finish closing, or it takes the next click.
        QVERIFY(waitFor([] { return !findItem("pickerNext"); }));
        click(findItem("repeatEndNever"), Qt::LeftButton, pointIn(findItem("repeatEndNever"), 0.04, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).recurrence.endType, int(Repeat::Never), 3000);
    }

    // "When you complete a repeating task and another occurrence remains, the reference app
    // keeps a completed copy for your history and moves the active task to the
    // next occurrence." Daily, due today: tomorrow next; Done shows the copy.
    void completingDailyMovesDueAndKeepsACopy() {
        App app(milkWith([](Task &t) { t.recurrence.frequency = Repeat::Daily; }));
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        const QDate today = QDate::currentDate();
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowRepeat", id), 3000);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 3000);
        QVERIFY(!app.task(id).completed);
        QCOMPARE(liveCopies(app, "Buy milk"), 2);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(today.addDays(1))), 3000);

        // The completed occurrence is in Done and does not repeat.
        app.press(Qt::Key_5, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("done"), 2000);
        qint64 copy = 0;
        for (const Task &t : app.store().tasks())
            if (!t.deleted && t.title == "Buy milk" && t.id != id) copy = t.id;
        QVERIFY(copy);
        QVERIFY(app.task(copy).completed);
        QVERIFY(!app.task(copy).recurrence.isSet());
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, copy), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!rowVisible(app, id), 3000);

        // One undo puts everything back as it was.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(liveCopies(app, "Buy milk"), 1, 3000);
    }

    // Weekly on Monday, Wednesday and Friday: the next occurrence is the next
    // of those days after the due date.
    void completingWeeklyOnChosenDays() {
        const QDate today = QDate::currentDate();
        App app(milkWith([](Task &t) {
            t.recurrence.frequency = Repeat::Weekly;
            t.recurrence.weekdays = 1 | 4 | 16;
            t.dueDate = QDate::currentDate();
        }));
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QDate expect = today.addDays(1);
        while (!(expect.dayOfWeek() == 1 || expect.dayOfWeek() == 3 || expect.dayOfWeek() == 5)) expect = expect.addDays(1);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, expect, 3000);
        QVERIFY(!app.task(id).completed);
    }

    // Monthly: the same date next month.
    void completingMonthly() {
        const QDate today = QDate::currentDate();
        App app(milkWith([](Task &t) { t.recurrence.frequency = Repeat::Monthly; }));
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(app.ids.buyMilk)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.buyMilk).dueDate, today.addMonths(1), 3000);
    }

    // "Repeat from Completion: the next occurrence is calculated from the date
    // you marked the task complete." A task due five days ago that repeats
    // every 3 days after completion is next due three days from today.
    void completingAfterCompletion() {
        const QDate today = QDate::currentDate();
        App app(milkWith([](Task &t) {
            t.dueDate = QDate::currentDate().addDays(-5);
            t.recurrence.frequency = Repeat::Daily;
            t.recurrence.interval = 3;
            t.recurrence.from = Repeat::FromCompletion;
        }));
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("overdue"), 3000);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(3), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(today.addDays(3))), 3000);
    }

    // "If the task has reached its End after count... the final occurrence
    // simply completes."
    void endAfterCountStopsTheSeries() {
        const QDate today = QDate::currentDate();
        App app(milkWith([](Task &t) {
            t.recurrence.frequency = Repeat::Daily;
            t.recurrence.endType = Repeat::AfterCount;
            t.recurrence.endCount = 2;
        }));
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 3000);
        QVERIFY(!app.task(id).completed);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).completed, 3000);
        QCOMPARE(app.task(id).dueDate, today.addDays(1));  // No third occurrence.
    }

    // End on a date: the occurrence that would fall after it is not made.
    void endOnDateStopsTheSeries() {
        const QDate today = QDate::currentDate();
        App app(milkWith([](Task &t) {
            t.recurrence.frequency = Repeat::Daily;
            t.recurrence.endType = Repeat::OnDate;
            t.recurrence.endDate = QDate::currentDate().addDays(2);
        }));
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 3000);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(2), 3000);
        QVERIFY(!app.task(id).completed);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).completed, 3000);
        QCOMPARE(app.task(id).dueDate, today.addDays(2));
    }
};

class E2ETasksAlarms : public QObject {
    Q_OBJECT

    static QString homeKey(App &app) { return "list:" + QString::number(app.ids.home); }
    static QList<int> offsets(App &app, qint64 id) {
        QList<int> out;
        for (const Alarm &a : alarmsOf(app, id))
            if (a.kind == Alarm::Relative) out << a.offset;
        std::sort(out.begin(), out.end());
        return out;
    }

private slots:
    // "Alerts": presets from the alarm button, several at once, one removed with
    // its chip's cross, one toggled off in the menu, then Remove All Alerts.
    // The row shows the alarm bell while any alert is set.
    void presetsMultipleAndRemoval() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        const int before = alarmsOf(app, id).size();

        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("15 minutes before"));
        QTRY_VERIFY_WITH_TIMEOUT(offsets(app, id).contains(15), 3000);
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("1 day before"));
        QTRY_VERIFY_WITH_TIMEOUT(offsets(app, id).contains(1440), 3000);
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("At due time"));
        QTRY_VERIFY_WITH_TIMEOUT(offsets(app, id).contains(0), 3000);
        QCOMPARE(alarmsOf(app, id).size(), before + 3);
        // Each alert has its chip in the editor.
        for (const Alarm &a : alarmsOf(app, id))
            QVERIFY(waitItem(QString("editorAlarmChip:%1").arg(a.id)));

        // Remove one with its cross.
        qint64 fifteen = 0;
        for (const Alarm &a : alarmsOf(app, id))
            if (a.kind == Alarm::Relative && a.offset == 15) fifteen = a.id;
        QVERIFY(fifteen);
        click(findItem(QString("editorAlarmRemove:%1").arg(fifteen)));
        QTRY_VERIFY_WITH_TIMEOUT(!offsets(app, id).contains(15), 3000);
        QVERIFY(waitFor([&] { return !findItem(QString("editorAlarmChip:%1").arg(fifteen)); }));

        // Choosing a checked preset again switches it off.
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("1 day before"));
        QTRY_VERIFY_WITH_TIMEOUT(!offsets(app, id).contains(1440), 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowAlarm", id), 3000);

        // Remove All Alerts.
        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("Remove All Alerts"));
        QTRY_COMPARE_WITH_TIMEOUT(alarmsOf(app, id).size(), 0, 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowAlarm", id), 3000);
    }

    // "Days Before at a Time": N days before the due date at a time of day, and
    // "At a Date and Time": one fixed moment.
    void daysBeforeAndAbsolute() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;  // Due tomorrow.
        QVERIFY(openEditor(app, id));

        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("Days Before at a Time…"));
        QVERIFY(waitItem("alarmDays"));
        QQuickItem *spin = findItem("alarmDays");
        click(spin, Qt::LeftButton, pointIn(spin, 0.95, 0.5));   // 1 day -> 2 days
        QTRY_COMPARE_WITH_TIMEOUT(prop("alarmDays", "value").toInt(), 2, 2000);
        QVERIFY(waitFor([] { return findText("OK") != nullptr; }));
        click(findText("OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!alarmsOf(app, id).isEmpty(), 3000);
        Alarm daysBefore;
        for (const Alarm &a : alarmsOf(app, id))
            if (a.kind == Alarm::DaysBefore) daysBefore = a;
        QVERIFY(daysBefore.id);
        QCOMPARE(daysBefore.offset, 2);
        QCOMPARE(daysBefore.at.time(), QTime(9, 0));

        QVERIFY(waitFor([] { return findText("OK") == nullptr; }));
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("At a Date and Time…"));
        QVERIFY(waitFor([] { return findText("OK") != nullptr; }));
        click(findText("OK"));
        QTRY_COMPARE_WITH_TIMEOUT(alarmsOf(app, id).size(), 2, 3000);
        Alarm absolute;
        for (const Alarm &a : alarmsOf(app, id))
            if (a.kind == Alarm::Absolute) absolute = a;
        QVERIFY(absolute.id);
        QCOMPARE(absolute.at.date(), QDate::currentDate().addDays(1));  // The due date.
        QCOMPARE(absolute.at.time(), QTime(9, 0));

        // Both go with Remove All Alerts.
        QVERIFY(waitFor([] { return findText("OK") == nullptr; }));
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("Remove All Alerts"));
        QTRY_COMPARE_WITH_TIMEOUT(alarmsOf(app, id).size(), 0, 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowAlarm", id), 3000);
    }
};

REGISTER_TEST(E2ETasksRepeat)
REGISTER_TEST(E2ETasksAlarms)

#include "test_tasks_repeat.moc"
