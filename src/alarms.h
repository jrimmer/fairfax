#pragma once

// Delivers alarms as desktop notifications (org.freedesktop.Notifications,
// which mako shows on Omarchy), with Complete and Snooze actions. Like the reference app's
// Alarm Helper it only works while Precrime runs, which is why the app keeps
// running in the background after its window closes.

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <optional>

#include "model.h"

class Store;

namespace AlarmTimes {

// When an alarm goes off: an absolute alarm at its time; a relative one the
// offset before the due moment, where a date-only deadline counts from the
// default due time; a "days before" one at its time of day that many days
// before the date. A task with only a start date counts from that instead.
// A snooze overrides all of them. Invalid when there is nothing to fire (no
// date, completed task).
QDateTime fireTime(const Alarm &alarm, const Task &task, int defaultDueMinutes);

// Where a "days before at HH:MM" alarm keeps its time of day.
QDateTime clockTime(int minutes);

// Settings > Alerts > Default Reminders: an alert added whenever a task
// gets a due date and has none yet.
struct DefaultReminders {
    int timedOffset = -1;      // Minutes before the due time, or -1 for none.
    int dateOnlyDays = -1;     // Days before a date-only due date, or -1 for none.
    int dateOnlyMinutes = 540; // The time of day for that alert.

    static DefaultReminders load(const Store &store);
    // The alert to add for `task` (without an id), if one applies.
    std::optional<Alarm> alarmFor(const Task &task) const;
};

// Adds the default reminder to `task` unless it already has alerts. Call it
// inside the transaction that sets the due date, after the task is saved.
void applyDefaultReminder(Store &store, const Task &task);

// When a snooze action ("snooze5", "snooze-evening", ...) lands. A negative
// `morningMinutes` means no Default Due Time is in use (9:00 AM). Invalid for
// an unknown key.
QDateTime snoozeTarget(const QString &key, const QDateTime &now, int morningMinutes);

}  // namespace AlarmTimes

class AlarmService : public QObject {
    Q_OBJECT

public:
    explicit AlarmService(Store *store, QObject *parent = nullptr);

    // Alarms due at or before `now` that haven't gone off for this moment.
    QVector<Alarm> due(const QDateTime &now) const;
    void check();

signals:
    void openTask(qint64 taskId);
    void completeTask(qint64 taskId);
    // Where there's no freedesktop notification service (macOS, Windows),
    // alarms are shown through this instead, by the tray icon.
    void showFallback(const QString &title, const QString &body, qint64 taskId);

private:
    void schedule();
    void notify(const Alarm &alarm, const Task &task, const QDateTime &moment);
    void markFired(const Alarm &alarm, const QDateTime &moment);
    void snooze(qint64 alarmId, const QDateTime &until);
    void closeStale();
    int nagMinutes() const;
    int morningMinutes() const;

private slots:
    void onAction(uint id, const QString &key);
    void onClosed(uint id, uint reason);

private:
    Store *m_store;
    QTimer m_timer;
    QHash<uint, qint64> m_notifications;  // Notification id → alarm id.
    QHash<qint64, QDateTime> m_nagging;   // Alarm id → when to remind again.
    QTimer m_nagTimer;
    bool m_connected = false;
};
