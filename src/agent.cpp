#include "agent.h"

#include "diagnostics.h"
#include "query.h"

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

// The schema a query compile answers with: one object holding the query string.
const char *QuerySchema = R"({"query": "type: hiprio"})";

// The Fairfax query grammar, shipped to the model so it compiles into the real
// language rather than inventing one. The keyword set mirrors src/query.cpp;
// the value lists name the presets the type: filter understands.
const char *QueryGrammar = R"(Fairfax search queries combine free text and keyword filters:

- Free text matches a task's title, notes and tags. Words are implicitly ANDed;
  quote a phrase to keep it together ("exact phrase"). A leading - or ! excludes
  a word. A trailing * is a prefix match and a leading * a suffix match.
- OR joins alternatives, AND is the implicit join. There is no NOT keyword:
  negate a word with a leading ! or - (e.g. !urgent) and negate a keyword filter
  with ! before it (e.g. !type: donetoday, !tags: work). Parentheses group whole
  filters, e.g. (lists: Work OR lists: Home).
- Keyword filters:
  - type: a task kind or preset. Values include task, subtasks, project, checklist,
    overdue, today, duetoday, tomorrow, duetomorrow, duethisweek, starttoday,
    starttomorrow, startthisweek, inprogress, scheduled, paused, hiprio, medprio,
    lowprio, prio, noprio, starred, repeat, alarms, note, duration, due, nodue,
    start, nostart, anytag, notag, anylocation, nolocation, capturedmail, donetoday,
    doneyesterday, donethisweek, emptyproj, emptychck, alltasks. Separate values
    with commas or OR, e.g. type: hiprio OR medprio.
  - tags: tag names (comma separated, quote names with spaces); tags: ~ means no
    tag and tags: ? means any tag. A leading ! excludes, e.g. tags: !work.
  - taggroups:, lists:, groups:, locations: the same way for tag groups, list
    names, list groups and locations.
  - modified:, created:, completed: a recency like 7d, 2w, >2d for older than.
  - {datefilter} stands for the Smart List's own due-date range.
- Free text and keyword filters AND together. Case does not matter, and a task
  matches notes whether the words appear in the title, the notes or a tag.)";

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

QString AgentService::queryGrammar() { return QString::fromUtf8(QueryGrammar); }

QString AgentService::querySystemPrompt() {
    return QStringLiteral(
        "You translate a plain-English question about a task list into a Fairfax search query. "
        "Answer with one JSON object and nothing else, matching this schema:\n%1\n"
        "%2\n"
        "Put the full query in the \"query\" string. Do not explain it and do not add other keys. "
        "If the question cannot be expressed, answer with the closest query that keeps its words "
        "as free text.")
        .arg(QString::fromUtf8(QuerySchema), queryGrammar());
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

QByteArray AgentService::buildQueryRequest(const QString &prompt) const {
    QJsonObject system{{"role", "system"}, {"content", querySystemPrompt()}};
    QJsonObject user{{"role", "user"}, {"content", prompt}};
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
    send(buildRequest(trimmed), trimmed, Mode::Proposals);
}

void AgentService::compileQuery(const QString &prompt) {
    // The same guard: no compile, no transport, no network while off.
    if (!enabled())
        return;
    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty())
        return;
    send(buildQueryRequest(trimmed), trimmed, Mode::Query);
}

void AgentService::send(const QByteArray &body, const QString &message, Mode mode) {
    cancel();
    ++m_generation;
    const quint64 generation = m_generation;
    m_mode = mode;
    m_lastRequest = message;
    m_lastResponse.clear();
    m_lastError.clear();
    if (mode == Mode::Query) {
        m_compiledQuery.clear();
        m_compiledHint.clear();
        m_compileError.clear();
        setCompiling(true);
        emit compileChanged();
    }
    emit stateChanged();
    setBusy(true);

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
    if (m_compiling)
        setCompiling(false);
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
    } else if (m_mode == Mode::Query) {
        finishQuery(body);
    } else {
        const int count = ingest(body);
        if (count < 0)
            m_lastError = m_lastError.isEmpty() ? QStringLiteral("The model's answer held no JSON.") : m_lastError;
        else
            m_lastError.clear();
    }
    emit stateChanged();
    setBusy(false);
    if (m_mode == Mode::Query) {
        if (!m_lastError.isEmpty())
            m_compileError = m_lastError;
        setCompiling(false);
    }
}

// Reads the assistant's answer, takes its "query" string, and parses it with
// the real query parser. The parse never fails; what it ignored is kept as a
// one-line hint under the field. The ask and result are recorded as a "query"
// proposal so the Diagnostics pane lists the history and applying one re-runs it.
void AgentService::finishQuery(const QByteArray &body) {
    const QByteArray content = assistantContent(body);
    QJsonObject root = QJsonDocument::fromJson(content).object();
    if (root.isEmpty()) {
        const QByteArray object = firstJsonObject(content);
        if (!object.isEmpty())
            root = QJsonDocument::fromJson(object).object();
    }
    if (!root.contains("query") || root.value("query").toString().trimmed().isEmpty()) {
        m_compileError = QStringLiteral("The model's answer held no query.");
        m_lastError = m_compileError;
        Diagnostics::note(QStringLiteral("Agent: ") + m_compileError);
        emit compileChanged();
        return;
    }
    m_compiledQuery = root.value("query").toString().trimmed();
    QString parseError;
    Query::parse(m_compiledQuery, &parseError);
    m_compiledHint = parseError.isEmpty()
                         ? QString()
                         : QStringLiteral("The model's query was adjusted: %1").arg(parseError);
    m_compileError.clear();
    m_lastError.clear();

    // Record the ask and its result as history, applied later by re-running it.
    Proposal p;
    p.kind = QStringLiteral("query");
    p.payload = {{QStringLiteral("prompt"), m_lastRequest}, {QStringLiteral("query"), m_compiledQuery}};
    storeProposals({p});
    emit compileChanged();
    emit queryCompiled();
}

void AgentService::setBusy(bool on) {
    if (m_busy == on)
        return;
    m_busy = on;
    emit busyChanged();
}

void AgentService::setCompiling(bool on) {
    if (m_compiling == on)
        return;
    m_compiling = on;
    emit compileChanged();
}

QByteArray AgentService::assistantContent(const QByteArray &body) {
    // A chat completion keeps the model's answer in the first choice's message
    // content; a transport that returns the object directly has no choices.
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonArray choices = document.object().value("choices").toArray();
        if (!choices.isEmpty())
            return choices.first().toObject().value("message").toObject().value("content").toString().toUtf8();
    }
    return body;
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
