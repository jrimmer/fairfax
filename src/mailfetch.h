#pragma once

// The Mail to Fairfax connector: watches an IMAP mailbox you own and turns the
// messages meant for Fairfax into tasks (src/mailtask.h does the converting).
// The mailbox can be your everyday one: only mail that matches the filters
// (sent to a plus-address, carrying a label or keyword) is imported, and
// everything else is left alone.
//
// Settings are key/value in the store (`mail.*`, see option()); secrets are in
// the keyring (src/keyring.h) under service "fairfax": account "mail" holds
// the password or Gmail app password, "mail-client" a Google OAuth client
// secret, "mail-oauth" the OAuth refresh token.
//
// No OAuth client id or secret ships with Fairfax, and none ever should: it
// is open source, and nobody runs a registration for forks. The defaults need
// none (a Gmail app password; Outlook through forwarding or a local DavMail
// gateway). OAuth exists only for a client the user brings: an imported Google
// "Desktop app" client JSON, or a pasted Microsoft application (client) id.

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

#include "imapclient.h"
#include "mailtask.h"
#include "oauth.h"

class Store;

class MailFetch : public QObject {
    Q_OBJECT
    // The first five make the connector look like the sync providers to SyncHub.
    Q_PROPERTY(bool configured READ enabled NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ hubStatus NOTIFY stateChanged)
    Q_PROPERTY(QDateTime lastSync READ lastCheck NOTIFY stateChanged)
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)   // off, connecting, idle, fetching, error
    Q_PROPERTY(QDateTime lastCheck READ lastCheck NOTIFY stateChanged)
    Q_PROPERTY(int imported READ imported NOTIFY stateChanged)      // Tasks made this session.
    Q_PROPERTY(int skipped READ skipped NOTIFY stateChanged)        // Messages over 25 MB left alone this session.
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString statusLine READ statusLine NOTIFY stateChanged)
    // Changes whenever any option does; bind to it to re-read option().
    Q_PROPERTY(int revision READ revision NOTIFY optionsChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY optionsChanged)
    Q_PROPERTY(QString plusAddress READ plusAddress NOTIFY optionsChanged)
    Q_PROPERTY(bool unfiltered READ unfiltered NOTIFY optionsChanged)
    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY optionsChanged)
    Q_PROPERTY(bool hasGoogleClient READ hasGoogleClient NOTIFY optionsChanged)
    Q_PROPERTY(QString oauthState READ oauthState NOTIFY oauthChanged)    // none, waiting, signedIn
    Q_PROPERTY(QString oauthError READ oauthError NOTIFY oauthChanged)
    Q_PROPERTY(QString signedInAs READ signedInAs NOTIFY oauthChanged)

public:
    enum class State { Off, Connecting, Idle, Fetching, Error };
    static constexpr qint64 MaxMessageSize = 25 * 1024 * 1024;

    explicit MailFetch(Store *store, QObject *parent = nullptr);
    ~MailFetch() override;

    bool enabled() const;
    bool busy() const { return m_state == State::Connecting || m_state == State::Fetching; }
    State state() const { return m_state; }
    QString stateName() const;
    QString hubStatus() const;
    QDateTime lastCheck() const { return m_lastCheck; }
    int imported() const { return m_imported; }
    int skipped() const { return m_skipped; }
    QString lastError() const { return m_lastError; }
    QString statusLine() const;
    int revision() const { return m_revision; }

    // The setting `key` ("mail.host", ...) as the pane shows it: the stored
    // value, or the default for the current account type. Booleans are
    // "true"/"false"; numbers are text.
    Q_INVOKABLE QString option(const QString &key) const;
    // Every key option() understands, so a test can insist that each has a case.
    static QStringList optionKeys();
    // Stores a setting. Changing the address of a Gmail account moves the
    // to-filter with it (until the user has edited the filter themselves), and
    // any change while the connector is on makes it reconnect.
    Q_INVOKABLE void setOption(const QString &key, const QVariant &value);
    // Fills in the host, port, security and the Gmail defaults for an account
    // type ("gmail", "outlook" or "other").
    Q_INVOKABLE void setAccount(const QString &account);
    // Outlook through a local DavMail gateway: the Other preset with 127.0.0.1:1143, no TLS.
    Q_INVOKABLE void useDavMail();

    // jane@gmail.com -> jane+fairfax@gmail.com; empty when there is no address or it already has a plus.
    static QString plusAddressOf(const QString &address);
    QString plusAddress() const { return plusAddressOf(option("mail.address")); }
    bool unfiltered() const;
    Q_INVOKABLE void copyText(const QString &text);

    // Password or Gmail app password, kept in the keyring only.
    bool hasPassword() const;
    Q_INVOKABLE void setPassword(const QString &password);

    // OAuth with a client the user brings. importClientJson reads a Google
    // "Desktop app" client_secret.json ({"installed": {client_id, client_secret,
    // ...}}) and returns an error message, or an empty string on success.
    bool hasGoogleClient() const;
    Q_INVOKABLE QString importClientJson(const QString &path);
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void signOut();
    QString oauthState() const { return m_oauthWaiting ? QStringLiteral("waiting") : usesOAuth() ? QStringLiteral("signedIn") : QStringLiteral("none"); }
    QString oauthError() const { return m_oauthError; }
    QString signedInAs() const;

    // Connects (if needed) and imports what is waiting. SyncHub calls it as syncNow.
    Q_INVOKABLE void fetchNow();
    Q_INVOKABLE void syncNow() { fetchNow(); }
    // Called once at launch: connects when the connector is on.
    Q_INVOKABLE void start();

    // For tests.
    struct Timings {
        int imapTimeout = 30000;
        int backoffBase = 30000;      // First retry delay; doubles up to 15 minutes.
        int pollOverride = 0;         // Replaces mail.interval minutes when not zero (ms).
        int idleRenew = 25 * 60000;
    };
    void setTimings(const Timings &timings);
    void setHttpTransport(OAuth::Transport transport);

signals:
    void stateChanged();
    void optionsChanged();
    void oauthChanged();
    void checked(int imported);   // A pass over the mailbox ended without error.

private:
    struct Config {
        QString account, host, user, folder, toFilter, labelFilter, after, moveTo;
        quint16 port = 993;
        ImapClient::Security security = ImapClient::Security::Tls;
        qint64 listId = 0;
        bool naturalLanguage = false;
    };
    Config config() const;
    bool toFilterMatches(const MailTask::Message &message, const QString &filter) const;
    QByteArray searchCriteria(const Config &c) const;

    void startSession();
    void authenticate();
    void afterAuth();
    void runCycle();
    void nextBatch();
    void processNext();
    void afterImport(qint64 uid);
    void advanceMark(qint64 uid);
    void finishCycle();
    void enterIdle();
    void leaveIdle(std::function<void()> then);
    void failSession(const QString &message, bool retry);
    void dropSession();
    void setState(State state, const QString &error = QString());
    void scheduleRetry();
    void ensureAccessToken(std::function<void(bool ok, const QString &tokenOrError)> done);
    OAuth::Provider oauthProvider() const;
    bool usesOAuth() const;
    void changed();
    void put(const QString &key, const QVariant &value);
    QString markKey(const Config &c) const;

    Store *m_store;
    State m_state = State::Off;
    QString m_lastError;
    QDateTime m_lastCheck;
    int m_imported = 0, m_skipped = 0;
    int m_revision = 0;
    Timings m_timings;

    // The session. Callbacks carry the generation they were made for and
    // return at once when it has moved on.
    ImapClient *m_client = nullptr;
    quint64 m_session = 0;
    bool m_cycleBusy = false;
    bool m_again = false;          // fetchNow() arrived mid-cycle: go round once more.
    bool m_inIdle = false;
    bool m_stopRequested = false;
    std::function<void()> m_afterIdle;
    bool m_moveFolderReady = false;
    int m_failures = 0;
    Config m_config;               // What this session runs with.
    qint64 m_validity = 0;
    QList<qint64> m_todo;          // Uids found by the search, still to do.
    QList<qint64> m_batch;         // The part of m_todo whose sizes are known.
    QHash<qint64, qint64> m_sizes;
    QSet<qint64> m_rejected;       // Uids that were not for Fairfax or too big; not fetched again this session.
    QTimer m_retry, m_poll, m_idleRenew, m_reconfigure;

    OAuth::Flow m_flow;      // Signing in.
    OAuth::Flow m_refresh;   // Renewing the access token.
    OAuth::Transport m_transport;
    bool m_oauthWaiting = false;
    QString m_oauthError;
    QString m_accessToken;
    QDateTime m_accessExpires;
    mutable int m_havePassword = -1;   // -1: not looked up yet.
};
