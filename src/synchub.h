#pragma once

// One place for the sidebar's sync indicator to ask "is anything syncing,
// when did it last succeed, did something fail", across git, Todoist, folder
// and WebDAV sync. Providers are registered as plain QObjects and read through
// their properties (configured or connected, busy, status, lastSync), so the
// hub needs no common base class.

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QVector>

class SyncHub : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    explicit SyncHub(QObject *parent = nullptr) : QObject(parent) {}

    // `name` is how the provider appears in the tooltip ("Git", "Todoist").
    // `announceFailures` is false for the mail connector, which reports its
    // own errors as "Mail: ..." rather than as a sync failure.
    void add(QObject *provider, const QString &name, bool announceFailures = true);

    bool active() const;       // At least one provider is set up.
    bool busy() const;
    bool failed() const;       // A set-up provider's last attempt failed or needs a decision.
    QDateTime lastSync() const;  // The oldest last success among set-up providers; invalid if one never synced.
    QString summary() const;   // One line per provider, for the tooltip.

    Q_INVOKABLE void syncAll();

signals:
    void changed();
    // A provider's cycle ended in failure: its status is the reason. Main.qml
    // announces it assertively, since a screen reader user cannot see the
    // sidebar's warning mark.
    void syncFailed(const QString &reason);

private slots:
    // One provider's state changed; a failure that just appeared is announced.
    void onProviderStateChanged();

private:
    struct Entry {
        QObject *provider;
        QString name;
    };
    static bool configured(const QObject *p);
    static bool statusFailed(const QObject *p);
    QVector<Entry> m_entries;
    QSet<QObject *> m_failureReported;  // Providers whose current failure is already announced.
};
