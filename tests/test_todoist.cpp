#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <memory>

#include "store.h"
#include "testregistry.h"
#include "todoist.h"

using namespace TodoistMapping;

namespace {

// A stand-in for Todoist: records every request, answers commands with "ok"
// and temp id mappings, and hands out canned resources once.
struct FakeServer {
    QList<QUrlQuery> forms;
    QJsonObject canned;   // Resources merged into the next response.
    int failStatus = 0;   // When set, answers with this HTTP status.
    int failTimes = 0;    // ...this many times, then behaves.
    int counter = 100;
    QJsonObject rejectTypes;  // type -> true: answers those commands with an error.

    QJsonObject operator()(const QUrlQuery &form) {
        forms << form;
        if (failStatus && failTimes != 0) {
            if (failTimes > 0)
                --failTimes;
            return {{"http_status", failStatus}, {"retry_after", 0}};
        }
        QJsonObject r;
        QJsonObject status, mapping;
        const auto cmds = QJsonDocument::fromJson(form.queryItemValue("commands").toUtf8()).array();
        for (const auto &cv : cmds) {
            const QJsonObject c = cv.toObject();
            if (rejectTypes.value(c.value("type").toString()).toBool())
                status.insert(c.value("uuid").toString(), QJsonObject{{"error", "nope"}, {"http_code", 400}});
            else
                status.insert(c.value("uuid").toString(), "ok");
            if (c.contains("temp_id"))
                mapping.insert(c.value("temp_id").toString(), QString("R%1").arg(++counter));
        }
        if (!cmds.isEmpty()) {
            r.insert("sync_status", status);
            r.insert("temp_id_mapping", mapping);
        }
        for (auto it = canned.begin(); it != canned.end(); ++it)
            r.insert(it.key(), it.value());
        canned = {};
        r.insert("sync_token", QString("T%1").arg(forms.size()));
        r.insert("full_sync", form.queryItemValue("sync_token") == "*");
        return r;
    }

    QJsonArray commandsOf(int request) const {
        return QJsonDocument::fromJson(forms.value(request).queryItemValue("commands").toUtf8()).array();
    }
    QJsonArray allCommands() const {
        QJsonArray all;
        for (int i = 0; i < forms.size(); ++i)
            for (const auto &c : commandsOf(i))
                all.append(c);
        return all;
    }
    QStringList types() const {
        QStringList t;
        for (const auto &c : allCommands())
            t << c.toObject().value("type").toString();
        return t;
    }
    QJsonObject first(const QString &type) const {
        for (const auto &c : allCommands())
            if (c.toObject().value("type").toString() == type)
                return c.toObject();
        return {};
    }
};

QJsonObject project(const QString &id, const QString &name, bool inbox = false) {
    QJsonObject p{{"id", id}, {"name", name}, {"is_deleted", false}, {"child_order", 1}};
    if (inbox)
        p.insert("is_inbox_project", true);
    return p;
}

QJsonObject item(const QString &id, const QString &content, const QString &projectId, QJsonObject extra = {}) {
    QJsonObject o{{"id", id}, {"content", content}, {"description", ""}, {"project_id", projectId}, {"priority", 1},
                  {"checked", false}, {"is_deleted", false}, {"labels", QJsonArray()}, {"parent_id", QJsonValue::Null},
                  {"due", QJsonValue::Null}, {"duration", QJsonValue::Null}, {"child_order", 1},
                  {"updated_at", "2025-01-01T10:00:00Z"}, {"added_at", "2025-01-01T09:00:00Z"}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        o.insert(it.key(), it.value());
    return o;
}

}  // namespace

class TodoistTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_n = 0;
    std::unique_ptr<Store> m_store;
    std::unique_ptr<TodoistSync> m_sync;
    FakeServer m_server;
    qint64 m_inbox = 0;

    void setup() {
        m_server = FakeServer();
        m_store = std::make_unique<Store>();
        QString err;
        QVERIFY2(m_store->open(m_dir.filePath(QString("t%1.db").arg(++m_n)), &err), qPrintable(err));
        TaskList inbox;
        inbox.id = m_store->nextId("lists");
        inbox.kind = ListKind::Inbox;
        inbox.name = "Inbox";
        m_store->begin("inbox");
        m_store->putList(inbox);
        m_store->commit();
        m_inbox = inbox.id;
        m_sync = std::make_unique<TodoistSync>(m_store.get());
        m_sync->setTransport([this](const QUrlQuery &f) { return m_server(f); });
    }

    void runSync() {
        m_sync->syncNow();
        QTRY_VERIFY_WITH_TIMEOUT(!m_sync->busy(), 5000);
    }

    Task byTitle(const QString &title) const {
        for (const auto &t : m_store->tasks())
            if (t.title == title && !t.deleted)
                return t;
        return {};
    }
    TaskList listNamed(const QString &name) const {
        for (const auto &l : m_store->lists())
            if (l.name == name && !l.deleted)
                return l;
        return {};
    }

    qint64 addTask(Task t) {
        t.id = m_store->nextId("tasks");
        m_store->begin("add");
        m_store->putTask(t);
        m_store->commit();
        return t.id;
    }
    void edit(qint64 id, const std::function<void(Task &)> &fn) {
        Task t = m_store->task(id);
        fn(t);
        m_store->begin("edit");
        m_store->putTask(t);
        m_store->commit();
    }

    // Gets a one-task account into the synced state.
    void syncedBase() {
        setup();
        m_server.canned = {{"projects", QJsonArray{project("P0", "Inbox", true)}},
                           {"items", QJsonArray{item("I1", "Alpha", "P0")}}};
        runSync();
        QVERIFY(!byTitle("Alpha").uid.isEmpty());
        m_server.forms.clear();
    }

private slots:
    void firstSyncImports() {
        setup();
        const QString meta = R"(<Precrime Meta>{"uid":"UID-1","start_date":"2025-03-01","start_time":540,"starred":true,"type":2,"action_type":1,"action_value":"555-1234"}</Precrime Meta>)";
        m_server.canned = {
            {"projects", QJsonArray{project("P0", "Inbox", true), project("P1", "Work")}},
            {"labels", QJsonArray{QJsonObject{{"id", "L1"}, {"name", "home"}, {"is_deleted", false}}}},
            {"items",
             QJsonArray{
                 item("I1", "Parent", "P1",
                      {{"priority", 4}, {"description", "Some notes\n\n" + meta}, {"labels", QJsonArray{"home"}},
                       {"due", QJsonObject{{"date", "2025-03-05T14:30:00"}, {"is_recurring", false}, {"string", "Mar 5 2:30pm"}}},
                       {"duration", QJsonObject{{"amount", 45}, {"unit", "minute"}}}}),
                 item("I2", "Child", "P1", {{"parent_id", "I1"}, {"priority", 3}}),
                 item("I3", "Every3", "P0",
                      {{"priority", 2},
                       {"due", QJsonObject{{"date", "2025-03-07"}, {"is_recurring", true}, {"string", "every! 3 days"}}}}),
                 item("I4", "Mondays", "P0",
                      {{"due", QJsonObject{{"date", "2025-03-10"}, {"is_recurring", true}, {"string", "every monday"}}}}),
                 item("I5", "Done", "P0", {{"checked", true}, {"completed_at", "2025-02-01T08:00:00.123456Z"}}),
             }}};
        runSync();
        QVERIFY2(m_sync->status().startsWith("Synced"), qPrintable(m_sync->status()));
        QCOMPARE(m_server.forms.size(), 1);  // Nothing to push afterwards.
        QCOMPARE(m_server.forms[0].queryItemValue("sync_token"), QString("*"));
        QCOMPARE(m_store->setting("todoist.syncToken").toString(), QString("T1"));

        const TaskList work = listNamed("Work");
        QVERIFY(work.id);
        QCOMPARE(m_store->lists().size(), 2);  // Inbox adopted, not duplicated.
        QCOMPARE(m_store->syncLinks("todoist", "lists").size(), 2);

        const Task p = byTitle("Parent");
        QCOMPARE(p.uid, QString("UID-1"));
        QCOMPARE(p.listId, work.id);
        QCOMPARE(p.priority, int(Priority::High));
        QCOMPARE(p.notes, QString("Some notes"));
        QCOMPARE(p.dueDate, QDate(2025, 3, 5));
        QCOMPARE(p.dueTime, 14 * 60 + 30);
        QCOMPARE(p.duration, 45);
        QCOMPARE(p.startDate, QDate(2025, 3, 1));
        QCOMPARE(p.startTime, 540);
        QVERIFY(p.starred);
        QCOMPARE(p.type, int(TaskType::Checklist));
        QCOMPARE(p.actionType, int(ActionType::Call));
        QCOMPARE(p.actionValue, QString("555-1234"));
        QCOMPARE(p.tagIds.size(), 1);
        QCOMPARE(m_store->tag(p.tagIds[0]).name, QString("home"));
        QCOMPARE(p.modified, QDateTime::fromString("2025-01-01T10:00:00Z", Qt::ISODate).toLocalTime());

        const Task c = byTitle("Child");
        QCOMPARE(c.parentId, p.id);
        QCOMPARE(c.priority, int(Priority::Medium));

        const Task e = byTitle("Every3");
        QCOMPARE(e.priority, int(Priority::Low));
        QCOMPARE(e.recurrence.frequency, int(Repeat::Daily));
        QCOMPARE(e.recurrence.interval, 3);
        QCOMPARE(e.recurrence.from, int(Repeat::FromCompletion));
        QCOMPARE(e.listId, m_inbox);

        const Task m = byTitle("Mondays");
        QCOMPARE(m.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(m.recurrence.weekdays, 1);
        QCOMPARE(m.priority, int(Priority::None));

        const Task d = byTitle("Done");
        QVERIFY(d.completed);
        QVERIFY(d.completedAt.isValid());

        QVERIFY(buildCommands(*m_store).isEmpty());
        // A second sync with no news sends nothing.
        runSync();
        QVERIFY(m_server.commandsOf(m_server.forms.size() - 1).isEmpty());
        QCOMPARE(m_server.forms.last().queryItemValue("sync_token"), QString("T1"));
    }

    void localCreateEditCompleteDelete() {
        syncedBase();
        TaskList work;
        work.id = m_store->nextId("lists");
        work.name = "Errands";
        m_store->begin("l");
        m_store->putList(work);
        m_store->commit();
        Tag tag;
        tag.id = m_store->nextId("tags");
        tag.name = "urgent";
        m_store->begin("t");
        m_store->putTag(tag);
        m_store->commit();

        Task parent;
        parent.title = "Shop";
        parent.listId = work.id;
        parent.priority = Priority::High;
        parent.dueDate = QDate(2030, 5, 1);
        parent.dueTime = 9 * 60;
        parent.duration = 30;
        parent.notes = "milk";
        parent.starred = true;
        parent.tagIds = {tag.id};
        const qint64 pid = addTask(parent);
        Task child;
        child.title = "Milk";
        child.listId = work.id;
        child.parentId = pid;
        child.dueDate = QDate(2030, 5, 2);
        const qint64 cid = addTask(child);

        runSync();
        QVERIFY2(m_sync->status().startsWith("Synced"), qPrintable(m_sync->status()));
        const QStringList types = m_server.types();
        QVERIFY(types.indexOf("project_add") < types.indexOf("item_add"));
        QVERIFY(types.indexOf("label_add") < types.indexOf("item_add"));
        QCOMPARE(types.count("item_add"), 2);
        const QJsonArray cmds = m_server.commandsOf(1);  // Request 0 was the pull.
        QVERIFY(!cmds.isEmpty());
        QVERIFY(!cmds[0].toObject().contains("_local"));
        const QJsonObject pa = [&] {
            for (const auto &c : cmds)
                if (c.toObject().value("args").toObject().value("content") == "Shop")
                    return c.toObject();
            return QJsonObject();
        }();
        const QJsonObject args = pa.value("args").toObject();
        QCOMPARE(pa.value("temp_id").toString(), m_store->task(pid).uid);
        QCOMPARE(args.value("priority").toInt(), 4);
        QCOMPARE(args.value("project_id").toString(), m_server.first("project_add").value("temp_id").toString());
        QCOMPARE(args.value("labels").toArray().at(0).toString(), QString("urgent"));
        QCOMPARE(args.value("due").toObject().value("date").toString(), QString("2030-05-01T09:00:00"));
        QCOMPARE(args.value("duration").toObject().value("amount").toInt(), 30);
        QCOMPARE(args.value("duration").toObject().value("unit").toString(), QString("minute"));
        QVERIFY(args.value("description").toString().startsWith("milk\n\n<Precrime Meta>"));
        QVERIFY(args.value("description").toString().contains(R"("starred":true)"));
        QVERIFY(args.value("description").toString().contains(m_store->task(pid).uid));
        // The child names its parent by temp id, and comes after it.
        QJsonObject ca;
        int pi = -1, ci = -1;
        for (int i = 0; i < cmds.size(); ++i) {
            const auto o = cmds[i].toObject();
            if (o.value("temp_id").toString() == m_store->task(pid).uid) pi = i;
            if (o.value("temp_id").toString() == m_store->task(cid).uid) { ci = i; ca = o; }
        }
        QVERIFY(pi >= 0 && ci > pi);
        QCOMPARE(ca.value("args").toObject().value("parent_id").toString(), m_store->task(pid).uid);

        const auto links = m_store->syncLinks("todoist", "tasks");
        QVERIFY(links.contains(pid) && links.contains(cid));
        QVERIFY(links[pid].remoteId.startsWith("R"));
        QVERIFY(m_store->syncLinks("todoist", "lists").contains(work.id));
        QVERIFY(m_store->syncLinks("todoist", "tags").contains(tag.id));
        QVERIFY(buildCommands(*m_store).isEmpty());

        // Edit.
        m_server.forms.clear();
        edit(pid, [](Task &t) { t.title = "Shop more"; t.priority = Priority::Low; });
        runSync();
        QCOMPARE(m_server.types(), QStringList({"item_update"}));
        const QJsonObject up = m_server.first("item_update").value("args").toObject();
        QCOMPARE(up.value("id").toString(), links[pid].remoteId);
        QCOMPARE(up.value("content").toString(), QString("Shop more"));
        QCOMPARE(up.value("priority").toInt(), 2);

        // Complete.
        m_server.forms.clear();
        edit(cid, [](Task &t) { t.completed = true; t.completedAt = QDateTime::currentDateTime(); });
        runSync();
        QCOMPARE(m_server.types(), QStringList({"item_close"}));
        QCOMPARE(m_server.first("item_close").value("args").toObject().value("id").toString(), links[cid].remoteId);

        // Uncomplete.
        m_server.forms.clear();
        edit(cid, [](Task &t) { t.completed = false; t.completedAt = QDateTime(); });
        runSync();
        QCOMPARE(m_server.types(), QStringList({"item_uncomplete"}));

        // Delete.
        m_server.forms.clear();
        edit(cid, [](Task &t) { t.deleted = true; });
        runSync();
        QCOMPARE(m_server.types(), QStringList({"item_delete"}));
        QVERIFY(!m_store->syncLinks("todoist", "tasks").contains(cid));

        // Delete a list and a tag.
        m_server.forms.clear();
        TaskList l = m_store->list(work.id);
        l.deleted = true;
        Tag tg = m_store->tag(tag.id);
        tg.deleted = true;
        m_store->begin("d");
        m_store->putList(l);
        m_store->putTag(tg);
        m_store->commit();
        runSync();
        QVERIFY(m_server.types().contains("project_delete"));
        QVERIFY(m_server.types().contains("label_delete"));
        QVERIFY(!m_store->syncLinks("todoist", "lists").contains(work.id));
    }

    void completedSeriesCopyIsNotPushed() {
        syncedBase();
        Task copy;
        copy.title = "Water plants";
        copy.completed = true;
        copy.seriesUid = "SERIES";
        copy.listId = m_inbox;
        addTask(copy);
        QVERIFY(buildCommands(*m_store).isEmpty());
        runSync();
        QVERIFY(m_server.types().isEmpty());
    }

    void repeatingCompletionIsItemClose() {
        syncedBase();
        const Task a = byTitle("Alpha");
        edit(a.id, [](Task &t) {
            t.dueDate = QDate(2025, 3, 3);
            t.recurrence.frequency = Repeat::Daily;
        });
        runSync();  // Pushes the due date + recurrence.
        const QJsonObject up = m_server.first("item_update").value("args").toObject();
        QCOMPARE(up.value("due").toObject().value("string").toString().left(11), QString("every day s"));  // ... starting <date>
        m_server.forms.clear();

        // Completing it locally advances it and leaves a completed copy.
        edit(a.id, [](Task &t) {
            t.dueDate = QDate(2025, 3, 4);
            t.recurrence.occurrencesDone = 1;
        });
        Task copy;
        copy.title = "Alpha";
        copy.completed = true;
        copy.seriesUid = "S";
        copy.listId = m_inbox;
        addTask(copy);
        runSync();
        QCOMPARE(m_server.types().count("item_close"), 1);
        QCOMPARE(m_server.types().count("item_add"), 0);
        QCOMPARE(m_server.first("item_close").value("args").toObject().value("id").toString(), QString("I1"));
        QVERIFY(buildCommands(*m_store).isEmpty());
    }

    void recurrenceStrings() {
        Recurrence r;
        QCOMPARE(recurrenceString(r), QString());
        r.frequency = Repeat::Weekly;
        r.weekdays = 1;
        QCOMPARE(recurrenceString(r), QString("every mon"));
        r.weekdays = 1 | 4;
        QCOMPARE(recurrenceString(r), QString("every mon, wed"));
        r.weekdays = 0;
        r.interval = 2;
        QCOMPARE(recurrenceString(r), QString("every 2 weeks"));
        r.frequency = Repeat::Daily;
        r.interval = 3;
        r.from = Repeat::FromCompletion;
        QCOMPARE(recurrenceString(r), QString("every! 3 days"));
        r = Recurrence();
        r.frequency = Repeat::Monthly;
        r.monthOrdinal = -1;
        r.monthWeekday = 5;
        QCOMPARE(recurrenceString(r), QString("every last fri"));
        r = Recurrence();
        r.frequency = Repeat::Yearly;
        QCOMPARE(recurrenceString(r), QString("every year"));
        r.frequency = Repeat::Weekdays;
        QCOMPARE(recurrenceString(r), QString("every weekday"));
    }

    void recurrenceStringsParseBack() {
        // Every rule we can write, Todoist's own text reads back the same through the parser.
        const struct { QString text; int freq, interval, from; } cases[] = {
            {"every day", Repeat::Daily, 1, 0}, {"every 3 days", Repeat::Daily, 3, 0},
            {"every! 2 weeks", Repeat::Weekly, 2, 1}, {"every weekday", Repeat::Weekdays, 1, 0},
            {"every month", Repeat::Monthly, 1, 0}, {"every 2 months", Repeat::Monthly, 2, 0},
            {"every year", Repeat::Yearly, 1, 0}, {"every mon, wed", Repeat::Weekly, 1, 0},
            {"every last fri", Repeat::Monthly, 1, 0}, {"every 2nd tue", Repeat::Monthly, 1, 0},
        };
        for (const auto &c : cases) {
            setup();
            m_server.canned = {{"items", QJsonArray{item("X", "R", "P0",
                {{"due", QJsonObject{{"date", "2030-03-07"}, {"is_recurring", true}, {"string", c.text + " at 15:00 starting 2030-03-07"}}}})}}};
            runSync();
            const Task t = byTitle("R");
            QVERIFY2(t.recurrence.frequency == c.freq && t.recurrence.interval == c.interval && t.recurrence.from == c.from,
                     qPrintable(c.text));
            QCOMPARE(recurrenceString(t.recurrence), c.text);
        }
    }

    void recurrenceSurvivesRoundTrip() {
        syncedBase();
        const Task a = byTitle("Alpha");
        edit(a.id, [](Task &t) {
            t.dueDate = QDate(2030, 1, 7);
            t.recurrence.frequency = Repeat::Weekly;
            t.recurrence.weekdays = 1 | 4;
            t.recurrence.endType = Repeat::AfterCount;
            t.recurrence.endCount = 5;
        });
        runSync();
        const QString desc = m_server.first("item_update").value("args").toObject().value("description").toString();
        // Another machine pulling it gets everything back, including what Todoist can't hold.
        QJsonObject remote = item("I1", "Alpha", "P0",
                                  {{"description", desc}, {"updated_at", "2099-01-01T00:00:00Z"},
                                   {"due", QJsonObject{{"date", "2030-01-09"}, {"is_recurring", true}, {"string", "every mon, wed starting 2030-01-07"}}}});
        m_server.canned = {{"items", QJsonArray{remote}}};
        runSync();
        const Task b = byTitle("Alpha");
        QCOMPARE(b.dueDate, QDate(2030, 1, 9));
        QCOMPARE(b.recurrence.weekdays, 5);
        QCOMPARE(b.recurrence.endType, int(Repeat::AfterCount));
        QCOMPARE(b.recurrence.endCount, 5);
    }

    void conflictLocalNewerWins() {
        syncedBase();
        const Task a = byTitle("Alpha");
        edit(a.id, [](Task &t) { t.title = "Local title"; });  // modified = now
        m_server.canned = {{"items", QJsonArray{item("I1", "Remote old", "P0", {{"updated_at", "2025-06-01T00:00:00Z"}})}}};
        runSync();
        QCOMPARE(m_store->task(a.id).title, QString("Local title"));
        QCOMPARE(m_server.first("item_update").value("args").toObject().value("content").toString(), QString("Local title"));
    }

    void conflictRemoteNewerWins() {
        syncedBase();
        const Task a = byTitle("Alpha");
        edit(a.id, [](Task &t) { t.title = "Local title"; });
        m_server.canned = {{"items", QJsonArray{item("I1", "Remote new", "P0", {{"updated_at", "2099-06-01T00:00:00Z"}, {"priority", 4}})}}};
        runSync();
        QCOMPARE(m_store->task(a.id).title, QString("Remote new"));
        QCOMPARE(m_store->task(a.id).priority, int(Priority::High));
        QVERIFY(m_server.types().isEmpty());
        QCOMPARE(m_store->task(a.id).modified, QDateTime::fromString("2099-06-01T00:00:00Z", Qt::ISODate).toLocalTime());
    }

    void remoteChangeAndDelete() {
        syncedBase();
        const Task a = byTitle("Alpha");
        m_server.canned = {{"items", QJsonArray{item("I1", "Alpha", "P0", {{"checked", true}, {"updated_at", "2025-07-01T00:00:00Z"}})}}};
        runSync();
        QVERIFY(m_store->task(a.id).completed);
        m_server.canned = {{"items", QJsonArray{item("I1", "Alpha", "P0", {{"is_deleted", true}})}}};
        runSync();
        QVERIFY(m_store->task(a.id).deleted);
        QVERIFY(m_store->syncLinks("todoist", "tasks").isEmpty());
        QVERIFY(m_server.types().isEmpty());
    }

    void duplicatesAvoidedByMetaUid() {
        setup();
        TaskList work;
        work.id = m_store->nextId("lists");
        work.name = "Work";
        m_store->begin("l");
        m_store->putList(work);
        m_store->commit();
        Tag tag;
        tag.id = m_store->nextId("tags");
        tag.name = "Home";
        m_store->begin("t");
        m_store->putTag(tag);
        m_store->commit();
        Task t;
        t.title = "Already there";
        t.listId = work.id;
        t.uid = "SHARED-UID";
        t.modified = QDateTime::fromString("2020-01-01T00:00:00Z", Qt::ISODate);
        const qint64 id = addTask(t);
        edit(id, [](Task &x) { x.title = "Already there"; });  // keeps modified = now; fine either way

        m_server.canned = {
            {"projects", QJsonArray{project("P0", "Inbox", true), project("P1", "Work")}},
            {"labels", QJsonArray{QJsonObject{{"id", "L1"}, {"name", "home"}, {"is_deleted", false}}}},
            {"items", QJsonArray{item("I1", "Already there", "P1",
                                      {{"description", R"(<Precrime Meta>{"uid":"SHARED-UID"}</Precrime Meta>)"}})}}};
        runSync();
        QVERIFY2(m_sync->status().startsWith("Synced"), qPrintable(m_sync->status()));
        QVERIFY(!m_server.types().contains("item_add"));
        QVERIFY(!m_server.types().contains("project_add"));
        QVERIFY(!m_server.types().contains("label_add"));
        int live = 0;
        for (const auto &x : m_store->tasks())
            live += !x.deleted;
        QCOMPARE(live, 1);
        QCOMPARE(m_store->lists().size(), 2);
        QCOMPARE(m_store->tags().size(), 1);
        QCOMPARE(m_store->syncLinks("todoist", "tasks").value(id).remoteId, QString("I1"));
        QCOMPARE(m_store->task(id).uid, QString("SHARED-UID"));
    }

    void batchesOfAtMost100() {
        setup();
        m_server.canned = {{"projects", QJsonArray{project("P0", "Inbox", true)}}};
        runSync();
        m_server.forms.clear();
        Task parent;
        parent.title = "Parent";
        parent.listId = m_inbox;
        const qint64 pid = addTask(parent);
        for (int i = 0; i < 249; ++i) {
            Task t;
            t.title = QString("Task %1").arg(i);
            t.listId = m_inbox;
            t.parentId = i % 2 ? pid : 0;
            addTask(t);
        }
        runSync();
        QVERIFY2(m_sync->status().startsWith("Synced: 0 in, 250 out"), qPrintable(m_sync->status()));
        QVector<int> sizes;
        for (int i = 0; i < m_server.forms.size(); ++i)
            sizes << m_server.commandsOf(i).size();
        QCOMPARE(sizes, QVector<int>({0, 100, 100, 50}));
        QCOMPARE(m_server.types().count("item_add"), 250);
        QCOMPARE(m_store->syncLinks("todoist", "tasks").size(), 250);
        // Children in later batches refer to the real id, not a stale temp id.
        const QString parentRemote = m_store->syncLinks("todoist", "tasks").value(pid).remoteId;
        bool sawReal = false;
        for (const auto &c : m_server.allCommands()) {
            const QString p = c.toObject().value("args").toObject().value("parent_id").toString();
            if (p == parentRemote) sawReal = true;
        }
        QVERIFY(sawReal);
        QVERIFY(buildCommands(*m_store).isEmpty());
    }

    void rejectedCommandsAreRetriedNotDuplicated() {
        syncedBase();
        Task t;
        t.title = "Beta";
        t.listId = m_inbox;
        t.completed = true;
        const qint64 id = addTask(t);
        m_server.rejectTypes = {{"item_close", true}};
        runSync();
        QVERIFY(m_store->syncLinks("todoist", "tasks").contains(id));  // item_add succeeded: linked
        m_server.rejectTypes = {};
        m_server.forms.clear();
        runSync();
        QCOMPARE(m_server.types(), QStringList({"item_close"}));
    }

    void unauthorized() {
        syncedBase();
        m_server.failStatus = 401;
        m_server.failTimes = -1;
        runSync();
        QVERIFY2(m_sync->status().contains("401"), qPrintable(m_sync->status()));
        QCOMPARE(m_server.forms.size(), 1);
        QVERIFY(!m_sync->busy());
    }

    void rateLimitedThenRecovers() {
        setup();
        m_server.failStatus = 429;
        m_server.failTimes = 2;
        m_server.canned = {{"projects", QJsonArray{project("P0", "Inbox", true)}}};
        runSync();
        QVERIFY2(m_sync->status().startsWith("Synced"), qPrintable(m_sync->status()));
        QVERIFY(m_server.forms.size() >= 3);
        QCOMPARE(m_server.forms[0].queryItemValue("sync_token"), m_server.forms[2].queryItemValue("sync_token"));
    }

    void rateLimitedForever() {
        setup();
        m_server.failStatus = 429;
        m_server.failTimes = -1;
        runSync();
        QVERIFY2(m_sync->status().contains("rate limit"), qPrintable(m_sync->status()));
        QCOMPARE(m_server.forms.size(), 4);  // First try plus three retries.
    }

    void networkError() {
        setup();
        m_server.failStatus = 0;
        m_sync->setTransport([](const QUrlQuery &) { return QJsonObject{{"http_status", 0}, {"error", "Host not found"}}; });
        runSync();
        QVERIFY2(m_sync->status().contains("Host not found"), qPrintable(m_sync->status()));
    }
};

REGISTER_TEST(TodoistTest)
#include "test_todoist.moc"
