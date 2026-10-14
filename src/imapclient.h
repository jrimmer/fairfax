#pragma once

// A small asynchronous IMAP4rev1 client on QSslSocket, for Mail to Precrime
// (src/mailfetch.h). It speaks only what the connector needs: LOGIN or
// AUTHENTICATE PLAIN / XOAUTH2, SELECT, UID SEARCH, UID FETCH, UID STORE,
// CREATE, UID MOVE (or COPY + \Deleted + UID EXPUNGE), IDLE and LOGOUT.
//
// One command is in flight at a time; later ones wait their turn. Every
// command has a timeout, and a timeout or a lost connection ends the session:
// the command's callback gets a failed Response and the client is `closed()`.
// Callbacks run from the event loop (never from inside the call that queued
// the command), so a caller may queue the next command from a callback.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>

class ImapClient : public QObject {
    Q_OBJECT

public:
    enum class Security { Tls, StartTls, None };

    // One untagged response line. Literals ({n}\r\n then n bytes) are lifted
    // out of `line` and left as "{#i}" markers, so the line is plain text.
    struct Untagged {
        QByteArray line;               // Without the leading "* ".
        QList<QByteArray> literals;
    };

    struct Response {
        bool ok = false;               // The tagged answer was OK.
        QString status;                // "OK", "NO", "BAD", or "BYE" when the connection ended first.
        QString text;                  // The human-readable part of the tagged answer.
        QList<Untagged> untagged;
        bool timedOut = false;
        bool disconnected = false;     // The connection went away (or timed out) before an answer.
    };
    using Done = std::function<void(const Response &)>;

    // Answers a "+" continuation request (the text after "+ "); the returned
    // bytes are sent as a line. Used by AUTHENTICATE.
    using Continue = std::function<QByteArray(const QByteArray &)>;

    explicit ImapClient(QObject *parent = nullptr);
    ~ImapClient() override;

    // Milliseconds each command may take (default 30000). A FETCH of a large
    // message is given four times as long.
    void setTimeout(int ms) { m_timeout = ms; }
    int timeout() const { return m_timeout; }

    // Opens the connection; `done` gets the server greeting (ok when it was OK
    // or PREAUTH), or a failed Response when the connection or TLS handshake
    // failed. With StartTls the upgrade happens in startTls().
    void connectToServer(const QString &host, quint16 port, Security security, Done done);
    void close();
    bool isClosed() const { return m_closed; }

    // What the server advertises, as of the last capability() (or the greeting's
    // own [CAPABILITY ...] code). Names are upper case.
    bool hasCapability(const QString &name) const { return m_capabilities.contains(name.toUpper()); }
    QStringList capabilities() const { return m_capabilities; }

    // Raw command: `line` without the tag or CRLF. `cont` answers continuations.
    void command(const QByteArray &line, Done done, Continue cont = {}, int timeoutMs = -1);

    void capability(Done done);
    void startTls(Done done);   // STARTTLS, then the TLS handshake; capabilities are forgotten.
    // LOGIN, or AUTHENTICATE PLAIN when the server forbids LOGIN or the
    // password has characters a quoted string cannot carry.
    void login(const QString &user, const QString &password, Done done);
    void authenticateXoauth2(const QString &user, const QString &accessToken, Done done);

    struct Selection {
        bool ok = false;
        qint64 uidValidity = 0;
        qint64 exists = 0;
        QString error;
    };
    void select(const QString &folder, std::function<void(const Selection &)> done);

    // `criteria` is the raw search key text, already quoted where needed.
    void uidSearch(const QByteArray &criteria, std::function<void(bool ok, const QList<qint64> &uids, const QString &error)> done);
    // RFC822.SIZE of each uid (uids the server did not answer for are absent).
    void uidFetchSizes(const QList<qint64> &uids, std::function<void(bool ok, const QHash<qint64, qint64> &sizes, const QString &error)> done);
    // The whole message, fetched with BODY.PEEK[] so it is not marked read.
    void uidFetchMessage(qint64 uid, std::function<void(bool ok, const QByteArray &message, const QString &error)> done);
    void uidStoreSeen(qint64 uid, Done done);
    void createFolder(const QString &folder, Done done);
    // UID MOVE when the server has MOVE; otherwise COPY, mark the original
    // \Deleted and (with UIDPLUS) UID EXPUNGE it.
    void uidMove(qint64 uid, const QString &folder, Done done);

    // IDLE. `onExists` is called for each "* n EXISTS" while idling; `done`
    // gets the tagged answer once stopIdle() has sent DONE (or the server
    // ended the idle). There is no timeout while idling.
    void idle(std::function<void(qint64 exists)> onExists, Done done);
    void stopIdle();
    bool isIdling() const { return m_idling; }

    void logout(Done done = {});

    // An IMAP quoted string (backslash and quote escaped); folder names and
    // criteria must be ASCII or UTF-8 the server accepts.
    static QByteArray quote(const QString &text);

signals:
    void closed();   // The connection ended (by close(), the server, an error or a timeout).

private:
    struct Pending {
        QByteArray tag;
        QByteArray line;
        Done done;
        Continue cont;
        int timeoutMs = 0;
        std::function<void(qint64)> onExists;
        bool isIdle = false;
        Response response;
    };

    void pump();
    void onReadyRead();
    void feed();
    void dispatch(const Untagged &line);
    void finish(const Response &response);
    void fail(const QString &why, bool timedOut);
    void later(const std::function<void()> &fn);
    void armTimer(int ms);
    static void parseCapabilities(const QByteArray &text, QStringList &out);

    QSslSocket m_socket;
    QTimer m_timer;
    int m_timeout = 30000;
    QByteArray m_buffer;
    Untagged m_current;
    qint64 m_literalLeft = 0;
    bool m_inLiteral = false;
    QList<Pending> m_queue;
    bool m_running = false;      // The head of m_queue has been sent.
    bool m_closed = true;
    bool m_idling = false;
    bool m_greeted = false;
    bool m_upgrading = false;    // STARTTLS handshake under way.
    int m_nextTag = 1;
    QStringList m_capabilities;
    Done m_greetingDone;
    bool m_stopPending = false;  // stopIdle() was called before the server's "+ idling".
};
