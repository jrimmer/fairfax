#include "agent.h"

#include "diagnostics.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int RequestTimeoutMs = 30000;

// The JSON schema the system prompt asks for: a root object with a
// "proposals" array whose items each carry a "kind" and a "payload".
const char *Schema = R"({
  "proposals": [
    {
      "kind": "set-due-date",
      "payload": {"taskId": 12, "due": "2026-07-20"}
    }
  ]
})";

// Walks `text` from the first '{' to its matching '}', skipping strings so a
// brace inside a string does not fool it. Returns an empty array when there
// is no object.
QByteArray balancedObject(const QByteArray &text, int start) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = start; i < text.size(); ++i) {
        const char c = text.at(i);
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (--depth == 0)
                return text.mid(start, i - start + 1);
        }
    }
    return {};
}

}  // namespace

AgentService::AgentService(Store *store, QObject *parent) : QObject(parent), m_store(store) {}

void AgentService::setTransport(Transport transport) {
    m_transport = std::move(transport);
    m_transportSet = true;
}

bool AgentService::enabled() const { return m_store->setting("agent.enabled", false).toBool(); }

QString AgentService::baseUrl() const {
    const QString raw = m_store->setting("agent.baseUrl", "http://127.0.0.1:11434/v1").toString().trimmed();
    return raw.isEmpty() ? QStringLiteral("http://127.0.0.1:11434/v1") : raw;
}

QString AgentService::model() const { return m_store->setting("agent.model", QString()).toString().trimmed(); }

QString AgentService::chatUrl(const QString &baseUrl) {
    QString base = baseUrl.trimmed();
    while (base.endsWith('/'))
        base.chop(1);
    return base + "/chat/completions";
}

QString AgentService::systemPrompt() {
    return QStringLiteral(
        "You suggest changes to a task manager. Answer with one JSON object and nothing else, "
        "matching this schema:\n%1\n"
        "Each proposal has a \"kind\" naming the change and a \"payload\" object with its fields. "
        "Use only the kinds and fields the schema lists. Do not explain the JSON.")
        .arg(QString::fromUtf8(Schema));
}

QByteArray AgentService::buildRequest(const QString &message) const {
    QJsonObject system{{"role", "system"}, {"content", systemPrompt()}};
    QJsonObject user{{"role", "user"}, {"content", message}};
    QJsonObject root;
    root.insert("model", model());
    root.insert("messages", QJsonArray{system, user});
    root.insert("stream", false);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray AgentService::firstJsonObject(const QByteArray &text) {
    int start = text.indexOf('{');
    while (start >= 0) {
        const QByteArray candidate = balancedObject(text, start);
        if (!candidate.isEmpty())
            return candidate;
        start = text.indexOf('{', start + 1);
    }
    return {};
}

std::function<void()> AgentService::sendOverNetwork(const QUrl &url, const QByteArray &body, int timeoutMs, Response done) {
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", "Fairfax");
    QNetworkReply *reply = m_network->post(request, body);
    m_reply = reply;
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply] { reply->abort(); });
    timer->start(qMax(1000, timeoutMs));
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, done, timer] {
        timer->stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        const QString error = status == 0 ? reply->errorString() : QString();
        reply->deleteLater();
        done(status, payload, error);
    });
    return [reply] { reply->abort(); };
}

void AgentService::request(const QString &message) {
    // The privacy guard: nothing is sent while the feature is off.
    if (!enabled())
        return;
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty())
        return;
    cancel();
    ++m_generation;
    const quint64 generation = m_generation;
    m_lastRequest = trimmed;
    m_lastResponse.clear();
    m_lastError.clear();
    emit stateChanged();
    setBusy(true);

    const QByteArray body = buildRequest(trimmed);
    const QUrl url(chatUrl(baseUrl()));
    Transport transport = m_transport;
    if (!transport)
        transport = [this](const QUrl &u, const QByteArray &b, int timeout, Response done) {
            return sendOverNetwork(u, b, timeout, done);
        };
    auto guard = std::make_shared<quint64>(generation);
    m_abort = transport(url, body, RequestTimeoutMs,
                        [this, guard](int status, const QByteArray &response, const QString &error) {
                            if (*guard != m_generation)
                                return;  // Cancelled or replaced: a late answer is ignored.
                            finish(status, response, error);
                        });
}

void AgentService::cancel() {
    ++m_generation;  // Ignore whatever the transport has in flight.
    if (m_abort) {
        auto abort = m_abort;
        m_abort = nullptr;
        abort();
    }
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }
    if (m_busy)
        setBusy(false);
}

void AgentService::finish(int status, const QByteArray &body, const QString &error) {
    m_abort = nullptr;
    m_reply = nullptr;
    m_lastResponse = QString::fromUtf8(body);
    if (!error.isEmpty() || status == 0) {
        m_lastError = error.isEmpty() ? QStringLiteral("The model did not answer.") : error;
    } else if (status < 200 || status >= 300) {
        m_lastError = QStringLiteral("The model answered %1.").arg(status);
        Diagnostics::note(QStringLiteral("Agent: request failed with HTTP %1").arg(status));
    } else {
        const int count = ingest(body);
        if (count < 0)
            m_lastError = m_lastError.isEmpty() ? QStringLiteral("The model's answer held no JSON.") : m_lastError;
        else
            m_lastError.clear();
    }
    emit stateChanged();
    setBusy(false);
}

void AgentService::setBusy(bool on) {
    if (m_busy == on)
        return;
    m_busy = on;
    emit busyChanged();
}

QJsonObject AgentService::proposalRoot(const QByteArray &body) const {
    // The response is an OpenAI chat completion; the model's answer is the
    // assistant message's content. Fall back to the whole body for a
    // transport that already returns the proposals directly.
    const QJsonDocument document = QJsonDocument::fromJson(body);
    QByteArray content;
    if (document.isObject()) {
        const QJsonObject root = document.object();
        const QJsonArray choices = root.value("choices").toArray();
        if (!choices.isEmpty())
            content = choices.first().toObject().value("message").toObject().value("content").toString().toUtf8();
        if (content.isEmpty() && root.contains("proposals"))
            return root;
    }
    const QByteArray object = firstJsonObject(content.isEmpty() ? body : content);
    if (object.isEmpty())
        return {};
    return QJsonDocument::fromJson(object).object();
}

int AgentService::ingest(const QByteArray &body, const QString &transportError) {
    if (!transportError.isEmpty()) {
        m_lastError = transportError;
        return -1;
    }
    const QJsonObject root = proposalRoot(body);
    if (root.isEmpty() || !root.contains("proposals")) {
        m_lastError = QStringLiteral("The model's answer held no proposals.");
        Diagnostics::note(QStringLiteral("Agent: ") + m_lastError);
        return -1;
    }
    QList<Proposal> proposals;
    for (const QJsonValue &value : root.value("proposals").toArray()) {
        const QJsonObject item = value.toObject();
        const QString kind = item.value("kind").toString().trimmed();
        if (kind.isEmpty())
            continue;
        if (!item.value("payload").isObject())
            continue;
        Proposal p;
        p.kind = kind;
        p.payload = item.value("payload").toObject().toVariantMap();
        proposals.append(p);
    }
    return storeProposals(proposals);
}

int AgentService::storeProposals(const QList<Proposal> &proposals) {
    m_store->begin("Suggest Changes");
    for (Proposal p : proposals) {
        p.id = m_store->nextId("proposals");
        m_store->putProposal(p);
    }
    m_store->commit();
    if (!proposals.isEmpty())
        emit proposalsChanged();
    return proposals.size();
}

QVector<Proposal> AgentService::pending() const { return m_store->proposals(); }
