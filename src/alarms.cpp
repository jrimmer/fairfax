#include "alarms.h"

#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#endif
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include "schedule.h"
#include "store.h"

namespace {

constexpr auto Service = "org.freedesktop.Notifications";
constexpr auto Path = "/org/freedesktop/Notifications";
constexpr auto Interface = "org.freedesktop.Notifications";

}  // namespace

QDateTime AlarmTimes::clockTime(int minutes) {
    return QDateTime(QDate(2000, 1, 1), QTime(0, 0).addSecs(qint64(minutes) * 60));
}

QDateTime AlarmTimes::fireTime(const Alarm &a, const Task &t, int defaultDueMinutes) {
    if (a.deleted || t.deleted || t.completed)
        return {};
    if (a.snoozedUntil.isValid())
        return a.snoozedUntil;
    if (a.kind == Alarm::Absolute)
        return a.at;
    // Alerts count from the due date, or from the start date when that is all
    // the task has.
    QDate date = t.dueDate;
    int minutes = t.dueTime;
    if (!date.isValid()) {
        date = t.startDate;
        minutes = t.startTime;
    }
    if (!date.isValid())
        return {};
    if (a.kind == Alarm::DaysBefore)
        return QDateTime(date.addDays(-a.offset), a.at.time());
    if (minutes < 0)
        minutes = defaultDueMinutes;
    return QDateTime(date, QTime(0, 0)).addSecs(qint64(minutes - a.offset) * 60);
}

bool AlarmTimes::hasPending(const Store &store, const QDateTime &now) {
    Q_UNUSED(now);
    const int defaultDue = store.setting("tasks.defaultDueTime", 9 * 60).toInt();
    for (const auto &a : store.alarms()) {
        if (a.deleted)
            continue;
        const Task t = store.task(a.taskId);
        const QDateTime moment = fireTime(a, t, defaultDue);
        if (moment.isValid() && a.firedFor != moment && !store.list(t.listId).muteAlerts)
            return true;
    }
    return false;
}

QString AlarmTimes::notesTeaser(const QString &notes, int maxChars) {
    static const QRegularExpression marker("^(#+\\s+|[-*+]\\s+(\\[[ xX]\\]\\s+)?|>\\s*)");
    for (QString line : notes.split('\n')) {
        line = line.trimmed();
        line.remove(marker);
        if (line.isEmpty())
            continue;
        return line.size() > maxChars ? line.left(maxChars - 1).trimmed() + QStringLiteral("…") : line;
    }
    return {};
}

QString AlarmTimes::actionLabel(const Task &t) {
    if (t.actionValue.isEmpty())
        return {};
    switch (t.actionType) {
    case ActionType::Call: return QStringLiteral("Call");
    case ActionType::Message: return QStringLiteral("Message");
    case ActionType::Mail: return QStringLiteral("Email");
    case ActionType::Visit: return QStringLiteral("Show on Map");
    case ActionType::Url: return QStringLiteral("Open Link");
    case ActionType::Google: return QStringLiteral("Search Google");
    }
    return {};
}

AlarmTimes::DefaultReminders AlarmTimes::DefaultReminders::load(const Store &store) {
    DefaultReminders d;
    d.timedOffset = store.setting("alerts.defaultTimed", -1).toInt();
    d.dateOnlyDays = store.setting("alerts.defaultDateOnly", -1).toInt();
    d.dateOnlyMinutes = store.setting("alerts.defaultDateOnlyTime", store.setting("tasks.defaultDueTime", 9 * 60)).toInt();
    return d;
}

std::optional<Alarm> AlarmTimes::DefaultReminders::alarmFor(const Task &task) const {
    Alarm a;
    if (!task.dueDate.isValid() || task.completed || task.deleted)
        return std::nullopt;
    if (task.dueTime >= 0 && timedOffset >= 0) {
        a.offset = timedOffset;
    } else if (task.dueTime < 0 && dateOnlyDays >= 0) {
        a.kind = Alarm::DaysBefore;
        a.offset = dateOnlyDays;
        a.at = clockTime(dateOnlyMinutes);
    } else {
        return std::nullopt;
    }
    a.taskId = task.id;
    return a;
}

void AlarmTimes::applyDefaultReminder(Store &store, const Task &task) {
    auto alarm = DefaultReminders::load(store).alarmFor(task);
    if (!alarm || !store.alarmsFor(task.id).isEmpty())
        return;
    Alarm a = *alarm;
    // Checklist items carry no alerts of their own.
    if (task.parentId && store.task(task.parentId).type == TaskType::Checklist)
        return;
    a.id = store.nextId("alarms");
    store.putAlarm(a);
}

QDateTime AlarmTimes::snoozeTarget(const QString &key, const QDateTime &now, int morningMinutes) {
    if (key == "snooze5")
        return now.addSecs(5 * 60);
    if (key == "snooze")
        return now.addSecs(15 * 60);
    if (key == "snooze-hour")
        return now.addSecs(60 * 60);
    if (key == "snooze-evening") {
        QDateTime at(now.date(), QTime(19, 0));
        return at <= now ? at.addDays(1) : at;
    }
    if (key == "snooze-morning")
        return QDateTime(now.date().addDays(1), QTime(0, 0).addSecs(qint64(morningMinutes < 0 ? 9 * 60 : morningMinutes) * 60));
    return {};
}

AlarmService::AlarmService(Store *store, QObject *parent) : QObject(parent), m_store(store) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &AlarmService::check);
    connect(m_store, &Store::changed, this, &AlarmService::schedule);
    connect(m_store, &Store::changed, this, &AlarmService::closeStale);
#if defined(Q_OS_LINUX)
    m_connected = QDBusConnection::sessionBus().connect(Service, Path, Interface, "ActionInvoked", this,
                                                        SLOT(onAction(uint, QString)));
    QDBusConnection::sessionBus().connect(Service, Path, Interface, "NotificationClosed", this,
                                          SLOT(onClosed(uint, uint)));
#endif
    // Sounding alerts can come back until they're handled, like the reference app's
    // "Sound alerts repeat until dismissed".
    if (!m_store->setting("alerts.nagMinutes").isValid())
        m_store->setSetting("alerts.nagMinutes", m_store->setting("alerts.nag", false).toBool() ? 5 : 0);  // The old on/off switch.
    m_nagTimer.setInterval(30 * 1000);
    connect(&m_nagTimer, &QTimer::timeout, this, [this] {
        const QDateTime now = QDateTime::currentDateTime();
        const int minutes = nagMinutes();
        for (auto it = m_nagging.begin(); it != m_nagging.end();) {
            const Alarm a = m_store->alarms().value(it.key());
            const Task t = m_store->task(a.taskId);
            if (!minutes || !a.id || a.deleted || !a.sound || t.completed || t.deleted) {
                it = m_nagging.erase(it);
                continue;
            }
            if (it.value() <= now) {
                it.value() = now.addSecs(qint64(minutes) * 60);
                notify(a, t, now);
            }
            ++it;
        }
    });
    m_nagTimer.start();
    // Alarms that came due while Fairfax wasn't running go off now.
    QTimer::singleShot(1500, this, &AlarmService::check);
}

QVector<Alarm> AlarmService::due(const QDateTime &now) const {
    const int defaultDue = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    QVector<Alarm> result;
    for (const auto &a : m_store->alarms()) {
        const Task t = m_store->task(a.taskId);
        const QDateTime moment = AlarmTimes::fireTime(a, t, defaultDue);
        if (moment.isValid() && moment <= now && a.firedFor != moment && !m_store->list(t.listId).muteAlerts)
            result.append(a);
    }
    return result;
}

void AlarmService::check() {
    const QDateTime now = QDateTime::currentDateTime();
    const int defaultDue = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    for (const auto &a : due(now)) {
        const Task t = m_store->task(a.taskId);
        const QDateTime moment = AlarmTimes::fireTime(a, t, defaultDue);
        // Very old misses aren't worth a notification: they are just marked
        // as handled. (due() leaves out the alarms of muted lists, which
        // therefore go off if the list is unmuted in time.)
        if (moment.secsTo(now) < 24 * 3600)
            notify(a, t, moment);
        markFired(a, moment);
    }
    schedule();
}

void AlarmService::schedule() {
    const int defaultDue = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime next;
    for (const auto &a : m_store->alarms()) {
        const Task t = m_store->task(a.taskId);
        const QDateTime moment = AlarmTimes::fireTime(a, t, defaultDue);
        if (!moment.isValid() || a.firedFor == moment || m_store->list(t.listId).muteAlerts)
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
    // A teaser of the notes under the headline, so the alert says what the
    // task is about without opening it.
    const QString teaser = AlarmTimes::notesTeaser(t.notes);
    if (!teaser.isEmpty())
        body += (body.isEmpty() ? "" : "\n") + teaser;

#if !defined(Q_OS_LINUX)
    emit showFallback(t.title, body, t.id);
    Q_UNUSED(moment);
    return;
#else
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Interface, "Notify");
    QVariantMap hints{{"urgency", QVariant::fromValue<uchar>(1)}, {"category", "x-fairfax.alarm"},
                      {"desktop-entry", "fairfax"}};
    // In Do Not Disturb the notification still appears, quietly.
    const bool quiet = a.sound && doNotDisturb();
    if (a.sound && !quiet)
        hints.insert("sound-name", "alarm-clock-elapsed");
    // the reference app's snooze menu: the two date-based rows show the time they land on,
    // and "This evening" is left out when 7 PM is less than 30 minutes away.
    const QDateTime now = QDateTime::currentDateTime();
    QStringList actions{"default", "Open", "complete", "Complete", "snooze5", "Snooze 5 Minutes", "snooze", "Snooze 15 Minutes",
                        "snooze-hour", "Snooze 1 Hour"};
    // The task's own action (Call, Email, Open Link…) is one tap away.
    if (const QString label = AlarmTimes::actionLabel(t); !label.isEmpty()) {
        actions.insert(4, "act");
        actions.insert(5, label);
    }
    const QDateTime evening(now.date(), QTime(19, 0));
    if (now.secsTo(evening) >= 30 * 60)
        actions << "snooze-evening" << "This Evening (" + Schedule::formatTime(19 * 60) + ")";
    const QDateTime morning = AlarmTimes::snoozeTarget("snooze-morning", now, morningMinutes());
    actions << "snooze-morning" << "Tomorrow Morning (" + Schedule::formatTime(morning.time().hour() * 60 + morning.time().minute()) + ")";
    call << QStringLiteral("Fairfax") << uint(0) << QStringLiteral("fairfax") << t.title << body << actions << hints << int(0);
    const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(call);
    if (reply.isValid())
        m_notifications.insert(reply.value(), a.id);
    else
        qWarning("fairfax: couldn't show a notification: %s", qPrintable(reply.error().message()));
    // Only sounding alerts nag; a plain message is shown once.
    if (a.sound && nagMinutes() && !m_nagging.contains(a.id))
        m_nagging.insert(a.id, now.addSecs(qint64(nagMinutes()) * 60));

    // Notification daemons rarely play sounds themselves.
    if (a.sound && !quiet) {
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

void AlarmService::snooze(qint64 alarmId, const QDateTime &until) {
    Alarm a = m_store->alarms().value(alarmId);
    if (!a.id)
        return;
    a.snoozedUntil = until;
    m_store->begin("Snooze", false);
    m_store->putAlarm(a);
    m_store->commit();
}

bool AlarmService::doNotDisturb() {
#if defined(Q_OS_LINUX)
    QDBusConnection bus = QDBusConnection::sessionBus();
    // dunst exposes a "paused" property on its command interface.
    QDBusInterface dunst("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.dunstproject.cmd0", bus);
    if (dunst.isValid()) {
        const QVariant paused = dunst.property("paused");
        if (paused.isValid() && paused.toBool())
            return true;
    }
    // mako (Omarchy's notification daemon) reports its active modes; Omarchy
    // turns Do Not Disturb on as the "do-not-disturb" mode.
    QDBusInterface mako("fi.emersion.mako", "/fi/emersion/mako", "fi.emersion.mako", bus);
    if (mako.isValid()) {
        mako.setTimeout(300);
        const QDBusReply<QStringList> modes = mako.call("ListModes");
        if (modes.isValid() && modes.value().contains("do-not-disturb"))
            return true;
    }
#endif
    return false;
}

int AlarmService::nagMinutes() const {
    return m_store->setting("alerts.nagMinutes", 0).toInt();
}

int AlarmService::morningMinutes() const {
    // "Tomorrow morning" follows the Default Due Time when that is in use and
    // is a morning hour.
    const int minutes = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    return m_store->setting("tasks.useDefaultDueTime", false).toBool() && minutes < 12 * 60 ? minutes : -1;
}

void AlarmService::closeStale() {
    // A notification goes away once its task is completed, deleted or
    // re-dated (the alarm then has a different moment to fire at).
    const int defaultDue = m_store->setting("tasks.defaultDueTime", 9 * 60).toInt();
    for (auto it = m_notifications.begin(); it != m_notifications.end();) {
        const Alarm a = m_store->alarms().value(it.value());
        const Task t = m_store->task(a.taskId);
        const bool stale = !a.id || AlarmTimes::fireTime(a, t, defaultDue) != a.firedFor;
        if (!stale) {
            ++it;
            continue;
        }
        m_nagging.remove(it.value());
#if defined(Q_OS_LINUX)
        QDBusMessage close = QDBusMessage::createMethodCall(Service, Path, Interface, "CloseNotification");
        close << it.key();
        QDBusConnection::sessionBus().asyncCall(close);
#endif
        it = m_notifications.erase(it);
    }
}

void AlarmService::onClosed(uint id, uint reason) {
    // Reason 2 is "dismissed by the user", which ends nagging for that alarm.
    if (reason == 2 && m_notifications.contains(id))
        m_nagging.remove(m_notifications.take(id));
    else
        m_notifications.remove(id);
}

void AlarmService::onAction(uint id, const QString &key) {
    if (!m_notifications.contains(id))
        return;
    const qint64 alarmId = m_notifications.take(id);
    m_nagging.remove(alarmId);
    const qint64 taskId = m_store->alarms().value(alarmId).taskId;
    if (key == "complete")
        emit completeTask(taskId);
    else if (key == "act")
        emit performAction(taskId);
    else if (key.startsWith("snooze"))
        snooze(alarmId, AlarmTimes::snoozeTarget(key, QDateTime::currentDateTime(), morningMinutes()));
    else
        emit openTask(taskId);
}
