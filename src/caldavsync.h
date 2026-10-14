#pragma once

// Two-way CalDAV sync for tasks. Each Fairfax list maps to at most one VTODO
// calendar and each calendar to at most one list; the pairing (and the
// calendar's last ctag and sync token) lives in the "lists" sync links. Tasks
// keep their resource href, etag and the VTODO properties Fairfax does not
// interpret ("Extra") in the "tasks" sync links, so a later write puts the
// unknown properties back unchanged.
//
// One sync discovers the calendars, pairs and creates them, then works
// through each mapped calendar: skip it when its ctag is unchanged and
// nothing local changed, otherwise pull the remote changes (sync-collection
// where offered, an etag listing otherwise) and apply them, then push the
// local creates, edits, deletes and moves. When both sides edited a task the
// newer LAST-MODIFIED wins; a 412 on a write refetches the resource and
// decides again before one retry.
//
// Deletes go both ways: a local deletion sends DELETE If-Match, and a remote
// deletion removes the local task unless it changed since the last sync, in
// which case it is uploaded again. A new local task goes up as <uid>.ics with
// If-None-Match: *; a new remote resource becomes a task in the mapped list.
// Disconnect forgets the links and the keyring entry and never deletes tasks.

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "caldavclient.h"
#include "model.h"
#include "synctree.h"
#include "vtodo.h"

class Store;
class Theme;

class CalDavSync : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool needsChoice READ needsChoice NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY stateChanged)
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY stateChanged)
    Q_PROPERTY(QVariantList mappings READ mappings NOTIFY stateChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY stateChanged)
    Q_PROPERTY(QString url READ url NOTIFY stateChanged)
    Q_PROPERTY(QString user READ user NOTIFY stateChanged)

public:
    // `theme` colours a calendar created for a new list; without one a small
    // built-in palette stands in.
    explicit CalDavSync(Store *store, Theme *theme = nullptr, QObject *parent = nullptr);

    bool configured() const;
    bool busy() const { return m_busy; }
    bool needsChoice() const { return m_needsChoice; }
    QString status() const { return m_status; }
    QDateTime lastSync() const;
    QVariantList calendars() const;
    QVariantList mappings() const;
    QString accountName() const;
    QString url() const;
    QString user() const;

    // The password goes to the desktop keyring, never into the database.
    Q_INVOKABLE void connectAccount(const QString &url, const QString &user, const QString &password);
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void syncNow();
    // After the first-sync question: "merge", "local" (replace local) or "server".
    Q_INVOKABLE void chooseFirstSync(const QString &choice);

    // Mapping, for Settings: pair a list with a calendar href (empty unmaps),
    // and re-read the calendar list from the server.
    Q_INVOKABLE void setCalendarForList(qint64 listId, const QString &calendarHref);
    // True when CalDAV skips the list: set to "Don't sync" here, or excluded from every provider.
    Q_INVOKABLE bool isListSkipped(qint64 listId) const;
    // Settings' "New calendar": makes a calendar for the list on the server
    // and pairs it, without running a whole sync cycle.
    Q_INVOKABLE void createCalendarForList(qint64 listId);
    Q_INVOKABLE void refreshCalendars();

    // Tests shorten the client's retry timings.
    void setTimings(const CalDavClient::Timings &timings);
    // Tests connect without starting a sync, so they drive the first cycle
    // themselves.
    void setQuietConnect(bool quiet) { m_quietConnect = quiet; }

    // Runs one cycle and returns when it settles, for tests.
    struct Result {
        bool ok = false;
        bool needsChoice = false;
        int changed = 0;
        QString error;
    };
    Result syncBlocking(SyncTree::FirstSync first = SyncTree::FirstSync::Merge);

signals:
    void stateChanged();
    void synced(const QString &summary);

private:
    // One local change to push.
    struct Op {
        qint64 taskId = 0;
        QString href;      // Target resource, or the resource to delete.
        QString oldHref;   // Move: the resource to delete after the write.
        QString etag;      // If-Match for an update or delete.
        QString oldEtag;   // Move: the If-Match for the old resource.
        Vtodo::Extra extra;
        bool remove = false;
        bool update = false;
        bool retried = false;
    };
    struct ListState {
        qint64 listId = 0;
        QString href;
        QString ctag;
        QString token;
    };

    // The run
    void startRun();
    void onDiscovered(const CalDavClient::Outcome &outcome, const QList<CalDavClient::Calendar> &calendars);
    void buildMapping();
    void createNextCalendar();
    void afterMapping();
    void computeDirty();
    void finishOk();
    void fail(const QString &error);

    // First sync
    void startFirstCheck();
    void wipeRemote();
    void onWiped();

    // Calendar cycle
    void processNextCalendar();
    void pullRemote();
    void onSyncCollection(const CalDavClient::Outcome &outcome, const QStringList &changed,
                          const QStringList &removed, const QString &newToken);
    void listRemoteEtags();
    void onEtags(const CalDavClient::Outcome &outcome, const QMap<QString, QString> &etags);
    void fetchResources(const QStringList &hrefs);
    void onMultiget(const CalDavClient::Outcome &outcome, const QMap<QString, CalDavClient::Resource> &resources);
    qint64 applyRemoteResource(const QString &href, const QString &etag, const QByteArray &data);
    void applyRemoteRemoval(const QString &href);
    void pushLocal();
    void nextOp();
    void runOp(const Op &op);
    void onPut(const Op &op, const CalDavClient::Outcome &outcome, const QString &etag);
    void onRemove(const Op &op, const CalDavClient::Outcome &outcome);
    void resolvePutConflict(const Op &op);
    void afterCalendar();

    // Wrapping up
    void refreshCtags();
    void onCtagsRefreshed(const CalDavClient::Outcome &outcome, const QList<CalDavClient::Calendar> &calendars);

    // Bookkeeping
    QList<qint64> tagIdsFor(const QStringList &names);
    QList<qint64> locationIdsFor(const QString &name);
    void applyAlarms(qint64 taskId, const QList<Alarm> &alarms);
    void storeTaskLink(qint64 taskId, const QString &href, const QString &etag, const Vtodo::Extra &extra);
    void updateListLink(const ListState &state);
    QString calendarColor(const TaskList &list) const;
    QString homeHref() const;
    QString accountNameFor(const QUrl &url) const;
    void mapCalendarsIntoStore();

    Store *m_store;
    Theme *m_theme;
    CalDavClient *m_client;
    QTimer m_periodic;
    QTimer m_debounce;
    bool m_busy = false;
    bool m_again = false;
    bool m_applying = false;
    bool m_needsChoice = false;
    QString m_status;
    SyncTree::FirstSync m_mode = SyncTree::FirstSync::Ask;
    mutable QString m_cachedPassword;
    mutable bool m_passwordLooked = false;
    QUrl m_url;
    QString m_accountName;
    QList<CalDavClient::Calendar> m_calendars;

    // One run.
    int m_in = 0;
    int m_out = 0;
    QString m_writeError;
    QList<ListState> m_states;
    int m_stateIndex = 0;
    QList<qint64> m_pendingCreate;
    QSet<qint64> m_dirty;
    bool m_localHasTasks = false;
    bool m_remoteHasTasks = false;
    int m_probeIndex = 0;
    bool m_finished = true;
    bool m_quietConnect = false;
    Result m_lastResult;
    QStringList m_wipeHrefs;
    int m_wipeIndex = 0;

    // The calendar being processed.
    ListState m_job;
    QStringList m_pullChanged;
    QStringList m_pullRemoved;
    QList<Op> m_ops;
    int m_opIndex = 0;
};
