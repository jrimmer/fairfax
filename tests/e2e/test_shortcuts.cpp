// End-to-end tests for Settings > Shortcuts: every command is listed, a key
// can be filtered, rebound, cleared and reset, conflicts are resolved with
// Swap, Replace or Cancel, a bad key is refused, a binding survives a
// relaunch, and the new key works in the list, the overlay, the editor and
// Quick Entry.

#include "misckit.h"
#include "settingskit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::mk;

namespace {

// The number of command rows drawn in the Shortcuts pane.
int rowCount() {
    int n = 0;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) { n += i->objectName().startsWith("shortcutRow:"); return false; });
    return n;
}

// Opens the pane and waits until every row is built, so a control found next
// stays where it is once scrolled into view.
void openShortcutsPane(App &app, QQuickWindow *&sw) {
    openSettings(app, sw);
    selectPane(sw, "Shortcuts");
    const int total = app.backend().shortcutCommands().size();
    QTRY_COMPARE_WITH_TIMEOUT(rowCount(), total, 5000);
    QTest::qWait(200);
}

// Starts recording on a command and types a key sequence; `key`/`mods` name
// the new binding, `typed` is a bare letter typed for a bare-key command.
void recordShortcut(QQuickWindow *sw, const QString &id, Qt::Key key, Qt::KeyboardModifiers mods) {
    QQuickItem *button = nullptr;
    control("shortcut:" + id, button);
    QVERIFY(button);
    click(button);
    QTRY_COMPARE_WITH_TIMEOUT(button->property("text").toString(), QString("Press a shortcut…"), 5000);
    QTest::keyClick(sw, key, mods);
}

// The selected text of the ? overlay, joined for a substring test.
QString overlayText() {
    QObject *help = findObject("shortcutOverlay");
    if (!help || !help->property("visible").toBool())
        return {};
    QStringList seen;
    findIn(popupSurface(help), [&](QQuickItem *i) {
        if (i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0)
            seen << i->property("text").toString();
        return false;
    });
    return seen.join('\n');
}

}  // namespace

class E2EShortcuts : public QObject {
    Q_OBJECT

private slots:
    // The pane lists every command, and the filter narrows it to matches.
    void paneListsEveryCommandAndFilters() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        const int total = s.app->backend().shortcutCommands().size();
        QTRY_COMPARE_WITH_TIMEOUT(rowCount(), total, 5000);
        // A filter matches a title.
        QQuickItem *filter = nullptr;
        control("shortcutFilter", filter);
        QVERIFY(filter);
        click(filter);
        typeInto(sw, "quit");
        QTRY_VERIFY_WITH_TIMEOUT(rowCount() > 0 && rowCount() < total, 5000);
        QVERIFY(findItem("shortcut:app.quit"));
        QVERIFY(!findItem("shortcut:task.new"));
        // Clearing shows every row again.
        click(filter);
        QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(sw, Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(rowCount(), total, 5000);
    }

    // New Task to Ctrl+Shift+Y: the old key stops working, the new one works,
    // the overlay shows it, and a relaunch keeps it.
    void rebindAppCommandPersists() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        recordShortcut(sw, "task.new", Qt::Key_Y, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.new").toString().contains("Ctrl+Shift+Y"), 5000);
        closeSettings(*s.app);

        // Ctrl+N no longer creates a task.
        Task made;
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        s.app->press(Qt::Key_N, Qt::ControlModifier);
        QTest::qWait(300);
        QVERIFY2(!findItem("editorCard"), "Ctrl+N still opened the editor");
        // Ctrl+Shift+Y does.
        s.app->press(Qt::Key_Y, Qt::ControlModifier | Qt::ShiftModifier);
        QQuickItem *title = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((title = findItem("editorTitle")) != nullptr, 5000);
        s.app->type("Made by the new key");
        s.app->press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(s.app->taskTitled("Made by the new key").id != 0, 5000);
        QVERIFY(closeEditor(*s.app));

        // The overlay shows the new key.
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        s.app->press(Qt::Key_Question, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(popupOpen("shortcutOverlay"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(overlayText().contains("Ctrl+Shift+Y"), 5000);
        s.app->press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!popupOpen("shortcutOverlay"), 5000);

        // A relaunch keeps the binding and its behaviour.
        QVERIFY(s.restart());
        QTRY_VERIFY_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.new").toString().contains("Ctrl+Shift+Y"), 5000);
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        s.app->press(Qt::Key_N, Qt::ControlModifier);
        QTest::qWait(300);
        QVERIFY(!findItem("editorCard"));
        s.app->press(Qt::Key_Y, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("editorTitle") != nullptr, 5000);
        QVERIFY(closeEditor(*s.app));
    }

    // A List bare key, "Due today", moves from `t` to `g` and acts in the list.
    void rebindListBareKey() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        QQuickItem *filter = nullptr;
        control("shortcutFilter", filter);
        QVERIFY(filter);
        click(filter);
        typeInto(sw, "due today");
        QQuickItem *button = nullptr;
        control("shortcut:list.dueToday", button);
        click(button);
        QTRY_COMPARE_WITH_TIMEOUT(button->property("text").toString(), QString("Press a shortcut…"), 5000);
        QTest::keyClick(sw, Qt::Key_G);
        QTRY_COMPARE_WITH_TIMEOUT(s.app->store().setting("shortcuts.list.dueToday").toString(), QString("G"), 5000);
        closeSettings(*s.app);

        // `t` no longer sets the due date, `g` does.
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        const QDate before = s.app->task(s.app->ids.plants).dueDate;
        s.app->press(Qt::Key_T);
        QTest::qWait(300);
        QCOMPARE(s.app->task(s.app->ids.plants).dueDate, before);
        s.app->press(Qt::Key_G);
        QTRY_COMPARE_WITH_TIMEOUT(s.app->task(s.app->ids.plants).dueDate, QDate::currentDate(), 5000);
    }

    // Conflict: bind New Task to Ctrl+D (Duplicate's key), then Swap, then
    // Replace, then Cancel.
    void conflictSwapReplaceCancel() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;

        auto copies = [&] {
            int n = 0;
            for (const Task &t : s.app->store().tasks())
                n += !t.deleted && t.title == "Water the plants";
            return n;
        };
        auto recordDuplicate = [&] {
            QQuickItem *button = nullptr;
            control("shortcut:task.new", button);
            click(button);
            QTRY_COMPARE_WITH_TIMEOUT(button->property("text").toString(), QString("Press a shortcut…"), 5000);
            QTest::keyClick(sw, Qt::Key_D, Qt::ControlModifier);
            QTRY_VERIFY_WITH_TIMEOUT(findItem("shortcutConflict:task.new") != nullptr, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(findItem("shortcutSwap") != nullptr, 5000);
        };

        openShortcutsPane(*s.app, sw);
        // Swap.
        recordDuplicate();
        { QQuickItem *b = nullptr; control("shortcutSwap", b); click(b); }
        QTRY_COMPARE_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.new").toString(), QString("Ctrl+D"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.duplicate").toString(), QString("Ctrl+N"), 5000);
        closeSettings(*s.app);
        // Duplicate now answers to Ctrl+N.
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        s.app->press(Qt::Key_N, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(copies(), 2, 5000);
        s.app->press(Qt::Key_Z, Qt::ControlModifier);   // Undo the duplicate.
        QTRY_COMPARE_WITH_TIMEOUT(copies(), 1, 5000);

        // Replace: Duplicate is unbound and Ctrl+D runs New Task. Start again
        // from the defaults.
        s.app->backend().resetAllShortcuts();
        openShortcutsPane(*s.app, sw);
        recordDuplicate();
        { QQuickItem *b = nullptr; control("shortcutReplace", b); click(b); }
        QTRY_COMPARE_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.new").toString(), QString("Ctrl+D"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.duplicate").toString(), QString(), 5000);
        closeSettings(*s.app);
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        s.app->press(Qt::Key_D, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("editorTitle") != nullptr, 5000);
        QVERIFY(closeEditor(*s.app));

        // Cancel: nothing changes.
        const QString kept = s.app->store().setting("shortcuts.task.new").toString();
        openShortcutsPane(*s.app, sw);
        QQuickItem *button = nullptr;
        control("shortcut:task.new", button);
        click(button);
        QTest::keyClick(sw, Qt::Key_Comma, Qt::ControlModifier);   // Settings' own key.
        QTRY_VERIFY_WITH_TIMEOUT(findItem("shortcutCancel") != nullptr, 5000);
        { QQuickItem *b = nullptr; control("shortcutCancel", b); click(b); }
        QTRY_VERIFY_WITH_TIMEOUT(findItem("shortcutConflict:task.new") == nullptr, 5000);
        QCOMPARE(s.app->store().setting("shortcuts.task.new").toString(), kept);
    }

    // A bare key for an App command is refused, and the binding is unchanged.
    void bareKeyRefusedForAppCommand() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        const QString before = s.app->store().setting("shortcuts.task.new").toString();
        recordShortcut(sw, "task.new", Qt::Key_G, Qt::NoModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("shortcutMessage:task.new") != nullptr, 5000);
        QVERIFY(findItem("shortcutMessage:task.new")->property("visible").toBool());
        QCOMPARE(s.app->store().setting("shortcuts.task.new").toString(), before);
    }

    // Clear: the old key does nothing and the overlay says "Not set".
    void clearCommand() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        QQuickItem *clear = nullptr;
        control("shortcutClear:task.complete", clear);
        click(clear);
        QTRY_COMPARE_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.complete").toString(), QString(), 5000);
        closeSettings(*s.app);
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        s.app->press(Qt::Key_Period, Qt::ControlModifier);
        QTest::qWait(300);
        QVERIFY(!s.app->task(s.app->ids.plants).completed);

        s.app->press(Qt::Key_Question, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(popupOpen("shortcutOverlay"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(overlayText().contains("Not set"), 5000);
        s.app->press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!popupOpen("shortcutOverlay"), 5000);
    }

    // Reset one command: the reset button goes and the default key works again.
    void resetOneCommand() {
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedWorld(st);
            return ids;
        };
        QVERIFY(s.start());
        s.app->backend().setShortcut("task.new", "Ctrl+Shift+Y");
        QTRY_COMPARE_WITH_TIMEOUT(s.app->store().setting("shortcuts.task.new").toString(), QString("Ctrl+Shift+Y"), 5000);
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        QQuickItem *reset = nullptr;
        control("shortcutReset:task.new", reset);
        click(reset);
        QTRY_VERIFY_WITH_TIMEOUT(!s.app->store().setting("shortcuts.task.new").isValid(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("shortcutReset:task.new") == nullptr, 5000);
        closeSettings(*s.app);
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        s.app->press(Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("editorTitle") != nullptr, 5000);
        QVERIFY(closeEditor(*s.app));
    }

    // Reset All with confirmation brings every default back, one per context.
    void resetAllRestoresDefaults() {
        Session s;
        QVERIFY(s.start());
        s.app->backend().setShortcut("task.new", "Ctrl+Shift+Y");
        s.app->backend().setShortcut("list.dueToday", "G");
        s.app->backend().setShortcut("quickEntry.hide", "Ctrl+Shift+W");
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        QQuickItem *resetAll = nullptr;
        control("shortcutResetAll", resetAll);
        click(resetAll);
        QObject *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = findObject("shortcutResetAllConfirm")) != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 5000);
        QQuickItem *ok = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((ok = findText("OK")) != nullptr, 5000);
        click(ok);
        QTRY_VERIFY_WITH_TIMEOUT(!s.app->store().setting("shortcuts.task.new").isValid(), 5000);
        QVERIFY(!s.app->store().setting("shortcuts.list.dueToday").isValid());
        QVERIFY(!s.app->store().setting("shortcuts.quickEntry.hide").isValid());
        // One per context is back at its default.
        QCOMPARE(s.app->backend().shortcutText("task.new"), QString("Ctrl+N"));
        QCOMPARE(s.app->backend().shortcutText("list.dueToday"), QString("T"));
        QCOMPARE(s.app->backend().shortcutText("quickEntry.hide"), QString("Ctrl+W"));
    }

    // While recording, Ctrl+N creates no task and becomes the recorded key;
    // Escape cancels a recording.
    void recordingSwallowsKeysAndEscapeCancels() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        recordShortcut(sw, "task.new", Qt::Key_N, Qt::ControlModifier);
        // No task was created, and the key was recorded (it conflicts with
        // itself, so it is not saved; here we only prove the swallow).
        QTest::qWait(200);
        QCOMPARE(s.app->backend().editingTaskId(), qint64(0));

        // Escape cancels without a change.
        const QString before = s.app->store().setting("shortcuts.task.new").toString();
        QQuickItem *button = nullptr;
        control("shortcut:task.new", button);
        click(button);
        QTRY_COMPARE_WITH_TIMEOUT(button->property("text").toString(), QString("Press a shortcut…"), 5000);
        QTest::keyClick(sw, Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(button->property("text").toString(), s.app->backend().shortcutText("task.new"), 5000);
        QCOMPARE(s.app->store().setting("shortcuts.task.new").toString(), before);
    }

    // A click elsewhere, another pane, or closing Settings ends a recording, so
    // the application shortcuts are never left switched off.
    void strayRecordingIsCancelled() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openShortcutsPane(*s.app, sw);
        auto start = [&] {
            QQuickItem *button = nullptr;
            control("shortcut:task.new", button);
            QVERIFY(button);
            click(button);
            QTRY_VERIFY_WITH_TIMEOUT(s.app->backend().recordingShortcut(), 5000);
        };
        // A click on another control.
        start();
        QQuickItem *filter = nullptr;
        control("shortcutFilter", filter);
        click(filter);
        QTRY_VERIFY_WITH_TIMEOUT(!s.app->backend().recordingShortcut(), 5000);
        // A click on nothing in particular.
        start();
        QTest::mouseClick(sw, Qt::LeftButton, Qt::NoModifier, QPoint(240, 30));   // The pane's heading.
        QTRY_VERIFY_WITH_TIMEOUT(!s.app->backend().recordingShortcut(), 5000);
        // Another pane.
        start();
        selectPane(sw, "General");
        QTRY_VERIFY_WITH_TIMEOUT(!s.app->backend().recordingShortcut(), 5000);
        // Closing the window.
        selectPane(sw, "Shortcuts");
        start();
        closeSettings(*s.app);
        QTRY_VERIFY_WITH_TIMEOUT(!s.app->backend().recordingShortcut(), 5000);
        QCOMPARE(s.app->store().setting("shortcuts.task.new").toString(), QString());
    }

    // The editor's keyboard menu shows a remapped priority key.
    void editorMenuShowsRemappedKey() {
        Session s;
        QVERIFY(s.start());
        s.app->backend().setShortcut("edit.priorityHigh", "Ctrl+Alt+9");
        QVERIFY(homeWith(*s.app, s.app->ids.plants));
        QVERIFY(openEditor(*s.app, s.app->ids.plants));
        QVERIFY(clickNamed("editorKeyboard"));
        QTRY_VERIFY_WITH_TIMEOUT(menuEntry("Priority High\tCtrl+Alt+9") != nullptr, 5000);
        QQuickItem *entry = menuEntry("Priority High\tCtrl+Alt+9");
        click(entry);
        QTRY_COMPARE_WITH_TIMEOUT(s.app->task(s.app->ids.plants).priority, 3, 5000);
        QVERIFY(closeEditor(*s.app));
    }

    // A Quick Entry command, save to Ctrl+Shift+Return, still saves.
    void rebindQuickEntryCommand() {
        Session s;
        QVERIFY(s.start());
        s.app->backend().setShortcut("quickEntry.save", "Ctrl+Shift+Return");
        QQuickWindow *quick = nullptr;
        openQuickEntry(*s.app, quick);
        QQuickItem *field = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((field = findItem("quickTitle")) != nullptr, 5000);
        click(field);
        typeInto(quick, "Saved with the new key");
        QTest::keyClick(quick, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(s.app->taskTitled("Saved with the new key").id != 0, 5000);
        // Saving without opening keeps Quick Entry up for the next task, empty.
        QTRY_VERIFY_WITH_TIMEOUT(findItem("quickTitle") && findItem("quickTitle")->property("text").toString().isEmpty(), 5000);
        // The old key no longer saves.
        typeInto(quick, "Not saved");
        QTest::keyClick(quick, Qt::Key_Return, Qt::ShiftModifier);
        QTest::qWait(300);
        QCOMPARE(s.app->taskTitled("Not saved").id, qint64(0));
    }
};

REGISTER_TEST(E2EShortcuts)

#include "test_shortcuts.moc"
