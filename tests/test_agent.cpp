// The proposal engine's rules: the request body it builds, the lenient parsing
// of a model's answer, and applying or dismissing a proposal through the
// Backend. Nothing here touches the network: every request goes through an
// injected transport that answers from a canned body, the same way the CalDAV
// tests use a fake server.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "agent.h"
#include "backend.h"
#include "diagnostics.h"
#include "model.h"
#include "store.h"
#include "testregistry.h"

namespace {

// A world with one list and one task, so a proposal has something to change.
struct Fixture {
    QTemporaryDir dir;
    Store store;
    qint64 listId = 0;
    qint64 workId = 0;
    qint64 tagId = 0;
    qint64 taskId = 0;

    Fixture() {
        [&] { QVERIFY2(store.open(dir.filePath("agent.db"), nullptr), "open"); }();
        store.begin("seed", false);
        TaskList list;
        list.id = store.nextId("lists");
        list.name = "Home";
        store.putList(list);
        TaskList work;
        work.id = store.nextId("lists");
        work.name = "Work";
        work.order = 1;
        store.putList(work);
        Tag garden;
        garden.id = store.nextId("tags");
        garden.name = "garden";
        store.putTag(garden);
        Task task;
        task.id = store.nextId("tasks");
        task.listId = list.id;
        task.title = "Buy milk";
        task.order = task.id;
        store.putTask(task);
        store.commit();
        listId = list.id;
        workId = work.id;
        tagId = garden.id;
        taskId = task.id;
    }
};

// A transport that records what it was asked to send and answers with a canned
// body when deliver() is called, so a request can be cancelled before it lands.
struct FakeTransport {
    QList<QByteArray> requests;
    QUrl lastUrl;
    int timeoutMs = 0;
    int status = 200;
    QByteArray response;
    QString error;
    AgentService::Response done;

    std::function<void()> operator()(const QUrl &url, const QByteArray &body, int timeout, AgentService::Response callback) {
        lastUrl = url;
        requests.append(body);
        timeoutMs = timeout;
        done = callback;
        return [] {};
    }

    // Delivers the canned answer, as the event loop would.
    void deliver() {
        if (done)
            done(status, response, error);
    }
};

QJsonObject parse(const QByteArray &body) { return QJsonDocument::fromJson(body).object(); }

// A response body for one proposal, built by hand so the source keeps no raw
// string literals for the meta-object compiler.
QByteArray oneProposal(qint64 taskId) {
    return QByteArray("{\"proposals\": [{\"kind\": \"set-due-date\", \"payload\": {\"taskId\": ")
           + QByteArray::number(taskId) + ", \"due\": \"2026-07-20\"}}]}";
}

// A chat completion whose assistant message content is one compiled query, as
// a model would answer a compileQuery request.
QByteArray queryAnswer(const QString &query) {
    QJsonObject content{{QStringLiteral("query"), query}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}};
    QJsonObject choice{{QStringLiteral("message"), message}};
    QJsonObject root{{QStringLiteral("choices"), QJsonArray{choice}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// A chat completion wrapping an arbitrary answer object, as a model would
// answer a triage or priority request.
QByteArray wrappedAnswer(const QJsonObject &content) {
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}};
    QJsonObject choice{{QStringLiteral("message"), message}};
    QJsonObject root{{QStringLiteral("choices"), QJsonArray{choice}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// A triage answer for `taskId`, with the named list and tags and a due date.
QByteArray triageAnswer(qint64 taskId, const QString &list, const QStringList &tags, const QString &due, int priority) {
    QJsonArray tagArray;
    for (const QString &t : tags)
        tagArray.append(t);
    QJsonObject row{{QStringLiteral("taskId"), taskId},
                    {QStringLiteral("list"), list},
                    {QStringLiteral("tags"), tagArray},
                    {QStringLiteral("due"), due},
                    {QStringLiteral("priority"), priority}};
    return wrappedAnswer(QJsonObject{{QStringLiteral("triage"), QJsonArray{row}}});
}

// A priority answer for one or more (taskId, priority) pairs.
QByteArray priorityAnswer(const QList<QPair<qint64, int>> &choices) {
    QJsonArray rows;
    for (const auto &c : choices)
        rows.append(QJsonObject{{QStringLiteral("taskId"), c.first}, {QStringLiteral("priority"), c.second}});
    return wrappedAnswer(QJsonObject{{QStringLiteral("priorities"), rows}});
}

}  // namespace

class AgentTest : public QObject {
    Q_OBJECT

private slots:
    // The request body the engine builds, as the schema names it.
    void requestBodyIsBuiltAsSpecified() {
        Fixture f;
        AgentService agent(&f.store);
        QVERIFY(agent.usingDefaultTransport());
        const QJsonObject root = parse(agent.buildRequest("Move my overdue tasks to today"));
        QCOMPARE(root.value("model").toString(), QString());  // No model chosen yet.
        QVERIFY(!root.value("stream").toBool(true));
        const QJsonArray messages = root.value("messages").toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value("role").toString(), QString("system"));
        QVERIFY(messages.at(0).toObject().value("content").toString().contains("proposals"));
        QVERIFY(messages.at(0).toObject().value("content").toString().contains("schema"));
        QCOMPARE(messages.at(1).toObject().value("role").toString(), QString("user"));
        QCOMPARE(messages.at(1).toObject().value("content").toString(), QString("Move my overdue tasks to today"));

        // A chosen model rides in the body.
        f.store.setSetting("agent.model", "llama3.2");
        QCOMPARE(parse(agent.buildRequest("hi")).value("model").toString(), QString("llama3.2"));
    }

    void chatUrlJoinsTheBaseCorrectly() {
        QCOMPARE(AgentService::chatUrl("http://127.0.0.1:11434/v1"), QString("http://127.0.0.1:11434/v1/chat/completions"));
        QCOMPARE(AgentService::chatUrl("http://127.0.0.1:11434/v1/"), QString("http://127.0.0.1:11434/v1/chat/completions"));
        QCOMPARE(AgentService::chatUrl("https://example.com"), QString("https://example.com/chat/completions"));
    }

    // The privacy guard: while the feature is off, no transport is called at
    // all; while it is on, exactly one request goes out.
    void disabledSendsNothing() {
        Fixture f;
        AgentService agent(&f.store);
        FakeTransport transport;
        agent.setTransport(std::ref(transport));
        agent.request("Do something");
        QCOMPARE(transport.requests.size(), 0);
        QVERIFY(!agent.busy());
        QVERIFY(agent.lastRequest().isEmpty());
    }

    void enabledSendsOneRequest() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        FakeTransport transport;
        agent.setTransport(std::ref(transport));
        QSignalSpy busy(&agent, &AgentService::busyChanged);
        agent.request("Do something");
        QCOMPARE(transport.requests.size(), 1);
        QCOMPARE(transport.lastUrl.toString(), QString("http://127.0.0.1:11434/v1/chat/completions"));
        QCOMPARE(transport.timeoutMs, 30000);
        QVERIFY(agent.busy());
        QVERIFY(busy.count() >= 1);
        QCOMPARE(agent.lastRequest(), QString("Do something"));
    }

    // A prose-wrapped JSON object is read anyway.
    void lenientParsingTakesProseWrappedJson() {
        QVERIFY(AgentService::firstJsonObject("Sure! Here is the change: {\"a\": 1} Done.").contains("\"a\""));
        // A brace inside a string does not confuse the scanner.
        const QByteArray wrapped = "Note: {\"text\": \"a } brace\"} tail";
        QCOMPARE(AgentService::firstJsonObject(wrapped), QByteArray("{\"text\": \"a } brace\"}"));
        QVERIFY(AgentService::firstJsonObject("no objects here").isEmpty());
    }

    // A chat completion whose content is wrapped in prose still yields the
    // proposals; invalid JSON leaves an error and creates nothing.
    void ingestHandlesProseAndInvalidJson() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        QSignalSpy changed(&agent, &AgentService::proposalsChanged);

        const QByteArray completion =
            "{\"choices\": [{\"message\": {\"role\": \"assistant\", \"content\": "
            "\"Here you go: {\\\"proposals\\\": [{\\\"kind\\\": \\\"set-due-date\\\", "
            "\\\"payload\\\": {\\\"taskId\\\": 2, \\\"due\\\": \\\"2026-07-20\\\"}}]} Hope that helps!\"}}]}";
        QCOMPARE(agent.ingest(completion), 1);
        QCOMPARE(changed.count(), 1);
        const QVector<Proposal> stored = f.store.proposals();
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().kind, QString("set-due-date"));
        QCOMPARE(stored.first().payload.value("taskId").toLongLong(), qint64(2));

        // Invalid JSON: an error, still no crash, and nothing added.
        const int before = f.store.proposals().size();
        QCOMPARE(agent.ingest("not json at all"), -1);
        QVERIFY(!agent.lastError().isEmpty());
        QCOMPARE(f.store.proposals().size(), before);

        // A transport error is reported and creates nothing.
        QCOMPARE(agent.ingest("{}", "connection refused"), -1);
        QCOMPARE(agent.lastError(), QString("connection refused"));
        QCOMPARE(f.store.proposals().size(), before);
    }

    // A full request cycle: the answer arrives, the proposals are stored, and
    // busy goes back off.
    void requestStoresProposalsFromTheAnswer() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        FakeTransport transport;
        transport.response = oneProposal(1);
        agent.setTransport(std::ref(transport));
        agent.request("Set a due date");
        QVERIFY(agent.busy());
        transport.deliver();
        QVERIFY(!agent.busy());
        QVERIFY(agent.lastError().isEmpty());
        QCOMPARE(f.store.proposals().size(), 1);
    }

    // Cancelling abandons the request: a late answer is ignored.
    void cancelIgnoresALateAnswer() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        FakeTransport transport;
        transport.response = oneProposal(1);
        agent.setTransport(std::ref(transport));
        agent.request("Set a due date");
        QVERIFY(agent.busy());
        agent.cancel();
        QVERIFY(!agent.busy());
        transport.deliver();
        QCOMPARE(f.store.proposals().size(), 0);
    }

    // The compile request body carries the query schema, the real grammar and
    // the user's question; the compile prompt names the grammar's keywords.
    void compileRequestBodyIsBuiltAsSpecified() {
        Fixture f;
        AgentService agent(&f.store);
        const QJsonObject root = parse(agent.buildQueryRequest("what's waiting on Sam?"));
        const QJsonArray messages = root.value("messages").toArray();
        QCOMPARE(messages.size(), 2);
        const QString system = messages.at(0).toObject().value("content").toString();
        QCOMPARE(messages.at(0).toObject().value("role").toString(), QString("system"));
        QVERIFY2(system.contains("query"), qPrintable(system));
        QVERIFY2(system.contains("type:") && system.contains("tags:") && system.contains("lists:"), qPrintable(system));
        QVERIFY2(system.contains("AND") && system.contains("OR"), qPrintable(system));
        // The grammar must not promise a NOT keyword the parser lacks.
        QVERIFY2(!system.contains("NOT / !"), qPrintable(system));
        QCOMPARE(messages.at(1).toObject().value("content").toString(), QString("what's waiting on Sam?"));
        // The static helpers expose the same text for the Diagnostics pane.
        QVERIFY(AgentService::querySystemPrompt().contains(AgentService::queryGrammar()));
        QVERIFY(AgentService::queryGrammar().contains("type:"));
    }

    // A canned query parse-back: the answer's "query" lands in compiledQuery,
    // no parse error, and the ask and result are recorded as history.
    void compileQueryReadsTheModelsQueryBack() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        FakeTransport transport;
        transport.response = queryAnswer("type: hiprio tags: sam");
        agent.setTransport(std::ref(transport));
        QSignalSpy compiled(&agent, &AgentService::queryCompiled);
        QSignalSpy changed(&agent, &AgentService::compileChanged);
        agent.compileQuery("what's waiting on Sam?");
        QVERIFY(agent.compiling());
        QCOMPARE(transport.requests.size(), 1);
        transport.deliver();
        QVERIFY(!agent.compiling());
        QCOMPARE(compiled.count(), 1);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(agent.compiledQuery(), QString("type: hiprio tags: sam"));
        QVERIFY(agent.compiledHint().isEmpty());
        QVERIFY(agent.compileError().isEmpty());
        // The history row records the ask and its result.
        QCOMPARE(f.store.proposals().size(), 1);
        QCOMPARE(f.store.proposals().first().kind, QString("query"));
        QCOMPARE(f.store.proposals().first().payload.value("prompt").toString(), QString("what's waiting on Sam?"));
        QCOMPARE(f.store.proposals().first().payload.value("query").toString(), QString("type: hiprio tags: sam"));
    }

    // A query the parser adjusts lands in the error-free hint state with the
    // one-line "adjusted" message and does not crash.
    void compileQueryKeepsTheParseHint() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        FakeTransport transport;
        transport.response = queryAnswer("type: nonsense");
        agent.setTransport(std::ref(transport));
        agent.compileQuery("something odd");
        transport.deliver();
        // Query::parse never fails; it says what it ignored.
        QCOMPARE(agent.compiledQuery(), QString("type: nonsense"));
        QVERIFY(!agent.compiledHint().isEmpty());
        QVERIFY2(agent.compiledHint().contains("adjusted"), qPrintable(agent.compiledHint()));
        QVERIFY2(agent.compiledHint().contains("nonsense"), qPrintable(agent.compiledHint()));
        QVERIFY(agent.compileError().isEmpty());
        // The row is still recorded; the hint is only a note.
        QCOMPARE(f.store.proposals().size(), 1);
    }

    // An answer with no query string is an error, not a crash, and records
    // nothing.
    void compileQueryWithoutAQueryIsAnError() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        FakeTransport transport;
        transport.response = queryAnswer(QString());
        agent.setTransport(std::ref(transport));
        agent.compileQuery("anything");
        transport.deliver();
        QVERIFY(agent.compiledQuery().isEmpty());
        QVERIFY(!agent.compileError().isEmpty());
        QVERIFY(f.store.proposals().isEmpty());

        // And a transport error is reported, still without a crash.
        FakeTransport failing;
        failing.status = 0;
        failing.error = "connection refused";
        agent.setTransport(std::ref(failing));
        agent.compileQuery("again");
        failing.deliver();
        QVERIFY(agent.compiledQuery().isEmpty());
        QCOMPARE(agent.compileError(), QString("connection refused"));
        QVERIFY(f.store.proposals().isEmpty());
    }

    // With the feature off the compile sends nothing at all; the off-switch is
    // inert.
    void disabledCompileSendsNothing() {
        Fixture f;
        AgentService agent(&f.store);
        FakeTransport transport;
        agent.setTransport(std::ref(transport));
        agent.compileQuery("what's waiting on Sam?");
        QCOMPARE(transport.requests.size(), 0);
        QVERIFY(!agent.compiling());
        QVERIFY(agent.compiledQuery().isEmpty());
        QVERIFY(f.store.proposals().isEmpty());
    }

    // The store round-trips a proposal, payload and all, across a reopen of
    // the same database.
    void proposalsRoundTripThroughTheStore() {
        Fixture f;
        Proposal p;
        p.kind = "set-due-date";
        p.payload = {{"taskId", 7}, {"due", "2026-08-01"}};
        f.store.begin("add proposal");
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        QVERIFY(f.store.commit());

        // A second Store on the same file sees the row, payload and all.
        Store reopened;
        QVERIFY(reopened.open(f.store.path(), nullptr));
        const QVector<Proposal> stored = reopened.proposals();
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().id, p.id);
        QCOMPARE(stored.first().kind, QString("set-due-date"));
        QCOMPARE(stored.first().payload.value("taskId").toLongLong(), qint64(7));
        QCOMPARE(stored.first().payload.value("due").toString(), QString("2026-08-01"));
    }

    // Removing a proposal is a plain store change, so it round-trips too.
    void removalSurvivesReopen() {
        Fixture f;
        Proposal p;
        p.kind = "set-due-date";
        p.payload = {{"taskId", f.taskId}, {"due", "2026-08-01"}};
        f.store.begin("add proposal");
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();
        f.store.begin("dismiss");
        f.store.removeProposal(p.id);
        f.store.commit();

        Store reopened;
        QVERIFY(reopened.open(f.store.path(), nullptr));
        QVERIFY(reopened.proposals().isEmpty());
    }

    // Apply routes by kind to the ordinary Backend method and removes the row.
    void applyChangesDataAndRemovesTheProposal() {
        Fixture f;
        Backend backend(&f.store);
        Proposal p;
        p.kind = "set-due-date";
        p.payload = {{"taskId", f.taskId}, {"due", "2026-07-20"}};
        f.store.begin("add proposal");
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();

        QVERIFY(backend.applyProposal(p.id));
        QCOMPARE(f.store.task(f.taskId).dueDate, QDate(2026, 7, 20));
        QVERIFY(f.store.proposals().isEmpty());
    }

    // Dismiss removes the row and changes nothing else.
    void dismissRemovesWithoutChangingData() {
        Fixture f;
        Backend backend(&f.store);
        Proposal p;
        p.kind = "set-due-date";
        p.payload = {{"taskId", f.taskId}, {"due", "2026-07-20"}};
        f.store.begin("add proposal");
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();

        backend.dismissProposal(p.id);
        QVERIFY(f.store.proposals().isEmpty());
        QVERIFY(!f.store.task(f.taskId).dueDate.isValid());
    }

    // A recorded ask re-runs its compiled query and is kept as history.
    void applyingAQueryHistoryRowRunsItAndKeepsIt() {
        Fixture f;
        Backend backend(&f.store);
        Proposal p;
        p.kind = "query";
        p.payload = {{"prompt", "what's waiting on Sam?"}, {"query", "type: hiprio tags: sam"}};
        f.store.begin("add proposal");
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();

        QVERIFY(backend.applyProposal(p.id));
        QCOMPARE(backend.search(), QString("type: hiprio tags: sam"));
        // History stays put rather than being consumed like a change proposal.
        QCOMPARE(f.store.proposals().size(), 1);

        // An empty recorded query is refused and changes nothing.
        Proposal empty;
        empty.kind = "query";
        empty.payload = {{"prompt", "blank"}, {"query", "  "}};
        f.store.begin("add proposal");
        empty.id = f.store.nextId("proposals");
        f.store.putProposal(empty);
        f.store.commit();
        QVERIFY(!backend.applyProposal(empty.id));
    }

    // An unknown kind is refused, not applied, and stays put for a later build.
    void unknownKindIsRefused() {
        Fixture f;
        Backend backend(&f.store);
        Proposal p;
        p.kind = "reticulate-splines";
        p.payload = {{"taskId", f.taskId}};
        f.store.begin("add proposal");
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();

        QVERIFY(!backend.applyProposal(p.id));
        QCOMPARE(f.store.proposals().size(), 1);
        QVERIFY(backend.statusMessage().contains("unknown suggestion kind"));
        // The task is untouched.
        QCOMPARE(f.store.task(f.taskId).title, QString("Buy milk"));
    }

    // A proposal whose target no longer exists is refused; so is one with an
    // unreadable date.
    void unapplyableProposalsAreRefused() {
        Fixture f;
        Backend backend(&f.store);
        auto add = [&](const QString &kind, const QVariantMap &payload) {
            Proposal p;
            p.kind = kind;
            p.payload = payload;
            f.store.begin("add proposal");
            p.id = f.store.nextId("proposals");
            f.store.putProposal(p);
            f.store.commit();
            return p.id;
        };
        const qint64 gone = add("set-due-date", {{"taskId", 9999}, {"due", "2026-07-20"}});
        QVERIFY(!backend.applyProposal(gone));
        const qint64 badDate = add("set-due-date", {{"taskId", f.taskId}, {"due", "someday"}});
        QVERIFY(!backend.applyProposal(badDate));
        QVERIFY(!f.store.task(f.taskId).dueDate.isValid());
        // A missing proposal is refused too.
        QVERIFY(!backend.applyProposal(123456));
    }

    // The triage request carries the Inbox tasks as JSON and names the real
    // list and tag choices, never anything invented.
    void triageRequestNamesTasksAndRealChoices() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        QVariantList tasks;
        tasks.append(QVariantMap{{"taskId", f.taskId}, {"title", "Buy milk"}, {"notes", "semi-skimmed"}});
        const QStringList lists{"Home", "Work"};
        const QStringList tags{"garden", "errand"};
        const QJsonObject root = parse(agent.buildTriageRequest(tasks, lists, tags));
        const QJsonArray messages = root.value("messages").toArray();
        QCOMPARE(messages.size(), 2);
        const QString system = messages.at(0).toObject().value("content").toString();
        QVERIFY2(system.contains("triage"), qPrintable(system));
        QVERIFY2(system.contains("taskId"), qPrintable(system));
        const QJsonObject user = parse(messages.at(1).toObject().value("content").toString().toUtf8());
        const QJsonArray sentTasks = user.value("tasks").toArray();
        QCOMPARE(sentTasks.size(), 1);
        QCOMPARE(sentTasks.first().toObject().value("title").toString(), QString("Buy milk"));
        QCOMPARE(sentTasks.first().toObject().value("notes").toString(), QString("semi-skimmed"));
        QCOMPARE(user.value("lists").toArray().size(), 2);
        QCOMPARE(user.value("tags").toArray().size(), 2);
        // The buildTriageRequest helper and the system prompt agree.
        QVERIFY(AgentService::triageSystemPrompt().contains("never invent"));
    }

    // The priority request includes each task's current priority as ground truth.
    void priorityRequestIncludesCurrentPriorities() {
        Fixture f;
        AgentService agent(&f.store);
        QVariantList tasks;
        tasks.append(QVariantMap{{"taskId", f.taskId}, {"title", "Buy milk"}, {"notes", ""}, {"priority", 2}});
        const QJsonObject root = parse(agent.buildPriorityRequest(tasks));
        const QString system = root.value("messages").toArray().at(0).toObject().value("content").toString();
        QVERIFY2(system.contains("priorities"), qPrintable(system));
        const QJsonObject user = parse(root.value("messages").toArray().at(1).toObject().value("content").toString().toUtf8());
        QCOMPARE(user.value("tasks").toArray().first().toObject().value("priority").toInt(), 2);
    }

    // An inbox triage request over a real store names only real lists and tags.
    void inboxTriageBuildsFromTheStore() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        // Add an Inbox and a task in it.
        f.store.begin("inbox", false);
        TaskList inbox;
        inbox.id = f.store.nextId("lists");
        inbox.name = "Inbox";
        inbox.kind = ListKind::Inbox;
        f.store.putList(inbox);
        Task t;
        t.id = f.store.nextId("tasks");
        t.listId = inbox.id;
        t.title = "Unfiled thought";
        t.order = t.id;
        f.store.putTask(t);
        f.store.commit();
        Backend backend(&f.store);
        AgentService agent(&f.store);
        backend.setAgent(&agent);
        const QVariantList tasks = backend.inboxTriageTasks();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().toMap().value("title").toString(), QString("Unfiled thought"));
        // The request actually built names only the store's real lists and tags.
        FakeTransport transport;
        agent.setTransport(std::ref(transport));
        backend.triageInbox();
        QCOMPARE(transport.requests.size(), 1);
        const QJsonObject root = parse(transport.requests.first());
        const QJsonObject user = parse(root.value("messages").toArray().at(1).toObject().value("content").toString().toUtf8());
        QStringList lists;
        for (const QJsonValue &v : user.value("lists").toArray())
            lists << v.toString();
        QVERIFY2(lists.contains("Home"), qPrintable(lists.join(',')));
        QVERIFY2(lists.contains("Work"), qPrintable(lists.join(',')));
        QStringList tags;
        for (const QJsonValue &v : user.value("tags").toArray())
            tags << v.toString();
        QVERIFY2(tags.contains("garden"), qPrintable(tags.join(',')));
    }

    // With the feature off neither collection call sends anything, even though
    // the tasks are there.
    void disabledCollectionCallsSendNothing() {
        Fixture f;
        AgentService agent(&f.store);
        Backend backend(&f.store);
        backend.setAgent(&agent);
        FakeTransport transport;
        agent.setTransport(std::ref(transport));
        backend.triageInbox();
        QVariantList tasks;
        tasks.append(QVariantMap{{"taskId", f.taskId}, {"title", "Buy milk"}, {"notes", ""}});
        agent.triageInbox(tasks, {"Home"}, {"garden"});
        agent.suggestPriorities(tasks);
        QCOMPARE(transport.requests.size(), 0);
        QVERIFY(f.store.proposals().isEmpty());
    }

    // A triage answer becomes one "triage" proposal per row, with the choices
    // validated against the request.
    void triageAnswerParsesIntoProposals() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        QCOMPARE(agent.ingestTriage(triageAnswer(f.taskId, "Work", {"garden"}, "friday", 2), {"Home", "Work"}, {"garden"}), 1);
        const QVector<Proposal> stored = f.store.proposals();
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().kind, QString("triage"));
        QCOMPARE(stored.first().payload.value("list").toString(), QString("Work"));
        QCOMPARE(stored.first().payload.value("tags").toStringList(), QStringList{"garden"});
        QCOMPARE(stored.first().payload.value("due").toString(), QString("friday"));
        QCOMPARE(stored.first().payload.value("priority").toInt(), 2);

        // A transport error creates nothing.
        QCOMPARE(agent.ingestTriage("{}", {"Home"}, {}), -1);
        QVERIFY(!agent.lastError().isEmpty());
        QCOMPARE(f.store.proposals().size(), 1);
    }

    // An invented list or tag is dropped with a note, never created.
    void triageAnswerDropsUnknownNames() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        Diagnostics::clear();
        QCOMPARE(agent.ingestTriage(triageAnswer(f.taskId, "Atlantis", {"dragon", "garden"}, "", 0), {"Home", "Work"}, {"garden"}), 1);
        const QVector<Proposal> stored = f.store.proposals();
        QCOMPARE(stored.size(), 1);
        // The unknown list is gone, the known tag kept.
        QVERIFY(!stored.first().payload.contains("list"));
        QCOMPARE(stored.first().payload.value("tags").toStringList(), QStringList{"garden"});
        // No tag was invented in the store.
        for (const auto &t : f.store.tags())
            QVERIFY(t.name != "dragon");
        bool sawList = false, sawTag = false;
        for (const QString &line : Diagnostics::recent()) {
            if (line.contains("Atlantis")) sawList = true;
            if (line.contains("dragon")) sawTag = true;
        }
        QVERIFY2(sawList, qPrintable(Diagnostics::recent().join('\n')));
        QVERIFY2(sawTag, qPrintable(Diagnostics::recent().join('\n')));
    }

    // A triage proposal applies through the real Backend methods: the task
    // moves, is tagged and gets a parsed due date and priority.
    void triageProposalAppliesRealChanges() {
        Fixture f;
        Backend backend(&f.store);
        Proposal p;
        p.kind = "triage";
        p.payload = {{"taskId", f.taskId}, {"list", "Work"}, {"tags", QStringList{"garden"}},
                     {"due", "friday"}, {"priority", 3}};
        f.store.begin("add proposal", false);
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();

        QVERIFY(backend.applyProposal(p.id));
        const Task after = f.store.task(f.taskId);
        QCOMPARE(after.listId, f.workId);
        QCOMPARE(after.tagIds, QList<qint64>{f.tagId});
        QCOMPARE(after.priority, 3);        // The English date went through the real parser, so it is a valid,
        // future QDate, never the raw string.
        QVERIFY(after.dueDate.isValid());
        QVERIFY(after.dueDate >= QDate::currentDate());
        QVERIFY(f.store.proposals().isEmpty());
    }

    // A triage row with an unreadable due date still moves and tags, and the
    // date is left alone rather than written raw.
    void triageIgnoresUnreadableDueDate() {
        Fixture f;
        Backend backend(&f.store);
        Proposal p;
        p.kind = "triage";
        p.payload = {{"taskId", f.taskId}, {"list", "Work"}, {"due", "blursday-ish"}};
        f.store.begin("add proposal", false);
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();

        QVERIFY(backend.applyProposal(p.id));
        QCOMPARE(f.store.task(f.taskId).listId, f.workId);
        QVERIFY(!f.store.task(f.taskId).dueDate.isValid());
        QVERIFY(f.store.proposals().isEmpty());
    }

    // A priority answer becomes "priority" proposals; applying one calls
    // setPriority for that task only.
    void priorityProposalAppliesSetPriority() {
        Fixture f;
        Backend backend(&f.store);
        AgentService agent(&f.store);
        backend.selectTask(f.taskId);
        // selectedPriorityTasks names the selection, current priority and all.
        QCOMPARE(backend.selectedPriorityTasks().size(), 1);
        QCOMPARE(backend.selectedPriorityTasks().first().toMap().value("taskId").toLongLong(), f.taskId);
        QCOMPARE(backend.selectedPriorityTasks().first().toMap().value("priority").toInt(), 0);
        agent.ingestPriorities(priorityAnswer({{f.taskId, 3}}));
        QCOMPARE(f.store.proposals().size(), 1);
        QVERIFY(backend.applyProposal(f.store.proposals().first().id));
        QCOMPARE(f.store.task(f.taskId).priority, 3);
        QVERIFY(f.store.proposals().isEmpty());
    }

    // A priority answer wrapping several tasks creates one proposal each and
    // no proposal for a row with no priority.
    void priorityAnswerParsesIntoProposals() {
        Fixture f;
        f.store.setSetting("agent.enabled", true);
        AgentService agent(&f.store);
        QJsonArray rows;
        rows.append(QJsonObject{{"taskId", f.taskId}, {"priority", 1}});
        rows.append(QJsonObject{{"taskId", 9999}, {"priority", 3}});
        rows.append(QJsonObject{{"taskId", 1234}});  // No priority: skipped.
        QCOMPARE(agent.ingestPriorities(wrappedAnswer(QJsonObject{{"priorities", rows}})), 2);
        QCOMPARE(f.store.proposals().size(), 2);
        QCOMPARE(f.store.proposals().first().kind, QString("priority"));
        QCOMPARE(f.store.proposals().first().payload.value("priority").toInt(), 1);
    }

    // Applying a priority row for a missing task is refused and leaves the
    // queue alone.
    void priorityProposalRefusesMissingTask() {
        Fixture f;
        Backend backend(&f.store);
        Proposal p;
        p.kind = "priority";
        p.payload = {{"taskId", 9999}, {"priority", 3}};
        f.store.begin("add proposal", false);
        p.id = f.store.nextId("proposals");
        f.store.putProposal(p);
        f.store.commit();
        QVERIFY(!backend.applyProposal(p.id));
        QCOMPARE(f.store.proposals().size(), 1);
    }
};

REGISTER_TEST(AgentTest)

#include "test_agent.moc"
