#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <memory>

#include "automation.h"
#include "store.h"
#include "testregistry.h"

class AutomationTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<Store> m_store;
    std::unique_ptr<Automation> m_auto;
    qint64 m_inbox = 0, m_work = 0;

    static QString id(qint64 v) { return QString::number(v); }

    QJsonObject ok(const QString &cmd, const QJsonObject &args = {}) {
        const QJsonObject r = m_auto->run(cmd, args);
        if (!r["ok"].toBool())
            qWarning() << cmd << QJsonDocument(r).toJson(QJsonDocument::Compact);
        return r;
    }

    static QString code(const QJsonObject &r) { return r["error"].toObject()["code"].toString(); }

    qint64 addTask(const QJsonObject &args) {
        const auto r = m_auto->run("create_task", args);
        return r["task"].toObject()["id"].toString().toLongLong();
    }

    int liveTasks() {
        int n = 0;
        for (const auto &t : m_store->tasks())
            n += !t.deleted;
        return n;
    }

private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        m_auto.reset();
        m_store = std::make_unique<Store>();
        const QString path = m_dir.filePath(QString("a%1.db").arg(QTest::currentTestFunction()));
        QFile::remove(path);
        QString err;
        QVERIFY2(m_store->open(path, &err), qPrintable(err));
        TaskList inbox;
        inbox.id = m_store->nextId("lists");
        inbox.kind = ListKind::Inbox;
        inbox.name = "Inbox";
        TaskList work;
        work.id = m_store->nextId("lists");
        work.name = "Work";
        work.order = 1;
        TaskList smart;
        smart.id = m_store->nextId("lists");
        smart.kind = ListKind::Smart;
        smart.name = "Urgent";
        smart.smartQuery = "type: hiprio";
        m_store->begin("setup");
        m_store->putList(inbox);
        m_store->putList(work);
        m_store->putList(smart);
        m_store->commit();
        m_inbox = inbox.id;
        m_work = work.id;
        m_auto = std::make_unique<Automation>(*m_store);
        m_auto->setNow(QDateTime(QDate(2026, 7, 10), QTime(10, 0)));
    }

    void discovery() {
        auto lists = ok("list_lists");
        QCOMPARE(lists["item_count"].toInt(), 2);
        QCOMPARE(lists["lists"].toArray().first().toObject()["name"].toString(), "Inbox");
        QCOMPARE(ok("list_smart_lists")["smart_lists"].toArray().size(), 1);
        QCOMPARE(ok("list_list_groups")["item_count"].toInt(), 0);
        QCOMPARE(ok("list_tag_groups")["item_count"].toInt(), 0);
        QCOMPARE(ok("list_tags")["item_count"].toInt(), 0);
        QCOMPARE(ok("list_locations")["item_count"].toInt(), 0);
        const auto dest = ok("get_default_task_destination")["destination"].toObject();
        QCOMPARE(dest["list_id"].toString(), id(m_inbox));
        QCOMPARE(dest["destination_id"].toString(), "list:" + id(m_inbox));
        QCOMPARE(code(m_auto->run("list_lists", {{"bogus", 1}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("nope")), "invalid_arguments");
    }

    void createAndGet() {
        const auto r = ok("create_task", {{"title", "Pay rent"},
                                          {"notes", "Landlord"},
                                          {"list_id", id(m_work)},
                                          {"priority", "high"},
                                          {"starred", true},
                                          {"due_date", "2026-07-03"},
                                          {"due_time", "09:30"},
                                          {"start_date", "2026-07-01"},
                                          {"duration_seconds", 1800},
                                          {"tag_names", QJsonArray{"Money", "home"}},
                                          {"action", QJsonObject{{"type", "url"}, {"value", "https://example.com"}}}});
        QVERIFY(r["ok"].toBool());
        const auto t = r["task"].toObject();
        QCOMPARE(t["title"].toString(), "Pay rent");
        QCOMPARE(t["list"].toObject()["name"].toString(), "Work");
        QCOMPARE(t["priority"].toString(), "high");
        QCOMPARE(t["due_time"].toString(), "09:30");
        QCOMPARE(t["start_date"].toString(), "2026-07-01");
        QVERIFY(t["start_time"].isNull());
        QCOMPARE(t["duration_seconds"].toInt(), 1800);
        QCOMPARE(t["tags"].toArray().size(), 2);
        QCOMPARE(t["action"].toObject()["type"].toString(), "url");
        QCOMPARE(t["type"].toString(), "normal");
        QVERIFY(t["overdue"].toBool());  // Due before the pinned clock.
        QVERIFY(!t["uid"].toString().isEmpty());
        QCOMPARE(ok("get_task", {{"task_id", t["id"]}})["task"].toObject()["title"].toString(), "Pay rent");
        QCOMPARE(ok("get_task", {{"task_id", t["uid"]}})["task"].toObject()["id"], t["id"]);
        // Without a list the default destination (the Inbox) is used.
        QCOMPARE(ok("create_task", {{"title", "Loose"}})["task"].toObject()["list"].toObject()["id"].toString(), id(m_inbox));
        // Tags are matched case-insensitively and not duplicated.
        ok("create_task", {{"title", "Again"}, {"tag_names", QJsonArray{"money"}}});
        QCOMPARE(ok("list_tags")["item_count"].toInt(), 2);
    }

    void createValidation() {
        QCOMPARE(code(m_auto->run("create_task", {})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "  "}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"due_date", "07/03/2026"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"due_date", "2026-02-30"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"due_time", "09:30"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"due_date", "2026-07-03"}, {"due_time", "25:00"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"priority", "urgent"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"task_type", "folder"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"duration_seconds", -1}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"action", QJsonObject{{"type", "call"}}}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"colour", "red"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"list_id", "999"}})), "not_found");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"list_id", "abc"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"parent_task_id", "999"}})), "not_found");
        // A Smart List is not a place to put a task.
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"list_id", id(m_work + 1)}})), "not_found");
        QJsonArray many;
        for (int i = 0; i < 101; ++i)
            many.append(QString("s%1").arg(i));
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"subtasks", many}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", QString(1001, 'a')}})), "invalid_arguments");
        QCOMPARE(liveTasks(), 0);
        QVERIFY(!m_store->canUndo() || m_store->undoLabel() == "setup");
    }

    void subtasksAndParents() {
        const auto r = ok("create_task", {{"title", "Trip"}, {"list_id", id(m_work)}, {"subtasks", QJsonArray{"Flights", "Hotel"}}});
        const auto trip = r["task"].toObject();
        QCOMPARE(trip["type"].toString(), "checklist");
        QCOMPARE(r["subtasks"].toArray().size(), 2);
        const auto subs = ok("list_subtasks", {{"task_id", trip["id"]}});
        QCOMPARE(subs["item_count"].toInt(), 2);
        QCOMPARE(subs["parent_task_id"].toString(), trip["id"].toString());
        QCOMPARE(subs["tasks"].toArray().first().toObject()["title"].toString(), "Flights");
        QCOMPARE(subs["tasks"].toArray().first().toObject()["parent_id"].toString(), trip["id"].toString());

        // A plain task given a child becomes a project, in one undoable change.
        const qint64 plain = addTask({{"title", "Plain"}, {"list_id", id(m_work)}});
        const auto child = ok("create_task", {{"title", "Child"}, {"parent_task_id", id(plain)}});
        QCOMPARE(child["task"].toObject()["list"].toObject()["id"].toString(), id(m_work));
        QCOMPARE(m_store->task(plain).type, int(TaskType::Project));
        QVERIFY(m_store->undo());
        QCOMPARE(m_store->task(plain).type, int(TaskType::Task));
        // Sub-tasks do not nest.
        const auto again = ok("create_task", {{"title", "Child"}, {"parent_task_id", id(plain)}});
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"parent_task_id", again["task"].toObject()["id"]}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("create_task", {{"title", "x"}, {"parent_task_id", id(plain)}, {"subtasks", QJsonArray{"a"}}})), "invalid_arguments");
    }

    void naturalLanguage() {
        const auto t = ok("create_task", {{"title", "Call Sam tomorrow at 3pm #family !! [30m]"}, {"natural_language", true}})["task"].toObject();
        QCOMPARE(t["title"].toString(), "Call Sam");
        QCOMPARE(t["due_date"].toString(), "2026-07-11");
        QCOMPARE(t["due_time"].toString(), "15:00");
        QCOMPARE(t["priority"].toString(), "medium");
        QCOMPARE(t["duration_seconds"].toInt(), 1800);
        QCOMPARE(t["tags"].toArray().first().toObject()["name"].toString(), "family");
        // Without the flag the title is taken literally.
        QCOMPARE(ok("create_task", {{"title", "Call Sam tomorrow"}})["task"].toObject()["title"].toString(), "Call Sam tomorrow");
    }

    void updateTask() {
        const qint64 t = addTask({{"title", "Old"}, {"list_id", id(m_work)}, {"due_date", "2026-07-20"}, {"due_time", "10:00"}, {"tag_names", QJsonArray{"a", "b"}}});
        auto r = ok("update_task", {{"task_id", id(t)}, {"title", "New"}, {"priority", "low"}, {"notes", "n"}})["task"].toObject();
        QCOMPARE(r["title"].toString(), "New");
        QCOMPARE(r["priority"].toString(), "low");
        QCOMPARE(r["due_date"].toString(), "2026-07-20");  // Untouched.
        // null clears the date and its time.
        r = ok("update_task", {{"task_id", id(t)}, {"due_date", QJsonValue(QJsonValue::Null)}})["task"].toObject();
        QVERIFY(r["due_date"].isNull());
        QVERIFY(r["due_time"].isNull());
        r = ok("update_task", {{"task_id", id(t)}, {"tag_update", QJsonObject{{"mode", "append"}, {"tag_names", QJsonArray{"c"}}}}})["task"].toObject();
        QCOMPARE(r["tags"].toArray().size(), 3);
        r = ok("update_task", {{"task_id", id(t)}, {"tag_update", QJsonObject{{"mode", "remove"}, {"tag_names", QJsonArray{"a", "zzz"}}}}})["task"].toObject();
        QCOMPARE(r["tags"].toArray().size(), 2);
        r = ok("update_task", {{"task_id", id(t)}, {"tag_update", QJsonObject{{"mode", "set"}, {"tag_names", QJsonArray{"only"}}}}})["task"].toObject();
        QCOMPARE(r["tags"].toArray().size(), 1);
        r = ok("update_task_action", {{"task_id", id(t)}, {"action", QJsonObject{{"type", "call"}, {"value", "555"}}}})["task"].toObject();
        QCOMPARE(r["action"].toObject()["value"].toString(), "555");
        QCOMPARE(code(m_auto->run("update_task", {{"task_id", "999"}, {"title", "x"}})), "not_found");
        QCOMPARE(code(m_auto->run("update_task", {{"title", "x"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("update_task", {{"task_id", id(t)}, {"due_time", "11:00"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("update_task", {{"task_id", id(t)}, {"tag_update", QJsonObject{{"mode", "swap"}, {"tag_names", QJsonArray{}}}}})), "invalid_arguments");
        QCOMPARE(m_store->task(t).title, "New");
    }

    void queryTasks() {
        addTask({{"title", "Invoice Acme"}, {"list_id", id(m_work)}, {"due_date", "2026-07-05"}, {"priority", "high"}});
        addTask({{"title", "Invoice Beta"}, {"list_id", id(m_inbox)}, {"due_date", "2026-08-01"}});
        addTask({{"title", "Future thing"}, {"list_id", id(m_work)}, {"start_date", "2026-09-01"}});
        addTask({{"title", "Starred"}, {"list_id", id(m_work)}, {"starred", true}});
        const qint64 done = addTask({{"title", "Finished"}, {"list_id", id(m_work)}});
        ok("complete_task", {{"task_id", id(done)}});

        QCOMPARE(ok("query_tasks")["total_count"].toInt(), 4);
        QCOMPARE(ok("query_tasks", {{"include_completed", true}})["total_count"].toInt(), 5);
        QCOMPARE(ok("query_tasks", {{"list_id", id(m_work)}})["total_count"].toInt(), 3);
        QCOMPARE(ok("query_tasks", {{"search", "invoice"}})["total_count"].toInt(), 2);
        QCOMPARE(ok("query_tasks", {{"search", "invoice -beta"}})["tasks"].toArray().first().toObject()["title"].toString(), "Invoice Acme");
        QCOMPARE(ok("query_tasks", {{"due_on_or_before", "2026-07-31"}})["total_count"].toInt(), 1);
        QCOMPARE(ok("query_tasks", {{"start_on_or_after", "2026-08-01"}})["total_count"].toInt(), 1);
        QCOMPARE(ok("query_tasks", {{"start_on_or_before", "2026-08-01"}})["total_count"].toInt(), 0);
        QCOMPARE(ok("query_tasks", {{"smart_list_id", id(m_work + 1)}})["total_count"].toInt(), 1);  // type: hiprio
        QCOMPARE(ok("query_tasks", {{"focus", "starred"}})["total_count"].toInt(), 1);
        QCOMPARE(ok("query_tasks", {{"focus", "scheduled"}})["total_count"].toInt(), 1);
        QCOMPARE(ok("query_tasks", {{"focus", "done"}})["total_count"].toInt(), 1);
        QCOMPARE(ok("query_tasks", {{"focus", "today"}})["total_count"].toInt(), 1);  // The overdue invoice.
        const auto page = ok("query_tasks", {{"limit", 2}, {"offset", 1}});
        QCOMPARE(page["item_count"].toInt(), 2);
        QCOMPARE(page["total_count"].toInt(), 4);
        QCOMPARE(ok("query_tasks", {{"offset", 10}})["item_count"].toInt(), 0);

        QCOMPARE(code(m_auto->run("query_tasks", {{"limit", 0}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("query_tasks", {{"limit", 201}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("query_tasks", {{"offset", -1}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("query_tasks", {{"search", QString(257, 'a')}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("query_tasks", {{"due_on_or_before", "soon"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("query_tasks", {{"list_id", id(m_work)}, {"focus", "all"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("query_tasks", {{"list_id", "404"}})), "not_found");
        QCOMPARE(code(m_auto->run("query_tasks", {{"focus", "never"}})), "invalid_arguments");
    }

    void pausedTagsAndTagCommands() {
        addTask({{"title", "Hidden"}, {"list_id", id(m_work)}, {"tag_names", QJsonArray{"waiting"}}});
        addTask({{"title", "Shown"}, {"list_id", id(m_work)}});
        QCOMPARE(ok("pause_tag", {{"tag_name", "Waiting"}})["tag"].toObject()["paused"].toBool(), true);
        const int undoDepthLabel = m_store->undoLabel() == "Pause Tag";
        QVERIFY(undoDepthLabel);
        QCOMPARE(ok("query_tasks")["total_count"].toInt(), 1);
        QCOMPARE(ok("query_tasks", {{"paused_filter", "include"}})["total_count"].toInt(), 2);
        const auto only = ok("query_tasks", {{"paused_filter", "only"}});
        QCOMPARE(only["total_count"].toInt(), 1);
        QVERIFY(only["tasks"].toArray().first().toObject()["paused"].toBool());
        // Idempotent: pausing again records nothing new.
        ok("pause_tag", {{"tag_name", "waiting"}});
        QVERIFY(m_store->undo());
        QCOMPARE(m_store->undoLabel(), "New Task");  // The next undo is the second create, not a second pause.
        QVERIFY(m_store->redo());
        ok("unpause_tag", {{"tag_id", ok("list_tags")["tags"].toArray().first().toObject()["id"]}});
        QCOMPARE(ok("query_tasks")["total_count"].toInt(), 2);
        QCOMPARE(code(m_auto->run("pause_tag", {})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("pause_tag", {{"tag_id", "1"}, {"tag_name", "x"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("pause_tag", {{"tag_name", "nothing"}})), "not_found");
        QCOMPARE(code(m_auto->run("list_tags", {{"tag_group_id", "77"}})), "not_found");
    }

    void deleteDuplicateMove() {
        const qint64 project = addTask({{"title", "Proj"}, {"list_id", id(m_work)}, {"task_type", "project"}});
        ok("create_task", {{"title", "Kid"}, {"parent_task_id", id(project)}});
        const qint64 loose = addTask({{"title", "Loose"}, {"list_id", id(m_work)}});

        // Duplicate copies the subtree.
        const int before = liveTasks();
        const auto copy = ok("duplicate_task", {{"task_id", id(project)}})["task"].toObject();
        QCOMPARE(liveTasks(), before + 2);
        QVERIFY(copy["uid"].toString() != m_store->task(project).uid);
        QVERIFY(m_store->undo());
        QCOMPARE(liveTasks(), before);

        // Move to another list carries the children along.
        ok("move_task", {{"task_id", id(project)}, {"list_id", id(m_inbox)}});
        for (const auto &t : m_store->tasks())
            if (t.title == "Kid") QCOMPARE(t.listId, m_inbox);
        QVERIFY(m_store->undo());
        for (const auto &t : m_store->tasks())
            if (t.title == "Kid") QCOMPARE(t.listId, m_work);

        // Move a loose task into the project.
        const auto moved = ok("move_task", {{"task_ids", QJsonArray{id(loose)}}, {"parent_task_id", id(project)}});
        QCOMPARE(moved["tasks"].toArray().first().toObject()["parent_id"].toString(), id(project));
        QCOMPARE(code(m_auto->run("move_task", {{"task_id", id(project)}, {"parent_task_id", id(loose)}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("move_task", {{"task_id", id(loose)}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("move_task", {{"task_id", id(loose)}, {"list_id", id(m_work)}, {"parent_task_id", id(project)}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("move_task", {{"task_id", id(loose)}, {"list_id", "404"}})), "not_found");

        // Deleting a project deletes its children, in one undoable step.
        const auto del = ok("delete_task", {{"task_id", id(project)}});
        QCOMPARE(del["deleted"].toArray().size(), 1);
        QCOMPARE(liveTasks(), 0);
        QCOMPARE(code(m_auto->run("get_task", {{"task_id", id(project)}})), "not_found");
        QVERIFY(m_store->undo());
        QCOMPARE(liveTasks(), 3);
        QCOMPARE(code(m_auto->run("delete_task", {{"task_id", "1"}, {"task_ids", QJsonArray{"1"}}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("delete_task", {})), "invalid_arguments");
    }

    void completeAndUncomplete() {
        const qint64 a = addTask({{"title", "A"}, {"list_id", id(m_work)}});
        const qint64 b = addTask({{"title", "B"}, {"list_id", id(m_work)}});
        const auto r = ok("complete_task", {{"task_ids", QJsonArray{id(a), id(b)}}});
        QCOMPARE(r["item_count"].toInt(), 2);
        QVERIFY(m_store->task(a).completed && m_store->task(b).completed);
        QVERIFY(r["tasks"].toArray().first().toObject()["completed_at"].isString());
        QVERIFY(m_store->undo());
        QVERIFY(!m_store->task(a).completed && !m_store->task(b).completed);
        ok("complete_task", {{"task_id", id(a)}});
        ok("uncomplete_task", {{"task_id", id(a)}});
        QVERIFY(!m_store->task(a).completed);
        QVERIFY(!m_store->task(a).completedAt.isValid());

        QJsonArray many;
        for (int i = 0; i < 101; ++i)
            many.append(id(a));
        QCOMPARE(code(m_auto->run("complete_task", {{"task_ids", many}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("complete_task", {{"task_ids", QJsonArray{}}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("complete_task", {{"task_id", "999"}})), "not_found");
    }

    void completingProjectCompletesChildren() {
        const qint64 p = addTask({{"title", "P"}, {"list_id", id(m_work)}, {"task_type", "project"}});
        ok("create_task", {{"title", "K1"}, {"parent_task_id", id(p)}});
        ok("create_task", {{"title", "K2"}, {"parent_task_id", id(p)}});
        ok("complete_task", {{"task_id", id(p)}});
        for (const auto &t : m_store->tasks())
            QVERIFY(t.completed);
        QVERIFY(m_store->undo());
        for (const auto &t : m_store->tasks())
            QVERIFY(!t.completed);
    }

    void completingRepeatingTaskKeepsCopy() {
        const qint64 t = addTask({{"title", "Water plants"}, {"list_id", id(m_work)}, {"due_date", "2026-07-10"}});
        Task rec = m_store->task(t);
        rec.recurrence.frequency = Repeat::Weekly;
        rec.recurrence.interval = 1;
        rec.recurrence.weekdays = 1 << (QDate(2026, 7, 10).dayOfWeek() - 1);
        m_store->begin("make repeating");
        m_store->putTask(rec);
        m_store->commit();

        const auto r = ok("complete_task", {{"task_id", id(t)}})["task"].toObject();
        QVERIFY(!r["completed"].toBool());
        QCOMPARE(r["due_date"].toString(), "2026-07-17");
        QVERIFY(r["repeat"].isString());
        int copies = 0;
        for (const auto &c : m_store->tasks())
            if (c.completed && c.title == "Water plants" && !c.recurrence.isSet()) {
                ++copies;
                QCOMPARE(c.seriesUid, rec.uid);
            }
        QCOMPARE(copies, 1);
        QVERIFY(m_store->undo());
        QCOMPARE(m_store->task(t).dueDate, QDate(2026, 7, 10));
        QCOMPARE(liveTasks(), 1);
    }

    void everyMutationIsOneUndo() {
        struct Case { QString cmd; QJsonObject args; };
        const qint64 a = addTask({{"title", "A"}, {"list_id", id(m_work)}, {"tag_names", QJsonArray{"t"}}});
        const QList<Case> cases = {
            {"create_task", {{"title", "N"}, {"subtasks", QJsonArray{"x", "y"}}, {"tag_names", QJsonArray{"fresh"}}}},
            {"update_task", {{"task_id", id(a)}, {"title", "Z"}}},
            {"duplicate_task", {{"task_id", id(a)}}},
            {"move_task", {{"task_id", id(a)}, {"list_id", id(m_inbox)}}},
            {"complete_task", {{"task_id", id(a)}}},
            {"pause_tag", {{"tag_name", "t"}}},
            {"delete_task", {{"task_id", id(a)}}},
        };
        for (const auto &c : cases) {
            const QByteArray snapshot = [&] {
                QByteArray b;
                QStringList rows;
                for (const auto &t : m_store->tasks()) rows << QString("%1|%2|%3|%4|%5|%6").arg(t.id).arg(t.title).arg(t.listId).arg(t.completed).arg(t.deleted).arg(t.tagIds.size());
                for (const auto &g : m_store->tags()) rows << QString("tag%1|%2|%3|%4").arg(g.id).arg(g.name).arg(g.paused).arg(g.deleted);
                rows.sort();
                return rows.join('\n').toUtf8();
            }();
            QVERIFY2(ok(c.cmd, c.args)["ok"].toBool(), qPrintable(c.cmd));
            QVERIFY2(m_store->undo(), qPrintable(c.cmd));
            // Undoing the single change must restore everything, including tags it created.
            QStringList rows;
            for (const auto &t : m_store->tasks()) rows << QString("%1|%2|%3|%4|%5|%6").arg(t.id).arg(t.title).arg(t.listId).arg(t.completed).arg(t.deleted).arg(t.tagIds.size());
            for (const auto &g : m_store->tags()) rows << QString("tag%1|%2|%3|%4").arg(g.id).arg(g.name).arg(g.paused).arg(g.deleted);
            rows.sort();
            // Records created by the undone change linger only as absent rows.
            QStringList before = QString::fromUtf8(snapshot).split('\n', Qt::SkipEmptyParts);
            QCOMPARE(rows.size(), before.size());
            QCOMPARE(rows, before);
        }
    }

    void uiCommands() {
        QCOMPARE(code(m_auto->run("select_focus", {{"focus", "today"}})), "automation_unavailable");
        QCOMPARE(code(m_auto->run("select_list", {{"list_id", id(m_work)}})), "automation_unavailable");
        QCOMPARE(code(m_auto->run("select_smart_list", {{"smart_list_id", id(m_work + 1)}})), "automation_unavailable");
        QCOMPARE(code(m_auto->run("selected_tasks")), "automation_unavailable");
        const qint64 t = addTask({{"title", "T"}, {"list_id", id(m_work)}});
        QCOMPARE(code(m_auto->run("open_task", {{"task_id", id(t)}})), "automation_unavailable");

        QStringList keys;
        qint64 opened = 0;
        m_auto->selectSource = [&](const QString &k) { keys << k; };
        m_auto->openTask = [&](qint64 i) { opened = i; };
        m_auto->selectedTaskIds = [&] { return QList<qint64>{t, 9999}; };
        QVERIFY(ok("select_focus", {{"focus", "starred"}})["selected"].toBool());
        ok("select_list", {{"list_id", id(m_work)}});
        ok("select_smart_list", {{"smart_list_id", id(m_work + 1)}});
        QCOMPARE(keys, (QStringList{"starred", "list:" + id(m_work), "list:" + id(m_work + 1)}));
        QCOMPARE(ok("open_task", {{"task_id", id(t)}})["task_id"].toString(), id(t));
        QCOMPARE(opened, t);
        QCOMPARE(ok("selected_tasks")["item_count"].toInt(), 1);
        QCOMPARE(code(m_auto->run("select_list", {{"list_id", "404"}})), "not_found");
        QCOMPARE(code(m_auto->run("select_list", {{"list_id", id(m_work + 1)}})), "not_found");  // A Smart List.
        QCOMPARE(code(m_auto->run("select_list_group", {{"list_group_id", "5"}})), "not_found");
        QCOMPARE(code(m_auto->run("select_focus", {{"focus", "yesterday"}})), "invalid_arguments");
        QCOMPARE(code(m_auto->run("open_task", {{"task_id", "404"}})), "not_found");
    }

    void mcpRoundTrip() {
        auto rpc = [](const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n"; };
        auto request = [&](int n, const QString &method, const QJsonObject &params = {}) {
            QJsonObject o{{"jsonrpc", "2.0"}, {"id", n}, {"method", method}};
            if (!params.isEmpty()) o["params"] = params;
            return rpc(o);
        };
        QByteArray inBytes = request(1, "initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", QJsonObject()}});
        inBytes += rpc({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        inBytes += request(2, "tools/list");
        inBytes += request(3, "tools/call", {{"name", "create_task"}, {"arguments", QJsonObject{{"title", "Via MCP"}, {"list_id", id(m_work)}}}});
        inBytes += request(4, "tools/call", {{"name", "create_task"}, {"arguments", QJsonObject()}});
        inBytes += request(5, "ping");
        inBytes += request(6, "resources/list");
        inBytes += "not json\n";
        QBuffer inBuf(&inBytes);
        QVERIFY(inBuf.open(QIODevice::ReadOnly));
        QByteArray outBytes;
        QBuffer outBuf(&outBytes);
        QVERIFY(outBuf.open(QIODevice::WriteOnly));
        QTextStream in(&inBuf), out(&outBuf);
        McpServer server(*m_auto, "9.9");
        QCOMPARE(server.run(in, out), 0);
        out.flush();

        QList<QJsonObject> replies;
        for (const auto &line : outBytes.split('\n'))
            if (!line.isEmpty())
                replies << QJsonDocument::fromJson(line).object();
        QCOMPARE(replies.size(), 7);  // The notification gets no reply.
        const auto init = replies[0]["result"].toObject();
        QCOMPARE(init["protocolVersion"].toString(), "2025-06-18");
        QCOMPARE(init["serverInfo"].toObject()["name"].toString(), "precrime");
        QCOMPARE(init["serverInfo"].toObject()["version"].toString(), "9.9");
        QVERIFY(init["capabilities"].toObject().contains("tools"));
        const auto tools = replies[1]["result"].toObject()["tools"].toArray();
        QCOMPARE(tools.size(), Automation::commandNames().size());
        QVERIFY(Automation::commandNames().size() >= 26);
        for (const auto &v : tools) {
            const auto tool = v.toObject();
            QVERIFY(!tool["description"].toString().isEmpty());
            QCOMPARE(tool["inputSchema"].toObject()["type"].toString(), "object");
        }
        const auto call = replies[2]["result"].toObject();
        QVERIFY(!call["isError"].toBool());
        const auto body = QJsonDocument::fromJson(call["content"].toArray().first().toObject()["text"].toString().toUtf8()).object();
        QVERIFY(body["ok"].toBool());
        QCOMPARE(body["task"].toObject()["title"].toString(), "Via MCP");
        QCOMPARE(replies[2]["id"].toInt(), 3);
        const auto bad = replies[3]["result"].toObject();
        QVERIFY(bad["isError"].toBool());
        QVERIFY(bad["content"].toArray().first().toObject()["text"].toString().contains("invalid_arguments"));
        QVERIFY(replies[4]["result"].isObject());
        QCOMPARE(replies[5]["error"].toObject()["code"].toInt(), -32601);
        QCOMPARE(replies[6]["error"].toObject()["code"].toInt(), -32700);
        QCOMPARE(liveTasks(), 1);
    }

    void cliParsing() {
        auto cli = [&](const QStringList &args, int *exit = nullptr) {
            QString o, e;
            QTextStream out(&o), err(&e);
            const int rc = m_auto->runCli(args, out, err);
            out.flush();
            err.flush();
            if (exit) *exit = rc;
            return QPair<QString, QString>(o, e);
        };
        int rc = -1;
        auto help = cli({"help"}, &rc);
        QCOMPARE(rc, 0);
        for (const auto &name : Automation::commandNames())
            QVERIFY2(help.first.contains(name), qPrintable(name));
        QCOMPARE(cli({}, &rc).first, help.first);
        QVERIFY(cli({"help", "create_task"}).first.contains("natural_language"));

        auto r = cli({"create_task", "{\"title\":\"From JSON\",\"list_id\":\"" + id(m_work) + "\"}"}, &rc);
        QCOMPARE(rc, 0);
        QVERIFY(QJsonDocument::fromJson(r.first.toUtf8()).object()["ok"].toBool());
        QVERIFY(r.first.contains("\n  "));  // Pretty printed.

        // key=value arguments are typed from the schema: title stays a string, limit a number, flags booleans.
        r = cli({"create_task", "title=123", "priority=high", "starred=true", "tag_names=a,b", "due_date=2026-07-09", "list_id=" + id(m_work)}, &rc);
        QCOMPARE(rc, 0);
        const auto t = QJsonDocument::fromJson(r.first.toUtf8()).object()["task"].toObject();
        QCOMPARE(t["title"].toString(), "123");
        QVERIFY(t["starred"].toBool());
        QCOMPARE(t["tags"].toArray().size(), 2);
        r = cli({"query_tasks", "limit=1", "search=from", "include_completed=false"}, &rc);
        QCOMPARE(rc, 0);
        QCOMPARE(QJsonDocument::fromJson(r.first.toUtf8()).object()["item_count"].toInt(), 1);
        r = cli({"update_task", "task_id=" + t["id"].toString(), "due_date=null"}, &rc);
        QVERIFY(QJsonDocument::fromJson(r.first.toUtf8()).object()["task"].toObject()["due_date"].isNull());

        cli({"query_tasks", "limit=500"}, &rc);
        QCOMPARE(rc, 1);
        cli({"nope"}, &rc);
        QCOMPARE(rc, 1);
        QVERIFY(!cli({"create_task", "{bad json"}, &rc).second.isEmpty());
        QCOMPARE(rc, 1);
        QVERIFY(!cli({"create_task", "titleonly"}, &rc).second.isEmpty());
        QCOMPARE(rc, 1);
        cli({"url", "precrime://x-callback-url/showtoday"}, &rc);
        QCOMPARE(rc, 0);
    }

    void urlActions() {
        QStringList keys;
        m_auto->selectSource = [&](const QString &k) { keys << k; };
        qint64 opened = 0;
        m_auto->openTask = [&](qint64 i) { opened = i; };

        auto url = [&](const QString &u) { return m_auto->handleUrl(QUrl(u)); };
        QCOMPARE(url("precrime://x-callback-url/showToday")["source"].toString(), "today");
        QCOMPARE(url("precrime://x-callback-url/showall")["source"].toString(), "all");
        QCOMPARE(url("precrime://x-callback-url/showstarred")["source"].toString(), "starred");
        QCOMPARE(url("precrime://x-callback-url/showscheduled")["source"].toString(), "scheduled");
        QCOMPARE(keys, (QStringList{"today", "all", "starred", "scheduled"}));
        QVERIFY(url("precrime://x-callback-url/launch")["ok"].toBool());
        QCOMPARE(url("precrime://x-callback-url/showlist?name=Work")["source"].toString(), "list:" + id(m_work));
        QCOMPARE(code(url("precrime://x-callback-url/showlist?name=work")), "not_found");  // Case-sensitive.
        QCOMPARE(url("precrime://x-callback-url/search?text=invoice%20acme")["search"].toString(), "invoice acme");
        QCOMPARE(code(url("precrime://x-callback-url/frobnicate")), "invalid_arguments");
        QCOMPARE(code(url("twodo://x-callback-url/showtoday")), "invalid_arguments");

        const auto added = url("precrime://x-callback-url/add?task=Submit%20expenses&note=Receipts&forlist=work&due=2026-07-15&duetime=17:00"
                               "&tags=finance%2Cadmin&priority=2&starred=1&action=call%3A%2B15551234567&x-success=other%3A%2F%2Fdone");
        QVERIFY2(added["ok"].toBool(), qPrintable(QJsonDocument(added).toJson()));
        const auto task = added["task"].toObject();
        QCOMPARE(task["title"].toString(), "Submit expenses");
        QCOMPARE(task["list"].toObject()["name"].toString(), "Work");
        QCOMPARE(task["due_time"].toString(), "17:00");
        QCOMPARE(task["priority"].toString(), "medium");
        QCOMPARE(task["tags"].toArray().size(), 2);
        QCOMPARE(task["action"].toObject()["value"].toString(), "+15551234567");
        QCOMPARE(task["notes"].toString(), "Receipts");
        QVERIFY(task["starred"].toBool());
        QVERIFY(added["callback"].toString().startsWith("other://done?add="));
        QVERIFY(m_store->canUndo());

        // Day offsets, phrases, checklists and repeat presets.
        auto rel = url("precrime://x-callback-url/add?task=Offset&due=1&repeat=4")["task"].toObject();
        QCOMPARE(rel["due_date"].toString(), "2026-07-11");
        QVERIFY(rel["repeat"].isString());
        QCOMPARE(url("precrime://x-callback-url/add?task=Phrase&due=tomorrow")["task"].toObject()["due_date"].toString(), "2026-07-11");
        const auto list = url("precrime://x-callback-url/add?task=Trip&type=2&subtasks=Flights%0AHotel&forlist=Work");
        QCOMPARE(list["task"].toObject()["type"].toString(), "checklist");
        QCOMPARE(list["subtasks"].toArray().size(), 2);
        QCOMPARE(url("precrime://x-callback-url/add")["task"].toObject()["title"].toString(), "New Task");

        // A parent by name inside a list.
        const auto proj = url("precrime://x-callback-url/add?task=Launch&type=1&forlist=Work")["task"].toObject();
        const auto kid = url("precrime://x-callback-url/add?task=Copy&forlist=Work&forparentname=launch")["task"].toObject();
        QCOMPARE(kid["parent_id"].toString(), proj["id"].toString());
        const auto found = url("precrime://x-callback-url/gettaskid?task=Launch&forlist=Work");
        QCOMPARE(found["uid"].toString(), proj["uid"].toString());

        QCOMPARE(code(url("precrime://x-callback-url/add?task=x&priority=9")), "invalid_arguments");
        QCOMPARE(code(url("precrime://x-callback-url/add?task=x&type=7")), "invalid_arguments");
        QCOMPARE(code(url("precrime://x-callback-url/add?task=x&due=2026-13-45")), "invalid_arguments");
        QCOMPARE(code(url("precrime://x-callback-url/add?task=x&forlist=Nowhere")), "not_found");
        QCOMPARE(code(url("precrime://x-callback-url/add?task=x&repeat=2")), "invalid_arguments");
        QCOMPARE(code(url("precrime://x-callback-url/add?task=x&action=teleport%3Amars")), "invalid_arguments");

        // Quick Entry hand-off creates nothing.
        const int tasksBefore = liveTasks();
        const auto qe = url("precrime://x-callback-url/add?task=Call%20Sam&usequickentry=1&tags=family&priority=2&due=1");
        QVERIFY(qe["quickEntry"].toBool());
        QCOMPARE(qe["text"].toString(), "Call Sam #family !! tomorrow");
        QCOMPARE(liveTasks(), tasksBefore);
        QVERIFY(url("precrime://x-callback-url/addnewtask")["quickEntry"].toBool());

        // showtask / completetasks by uid, and the shareable link round trip.
        const Task shown = m_store->task(task["id"].toString().toLongLong());
        const QString link = Automation::shareableLink(shown);
        QCOMPARE(link, "precrime://x-callback-url/showTask?uid=" + shown.uid);
        QVERIFY(url(link)["ok"].toBool());
        QCOMPARE(opened, shown.id);
        QCOMPARE(code(url("precrime://x-callback-url/showtask?uid=missing")), "not_found");
        const auto completed = url("precrime://x-callback-url/completetasks?uids=" + shown.uid);
        QVERIFY(completed["ok"].toBool());
        QVERIFY(m_store->task(shown.id).completed);
    }
};

REGISTER_TEST(AutomationTest)
#include "test_automation.moc"
