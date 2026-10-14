// End-to-end accessibility guard: walks the visible items of every window the
// app can show and reports every interactive one that a screen reader could
// not name. New controls are added to pendingAccessibility() only while the
// slice that names them is still to come; by the last slice the list is empty.
//
// Run against main before this slice, with nothing excused and every window
// state probed, the guard found 809 offenders across those states (about 160
// distinct items). The shared components named by this slice remove the ones
// they cover; the rest sit in pendingAccessibility().

#include <QAccessible>
#include <QMetaEnum>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>

#include "misckit.h"
#include "settingskit.h"
#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

// The class name without the QML wrapper ("QQuickRectangle_QMLTYPE_4" ->
// "QQuickRectangle").
QString shortClass(const QObject *o) { return className(o); }

// A readable path to an item, for failure messages: its objectName and class,
// then its parents up to the window.
QString where(QQuickItem *item) {
    QStringList chain;
    for (QQuickItem *i = item; i && chain.size() < 8; i = i->parentItem())
        chain << QString("%1[%2]").arg(shortClass(i), i->objectName());
    return chain.join(" < ");
}

// Whether an item is one a user can act on: it takes keyboard focus, it is a
// button, it holds a mouse area or tap handler, or it is a text field. A bare
// MouseArea or TapHandler is the click target of the item that owns it, not an
// item in its own right, so it is not judged on its own.
bool interactive(QQuickItem *item) {
    if (item->inherits("QQuickMouseArea") || item->inherits("QQuickTapHandler"))
        return false;
    if (item->property("activeFocusOnTab").toBool())
        return true;
    if (item->inherits("QQuickAbstractButton"))
        return true;
    if (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
        return true;
    for (QQuickItem *child : item->childItems()) {
        if (!child->isVisible() || !child->isEnabled())
            continue;
        if (child->inherits("QQuickMouseArea") && child->property("acceptedButtons").toInt() != Qt::NoButton)
            return true;
        if (child->inherits("QQuickTapHandler"))
            return true;
    }
    return false;
}

// An item that is a control in its own right (a button or a text field), as
// opposed to one that merely owns the mouse area for its row.
bool independent(QQuickItem *item) {
    return item->property("activeFocusOnTab").toBool() || item->inherits("QQuickAbstractButton")
           || item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit");
}

// Whether a control lies below `item`: a wrapper that only forwards clicks to
// a field or button inside it is not itself a control.
bool hasIndependentDescendant(QQuickItem *item) {
    for (QQuickItem *child : item->childItems()) {
        if (!child->isVisible() || !child->isEnabled())
            continue;
        if (independent(child) || hasIndependentDescendant(child))
            return true;
    }
    return false;
}

// The accessible properties of an item, if Qt Quick built an interface for it.
struct Report {
    QQuickItem *item = nullptr;
    bool hasInterface = false;
    QString name;
    QAccessible::Role role = QAccessible::NoRole;
};

Report reportOf(QQuickItem *item) {
    Report r;
    r.item = item;
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item);
    if (!iface || !iface->isValid())
        return r;
    r.hasInterface = true;
    r.name = iface->text(QAccessible::Name).trimmed();
    r.role = iface->role();
    return r;
}

bool acceptable(const Report &r) {
    if (!r.hasInterface)
        return false;
    if (r.name.isEmpty())
        return false;
    return r.role != QAccessible::NoRole && r.role != QAccessible::Client;
}

// Gives an item the keyboard so its Keys handlers can be exercised, and
// returns it.
QQuickItem *focusOn(const QString &name) {
    QQuickItem *item = tk::waitItem(name);
    if (item)
        item->forceActiveFocus(Qt::TabFocusReason);
    return item;
}


// Every visible interactive item under `root`, in any window. An item that
// only owns a mouse area is skipped when an ancestor is also interactive: the
// row or badge above it is the one control a screen reader should read. An
// item that only owns a mouse area but wraps real controls of its own (such as
// the calendar's drag layer over its day buttons, or the task list's scroll
// viewport) is skipped too: the controls inside it are judged instead, so the
// wrapper cannot hide an unnamed one.
void collect(QQuickItem *root, bool underInteractive, QList<QQuickItem *> &out) {
    if (!root->isVisible() || !root->isEnabled())
        return;
    const bool here = interactive(root);
    if (here && (!underInteractive || independent(root)) && (independent(root) || !hasIndependentDescendant(root)))
        out << root;
    for (QQuickItem *child : root->childItems())
        collect(child, underInteractive || here, out);
}

QList<QQuickItem *> visibleInteractive() {
    QList<QQuickItem *> out;
    for (QQuickWindow *w : quickWindows())
        collect(w->contentItem(), false, out);
    return out;
}

// The interactive items that will be named by a later slice. Each pattern is
// matched against an item's objectName and class name: a plain prefix by
// startsWith (so "taskRow:" covers every task row), and a pattern containing
// '*' as a wildcard. Every entry is removed by the slice that names the
// control. The ObjectName patterns are deliberately specific, so a new
// offender cannot hide behind a broad prefix.
const QVector<QString> &pendingAccessibility() {
    static const QVector<QString> pending{
        // slice 3: editor, settings, search builder
        "editorTitle", "editorCard", "setting:", "settingsPane:", "settingsClose",
        "ruleProp:", "ruleOp:", "ruleValue:", "ruleAdd", "ruleRemove:", "ruleGroupRemove:",
        "ruleMatchAll:", "ruleMatchAny:", "nameChip:", "pickerTab:", "groupJoin:", "groupField",
        "dateN:", "dateUnit:", "rangeNone", "rangeFixed", "rangeNext", "rangeNoDue",
        "rangeFrom", "rangeTo", "rangeN", "rangeUnit:",
        "completedDefault", "completedInclude", "completedExclude",
        "freeInput", "preview", "clearButton",
        "saveButton", "updateButton", "searchButton", "ruleAddMenu", "soundexBox",
        "aboutLink", "aboutLicense", "mailPipe", "mailPassword", "addGroup", "scopeThis",
        "scopeAll", "searchScheduledHide", "searchScheduledInclude",
        "editorStart", "editorStartTime", "editorDue", "editorDueTime", "editorTagsInput",
        "editorNewSubtask", "notesText", "notesStamp", "notesPreviewToggle",
        "quickStart", "quickStartTime", "quickDue", "quickDueTime", "quickTitle", "quickNotes",
        "quickTagInput", "quickTagChip", "quickTagRemove", "quickTagMatch",
        "quickType", "quickCancel", "quickSave",
        // slice 5: anything left
        "quickUnit:", "quickWeekday:",
        "utilityTabLocations", "utilityTabTags",
        "quickCustomDone", "quickPriority", "quickList",
        "quickAttach", "quickLocation", "quickRepeat", "quickAlarm", "quickDuration",
        "calTitle", "searchClear", "commandChip:", "commandInput",
    };
    return pending;
}

// Whether an item is excused for a later slice. A named pattern is matched
// against the item's objectName and class; an anonymous helper item (one with
// no objectName) is matched by the nearest named ancestor only, so one
// not-yet-named component is excused without a broader subtree behind it.
bool pending(const QQuickItem *item) {
    QStringList names{item->objectName()};
    if (item->objectName().isEmpty())
        for (QQuickItem *p = item->parentItem(); p; p = p->parentItem())
            if (!p->objectName().isEmpty()) {
                names << p->objectName();
                break;
            }
    const QString type = shortClass(item);
    for (const QString &p : pendingAccessibility()) {
        if (p.contains('*')) {
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(p));
            if (re.match(type).hasMatch())
                return true;
            for (const QString &n : names)
                if (re.match(n).hasMatch())
                    return true;
        } else {
            if (type.startsWith(p))
                return true;
            for (const QString &n : names)
                if (n.startsWith(p))
                    return true;
        }
    }
    return false;
}

// The items that are interactive but not named and not excused for a later
// slice, one line each.
QStringList guardOffenders() {
    QStringList out;
    QSet<QQuickItem *> seen;
    for (QQuickItem *item : visibleInteractive()) {
        if (seen.contains(item))
            continue;
        seen.insert(item);
        if (pending(item))
            continue;
        const Report r = reportOf(item);
        if (acceptable(r))
            continue;
        out << QString("%1 role=%2 name=\"%3\" at %4")
                   .arg(r.hasInterface ? "unnamed" : "no interface",
                        QMetaEnum::fromType<QAccessible::Role>().valueToKey(r.role),
                        r.name, where(item));
    }
    out.sort();
    return out;
}

}  // namespace

class E2EAccessibility : public QObject {
    Q_OBJECT

    // The accessible interface of an item, or a null one when Qt Quick built
    // none. Qt Quick builds the interfaces lazily, so the guard and the naming
    // tests touch every item first.
    QAccessibleInterface *iface(const QString &name) {
        QQuickItem *item = tk::waitItem(name);
        if (!item)
            return nullptr;
        return QAccessible::queryAccessibleInterface(item);
    }

    QString axName(const QString &name) {
        QAccessibleInterface *i = iface(name);
        return i && i->isValid() ? i->text(QAccessible::Name) : QString();
    }

    QAccessible::Role axRole(const QString &name) {
        QAccessibleInterface *i = iface(name);
        return i && i->isValid() ? i->role() : QAccessible::NoRole;
    }

    // Prints the offenders of one state and fails with them all.
    void checkState(const QString &state) {
        const QStringList offenders = guardOffenders();
        for (const QString &o : offenders)
            qInfo().noquote() << "AX" << state << o;
        QVERIFY2(offenders.isEmpty(), qPrintable(state + " has " + QString::number(offenders.size()) + " unnamed interactive items:\n"
                                                  + offenders.join('\n')));
    }

private slots:
    void init() { QAccessible::setActive(true); }

    void mainWindowIsNamed() {
        App app;
        QVERIFY(app.ok);
        checkState("main window");
    }

    void editorIsNamed() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        QVERIFY(tk::openEditor(app, app.ids.buyMilk));
        checkState("editor");
    }

    void settingsPanesAreNamed() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        for (const QString &pane : paneNames()) {
            selectPane(sw, pane);
            settleWindow(sw);
            checkState("settings: " + pane);
        }
        closeSettings(app);
    }

    void searchOptionsAreNamed() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(openBuilder(app));
        QVERIFY(!addRuleRow("Starred", "is").isEmpty());
        QVERIFY(!addRuleRow("Note", "has").isEmpty());
        QVERIFY(pressBuilderForChange("ruleAdd:0", Qt::AltModifier));
        QVERIFY(waitUntil([] { return ruleRowShown("1.0"); }));
        checkState("search options");
        QVERIFY(closeBuilder(app));
    }

    // The shared components the guard relies on read the role and name the
    // screen reader spec asks for.
    void sharedComponentsReportRoleAndName() {
        App app;
        QVERIFY(app.ok);

        // An IconButton is a button named by its tip.
        QQuickItem *add = tk::waitItem("newTaskButton");
        QVERIFY(add);
        QCOMPARE(axRole("newTaskButton"), QAccessible::Button);
        QCOMPARE(axName("newTaskButton"), add->property("tip").toString());

        // The sort button reads its current sort; it is a button too.
        QCOMPARE(axRole("sortButton"), QAccessible::Button);
        QVERIFY(axName("sortButton").startsWith("Sort by "));

        // The focus filter names its state.
        QCOMPARE(axRole("focusToggle"), QAccessible::Button);
        QVERIFY(!axName("focusToggle").isEmpty());

        // A calendar day reads its full date.
        QQuickItem *calendar = tk::waitItem("miniCalendar");
        QVERIFY(calendar);
        QQuickItem *day = nullptr;
        QVERIFY(waitUntil([&] {
            day = findIn(calendar, [](QQuickItem *i) { return i->objectName().startsWith("calDay:"); }, false);
            return day != nullptr;
        }));
        const QString iso = day->objectName().mid(7);
        const QDate date = QDate::fromString(iso, Qt::ISODate);
        QVERIFY(date.isValid());
        QAccessibleInterface *dayIface = QAccessible::queryAccessibleInterface(day);
        QVERIFY(dayIface && dayIface->isValid());
        QCOMPARE(int(dayIface->role()), int(QAccessible::Button));
        QVERIFY2(dayIface->text(QAccessible::Name).startsWith(date.toString("dddd d MMMM yyyy")),
                 qPrintable(dayIface->text(QAccessible::Name)));

        // A section heading is a heading with its visible text.
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        QQuickItem *section = nullptr;
        QVERIFY(waitUntil([&] {
            section = findItem("section:d:" + QDate::currentDate().toString(Qt::ISODate));
            return section != nullptr;
        }));
        QAccessibleInterface *sectionIface = QAccessible::queryAccessibleInterface(section);
        QVERIFY(sectionIface && sectionIface->isValid());
        QCOMPARE(int(sectionIface->role()), int(QAccessible::Heading));
        QVERIFY(!sectionIface->text(QAccessible::Name).isEmpty());

        // Dialogs and popups report their title.
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        QAccessibleInterface *settingsIface = QAccessible::queryAccessibleInterface(sw);
        QVERIFY(settingsIface && settingsIface->isValid());
        QVERIFY2(settingsIface->text(QAccessible::Name).contains("Settings"), qPrintable(settingsIface->text(QAccessible::Name)));
        closeSettings(app);
    }

    // The accessible item of a popup or dialog, and a check that it reports
    // the Dialog role and the given title.
    void checkDialog(const QString &objectName, const QString &title) {
        QObject *p = findObject(objectName);
        QVERIFY2(p, qPrintable("no popup " + objectName));
        // Accessible properties on a Popup's contentItem surface through the
        // content item; a Dialog puts them on its own surface.
        auto *content = p->property("contentItem").value<QQuickItem *>();
        QQuickItem *item = (content && QAccessible::queryAccessibleInterface(content) && QAccessible::queryAccessibleInterface(content)->isValid())
                               ? content : popupSurface(p);
        QVERIFY2(item, qPrintable(objectName + " has no accessible item"));
        QAccessibleInterface *i = QAccessible::queryAccessibleInterface(item);
        QVERIFY2(i && i->isValid(), qPrintable(objectName + " has no interface"));
        QCOMPARE(int(i->role()), int(QAccessible::Dialog));
        QCOMPARE(i->text(QAccessible::Name), title);
    }

    // The command popup, the Quick Look and shortcut popups and the
    // ThemedDialog windows all report the Dialog role and their title.
    void popupsReportDialogAndTitle() {
        App app;
        QVERIFY(app.ok);

        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        app.press(Qt::Key_Space);
        QVERIFY(waitUntil([] { QObject *p = findObject("quickLook"); return p && p->property("visible").toBool(); }));
        checkDialog("quickLook", "Quick Look");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("quickLook")->property("visible").toBool(), 5000);

        app.press(Qt::Key_Question, Qt::ShiftModifier);
        QVERIFY(waitUntil([] { QObject *p = findObject("shortcutOverlay"); return p && p->property("visible").toBool(); }));
        checkDialog("shortcutOverlay", "Keyboard Shortcuts");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("shortcutOverlay")->property("visible").toBool(), 5000);

        // The command bar's title comes from its kind, first Defer (d) then
        // Quick Open (Ctrl+Shift+O).
        app.press(Qt::Key_D);
        QVERIFY(waitUntil([] { QObject *p = findObject("commandPopup"); return p && p->property("visible").toBool(); }));
        checkDialog("commandPopup", "Defer");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("commandPopup")->property("visible").toBool(), 5000);
        app.press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(waitUntil([] { QObject *p = findObject("commandPopup"); return p && p->property("visible").toBool(); }));
        checkDialog("commandPopup", "Quick Open");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("commandPopup")->property("visible").toBool(), 5000);

        // A ThemedDialog is a Dialog named by its title: Export Tasks.
        app.window->requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(app.window->isActive(), 5000);
        app.press(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
        QObject *exportDialog = nullptr;
        QVERIFY(waitUntil([&] { return (exportDialog = findObject("exportDialog")) && exportDialog->property("opened").toBool(); }));
        checkDialog("exportDialog", "Export Tasks");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!exportDialog->property("opened").toBool(), 5000);

        // Quick Entry is its own window, so it is identified by its title and
        // reports Dialog with that title.
        app.press(Qt::Key_2, Qt::ShiftModifier | Qt::ControlModifier);
        QQuickWindow *quick = nullptr;
        QVERIFY(waitUntil([&] { return (quick = windowTitled("Fairfax Quick Entry")) != nullptr; }));
        QQuickItem *quickBody = nullptr;
        QVERIFY(waitUntil([&] { return (quickBody = findItem("quickEntryBody")) != nullptr; }));
        QAccessibleInterface *qi = QAccessible::queryAccessibleInterface(quickBody);
        QVERIFY(qi && qi->isValid());
        QCOMPARE(int(qi->role()), int(QAccessible::Dialog));
        QVERIFY2(qi->text(QAccessible::Name).contains("Quick Entry"), qPrintable(qi->text(QAccessible::Name)));
        quick->close();
        QTRY_VERIFY_WITH_TIMEOUT(windowTitled("Fairfax Quick Entry") == nullptr, 5000);
    }

    // The other ThemedDialogs keep the Dialog role and their title through Qt's
    // own defaults: the archive, the list editor and the colour picker.
    void themedDialogsReportDialogAndTitle() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();

        // Archived Lists, opened from the sidebar's Archived row.
        QVERIFY(listMenu(app, w.errands, "Archive"));
        QQuickItem *archived = nullptr;
        QVERIFY(waitUntil([&] { return (archived = findItem("sidebarArchived")) != nullptr; }));
        clickSteady(archived);
        QVERIFY(waitUntil([] { QObject *p = findObject("archiveDialog"); return p && p->property("opened").toBool(); }));
        checkDialog("archiveDialog", "Archived Lists");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("archiveDialog")->property("opened").toBool(), 5000);

        // Edit List, from a list row's context menu.
        QVERIFY(listMenu(app, app.ids.home, "Edit List…"));
        QVERIFY(waitUntil([] { QObject *p = findObject("editListDialog"); return p && p->property("opened").toBool(); }));
        checkDialog("editListDialog", "Edit List");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("editListDialog")->property("opened").toBool(), 5000);

        // Custom Color, from the same menu's Color submenu.
        QVERIFY(listSubmenu(app, app.ids.home, "Color", "Custom…"));
        QVERIFY(waitUntil([] { QObject *p = findObject("colorPicker"); return p && p->property("opened").toBool(); }));
        checkDialog("colorPicker", "Custom Color");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("colorPicker")->property("opened").toBool(), 5000);
    }

    // The guard's own scan detects a MouseArea-owned control (IconButton), so
    // an unnamed one would be reported. If interactive() skipped MouseArea
    // items, this would fail.
    void guardDetectsMouseAreaControls() {
        App app;
        QVERIFY(app.ok);
        QQuickItem *add = tk::waitItem("newTaskButton");
        QVERIFY(add);
        QVERIFY2(interactive(add), "IconButton is interactive (it owns a MouseArea)");
        const Report r = reportOf(add);
        QVERIFY(r.hasInterface);
        QVERIFY(acceptable(r));
        QCOMPARE(r.role, QAccessible::Button);
        QCOMPARE(r.name, add->property("tip").toString());
    }

    // Each custom control answers Space and Return (and provides a
    // press action) the way a screen reader expects.
    void keyboardActivatesControls() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));

        // The toolbar buttons toggle and open their menus. A refilter pulls
        // focus back to the task list (TaskPane's viewChanged handler), so each
        // activation is given the keyboard again before the next key.
        const bool asc = app.backend().sortAscending();
        QVERIFY(focusOn("sortReverse"));
        app.press(Qt::Key_Space);
        QCOMPARE(app.backend().sortAscending(), !asc);
        QVERIFY(focusOn("sortReverse"));
        app.press(Qt::Key_Return);
        QCOMPARE(app.backend().sortAscending(), asc);

        QVERIFY(focusOn("sortButton"));
        app.press(Qt::Key_Return);
        QVERIFY(tk::waitItem("sortSeparated"));
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("sortSeparated"), 5000);

        // A calendar day filters to it on Return.
        QQuickItem *day = nullptr;
        QVERIFY(waitUntil([&] {
            day = findIn(tk::waitItem("miniCalendar"), [](QQuickItem *i) { return i->objectName().startsWith("calDay:"); }, true);
            return day != nullptr;
        }));
        const QString iso = day->objectName().mid(7);
        QVERIFY(focusOn(day->objectName()));
        app.press(Qt::Key_Return);
        QCOMPARE(app.backend().dayFilter(), iso);
        app.backend().setDayFilter(QString());

        // A removable tag chip takes the keyboard and removes the tag.
        QVERIFY(mk::homeWith(app, app.ids.plants));
        QVERIFY(tk::openEditor(app, app.ids.plants));
        QVERIFY(tk::waitItem("editorTagsRemove:garden"));
        QVERIFY(!app.task(app.ids.plants).tagIds.isEmpty());
        QVERIFY(focusOn("editorTagsRemove:garden"));
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.plants).tagIds.isEmpty(), 5000);
    }

    // A task row reads its title and the facts the sighted row shows: the
    // friendly due text, the priority and the star.
    void taskRowIsNamed() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));

        // Pay electricity bill is overdue and high priority.
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(app.ids.electricity)));
        const QString due = tk::rowText("rowDue", app.ids.electricity);
        QVERIFY(!due.isEmpty());
        const QString billed = axName(QString("taskRow:%1").arg(app.ids.electricity));
        QVERIFY2(billed.contains("Pay electricity bill"), qPrintable(billed));
        QVERIFY2(billed.contains(due), qPrintable(billed));
        QVERIFY2(billed.contains("high priority"), qPrintable(billed));

        // Fix the shelf is starred (and has no due date).
        const QString shelf = axName(QString("taskRow:%1").arg(app.ids.shelf));
        QVERIFY2(shelf.contains("Fix the shelf"), qPrintable(shelf));
        QVERIFY2(shelf.contains("starred"), qPrintable(shelf));
    }

    // Completing a task adds "completed" to the name and sets the checked
    // state; selecting it sets the selected state.
    void taskRowCheckedAndSelected() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(id)));

        QVERIFY(!axName(QString("taskRow:%1").arg(id)).contains("completed"));
        QVERIFY(!iface(QString("taskRow:%1").arg(id))->state().checked);

        QVERIFY(tk::clickNamed(QString("taskCheck:%1").arg(id)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(id).completed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(axName(QString("taskRow:%1").arg(id)).contains("completed"), 5000);
        QVERIFY(iface(QString("taskRow:%1").arg(id))->state().checked);

        QVERIFY(tk::selectRow(app, id));
        QTRY_VERIFY_WITH_TIMEOUT(iface(QString("taskRow:%1").arg(id))->state().selected, 5000);
    }

    // A project's row says what it is and how its sub-tasks are going.
    void projectRowSaysProjectAndProgress() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        QVERIFY(app.openSource(lk(app.ids.work)));
        QVERIFY(tk::waitItem(QString("taskRow:%1").arg(w.launch)));

        const QString name = axName(QString("taskRow:%1").arg(w.launch));
        QVERIFY2(name.contains("Launch"), qPrintable(name));
        QVERIFY2(name.contains("project"), qPrintable(name));
        QVERIFY2(name.contains("0 of 1 done"), qPrintable(name));

        // Completing the one sub-task moves the count.
        QVERIFY(tk::clickNamed(QString("taskCheck:%1").arg(w.announce)));
        QTRY_VERIFY_WITH_TIMEOUT(app.task(w.announce).completed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(axName(QString("taskRow:%1").arg(w.launch)).contains("1 of 1 done"), 5000);
    }

    // A sidebar list row's name is its title and the counts the row shows.
    void sidebarListRowIsNamed() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        const QString key = lk(w.errands);
        QVERIFY(tk::waitItem("sidebarRow:" + key));
        const auto badge = badgeOf(app, key);
        QVERIFY(badge.overdueShown);
        QVERIFY(badge.undoneShown);

        const QString name = axName("sidebarRow:" + key);
        QVERIFY2(name.contains("Errands"), qPrintable(name));
        QVERIFY2(name.contains(QString::number(badge.undone) + " tasks"), qPrintable(name));
        QVERIFY2(name.contains(QString::number(badge.overdue) + " overdue"), qPrintable(name));

        // Focus lists say what they are and carry the counts shown too.
        const QString today = axName("sidebarRow:today");
        QVERIFY2(today.contains("Today") && today.contains("focus list"), qPrintable(today));
    }

    // A group header is a tree item whose name and state track expanded and
    // collapsed.
    void sidebarGroupReportsExpanded() {
        App app(seedLists);
        QVERIFY(app.ok);
        const auto &w = world();
        QQuickItem *header = nullptr;
        QVERIFY(tk::waitFor([&] { return (header = groupHeader(w.projects)) != nullptr; }));
        const QString name = "sidebarGroup:" + QString::number(w.projects);
        QCOMPARE(axRole(name), QAccessible::TreeItem);
        QVERIFY(iface(name)->state().expanded);
        QVERIFY(!iface(name)->state().collapsed);
        QVERIFY2(axName(name).contains("expanded"), qPrintable(axName(name)));

        QVERIFY(tk::clickNamed(name));
        QTRY_VERIFY_WITH_TIMEOUT(liveGroup(app, w.projects).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!iface(name)->state().expanded, 5000);
        QVERIFY2(axName(name).contains("collapsed"), qPrintable(axName(name)));

        QVERIFY(tk::clickNamed(name));
        QTRY_VERIFY_WITH_TIMEOUT(!liveGroup(app, w.projects).collapsed, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(iface(name)->state().expanded, 5000);
    }

    void popupsAndMenusAreNamed() {
        App app;
        QVERIFY(app.ok);

        // Quick Look on the selected task.
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        app.press(Qt::Key_Space);
        QObject *look = findObject("quickLook");
        QVERIFY(look);
        QVERIFY(waitUntil([&] { return look->property("visible").toBool(); }));
        checkState("quick look");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!look->property("visible").toBool(), 5000);

        // The ? overlay.
        app.press(Qt::Key_Question, Qt::ShiftModifier);
        QObject *help = findObject("shortcutOverlay");
        QVERIFY(help);
        QVERIFY(waitUntil([&] { return help->property("visible").toBool(); }));
        checkState("shortcut overlay");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!help->property("visible").toBool(), 5000);

        // The command popup (Defer bar), the shared palette behind the reference app's
        // quick commands.
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        app.press(Qt::Key_D);
        QVERIFY(waitUntil([] { QObject *b = findObject("commandPopup"); return b && b->property("visible").toBool(); }));
        checkState("command popup");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("commandPopup")->property("visible").toBool(), 5000);

        // The task context menu.
        QVERIFY(tk::contextMenuOn(app, app.ids.buyMilk));
        checkState("task context menu");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(tk::noMenuOpen(), 5000);

        // Quick Entry, in its own window.
        app.window->requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(app.window->isActive(), 5000);
        app.press(Qt::Key_2, Qt::ShiftModifier | Qt::ControlModifier);
        QQuickWindow *quick = nullptr;
        QVERIFY(waitUntil([&] { return (quick = windowTitled("Quick Entry")) != nullptr; }));
        settleWindow(quick);
        checkState("quick entry");
        quick->close();
        QTRY_VERIFY_WITH_TIMEOUT(windowTitled("Quick Entry") == nullptr, 5000);
    }
};

REGISTER_TEST(E2EAccessibility)

#include "test_accessibility.moc"
