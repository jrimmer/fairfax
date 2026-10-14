#include "synchub.h"

#include <QMetaObject>
#include <QStringList>

void SyncHub::add(QObject *provider, const QString &name) {
    if (!provider)
        return;
    m_entries.append({provider, name});
    connect(provider, SIGNAL(stateChanged()), this, SIGNAL(changed()));
    emit changed();
}

bool SyncHub::configured(const QObject *p) {
    const QVariant v = p->property("configured");
    return (v.isValid() ? v : p->property("connected")).toBool();
}

bool SyncHub::statusFailed(const QObject *p) {
    if (p->property("busy").toBool())
        return false;
    const QString status = p->property("status").toString();
    return !(status.isEmpty() || status.startsWith("Synced") || status == "Not synced yet" || status == "Connecting…"
             || status == "Connected");
}

bool SyncHub::active() const {
    for (const auto &e : m_entries)
        if (configured(e.provider))
            return true;
    return false;
}

bool SyncHub::busy() const {
    for (const auto &e : m_entries)
        if (configured(e.provider) && e.provider->property("busy").toBool())
            return true;
    return false;
}

bool SyncHub::failed() const {
    for (const auto &e : m_entries)
        if (configured(e.provider) && statusFailed(e.provider))
            return true;
    return false;
}

QDateTime SyncHub::lastSync() const {
    QDateTime oldest;
    for (const auto &e : m_entries) {
        if (!configured(e.provider))
            continue;
        const QDateTime t = e.provider->property("lastSync").toDateTime();
        if (!t.isValid())
            return QDateTime();
        if (!oldest.isValid() || t < oldest)
            oldest = t;
    }
    return oldest;
}

QString SyncHub::summary() const {
    QStringList lines;
    for (const auto &e : m_entries)
        if (configured(e.provider))
            lines << e.name + ": " + e.provider->property("status").toString();
    return lines.join('\n');
}

void SyncHub::syncAll() {
    for (const auto &e : m_entries)
        if (configured(e.provider))
            QMetaObject::invokeMethod(e.provider, "syncNow");
}
