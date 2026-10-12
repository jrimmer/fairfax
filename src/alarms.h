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

#include "model.h"

class Store;

namespace AlarmTimes {

// When an alarm goes off: an absolute alarm at its time; a relative one the
// offset before the due moment, where a date-only deadline counts from the
// default due time. A snooze overrides both. Invalid when there is nothing
// to fire (no due date, completed task).
QDateTime fireTime(const Alarm &alarm, const Task &task, int defaultDueMinutes);

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

private:
    void schedule();
    void notify(const Alarm &alarm, const Task &task, const QDateTime &moment);
    void markFired(const Alarm &alarm, const QDateTime &moment);
    void snooze(qint64 alarmId, int minutes);

private slots:
    void onAction(uint id, const QString &key);

private:
    Store *m_store;
    QTimer m_timer;
    QHash<uint, qint64> m_notifications;  // Notification id → alarm id.
    bool m_connected = false;
};
