// End-to-end task tests, part 3: the inline editor's fields, each set through
// the editor's own controls and then cleared or reversed: title, dates (typed
// and picked), times, priority and star (menu, Ctrl+Alt shortcuts, bare keys),
// tags, duration, notes, action, location and the list badge.

#include "taskkit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::tk;

namespace {

}  // namespace

class E2ETasksEditor : public QObject {
    Q_OBJECT

    static QString homeKey(App &app) { return "list:" + QString::number(app.ids.home); }

private slots:
    // "Title: The headline of the task." (task-editor.md) Typing a new title
    // renames it; "An existing task never loses its name to an empty field".
    void titleEditedAndNeverEmptied() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(typeIn("editorTitle", "Buy oat milk"));
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).title, QString("Buy oat milk"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(rowText("taskTitle", id), QString("Buy oat milk"), 3000);

        // Clearing the field and leaving keeps the old name.
        QVERIFY(openEditor(app, id));
        QVERIFY(typeIn("editorTitle", ""));
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 3000);
        QCOMPARE(app.task(id).title, QString("Buy oat milk"));
        QCOMPARE(rowText("taskTitle", id), QString("Buy oat milk"));

        // And it is one undo step back to the first name.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).title, QString("Buy milk"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(rowText("taskTitle", id), QString("Buy milk"), 3000);
    }

    // "The Start and Due fields are typeable... tom, in 2w, fri 3pm": typed
    // dates apply on Return, move the task between sections of the list, and
    // the clear button takes them off again.
    void dueDateTypedThenCleared() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.electricity;  // Overdue by two days.
        const QDate today = QDate::currentDate();
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("overdue"), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(typeDate("editorDue", "tom"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 3000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(today.addDays(1))), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(typeDate("editorDue", "fri 3pm"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, nextWeekday(5), 3000);
        QCOMPARE(app.task(id).dueTime, 15 * 60);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(nextWeekday(5))), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(typeDate("editorDue", "in 2w"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(14), 3000);
        QCOMPARE(app.task(id).dueTime, 15 * 60);  // Typing only a date keeps the time.

        // Clear: the time first, then the date. (A field being typed in hides its
        // clear button, so the caret goes back to the title first.)
        click(findItem("editorTitle"));
        QVERIFY(waitItem("editorDueTimeClear"));
        click(findItem("editorDueTimeClear"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueTime, -1, 3000);
        QVERIFY(waitItem("editorDueClear"));
        click(findItem("editorDueClear"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).dueDate.isValid(), 3000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("nodue"), 3000);
        QVERIFY(!badge("rowDue", id));
    }

    // "Prefer picking visually? Clicking the date field also reveals a mini
    // calendar, with quick picks above it: Today, Tomorrow... None clears the
    // date." (task-editor.md)
    void dueDatePickedFromCalendar() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.shelf;  // No dates.
        const QDate today = QDate::currentDate();
        QVERIFY(openEditor(app, id));

        const QDate day = today.addDays(5);
        QVERIFY(pickDay("editorDue", day));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, day, 3000);
        QVERIFY(pickChip("editorDue", "Tomorrow"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today.addDays(1), 3000);
        QVERIFY(pickChip("editorDue", "Today"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueDate, today, 3000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("d:" + isoOf(today)), 3000);

        // "None" in the quick picks clears it again.
        QVERIFY(openEditor(app, id));
        QVERIFY(pickChip("editorDue", "None"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).dueDate.isValid(), 3000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(sectionOf(id), QString("nodue"), 3000);
    }

    // Start dates: a future start makes a task Scheduled, hidden from the list
    // until it begins ("Smart Scheduling"); the Scheduled view shows it, and
    // clearing the start brings it back.
    void startDateSchedulesTheTask() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        const QDate today = QDate::currentDate();
        QVERIFY(openEditor(app, id));
        // The time first (it starts today); the date last, because a start in
        // the future takes the task out of the list and the editor with it.
        QVERIFY(typeDate("editorStartTime", "9am"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startTime, 9 * 60, 3000);
        QCOMPARE(app.task(id).startDate, today);
        QVERIFY(typeDate("editorStart", "in 2w"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startDate, today.addDays(14), 3000);
        QCOMPARE(app.task(id).startTime, 9 * 60);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 3000);

        app.press(Qt::Key_4, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("scheduled"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowStart", id), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(waitItem("editorStartTimeClear"));
        click(findItem("editorStartTimeClear"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).startTime, -1, 3000);
        QVERIFY(waitItem("editorStartClear"));
        click(findItem("editorStartClear"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).startDate.isValid(), 3000);
        QVERIFY(closeEditor(app));
        QVERIFY(app.openSource(homeKey(app)));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 3000);
    }

    // Due times: typed into the time segment, and "No Time" via the clear button.
    void dueTimeSetAndCleared() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;  // Due today, no time.
        QVERIFY(openEditor(app, id));
        QVERIFY(typeDate("editorDueTime", "2:30pm"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueTime, 14 * 60 + 30, 3000);
        QCOMPARE(app.task(id).dueDate, QDate::currentDate());
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(rowText("rowDue", id).contains("2:30") || rowText("rowDue", id).contains("14:30"), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(waitItem("editorDueTimeClear"));
        click(findItem("editorDueTimeClear"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).dueTime, -1, 3000);
        QCOMPARE(app.task(id).dueDate, QDate::currentDate());
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!rowText("rowDue", id).contains(":"), 3000);
    }

    // Priority from the editor's star menu: every level, and Star on and off.
    // "Four levels: None, Low, Medium, High" (task-editor.md).
    void priorityAndStarFromEditorMenu() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        const QList<QPair<QString, int>> levels{{"High", 3}, {"Medium", 2}, {"Low", 1}, {"None", 0}};
        for (const auto &lv : levels) {
            QVERIFY(clickNamed("editorPriority"));
            QVERIFY(clickMenu(lv.first));
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, lv.second, 3000);
            QTRY_COMPARE_WITH_TIMEOUT(app.backend().editor().value("priority").toInt(), lv.second, 3000);
        }
        QVERIFY(clickNamed("editorPriority"));
        QVERIFY(clickMenu("Star"));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).starred, 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowStar", id), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorPriority"));
        QVERIFY(clickMenu("Star"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).starred, 3000);
        QVERIFY(clickNamed("editorPriority"));
        QVERIFY(clickMenu("Medium"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, 2, 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowPriority", id), 3000);
        QVERIFY(!badge("rowStar", id));
    }

    // "While you're editing a task, set priority without leaving the keyboard:
    // ⌥⌘0 None, ⌥⌘1 Low, ⌥⌘2 Medium, ⌥⌘3 High, and ⌥⌘S to toggle the star."
    // (task-editor.md; Ctrl stands in for Command.)
    void prioritySetByEditorShortcuts() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        const Qt::KeyboardModifiers m = Qt::ControlModifier | Qt::AltModifier;
        const QList<QPair<Qt::Key, int>> keys{{Qt::Key_3, 3}, {Qt::Key_2, 2}, {Qt::Key_1, 1}, {Qt::Key_0, 0}};
        for (const auto &k : keys) {
            app.press(k.first, m);
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, k.second, 3000);
        }
        app.press(Qt::Key_S, m);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).starred, 3000);
        app.press(Qt::Key_S, m);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).starred, 3000);
        // The title keeps its text through all of it.
        QCOMPARE(prop("editorTitle", "text").toString(), QString("Buy milk"));
    }

    // Bare keys on a selected row: "0–3 set the priority, S stars" (the
    // task-list shortcuts), then undone again with Ctrl+Z.
    void bareKeysSetPriorityAndStar() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.plants;
        QVERIFY(selectRow(app, id));
        const QList<QPair<Qt::Key, int>> keys{{Qt::Key_3, 3}, {Qt::Key_2, 2}, {Qt::Key_1, 1}};
        for (const auto &k : keys) {
            app.press(k.first);
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, k.second, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(badge("rowPriority", id), 3000);
        }
        app.press(Qt::Key_0);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).priority, 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowPriority", id), 3000);

        app.press(Qt::Key_S);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).starred, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowStar", id), 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).starred, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowStar", id), 3000);
        // Ctrl+Alt+S toggles it from the list as well.
        app.press(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).starred, 3000);
        app.press(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).starred, 3000);
    }

    // Tags: "a tokenized entry field with auto-completion... press Return or
    // click to accept a suggestion... Chips shown in the order you added them".
    // Also a comma, Backspace on the empty field, and the chip's cross.
    void tagsTypedAutocompletedAndRemoved() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(waitItem("editorTagsInput"));
        click(findItem("editorTagsInput"));
        QVERIFY(waitFor([] { return prop("editorTagsInput", "activeFocus").toBool(); }));
        app.type("errands");
        app.press(Qt::Key_Return);                       // Return makes a chip.
        QVERIFY(waitItem("editorTagsChip:errands"));
        QCOMPARE(tagNames(app, app.task(id)), QStringList{"errands"});
        app.type("gar");                                 // Autocomplete offers "garden".
        QVERIFY(waitItem("editorTagsMatch:garden"));
        click(findItem("editorTagsMatch:garden"));
        QVERIFY(waitItem("editorTagsChip:garden"));
        app.type("shop,");                               // A comma makes a chip.
        QVERIFY(waitItem("editorTagsChip:shop"));
        QCOMPARE(tagNames(app, app.task(id)), (QStringList{"errands", "garden", "shop"}));
        // Backspace on the empty field removes the last chip.
        app.press(Qt::Key_Backspace);
        QVERIFY(waitFor([] { return !findItem("editorTagsChip:shop"); }));
        QCOMPARE(tagNames(app, app.task(id)), (QStringList{"errands", "garden"}));
        // The cross removes a chosen chip.
        click(findItem("editorTagsRemove:errands"));
        QVERIFY(waitFor([] { return !findItem("editorTagsChip:errands"); }));
        QCOMPARE(tagNames(app, app.task(id)), QStringList{"garden"});
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowTagIcon", id), 3000);

        // Take the last one away: no tag icon any more.
        QVERIFY(openEditor(app, id));
        click(findItem("editorTagsRemove:garden"));
        QVERIFY(waitFor([] { return !findItem("editorTagsChip:garden"); }));
        QVERIFY(app.task(id).tagIds.isEmpty());
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowTagIcon", id), 3000);
    }

    // Duration presets and None ("The duration picker offers 30 mins, 1 hour,
    // 2 hours, None", task-editor.md), with the row's duration badge.
    void durationPresetsAndNone() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        const QList<QPair<QString, int>> presets{{"30 mins", 30}, {"1 hour", 60}, {"2 hours", 120}, {"15 mins", 15}, {"45 mins", 45}, {"4 hours", 240}, {"1 day", 1440}};
        for (const auto &p : presets) {
            QVERIFY(clickNamed("editorDuration"));
            QVERIFY(clickMenu(p.first));
            QTRY_COMPARE_WITH_TIMEOUT(app.task(id).duration, p.second, 3000);
        }
        QVERIFY(clickNamed("editorDuration"));
        QVERIFY(clickMenu("30 mins"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).duration, 30, 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowDuration", id), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorDuration"));
        QVERIFY(clickMenu("None"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).duration, 0, 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowDuration", id), 3000);
    }

    // Notes: multi-line text and Markdown persist exactly, and Return at the end
    // of a list item starts the next item.
    void notesMultiLineAndMarkdownPersist() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(waitItem("editorNotes"));
        click(findItem("editorNotes"), Qt::LeftButton, pointIn(findItem("editorNotes"), 0.5, 0.12));
        QQuickItem *area = nullptr;
        QVERIFY(waitFor([&] {
            area = app.window->activeFocusItem();
            return area && area->property("text").isValid() && area->objectName() != "editorTitle";
        }));
        app.type("# Shopping");
        app.press(Qt::Key_Return);
        app.type("- oat milk");
        app.press(Qt::Key_Return);        // Continues the list with "- ".
        app.type("soya milk");
        app.press(Qt::Key_Return);
        app.press(Qt::Key_Return);        // An empty item ends the list.
        app.type("**fresh** only");
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).notes.isEmpty(), 3000);
        const QString expected = app.task(id).notes;
        // The list continued by itself and an empty item ended it.
        QVERIFY2(expected.startsWith("# Shopping\n- oat milk\n- soya milk\n"), qPrintable(expected));
        QVERIFY2(expected.endsWith("**fresh** only"), qPrintable(expected));
        QVERIFY2(!expected.contains("- \n") && !expected.contains("- **"), qPrintable(expected));

        // Reopening shows the same text; clearing it empties the notes.
        QVERIFY(openEditor(app, id));
        QCOMPARE(app.backend().editor().value("notes").toString(), expected);
        click(findItem("editorNotes"), Qt::LeftButton, pointIn(findItem("editorNotes"), 0.5, 0.12));
        QVERIFY(waitFor([&] { QQuickItem *f = app.window->activeFocusItem(); return f && f->objectName() != "editorTitle" && f->property("text").toString() == expected; }));
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.press(Qt::Key_Delete);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString(), 3000);
    }

    // Action: "Pick an action type" (Call, Message, Mail, Visit, URL, Google)
    // with its value; the row shows the action icon; None removes it.
    void actionSetAndRemoved() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorAction"));
        QVERIFY(clickMenu("URL"));
        QVERIFY(waitItem("actionValue"));
        QVERIFY(waitFor([] { return prop("actionValue", "activeFocus").toBool(); }));
        app.type("https://example.com/milk");
        QVERIFY(waitFor([] { return findText("OK") != nullptr; }));
        click(findText("OK"));
        QVERIFY(waitFor([] { return findText("OK") == nullptr; }));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).actionType, int(ActionType::Url), 3000);
        QCOMPARE(app.task(id).actionValue, QString("https://example.com/milk"));
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowAction", id), 3000);

        // Change it to a phone call, then remove it.
        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorAction"));
        QVERIFY(clickMenu("Call"));
        QVERIFY(waitItem("actionValue"));
        QVERIFY(waitFor([] { return prop("actionValue", "activeFocus").toBool(); }));
        app.press(Qt::Key_A, Qt::ControlModifier);
        app.type("5551234");
        click(findText("OK"));
        QVERIFY(waitFor([] { return findText("OK") == nullptr; }));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).actionType, int(ActionType::Call), 3000);
        QCOMPARE(app.task(id).actionValue, QString("5551234"));
        QVERIFY(clickNamed("editorAction"));
        QVERIFY(clickMenu("None"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).actionType, int(ActionType::None), 3000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowAction", id), 3000);
    }

    // Location: a new place from the editor's pin button, attached with a chip
    // and a pin on the row; removed again from the same menu.
    void locationAddedAndRemoved() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorLocation"));
        QVERIFY(clickMenu("New Location…"));
        QVERIFY(waitItem("locationName"));
        QVERIFY(waitFor([] { return prop("locationName", "activeFocus").toBool(); }));
        app.type("Corner shop");
        click(findText("OK"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).locationIds.size(), 1, 3000);
        QVERIFY(waitItem("editorLocationChip:Corner shop"));
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowLocation", id), 3000);

        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorLocation"));
        QVERIFY(clickMenu("Corner shop"));   // Checked: choosing it again detaches it.
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).locationIds.size(), 0, 3000);
        QVERIFY(waitFor([] { return !findItem("editorLocationChip:Corner shop"); }));
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowLocation", id), 3000);
    }

    // "moving the task to another list from the ribbon list badge".
    void movedToAnotherListFromTheRibbon() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QCOMPARE(prop("editorList", "text").toString(), QString("Home"));
        QVERIFY(clickNamed("editorList"));
        QVERIFY(clickMenu("Work"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).listId, app.ids.work, 3000);
        // It has left Home, so the open editor goes with it.
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 3000);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.work)));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, id), 3000);   // ...now in Work.

        // And back again.
        QVERIFY(openEditor(app, id));
        QCOMPARE(prop("editorList", "text").toString(), QString("Work"));
        QVERIFY(clickNamed("editorList"));
        QVERIFY(clickMenu("Home"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).listId, app.ids.home, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, id), 3000);
    }
};

REGISTER_TEST(E2ETasksEditor)

#include "test_tasks_editor.moc"
