// End-to-end accessibility guard: walks the visible items of every window the
// app can show and reports every interactive one that a screen reader could
// not name. New controls are added to pendingAccessibility() only while the
// slice that names them is still to come; by the last slice the list is empty.

#include <QAccessible>
#include <QMetaEnum>
#include <QQuickItem>
#include <QtQuick/private/qquickaccessibleattached_p.h>
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

// An item a later slice has marked to be skipped, so the guard neither names
// it nor the list behind it.
bool ignored(QQuickItem *item) {
    QQuickAccessibleAttached *att = QQuickAccessibleAttached::attachedProperties(item);
    return att && att->ignored();
}

// Whether an item is one a user can act on: it takes keyboard focus, it is a
// button, it holds a mouse area or tap handler, or it is a text field. A bare
// MouseArea or TapHandler is the click target of the item that owns it, not an
// item in its own right, so it is not judged on its own; a mouse area marked
// Accessible.ignored is a gestural layer the slice has named its child for, so
// it does not make its owner interactive either.
bool interactive(QQuickItem *item) {
    if (ignored(item))
        return false;
    if (item->inherits("QQuickMouseArea") || item->inherits("QQuickTapHandler"))
        return false;
    if (item->property("activeFocusOnTab").toBool())
        return true;
    if (item->inherits("QQuickAbstractButton"))
        return true;
    if (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
        return true;
    for (QQuickItem *child : item->childItems()) {
        if (!child->isVisible() || !child->isEnabled() || ignored(child))
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

// Whether an item reports the given role and a name that starts with `name`.
bool reports(QQuickItem *item, QAccessible::Role role, const QString &name) {
    const Report r = reportOf(item);
    return r.hasInterface && r.role == role && r.name.startsWith(name);
}

// Every visible interactive item under `root`, in any window. An item that
// only owns a mouse area is skipped when an ancestor is also interactive: the
// row or badge above it is the one control a screen reader should read.
void collect(QQuickItem *root, bool underInteractive, QList<QQuickItem *> &out) {
    if (!root->isVisible() || !root->isEnabled())
        return;
    const bool here = interactive(root) && !ignored(root);
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
// matched against an item's objectName and class name; a pattern that is not
// a plain name (it contains ':' or '*') is a regular expression. Every entry
// is removed by the slice that names the control.
const QVector<QString> &pendingAccessibility() {
    static const QVector<QString> pending{
        // slice 2: task and sidebar rows
        "taskRow:", "taskCheck:", "rowStarArea:", "paneTitle",
        "sidebarRow:", "sidebarGroup:", "sidebarAdd", "sidebarTailDrop", "sidebarArchived",
        "tagRow:", "tagGroup:",
        // slice 3: editor, settings, search builder
        "editorTitle", "editorCard", "setting:", "settingsPane:", "settingsClose",
        "ruleProp:", "ruleOp:", "ruleValue:", "ruleAdd", "ruleRemove:", "ruleGroupRemove:",
        "ruleMatchAll:", "ruleMatchAny:", "nameChip:", "pickerTab:", "groupJoin:", "groupField",
        "dateN:", "dateUnit:", "range", "completed", "freeInput", "preview", "clearButton",
        "saveButton", "updateButton", "searchButton", "ruleAddMenu", "soundexBox",
        "aboutLink", "aboutLicense", "mailPipe", "mailPassword", "addGroup", "scopeThis",
        "scopeAll", "searchScheduledHide", "searchScheduledInclude",
        "editorStart", "editorStartTime", "editorDue", "editorDueTime", "editorTagsInput",
        "editorNewSubtask", "notesText", "notesStamp", "notesPreviewToggle", "editorTags",
        "quickStart", "quickStartTime", "quickDue", "quickDueTime", "quickTitle", "quickNotes",
        "quickTagInput", "quickTagChip", "quickTagRemove", "quickTagMatch",
        "quickType", "quickCancel", "quickSave",
        // slice 5: anything left
        "quickTag", "quickUnit:", "quickWeekday:",
        "quickCustomDone", "quickPriority", "quickList",
        "quickAttach", "quickLocation", "quickRepeat", "quickAlarm", "quickDuration",
        "utilityTab", "calTitle", "searchClear",
    };
    return pending;
}

// Whether an item is excused for a later slice. A named pattern is matched
// against the item's objectName and class; an anonymous helper item (one with
// no objectName) is also matched by the names of its parents, so the whole
// body of a not-yet-named component is excused together.
bool pending(const QQuickItem *item) {
    QStringList names{item->objectName()};
    if (item->objectName().isEmpty())
        for (QQuickItem *p = item->parentItem(); p; p = p->parentItem())
            names << p->objectName();
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
