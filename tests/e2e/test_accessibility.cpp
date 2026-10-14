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

// Every visible interactive item under `root`, in any window. An item that
// only owns a mouse area is skipped when an ancestor is also interactive: the
// row or badge above it is the one control a screen reader should read.
void collect(QQuickItem *root, bool underInteractive, QList<QQuickItem *> &out) {
    if (!root->isVisible() || !root->isEnabled())
        return;
    const bool here = interactive(root) && !ignored(root);
    if (here && (!underInteractive || independent(root)))
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
        // slice 3: editor, settings, search builder
        "editorTitle", "editorCard", "setting:", "settingsPane:", "settingsClose",
        "ruleProp:", "ruleOp:", "ruleValue:", "ruleAdd", "ruleRemove:", "ruleGroupRemove:",
        "ruleMatchAll:", "ruleMatchAny:", "nameChip:", "pickerTab:", "groupJoin:", "groupField",
        "dateN:", "dateUnit:", "range", "completed", "freeInput", "preview", "clearButton",
        "saveButton", "updateButton", "searchButton", "ruleAddMenu", "soundexBox",
        // slice 5: anything left
        "calDay:", "calDots", "calWeekendShade", "calDivider", "calTitle", "calToday",
        "dayBar", "tagRow:", "tagGroup:", "quickTag", "quickUnit:", "quickWeekday:",
        "quickCustomDone", "quickType", "quickCancel", "quickSave", "quickPriority", "quickList",
        "quickAttach", "quickLocation", "quickRepeat", "quickAlarm", "quickDuration",
        "utilityTab", "utilityAdd", "utility", "focusToggle", "focusConditions", "sortReverse",
        "searchPresets", "newTaskButton", "toggleUtility", "searchClear", "hideSidebar",
        "calendarToggle", "calendarStyle", "tagsManualToggle", "tagUsedOnly", "tagShowPaused",
        "section:", "sectionTitle", "exportDialog", "quickLook", "shortcutOverlay", "dayBarDetails",
        "archiveDialog", "editListDialog", "colorPickerDialog",
    };
    return pending;
}

bool pending(const QQuickItem *item) {
    const QString name = item->objectName();
    const QString type = shortClass(item);
    for (const QString &p : pendingAccessibility()) {
        if (p.contains('*')) {
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(p));
            if (re.match(name).hasMatch() || re.match(type).hasMatch())
                return true;
        } else if (name.startsWith(p) || type.startsWith(p)) {
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

    QStringList m_seen;

    // Prints the offenders of one state and records how many there were.
    void checkState(const QString &state) {
        const QStringList offenders = guardOffenders();
        m_seen << QString("%1: %2").arg(state).arg(offenders.size());
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
};

REGISTER_TEST(E2EAccessibility)

#include "test_accessibility.moc"
