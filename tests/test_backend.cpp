#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "store.h"
#include "testregistry.h"

// Drives the backend the way the keyboard does, on a temporary database.
class BackendTest : public QObject {
    Q_OBJECT

    struct Fixture {
        QTemporaryDir dir;
        Store store;
        std::unique_ptr<Backend> backend;
        Fixture() {
            store.open(dir.filePath("t.db"));
            backend = std::make_unique<Backend>(&store);
            backend->ensureDefaults();
        }
        qint64 firstList() const {
            for (const auto &l : store.lists())
                if (!l.deleted) return l.id;
            return 0;
        }
        Task only() const {
            for (const auto &t : store.tasks())
                if (!t.deleted) return t;
            return {};
        }
    };

private slots:
    void newTaskInTodayIsDueToday() {
        Fixture f;
        f.backend->selectSource("today");
        f.backend->newTask();
        QCOMPARE(f.backend->editingTaskId(), f.only().id);
        f.backend->setTitle(f.only().id, "Buy milk");
        f.backend->endEdit();
        QCOMPARE(f.only().dueDate, QDate::currentDate());
        QCOMPARE(f.only().title, QString("Buy milk"));
    }

    void newTaskInStarredIsStarred() {
        Fixture f;
        f.backend->selectSource("starred");
        f.backend->newTask();
        f.backend->setTitle(f.only().id, "Focus");
        f.backend->endEdit();
        QVERIFY(f.only().starred);
    }

    void abandonedNewTaskLeavesNoTrace() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        const bool couldUndo = f.store.canUndo();
        f.backend->newTask();
        f.backend->endEdit();
        QCOMPARE(f.only().id, qint64(0));
        QCOMPARE(f.store.canUndo(), couldUndo);
    }

    void completingRepeatingTaskAdvancesIt() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->newTask();
        const qint64 id = f.backend->editingTaskId();
        f.backend->setTitle(id, "Water plants");
        f.backend->endEdit();
        f.backend->selectTask(id);
        f.backend->setDueInDays(0);
        f.backend->setRepeatPreset(id, 1);  // Daily.
        f.backend->toggleComplete(id);

        const Task original = f.store.task(id);
        QVERIFY(!original.completed);
        QCOMPARE(original.dueDate, QDate::currentDate().addDays(1));
        int copies = 0;
        for (const auto &t : f.store.tasks())
            if (t.id != id && t.completed && t.title == "Water plants") ++copies;
        QCOMPARE(copies, 1);

        f.backend->undo();
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate());
        int remaining = 0;
        for (const auto &t : f.store.tasks())
            if (!t.deleted) ++remaining;
        QCOMPARE(remaining, 1);
    }

    void bareKeyDateNudges() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->newTask();
        const qint64 id = f.backend->editingTaskId();
        f.backend->setTitle(id, "Report");
        f.backend->endEdit();
        f.backend->selectTask(id);
        f.backend->nudgeDue(1);  // `]` on an undated task: due today.
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate());
        f.backend->nudgeDue(7);  // `}`: a week later.
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate().addDays(7));
        f.backend->clearDue();
        QVERIFY(!f.store.task(id).dueDate.isValid());
        f.backend->setPriority(3);
        f.backend->bumpPriority(1);
        QCOMPARE(f.store.task(id).priority, 3);
        f.backend->bumpPriority(-1);
        QCOMPARE(f.store.task(id).priority, 2);
    }

    void typedDatesApply() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->newTask();
        const qint64 id = f.backend->editingTaskId();
        f.backend->setTitle(id, "Dentist");
        QVERIFY(f.backend->setDueFromText(id, "tomorrow 3pm"));
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate().addDays(1));
        QCOMPARE(f.store.task(id).dueTime, 15 * 60);
        QVERIFY(!f.backend->setDueFromText(id, "gibberish words"));
    }

    void tagsAndPausing() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->newTask();
        const qint64 id = f.backend->editingTaskId();
        f.backend->setTitle(id, "Call plumber");
        f.backend->addTag(id, "#waiting");
        f.backend->endEdit();
        QCOMPARE(f.store.task(id).tagIds.size(), 1);
        const qint64 tag = f.store.task(id).tagIds.first();
        f.backend->setTagPaused(tag, true);
        bool listed = false;
        for (const auto &row : f.backend->tasks()->rows())
            listed = listed || row.value("taskId").toLongLong() == id;
        QVERIFY(!listed);
        f.backend->setShowPaused(true);
        for (const auto &row : f.backend->tasks()->rows())
            listed = listed || row.value("taskId").toLongLong() == id;
        QVERIFY(listed);
    }

    void deleteMovesCursorAndUndoRestores() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        QVector<qint64> ids;
        for (const char *title : {"One", "Two", "Three"}) {
            f.backend->newTask();
            ids << f.backend->editingTaskId();
            f.backend->setTitle(ids.last(), title);
            f.backend->endEdit();
        }
        f.backend->setSort(SortField::Manual);
        f.backend->selectTask(ids.at(1));
        f.backend->deleteSelection();
        QVERIFY(f.store.task(ids.at(1)).deleted);
        QCOMPARE(f.backend->currentTaskId(), ids.at(2));
        f.backend->undo();
        QVERIFY(!f.store.task(ids.at(1)).deleted);
    }

    void sectionsGroupTasksAndUndo() {
        Fixture f;
        const qint64 list = f.firstList();
        f.backend->selectSource("list:" + QString::number(list));
        f.backend->newTask();
        const qint64 id = f.backend->editingTaskId();
        f.backend->setTitle(id, "Plan");
        f.backend->endEdit();
        const qint64 sec = f.backend->newSection(list, "Next Up");
        QCOMPARE(f.store.list(list).sortField, int(SortField::Section));  // First section switches the sort.
        f.backend->selectTask(id);
        f.backend->moveSelectionToSection(sec);
        QCOMPARE(f.store.task(id).sectionId, sec);
        QStringList headings;
        for (const auto &row : f.backend->tasks()->rows())
            if (row.value("kind") == "section")
                headings << row.value("title").toString();
        QCOMPARE(headings, QStringList{"Next Up"});
        f.backend->deleteSection(sec);
        QCOMPARE(f.store.task(id).sectionId, qint64(0));  // Tasks stay, outside any section.
        f.backend->undo();
        QCOMPARE(f.store.task(id).sectionId, sec);
    }

    void calendarCountsEachTaskOnceAndFiltersRanges() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->newTask();
        const qint64 id = f.backend->editingTaskId();
        f.backend->setTitle(id, "Span");
        f.backend->endEdit();
        const QDate first(2031, 3, 1);
        Task t = f.store.task(id);
        t.startDate = first.addDays(2);
        t.dueDate = first.addDays(5);
        f.store.begin("test");
        f.store.putTask(t);
        f.store.commit();
        const QVariantList days = f.backend->monthDays(2031, 3);
        QCOMPARE(days.at(2).toInt(), 0);  // Its start day doesn't count too.
        QCOMPARE(days.at(5).toInt(), 1);
        t.startDate = {};  // A future start would hide it as scheduled.
        f.store.begin("test");
        f.store.putTask(t);
        f.store.commit();
        f.backend->setDayRange("2031-03-10", "2031-03-04");  // Dragged backwards.
        QCOMPARE(f.backend->dayFilter(), QString("2031-03-04"));
        QCOMPARE(f.backend->dayFilterTo(), QString("2031-03-10"));
        bool listed = false;
        for (const auto &row : f.backend->tasks()->rows())
            listed = listed || row.value("taskId").toLongLong() == id;
        QVERIFY(listed);
    }

    void panesAreFilledAtStartup() {
        // A backend opened on an existing database shows it immediately,
        // without waiting for a change.
        QTemporaryDir dir;
        {
            Store store;
            QVERIFY(store.open(dir.filePath("t.db")));
            Backend first(&store);
            first.ensureDefaults();
            first.selectSource("list:" + QString::number(store.lists().keys().first()));
            first.newTask();
            first.setTitle(first.editingTaskId(), "Already here");
            first.endEdit();
            first.selectSource("all");
        }
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        Backend backend(&store);
        bool hasList = false, hasTask = false;
        for (const auto &row : backend.sidebar()->rows())
            hasList = hasList || row.value("kind") == "list";
        for (const auto &row : backend.tasks()->rows())
            hasTask = hasTask || row.value("title") == "Already here";
        QVERIFY(hasList);
        QVERIFY(hasTask);
    }
};

REGISTER_TEST(BackendTest)
#include "test_backend.moc"
