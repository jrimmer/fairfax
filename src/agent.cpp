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

// The schema an inbox triage answers with: one "triage" array, one row per
// task. A row's list and tags must name the choices the request supplied.
const char *TriageSchema = R"({
  "triage": [
    {"taskId": 12, "list": "Home", "tags": ["errand"], "due": "friday", "priority": 2}
  ]
})";

// The schema a priority suggestion answers with: one "priorities" array.
const char *PrioritySchema = R"({
  "priorities": [
    {"taskId": 12, "priority": 2}
  ]
})";

// The schema a project breakdown answers with: one "steps" array, each step a
// title and, when the user has a habit for it, a duration in minutes.
const char *StepsSchema = R"({
  "steps": [
    {"title": "Draft the outline", "duration": 30}
  ]
})";

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

QString AgentService::triageSystemPrompt() {
    return QStringLiteral(
        "You triage the open tasks of a task manager's Inbox. For each task, choose a list to move "
        "it to, tags to add, a due date and a priority. Answer with one JSON object and nothing else, "
        "matching this schema:\n%1\n"
        "Each row carries the task's \"taskId\" and holds only the changes you suggest. Use only list "
        "and tag names given in the request: never invent a name. The \"due\" value is an English "
        "phrase such as \"friday\", \"tomorrow\", \"next week\" or \"in 3 days\", or omitted for no "
        "date. \"priority\" is 0 for none, 1 for low, 2 for medium and 3 for high. Do not explain the JSON.")
        .arg(QString::fromUtf8(TriageSchema));
}

QString AgentService::prioritySystemPrompt() {
    return QStringLiteral(
        "You suggest a priority for each of a task manager's selected tasks. Each task's current "
        "priority is given as a guide to the user's habits. Answer with one JSON object and nothing "
        "else, matching this schema:\n%1\n"
        "Each row carries the task's \"taskId\" and a \"priority\": 0 for none, 1 for low, 2 for medium "
        "and 3 for high. Do not explain the JSON.")
        .arg(QString::fromUtf8(PrioritySchema));
}

QString AgentService::stepsSystemPrompt() {
    return QStringLiteral(
        "You break a task or project down into the concrete steps it takes. Answer with one JSON "
        "object and nothing else, matching this schema:\n%1\n"
        "Each step has a \"title\" and, when a sensible estimate is possible, a \"duration\" in minutes. "
        "The request lists the task's title and notes, its existing checklist items or sub-tasks so "
        "you do not repeat them, and the durations this user tends to use, so a duration can follow "
        "their habits. Keep the steps in the order they should be done and do not explain the JSON.")
        .arg(QString::fromUtf8(StepsSchema));
}

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

QByteArray AgentService::buildTriageRequest(const QVariantList &tasks, const QStringList &listNames, const QStringList &tagNames) const {
    QJsonArray taskArray;
    for (const QVariant &v : tasks) {
        const QVariantMap t = v.toMap();
        taskArray.append(QJsonObject{{QStringLiteral("taskId"), t.value("taskId").toLongLong()},
                                     {QStringLiteral("title"), t.value("title").toString()},
                                     {QStringLiteral("notes"), t.value("notes").toString()}});
    }
    QJsonArray lists;
    for (const QString &name : listNames)
        lists.append(name);
    QJsonArray tags;
    for (const QString &name : tagNames)
        tags.append(name);
    QJsonObject user;
    user.insert("tasks", taskArray);
    user.insert("lists", lists);
    user.insert("tags", tags);
    const QString prompt = QString::fromUtf8(QJsonDocument(user).toJson(QJsonDocument::Compact));
    QJsonObject system{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), triageSystemPrompt()}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), prompt}};
    QJsonObject root;
    root.insert("model", model());
    root.insert("messages", QJsonArray{system, message});
    root.insert("stream", false);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray AgentService::buildPriorityRequest(const QVariantList &tasks) const {
    QJsonArray taskArray;
    for (const QVariant &v : tasks) {
        const QVariantMap t = v.toMap();
        taskArray.append(QJsonObject{{QStringLiteral("taskId"), t.value("taskId").toLongLong()},
                                     {QStringLiteral("title"), t.value("title").toString()},
                                     {QStringLiteral("notes"), t.value("notes").toString()},
                                     {QStringLiteral("priority"), t.value("priority").toInt()}});
    }
    QJsonObject user;
    user.insert("tasks", taskArray);
    const QString prompt = QString::fromUtf8(QJsonDocument(user).toJson(QJsonDocument::Compact));
    QJsonObject system{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), prioritySystemPrompt()}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), prompt}};
    QJsonObject root;
    root.insert("model", model());
    root.insert("messages", QJsonArray{system, message});
    root.insert("stream", false);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray AgentService::buildStepsRequest(const QVariantMap &task, const QStringList &existingChildren, const QVariantList &durations) const {
    QJsonObject taskObject{{QStringLiteral("taskId"), task.value("taskId").toLongLong()},
                           {QStringLiteral("title"), task.value("title").toString()},
                           {QStringLiteral("notes"), task.value("notes").toString()}};
    QJsonArray children;
    for (const QString &title : existingChildren)
        children.append(title);
    QJsonArray durationArray;
    for (const QVariant &value : durations)
        durationArray.append(value.toInt());
    QJsonObject user;
    user.insert("task", taskObject);
    user.insert("existingChildren", children);
    user.insert("durations", durationArray);
    const QString prompt = QString::fromUtf8(QJsonDocument(user).toJson(QJsonDocument::Compact));
    QJsonObject system{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), stepsSystemPrompt()}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), prompt}};
    QJsonObject root;
    root.insert("model", model());
    root.insert("messages", QJsonArray{system, message});
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

void AgentService::triageInbox(const QVariantList &tasks, const QStringList &listNames, const QStringList &tagNames) {
    // The same guard: no triage, no transport, no network while off.
    if (!enabled() || tasks.isEmpty())
        return;
    m_triageLists = listNames;
    m_triageTags = tagNames;
    send(buildTriageRequest(tasks, listNames, tagNames), QStringLiteral("Triage %1 Inbox task(s)").arg(tasks.size()), Mode::Triage);
}

void AgentService::suggestPriorities(const QVariantList &tasks) {
    if (!enabled() || tasks.isEmpty())
        return;
    send(buildPriorityRequest(tasks), QStringLiteral("Suggest priorities for %1 task(s)").arg(tasks.size()), Mode::Priorities);
}

void AgentService::suggestSteps(const QVariantMap &task, const QStringList &existingChildren, const QVariantList &durations) {
    // The same guard: no breakdown, no transport, no network while off.
    if (!enabled())
        return;
    const qint64 taskId = task.value("taskId").toLongLong();
    if (!taskId)
        return;
    m_stepsTaskId = taskId;
    send(buildStepsRequest(task, existingChildren, durations),
         QStringLiteral("Break down \u201c%1\u201d").arg(task.value("title").toString()), Mode::Steps);
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
    } else if (m_mode == Mode::Triage) {
        const int count = ingestTriage(body, m_triageLists, m_triageTags);
        if (count < 0)
            m_lastError = m_lastError.isEmpty() ? QStringLiteral("The model's answer held no triage.") : m_lastError;
        else
            m_lastError.clear();
    } else if (m_mode == Mode::Priorities) {
        const int count = ingestPriorities(body);
        if (count < 0)
            m_lastError = m_lastError.isEmpty() ? QStringLiteral("The model's answer held no priorities.") : m_lastError;
        else
            m_lastError.clear();
    } else if (m_mode == Mode::Steps) {
        const int count = ingestSteps(body, m_stepsTaskId);
        if (count < 0)
            m_lastError = m_lastError.isEmpty() ? QStringLiteral("The model's answer held no steps.") : m_lastError;
        else
            m_lastError.clear();
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

QJsonObject AgentService::answerRoot(const QByteArray &body) const {
    // The response may be an OpenAI chat completion whose assistant message
    // holds the JSON, or the JSON object itself; prose around it is allowed.
    const QByteArray content = assistantContent(body);
    QJsonObject root = QJsonDocument::fromJson(content).object();
    if (root.isEmpty()) {
        const QByteArray object = firstJsonObject(content);
        if (!object.isEmpty())
            root = QJsonDocument::fromJson(object).object();
    }
    return root;
}

int AgentService::ingestTriage(const QByteArray &body, const QStringList &listNames, const QStringList &tagNames) {
    const QJsonObject root = answerRoot(body);
    if (root.isEmpty() || !root.contains("triage")) {
        m_lastError = QStringLiteral("The model's answer held no triage.");
        Diagnostics::note(QStringLiteral("Agent: ") + m_lastError);
        return -1;
    }
    QList<Proposal> proposals;
    for (const QJsonValue &value : root.value("triage").toArray()) {
        const QJsonObject item = value.toObject();
        const qint64 taskId = item.value("taskId").toVariant().toLongLong();
        if (!taskId)
            continue;
        QVariantMap payload{{QStringLiteral("taskId"), taskId}};
        // A list name the model invented is dropped with a note, never created.
        const QString wantedList = item.value("list").toString().trimmed();
        QString list;
        for (const QString &name : listNames)
            if (!wantedList.isEmpty() && name.compare(wantedList, Qt::CaseInsensitive) == 0)
                list = name;
        if (!wantedList.isEmpty() && list.isEmpty())
            Diagnostics::note(QStringLiteral("Agent: ignored unknown list \"%1\"").arg(wantedList));
        if (!list.isEmpty())
            payload.insert(QStringLiteral("list"), list);
        QStringList tags;
        for (const QJsonValue &tagValue : item.value("tags").toArray()) {
            const QString wanted = tagValue.toString().trimmed();
            if (wanted.isEmpty())
                continue;
            QString known;
            for (const QString &name : tagNames)
                if (name.compare(wanted, Qt::CaseInsensitive) == 0)
                    known = name;
            if (known.isEmpty())
                Diagnostics::note(QStringLiteral("Agent: ignored unknown tag \"%1\"").arg(wanted));
            else if (!tags.contains(known))
                tags << known;
        }
        if (!tags.isEmpty())
            payload.insert(QStringLiteral("tags"), tags);
        const QString due = item.value("due").toString().trimmed();
        if (!due.isEmpty())
            payload.insert(QStringLiteral("due"), due);
        if (item.value("priority").isDouble() || item.value("priority").isString())
            payload.insert(QStringLiteral("priority"), qBound(0, item.value("priority").toVariant().toInt(), 3));
        Proposal p;
        p.kind = QStringLiteral("triage");
        p.payload = payload;
        proposals.append(p);
    }
    return storeProposals(proposals);
}

int AgentService::ingestPriorities(const QByteArray &body) {
    const QJsonObject root = answerRoot(body);
    if (root.isEmpty() || !root.contains("priorities")) {
        m_lastError = QStringLiteral("The model's answer held no priorities.");
        Diagnostics::note(QStringLiteral("Agent: ") + m_lastError);
        return -1;
    }
    QList<Proposal> proposals;
    for (const QJsonValue &value : root.value("priorities").toArray()) {
        const QJsonObject item = value.toObject();
        const qint64 taskId = item.value("taskId").toVariant().toLongLong();
        if (!taskId || !(item.value("priority").isDouble() || item.value("priority").isString()))
            continue;
        Proposal p;
        p.kind = QStringLiteral("priority");
        p.payload = {{QStringLiteral("taskId"), taskId},
                     {QStringLiteral("priority"), qBound(0, item.value("priority").toVariant().toInt(), 3)}};
        proposals.append(p);
    }
    return storeProposals(proposals);
}

int AgentService::ingestSteps(const QByteArray &body, qint64 taskId) {
    const QJsonObject root = answerRoot(body);
    if (root.isEmpty() || !root.contains("steps")) {
        m_lastError = QStringLiteral("The model's answer held no steps.");
        Diagnostics::note(QStringLiteral("Agent: ") + m_lastError);
        return -1;
    }
    QList<Proposal> proposals;
    for (const QJsonValue &value : root.value("steps").toArray()) {
        const QJsonObject item = value.toObject();
        const QString title = item.value("title").toString().trimmed();
        if (title.isEmpty())
            continue;
        Proposal p;
        p.kind = QStringLiteral("step");
        p.payload = {{QStringLiteral("taskId"), taskId}, {QStringLiteral("title"), title}};
        // A duration is optional: a step with none is perfectly valid and
        // simply becomes a child with no duration.
        if (item.value("duration").isDouble() || item.value("duration").isString()) {
            const int minutes = item.value("duration").toVariant().toInt();
            if (minutes > 0)
                p.payload.insert(QStringLiteral("duration"), minutes);
        }
        proposals.append(p);
    }
    return storeProposals(proposals);
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
