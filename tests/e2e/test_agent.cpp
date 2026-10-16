// End-to-end tests for the proposal engine: the opt-in Settings rows, the
// privacy guard (nothing leaves the machine while the feature is off), and the
// review sheet applying or dismissing a canned model answer. The transport is
// injected, so no test reaches the network; the model's answer is a fixed
// string.

#include "agent.h"
#include "taskkit.h"
#include "settingskit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::tk;

namespace {

// The canned answer a fake transport delivers, as a chat completion whose
// content is the proposals JSON.
QByteArray cannedAnswer(qint64 taskId, const QString &due) {
    QJsonObject payload{{"taskId", taskId}, {"due", due}};
    QJsonObject proposal{{"kind", "set-due-date"}, {"payload", payload}};
    QJsonObject content{{"proposals", QJsonArray{proposal}}};
    const QByteArray inner = QJsonDocument(content).toJson(QJsonDocument::Compact);
    QJsonObject message{{"role", "assistant"}, {"content", QString::fromUtf8(inner)}};
    QJsonObject choice{{"message", message}};
    QJsonObject root{{"choices", QJsonArray{choice}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// A fake transport that records every request and holds the answer until
// deliver() runs.
struct Transport {
    QList<QByteArray> requests;
    QByteArray answer;
    AgentService::Response done;

    std::function<void()> operator()(const QUrl &, const QByteArray &body, int, AgentService::Response callback) {
        requests.append(body);
        done = callback;
        return [] {};
    }
    void deliver() {
        if (done)
            done(200, answer, QString());
    }
};

// The store the agent tests run on: the default world, with the agent off
// unless a test asks for it.
Seed seedAgentOff(Store &store) {
    const Seed s = seedWorld(store);
    store.setSetting("agent.enabled", false);
    return s;
}

}  // namespace

class E2EAgent : public QObject {
    Q_OBJECT

private slots:
    // With the setting off, a request makes no transport call at all and no
    // suggestion UI appears.
    void disabledSendsNothingAndShowsNothing() {
        App app(seedAgentOff);
        QVERIFY(app.ok);
        AgentService &agent = app.services().agent;
        Transport transport;
        agent.setTransport(std::ref(transport));
        agent.request("Move everything to today");
        QCOMPARE(transport.requests.size(), 0);
        QVERIFY(agent.lastRequest().isEmpty());
        // No toolbar entry either.
        QTest::qWait(100);
        QVERIFY(findItem("suggestionsButton") == nullptr);
        QCOMPARE(app.backend().proposalCount(), 0);
    }

    // The Settings rows exist, enable the feature, and persist across a
    // restart. This test also covers the three keys the settings guard names.
    void settingsRowsExistAndPersist() {
        Session s;
        s.seed = seedAgentOff;
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Sync");
        // Off by default; the server and model fields are disabled with a hint.
        QQuickItem *enable = nullptr;
        control("setting:agent.enabled", enable);
        QVERIFY(!isChecked(enable));
        QVERIFY(!findItem("setting:agent.baseUrl")->isEnabled());
        QVERIFY(!findItem("setting:agent.model")->isEnabled());
        QVERIFY(textMatching(sw->contentItem(), QRegularExpression("Turn on Offer suggested changes")) != nullptr);

        setCheck("setting:agent.enabled", true);
        QTRY_COMPARE_WITH_TIMEOUT(app.store().setting("agent.enabled").toString(), QString("true"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("setting:agent.baseUrl")->isEnabled(), 5000);
        setText(sw, "setting:agent.baseUrl", "http://127.0.0.1:9999/v1");
        setText(sw, "setting:agent.model", "llama3.2");
        QCOMPARE(app.store().setting("agent.baseUrl").toString(), QString("http://127.0.0.1:9999/v1"));
        QCOMPARE(app.store().setting("agent.model").toString(), QString("llama3.2"));

        // A restart keeps the switch and both fields.
        QVERIFY(s.restart());
        App &again = *s.app;
        QVERIFY(again.backend().agentEnabled());
        QCOMPARE(again.store().setting("agent.baseUrl").toString(), QString("http://127.0.0.1:9999/v1"));
        QCOMPARE(again.store().setting("agent.model").toString(), QString("llama3.2"));
        // The toolbar entry is there now that the feature is on.
        QQuickItem *button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("suggestionsButton")) != nullptr, 5000);
        QVERIFY(button->isVisible());
    }

    // With the feature on and a canned answer, the sheet lists the proposal;
    // Y applies it through the real Backend, changing the task.
    void reviewSheetAppliesAProposal() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedAgentOff(st);
            st.setSetting("agent.enabled", true);
            return ids;
        };
        QVERIFY(s.start());
        App &app = *s.app;
        const qint64 taskId = s.ids.buyMilk;
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = cannedAnswer(taskId, "2026-08-01");
        agent.setTransport(std::ref(transport));
        agent.request("Set the due date");
        QCOMPARE(transport.requests.size(), 1);
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 1, 5000);

        // Open the sheet from the toolbar.
        QQuickItem *button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("suggestionsButton")) != nullptr, 5000);
        click(button);
        QObject *sheet = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((sheet = findObject("proposalSheet")) != nullptr && sheet->property("visible").toBool(), 5000);
        QQuickItem *list = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((list = findItem("proposalList")) != nullptr, 5000);
        QCOMPARE(list->property("count").toInt(), 1);

        // Y applies the highlighted proposal.
        app.press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(taskId).dueDate, QDate(2026, 8, 1), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 0, 5000);
        // The queue is empty, so the sheet closed itself.
        QTRY_VERIFY_WITH_TIMEOUT(!sheet->property("visible").toBool(), 5000);
    }

    // N dismisses the proposal and leaves the task alone.
    void reviewSheetDismissesAProposal() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedAgentOff(st);
            st.setSetting("agent.enabled", true);
            return ids;
        };
        QVERIFY(s.start());
        App &app = *s.app;
        // "Fix the shelf" has no due date, so a dismissal leaves it unset.
        const qint64 taskId = s.ids.shelf;
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = cannedAnswer(taskId, "2026-08-01");
        agent.setTransport(std::ref(transport));
        agent.request("Set the due date");
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 1, 5000);
        QVERIFY(!app.task(taskId).dueDate.isValid());

        QQuickItem *button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("suggestionsButton")) != nullptr, 5000);
        click(button);
        QObject *sheet = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((sheet = findObject("proposalSheet")) != nullptr && sheet->property("visible").toBool(), 5000);
        app.press(Qt::Key_N);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 0, 5000);
        QVERIFY(!app.task(taskId).dueDate.isValid());
    }

    // A proposal written to the store survives a restart and is still listed.
    void proposalsSurviveARestart() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedAgentOff(st);
            st.setSetting("agent.enabled", true);
            return ids;
        };
        QVERIFY(s.start());
        const qint64 taskId = s.ids.buyMilk;
        AgentService &agent = s.app->services().agent;
        Transport transport;
        transport.answer = cannedAnswer(taskId, "2026-08-01");
        agent.setTransport(std::ref(transport));
        agent.request("Set the due date");
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(s.app->backend().proposalCount(), 1, 5000);

        // Restart: the queue is still there.
        QVERIFY(s.restart());
        QCOMPARE(s.app->backend().proposalCount(), 1);
        QQuickItem *button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("suggestionsButton")) != nullptr, 5000);
        click(button);
        QQuickItem *list = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((list = findItem("proposalList")) != nullptr, 5000);
        QCOMPARE(list->property("count").toInt(), 1);
        // And the proposal still applies to the task.
        s.app->press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(s.app->task(taskId).dueDate, QDate(2026, 8, 1), 5000);
    }
};

REGISTER_TEST(E2EAgent)

#include "test_agent.moc"
