#include "oauth.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>

namespace OAuth {

namespace {

QString base64Url(const QByteArray &bytes) {
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// n is a multiple of 4.
QByteArray randomBytes(int n) {
    QByteArray out(n, 0);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(out.data()), n / 4);
    return out;
}

QString errorText(const QJsonObject &reply, int status) {
    const QString description = reply.value("error_description").toString();
    if (!description.isEmpty())
        return description;
    const QString error = reply.value("error").toString();
    if (!error.isEmpty())
        return error;
    return status == 0 ? QStringLiteral("Couldn't reach the sign-in server.") : QStringLiteral("The sign-in server answered %1.").arg(status);
}

}  // namespace

Provider google(const QString &clientId, const QString &clientSecret) {
    Provider p;
    p.name = "google";
    p.authUrl = QUrl("https://accounts.google.com/o/oauth2/v2/auth");
    p.tokenUrl = QUrl("https://oauth2.googleapis.com/token");
    p.scope = "https://mail.google.com/";
    p.clientId = clientId;
    p.clientSecret = clientSecret;
    p.redirectHost = "127.0.0.1";
    // offline for a refresh token, consent so Google sends one again on a repeat sign-in.
    p.authParams = {{"access_type", "offline"}, {"prompt", "consent"}};
    return p;
}

Provider microsoft(const QString &clientId) {
    Provider p;
    p.name = "microsoft";
    p.authUrl = QUrl("https://login.microsoftonline.com/common/oauth2/v2.0/authorize");
    p.tokenUrl = QUrl("https://login.microsoftonline.com/common/oauth2/v2.0/token");
    p.scope = "https://outlook.office.com/IMAP.AccessAsUser.All offline_access";
    p.clientId = clientId;
    p.redirectHost = "localhost";
    return p;
}

QString newVerifier() { return base64Url(randomBytes(48)); }

QString challengeFor(const QString &verifier) {
    return base64Url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
}

Flow::Flow(QObject *parent) : QObject(parent), m_server(this), m_limit(this) {
    m_limit.setSingleShot(true);
    connect(&m_limit, &QTimer::timeout, this, [this] {
        stop();
        emit failed(QStringLiteral("Timed out waiting for the browser."));
    });
    connect(&m_server, &QTcpServer::newConnection, this, &Flow::onConnection);
}

void Flow::stop() {
    m_limit.stop();
    m_server.close();
    // A connection that already sits in the accept queue is ours to end;
    // the one-shot listener answers nobody, whatever the platform did with
    // the listen socket's teardown.
    while (QTcpSocket *pending = m_server.nextPendingConnection())
        pending->abort();
}

void Flow::cancel() {
    ++m_generation;
    stop();
}

void Flow::start(const Provider &provider) {
    cancel();
    m_provider = provider;
    if (!m_server.listen(QHostAddress::LocalHost, 0)) {
        emit failed(QStringLiteral("Couldn't listen for the browser: ") + m_server.errorString());
        return;
    }
    m_verifier = newVerifier();
    m_state = base64Url(randomBytes(16));
    m_redirect = QStringLiteral("http://%1:%2").arg(provider.redirectHost).arg(m_server.serverPort());
    QUrlQuery q;
    q.addQueryItem("response_type", "code");
    q.addQueryItem("client_id", provider.clientId);
    q.addQueryItem("redirect_uri", m_redirect);
    q.addQueryItem("scope", provider.scope);
    q.addQueryItem("state", m_state);
    q.addQueryItem("code_challenge", challengeFor(m_verifier));
    q.addQueryItem("code_challenge_method", "S256");
    for (const auto &extra : provider.authParams)
        q.addQueryItem(extra.first, extra.second);
    QUrl url = provider.authUrl;
    url.setQuery(q);
    m_limit.start(m_waitLimit);
    if (m_opener)
        m_opener(url);
    else
        QDesktopServices::openUrl(url);
}

void Flow::respond(QTcpSocket *socket, int status, const QString &title, const QString &message) {
    const QByteArray body = QStringLiteral("<!doctype html><meta charset=utf-8><title>%1</title>"
                                           "<body style=\"font-family:sans-serif;max-width:32em;margin:4em auto\"><h2>%1</h2><p>%2</p>")
                                .arg(title, message).toUtf8();
    socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") + "\r\nContent-Type: text/html; charset=utf-8\r\n"
                  "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    socket->disconnectFromHost();
}

void Flow::onConnection() {
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (socket->property("handled").toBool()) {
                socket->readAll();   // The rest of the request's headers.
                return;
            }
            const QByteArray data = socket->peek(socket->bytesAvailable());
            if (!data.contains("\r\n"))
                return;
            socket->readAll();
            socket->setProperty("handled", true);
            const QList<QByteArray> parts = data.left(data.indexOf("\r\n")).split(' ');
            if (parts.size() < 2 || parts[0] != "GET") {
                respond(socket, 400, "Bad request", "Fairfax expected a browser redirect.");
                return;
            }
            const QUrl target = QUrl::fromEncoded("http://localhost" + parts[1]);
            const QUrlQuery q(target);
            if (!q.hasQueryItem("code") && !q.hasQueryItem("error")) {
                // A favicon request or the like: not the redirect; keep waiting.
                respond(socket, 404, "Not found", "Nothing here.");
                return;
            }
            if (!m_server.isListening()) {
                respond(socket, 400, "Already used", "This sign-in link was already used.");
                return;
            }
            if (q.queryItemValue("state") != m_state) {
                respond(socket, 400, "Sign-in failed", "The sign-in response didn't match this request. Close this tab and try again from Fairfax.");
                stop();
                emit failed(QStringLiteral("The sign-in response didn't match the request (state mismatch)."));
                return;
            }
            if (q.hasQueryItem("error")) {
                const QString why = q.queryItemValue("error_description", QUrl::FullyDecoded).replace('+', ' ');
                respond(socket, 400, "Sign-in failed", "The provider refused the sign-in. You can close this tab.");
                stop();
                emit failed(why.isEmpty() ? q.queryItemValue("error") : why);
                return;
            }
            respond(socket, 200, "Signed in", "You can close this tab and return to Fairfax.");
            stop();   // One-shot.
            exchange(q.queryItemValue("code", QUrl::FullyDecoded));
        });
    }
}

void Flow::post(const QUrl &url, const QUrlQuery &form, std::function<void(int, const QJsonObject &)> done) {
    if (m_transport) {
        m_transport(url, form, std::move(done));
        return;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 0 && json.isEmpty())
            json.insert("error_description", reply->errorString());
        reply->deleteLater();
        done(status, json);
    });
}

void Flow::exchange(const QString &code) {
    QUrlQuery form;
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code", code);
    form.addQueryItem("redirect_uri", m_redirect);
    form.addQueryItem("client_id", m_provider.clientId);
    form.addQueryItem("code_verifier", m_verifier);
    if (!m_provider.clientSecret.isEmpty())
        form.addQueryItem("client_secret", m_provider.clientSecret);
    const quint64 generation = m_generation;
    post(m_provider.tokenUrl, form, [this, generation](int status, const QJsonObject &reply) {
        if (generation == m_generation)
            handleTokenReply(status, reply, QString());
    });
}

void Flow::refresh(const Provider &provider, const QString &refreshToken) {
    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", refreshToken);
    form.addQueryItem("client_id", provider.clientId);
    if (!provider.clientSecret.isEmpty())
        form.addQueryItem("client_secret", provider.clientSecret);
    post(provider.tokenUrl, form, [this, refreshToken](int status, const QJsonObject &reply) {
        handleTokenReply(status, reply, refreshToken);
    });
}

void Flow::handleTokenReply(int status, const QJsonObject &reply, const QString &oldRefreshToken) {
    const QString access = reply.value("access_token").toString();
    if (status < 200 || status >= 300 || access.isEmpty()) {
        emit failed(errorText(reply, status));
        return;
    }
    Tokens t;
    t.accessToken = access;
    t.refreshToken = reply.value("refresh_token").toString();
    if (t.refreshToken.isEmpty())
        t.refreshToken = oldRefreshToken;
    t.expires = QDateTime::currentDateTime().addSecs(reply.value("expires_in").toInt(3600));
    if (t.refreshToken.isEmpty()) {
        emit failed(QStringLiteral("The provider sent no refresh token, so Fairfax couldn't stay signed in."));
        return;
    }
    emit finished(t);
}

}  // namespace OAuth
