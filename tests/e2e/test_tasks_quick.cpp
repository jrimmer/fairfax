// End-to-end task tests, part 2: Quick Entry with every field set by its own
// control (list, type, priority and star, tags, dates and times, repeat,
// alerts, duration, location, notes), cancelling, and the project and
// checklist types. "Quick Entry" is a window of its own; its menus are popup
// windows, which the harness finds like any other.

#include "taskkit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::tk;

namespace {

// Types into Quick Entry's title; the window must be the active one.
bool titleIs(QQuickWindow *quick, const QString &text) {
    if (!waitItem("quickTitle")) return false;
    click(findItem("quickTitle"));
    if (!waitFor([&] { return prop("quickTitle", "activeFocus").toBool(); })) return false;
    typeLine(quick, text);
    return prop("quickTitle", "text").toString() == text;
}

// Saves with the Save button and waits for the window to go away.
bool saveQuick() {
    // A date field's suggestion list may still be open, and a click outside a
    // popup first closes it, so a second click can be needed.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!clickNamed("quickSave")) return false;
        if (waitFor([] { return !windowTitled("Quick Entry"); }, 1000)) return true;
    }
    QStringList seen;
    for (QQuickWindow *w : quickWindows()) seen << QString("%1 \"%2\"").arg(className(w), w->title());
    qWarning().noquote() << "Quick Entry did not close; windows:" << seen.join(" | ");
    return false;
}

bool pickFromQuickMenu(const QString &button, const QString &entry) {
    return clickNamed(button) && clickMenu(entry);
}

}  // namespace

class E2ETasksQuick : public QObject {
    Q_OBJECT

private slots:
    // "Every field of Quick Entry... list, type, priority, star" (quick-entry.md):
    // the list badge, the type button and the star menu each set their field,
    // and the saved task carries them.
    void listTypePriorityAndStar() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QVERIFY(app.openSource("list:" + QString::number(app.ids.work)));
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Quarterly review"));
        // "Set the fallback under Settings > Tasks > Default list": the first list.
        QCOMPARE(prop("quickList", "text").toString(), QString("Home"));
        QVERIFY(pickFromQuickMenu("quickList", "Work"));
        QTRY_COMPARE_WITH_TIMEOUT(prop("quickList", "text").toString(), QString("Work"), 2000);
        QVERIFY(pickFromQuickMenu("quickType", "Project"));
        QVERIFY(waitFor([&] { return findText("Project") != nullptr && prop("quickTitle", "placeholderText").toString() == "New Project"; }));
        QVERIFY(pickFromQuickMenu("quickPriority", "High"));
        QVERIFY(pickFromQuickMenu("quickPriority", "Star"));
        QVERIFY(saveQuick());

        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Quarterly review").id != 0, 3000);
        const Task t = app.taskTitled("Quarterly review");
        QCOMPARE(t.listId, app.ids.work);
        QCOMPARE(t.type, int(TaskType::Project));
        QCOMPARE(t.priority, int(Priority::High));
        QVERIFY(t.starred);

        // Seen in the Work list as a project with a star.
        QVERIFY(app.openSource("list:" + QString::number(app.ids.work)));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, t.id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowStar", t.id), 3000);

        // The same controls undo themselves: set, then cleared before saving.
        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QVERIFY(titleIs(again, "Plain note"));
        QVERIFY(pickFromQuickMenu("quickType", "Checklist"));
        QVERIFY(pickFromQuickMenu("quickType", "Task"));
        QVERIFY(pickFromQuickMenu("quickPriority", "Medium"));
        QVERIFY(pickFromQuickMenu("quickPriority", "None"));
        QVERIFY(pickFromQuickMenu("quickPriority", "Star"));
        QVERIFY(pickFromQuickMenu("quickPriority", "Star"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Plain note").id != 0, 3000);
        const Task plain = app.taskTitled("Plain note");
        QCOMPARE(plain.type, int(TaskType::Task));
        QCOMPARE(plain.priority, int(Priority::None));
        QVERIFY(!plain.starred);
        QCOMPARE(plain.listId, app.ids.home);
    }

    // Type menu: Checklist makes a checklist (square badge in the row).
    void checklistType() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Camping gear"));
        QVERIFY(pickFromQuickMenu("quickType", "Checklist"));
        QVERIFY(waitFor([&] { return prop("quickTitle", "placeholderText").toString() == "New Checklist"; }));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Camping gear").id != 0, 3000);
        QCOMPARE(app.taskTitled("Camping gear").type, int(TaskType::Checklist));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, app.taskTitled("Camping gear").id), 3000);
    }

    // "Tags: Return or Tab (or a comma) turns what you typed into a tag; Backspace
    // on an empty field removes the last" (task-editor.md), and autocomplete from
    // existing tags. The chip's x removes one.
    void tagsTypedAutocompletedAndRemoved() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Tagged thing"));
        click(findItem("quickTagInput"));
        QVERIFY(waitFor([] { return prop("quickTagInput", "activeFocus").toBool(); }));
        typeLine(quick, "errands,");               // A comma turns it into a chip.
        QVERIFY(waitItem("quickTagChip:errands"));
        typeLine(quick, "gar");                    // Autocomplete offers the existing "garden".
        QVERIFY(waitItem("quickTagMatch:garden"));
        click(findItem("quickTagMatch:garden"));
        QVERIFY(waitItem("quickTagChip:garden"));
        typeLine(quick, "urgent");
        QTest::keyClick(quick, Qt::Key_Return);    // Return takes what was typed.
        QVERIFY(waitItem("quickTagChip:urgent"));
        // Backspace on the empty field removes the last chip.
        QTest::keyClick(quick, Qt::Key_Backspace);
        QVERIFY(waitFor([] { return !findItem("quickTagChip:urgent"); }));
        QVERIFY(findItem("quickTagChip:garden") && findItem("quickTagChip:errands"));
        // The chip's cross removes a chosen one.
        QVERIFY(clickUntilGone("quickTagRemove:errands", "quickTagChip:errands"));
        QVERIFY(saveQuick());

        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Tagged thing").id != 0, 3000);
        const Task t = app.taskTitled("Tagged thing");
        QCOMPARE(tagNames(app, t), QStringList{"garden"});
        QCOMPARE(int(app.store().tags().size()), 1);  // The seeded "garden" was reused, and the removed chips never became tags.

        // And with every chip removed, the task carries no tag at all.
        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QVERIFY(titleIs(again, "Untagged thing"));
        click(findItem("quickTagInput"));
        typeLine(again, "temp,");
        QVERIFY(waitItem("quickTagChip:temp"));
        QVERIFY(clickUntilGone("quickTagRemove:temp", "quickTagChip:temp"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Untagged thing").id != 0, 3000);
        QVERIFY(app.taskTitled("Untagged thing").tagIds.isEmpty());
    }

    // Start and due, typed ("tom", "fri 3pm") with the suggestions list, picked
    // from the calendar, with times of their own, and cleared with the field's
    // clear button.
    void datesAndTimes() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        const QDate today = QDate::currentDate();
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Dated thing"));
        QVERIFY(typeDate("quickDue", "tom"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("quickDue", "value").toString().contains(QString::number(today.addDays(1).day())), 3000);
        QVERIFY(typeDate("quickDueTime", "3pm"));
        QVERIFY(typeDate("quickStart", "in 2w"));
        QVERIFY(typeDate("quickStartTime", "9am"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Dated thing").id != 0, 3000);
        Task t = app.taskTitled("Dated thing");
        QCOMPARE(t.dueDate, today.addDays(1));
        QCOMPARE(t.dueTime, 15 * 60);
        QCOMPARE(t.startDate, today.addDays(14));
        QCOMPARE(t.startTime, 9 * 60);

        // Weekday plus time in one entry, and the calendar picker.
        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QVERIFY(titleIs(again, "Friday thing"));
        QVERIFY(typeDate("quickDue", "fri 3pm"));
        click(findItem("quickTitle"));   // A field being typed in hides its clear button.
        QVERIFY(waitItem("quickDueClear"));
        const QDate day = today.addDays(10);
        QVERIFY(pickDay("quickStart", day));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Friday thing").id != 0, 3000);
        t = app.taskTitled("Friday thing");
        QCOMPARE(t.dueDate, nextWeekday(5));
        QCOMPARE(t.dueTime, 15 * 60);
        QCOMPARE(t.startDate, day);

        // Set, then cleared with the clear buttons: nothing is saved.
        QQuickWindow *third = openQuickEntry(app);
        QVERIFY(third);
        QVERIFY(titleIs(third, "Cleared dates"));
        QVERIFY(typeDate("quickDue", "tom"));
        QVERIFY(typeDate("quickDueTime", "3pm"));
        QVERIFY(typeDate("quickStart", "in 3d"));
        click(findItem("quickTitle"));
        QVERIFY(waitItem("quickDueClear"));
        QVERIFY(clickUntilGone("quickDueTimeClear", "quickDueTimeClear"));
        QVERIFY(clickUntilGone("quickDueClear", "quickDueClear"));
        QVERIFY(clickUntilGone("quickStartClear", "quickStartClear"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Cleared dates").id != 0, 3000);
        t = app.taskTitled("Cleared dates");
        QVERIFY(!t.dueDate.isValid());
        QCOMPARE(t.dueTime, -1);
        QVERIFY(!t.startDate.isValid());
    }

    // Repeat: a preset from the menu, "Never" to take it off again, and the
    // custom editor (every N weeks on chosen weekdays), then "After Completion".
    void repeatPresetsAndCustom() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Weekly thing"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "Weekly"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Weekly thing").id != 0, 3000);
        Task t = app.taskTitled("Weekly thing");
        QCOMPARE(t.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(t.recurrence.interval, 1);

        // Preset then Never: no rule is saved.
        QQuickWindow *second = openQuickEntry(app);
        QVERIFY(second);
        QVERIFY(titleIs(second, "Not repeating"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "Daily"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "Never"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Not repeating").id != 0, 3000);
        QVERIFY(!app.taskTitled("Not repeating").recurrence.isSet());

        // Custom: every 3 weeks on Monday, Wednesday and Friday, after completion.
        QQuickWindow *third = openQuickEntry(app);
        QVERIFY(third);
        QVERIFY(titleIs(third, "Custom thing"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "Custom…"));
        QVERIFY(waitItem("quickNumber"));
        QVERIFY(waitItem("quickUnit:weeks"));
        click(findItem("quickNumber"));
        QTest::keyClick(third, Qt::Key_A, Qt::ControlModifier);
        typeLine(third, "3");
        QVERIFY(waitItem("quickWeekday:0"));
        // The task's own weekday is preselected; end up with exactly Mon, Wed, Fri.
        for (int day = 0; day < 7; ++day) {
            QQuickItem *seg = findItem(QString("quickWeekday:%1").arg(day));
            QVERIFY(seg);
            const bool want = day == 0 || day == 2 || day == 4;
            if (seg->property("on").toBool() != want) click(seg);
            QTRY_COMPARE_WITH_TIMEOUT(findItem(QString("quickWeekday:%1").arg(day))->property("on").toBool(), want, 2000);
        }
        QVERIFY(clickNamed("quickCustomDone"));
        QVERIFY(waitFor([] { return !findItem("quickNumber"); }));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Custom thing").id != 0, 3000);
        t = app.taskTitled("Custom thing");
        QCOMPARE(t.recurrence.frequency, int(Repeat::Weekly));
        QCOMPARE(t.recurrence.interval, 3);
        QCOMPARE(t.recurrence.weekdays, 1 | 4 | 16);
        QCOMPARE(t.recurrence.from, int(Repeat::FromDue));
    }

    // "After completion" in the repeat menu is a separate switch, off again with
    // a second choice.
    void repeatAfterCompletionSwitch() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Chore"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "Monthly"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "After Completion"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Chore").id != 0, 3000);
        Task t = app.taskTitled("Chore");
        QCOMPARE(t.recurrence.frequency, int(Repeat::Monthly));
        QCOMPARE(t.recurrence.from, int(Repeat::FromCompletion));

        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QVERIFY(titleIs(again, "Chore two"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "Yearly"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "After Completion"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "After Completion"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Chore two").id != 0, 3000);
        t = app.taskTitled("Chore two");
        QCOMPARE(t.recurrence.frequency, int(Repeat::Yearly));
        QCOMPARE(t.recurrence.from, int(Repeat::FromDue));
    }

    // Alerts: presets from the alarm button (several at once), then Remove All.
    void alertsPresetsAndRemoveAll() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Alerted thing"));
        QVERIFY(typeDate("quickDue", "tom"));
        QVERIFY(typeDate("quickDueTime", "3pm"));
        QVERIFY(pickFromQuickMenu("quickAlarm", "15 minutes before"));
        QVERIFY(pickFromQuickMenu("quickAlarm", "1 day before"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Alerted thing").id != 0, 3000);
        QList<int> offsets;
        for (const Alarm &a : alarmsOf(app, app.taskTitled("Alerted thing").id)) offsets << a.offset;
        std::sort(offsets.begin(), offsets.end());
        QVERIFY2(offsets.contains(15) && offsets.contains(1440), qPrintable(QString::number(offsets.size())));

        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QVERIFY(titleIs(again, "Quiet thing"));
        QVERIFY(typeDate("quickDue", "tom"));
        QVERIFY(pickFromQuickMenu("quickAlarm", "5 minutes before"));
        QVERIFY(pickFromQuickMenu("quickAlarm", "At due time"));
        QVERIFY(pickFromQuickMenu("quickAlarm", "Remove All"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Quiet thing").id != 0, 3000);
        for (const Alarm &a : alarmsOf(app, app.taskTitled("Quiet thing").id))
            QVERIFY2(false, qPrintable(QString("alert left over: %1 minutes").arg(a.offset)));
    }

    // Duration: a preset, the custom editor (90 minutes, then 2 hours), and None.
    void durationPresetCustomAndNone() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Timed thing"));
        QVERIFY(pickFromQuickMenu("quickDuration", "45 min"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Timed thing").id != 0, 3000);
        QCOMPARE(app.taskTitled("Timed thing").duration, 45);

        QQuickWindow *second = openQuickEntry(app);
        QVERIFY(second);
        QVERIFY(titleIs(second, "Custom timed thing"));
        QVERIFY(pickFromQuickMenu("quickDuration", "Custom…"));
        QVERIFY(waitItem("quickNumber"));
        click(findItem("quickNumber"));
        QTest::keyClick(second, Qt::Key_A, Qt::ControlModifier);
        typeLine(second, "90");
        QVERIFY(clickNamed("quickCustomDone"));
        QVERIFY(waitFor([] { return !findItem("quickNumber"); }));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Custom timed thing").id != 0, 3000);
        QCOMPARE(app.taskTitled("Custom timed thing").duration, 90);

        QQuickWindow *third = openQuickEntry(app);
        QVERIFY(third);
        QVERIFY(titleIs(third, "Untimed thing"));
        QVERIFY(pickFromQuickMenu("quickDuration", "1 hour"));
        QVERIFY(pickFromQuickMenu("quickDuration", "None"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Untimed thing").id != 0, 3000);
        QCOMPARE(app.taskTitled("Untimed thing").duration, 0);
    }

    // Location: choose a saved place, or None.
    void locationChosenAndCleared() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        const qint64 office = app.backend().newLocation("Office", "1 High Street");
        QVERIFY(office > 0);
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Place thing"));
        QVERIFY(pickFromQuickMenu("quickLocation", "Office"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Place thing").id != 0, 3000);
        QCOMPARE(app.taskTitled("Place thing").locationIds, QList<qint64>{office});

        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QVERIFY(titleIs(again, "Placeless thing"));
        QVERIFY(pickFromQuickMenu("quickLocation", "Office"));
        QVERIFY(pickFromQuickMenu("quickLocation", "None"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Placeless thing").id != 0, 3000);
        QVERIFY(app.taskTitled("Placeless thing").locationIds.isEmpty());
    }

    // Notes: multi-line text typed in the notes box is saved with the task.
    void notesSaved() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        app.backend().setSetting("quickEntry.clipboard", false);
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Noted thing"));
        click(findItem("quickNotes"));
        QVERIFY(waitFor([] { return prop("quickNotes", "activeFocus").toBool(); }));
        typeLine(quick, "Bring the *blue* folder\nand the receipts");
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Noted thing").id != 0, 3000);
        QCOMPARE(app.taskTitled("Noted thing").notes, QString("Bring the *blue* folder\nand the receipts"));

        // A task saved without touching the notes has none.
        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QVERIFY(titleIs(again, "Bare thing"));
        QVERIFY(saveQuick());
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Bare thing").id != 0, 3000);
        QVERIFY(app.taskTitled("Bare thing").notes.isEmpty());
    }

    // Cancel discards everything typed and set, and Escape does the same; the
    // next Quick Entry starts blank.
    void cancelDiscards() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        const int before = app.store().tasks().size();
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Never saved"));
        QVERIFY(pickFromQuickMenu("quickPriority", "High"));
        QVERIFY(pickFromQuickMenu("quickRepeat", "Daily"));
        QVERIFY(clickNamed("quickCancel"));
        QVERIFY(waitFor([] { return !windowTitled("Quick Entry"); }));
        QCOMPARE(int(app.store().tasks().size()), before);
        QVERIFY(app.taskTitled("Never saved").id == 0);

        QQuickWindow *again = openQuickEntry(app);
        QVERIFY(again);
        QCOMPARE(prop("quickTitle", "text").toString(), QString());
        QVERIFY(titleIs(again, "Not saved either"));
        QTest::keyClick(again, Qt::Key_Escape);
        QVERIFY(waitFor([] { return !windowTitled("Quick Entry"); }));
        QCOMPARE(int(app.store().tasks().size()), before);

        // Save stays disabled while the title is empty.
        QQuickWindow *third = openQuickEntry(app);
        QVERIFY(third);
        QVERIFY(clickNamed("quickSave"));
        QVERIFY(windowTitled("Quick Entry") != nullptr);
        QCOMPARE(int(app.store().tasks().size()), before);
        QVERIFY(clickNamed("quickCancel"));
    }

    // Return in the title saves and closes; the task shows in the list.
    void returnSavesFromTheTitle() {
        App app;
        QVERIFY(app.ok);
        app.backend().setSetting("nl.quickEntry", false);  // Titles here are plain words, whatever they sound like.
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        QQuickWindow *quick = openQuickEntry(app);
        QVERIFY(quick);
        QVERIFY(titleIs(quick, "Typed and entered"));
        QTest::keyClick(quick, Qt::Key_Return);
        QVERIFY(waitFor([] { return !windowTitled("Quick Entry"); }));
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Typed and entered").id != 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, app.taskTitled("Typed and entered").id), 3000);
    }
};

REGISTER_TEST(E2ETasksQuick)

#include "test_tasks_quick.moc"
