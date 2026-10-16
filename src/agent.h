#pragma once

// The proposal engine: one place where a model's suggestions become rows the
// user reviews. A request carries a system prompt naming the JSON schema and
// the user's message; the answer is parsed leniently, so a model that wraps
// its JSON in prose still works. Every proposal is persisted, so a review
// survives a restart, and applying one routes through the Backend's ordinary
// methods rather than a new write path.
//
// The transport is injectable so tests never touch the network: the default
// talks to an OpenAI-compatible /v1/chat/completions endpoint over
// QNetworkAccessManager, localhost by default. With agent.enabled off nothing
// is sent anywhere.

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

#include "model.h"
#include "store.h"

class QNetworkAccessManager;
class QNetworkReply;

class AgentService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastRequest READ lastRequest NOTIFY stateChanged)
    Q_PROPERTY(QString lastResponse READ lastResponse NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    // Performs one request and calls `done` when it finishes. `status` is the
    // HTTP status (0 for a network error), `body` the response and `error` a
    // human message when the request itself failed. Returns an abort function,
    // which may be empty.
    using Response = std::function<void(int status, const QByteArray &body, const QString &error)>;
    using Transport = std::function<std::function<void()>(const QUrl &url, const QByteArray &body, int timeoutMs, Response done)>;

    explicit AgentService(Store *store, QObject *parent = nullptr);

    // Replaces the network transport, for tests and for a future local model
    // running in-process. A set transport is used even while the feature is
    // off, so a test can prove that nothing is sent.
    void setTransport(Transport transport);
    bool usingDefaultTransport() const { return !m_transportSet; }

    bool enabled() const;
    QString baseUrl() const;
    QString model() const;

    // Sends one message to the model. Does nothing at all while disabled:
    // no transport call, no network.
    Q_INVOKABLE void request(const QString &message);
    // Abandons the request in flight; a late answer is ignored.
    Q_INVOKABLE void cancel();
    bool busy() const { return m_busy; }
    QString lastRequest() const { return m_lastRequest; }
    QString lastResponse() const { return m_lastResponse; }
    QString lastError() const { return m_lastError; }

    // The request body, for tests and for the Diagnostics pane.
    QByteArray buildRequest(const QString &message) const;
    static QString systemPrompt();
    // The chat-completions URL for a configured base URL.
    static QString chatUrl(const QString &baseUrl);

    // Parses a response and stores every proposal it holds; returns how many.
    // Returns -1 and sets lastError for a response that carries no usable
    // JSON or when `transportError` is set.
    int ingest(const QByteArray &body, const QString &transportError = QString());
    // The first balanced JSON object in `text`, ignoring prose around it.
    static QByteArray firstJsonObject(const QByteArray &text);

    // What the review sheet lists, oldest first (the sheet appends).
    QVector<Proposal> pending() const;

signals:
    void busyChanged();
    void stateChanged();
    void proposalsChanged();

private:
    void finish(int status, const QByteArray &body, const QString &error);
    void setBusy(bool on);
    int storeProposals(const QList<Proposal> &proposals);
    QJsonObject proposalRoot(const QByteArray &body) const;
    std::function<void()> sendOverNetwork(const QUrl &url, const QByteArray &body, int timeoutMs, Response done);

    Store *m_store;
    Transport m_transport;
    bool m_transportSet = false;
    bool m_busy = false;
    quint64 m_generation = 0;
    std::function<void()> m_abort;
    QString m_lastRequest;
    QString m_lastResponse;
    QString m_lastError;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
};
