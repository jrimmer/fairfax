// End-to-end tests for the proposal engine: the opt-in Settings rows, the
// privacy guard (nothing leaves the machine while the feature is off), and the
// review sheet applying or dismissing a canned model answer. The transport is
// injected, so no test reaches the network; the model's answer is a fixed
// string.

#include "agent.h"
#include "smartkit.h"
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

// The canned answer to a compile: a chat completion whose content is a single
// {"query": "..."} object, as the compile system prompt demands.
QByteArray compiledAnswer(const QString &query) {
    QJsonObject content{{"query", query}};
    QJsonObject message{{"role", "assistant"}, {"content", QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}};
    QJsonObject choice{{"message", message}};
    QJsonObject root{{"choices", QJsonArray{choice}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// A world with one task whose notes mention Sam, and one that does not, so a
// compiled query has something to filter.
Seed seedAgentWithNotes(Store &store) {
    Seed s = seedWorld(store);
    store.begin("Seed notes", false);
    Task milk = store.task(s.buyMilk);
    milk.notes = "Waiting on Sam for the invoice number";
    store.putTask(milk);
    store.commit();
    return s;
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

    // The flagship: ask "tasks with notes mentioning Sam", the model answers
    // with a query, the search field holds it, the results filter, and Save as
    // Smart List creates the list named after the query.
    void askCompilesQueryFiltersAndSaves() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedAgentWithNotes(st);
            st.setSetting("agent.enabled", true);
            return ids;
        };
        QVERIFY(s.start());
        App &app = *s.app;
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = compiledAnswer("sam");
        agent.setTransport(std::ref(transport));

        // The presets menu carries the entry while the feature is on.
        QQuickItem *presets = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((presets = findItem("searchPresets")) != nullptr, 5000);
        click(presets);
        QVERIFY2(clickMenuEntry("Ask in plain English…"),
                 qPrintable(QString("the ask entry is missing from the presets menu: %1").arg(tk::menuEntries())));

        // The sheet: one field and a Go.
        QObject *sheet = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((sheet = findObject("askSheet")) != nullptr && sheet->property("visible").toBool(), 5000);
        QQuickItem *field = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((field = findItem("askField")) != nullptr, 5000);
        click(field);
        QTRY_VERIFY_WITH_TIMEOUT(field->property("activeFocus").toBool(), 5000);
        app.type("tasks with notes mentioning Sam");
        QQuickItem *go = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((go = findItem("askGo")) != nullptr && go->property("enabled").toBool(), 5000);
        click(go);
        QCOMPARE(transport.requests.size(), 1);
        // The request names the question and the schema, and never the raw JSON.
        const QByteArray request = transport.requests.first();
        QVERIFY2(request.contains("tasks with notes mentioning Sam"), request.constData());

        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().compiledQuery(), QString("sam"), 5000);
        // The compiled query lands in the search field as if typed.
        QQuickItem *search = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((search = findItem("searchField")) != nullptr, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(search->property("text").toString(), QString("sam"), 5000);
        QCOMPARE(app.backend().search(), QString("sam"));
        // The results filter: only the task whose notes mention Sam.
        SMART_SHOWS(app, QStringList{"Buy milk"});
        // The ask and its result are recorded as history.
        QCOMPARE(app.backend().proposalCount(), 1);

        // Save as Smart List… hands the compiled query to the builder's own flow.
        QQuickItem *save = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((save = findItem("askSave")) != nullptr, 5000);
        clickSteady(save);
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("sam"), 5000);
        const qint64 id = saveFromBuilder(app, "Waiting on Sam");
        QVERIFY(id);
        QCOMPARE(app.store().list(id).smartQuery, QString("sam"));
        QCOMPARE(app.store().list(id).kind, int(ListKind::Smart));
        QTRY_COMPARE_WITH_TIMEOUT(paneTitle(), QString("Waiting on Sam"), 5000);
        SMART_SHOWS(app, QStringList{"Buy milk"});
    }

    // The shortcut opens the sheet while the feature is on (Ctrl+Shift+E in the
    // App context); it is the keyboard twin of the preset-menu entry.
    void shortcutOpensTheAskSheetWhileOn() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedAgentWithNotes(st);
            st.setSetting("agent.enabled", true);
            return ids;
        };
        QVERIFY(s.start());
        App &app = *s.app;
        QVERIFY(app.backend().agentEnabled());
        app.window->requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(app.window->isActive(), 5000);
        app.press(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
        QObject *sheet = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((sheet = findObject("askSheet")) != nullptr && sheet->property("visible").toBool(), 5000);
        QVERIFY(findItem("askField") != nullptr);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!sheet->property("visible").toBool(), 5000);
    }

    // With agent.enabled off the menu entry is not visible and the shortcut
    // does nothing at all.
    void askEntryAndShortcutDoNothingWhileOff() {
        Session s;
        s.seed = seedAgentOff;
        QVERIFY(s.start());
        App &app = *s.app;
        QVERIFY(!app.backend().agentEnabled());
        QTest::qWait(100);

        // The presets menu has no visible ask entry.
        QQuickItem *presets = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((presets = findItem("searchPresets")) != nullptr, 5000);
        click(presets);
        QTest::qWait(100);
        QVERIFY2(findItem("searchAskEnglish") == nullptr || !findItem("searchAskEnglish")->isVisible(),
                 "the ask entry is visible while the agent is off");
        QVERIFY(findText("Ask in plain English…") == nullptr);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(noMenuOpen(), 5000);

        // The shortcut opens no sheet and changes nothing.
        app.press(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(150);
        QObject *sheet = findObject("askSheet");
        QVERIFY(!sheet || !sheet->property("visible").toBool());
        QVERIFY(app.backend().search().isEmpty());
        QVERIFY(app.backend().compiledQuery().isEmpty());
    }
};

REGISTER_TEST(E2EAgent)

#include "test_agent.moc"
