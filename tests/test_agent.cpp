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
#include "model.h"
#include "store.h"
#include "testregistry.h"

namespace {

// A world with one list and one task, so a proposal has something to change.
struct Fixture {
    QTemporaryDir dir;
    Store store;
    qint64 listId = 0;
    qint64 taskId = 0;

    Fixture() {
        [&] { QVERIFY2(store.open(dir.filePath("agent.db"), nullptr), "open"); }();
        store.begin("seed", false);
        TaskList list;
        list.id = store.nextId("lists");
        list.name = "Home";
        store.putList(list);
        Task task;
        task.id = store.nextId("tasks");
        task.listId = list.id;
        task.title = "Buy milk";
        task.order = task.id;
        store.putTask(task);
        store.commit();
        listId = list.id;
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
};

REGISTER_TEST(AgentTest)

#include "test_agent.moc"
