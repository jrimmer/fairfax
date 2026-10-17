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

// An Inbox holding three undated tasks, for the triage scenario. The ids are
// looked up from the store by title, so no shared global is needed.
qint64 inboxTaskId(Store &store, const QString &title) {
    for (const Task &t : store.tasks())
        if (!t.deleted && t.title == title)
            return t.id;
    return 0;
}

qint64 inboxListId(Store &store) {
    for (const TaskList &l : store.lists())
        if (!l.deleted && l.kind == ListKind::Inbox)
            return l.id;
    return 0;
}

Seed seedAgentInbox(Store &store, bool enabled = true) {
    Seed s = seedWorld(store);
    store.setSetting("agent.enabled", enabled);
    store.begin("Seed inbox", false);
    TaskList inbox;
    inbox.id = store.nextId("lists");
    inbox.name = "Inbox";
    inbox.kind = ListKind::Inbox;
    inbox.color = 5;
    store.putList(inbox);
    for (const QString &title : {QStringLiteral("Call the plumber"), QStringLiteral("Book a table"), QStringLiteral("Read the report")}) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = inbox.id;
        t.title = title;
        t.notes = title == QLatin1String("Call the plumber") ? QStringLiteral("the tap drips") : QString();
        t.order = t.id;
        store.putTask(t);
    }
    store.commit();
    return s;
}

// A canned triage answer, one row per task, built from real ids.
QByteArray triageAnswer(const QList<QPair<qint64, QByteArray>> &rows) {
    QJsonArray triage;
    for (const auto &row : rows)
        triage.append(QJsonDocument::fromJson(row.second).object());
    QJsonObject content{{QStringLiteral("triage"), triage}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}};
    QJsonObject choice{{QStringLiteral("message"), message}};
    QJsonObject root{{QStringLiteral("choices"), QJsonArray{choice}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray triageRow(qint64 taskId, const QString &list, const QString &tag, const QString &due, int priority) {
    QJsonObject row{{QStringLiteral("taskId"), taskId},
                    {QStringLiteral("list"), list},
                    {QStringLiteral("tags"), QJsonArray{tag}},
                    {QStringLiteral("due"), due},
                    {QStringLiteral("priority"), priority}};
    return QJsonDocument(row).toJson(QJsonDocument::Compact);
}

// A canned priority answer for the given tasks.
QByteArray priorityAnswer(const QList<QPair<qint64, int>> &choices) {
    QJsonArray rows;
    for (const auto &c : choices)
        rows.append(QJsonObject{{QStringLiteral("taskId"), c.first}, {QStringLiteral("priority"), c.second}});
    QJsonObject content{{QStringLiteral("priorities"), rows}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}};
    QJsonObject choice{{QStringLiteral("message"), message}};
    QJsonObject root{{QStringLiteral("choices"), QJsonArray{choice}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// A canned breakdown answer: each pair a step title and a duration, a
// negative duration meaning the step carries none.
QByteArray stepsAnswer(const QList<QPair<QString, int>> &steps) {
    QJsonArray rows;
    for (const auto &step : steps) {
        QJsonObject row{{QStringLiteral("title"), step.first}};
        if (step.second >= 0)
            row.insert(QStringLiteral("duration"), step.second);
        rows.append(row);
    }
    QJsonObject content{{QStringLiteral("steps"), rows}};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}};
    QJsonObject choice{{QStringLiteral("message"), message}};
    QJsonObject root{{QStringLiteral("choices"), QJsonArray{choice}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// A project with one existing child and two open sub-tasks, under Home, so a
// breakdown has something not to repeat and somewhere to add.
Seed seedAgentProject(Store &store, bool enabled = true) {
    Seed s = seedWorld(store);
    store.setSetting("agent.enabled", enabled);
    store.begin("Seed project", false);
    Task project;
    project.id = store.nextId("tasks");
    project.listId = s.home;
    project.type = TaskType::Project;
    project.title = "Move house";
    project.notes = "before winter";
    project.order = 1;
    store.putTask(project);
    Task existing;
    existing.id = store.nextId("tasks");
    existing.listId = s.home;
    existing.parentId = project.id;
    existing.title = "Book a van";
    existing.order = 1;
    store.putTask(existing);
    store.commit();
    return s;
}

// Looks a task up by title, for a seed whose project id is not in the Seed.
qint64 taskIdTitled(Store &store, const QString &title) {
    for (const Task &t : store.tasks())
        if (!t.deleted && t.title == title)
            return t.id;
    return 0;
}

// The children of a task in manual order, for checking what an apply added.
QVector<Task> childrenOf(App &app, qint64 parentId) {
    QVector<Task> out;
    for (const Task &t : app.store().tasks())
        if (!t.deleted && t.parentId == parentId)
            out.append(t);
    std::sort(out.begin(), out.end(), [](const Task &a, const Task &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return out;
}

// Opens the review sheet from the toolbar and waits for it. If a collection
// call already opened it (reviewRequested), the modal would swallow a click on
// the toolbar, so an already-open sheet is returned as is.
QObject *openProposalSheet() {
    QObject *sheet = findObject("proposalSheet");
    if (sheet && sheet->property("visible").toBool())
        return sheet;
    QQuickItem *button = nullptr;
    if (!QTest::qWaitFor([&] { return (button = findItem("suggestionsButton")) != nullptr; }, 5000))
        return nullptr;
    click(button);
    QTest::qWaitFor([&] { return (sheet = findObject("proposalSheet")) != nullptr && sheet->property("visible").toBool(); }, 5000);
    return sheet;
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

    // Triage an Inbox of three: the toolbar button sends one request with all
    // three, the sheet lists three, Y applies one (moved, tagged, dated) and A
    // applies the rest.
    void inboxTriageSendsThreeAndAppliesThem() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedAgentInbox(st);
            return ids;
        };
        QVERIFY(s.start());
        App &app = *s.app;
        QVERIFY(app.backend().agentEnabled());
        const qint64 inbox = inboxListId(app.store());
        const qint64 plumber = inboxTaskId(app.store(), "Call the plumber");
        const qint64 table = inboxTaskId(app.store(), "Book a table");
        const qint64 report = inboxTaskId(app.store(), "Read the report");
        QVERIFY(inbox && plumber && table && report);

        // Open the Inbox and confirm the toolbar button appears.
        app.backend().selectSource("list:" + QString::number(inbox));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceIsInbox(), true, 5000);
        QQuickItem *button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("triageInboxButton")) != nullptr, 5000);
        QVERIFY(button->isVisible());

        // The canned answer proposes a list, a tag, an English due date and a
        // priority for each of the three.
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = triageAnswer({
            {plumber, triageRow(plumber, "Work", "garden", "friday", 3)},
            {table, triageRow(table, "Home", "garden", "tomorrow", 1)},
            {report, triageRow(report, "Work", "garden", "next week", 2)},
        });
        agent.setTransport(std::ref(transport));
        click(button);
        QCOMPARE(transport.requests.size(), 1);
        // The request carries all three tasks and the real choices.
        const QByteArray request = transport.requests.first();
        QVERIFY2(request.contains("Call the plumber"), request.constData());
        QVERIFY2(request.contains("Book a table"), request.constData());
        QVERIFY2(request.contains("Read the report"), request.constData());

        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 3, 5000);
        QObject *sheet = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((sheet = findObject("proposalSheet")) != nullptr && sheet->property("visible").toBool(), 5000);
        QQuickItem *list = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((list = findItem("proposalList")) != nullptr, 5000);
        QCOMPARE(list->property("count").toInt(), 3);

        // Y applies the highlighted (first) proposal: the plumber's task moves
        // to Work, is tagged, and gets a parsed due date and priority.
        const qint64 work = listNamed(app, "Work");
        QVERIFY(work);
        app.press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(plumber).listId, work, 5000);
        QCOMPARE(app.task(plumber).priority, 3);
        QVERIFY(!app.task(plumber).tagIds.isEmpty());
        QVERIFY(app.task(plumber).dueDate.isValid());

        // A applies the rest; the sheet closes itself once the queue empties.
        app.press(Qt::Key_A);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 0, 5000);
        QCOMPARE(app.task(table).listId, listNamed(app, "Home"));
        QCOMPARE(app.task(report).listId, work);
        QVERIFY(app.task(report).dueDate.isValid());
        QTRY_VERIFY_WITH_TIMEOUT(!sheet->property("visible").toBool(), 5000);
    }

    // Dismissing a triage proposal leaves every task untouched.
    void inboxTriageDismissChangesNothing() {
        Session s;
        s.seed = [](Store &st) { return seedAgentInbox(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        const qint64 inbox = inboxListId(app.store());
        const qint64 plumber = inboxTaskId(app.store(), "Call the plumber");
        app.backend().selectSource("list:" + QString::number(inbox));
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = triageAnswer({{plumber, triageRow(plumber, "Work", "garden", "friday", 3)}});
        agent.setTransport(std::ref(transport));
        app.backend().triageInbox();
        QCOMPARE(transport.requests.size(), 1);
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 1, 5000);

        QObject *sheet = openProposalSheet();
        QVERIFY(sheet);
        QTRY_VERIFY_WITH_TIMEOUT(sheet->property("visible").toBool(), 5000);
        // The sheet has the keyboard; N dismisses the highlighted proposal.
        app.press(Qt::Key_N);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 0, 5000);
        // The task did not move, gain a tag, a date or a priority.
        QCOMPARE(app.task(plumber).listId, inbox);
        QVERIFY(app.task(plumber).tagIds.isEmpty());
        QVERIFY(!app.task(plumber).dueDate.isValid());
        QCOMPARE(app.task(plumber).priority, 0);
    }

    // Triage can also be started from the Inbox focus list's own context menu.
    void inboxTriageFromTheContextMenu() {
        Session s;
        s.seed = [](Store &st) { return seedAgentInbox(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        const qint64 inbox = inboxListId(app.store());
        app.backend().selectSource("list:" + QString::number(inbox));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().sourceIsInbox(), 5000);
        // Right-click the Inbox sidebar row: the menu carries Triage Inbox.
        QQuickItem *row = app.sidebarRow("list:" + QString::number(inbox));
        QVERIFY(row);
        click(row, Qt::RightButton);
        QTRY_VERIFY_WITH_TIMEOUT(findText("Triage Inbox", "QQuickMenuItem") != nullptr, 5000);
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = QByteArray("{\"triage\":[]}");
        agent.setTransport(std::ref(transport));
        clickMenuEntry("Triage Inbox");
        QCOMPARE(transport.requests.size(), 1);
    }

    // Suggest Priorities on a multi-task selection: the context menu entry,
    // the canned answer, and A applying each setPriority.
    void suggestPrioritiesOnASelection() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedAgentOff(st);
            st.setSetting("agent.enabled", true);
            return ids;
        };
        QVERIFY(s.start());
        App &app = *s.app;
        // Open Home so all three tasks are on screen; select two with Ctrl.
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(selectRow(app, app.ids.buyMilk));
        clickWith(app.row(app.ids.plants), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);

        // The context menu shows Suggest Priorities only on a multi-selection.
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        QQuickItem *entry = steadyMenuItem("Suggest Priorities");
        QVERIFY2(entry, qPrintable(QString("Suggest Priorities missing: %1").arg(tk::menuEntries())));
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = priorityAnswer({{app.ids.buyMilk, 3}, {app.ids.plants, 1}});
        agent.setTransport(std::ref(transport));
        clickMenu("Suggest Priorities");
        QCOMPARE(transport.requests.size(), 1);
        QVERIFY2(transport.requests.first().contains("Buy milk"), transport.requests.first().constData());
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 2, 5000);

        QObject *sheet = openProposalSheet();
        QVERIFY(sheet);
        QTRY_VERIFY_WITH_TIMEOUT(sheet->property("visible").toBool(), 5000);
        app.press(Qt::Key_A);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 0, 5000);
        QCOMPARE(app.task(app.ids.buyMilk).priority, 3);
        QCOMPARE(app.task(app.ids.plants).priority, 1);
    }

    // With the feature off neither triage entry nor Suggest Priorities appears,
    // and neither call sends anything.
    void collectionCallsAbsentWhileOff() {
        Session s;
        s.seed = [](Store &st) { return seedAgentInbox(st, false); };
        QVERIFY(s.start());
        App &app = *s.app;
        QVERIFY(!app.backend().agentEnabled());
        const qint64 inbox = inboxListId(app.store());
        app.backend().selectSource("list:" + QString::number(inbox));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().sourceIsInbox(), 5000);
        QTest::qWait(100);

        // No toolbar triage button, and no suggestions button either.
        QVERIFY(findItem("triageInboxButton") == nullptr);
        QVERIFY(findItem("suggestionsButton") == nullptr);

        // The Inbox context menu has no Triage Inbox.
        QQuickItem *row = app.sidebarRow("list:" + QString::number(inbox));
        QVERIFY(row);
        click(row, Qt::RightButton);
        QTest::qWait(100);
        QVERIFY(findItem("triageInboxMenu") == nullptr || !findItem("triageInboxMenu")->isVisible());
        QVERIFY(findText("Triage Inbox") == nullptr);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(noMenuOpen(), 5000);

        // A multi-selection's context menu has no Suggest Priorities.
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(selectRow(app, app.ids.buyMilk));
        clickWith(app.row(app.ids.plants), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        QVERIFY(contextMenuOn(app, app.ids.buyMilk));
        QTest::qWait(100);
        QVERIFY(findItem("suggestPriorities") == nullptr || !findItem("suggestPriorities")->isVisible());
        QVERIFY(findText("Suggest Priorities") == nullptr);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(noMenuOpen(), 5000);

        // Both calls refuse even when invoked directly.
        AgentService &agent = app.services().agent;
        Transport transport;
        agent.setTransport(std::ref(transport));
        app.backend().triageInbox();
        app.backend().suggestPriorities();
        QCOMPARE(transport.requests.size(), 0);
        QCOMPARE(app.backend().proposalCount(), 0);
    }

    // Breaking a project down: the editor and context menu entries exist, the
    // canned three-step answer lists three, Y adds one child, A adds the rest
    // in order, and each carries its proposed duration.
    void projectBreakdownReviewsAndAppliesSteps() {
        Session s;
        s.seed = [](Store &st) { return seedAgentProject(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        const qint64 project = taskIdTitled(app.store(), "Move house");
        QVERIFY(project);

        // Open the project so its tasks are on screen, then the editor's
        // Suggest Steps entry sends one request describing the task.
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(openEditor(app, project));
        QQuickItem *stepsButton = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((stepsButton = findItem("editorSuggestSteps")) != nullptr, 5000);
        QVERIFY(stepsButton->isVisible());
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = stepsAnswer({{"Sort the boxes", 30}, {"Change the address", -1}, {"Return the keys", 15}});
        agent.setTransport(std::ref(transport));
        click(stepsButton);
        QCOMPARE(transport.requests.size(), 1);
        // The request names the title, notes, the existing child and the schema.
        const QByteArray request = transport.requests.first();
        QVERIFY2(request.contains("Move house"), request.constData());
        QVERIFY2(request.contains("before winter"), request.constData());
        QVERIFY2(request.contains("Book a van"), request.constData());
        QVERIFY2(request.contains("steps"), request.constData());

        // Closing the editor leaves the sheet the keyboard.
        QVERIFY(closeEditor(app));
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 3, 5000);
        QObject *sheet = openProposalSheet();
        QVERIFY(sheet);
        QTRY_VERIFY_WITH_TIMEOUT(sheet->property("visible").toBool(), 5000);
        QQuickItem *list = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((list = findItem("proposalList")) != nullptr, 5000);
        QCOMPARE(list->property("count").toInt(), 3);

        // Y adds the first step as a real child of the project.
        app.press(Qt::Key_Y);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 2, 5000);
        QVector<Task> children = childrenOf(app, project);
        QTRY_COMPARE_WITH_TIMEOUT(children.size(), 2, 5000);
        QCOMPARE(children.at(0).title, QString("Book a van"));
        QCOMPARE(children.at(1).title, QString("Sort the boxes"));
        QCOMPARE(children.at(1).duration, 30);

        // A adds the rest, in the order they were proposed; the no-duration
        // step is allowed and simply has none.
        app.press(Qt::Key_A);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(childrenOf(app, project).size(), 4, 5000);
        children = childrenOf(app, project);
        QCOMPARE(children.at(2).title, QString("Change the address"));
        QCOMPARE(children.at(2).duration, 0);
        QCOMPARE(children.at(3).title, QString("Return the keys"));
        QCOMPARE(children.at(3).duration, 15);
        QTRY_VERIFY_WITH_TIMEOUT(!sheet->property("visible").toBool(), 5000);
    }

    // N skips a step and leaves the project exactly as it was.
    void projectBreakdownDismissChangesNothing() {
        Session s;
        s.seed = [](Store &st) { return seedAgentProject(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        const qint64 project = taskIdTitled(app.store(), "Move house");
        QVERIFY(project);
        QCOMPARE(childrenOf(app, project).size(), 1);

        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = stepsAnswer({{"Sort the boxes", 30}, {"Change the address", -1}});
        agent.setTransport(std::ref(transport));
        app.backend().suggestSteps(project);
        QCOMPARE(transport.requests.size(), 1);
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 2, 5000);

        QObject *sheet = openProposalSheet();
        QVERIFY(sheet);
        QTRY_VERIFY_WITH_TIMEOUT(sheet->property("visible").toBool(), 5000);
        app.press(Qt::Key_N);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 1, 5000);
        app.press(Qt::Key_N);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 0, 5000);
        // The project has only the child it started with.
        QCOMPARE(childrenOf(app, project).size(), 1);
        QCOMPARE(childrenOf(app, project).first().title, QString("Book a van"));
    }

    // The context menu offers Suggest Steps on any task, and it works on the
    // task that was right-clicked even when several are selected.
    void projectBreakdownFromTheContextMenu() {
        Session s;
        s.seed = [](Store &st) { return seedAgentProject(st); };
        QVERIFY(s.start());
        App &app = *s.app;
        const qint64 project = taskIdTitled(app.store(), "Move house");
        QVERIFY(project);
        QVERIFY(app.openSource(lk(app.ids.home)));
        // Select the project plus another task, so the batch temptation is there.
        QVERIFY(selectRow(app, project));
        clickWith(app.row(app.ids.buyMilk), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);

        QVERIFY(contextMenuOn(app, project));
        QQuickItem *entry = steadyMenuItem("Suggest Steps…");
        QVERIFY2(entry, qPrintable(QString("Suggest Steps missing: %1").arg(tk::menuEntries())));
        AgentService &agent = app.services().agent;
        Transport transport;
        transport.answer = stepsAnswer({{"Sort the boxes", 30}});
        agent.setTransport(std::ref(transport));
        clickMenu("Suggest Steps…");
        QCOMPARE(transport.requests.size(), 1);
        // It asked about the right-clicked project, not the whole selection.
        QVERIFY2(transport.requests.first().contains("Move house"), transport.requests.first().constData());
        QVERIFY2(!transport.requests.first().contains("Buy milk"), transport.requests.first().constData());
        transport.deliver();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 1, 5000);
    }

    // With the feature off neither entry exists and a direct call sends nothing.
    void projectBreakdownHiddenWhileOff() {
        Session s;
        s.seed = [](Store &st) { return seedAgentProject(st, false); };
        QVERIFY(s.start());
        App &app = *s.app;
        QVERIFY(!app.backend().agentEnabled());
        const qint64 project = taskIdTitled(app.store(), "Move house");
        QVERIFY(project);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QTest::qWait(100);

        // No Suggest Steps entry in the editor.
        QVERIFY(openEditor(app, project));
        QTest::qWait(100);
        QVERIFY(findItem("editorSuggestSteps") == nullptr);
        QVERIFY(closeEditor(app));

        // No Suggest Steps in the task context menu.
        QVERIFY(contextMenuOn(app, project));
        QTest::qWait(100);
        QVERIFY(findItem("suggestSteps") == nullptr || !findItem("suggestSteps")->isVisible());
        QVERIFY(findText("Suggest Steps…") == nullptr);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(noMenuOpen(), 5000);

        // A direct call refuses.
        AgentService &agent = app.services().agent;
        Transport transport;
        agent.setTransport(std::ref(transport));
        app.backend().suggestSteps(project);
        QCOMPARE(transport.requests.size(), 0);
        QCOMPARE(app.backend().proposalCount(), 0);
    }
};

REGISTER_TEST(E2EAgent)

#include "test_agent.moc"
