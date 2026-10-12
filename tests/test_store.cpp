#include <QtSql>
#include <QtTest>
#include <memory>

#include "store.h"
#include "testregistry.h"

class StoreTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_path;

    std::unique_ptr<Store> open() {
        auto s = std::make_unique<Store>();
        QString err;
        [&] { QVERIFY2(s->open(m_path, &err), qPrintable(err)); }();
        return s;
    }

    static qint64 put(Store &s, const QString &title, const QString &label = "add") {
        Task t;
        t.id = s.nextId("tasks");
        t.listId = 1;
        t.title = title;
        s.begin(label);
        s.putTask(t);
        s.commit();
        return t.id;
    }

    static void rename(Store &s, qint64 id, const QString &title, const QString &label = "rename") {
        Task t = s.task(id);
        t.title = title;
        s.begin(label);
        s.putTask(t);
        s.commit();
    }

private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        m_path = m_dir.filePath(QString("s%1.db").arg(QTest::currentTestFunction()));
        QFile::remove(m_path);
    }

    void persistsAcrossReopen() {
        qint64 id;
        {
            auto s = open();
            id = s->nextId("tasks");
            Task t;
            t.id = id;
            t.listId = 7;
            t.title = "Buy milk";
            t.notes = "semi-skimmed";
            t.priority = Priority::High;
            t.starred = true;
            t.dueDate = QDate(2026, 7, 15);
            t.dueTime = 15 * 60;
            t.duration = 45;
            t.recurrence.frequency = Repeat::Weekly;
            t.recurrence.weekdays = 5;
            t.tagIds = {3, 4};
            TaskList l;
            l.id = s->nextId("lists");
            l.name = "Home";
            l.showInToday = false;
            s->begin("add");
            s->putTask(t);
            s->putList(l);
            QVERIFY(s->commit());
        }
        auto s = open();
        QCOMPARE(s->tasks().size(), 1);
        const Task t = s->task(id);
        QCOMPARE(t.title, QString("Buy milk"));
        QCOMPARE(t.notes, QString("semi-skimmed"));
        QCOMPARE(t.listId, qint64(7));
        QCOMPARE(t.priority, int(Priority::High));
        QVERIFY(t.starred);
        QCOMPARE(t.dueDate, QDate(2026, 7, 15));
        QCOMPARE(t.dueTime, 15 * 60);
        QVERIFY(!t.startDate.isValid());
        QCOMPARE(t.startTime, -1);
        QCOMPARE(t.duration, 45);
        QCOMPARE(t.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(t.recurrence.weekdays, 5);
        QCOMPARE(t.tagIds, (QList<qint64>{3, 4}));
        QVERIFY(!t.uid.isEmpty());
        QVERIFY(t.created.isValid());
        QVERIFY(t.modified.isValid());
        QCOMPARE(s->lists().size(), 1);
        QCOMPARE(s->lists().begin()->name, QString("Home"));
        QVERIFY(!s->lists().begin()->showInToday);
    }

    void idsContinueAfterReopen() {
        qint64 first;
        {
            auto s = open();
            first = put(*s, "a");
        }
        auto s = open();
        QVERIFY(s->nextId("tasks") > first);
    }

    void undoRestoresPreviousState() {
        auto s = open();
        const auto id = put(*s, "one");
        rename(*s, id, "two", "rename it");
        QVERIFY(s->canUndo());
        QCOMPARE(s->undoLabel(), QString("rename it"));
        QVERIFY(s->undo());
        QCOMPARE(s->task(id).title, QString("one"));
        QVERIFY(s->canRedo());
        QCOMPARE(s->redoLabel(), QString("rename it"));
        QVERIFY(s->redo());
        QCOMPARE(s->task(id).title, QString("two"));
        QVERIFY(!s->canRedo());
    }

    void undoRedoReachDisk() {
        qint64 id;
        {
            auto s = open();
            id = put(*s, "one");
            rename(*s, id, "two");
            s->undo();
        }
        {
            auto s = open();
            QCOMPARE(s->task(id).title, QString("one"));
        }
        {
            auto s = open();
            rename(*s, id, "two");
            s->undo();
            s->redo();
        }
        auto s = open();
        QCOMPARE(s->task(id).title, QString("two"));
    }

    void undoOfCreationRemovesRecord() {
        qint64 id;
        {
            auto s = open();
            id = put(*s, "new");
            QVERIFY(s->tasks().contains(id));
            QVERIFY(s->undo());
            QVERIFY(!s->tasks().contains(id));
            QVERIFY(!s->canUndo());
        }
        auto s = open();
        QVERIFY(s->tasks().isEmpty());
    }

    void redoOfCreationRestoresRecord() {
        auto s = open();
        const auto id = put(*s, "new");
        s->undo();
        QVERIFY(s->redo());
        QCOMPARE(s->task(id).title, QString("new"));
    }

    void newChangeClearsRedo() {
        auto s = open();
        const auto id = put(*s, "a");
        rename(*s, id, "b");
        s->undo();
        QVERIFY(s->canRedo());
        rename(*s, id, "c");
        QVERIFY(!s->canRedo());
    }

    void undoWithNothingIsHarmless() {
        auto s = open();
        QVERIFY(!s->canUndo());
        QVERIFY(!s->undo());
        QVERIFY(!s->redo());
        QVERIFY(s->undoLabel().isEmpty());
    }

    void severalPutsOfOneRecordUndoAsOneStep() {
        auto s = open();
        const auto id = put(*s, "orig");
        Task t = s->task(id);
        s->begin("multi");
        t.title = "x";
        s->putTask(t);
        t.title = "y";
        s->putTask(t);
        t.title = "z";
        s->putTask(t);
        QVERIFY(s->commit());
        QCOMPARE(s->task(id).title, QString("z"));
        QVERIFY(s->undo());
        QCOMPARE(s->task(id).title, QString("orig"));  // not "x" or "y"
        QCOMPARE(s->undoLabel(), QString("add"));
    }

    void changeAcrossRecordsUndoesTogether() {
        auto s = open();
        const auto a = put(*s, "a");
        const auto b = put(*s, "b");
        Task ta = s->task(a), tb = s->task(b);
        ta.title = "A";
        tb.title = "B";
        s->begin("both");
        s->putTask(ta);
        s->putTask(tb);
        s->commit();
        s->undo();
        QCOMPARE(s->task(a).title, QString("a"));
        QCOMPARE(s->task(b).title, QString("b"));
    }

    void emptyCommitIsNotUndoable() {
        auto s = open();
        s->begin("nothing");
        QVERIFY(s->commit());
        QVERIFY(!s->canUndo());
    }

    void changedSignalFires() {
        auto s = open();
        QSignalSpy spy(s.get(), &Store::changed);
        const auto id = put(*s, "a");
        QCOMPARE(spy.count(), 1);
        s->undo();
        QCOMPARE(spy.count(), 2);
        Q_UNUSED(id);
    }

    void discardRemovesTaskAndScrubsHistory() {
        qint64 a, b;
        {
            auto s = open();
            a = put(*s, "A", "add A");
            rename(*s, a, "A2", "rename A");
            b = put(*s, "B", "add B");
            s->discard("tasks", a);
            QVERIFY(!s->tasks().contains(a));
            QVERIFY(s->tasks().contains(b));
            QCOMPARE(s->undoLabel(), QString("add B"));
            QVERIFY(s->undo());  // B's creation
            QVERIFY(!s->canUndo());  // A's two changes are gone from history
            QVERIFY(!s->tasks().contains(b));
            QVERIFY(!s->tasks().contains(a));
        }
        auto s = open();
        QVERIFY(s->tasks().isEmpty());
    }

    void discardScrubsRedoToo() {
        auto s = open();
        const auto a = put(*s, "A");
        s->undo();
        QVERIFY(s->canRedo());
        s->discard("tasks", a);
        QVERIFY(!s->canRedo());
    }

    void discardKeepsOtherEditsInSameChange() {
        auto s = open();
        const auto a = put(*s, "A");
        const auto b = put(*s, "B");
        Task ta = s->task(a), tb = s->task(b);
        ta.title = "A2";
        tb.title = "B2";
        s->begin("both");
        s->putTask(ta);
        s->putTask(tb);
        s->commit();
        s->discard("tasks", a);
        QCOMPARE(s->undoLabel(), QString("both"));
        s->undo();
        QCOMPARE(s->task(b).title, QString("B"));
        QVERIFY(!s->tasks().contains(a));
    }

    void settingsRoundTrip() {
        {
            auto s = open();
            QCOMPARE(s->setting("missing", 42).toInt(), 42);
            QVERIFY(!s->setting("missing").isValid());
            s->setSetting("theme", "dark");
            s->setSetting("count", 12);
            s->setSetting("flag", true);
            QCOMPARE(s->setting("theme").toString(), QString("dark"));
            s->setSetting("theme", "light");
        }
        auto s = open();
        QCOMPARE(s->setting("theme").toString(), QString("light"));
        QCOMPARE(s->setting("count").toInt(), 12);
        QCOMPARE(s->setting("flag").toString(), QString("true"));
        QCOMPARE(s->setting("missing", "fb").toString(), QString("fb"));
    }

    void emptyStringsSurviveAsEmptyNotNull() {
        qint64 id;
        {
            auto s = open();
            id = s->nextId("tasks");
            Task t;
            t.id = id;
            t.title = "";
            t.notes = "";
            t.actionValue = "";
            TaskList l;
            l.id = s->nextId("lists");
            l.name = "";
            s->begin("empty");
            s->putTask(t);
            s->putList(l);
            QVERIFY(s->commit());
        }
        {
            auto s = open();
            QVERIFY(s->tasks().contains(id));
            QCOMPARE(s->task(id).title, QString(""));
            QCOMPARE(s->task(id).notes, QString(""));
            QCOMPARE(s->lists().size(), 1);
        }
        const QString conn = "store-test-raw";
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
            db.setDatabaseName(m_path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM tasks WHERE title IS NULL OR notes IS NULL OR action_value IS NULL"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
            QVERIFY(q.exec("SELECT COUNT(*) FROM lists WHERE name IS NULL OR smart_query IS NULL"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
        }
        QSqlDatabase::removeDatabase(conn);
    }

    void otherRecordTypesPersist() {
        {
            auto s = open();
            ListGroup g;
            g.id = s->nextId("list_groups");
            g.name = "Work";
            Tag tag;
            tag.id = s->nextId("tags");
            tag.name = "someday";
            tag.paused = true;
            TagGroup tg;
            tg.id = s->nextId("tag_groups");
            tg.name = "Context";
            s->begin("misc");
            s->putGroup(g);
            s->putTag(tag);
            s->putTagGroup(tg);
            QVERIFY(s->commit());
        }
        auto s = open();
        QCOMPARE(s->groups().size(), 1);
        QCOMPARE(s->groups().begin()->name, QString("Work"));
        QCOMPARE(s->tags().size(), 1);
        QVERIFY(s->tags().begin()->paused);
        QCOMPARE(s->tagGroups().begin()->name, QString("Context"));
    }

    void newUidsAreUnique() {
        QVERIFY(Store::newUid() != Store::newUid());
        QVERIFY(!Store::newUid().isEmpty());
    }
};

REGISTER_TEST(StoreTest)
#include "test_store.moc"
