#include <QTemporaryDir>
#include <QtTest>

#include "alarms.h"
#include "backend.h"
#include "store.h"
#include "testregistry.h"

// When alerts fire, the automatic Default Reminders, and the snooze targets.
class AlarmsTest : public QObject {
    Q_OBJECT

    static Task dated(QDate due, int dueTime = -1) {
        Task t;
        t.id = 1;
        t.dueDate = due;
        t.dueTime = dueTime;
        return t;
    }

    struct Fixture {
        QTemporaryDir dir;
        Store store;
        std::unique_ptr<Backend> backend;
        Fixture() {
            store.open(dir.filePath("t.db"));
            backend = std::make_unique<Backend>(&store);
            backend->ensureDefaults();
        }
        Task only() const {
            for (const auto &t : store.tasks())
                if (!t.deleted) return t;
            return {};
        }
    };

private slots:
    void relativeCountsBackFromTheDueTime() {
        Alarm a;
        a.offset = 30;
        QCOMPARE(AlarmTimes::fireTime(a, dated(QDate(2026, 3, 10), 14 * 60), 9 * 60), QDateTime(QDate(2026, 3, 10), QTime(13, 30)));
    }

    void dateOnlyCountsFromTheDefaultDueTime() {
        Alarm a;
        QCOMPARE(AlarmTimes::fireTime(a, dated(QDate(2026, 3, 10)), 9 * 60), QDateTime(QDate(2026, 3, 10), QTime(9, 0)));
    }

    void relativeFiresForStartOnlyTasks() {
        Task t;
        t.id = 1;
        t.startDate = QDate(2026, 3, 10);
        t.startTime = 8 * 60;
        Alarm a;
        a.offset = 60;
        QCOMPARE(AlarmTimes::fireTime(a, t, 9 * 60), QDateTime(QDate(2026, 3, 10), QTime(7, 0)));
        t.startTime = -1;
        QCOMPARE(AlarmTimes::fireTime(a, t, 9 * 60), QDateTime(QDate(2026, 3, 10), QTime(8, 0)));
    }

    void dueDateWinsOverStartDate() {
        Task t = dated(QDate(2026, 3, 12), 10 * 60);
        t.startDate = QDate(2026, 3, 10);
        Alarm a;
        QCOMPARE(AlarmTimes::fireTime(a, t, 9 * 60), QDateTime(QDate(2026, 3, 12), QTime(10, 0)));
    }

    void noDateMeansNoFireTime() {
        Task t;
        t.id = 1;
        QVERIFY(!AlarmTimes::fireTime(Alarm{}, t, 9 * 60).isValid());
    }

    void daysBeforeUsesItsOwnTimeOfDay() {
        Alarm a;
        a.kind = Alarm::DaysBefore;
        a.offset = 2;
        a.at = AlarmTimes::clockTime(8 * 60 + 15);
        // The task's own due time doesn't matter.
        QCOMPARE(AlarmTimes::fireTime(a, dated(QDate(2026, 3, 10), 17 * 60), 9 * 60), QDateTime(QDate(2026, 3, 8), QTime(8, 15)));
        Task start;
        start.id = 1;
        start.startDate = QDate(2026, 3, 10);
        QCOMPARE(AlarmTimes::fireTime(a, start, 9 * 60), QDateTime(QDate(2026, 3, 8), QTime(8, 15)));
    }

    void snoozeBeatsEverything() {
        Alarm a;
        a.snoozedUntil = QDateTime(QDate(2026, 3, 9), QTime(12, 0));
        QCOMPARE(AlarmTimes::fireTime(a, dated(QDate(2026, 3, 10)), 9 * 60), a.snoozedUntil);
    }

    void completedTasksDoNotFire() {
        Task t = dated(QDate(2026, 3, 10));
        t.completed = true;
        QVERIFY(!AlarmTimes::fireTime(Alarm{}, t, 9 * 60).isValid());
    }

    void snoozeTargets() {
        const QDateTime now(QDate(2026, 3, 10), QTime(10, 0));
        QCOMPARE(AlarmTimes::snoozeTarget("snooze5", now, -1), now.addSecs(300));
        QCOMPARE(AlarmTimes::snoozeTarget("snooze", now, -1), now.addSecs(900));
        QCOMPARE(AlarmTimes::snoozeTarget("snooze-hour", now, -1), now.addSecs(3600));
        QCOMPARE(AlarmTimes::snoozeTarget("snooze-evening", now, -1), QDateTime(now.date(), QTime(19, 0)));
        QCOMPARE(AlarmTimes::snoozeTarget("snooze-morning", now, -1), QDateTime(QDate(2026, 3, 11), QTime(9, 0)));
        QCOMPARE(AlarmTimes::snoozeTarget("snooze-morning", now, 7 * 60 + 30), QDateTime(QDate(2026, 3, 11), QTime(7, 30)));
        QVERIFY(!AlarmTimes::snoozeTarget("nonsense", now, -1).isValid());
    }

    void defaultReminderForTimedAndDateOnlyTasks() {
        AlarmTimes::DefaultReminders d;
        d.timedOffset = 15;
        d.dateOnlyDays = 1;
        d.dateOnlyMinutes = 18 * 60;
        const auto timed = d.alarmFor(dated(QDate(2026, 3, 10), 14 * 60));
        QVERIFY(timed);
        QCOMPARE(timed->kind, int(Alarm::Relative));
        QCOMPARE(timed->offset, 15);
        const auto dateOnly = d.alarmFor(dated(QDate(2026, 3, 10)));
        QVERIFY(dateOnly);
        QCOMPARE(AlarmTimes::fireTime(*dateOnly, dated(QDate(2026, 3, 10)), 9 * 60), QDateTime(QDate(2026, 3, 9), QTime(18, 0)));
        QVERIFY(!d.alarmFor(Task{}));
        d.timedOffset = -1;
        QVERIFY(!d.alarmFor(dated(QDate(2026, 3, 10), 14 * 60)));
    }

    void settingDueDateAddsTheDefaultReminderOnce() {
        Fixture f;
        f.store.setSetting("alerts.defaultTimed", 0);
        f.store.setSetting("alerts.defaultDateOnly", 0);
        f.store.setSetting("alerts.defaultDateOnlyTime", 8 * 60);
        const qint64 id = f.backend->quickAdd("Pay rent", {}, 0, 0);
        QVERIFY(f.store.alarmsFor(id).isEmpty());  // No date yet.

        f.backend->setDue(id, "2030-01-15", -1);
        auto alarms = f.store.alarmsFor(id);
        QCOMPARE(alarms.size(), 1);
        QCOMPARE(alarms[0].kind, int(Alarm::DaysBefore));
        QCOMPARE(alarms[0].offset, 0);
        QCOMPARE(alarms[0].at.time(), QTime(8, 0));

        f.backend->setDue(id, "2030-01-16", 14 * 60);  // Already has an alert: nothing more.
        QCOMPARE(f.store.alarmsFor(id).size(), 1);
    }

    void quickAddWithATimeGetsTheTimedReminder() {
        Fixture f;
        f.store.setSetting("alerts.defaultTimed", 10);
        const qint64 id = f.backend->quickAdd("Call Sam tomorrow 3pm", {}, 0, 0);
        const auto alarms = f.store.alarmsFor(id);
        QCOMPARE(alarms.size(), 1);
        QCOMPARE(alarms[0].offset, 10);
        QCOMPARE(alarms[0].kind, int(Alarm::Relative));
    }

    void explicitAlertsReplaceTheDefault() {
        Fixture f;
        f.store.setSetting("alerts.defaultTimed", 10);
        const qint64 id = f.backend->quickAdd("Call Sam tomorrow 3pm alert 30m", {}, 0, 0);
        const auto alarms = f.store.alarmsFor(id);
        QCOMPARE(alarms.size(), 1);
        QCOMPARE(alarms[0].offset, 30);
    }

    void nothingHappensWithoutTheSettings() {
        Fixture f;
        const qint64 id = f.backend->quickAdd("Call Sam tomorrow 3pm", {}, 0, 0);
        QVERIFY(f.store.alarmsFor(id).isEmpty());
    }

    void alertStyleAndDaysBeforeThroughTheBackend() {
        Fixture f;
        const qint64 id = f.backend->quickAdd("Plan trip", {}, 0, 0);
        f.backend->setDue(id, "2030-02-01", -1);
        f.backend->addDaysBeforeAlarm(id, 3, 9 * 60);
        const auto alarms = f.store.alarmsFor(id);
        QCOMPARE(alarms.size(), 1);
        QVERIFY(alarms[0].sound);
        f.backend->setAlarmSound(alarms[0].id, false);
        QVERIFY(!f.store.alarmsFor(id)[0].sound);
        QCOMPARE(AlarmTimes::fireTime(f.store.alarmsFor(id)[0], f.store.task(id), 9 * 60), QDateTime(QDate(2030, 1, 29), QTime(9, 0)));
    }

    void mutedListsKeepTheirAlarmsQuiet() {
        Fixture f;
        const qint64 id = f.backend->quickAdd("Old news", {}, 0, 0);
        f.backend->setDue(id, "2000-01-01", -1);
        f.backend->toggleAlarm(id, 0);
        AlarmService service(&f.store);
        QCOMPARE(service.due(QDateTime::currentDateTime()).size(), 1);
        f.backend->setListFlag(f.only().listId, "muteAlerts", true);
        QVERIFY(service.due(QDateTime::currentDateTime()).isEmpty());
        // The old Today exclusion no longer matters.
        f.backend->setListFlag(f.only().listId, "muteAlerts", false);
        f.backend->setListFlag(f.only().listId, "today", false);
        QCOMPARE(service.due(QDateTime::currentDateTime()).size(), 1);
    }

    void oldNagSwitchMigrates() {
        Fixture f;
        f.store.setSetting("alerts.nag", true);
        AlarmService service(&f.store);
        QCOMPARE(f.store.setting("alerts.nagMinutes").toInt(), 5);
    }

    void notesTeaserTakesTheFirstUsefulLine() {
        QCOMPARE(AlarmTimes::notesTeaser(""), "");
        QCOMPARE(AlarmTimes::notesTeaser("\n  \n## Agenda\nsecond"), "Agenda");
        QCOMPARE(AlarmTimes::notesTeaser("- [ ] bring the laptop\n- more"), "bring the laptop");
        const QString teaser = AlarmTimes::notesTeaser(QString(300, 'x'), 50);
        QCOMPARE(teaser.size(), 50);
        QVERIFY(teaser.endsWith(QStringLiteral("…")));
    }

    void actionLabelsNameTheAction() {
        Task t;
        QCOMPARE(AlarmTimes::actionLabel(t), "");
        t.actionType = ActionType::Call;
        QCOMPARE(AlarmTimes::actionLabel(t), "");  // No number to call.
        t.actionValue = "+15551234567";
        QCOMPARE(AlarmTimes::actionLabel(t), "Call");
        t.actionType = ActionType::Mail;
        QCOMPARE(AlarmTimes::actionLabel(t), "Email");
        t.actionType = ActionType::Url;
        QCOMPARE(AlarmTimes::actionLabel(t), "Open Link");
    }

    void pendingAlarmsNeedADaemon() {
        Fixture f;
        QVERIFY(!AlarmTimes::hasPending(f.store, QDateTime::currentDateTime()));
        Task t;
        t.id = f.store.nextId("tasks");
        t.listId = f.store.lists().isEmpty() ? 0 : f.store.lists().cbegin()->id;
        t.title = "Call";
        t.dueDate = QDate::currentDate().addDays(3);
        t.dueTime = 10 * 60;
        Alarm a;
        a.id = f.store.nextId("alarms");
        a.taskId = t.id;
        a.offset = 15;
        f.store.begin("alarm");
        f.store.putTask(t);
        f.store.putAlarm(a);
        f.store.commit();
        QVERIFY(AlarmTimes::hasPending(f.store, QDateTime::currentDateTime()));
        // Once it has gone off for its moment, nothing is pending.
        a.firedFor = AlarmTimes::fireTime(a, t, 9 * 60);
        f.store.begin("fired");
        f.store.putAlarm(a);
        f.store.commit();
        QVERIFY(!AlarmTimes::hasPending(f.store, QDateTime::currentDateTime()));
    }
};

REGISTER_TEST(AlarmsTest)
#include "test_alarms.moc"
