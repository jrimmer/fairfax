#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "markdown.h"
#include "store.h"
#include "testregistry.h"
#include "views.h"

// Drives the backend the way the keyboard does, on a temporary database.
class BackendTest : public QObject {
    Q_OBJECT

    struct Fixture {
        QTemporaryDir dir;
        Store store;
        std::unique_ptr<Backend> backend;
        Fixture() {
            store.open(dir.filePath("t.db"));
            // These tests are about the panes and actions, not the landing
            // view; keep them on the old behaviour (restore ui.source).
            store.setSetting("ui.defaultView", "last");
            backend = std::make_unique<Backend>(&store);
            backend->ensureDefaults();
        }
        // The lowest live list id, so the answer doesn't shift with hash order.
        qint64 firstList() const {
            qint64 first = 0;
            for (const auto &l : store.lists())
                if (!l.deleted && (!first || l.id < first)) first = l.id;
            return first;
        }
        Task only() const {
            for (const auto &t : store.tasks())
                if (!t.deleted) return t;
            return {};
        }
        Task titled(const QString &title) const {
            for (const auto &t : store.tasks())
                if (!t.deleted && t.title == title) return t;
            return {};
        }
        qint64 addTask(const QString &title, int type = 0) {
            backend->selectSource("list:" + QString::number(firstList()));
            backend->newTask(type);
            const qint64 id = backend->editingTaskId();
            backend->setTitle(id, title);
            backend->endEdit();
            return id;
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

    void pastedOutlineNestsUnderParents() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->pasteText("Trip\n  - Pack\n  - Book\n      Hotel\nErrands\n");
        const Task trip = f.store.task(f.titled("Trip").id);
        QCOMPARE(trip.type, int(TaskType::Project));
        QCOMPARE(f.titled("Pack").parentId, trip.id);
        QCOMPARE(f.titled("Book").parentId, trip.id);
        QCOMPARE(f.titled("Hotel").parentId, f.titled("Book").id);
        QCOMPARE(f.titled("Errands").parentId, qint64(0));
        QCOMPARE(f.titled("Errands").type, int(TaskType::Task));
    }

    void pastedTabIndentsAndBullets() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->pasteText("* Parent\n\t* Child one\n\t* Child two");
        QCOMPARE(f.titled("Child one").parentId, f.titled("Parent").id);
        QCOMPARE(f.titled("Child two").parentId, f.titled("Parent").id);
    }

    void pastingFlatLinesStaysFlat() {
        Fixture f;
        f.backend->selectSource("list:" + QString::number(f.firstList()));
        f.backend->pasteText("one\ntwo\nthree");
        for (const char *title : {"one", "two", "three"}) {
            QCOMPARE(f.titled(title).parentId, qint64(0));
            QCOMPARE(f.titled(title).type, int(TaskType::Task));
        }
    }

    void pastingOntoSelectedProjectNestsUnderIt() {
        Fixture f;
        const qint64 project = f.addTask("Plan", TaskType::Project);
        f.backend->selectTask(project);
        f.backend->pasteText("Step one\nStep two\n  Detail");
        QCOMPARE(f.titled("Step one").parentId, project);
        QCOMPARE(f.titled("Step two").parentId, project);
        QCOMPARE(f.titled("Detail").parentId, f.titled("Step two").id);
        // Step two stays a plain task with a sub-task; only top-level lines become projects.
        QCOMPARE(f.titled("Step two").type, int(TaskType::Task));
    }

    void pastingWhileViewingAProjectNestsUnderIt() {
        Fixture f;
        const qint64 project = f.addTask("Plan", TaskType::Project);
        f.backend->selectSource("project:" + QString::number(project));
        f.backend->pasteText("Only step");
        QCOMPARE(f.titled("Only step").parentId, project);
    }

    void cutDeletesAndPasteRestoresEverything() {
        Fixture f;
        const qint64 id = f.addTask("Cut me");
        f.backend->setDue(id, "2026-12-24");
        f.backend->addTag(id, "keep");
        const qint64 child = f.backend->addSubtask(id, "Child");
        QVERIFY(child);
        f.backend->selectTask(id);
        f.backend->cutSelection();
        QVERIFY(f.titled("Cut me").id == 0);
        f.backend->pasteTasks();
        const Task back = f.titled("Cut me");
        QVERIFY(back.id && back.id != id);
        QCOMPARE(back.dueDate, QDate(2026, 12, 24));
        QCOMPARE(back.tagIds.size(), 1);
        QCOMPARE(f.titled("Child").parentId, back.id);
    }

    void cutIsUndoable() {
        Fixture f;
        const qint64 id = f.addTask("Oops");
        f.backend->selectTask(id);
        f.backend->cutSelection();
        QVERIFY(f.titled("Oops").id == 0);
        f.backend->undo();
        QCOMPARE(f.titled("Oops").id, id);
    }

    void newProjectFromSelectionAdoptsTheTasks() {
        Fixture f;
        const qint64 a = f.addTask("A");
        const qint64 b = f.addTask("B");
        f.addTask("Untouched");
        f.backend->selectTask(a);
        f.backend->selectTask(b, 2);
        f.backend->newFromSelection(TaskType::Project);
        const qint64 parent = f.backend->editingTaskId();
        QVERIFY(parent);
        QCOMPARE(f.store.task(parent).type, int(TaskType::Project));
        QCOMPARE(f.store.task(a).parentId, parent);
        QCOMPARE(f.store.task(b).parentId, parent);
        QCOMPARE(f.titled("Untouched").parentId, qint64(0));
    }

    void newChecklistFromSelectionKeepsNestedTasksTogether() {
        Fixture f;
        const qint64 parent = f.addTask("Parent");
        const qint64 sub = f.backend->addSubtask(parent, "Sub");
        f.backend->selectTask(parent);
        f.backend->selectTask(sub, 2);
        f.backend->newFromSelection(TaskType::Checklist);
        const qint64 list = f.backend->editingTaskId();
        QCOMPARE(f.store.task(list).type, int(TaskType::Checklist));
        // The sub-task rides along under its own parent instead of leaving it.
        QCOMPARE(f.store.task(parent).parentId, list);
        QCOMPARE(f.store.task(sub).parentId, parent);
    }

    void gotoProjectRevealsTheParent() {
        Fixture f;
        const qint64 project = f.addTask("Plan", TaskType::Project);
        const qint64 step = f.backend->addSubtask(project, "Step");
        f.backend->selectTask(step);
        f.backend->gotoProject();
        QCOMPARE(f.backend->currentTaskId(), project);
    }

    void untagRemovesOnlyTheChosenTags() {
        Fixture f;
        const qint64 id = f.addTask("Tagged");
        f.backend->addTag(id, "red");
        f.backend->addTag(id, "blue");
        f.backend->selectTask(id);
        const auto choices = f.backend->selectionTags();
        QCOMPARE(choices.size(), 2);
        qint64 red = 0;
        for (const auto &c : choices)
            if (c.toMap().value("name") == "red") red = c.toMap().value("id").toLongLong();
        f.backend->untagSelection(QVariantList{red});
        const Task t = f.store.task(id);
        QCOMPARE(t.tagIds.size(), 1);
        QCOMPARE(f.store.tag(t.tagIds.first()).name, QString("blue"));
        f.backend->untagSelection();
        QVERIFY(f.store.task(id).tagIds.isEmpty());
    }

    void movingTagsSetsAManualOrder() {
        Fixture f;
        const qint64 a = f.backend->newTag("alpha");
        const qint64 b = f.backend->newTag("bravo");
        const qint64 c = f.backend->newTag("charlie");
        f.backend->moveTag(c, a, 0);
        QVERIFY(f.backend->tagsManual());
        QStringList names;
        for (const auto &t : ViewBuilder::orderedTags(f.store))
            names << t.name;
        QCOMPARE(names, (QStringList{"charlie", "alpha", "bravo"}));
        // Back to alphabetical when the manual order is switched off.
        f.backend->setTagsManual(false);
        names.clear();
        for (const auto &t : ViewBuilder::orderedTags(f.store))
            names << t.name;
        QCOMPARE(names, (QStringList{"alpha", "bravo", "charlie"}));
        Q_UNUSED(b);
    }

    void copyToListLeavesTheOriginal() {
        Fixture f;
        const qint64 id = f.addTask("Twice");
        const qint64 home = f.store.task(id).listId;
        const qint64 other = f.backend->newList("Other");
        f.backend->selectTask(id);
        f.backend->copySelectionToList(other);
        int count = 0;
        for (const auto &t : f.store.tasks())
            if (!t.deleted && t.title == "Twice") ++count;
        QCOMPARE(count, 2);
        QCOMPARE(f.store.task(id).listId, home);
    }

    void editFieldOpensTheEditorAndRemembersTheField() {
        Fixture f;
        const qint64 id = f.addTask("Edit me");
        f.backend->selectTask(id);
        f.backend->editField("notes");
        QCOMPARE(f.backend->editingTaskId(), id);
        QCOMPARE(f.backend->takePendingField(), QString("notes"));
        QCOMPARE(f.backend->takePendingField(), QString());
    }

    void editFieldOnAnOpenEditorSignals() {
        Fixture f;
        const qint64 id = f.addTask("Edit me");
        f.backend->beginEdit(id);
        QSignalSpy spy(f.backend.get(), &Backend::editFieldRequested);
        f.backend->editField("repeat");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(f.backend->takePendingField(), QString("repeat"));
    }

    void findInViewReachesIntoCollapsedProjects() {
        Fixture f;
        const qint64 project = f.addTask("Plan", TaskType::Project);
        f.backend->addSubtask(project, "Hidden step");
        f.backend->selectTask(project);
        f.backend->toggleCollapsed();
        const auto hits = f.backend->findInView("hidden");
        QCOMPARE(hits.size(), 1);
        const qint64 step = hits.first().toMap().value("id").toLongLong();
        f.backend->revealHere(step);
        QVERIFY(!f.store.task(project).collapsed);
        QCOMPARE(f.backend->currentTaskId(), step);
    }

    void jumpTargetsIncludeListsInCollapsedGroups() {
        Fixture f;
        const qint64 group = f.backend->newGroup("Folder");
        const qint64 list = f.backend->newList("Tucked Away");
        f.backend->moveList(list, 0, group);
        f.backend->toggleGroupCollapsed(group);
        bool found = false;
        for (const auto &t : f.backend->jumpTargets("tucked"))
            found = found || t.toMap().value("label") == "Tucked Away";
        QVERIFY(found);
    }

    void quickOpenRecentsListVisitedTasksAndLists() {
        Fixture f;
        const qint64 id = f.addTask("Recently seen");
        f.backend->selectTask(id);
        const auto recents = f.backend->recentItems();
        bool hasTask = false, hasList = false;
        for (const auto &r : recents) {
            hasTask = hasTask || r.toMap().value("label") == "Recently seen";
            hasList = hasList || r.toMap().value("sub") == "List";
        }
        QVERIFY(hasTask);
        QVERIFY(hasList);
    }

    void settingFlagsReadStoredTextAndBumpTheRevision() {
        Fixture f;
        QVERIFY(f.backend->settingFlag("appearance.showCalendar", true));
        const int before = f.backend->settingsRevision();
        f.backend->setSetting("appearance.showCalendar", false);
        QVERIFY(!f.backend->settingFlag("appearance.showCalendar", true));
        QVERIFY(f.backend->settingsRevision() > before);
        // After a restart settings come back as text.
        f.store.setSetting("appearance.calendarWeekends", "false");
        QVERIFY(!f.backend->settingFlag("appearance.calendarWeekends", true));
        f.store.setSetting("appearance.calendarWeekends", "true");
        QVERIFY(f.backend->settingFlag("appearance.calendarWeekends", false));
    }

    void panesAreFilledAtStartup() {
        // A backend opened on an existing database shows it immediately,
        // without waiting for a change.
        QTemporaryDir dir;
        {
            Store store;
            QVERIFY(store.open(dir.filePath("t.db")));
            store.setSetting("ui.defaultView", "last");
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
        store.setSetting("ui.defaultView", "last");
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
        store.setSetting("ui.defaultView", "last");
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

    // ---- repeating projects, rescheduling, durations, offsets ----

    void repeatingProjectResetsChildrenAndArchivesCheckedOnes() {
        Fixture f;
        const qint64 proj = f.addTask("Weekly review", TaskType::Project);
        f.backend->selectTask(proj);
        f.backend->setDueInDays(0);
        f.backend->setRepeatPreset(proj, 2);  // Weekly.
        auto addChild = [&](const QString &title, bool done) {
            Task c;
            c.id = f.store.nextId("tasks");
            c.listId = f.store.task(proj).listId;
            c.parentId = proj;
            c.title = title;
            c.completed = done;
            if (done) c.completedAt = QDateTime::currentDateTime().addDays(-1);
            f.store.begin("child");
            f.store.putTask(c);
            f.store.commit();
        };
        addChild("Inbox zero", true);
        addChild("Plan week", false);
        f.backend->toggleComplete(proj);

        QVERIFY(!f.store.task(proj).completed);
        QCOMPARE(f.store.task(proj).dueDate, QDate::currentDate().addDays(7));
        qint64 copy = 0;
        for (const auto &t : f.store.tasks())
            if (t.id != proj && t.type == TaskType::Project && t.completed) copy = t.id;
        QVERIFY(copy);
        int live = 0, kept = 0, keptDone = 0;
        for (const auto &t : f.store.tasks()) {
            if (t.deleted || t.parentId == 0) continue;
            if (t.parentId == proj) { ++live; QVERIFY(!t.completed && !t.completedAt.isValid()); }
            if (t.parentId == copy) { ++kept; keptDone += t.completed; }
        }
        QCOMPARE(live, 2);
        QCOMPARE(kept, 2);
        QCOMPARE(keptDone, 1);  // The copy keeps the children as they were checked.
    }

    void repeatingTaskCarriesNotesWithoutStamps() {
        Fixture f;
        const qint64 id = f.addTask("Backup");
        f.backend->selectTask(id);
        f.backend->setDueInDays(0);
        f.backend->setRepeatPreset(id, 1);
        Task t = f.store.task(id);
        t.notes = "Check disks\n2026-07-15 10:30\nRotate drive";
        f.store.begin("notes");
        f.store.putTask(t);
        f.store.commit();
        f.backend->toggleComplete(id);
        QCOMPARE(f.store.task(id).notes, QString("Check disks\nRotate drive"));
    }

    void rescheduleStartResetsCompletedProjectChildren() {
        Fixture f;
        const qint64 proj = f.addTask("Trip", TaskType::Project);
        Task c;
        c.id = f.store.nextId("tasks");
        c.listId = f.store.task(proj).listId;
        c.parentId = proj;
        c.title = "Pack";
        c.completed = true;
        c.completedAt = QDateTime::currentDateTime();
        f.store.begin("child");
        f.store.putTask(c);
        f.store.commit();
        f.backend->setStart(proj, QDate::currentDate().addDays(3).toString(Qt::ISODate), -2);
        QVERIFY(!f.store.task(c.id).completed);
        c.completed = true;
        f.store.begin("child");
        f.store.putTask(c);
        f.store.commit();
        f.backend->selectTask(proj);
        f.backend->clearStart();
        QVERIFY(!f.store.task(c.id).completed);
    }

    void selectionDurationDoesNotDoubleCount() {
        Fixture f;
        const qint64 proj = f.addTask("Project", TaskType::Project);
        auto child = [&](const QString &title, int minutes) {
            Task c;
            c.id = f.store.nextId("tasks");
            c.listId = f.store.task(proj).listId;
            c.parentId = proj;
            c.title = title;
            c.duration = minutes;
            f.store.begin("child");
            f.store.putTask(c);
            f.store.commit();
            return c.id;
        };
        const qint64 a = child("A", 30), b = child("B", 30);
        const qint64 other = f.addTask("Solo");
        Task solo = f.store.task(other);
        solo.duration = 60;
        f.store.begin("d");
        f.store.putTask(solo);
        f.store.commit();
        f.backend->selectTask(proj);
        f.backend->selectTask(a, 2);
        f.backend->selectTask(b, 2);
        f.backend->selectTask(other, 2);
        // Parent stands in for its children (60 min), plus the solo task.
        QVERIFY2(f.backend->selectionSummary().contains("2h"), qPrintable(f.backend->selectionSummary()));
    }

    void deferByOffsetMovesDueDates() {
        Fixture f;
        const qint64 id = f.addTask("Report");
        f.backend->selectTask(id);
        f.backend->setDueInDays(0);
        QVERIFY(f.backend->deferBy("+3d"));
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate().addDays(3));
        QVERIFY(f.backend->deferBy("+1w"));
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate().addDays(10));
        QVERIFY(f.backend->deferBy("-2d"));
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate().addDays(8));
        QVERIFY(!f.backend->deferBy("soon"));
        QCOMPARE(f.store.task(id).dueDate, QDate::currentDate().addDays(8));
    }

    void dueTimeChipsReadSetting() {
        Fixture f;
        QCOMPARE(f.backend->dueTimeChips().size(), 4);
        f.backend->setSetting("tasks.dueTimeChips", "8am, 5:30pm, nonsense");
        const auto chips = f.backend->dueTimeChips();
        QCOMPARE(chips.size(), 2);
        QCOMPARE(chips.at(0).toMap().value("minutes").toInt(), 8 * 60);
        QCOMPARE(chips.at(1).toMap().value("minutes").toInt(), 17 * 60 + 30);
    }

    void soundexSettingPersists() {
        Fixture f;
        QVERIFY(!f.backend->soundex());
        f.backend->setSoundex(true);
        f.store.setSetting("ui.defaultView", "last");
        Backend again(&f.store);
        QVERIFY(again.soundex());
    }

    // A fresh database opens on My Day, the new default view.
    void freshStoreOpensOnMyDay() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        Backend backend(&store);
        QCOMPARE(backend.sourceKey(), QString("myday"));
    }

    // Ctrl+N in My Day keeps the new task in the pane so its editor shows,
    // like Today does, instead of silently dropping it from every section.
    void newTaskInMyDayShowsItsEditor() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        store.setSetting("ui.defaultView", "myday");
        Backend backend(&store);
        backend.ensureDefaults();
        backend.selectSource("myday");
        backend.newTask();
        const qint64 id = backend.editingTaskId();
        QVERIFY(id > 0);
        bool editingRow = false;
        for (const auto &row : backend.tasks()->rows())
            editingRow = editingRow || (row.value("editing").toBool() && row.value("taskId").toLongLong() == id);
        QVERIFY(editingRow);
    }

    // With My Day off, the fresh default falls back to Today.
    void myDayOffOpensOnToday() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        store.setSetting("myday.enabled", false);
        Backend backend(&store);
        QCOMPARE(backend.sourceKey(), QString("today"));
    }

    // "last" restores the previous source, as the app did before this slice.
    void defaultViewLastRestoresTheSource() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        store.setSetting("ui.defaultView", "last");
        store.setSetting("ui.source", "starred");
        Backend backend(&store);
        QCOMPARE(backend.sourceKey(), QString("starred"));
    }

    // A chosen focus list opens on it.
    void defaultViewOpensAChosenFocusList() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        store.setSetting("ui.defaultView", "all");
        Backend backend(&store);
        QCOMPARE(backend.sourceKey(), QString("all"));
    }

    // A chosen list opens on it, and one that has been deleted or archived
    // falls back to Today.
    void defaultViewOpensAChosenList() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        Backend seed(&store);
        seed.ensureDefaults();
        const qint64 list = store.lists().keys().first();
        store.setSetting("ui.defaultView", QStringLiteral("list:%1").arg(list));
        QCOMPARE(Backend(&store).sourceKey(), QStringLiteral("list:%1").arg(list));
        TaskList l = store.list(list);
        l.deleted = true;
        store.begin("delete", false);
        store.putList(l);
        store.commit();
        QCOMPARE(Backend(&store).sourceKey(), QString("today"));

        // An archived list is not offered by the Settings combo, so the
        // resolver falls back the same way.
        Store archived;
        QVERIFY(archived.open(dir.filePath("a.db")));
        Backend ensure(&archived);
        ensure.ensureDefaults();
        const qint64 other = archived.lists().keys().first();
        TaskList al = archived.list(other);
        al.archived = true;
        archived.begin("archive", false);
        archived.putList(al);
        archived.commit();
        archived.setSetting("ui.defaultView", QStringLiteral("list:%1").arg(other));
        QCOMPARE(Backend(&archived).sourceKey(), QString("today"));
    }

    // Remembering My Day while it is off falls back to Today, so the feature
    // being off leaves the launch as it was before My Day existed.
    void lastMyDayWithFeatureOffFallsBackToToday() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("t.db")));
        store.setSetting("ui.defaultView", "last");
        store.setSetting("ui.source", "myday");
        store.setSetting("myday.enabled", false);
        Backend backend(&store);
        QCOMPARE(backend.sourceKey(), QString("today"));
        QVERIFY(!backend.sourceIsMyDay());
    }

    void exactMatchMeansWholeWords() {
        Fixture f;
        QVERIFY(f.backend->textMatches("Catalog update", "log"));
        QVERIFY(!f.backend->textMatches("Catalog update", "log", true));
        QVERIFY(f.backend->textMatches("Log in to bank", "log", true));
        f.addTask("Catalog update");
        f.addTask("Log in to bank");
        QCOMPARE(f.backend->findTasks("log", 10, false).size(), 2);
        QCOMPARE(f.backend->findTasks("log", 10, true).size(), 1);
    }

    void taskLinksResolveByTitleTiers() {
        // Titles avoid words like "weekly" that the title parser would read as a repeat.
        Fixture f;
        const qint64 home = f.addTask("Home base");
        QTest::qWait(5);
        const qint64 exact = f.addTask("Budget review");
        QTest::qWait(5);
        f.addTask("budget REVIEW");
        QTest::qWait(5);
        const qint64 prefix = f.addTask("Budget review notes");
        // Exact beats the case-insensitive and prefix matches.
        QCOMPARE(f.backend->resolveTaskLink("Budget review", home), exact);
        // Without an exact match, ignoring case comes next, then prefix.
        QVERIFY(f.backend->resolveTaskLink("BUDGET review", home) != prefix);
        QCOMPARE(f.backend->resolveTaskLink("budget review n", home), prefix);
        QCOMPARE(f.backend->resolveTaskLink("nothing like it", home), qint64(0));
        QCOMPARE(f.backend->resolveTaskLink("", home), qint64(0));
    }

    void taskLinkPrefersSameListThenNewest() {
        Fixture f;
        qint64 other = 0;
        for (const auto &l : f.store.lists())
            if (!l.deleted && l.kind != ListKind::Smart && l.id != f.firstList() && (!other || l.id < other)) other = l.id;
        QVERIFY(other);
        const qint64 older = f.addTask("Call plumber");
        QTest::qWait(5);
        const qint64 newer = f.addTask("Call plumber");
        QTest::qWait(5);
        const qint64 source = f.addTask("Source");
        // Same list, same title: the most recently changed wins.
        QCOMPARE(f.backend->resolveTaskLink("Call plumber", source), newer);
        // Move the newer one elsewhere: the one in the source's list wins even though it is older.
        Task moved = f.store.task(newer);
        moved.listId = other;
        f.store.begin("move");
        f.store.putTask(moved);
        f.store.commit();
        QCOMPARE(f.backend->resolveTaskLink("Call plumber", source), older);
    }

    void taskLinkNeverResolvesToItself() {
        Fixture f;
        const qint64 only = f.addTask("Lonely");
        QCOMPARE(f.backend->resolveTaskLink("Lonely", only), qint64(0));
        QVERIFY(!f.backend->openTaskLink("Lonely", only));
        const qint64 twin = f.addTask("Lonely");
        QCOMPARE(f.backend->resolveTaskLink("Lonely", only), twin);
        QCOMPARE(f.backend->resolveTaskLink("Lonely", twin), only);
    }

    void taskLinksIncludeCompletedAndSkipDeleted() {
        Fixture f;
        const qint64 done = f.addTask("Archived idea");
        Task t = f.store.task(done);
        t.completed = true;
        f.store.begin("done");
        f.store.putTask(t);
        f.store.commit();
        QCOMPARE(f.backend->resolveTaskLink("Archived idea"), done);
        t.deleted = true;
        f.store.begin("del");
        f.store.putTask(t);
        f.store.commit();
        QCOMPARE(f.backend->resolveTaskLink("Archived idea"), qint64(0));
    }

    void notesAreCappedAt500KB() {
        Fixture f;
        const qint64 id = f.addTask("Big notes");
        f.backend->setNotes(id, QString(Markdown::kNotesLimit + 5000, QLatin1Char('x')));
        QCOMPARE(f.store.task(id).notes.toUtf8().size(), Markdown::kNotesLimit);
        f.backend->setNotes(id, "small");
        QCOMPARE(f.store.task(id).notes, QString("small"));
    }

    void taskInfoCarriesAudioAndNextAlarm() {
        Fixture f;
        const qint64 id = f.addTask("Remind me");
        QVERIFY(f.backend->taskInfo(id).value("nextAlarm").toString().isEmpty());
        QVERIFY(f.backend->taskInfo(id).value("audio").toList().isEmpty());
        QCOMPARE(f.backend->taskAsText(id), QString("Remind me"));
        f.backend->setNotes(id, "line two");
        QCOMPARE(f.backend->taskAsText(id), QString("Remind me\nline two"));
    }

    void groupColorRecoloursItsLists() {
        Fixture f;
        const qint64 group = f.backend->newGroup("Home");
        const qint64 a = f.backend->newList("A");
        const qint64 b = f.backend->newList("B");
        const qint64 outside = f.backend->newList("C");
        f.backend->moveList(a, 0, group);
        f.backend->moveList(b, 0, group);
        const int custom = ListColor::custom(0x112233);
        f.backend->setGroupColor(group, custom);
        QCOMPARE(f.store.groups().value(group).color, custom);
        QCOMPARE(f.store.list(a).color, custom);
        QCOMPARE(f.store.list(b).color, custom);
        QVERIFY(f.store.list(outside).color != custom);
        f.backend->undo();
        QVERIFY(f.store.list(a).color != custom);
    }

    void smartListsReorderAmongThemselves() {
        Fixture f;
        const qint64 group = f.backend->newGroup("Home");
        const qint64 plain = f.backend->newList("Plain");
        f.backend->moveList(plain, 0, group);
        const qint64 a = f.backend->saveSmartList("A", "type: starred");
        const qint64 b = f.backend->saveSmartList("B", "type: hiprio");
        const qint64 c = f.backend->saveSmartList("C", "type: overdue");
        auto order = [&] {
            QList<qint64> ids{a, b, c};
            std::sort(ids.begin(), ids.end(), [&](qint64 x, qint64 y) { return f.store.list(x).order < f.store.list(y).order; });
            return ids;
        };
        QCOMPARE(order(), (QList<qint64>{a, b, c}));
        f.backend->moveList(c, a, 0);
        QCOMPARE(order(), (QList<qint64>{c, a, b}));
        // The end of the Smart Lists, and the regular lists are left alone.
        f.backend->moveList(c, 0, group);
        QCOMPARE(order(), (QList<qint64>{a, b, c}));
        QCOMPARE(f.store.list(c).groupId, qint64(0));
        QCOMPARE(f.store.list(plain).groupId, group);
        f.backend->undo();
        QCOMPARE(order(), (QList<qint64>{c, a, b}));
    }

    void expandAndCollapseAllGroups() {
        Fixture f;
        const qint64 g1 = f.backend->newGroup("One");
        const qint64 g2 = f.backend->newGroup("Two");
        f.backend->toggleGroupCollapsed(g1);
        f.backend->setAllGroupsCollapsed(true);
        QVERIFY(f.store.groups().value(g1).collapsed && f.store.groups().value(g2).collapsed);
        f.backend->setAllGroupsCollapsed(false);
        QVERIFY(!f.store.groups().value(g1).collapsed && !f.store.groups().value(g2).collapsed);
    }

    void hidingAListKeepsItOutOfTheSidebarUntilRevealed() {
        Fixture f;
        const qint64 id = f.backend->newList("Private");
        auto inSidebar = [&] {
            for (const auto &row : f.backend->sidebar()->rows())
                if (row.value("listId").toLongLong() == id && row.value("kind") == "list") return true;
            return false;
        };
        QVERIFY(inSidebar());
        QVERIFY(!f.backend->hasHiddenLists());
        f.backend->setListHidden(id, true);
        QVERIFY(f.backend->hasHiddenLists());
        QVERIFY(f.backend->listInfo(id).value("hidden").toBool());
        QVERIFY(!inSidebar());
        QVERIFY(f.backend->listChoices().size() < f.backend->hideableLists().size());
        f.backend->setHiddenRevealed(true);
        QVERIFY(inSidebar());
        f.backend->setHiddenRevealed(false);
        f.backend->setListHidden(id, false);
        QVERIFY(inSidebar());
    }

    void doneWarnsAtFiveHundredCompletedTasks() {
        Fixture f;
        auto warned = [&] {
            for (const auto &row : f.backend->sidebar()->rows())
                if (row.value("sourceKey") == "done") return row.value("warn").toBool();
            return false;
        };
        const qint64 list = f.firstList();
        f.store.begin("bulk");
        for (int i = 0; i < Backend::DoneWarningThreshold; ++i) {
            Task t;
            t.id = f.store.nextId("tasks");
            t.listId = list;
            t.title = QString("done %1").arg(i);
            t.completed = true;
            t.completedAt = QDateTime::currentDateTime().addDays(-3);
            f.store.putTask(t);
        }
        f.store.commit();
        QCOMPARE(f.backend->completedCount(), 500);
        QVERIFY(warned());
        // The prompt asks once.
        QSignalSpy spy(f.backend.get(), &Backend::doneWarningRequested);
        f.backend->selectSource("done");
        f.backend->selectSource("all");
        f.backend->selectSource("done");
        QCOMPARE(spy.count(), 1);
        // Automatic deletion makes the warning moot.
        f.backend->setSetting("tasks.autoDeleteDays", 30);
        QVERIFY(!warned());
    }

    void googleDomainFollowsLocale() {
        QCOMPARE(Backend::googleDomain(QLocale(QLocale::English, QLocale::UnitedStates)), "google.com");
        QCOMPARE(Backend::googleDomain(QLocale(QLocale::English, QLocale::UnitedKingdom)), "google.co.uk");
        QCOMPARE(Backend::googleDomain(QLocale(QLocale::German, QLocale::Germany)), "google.de");
        QCOMPARE(Backend::googleDomain(QLocale(QLocale::Japanese, QLocale::Japan)), "google.co.jp");
        QCOMPARE(Backend::googleDomain(QLocale::c()), "google.com");
    }
};

REGISTER_TEST(BackendTest)
#include "test_backend.moc"
