#pragma once

// OAuth 2.0 for the installed-app loopback flow with PKCE (RFC 8252 and RFC
// 7636), by hand on QNetworkAccessManager and a one-shot QTcpServer. Mail to
// Precrime uses it to sign in to Gmail or Microsoft 365 with a client the
// user supplies; Precrime ships no client id or secret of its own (see
// src/mailfetch.h).
//
// The flow: listen on 127.0.0.1 at a random port, open the authorisation URL
// in the browser, wait for the redirect carrying `code` and `state`, check
// `state`, and exchange the code (with the PKCE verifier) for tokens. All
// HTTP goes through a Transport that tests replace.

#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

namespace OAuth {

struct Provider {
    QString name;               // "google" or "microsoft".
    QUrl authUrl, tokenUrl;
    QString scope;
    QString clientId;
    QString clientSecret;       // Sent with the token requests when not empty.
    QString redirectHost;       // "127.0.0.1" or "localhost": what the redirect URI names.
    QList<QPair<QString, QString>> authParams;   // Extra parameters on the authorisation URL.
};

Provider google(const QString &clientId, const QString &clientSecret);
Provider microsoft(const QString &clientId);

struct Tokens {
    QString accessToken;
    QString refreshToken;       // The new one if the answer carried one, else the one that was used.
    QDateTime expires;
};

// Posts a form and calls `done` with the HTTP status (0 when the request
// never got an answer) and the JSON object that came back.
using Transport = std::function<void(const QUrl &url, const QUrlQuery &form, std::function<void(int status, const QJsonObject &reply)> done)>;

// A random PKCE verifier (43 to 128 URL-safe characters) and its S256 challenge.
QString newVerifier();
QString challengeFor(const QString &verifier);

class Flow : public QObject {
    Q_OBJECT

public:
    explicit Flow(QObject *parent = nullptr);

    // Replaces the network. The default posts with QNetworkAccessManager.
    void setTransport(Transport transport) { m_transport = std::move(transport); }
    // Replaces how the authorisation URL is shown to the user; the default
    // is QDesktopServices::openUrl, which tests catch with a URL handler.
    void setOpener(std::function<void(const QUrl &)> opener) { m_opener = std::move(opener); }
    // How long to wait for the browser (default five minutes).
    void setWaitLimit(int ms) { m_waitLimit = ms; }

    bool waiting() const { return m_server.isListening(); }
    quint16 port() const { return m_server.serverPort(); }

    // Starts listening and opens the browser. finished() or failed() follows.
    void start(const Provider &provider);
    void cancel();

    // Exchanges a refresh token for a fresh access token. A new refresh token
    // in the answer replaces the old one in finished()'s Tokens.
    void refresh(const Provider &provider, const QString &refreshToken);

signals:
    void finished(const OAuth::Tokens &tokens);
    void failed(const QString &message);

private:
    void onConnection();
    void respond(QTcpSocket *socket, int status, const QString &title, const QString &message);
    void exchange(const QString &code);
    void handleTokenReply(int status, const QJsonObject &reply, const QString &oldRefreshToken);
    void post(const QUrl &url, const QUrlQuery &form, std::function<void(int, const QJsonObject &)> done);
    void stop();

    Provider m_provider;
    QTcpServer m_server;
    QTimer m_limit;
    Transport m_transport;
    std::function<void(const QUrl &)> m_opener;
    QNetworkAccessManager *m_network = nullptr;
    QString m_state, m_verifier, m_redirect;
    int m_waitLimit = 300000;
    quint64 m_generation = 0;   // Bumped by cancel() and start(), so a late reply to an old attempt is dropped.
};

}  // namespace OAuth
