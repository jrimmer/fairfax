// End-to-end task tests, part 6: structure. Sub-tasks added in the editor
// (complete, rename, delete), projects and checklists (created, filled,
// opened as a view), converting between task, project and checklist, and
// expanding and collapsing.

#include "taskkit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::tk;

namespace {

// The tasks whose parent is `id`, by title.
QMap<QString, Task> childrenOf(App &app, qint64 id) {
    QMap<QString, Task> out;
    for (const Task &t : app.store().tasks())
        if (!t.deleted && t.parentId == id) out.insert(t.title, t);
    return out;
}

QString textInside(QQuickItem *item) {
    QString found;
    if (!item) return found;
    findIn(item, [&](QQuickItem *i) {
        if (found.isEmpty() && i != item && i->metaObject()->indexOfProperty("text") >= 0 && !i->property("text").toString().isEmpty())
            found = i->property("text").toString();
        return false;
    });
    return found;
}

// Types one title into the editor's "add a sub-task" field and presses Return.
bool addSub(App &app, const QString &title) {
    if (!typeIn("editorNewSubtask", title)) return false;
    app.press(Qt::Key_Return);
    return waitFor([&] {
        QQuickItem *f = findItem("editorNewSubtask");
        return f && f->property("text").toString().isEmpty();
    });
}

}  // namespace

class E2ETasksStructure : public QObject {
    Q_OBJECT

    static QString homeKey(App &app) { return "list:" + QString::number(app.ids.home); }

private slots:
    // "Sub-tasks... a quick inline checklist" in the editor: the new field adds
    // one per Return; each can be completed, renamed and deleted from the
    // editor. The parent's row gains a disclosure arrow and the sub-task rows.
    void subTasksAddedCompletedRenamedDeleted() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(!badge("rowDisclosure", id));
        QVERIFY(openEditor(app, id));
        QVERIFY(addSub(app, "Oat milk"));
        QVERIFY(addSub(app, "Soya milk"));
        QVERIFY(addSub(app, "Almond milk"));
        QTRY_COMPARE_WITH_TIMEOUT(childrenOf(app, id).size(), 3, 5000);
        QVERIFY(childrenOf(app, id).contains("Oat milk"));
        QCOMPARE(childrenOf(app, id).value("Soya milk").listId, app.ids.home);
        const qint64 oat = childrenOf(app, id).value("Oat milk").id;
        const qint64 soya = childrenOf(app, id).value("Soya milk").id;
        const qint64 almond = childrenOf(app, id).value("Almond milk").id;
        QVERIFY(waitItem(QString("editorSubtask:%1").arg(oat)));

        // Complete one with its checkbox.
        QVERIFY(clickNamed(QString("editorSubtaskCheck:%1").arg(oat)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(oat).completed, 5000);
        QVERIFY(clickNamed(QString("editorSubtaskCheck:%1").arg(oat)));       // ... and reopen it.
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(oat).completed, 5000);
        QVERIFY(clickNamed(QString("editorSubtaskCheck:%1").arg(oat)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(oat).completed, 5000);

        // Rename one.
        QVERIFY(typeIn(QString("editorSubtask:%1").arg(soya), "Lactose-free milk"));
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(soya).title, QString("Lactose-free milk"), 5000);

        // Delete one with its cross.
        QVERIFY(clickNamed(QString("editorSubtaskDelete:%1").arg(almond)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(almond).deleted, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(childrenOf(app, id).size(), 2, 5000);
        QVERIFY(waitFor([&] { return !findItem(QString("editorSubtask:%1").arg(almond)); }));

        // Back in the list: an arrow, and the two sub-task rows beneath.
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowDisclosure", id), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, oat) && rowVisible(app, soya), 5000);
        QVERIFY(rowGone(app, almond));
        QCOMPARE(rowText("taskTitle", soya), QString("Lactose-free milk"));
    }

    // Folding: the arrow, the C key and Ctrl+Alt+Up / Down collapse and expand
    // the sub-tasks ("Collapse All").
    void expandAndCollapse() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(addSub(app, "First"));
        QVERIFY(addSub(app, "Second"));
        QTRY_COMPARE_WITH_TIMEOUT(childrenOf(app, id).size(), 2, 5000);
        const qint64 first = childrenOf(app, id).value("First").id;
        const qint64 second = childrenOf(app, id).value("Second").id;
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, first) && rowVisible(app, second), 5000);

        QVERIFY(clickNamed(QString("rowDisclosure:%1").arg(id)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, first) && rowGone(app, second), 5000);
        QVERIFY(rowVisible(app, id));
        QVERIFY(clickNamed(QString("rowDisclosure:%1").arg(id)));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, first) && rowVisible(app, second), 5000);

        QVERIFY(selectRow(app, id));
        app.press(Qt::Key_C);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, first), 5000);
        app.press(Qt::Key_C);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, first), 5000);

        app.press(Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, first), 5000);
        app.press(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(id).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, first), 5000);
    }

    // "Projects: tasks inside a task" (projects-and-checklists.md). Ctrl+Alt+N
    // makes a project; tasks added to it appear under it with a progress
    // figure; its own view (Ctrl+Alt+J) holds just its tasks, and Ctrl+N there
    // adds another inside.
    void projectCreatedFilledAndOpened() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        app.press(Qt::Key_N, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        QCOMPARE(prop("editorTitle", "placeholderText").toString(), QString("New Project"));
        app.type("Website relaunch");
        QVERIFY(addSub(app, "Write copy"));
        QVERIFY(addSub(app, "Pick photos"));
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Website relaunch").id != 0, 5000);
        const Task project = app.taskTitled("Website relaunch");
        QCOMPARE(project.type, int(TaskType::Project));
        QCOMPARE(project.listId, app.ids.home);
        QTRY_COMPARE_WITH_TIMEOUT(childrenOf(app, project.id).size(), 2, 5000);
        const qint64 copy = childrenOf(app, project.id).value("Write copy").id;
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowProject", project.id), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(textInside(findItem(QString("rowProgress:%1").arg(project.id))), QString("0%"), 5000);

        // Completing a task moves the progress along.
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(copy)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(copy).completed, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(textInside(findItem(QString("rowProgress:%1").arg(project.id))), QString("50%"), 5000);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(copy)));
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(copy).completed, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(textInside(findItem(QString("rowProgress:%1").arg(project.id))), QString("0%"), 5000);

        // The project's own view, and a task made inside it.
        QVERIFY(selectRow(app, project.id));
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("project:%1").arg(project.id), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowVisible(app, copy), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rowGone(app, app.ids.buyMilk), 5000);
        app.press(Qt::Key_N, Qt::ControlModifier);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Launch party");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Launch party").id != 0, 5000);
        QCOMPARE(app.taskTitled("Launch party").parentId, project.id);
        QCOMPARE(childrenOf(app, project.id).size(), 3);

        // Ctrl+Alt+J again goes back to the list.
        app.press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), homeKey(app), 5000);
    }

    // Checklists (Ctrl+Shift+Alt+N): items, and a square checkbox instead of a
    // round one.
    void checklistCreatedAndFilled() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        app.press(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        QCOMPARE(prop("editorTitle", "placeholderText").toString(), QString("New Checklist"));
        app.type("Packing list");
        QVERIFY(addSub(app, "Passport"));
        QVERIFY(addSub(app, "Charger"));
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Packing list").id != 0, 5000);
        const Task list = app.taskTitled("Packing list");
        QCOMPARE(list.type, int(TaskType::Checklist));
        QTRY_COMPARE_WITH_TIMEOUT(childrenOf(app, list.id).size(), 2, 5000);
        const qint64 passport = childrenOf(app, list.id).value("Passport").id;
        QVERIFY(clickNamed(QString("editorSubtaskCheck:%1").arg(passport)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(passport).completed, 5000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowChecklist", list.id), 5000);
        // The badge counts the items still open.
        QTRY_COMPARE_WITH_TIMEOUT(textInside(findItem(QString("rowChecklist:%1").arg(list.id))), QString("1"), 5000);
        QVERIFY(clickNamed(QString("taskCheck:%1").arg(childrenOf(app, list.id).value("Charger").id)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(childrenOf(app, list.id).value("Charger").id).completed, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(textInside(findItem(QString("rowChecklist:%1").arg(list.id))).isEmpty()
                                      || textInside(findItem(QString("rowChecklist:%1").arg(list.id))) == "0", true, 5000);
    }

    // Converting: Ctrl+Shift+Alt+1/2/3 (and Convert in the context menu) turn a
    // task into a task, project or checklist and back.
    void convertTaskToProjectAndChecklistAndBack() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(selectRow(app, id));
        const Qt::KeyboardModifiers m = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;
        app.press(Qt::Key_2, m);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).type, int(TaskType::Project), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowProject", id), 5000);
        app.press(Qt::Key_3, m);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).type, int(TaskType::Checklist), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowChecklist", id), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowProject", id), 5000);
        app.press(Qt::Key_1, m);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).type, int(TaskType::Task), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge("rowChecklist", id) && !badge("rowProject", id), 5000);

        // The same from the context menu, with the sub-tasks kept through it.
        QVERIFY(openEditor(app, id));
        QVERIFY(addSub(app, "Inner"));
        QTRY_COMPARE_WITH_TIMEOUT(childrenOf(app, id).size(), 1, 5000);
        QVERIFY(closeEditor(app));
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Convert", "To Project"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).type, int(TaskType::Project), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowProject", id), 5000);
        QCOMPARE(childrenOf(app, id).size(), 1);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Convert", "To Checklist"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).type, int(TaskType::Checklist), 5000);
        QVERIFY(contextMenuOn(app, id));
        QVERIFY(clickSub("Convert", "To Task"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).type, int(TaskType::Task), 5000);
        QCOMPARE(childrenOf(app, id).size(), 1);
        // Each step is undoable.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).type, int(TaskType::Checklist), 5000);
    }

    // "New > Project from Selection": picked tasks become the sub-tasks of a new
    // project.
    void projectFromSelection() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        QVERIFY(selectRow(app, app.ids.buyMilk));
        clickWith(app.row(app.ids.plants), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        app.press(Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(waitItem("editorTitle"));
        QTRY_VERIFY_WITH_TIMEOUT(prop("editorTitle", "activeFocus").toBool(), 5000);
        app.type("Errands");
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Errands").id != 0, 5000);
        const Task project = app.taskTitled("Errands");
        QCOMPARE(project.type, int(TaskType::Project));
        QCOMPARE(app.task(app.ids.buyMilk).parentId, project.id);
        QCOMPARE(app.task(app.ids.plants).parentId, project.id);
        QTRY_VERIFY_WITH_TIMEOUT(badge("rowProject", project.id), 5000);
        // One undo breaks the project up again.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.buyMilk).parentId, qint64(0), 5000);
        QCOMPARE(app.task(app.ids.plants).parentId, qint64(0));
    }
};

REGISTER_TEST(E2ETasksStructure)

#include "test_tasks_structure.moc"
