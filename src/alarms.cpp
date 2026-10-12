#include "alarms.h"

#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#endif
#include <QFileInfo>
#include <QProcess>

#include "schedule.h"
#include "store.h"

namespace {

constexpr auto Service = "org.freedesktop.Notifications";
constexpr auto Path = "/org/freedesktop/Notifications";
constexpr auto Interface = "org.freedesktop.Notifications";

}  // namespace

QDateTime AlarmTimes::fireTime(const Alarm &a, const Task &t, int defaultDueMinutes) {
    if (a.deleted || t.deleted || t.completed)
        return {};
    if (a.snoozedUntil.isValid())
        return a.snoozedUntil;
    if (a.kind == Alarm::Absolute)
        return a.at;
    if (!t.dueDate.isValid())
        return {};
    const int minutes = t.dueTime >= 0 ? t.dueTime : defaultDueMinutes;
    return QDateTime(t.dueDate, QTime(0, 0)).addSecs(qint64(minutes - a.offset) * 60);
}

AlarmService::AlarmService(Store *store, QObject *parent) : QObject(parent), m_store(store) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &AlarmService::check);
    connect(m_store, &Store::changed, this, &AlarmService::schedule);
#if defined(Q_OS_LINUX)
    m_connected = QDBusConnection::sessionBus().connect(Service, Path, Interface, "ActionInvoked", this,
                                                        SLOT(onAction(uint, QString)));
    QDBusConnection::sessionBus().connect(Service, Path, Interface, "NotificationClosed", this,
                                          SLOT(onClosed(uint, uint)));
#endif
    // Nagging, like the reference app's: an unanswered alert comes back every five minutes
    // until it's completed, snoozed or dismissed.
    m_nagTimer.setInterval(30 * 1000);
    connect(&m_nagTimer, &QTimer::timeout, this, [this] {
        const QDateTime now = QDateTime::currentDateTime();
        for (auto it = m_nagging.begin(); it != m_nagging.end();) {
            const Alarm a = m_store->alarms().value(it.key());
            const Task t = m_store->task(a.taskId);
            if (!a.id || a.deleted || t.completed || t.deleted) {
                it = m_nagging.erase(it);
                continue;
            }
            if (it.value() <= now) {
                it.value() = now.addSecs(5 * 60);
                notify(a, t, now);
            }
            ++it;
        }
    });
    m_nagTimer.start();
    // Alarms that came due while Precrime wasn't running go off now.
    QTimer::singleShot(1500, this, &AlarmService::check);
}

QVector<Alarm> AlarmService::due(const QDateTime &now) const {
    const int defaultDue = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    QVector<Alarm> result;
    for (const auto &a : m_store->alarms()) {
        const QDateTime moment = AlarmTimes::fireTime(a, m_store->task(a.taskId), defaultDue);
        if (moment.isValid() && moment <= now && a.firedFor != moment)
            result.append(a);
    }
    return result;
}

void AlarmService::check() {
    const QDateTime now = QDateTime::currentDateTime();
    const int defaultDue = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    for (const auto &a : due(now)) {
        const Task t = m_store->task(a.taskId);
        const TaskList list = m_store->list(t.listId);
        const QDateTime moment = AlarmTimes::fireTime(a, t, defaultDue);
        // Muted lists keep their alarms quiet, and very old misses aren't
        // worth a notification: they are just marked as handled.
        if (list.showInToday || !m_store->setting("alerts.respectExclusions", false).toBool()) {
            if (moment.secsTo(now) < 24 * 3600)
                notify(a, t, moment);
        }
        markFired(a, moment);
    }
    schedule();
}

void AlarmService::schedule() {
    const int defaultDue = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime next;
    for (const auto &a : m_store->alarms()) {
        const QDateTime moment = AlarmTimes::fireTime(a, m_store->task(a.taskId), defaultDue);
        if (!moment.isValid() || a.firedFor == moment)
            continue;
        if (!next.isValid() || moment < next)
            next = moment;
    }
    // Wake at the next alarm, but at least once a minute so a suspend or a
    // clock change can't make one late by more than that.
    const qint64 wait = next.isValid() ? qBound(qint64(0), now.msecsTo(next), qint64(60000)) : 60000;
    m_timer.start(int(wait));
}

void AlarmService::notify(const Alarm &a, const Task &t, const QDateTime &moment) {
    const ScheduleSettings settings;
    QString body = Schedule::dueHint(t, QDateTime::currentDateTime(), settings);
    const QString list = m_store->list(t.listId).name;
    if (!list.isEmpty())
        body = body.isEmpty() ? list : list + " · " + body;

#if !defined(Q_OS_LINUX)
    emit showFallback(t.title, body, t.id);
    Q_UNUSED(moment);
    return;
#else
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Interface, "Notify");
    QVariantMap hints{{"urgency", QVariant::fromValue<uchar>(1)}, {"category", "x-precrime.alarm"},
                      {"desktop-entry", "precrime"}};
    if (a.sound)
        hints.insert("sound-name", "alarm-clock-elapsed");
    call << QStringLiteral("Precrime") << uint(0) << QStringLiteral("precrime") << t.title << body
         << QStringList{"default", "Open", "complete", "Complete", "snooze", "Snooze 15 Minutes", "snooze-hour", "Snooze 1 Hour",
                        "snooze-evening", "This Evening", "snooze-morning", "Tomorrow Morning"}
         << hints << int(0);
    const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(call);
    if (reply.isValid())
        m_notifications.insert(reply.value(), a.id);
    else
        qWarning("precrime: couldn't show a notification: %s", qPrintable(reply.error().message()));
    if (m_store->setting("alerts.nag", false).toBool() && !m_nagging.contains(a.id))
        m_nagging.insert(a.id, QDateTime::currentDateTime().addSecs(5 * 60));

    // Notification daemons rarely play sounds themselves.
    if (a.sound) {
        const QString file = "/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga";
        if (QFileInfo::exists(file))
            QProcess::startDetached("pw-play", {file});
    }
    Q_UNUSED(moment);
#endif
}

void AlarmService::markFired(const Alarm &a, const QDateTime &moment) {
    Alarm copy = a;
    copy.firedFor = moment;
    copy.snoozedUntil = {};
    // Firing is bookkeeping, not an edit, so it stays out of the undo history.
    m_store->begin("Alarm", false);
    m_store->putAlarm(copy);
    m_store->commit();
}

void AlarmService::snooze(qint64 alarmId, int minutes) {
    Alarm a = m_store->alarms().value(alarmId);
    if (!a.id)
        return;
    a.snoozedUntil = QDateTime::currentDateTime().addSecs(qint64(minutes) * 60);
    m_store->begin("Snooze", false);
    m_store->putAlarm(a);
    m_store->commit();
}

void AlarmService::onClosed(uint id, uint reason) {
    // Reason 2 is "dismissed by the user", which ends nagging for that alarm.
    if (reason == 2 && m_notifications.contains(id))
        m_nagging.remove(m_notifications.take(id));
}

void AlarmService::onAction(uint id, const QString &key) {
    if (!m_notifications.contains(id))
        return;
    const qint64 alarmId = m_notifications.take(id);
    m_nagging.remove(alarmId);
    const qint64 taskId = m_store->alarms().value(alarmId).taskId;
    if (key == "complete")
        emit completeTask(taskId);
    else if (key == "snooze")
        snooze(alarmId, 15);
    else if (key == "snooze-hour")
        snooze(alarmId, 60);
    else if (key == "snooze-evening" || key == "snooze-morning") {
        // the reference app's presets: this evening at 7 PM, tomorrow morning at 9 AM.
        const QDateTime now = QDateTime::currentDateTime();
        QDateTime at = key == "snooze-evening" ? QDateTime(now.date(), QTime(19, 0)) : QDateTime(now.date().addDays(1), QTime(9, 0));
        if (at <= now)
            at = at.addDays(1);
        snooze(alarmId, int(now.secsTo(at) / 60));
    }
    else
        emit openTask(taskId);
}
