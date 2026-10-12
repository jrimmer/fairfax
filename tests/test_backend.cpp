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
        // Today's own due date stays; "tomorrow" was not read.
        QVERIFY(f.store.task(id).dueDate != QDate::currentDate().addDays(1));
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

    void defaultListSettingDrivesQuickEntry() {
        Fixture f;
        QList<qint64> ids;
        for (const auto &l : f.store.lists())
            if (!l.deleted) ids << l.id;
        std::sort(ids.begin(), ids.end());
        QVERIFY(ids.size() >= 2);
        f.backend->setSetting("tasks.defaultList", ids.last());
        QCOMPARE(f.backend->quickEntryListId(), ids.last());
        f.backend->setSetting("tasks.defaultList", 0);
        QCOMPARE(f.backend->quickEntryListId(), ids.first());
    }

    void oldQuickEntryListKeyMigrates() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        {
            Backend seed(&store);
            seed.ensureDefaults();
        }
        qint64 second = 0;
        for (const auto &l : store.lists())
            second = qMax(second, l.id);
        store.setSetting("tasks.quickEntryList", second);
        Backend backend(&store);
        QCOMPARE(store.setting("tasks.defaultList", 0).toLongLong(), second);
        QCOMPARE(backend.quickEntryListId(), second);
    }

    void lastUsedListFollowsQuickAdd() {
        Fixture f;
        QList<qint64> ids;
        for (const auto &l : f.store.lists())
            if (!l.deleted) ids << l.id;
        std::sort(ids.begin(), ids.end());
        f.backend->setSetting("tasks.defaultList", -1);
        f.backend->quickAdd("first", {}, 0, ids.last());
        QCOMPARE(f.backend->quickEntryListId(), ids.last());
        f.backend->quickAdd("second", {}, 0, ids.first());
        QCOMPARE(f.backend->quickEntryListId(), ids.first());
    }

    void alwaysUseDefaultListIgnoresViewedList() {
        Fixture f;
        QList<qint64> ids;
        for (const auto &l : f.store.lists())
            if (!l.deleted) ids << l.id;
        std::sort(ids.begin(), ids.end());
        f.backend->setSetting("tasks.defaultList", ids.first());
        f.backend->selectSource("list:" + QString::number(ids.last()));
        f.backend->newTask();
        QCOMPARE(f.only().listId, ids.last());
        f.backend->endEdit();

        f.backend->setSetting("tasks.alwaysDefaultList", true);
        f.backend->newTask();
        f.backend->setTitle(f.backend->editingTaskId(), "Elsewhere");
        QCOMPARE(f.store.task(f.backend->editingTaskId()).listId, ids.first());
    }

    void pastedTextSplitsIntoTitleAndNotes() {
        Fixture f;
        QVariantMap p = f.backend->splitPaste("\n  Buy milk \nfrom the shop\n\nand eggs\n");
        QCOMPARE(p.value("title").toString(), QString("Buy milk"));
        QCOMPARE(p.value("notes").toString(), QString("from the shop\n\nand eggs"));
        QVERIFY(p.value("url").toString().isEmpty());

        p = f.backend->splitPaste("https://example.com/a?b=1\nsome context");
        QCOMPARE(p.value("title").toString(), QString("https://example.com/a?b=1"));
        QCOMPARE(p.value("url").toString(), QString("https://example.com/a?b=1"));
        QCOMPARE(p.value("notes").toString(), QString("some context"));

        QVERIFY(f.backend->splitPaste(" \n ").value("title").toString().isEmpty());
    }

    void bareUrlTitleGetsUrlAction() {
        Fixture f;
        const qint64 id = f.backend->quickAddFields({{"title", "https://example.com/page"}});
        QCOMPARE(f.store.task(id).actionType, int(ActionType::Url));
        QCOMPARE(f.store.task(id).actionValue, QString("https://example.com/page"));

        const qint64 plain = f.backend->quickAddFields({{"title", "see https://example.com"}});
        QCOMPARE(f.store.task(plain).actionType, int(ActionType::None));

        const qint64 call = f.backend->quickAddFields({{"title", "Ring Sam"}, {"actionType", ActionType::Call}, {"actionValue", "555"}});
        QCOMPARE(f.store.task(call).actionType, int(ActionType::Call));
    }

    void stopDetectingKeepsTheTitleWhole() {
        Fixture f;
        f.backend->newTask();
        const qint64 id = f.backend->editingTaskId();
        f.backend->setDetecting(id, false);
        f.backend->setTitle(id, "Call mum tomorrow");
        f.backend->endEdit();
        QCOMPARE(f.store.task(id).title, QString("Call mum tomorrow"));
        // Today's own due date stays; "tomorrow" was not read.
        QVERIFY(f.store.task(id).dueDate != QDate::currentDate().addDays(1));
    }

    void exportListsHonoursScopeAndNeedsLists() {
        Fixture f;
        const qint64 list = f.firstList();
        const qint64 a = f.backend->quickAdd("Open", {}, 0, list);
        const qint64 b = f.backend->quickAdd("Closed", {}, 0, list);
        Task t = f.store.task(b);
        t.completed = true;
        f.store.begin("done");
        f.store.putTask(t);
        f.store.commit();
        Q_UNUSED(a);
        const QString path = f.dir.filePath("out.csv");
        QCOMPARE(f.backend->exportLists(path, "csv", {list}, 1), 1);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString csv = QString::fromUtf8(file.readAll());
        QVERIFY(csv.contains("Open"));
        QVERIFY(!csv.contains("Closed"));
        file.close();
        QCOMPARE(f.backend->exportLists(f.dir.filePath("none.csv"), "csv", {}, 0), 0);
        QVERIFY(!QFile::exists(f.dir.filePath("none.csv")));
    }

    void addToCalendarWritesEventFile() {
        Fixture f;
        const qint64 id = f.backend->quickAddFields({{"title", "Standup"}, {"dueIso", "2026-11-02"}, {"dueMinutes", 9 * 60}});
        const QString path = f.dir.filePath("event.ics");
        QVERIFY(f.backend->addToCalendar(id, path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("DTSTART:20261102T090000"));
        const qint64 undated = f.backend->quickAddFields({{"title", "Someday"}});
        QVERIFY(!f.backend->addToCalendar(undated, f.dir.filePath("none.ics")));
        QVERIFY(!QFile::exists(f.dir.filePath("none.ics")));
    }

    void deleteAllTasksKeepsListAndUndoes() {
        Fixture f;
        const qint64 list = f.firstList();
        f.backend->selectSource("list:" + QString::number(list));
        f.backend->newTask();
        f.backend->setTitle(f.only().id, "One");
        f.backend->endEdit();
        f.backend->deleteAllTasks(list);
        QCOMPARE(f.only().id, qint64(0));
        QVERIFY(!f.store.list(list).deleted);
        f.backend->undo();
        QCOMPARE(f.only().title, QString("One"));
    }

    void smartListCanBeLoadedAndUpdated() {
        Fixture f;
        const qint64 id = f.backend->saveSmartList("Hot", "type: hiprio", "next:3:d", 1, true);
        QVariantMap info = f.backend->smartListInfo(id);
        QCOMPARE(info.value("query").toString(), QString("type: hiprio"));
        QCOMPARE(info.value("range").toString(), QString("next:3:d"));
        QCOMPARE(info.value("completedMode").toInt(), 1);
        QVERIFY(info.value("includeScheduled").toBool());
        f.backend->updateSmartList(id, "", "starred", "nodue", 2, false);
        info = f.backend->smartListInfo(id);
        QCOMPARE(info.value("name").toString(), QString("Hot"));
        QCOMPARE(info.value("query").toString(), QString("starred"));
        QCOMPARE(info.value("range").toString(), QString("nodue"));
        QCOMPARE(info.value("completedMode").toInt(), 2);
        QVERIFY(!info.value("includeScheduled").toBool());
    }

    void listCountsFollowTheSettings() {
        Fixture f;
        f.backend->selectSource("today");
        f.backend->newTask();
        f.backend->setTitle(f.only().id, "Late");
        f.backend->endEdit();
        Task t = f.only();
        t.dueDate = QDate::currentDate().addDays(-2);
        f.store.begin("late");
        f.store.putTask(t);
        f.store.commit();
        auto counts = [&](const char *source) {
            f.backend->setSetting("test.refresh", f.backend->setting("test.refresh").value("value").toInt() + 1);
            for (const auto &row : f.backend->sidebar()->rows())
                if (row.value("sourceKey") == source)
                    return QPair<int, int>{row.value("overdue").toInt(), row.value("undone").toInt()};
            return QPair<int, int>{-1, -1};
        };
        QCOMPARE(counts("all"), (QPair<int, int>{1, 1}));
        f.backend->setSetting("lists.undoneIncludesOverdue", false);
        QCOMPARE(counts("all"), (QPair<int, int>{1, 0}));
        f.backend->setSetting("lists.counts", 1);
        QCOMPARE(counts("all"), (QPair<int, int>{1, 0}));
        f.backend->setSetting("lists.undoneIncludesOverdue", true);
        f.backend->setSetting("lists.counts", 0);
        QCOMPARE(counts("all"), (QPair<int, int>{0, 0}));
    }
};

REGISTER_TEST(BackendTest)
#include "test_backend.moc"
