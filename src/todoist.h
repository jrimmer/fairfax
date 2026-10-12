#pragma once

// Two-way sync with Todoist through its v1 Sync API (docs/todoist-api.md).
//
// Mapping: Todoist projects ⇄ lists (the Todoist Inbox ⇄ the GTD Inbox),
// labels ⇄ tags, items ⇄ tasks with parent_id for sub-tasks. Priority 4
// (p1) ⇄ High, 3 ⇄ Medium, 2 ⇄ Low, 1 ⇄ None. due ⇄ due date and time;
// duration ⇄ duration; checked ⇄ completed. Repeats travel as Todoist's own
// recurrence strings ("every 2 weeks", "every! 3 days" for repeat-from-
// completion) and are read back with the natural-language parser. Fields
// Todoist has no place for (start date and time, star, task type, action)
// ride in a "<Precrime Meta>{json}</Precrime Meta>" block at the end of the
// description, as the reference app does. List groups, smart lists, alarms and sections
// stay local.
//
// Change tracking uses Store::SyncLink snapshots: a local record differs
// from its snapshot when it changed since the last sync; a record without a
// link is new; a deleted record with a link is a deletion.

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <functional>

#include "model.h"

class Store;

namespace TodoistMapping {

constexpr auto Provider = "todoist";

// The commands that push local changes since the last sync, each with a
// fresh uuid; new records get temp_id = their local uid. Completed copies
// left behind by repeating tasks stay local (Todoist keeps its own history),
// and a repeating task that advanced locally is sent as item_close so the
// server advances it too. Parents come before children, projects and labels
// before items. Every command also carries a private "_local" object (which
// record it belongs to and its snapshot) that applyResponse reads back and
// that TodoistSync strips before sending. All commands for one record share
// a "_local.key"; `skip` lists keys to leave out (ones that failed already).
QJsonArray buildCommands(const Store &store, const QSet<QString> &skip = {});

// The longest prefix of `commands` that fits in one request: at most `max`
// commands (Todoist allows 100), never splitting one record's commands.
QJsonArray firstBatch(const QJsonArray &commands, int max = 100);

struct ApplyResult {
    int in = 0;                // Records created, updated or deleted locally.
    int out = 0;               // Local records whose commands all succeeded.
    QSet<QString> failed;      // "_local.key" of records with a rejected command.
    QStringList errors;
};
ApplyResult apply(Store &store, const QJsonArray &sentCommands, const QJsonObject &response);

// Applies one sync response as a single non-undoable store change: maps
// temp ids to remote ids for what was pushed, then upserts or deletes local
// records for every incoming project, label and item. When a record changed
// on both sides, the side modified most recently wins. Updates every touched
// record's snapshot. Returns a short summary ("3 in, 2 out"); `error` gets
// the first rejected command, if any.
QString applyResponse(Store &store, const QJsonArray &sentCommands, const QJsonObject &response, QString *error = nullptr);

// The English recurrence string Todoist understands for a repeat rule, or an
// empty string when the task doesn't repeat.
QString recurrenceString(const Recurrence &rule);

}  // namespace TodoistMapping

class TodoistSync : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool configured READ connected NOTIFY stateChanged)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)

public:
    explicit TodoistSync(Store *store, QObject *parent = nullptr);

    bool connected() const;
    QDateTime lastSync() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    // The token is kept in the desktop keyring through secret-tool
    // (libsecret), never in the database.
    Q_INVOKABLE void connectAccount(const QString &token);
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void syncNow();

    // Replaces the network: the function gets the form fields of each request
    // (sync_token, resource_types, commands) and returns the response JSON,
    // or an object with "http_status" (and "retry_after") to simulate a
    // failure. The token is used instead of the keyring's.
    using Transport = std::function<QJsonObject(const QUrlQuery &form)>;
    void setTransport(Transport transport, const QString &token = QStringLiteral("test-token"));

signals:
    void stateChanged();
    void synced(const QString &summary);

private:
    QString token() const;
    void finish(const QString &status);
    void step();
    void send(const QUrlQuery &form, std::function<void(const QJsonObject &)> done);
    void dispatch();
    void handle(const QJsonObject &reply);
    void setStatus(const QString &status);

    Store *m_store;
    QNetworkAccessManager m_network;
    QTimer m_timer;     // Periodic sync while connected.
    QTimer m_debounce;  // Sync shortly after local edits.
    bool m_busy = false;
    QString m_status;
    mutable QString m_cachedToken;
    mutable bool m_tokenLooked = false;
    Transport m_transport;
    QJsonArray m_sent;
    QSet<QString> m_failed;
    QUrlQuery m_form;
    std::function<void(const QJsonObject &)> m_done;
    int m_phase = 0;
    int m_rounds = 0;
    int m_retries = 0;
    int m_in = 0;
    int m_out = 0;
    QString m_firstError;
    bool m_applying = false;
};
