// End-to-end tests: the keyboard. Every shortcut of Main.qml and of the task
// list's own key handler, plus the README's table, is a row of the table in
// misckit.h (or has a named exemption pointing at the test that covers it);
// the meta-test fails, listing them, for any that has neither. Navigation keys
// (arrows, Return, Escape, Space) and the ? overlay have tests of their own.

#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

#include "misckit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::mk;

namespace {

// Stands in for the desktop's URL handlers, so p and u open nothing real.
class UrlSink : public QObject {
    Q_OBJECT
public slots:
    void opened(const QUrl &url) { counters().urls << url.toString(); }
};

QString slurp(const QString &path) { return readFile(path); }

// Notation shared by Main.qml and the README: modifiers in the order Ctrl,
// Shift, Alt; arrows and Esc spelled as Qt does.
QString canonical(QString token) {
    token = token.trimmed();
    token.replace(QStringLiteral("↑"), "Up").replace(QStringLiteral("↓"), "Down").replace(QStringLiteral("←"), "Left")
        .replace(QStringLiteral("→"), "Right");
    if (token == "Esc") return "Escape";
    QStringList parts = token.split('+');
    // A trailing "+" is the key itself ("Ctrl++").
    if (token.endsWith("++")) parts = token.left(token.size() - 2).split('+') << "+";
    QString key = parts.takeLast();
    QStringList mods;
    for (const char *m : {"Ctrl", "Shift", "Alt"})
        if (parts.contains(m)) mods << m;
    if (mods.isEmpty() && key.size() == 1 && key[0].isLetter()) key = key.toLower();
    return (mods + QStringList{key}).join('+');
}

// The sequences Main.qml registers.
QSet<QString> mainShortcuts() {
    QSet<QString> out;
    static const QRegularExpression re("sequence:\\s*\"([^\"]+)\"");
    const QString text = slurp(QString(SRCDIR) + "/qml/Main.qml");
    for (auto it = re.globalMatch(text); it.hasNext();)
        out.insert(canonical(it.next().captured(1)));
    return out;
}

// The first column of the README's "Keyboard" table.
QSet<QString> readmeShortcuts() {
    QSet<QString> out;
    const QString text = slurp(QString(SRCDIR) + "/../README.md");
    const int from = text.indexOf("\n## Keyboard");
    const int to = text.indexOf("\n## ", from + 5);
    static const QRegularExpression span("`([^`]+)`");
    for (const QString &line : text.mid(from, to - from).split('\n')) {
        if (!line.startsWith("| `"))
            continue;
        const QString cell = line.section('|', 1, 1);
        for (auto it = span.globalMatch(cell); it.hasNext();) {
            const QString token = it.next().captured(1);
            // Ranges: Ctrl+1…Ctrl+5, 0–3.
            static const QRegularExpression range("^(.*?)(\\d)[…–](?:.*?)(\\d)$");
            const auto m = range.match(token);
            if (m.hasMatch()) {
                for (int d = m.captured(2).toInt(); d <= m.captured(3).toInt(); ++d)
                    out.insert(canonical(m.captured(1) + QString::number(d)));
            } else {
                out.insert(canonical(token));
            }
        }
    }
    return out;
}

// The keys TaskPane.qml's handleKey acts on, in the notation above.
QSet<QString> listKeys() {
    QSet<QString> out;
    const QString all = slurp(QString(SRCDIR) + "/qml/TaskPane.qml");
    const QString text = all.mid(all.indexOf("function handleKey"));
    static const QHash<QString, QString> named{
        {"Equal", "="}, {"Plus", "+"}, {"Minus", "-"}, {"BracketRight", "]"}, {"BraceRight", "Shift+]"}, {"BracketLeft", "["},
        {"BraceLeft", "Shift+["}, {"Period", "."}, {"Greater", "Shift+."}, {"Comma", ","}, {"Less", "Shift+,"}, {"Slash", "/"},
        {"Question", "?"}, {"Down", "Down"}, {"Up", "Up"}, {"Return", "Return"}, {"Escape", "Escape"}, {"Space", "Space"},
        {"Delete", "Delete"}, {"Backspace", "Backspace"}};
    auto bare = [&](const QString &name) -> QString {
        if (name == "Enter" || name == "Alt") return {};
        if (named.contains(name)) return named.value(name);
        return name.size() == 1 ? canonical(name) : QString("?" + name);
    };
    static const QRegularExpression ctrlKey("ctrl && k === Qt\\.Key_(\\w+)");
    static const QRegularExpression key("Qt\\.Key_(\\w+)");
    for (const QString &line : text.split('\n')) {
        if (line.contains("ctrl && shift")) {
            out.insert("Ctrl+Shift+[");
        } else if (const auto c = ctrlKey.match(line); c.hasMatch()) {
            out.insert(canonical("Ctrl+" + c.captured(1)));
        } else if (line.contains("case Qt.Key_") || line.contains("k === Qt.Key_")) {
            for (auto it = key.globalMatch(line); it.hasNext();) {
                const QString token = bare(it.next().captured(1));
                if (!token.isEmpty()) out.insert(token);
            }
        }
    }
    return out;
}

// Shortcuts earlier units' tests (or this unit's other tests) cover; the value
// names file:test, which the meta-test checks exists.
const QHash<QString, QString> &exemptions() {
    static const QHash<QString, QString> map{
        {"Ctrl+N", "test_tasks.cpp:ctrlNInListThenUndo"},
        {"Ctrl+Alt+N", "test_tasks_structure.cpp:projectCreatedFilledAndOpened"},
        {"Ctrl+Shift+Alt+N", "test_tasks_structure.cpp:checklistCreatedAndFilled"},
        {"Ctrl+Shift+N", "test_lists.cpp:newListShortcutJoinsOpenGroup"},
        {"Ctrl+0", "test_lists_nav.cpp:focusListsShowTheirTasks"},
        {"Ctrl+1", "test_lists_nav.cpp:focusListsShowTheirTasks"},
        {"Ctrl+2", "test_lists_nav.cpp:focusListsShowTheirTasks"},
        {"Ctrl+3", "test_lists_nav.cpp:focusListsShowTheirTasks"},
        {"Ctrl+4", "test_lists_nav.cpp:focusListsShowTheirTasks"},
        {"Ctrl+5", "test_lists_nav.cpp:focusListsShowTheirTasks"},
        {"Ctrl+Up", "test_lists_nav.cpp:upAndDownWalkTheSidebar"},
        {"Ctrl+Down", "test_lists_nav.cpp:upAndDownWalkTheSidebar"},
        {"Ctrl+Alt+Up", "test_tasks_structure.cpp:expandAndCollapse"},
        {"Ctrl+Alt+Down", "test_tasks_structure.cpp:expandAndCollapse"},
        {"Ctrl+Shift+Alt+Up", "test_lists_groups.cpp:collapseAndExpandGroups"},
        {"Ctrl+Shift+Alt+Down", "test_lists_groups.cpp:collapseAndExpandGroups"},
        {"Ctrl+.", "test_tasks_lifecycle.cpp:completeAndReopenFromKeyboard"},
        {"Ctrl+Alt+.", "test_tasks_lifecycle.cpp:completeAndReopenFromKeyboard"},
        {"Ctrl+D", "test_tasks_lifecycle.cpp:duplicate"},
        {"Ctrl+Z", "test_tasks_lifecycle.cpp:undoAndRedoWalkThroughEdits"},
        {"Ctrl+Shift+Z", "test_tasks_lifecycle.cpp:undoAndRedoWalkThroughEdits"},
        {"Ctrl+Alt+0", "test_tasks_editor.cpp:prioritySetByEditorShortcuts"},
        {"Ctrl+Alt+1", "test_tasks_editor.cpp:prioritySetByEditorShortcuts"},
        {"Ctrl+Alt+2", "test_tasks_editor.cpp:prioritySetByEditorShortcuts"},
        {"Ctrl+Alt+3", "test_tasks_editor.cpp:prioritySetByEditorShortcuts"},
        {"Ctrl+Alt+S", "test_tasks_editor.cpp:bareKeysSetPriorityAndStar"},
        {"Ctrl+Shift+Alt+1", "test_tasks_structure.cpp:convertTaskToProjectAndChecklistAndBack"},
        {"Ctrl+Shift+Alt+2", "test_tasks_structure.cpp:convertTaskToProjectAndChecklistAndBack"},
        {"Ctrl+Shift+Alt+3", "test_tasks_structure.cpp:convertTaskToProjectAndChecklistAndBack"},
        {"Ctrl+;", "test_tasks_repeat.cpp:repeatShortcutAndAltChips"},
        {"Ctrl+Alt+J", "test_lists_nav.cpp:projectViewAndBack"},
        {"Ctrl+Alt+P", "test_tasks_structure.cpp:projectFromSelection"},
        {"Ctrl+Shift+2", "test_scenarios.cpp:quickEntryNaturalLanguage"},
        {"Ctrl+,", "test_settings_system.cpp:importAndExportRoutes"},
        {"Ctrl+Shift+O", "test_lists_nav.cpp:quickOpen"},
        {"Ctrl+F", "test_scenarios.cpp:searchFiltersAndEscapeClears"},
        {"Ctrl+Shift+Alt+F", "test_settings_lists.cpp:soundsLikeSearchIsRemembered"},
        {"Ctrl+Alt+F", "unit 3: search scope belongs to the search and smart list tests"},
        {"f", "unit 3: the task find bar belongs to the search and smart list tests"},
        {"j", "test_lists_nav.cpp:listJumpBar"},
        {"Ctrl+O", "test_settings_lists.cpp:hideCompletedToggle"},
        {"Ctrl+=", "test_settings_lists.cpp:textSizeKeysMoveTheSettingsSlider"},
        {"t", "test_tasks_lifecycle.cpp:undoAndRedoWalkThroughEdits"},
        {"y", "test_tasks_lifecycle.cpp:rowsMoveBetweenSections"},
        {"Alt+[", "test_tasks_lifecycle.cpp:rowsMoveBetweenSections"},
        {"s", "test_tasks_editor.cpp:bareKeysSetPriorityAndStar"},
        {"0", "test_tasks_editor.cpp:bareKeysSetPriorityAndStar"},
        {"1", "test_tasks_editor.cpp:bareKeysSetPriorityAndStar"},
        {"2", "test_tasks_editor.cpp:bareKeysSetPriorityAndStar"},
        {"3", "test_tasks_editor.cpp:bareKeysSetPriorityAndStar"},
        {"Delete", "test_tasks_lifecycle.cpp:deleteWithUndo"},
        {"c", "test_tasks_structure.cpp:expandAndCollapse"},
        {"Down", "E2EMiscKeys:arrowsMoveTheSelection"},
        {"Up", "E2EMiscKeys:arrowsMoveTheSelection"},
        {"Return", "E2EMiscKeys:returnEditsAndEscapeFinishes"},
        {"Escape", "E2EMiscKeys:returnEditsAndEscapeFinishes"},
        {"Space", "E2EMiscKeys:spaceOpensQuickLook"},
        {"Ctrl+S", "E2EMiscKeys:ctrlSSyncsNow"},
        {"+", "E2EMiscKeys:plusRaisesPriorityLikeEquals"},
    };
    return map;
}

}  // namespace

class E2EMiscKeys : public QObject {
    Q_OBJECT
    UrlSink m_sink;

    static bool inputFocused(App &app) {
        QQuickItem *f = app.window->activeFocusItem();
        return f && f != findItem("taskList") && f->metaObject()->indexOfProperty("cursorPosition") >= 0;
    }
    static bool listFocused() {
        QQuickItem *l = findItem("taskList");
        return l && l->property("activeFocus").toBool();
    }

private slots:
    void initTestCase() {
        for (const char *scheme : {"http", "https", "tel", "sms", "mailto"})
            QDesktopServices::setUrlHandler(scheme, &m_sink, "opened");
    }
    void cleanupTestCase() {
        for (const char *scheme : {"http", "https", "tel", "sms", "mailto"})
            QDesktopServices::unsetUrlHandler(scheme);
    }

    // One row of the table: the key is pressed in the context it needs, its
    // effect is seen, and it is taken back.
    void shortcutTable_data() {
        QTest::addColumn<int>("index");
        for (int i = 0; i < keyRows().size(); ++i)
            QTest::newRow(qPrintable(QString("%1 (%2)").arg(keyRows().at(i).name, keyRows().at(i).context))) << i;
    }
    void shortcutTable() {
        QFETCH(int, index);
        const KeyRow row = keyRows().at(index);
        App app;
        QVERIFY(app.ok);
        QVERIFY2(row.arrange ? row.arrange(app) : homeWith(app, app.ids.plants), "arranging the context");
        QVERIFY(row.effect);

        app.press(row.key, row.mods);
        if (!row.typed.isEmpty()) {
            QTRY_VERIFY2_WITH_TIMEOUT(inputFocused(app), qPrintable("no field took the keyboard; focus is on " + app.focusDescription()), 5000);
            app.type(QString(row.typed).remove('\n'));   // "\n" alone is just Return.
            app.press(Qt::Key_Return);
        }
        QTRY_VERIFY_WITH_TIMEOUT(row.effect(app), 5000);

        switch (row.undo) {
        case KeyRow::None:
            break;
        case KeyRow::CtrlZ:
            for (int i = 0; i < row.undoSteps; ++i)
                app.press(Qt::Key_Z, Qt::ControlModifier);
            break;
        case KeyRow::Again:
            app.press(row.key, row.mods);
            break;
        case KeyRow::Escape:
            app.press(Qt::Key_Escape);
            break;
        case KeyRow::Opposite:
            app.press(row.backKey, row.backMods);
            break;
        }
        if (row.undo != KeyRow::None) {
            QVERIFY(row.reverted);
            QTRY_VERIFY_WITH_TIMEOUT(row.reverted(app), 5000);
        }
    }

    // The table, the exemptions and the README against Main.qml: every
    // shortcut either has a row or names the test that covers it.
    void everyShortcutHasATest() {
        QSet<QString> found = mainShortcuts() + readmeShortcuts() + listKeys();
        QVERIFY2(found.size() > 80, qPrintable(QString("only %1 shortcuts found; the parsers are broken").arg(found.size())));
        QSet<QString> rows;
        for (const KeyRow &r : keyRows())
            rows.insert(canonical(r.name));
        QStringList missing, stale;
        for (const QString &s : found)
            if (!rows.contains(s) && !exemptions().contains(s))
                missing << s;
        missing.sort();
        QVERIFY2(missing.isEmpty(), qPrintable("shortcuts with no table row and no exemption: " + missing.join(", ")));

        // An exemption must point at a test that exists, and at a shortcut that exists.
        for (auto it = exemptions().cbegin(); it != exemptions().cend(); ++it) {
            if (!found.contains(it.key())) stale << it.key() + " is not a shortcut";
            const QString &where = it.value();
            if (where.startsWith("unit 3")) continue;
            const QString file = where.section(':', 0, 0), test = where.section(':', 1);
            if (file == "E2EMiscKeys") {
                if (metaObject()->indexOfMethod(qPrintable(test + "()")) < 0) stale << it.key() + " -> missing " + where;
                continue;
            }
            const QString source = slurp(QString(SRCDIR) + "/../tests/e2e/" + file);
            if (!source.contains("void " + test + "()")) stale << it.key() + " -> missing " + where;
        }
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join("; ")));
        // And a row is not hiding behind an exemption of the same name.
        for (const QString &r : rows)
            if (exemptions().contains(r))
                stale << r + " has a row and an exemption";
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join("; ")));
    }

    // "Each window keeps ..." - Up and Down move the selection, Shift extends it.
    void arrowsMoveTheSelection() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QQuickItem *list = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((list = findItem("taskList")) != nullptr, 5000);
        list->forceActiveFocus();
        QTRY_VERIFY(listFocused());
        // Home in Due Date order: electricity (overdue), milk, plants, shelf.
        QCOMPARE(app.backend().selectionCount(), 0);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), app.ids.electricity, 5000);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), app.ids.buyMilk, 5000);
        app.press(Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), app.ids.electricity, 5000);
        app.press(Qt::Key_Up);   // The top stays the top.
        QCOMPARE(app.backend().currentTaskId(), app.ids.electricity);
        // Shift extends the selection; a plain arrow collapses it again.
        app.press(Qt::Key_Down, Qt::ShiftModifier);
        app.press(Qt::Key_Down, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 3, 5000);
        QVERIFY(app.row(app.ids.plants) != nullptr);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 1, 5000);
        QCOMPARE(app.backend().currentTaskId(), app.ids.shelf);
        app.press(Qt::Key_Down);   // The bottom stays the bottom.
        QCOMPARE(app.backend().currentTaskId(), app.ids.shelf);
        // Ctrl+A selects all and Escape clears.
        app.press(Qt::Key_A, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 4, 5000);
        app.press(Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 0, 5000);
    }

    // Home, End, Page Up and Page Down are not bound by the task list (nor
    // listed by the reference app): they leave the selection and the data alone.
    void homeEndAndPageKeysLeaveTheSelection() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(homeWith(app, app.ids.buyMilk));
        const Task before = app.task(app.ids.buyMilk);
        for (Qt::Key k : {Qt::Key_Home, Qt::Key_End, Qt::Key_PageUp, Qt::Key_PageDown}) {
            app.press(k);
            QTest::qWait(30);
            QCOMPARE(app.backend().currentTaskId(), app.ids.buyMilk);
            QCOMPARE(app.backend().selectionCount(), 1);
        }
        QCOMPARE(app.task(app.ids.buyMilk).title, before.title);
        QCOMPARE(app.task(app.ids.buyMilk).modified, before.modified);
        QVERIFY(!findItem("editorCard"));
    }

    // "Return: Edit selected task ... Esc: End editing (handled by the task
    // list, not a menu command)" (keyboard-shortcuts.md).
    void returnEditsAndEscapeFinishes() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(homeWith(app, app.ids.plants));
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("editorCard") != nullptr && app.backend().editingTaskId() == app.ids.plants, 5000);
        QQuickItem *title = nullptr;
        QTRY_VERIFY2_WITH_TIMEOUT((title = findItem("editorTitle")) != nullptr && title->property("activeFocus").toBool(),
                                  qPrintable(app.focusDescription()), 3000);
        QTest::keyClick(app.window, Qt::Key_X);   // Typed into the title, not a command.
        QTRY_VERIFY_WITH_TIMEOUT(title->property("text").toString().contains('x'), 5000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
        QCOMPARE(app.backend().currentTaskId(), app.ids.plants);   // Still selected.
        QTRY_VERIFY(listFocused());
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).title, QString("Water the plantsx"), 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).title, QString("Water the plants"), 5000);
        // A second Escape clears the selection.
        app.press(Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 0, 5000);
    }

    // While editing, bare keys type into the fields instead of acting on the task.
    void bareKeysTypeWhileEditing() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(homeWith(app, app.ids.plants));
        app.press(Qt::Key_Return);
        QQuickItem *title = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((title = findItem("editorTitle")) != nullptr && title->property("activeFocus").toBool(), 5000);
        app.type("t3sd");
        QTRY_VERIFY_WITH_TIMEOUT(title->property("text").toString().endsWith("t3sd"), 5000);
        const Task t = app.task(app.ids.plants);
        QCOMPARE(t.priority, 0);
        QVERIFY(!t.starred);
        QCOMPARE(t.dueDate, day(1));
        QVERIFY(commandBarClosed());
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorCard"), 5000);
    }

    // Space opens Quick Look on the selected task, Up and Down keep moving
    // through the list and the preview follows, Space closes it again.
    void spaceOpensQuickLook() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(homeWith(app, app.ids.buyMilk));
        QObject *look = findObject("quickLook");
        QVERIFY(look);
        QVERIFY(!look->property("visible").toBool());
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(look->property("visible").toBool(), 5000);
        auto *surface = popupSurface(look);
        auto shows = [&](const QString &text) {
            return findIn(surface, [&](QQuickItem *i) {
                       return i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0 && i->property("text").toString() == text;
                   }) != nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(shows("Buy milk") && shows("Home"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(look->property("contentItem").value<QQuickItem *>()->property("activeFocus").toBool(), 5000);
        app.press(Qt::Key_Down);
        QTRY_VERIFY_WITH_TIMEOUT(shows("Water the plants"), 5000);
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(!look->property("visible").toBool(), 5000);
        QTRY_VERIFY(listFocused());
        // Escape closes it as well, and Ctrl+W.
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(look->property("visible").toBool(), 5000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!look->property("visible").toBool(), 5000);
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(look->property("visible").toBool(), 5000);
        app.press(Qt::Key_W, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!look->property("visible").toBool(), 5000);
        // With nothing selected there is nothing to look at.
        app.press(Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 0, 5000);
        app.press(Qt::Key_Space);
        QTest::qWait(200);
        QVERIFY(!look->property("visible").toBool());
    }

    // "+" is the same key as "=" on many keyboards: raise the priority.
    void plusRaisesPriorityLikeEquals() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(homeWith(app, app.ids.plants));
        app.press(Qt::Key_Plus, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).priority, 1, 5000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).priority, 0, 5000);
    }

    // "?" opens the overlay of every shortcut; any key closes it.
    void questionMarkShowsTheShortcutOverlay() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(homeWith(app, app.ids.plants));
        QObject *help = findObject("shortcutOverlay");
        QVERIFY(help);
        QVERIFY(!help->property("visible").toBool());
        app.press(Qt::Key_Question, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(help->property("visible").toBool(), 5000);
        auto *surface = popupSurface(help);
        QStringList seen;
        findIn(surface, [&](QQuickItem *i) {
            if (i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0)
                seen << i->property("text").toString();
            return false;
        });
        for (const QString &want : {"TASK LIST (BARE KEYS)", "NEW & EDIT", "VIEW", "Quick Look", "Defer / Schedule…", "New task", "Settings"})
            QVERIFY2(seen.contains(want), qPrintable(want + " not in the overlay"));
        // Every sequence Main.qml registers is written somewhere on it.
        QString everything = seen.join('\n');
        everything.replace(QStringLiteral("↑"), "Up").replace(QStringLiteral("↓"), "Down");
        QVERIFY(everything.contains("Ctrl+N") && everything.contains("Ctrl+Shift+E") && everything.contains("F11"));
        app.press(Qt::Key_A);   // Any key closes it, and is not also acted on.
        QTRY_VERIFY_WITH_TIMEOUT(!help->property("visible").toBool(), 5000);
        QCOMPARE(app.backend().selectionCount(), 1);
        QTRY_VERIFY(listFocused());
        // It also opens and closes with the same key.
        app.press(Qt::Key_Question, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(help->property("visible").toBool(), 5000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!help->property("visible").toBool(), 5000);
    }

    // Ctrl+S is Sync now, in the list too (the star toggles with Ctrl+Alt+S): it
    // writes the tasks to the sync folder and leaves a starred task starred.
    void ctrlSSyncsNow() {
        QTemporaryDir remote;
        QVERIFY(remote.isValid());
        App app(seedWorld, QString(), true);
        QVERIFY(app.ok);
        QVERIFY(app.services().folderSync);
        auto folderHas = [&](const QString &text) {
            QDirIterator it(remote.path(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                if (readFile(it.next()).contains(text)) return true;
            return false;
        };
        emit app.services().filePicker.picked(remote.path(), "sync-folder");
        QTRY_VERIFY_WITH_TIMEOUT(app.services().folderSync->configured() && !app.services().folderSync->busy() && folderHas("Buy milk"), 15000);
        // With automatic sync off, a new task stays here until Ctrl+S.
        app.store().setSetting("sync.auto", false);
        QVERIFY(homeWith(app, app.ids.shelf));
        QVERIFY(app.task(app.ids.shelf).starred);
        Task t;
        newTaskByKeyboard(app, "Made before Ctrl+S", t);
        QVERIFY(homeWith(app, app.ids.shelf));
        QTest::qWait(500);
        QVERIFY(!folderHas("Made before Ctrl+S"));
        app.press(Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(folderHas("Made before Ctrl+S"), 15000);
        QVERIFY(app.task(app.ids.shelf).starred);   // Not an unstar.
        // Ctrl+Alt+S is what unstars it.
        app.press(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(app.ids.shelf).starred, 5000);
    }
};

REGISTER_TEST(E2EMiscKeys)

#include "test_misc_keys.moc"
