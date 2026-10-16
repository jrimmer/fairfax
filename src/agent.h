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
#include <QStringList>
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
    // The plain-English search compile: the query the model produced, the
    // parse hint when it needed adjusting, and whether a compile is in flight.
    Q_PROPERTY(QString compiledQuery READ compiledQuery NOTIFY compileChanged)
    Q_PROPERTY(QString compiledHint READ compiledHint NOTIFY compileChanged)
    Q_PROPERTY(QString compileError READ compileError NOTIFY compileChanged)
    Q_PROPERTY(bool compiling READ compiling NOTIFY compileChanged)

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
    // Compiles a plain-English question into a Fairfax query. The answer's
    // "query" string is read back through Query::parse and any parse error is
    // kept as a one-line hint; the ask and its result are recorded as a
    // "query" proposal. Also does nothing while disabled.
    Q_INVOKABLE void compileQuery(const QString &prompt);
    // Inbox triage: asks the model to propose a list, tags, due date and
    // priority for every open Inbox task. `tasks` is a list of
    // {taskId, title, notes}; listNames and tagNames are the only choices, so
    // a name the model invents is dropped rather than created. One proposal of
    // kind "triage" per task. Nothing is sent while disabled.
    Q_INVOKABLE void triageInbox(const QVariantList &tasks, const QStringList &listNames, const QStringList &tagNames);
    // Suggest priorities: proposes None/Low/Medium/High for the selected tasks.
    // `tasks` is a list of {taskId, title, notes, priority}, the current
    // priority included so the model learns the user's habits. One proposal of
    // kind "priority" per task. Nothing is sent while disabled.
    Q_INVOKABLE void suggestPriorities(const QVariantList &tasks);
    // Abandons the request in flight; a late answer is ignored.
    Q_INVOKABLE void cancel();
    bool busy() const { return m_busy; }
    QString lastRequest() const { return m_lastRequest; }
    QString lastResponse() const { return m_lastResponse; }
    QString lastError() const { return m_lastError; }
    QString compiledQuery() const { return m_compiledQuery; }
    QString compiledHint() const { return m_compiledHint; }
    QString compileError() const { return m_compileError; }
    bool compiling() const { return m_compiling; }

    // The request body, for tests and for the Diagnostics pane.
    QByteArray buildRequest(const QString &message) const;
    static QString systemPrompt();
    // The query-compile request body and system prompt, for tests.
    QByteArray buildQueryRequest(const QString &prompt) const;
    static QString querySystemPrompt();
    // The triage and priority request bodies and system prompts, for tests.
    QByteArray buildTriageRequest(const QVariantList &tasks, const QStringList &listNames, const QStringList &tagNames) const;
    QByteArray buildPriorityRequest(const QVariantList &tasks) const;
    static QString triageSystemPrompt();
    static QString prioritySystemPrompt();
    // The query grammar the compile prompt names, as one static string.
    static QString queryGrammar();
    // The chat-completions URL for a configured base URL.
    static QString chatUrl(const QString &baseUrl);

    // Parses a response and stores every proposal it holds; returns how many.
    // Returns -1 and sets lastError for a response that carries no usable
    // JSON or when `transportError` is set.
    int ingest(const QByteArray &body, const QString &transportError = QString());
    // Parses a triage answer into "triage" proposals, one per row. A list or
    // tag name not among the choices is dropped with a Diagnostics note rather
    // than invented. Returns the count, or -1 for an unreadable answer.
    int ingestTriage(const QByteArray &body, const QStringList &listNames, const QStringList &tagNames);
    // Parses a priority answer into "priority" proposals. Returns the count, or
    // -1 for an unreadable answer.
    int ingestPriorities(const QByteArray &body);
    // The first balanced JSON object in `text`, ignoring prose around it.
    static QByteArray firstJsonObject(const QByteArray &text);

    // What the review sheet lists, oldest first (the sheet appends).
    QVector<Proposal> pending() const;

signals:
    void busyChanged();
    void stateChanged();
    void proposalsChanged();
    void compileChanged();
    void queryCompiled();  // A compile finished; compiledQuery/hint are set.

private:
    // What the request in flight is for: proposals or a query compile.
    enum class Mode { Proposals, Query, Triage, Priorities };

    void send(const QByteArray &body, const QString &message, Mode mode);
    void finish(int status, const QByteArray &body, const QString &error);
    void finishQuery(const QByteArray &body);
    void setBusy(bool on);
    void setCompiling(bool on);
    int storeProposals(const QList<Proposal> &proposals);
    // The assistant message's content, or the whole body when there is none.
    static QByteArray assistantContent(const QByteArray &body);
    QJsonObject proposalRoot(const QByteArray &body) const;
    // The JSON object an answer carries, whether it is a chat completion or the
    // object itself, prose around it allowed.
    QJsonObject answerRoot(const QByteArray &body) const;
    std::function<void()> sendOverNetwork(const QUrl &url, const QByteArray &body, int timeoutMs, Response done);

    Store *m_store;
    Transport m_transport;
    bool m_transportSet = false;
    bool m_busy = false;
    bool m_compiling = false;
    Mode m_mode = Mode::Proposals;
    quint64 m_generation = 0;
    std::function<void()> m_abort;
    QString m_lastRequest;
    QString m_lastResponse;
    QString m_lastError;
    QString m_compiledQuery;
    QString m_compiledHint;
    QString m_compileError;
    // The choices a triage answer is validated against, kept from the request.
    QStringList m_triageLists;
    QStringList m_triageTags;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
};
