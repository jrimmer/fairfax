// End-to-end accessibility guard: walks the visible items of every window the
// app can show and reports every interactive one that a screen reader could
// not name. Every slice named its share and the last one emptied the pending
// list, so the guard now judges every control it finds.
//
// Run against main before the five slices, with nothing excused and every
// window state probed, the guard found 809 offenders across those states
// (about 160 distinct items); the slices named them all.
//
// This slice also covers focus: Tab and Shift+Tab walk the regions and the
// editor's fields in visual order, a custom control shows its focus ring only
// when the keyboard brought it the focus, and focus never lands on a hidden
// item.

#include <QAccessible>
#include <QMetaEnum>
#include <QQuickItem>
#include <QQuickWindow>

#include "misckit.h"
#include "settingskit.h"
#include "smartkit.h"
#include "mailkit.h"
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
    // Qt blanks the name of a password field, whatever it is given, so the
    // label it is tied to with Accessible.labelledBy stands for it.
    if (r.name.isEmpty() && item->property("echoMode").toInt() != 0)
        for (QObject *child : item->findChildren<QObject *>(QString(), Qt::FindDirectChildrenOnly))
            if (!qstrcmp(child->metaObject()->className(), "QQuickAccessibleAttached"))
                if (auto *label = child->property("labelledBy").value<QQuickItem *>())
                    if (QAccessibleInterface *li = QAccessible::queryAccessibleInterface(label); li && li->isValid())
                        r.name = li->text(QAccessible::Name).trimmed();
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

// Captures the announcements a screen reader would hear, and the politeness
// each arrived with. Qt Quick turns Accessible.announce() into a
// QAccessibleAnnouncementEvent, the same event a native app posts, so the
// test sees exactly what Orca would. Only one update handler can be installed
// at a time, so the current one goes back in the destructor.
class AnnouncementLog {
public:
    struct Entry {
        QString message;
        QAccessible::AnnouncementPoliteness politeness;
    };

    AnnouncementLog() {
        m_previous = QAccessible::installUpdateHandler(&AnnouncementLog::handle);
        s_active = this;
    }
    ~AnnouncementLog() {
        QAccessible::installUpdateHandler(m_previous);
        s_active = nullptr;
    }

    QList<Entry> entries;

    // The politeness of a captured message that contains `part`, and NoRole
    // as a sentinel when nothing matched.
    QAccessible::AnnouncementPoliteness politenessOf(const QString &part) const {
        for (const Entry &e : entries)
            if (e.message.contains(part))
                return e.politeness;
        return QAccessible::AnnouncementPoliteness::Polite;
    }
    bool saw(const QString &part) const {
        for (const Entry &e : entries)
            if (e.message.contains(part))
                return true;
        return false;
    }

private:
    static void handle(QAccessibleEvent *event) {
        if (!s_active || !event || event->type() != QAccessible::Announcement)
            return;
        auto *announcement = dynamic_cast<QAccessibleAnnouncementEvent *>(event);
        if (announcement)
            s_active->entries.append({announcement->message(), announcement->politeness()});
    }
    QAccessible::UpdateHandler m_previous = nullptr;
    static AnnouncementLog *s_active;
};

AnnouncementLog *AnnouncementLog::s_active = nullptr;


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

// The items that are interactive but not named, one line each. Every slice
// has named its share and the pending list is gone, so this judges every
// control the guard finds: a new control added without a name fails here.
QStringList guardOffenders() {
    QStringList out;
    QSet<QQuickItem *> seen;
    for (QQuickItem *item : visibleInteractive()) {
        if (seen.contains(item))
            continue;
        seen.insert(item);
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

    static QString regionOf(QQuickItem *f) {
        for (QQuickItem *i = f; i; i = i->parentItem()) {
            if (i->objectName() == "editorCard") return "editor";
            if (i->objectName() == "taskList") return "tasklist";
            if (shortClass(i) == "UtilityPanel") return "utility";
            if (i->objectName() == "miniCalendar") return "sidebar-cal";
            if (i->objectName() == "searchBox") return "search";
            if (shortClass(i) == "Sidebar") return "sidebar";
            if (shortClass(i) == "TaskPane") return "taskpane";
        }
        return "other";
    }

    // From the task list, Tab and Shift+Tab step through the main regions in
    // visual order: sidebar, search, task list, editor (when open), utility.
    // The cycle repeats, so the first visit of each region is a rotation of
    // that order and the sequence closes where it began.
    void tabVisitsRegionsInOrder() {
        App app;
        QVERIFY(app.ok);
        QStringList hidden;
        auto cycle = [&](Qt::Key key) {
            QQuickItem *list = tk::waitItem("taskList");
            list->forceActiveFocus(Qt::TabFocusReason);
            QStringList regions;
            QString last;
            for (int i = 0; i < 160; ++i) {
                QQuickItem *f = app.window->activeFocusItem();
                if (!f) { hidden << "nothing"; break; }
                if (!f->isVisible()) hidden << regionOf(f) + ":" + f->objectName();
                QString r = regionOf(f);
                if (r == "sidebar-cal") r = "sidebar";
                // The toolbar sits in the task pane between the sidebar and the
                // search field; it is not a region of its own.
                if (r != "taskpane" && r != "other" && r != last) { regions << r; last = r; }
                QTest::keyClick(app.window, key);
            }
            return regions;
        };

        // With the editor open the regions are sidebar, search, task list,
        // editor and utility, left to right. From the task list, Tab visits
        // the regions to its right and then wraps: editor, utility, sidebar,
        // search. Shift+Tab walks the other way.
        QVERIFY(tk::openEditor(app, app.ids.buyMilk));
        // Each entry is the run of that region starting at the task list.
        const QList<QPair<Qt::Key, QStringList>> expected{
            {Qt::Key_Tab, {"tasklist", "editor", "utility", "sidebar", "search"}},
            {Qt::Key_Backtab, {"tasklist", "search", "sidebar", "utility", "editor"}},
        };
        for (const auto &[key, order] : expected) {
            QStringList seen = cycle(key);
            const int n = order.size();
            QVERIFY2(seen.size() >= n, qPrintable(seen.join(", ")));
            const int at = seen.indexOf("tasklist");
            QVERIFY2(at >= 0, qPrintable(seen.join(", ")));
            QCOMPARE(seen.mid(at, n), order);
        }
        QVERIFY2(hidden.isEmpty(), qPrintable("keyboard focus on invisible: " + hidden.join(", ")));
    }

    // A custom control that takes the keyboard shows a focus ring when Tab
    // brings it the focus, and the ring is gone once it is clicked: the mouse
    // look is unchanged.
    void focusRingShowsForKeyboardOnly() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));

        // Tab reaches the toolbar's IconButtons. Start from the sidebar's
        // hide button and walk forward to the reverse-sort toggle.
        QQuickItem *start = tk::waitItem("hideSidebar");
        start->forceActiveFocus(Qt::TabFocusReason);
        QQuickItem *button = nullptr;
        for (int i = 0; i < 200 && !button; ++i) {
            QTest::keyClick(app.window, Qt::Key_Tab);
            QQuickItem *f = app.window->activeFocusItem();
            if (f && f->objectName() == "sortReverse")
                button = f;
        }
        QVERIFY2(button, "sortReverse reached by Tab");
        QVERIFY(button->property("activeFocus").toBool());
        QQuickItem *ring = findItem("sortReverseRing");
        QVERIFY2(ring, "sortReverse has a focus ring");
        QVERIFY2(ring->isVisible(), "the ring shows for keyboard focus");

        // Clicking drops keyboard focus, and the ring with it.
        const bool asc = app.backend().sortAscending();
        tk::clickNamed("sortReverse");
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sortAscending(), !asc, 5000);
        QVERIFY(!button->property("activeFocus").toBool());
        ring = findItem("sortReverseRing", false);
        QVERIFY2(ring, "sortReverse ring still exists");
        QVERIFY2(!ring->isVisible(), "the ring is not shown for a mouse click");
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
        app.press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
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

    // Every field of the editor reads by its field name; the star menu's
    // entry reads "Starred" and carries the star's state.
    void editorFieldsAreNamed() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        QVERIFY(tk::openEditor(app, app.ids.buyMilk));

        // The spec's field names, plus the time halves of the two date fields.
        const QStringList fields{"Title", "Notes", "Due date", "Start date", "Repeat", "Alerts", "List", "Tags",
                                 "Action", "Location", "Priority", "Starred", "Duration"};
        const QList<QPair<QString, QString>> expected{
            {"editorTitle", "Title"},       {"notesText", "Notes"},        {"editorDue", "Due date"},
            {"editorStart", "Start date"},  {"editorRepeat", "Repeat"},    {"editorAlarm", "Alerts"},
            {"editorList", "List"},         {"editorTagsInput", "Tags"},   {"editorAction", "Action"},
            {"editorLocation", "Location"}, {"editorPriority", "Priority"}, {"editorDuration", "Duration"},
            {"editorDueTime", "Due time"},  {"editorStartTime", "Start time"}};
        for (const auto &[objectName, name] : expected) {
            QVERIFY2(tk::waitItem(objectName), qPrintable("no editor field " + objectName));
            QCOMPARE(axName(objectName), name);
            QVERIFY2(fields.contains(name) || name.endsWith(" time"), qPrintable(name));
        }

        // The list badge reads its list as the description.
        QAccessibleInterface *list = iface("editorList");
        QVERIFY(list && list->isValid());
        QVERIFY(!list->text(QAccessible::Description).isEmpty());

        // Starring is reached through the priority menu.
        QVERIFY(tk::clickNamed("editorPriority"));
        QQuickItem *star = nullptr;
        QVERIFY(waitUntil([&] { return (star = findItem("editorStarred")) != nullptr; }));
        QAccessibleInterface *si = QAccessible::queryAccessibleInterface(star);
        QVERIFY(si && si->isValid());
        QCOMPARE(si->text(QAccessible::Name), QString("Starred"));
        QVERIFY(si->state().checkable);
        QVERIFY(fields.contains(si->text(QAccessible::Name)));
        app.press(Qt::Key_Escape);
    }

    // The date picker, the repeat editor and the alert dialogs name their parts.
    void pickersAreNamed() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        QVERIFY(tk::openEditor(app, app.ids.buyMilk));

        // The date picker opens under the due field with Down.
        QVERIFY(focusOn("editorDue"));
        app.press(Qt::Key_Down);
        QQuickItem *none = nullptr;
        QVERIFY(waitUntil([&] { return (none = findItem("pickerChip:None")) != nullptr; }));
        QCOMPARE(axName("pickerChip:None"), QString("None"));
        QCOMPARE(axRole("pickerChip:None"), QAccessible::Button);
        QCOMPARE(axName("pickerPrevious"), QString("Previous month"));
        QCOMPARE(axName("pickerNext"), QString("Next month"));
        QQuickItem *day = findIn(none->window()->contentItem(), [](QQuickItem *i) { return i->objectName().startsWith("pickerDay:"); });
        QVERIFY(day);
        const QDate date = QDate::fromString(day->objectName().mid(10), Qt::ISODate);
        QVERIFY(date.isValid());
        QVERIFY2(axName(day->objectName()).startsWith(date.toString("dddd d MMMM yyyy")), qPrintable(axName(day->objectName())));
        checkState("date picker");
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("pickerChip:None"), 5000);

        // The repeat editor, in the weekly and the monthly shape.
        QVERIFY(tk::clickNamed("editorRepeat"));
        QVERIFY(tk::waitItem("repeatFreq:Weekly"));
        QVERIFY(tk::clickNamed("repeatFreq:Weekly"));
        QVERIFY(tk::waitItem("repeatWeekday:0"));
        QVERIFY(waitUntil([] { return findItem("repeatInterval") != nullptr; }));
        QCOMPARE(axName("repeatWeekday:0"), QString("Monday"));
        QCOMPARE(axName("repeatInterval"), QString("Repeat every"));
        checkState("repeat editor, weekly");
        QVERIFY(tk::clickNamed("repeatFreq:Monthly"));
        QVERIFY(tk::waitItem("repeatMonthNth"));
        QCOMPARE(axRole("repeatMonthNth"), QAccessible::RadioButton);
        QVERIFY(!axName("repeatOrdinal").isEmpty());
        checkState("repeat editor, monthly");
        app.press(Qt::Key_Escape);
    }

    // The pane list is a tab list and the panes are tabs; the selected pane
    // reports it.
    void settingsPanesAreTabs() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        QQuickItem *first = tk::waitItem("settingsPane:" + paneNames().first());
        QVERIFY(first);
        QAccessibleInterface *list = QAccessible::queryAccessibleInterface(first->parentItem());
        QVERIFY(list && list->isValid());
        QCOMPARE(int(list->role()), int(QAccessible::PageTabList));
        for (const QString &pane : paneNames()) {
            QQuickItem *tab = tk::waitItem("settingsPane:" + pane);
            QVERIFY2(tab, qPrintable(pane));
            QAccessibleInterface *i = QAccessible::queryAccessibleInterface(tab);
            QVERIFY(i && i->isValid());
            QCOMPARE(int(i->role()), int(QAccessible::PageTab));
            QCOMPARE(i->text(QAccessible::Name), pane);
        }
        selectPane(sw, "Tasks");
        QAccessibleInterface *tasks = QAccessible::queryAccessibleInterface(tk::waitItem("settingsPane:Tasks"));
        QVERIFY(tasks->state().selected);
        QAccessibleInterface *general = QAccessible::queryAccessibleInterface(tk::waitItem("settingsPane:General"));
        QVERIFY(!general->state().selected);
        closeSettings(app);
    }

    // Each Settings switch is named by its label and described by its note.
    void settingsTogglesReadLabelAndNote() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        int toggles = 0, withNote = 0;
        for (const QString &pane : paneNames()) {
            selectPane(sw, pane);
            settleWindow(sw);
            QList<QQuickItem *> boxes;
            findIn(sw->contentItem(), [&](QQuickItem *i) {
                QQuickItem *owner = i->parentItem();
                if (i->objectName().startsWith("setting:") && i->inherits("QQuickCheckBox") && owner
                    && owner->property("label").isValid() && owner->property("note").isValid())
                    boxes << i;
                return false;
            });
            for (QQuickItem *box : boxes) {
                const QString label = box->parentItem()->property("label").toString();
                const QString note = box->parentItem()->property("note").toString();
                QAccessibleInterface *i = QAccessible::queryAccessibleInterface(box);
                QVERIFY(i && i->isValid());
                QCOMPARE(i->text(QAccessible::Name), label);
                QCOMPARE(i->text(QAccessible::Description), note);
                QCOMPARE(int(i->role()), int(QAccessible::CheckBox));
                ++toggles;
                withNote += !note.isEmpty();
            }
        }
        QVERIFY2(toggles >= 10, qPrintable(QString::number(toggles)));
        QVERIFY2(withNote >= 1, qPrintable(QString::number(withNote)));
        closeSettings(app);
    }

    // The shortcut rows say which command their buttons belong to.
    void shortcutButtonsNameTheirCommand() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Shortcuts");
        settleWindow(sw);
        QQuickItem *key = tk::waitItem("shortcut:task.new");
        QVERIFY(key);
        const QString name = axName("shortcut:task.new");
        QVERIFY2(name.startsWith("New task: "), qPrintable(name));
        QVERIFY2(name.endsWith(app.backend().shortcutText("task.new")), qPrintable(name));
        QCOMPARE(axName("shortcutClear:task.new"), QString("Clear New task"));
        closeSettings(app);
    }

    // The About links take the keyboard and open on Space and Return.
    void aboutLinksAreKeyboardReachable() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        for (const QString &name : {QString("aboutLink"), QString("aboutLicense")}) {
            QQuickItem *link = tk::waitItem(name);
            QVERIFY(link);
            QVERIFY(link->property("activeFocusOnTab").toBool());
            QCOMPARE(axRole(name), QAccessible::Link);
            QVERIFY(!axName(name).isEmpty());
        }
        // Return and Space each open the licence.
        for (Qt::Key key : {Qt::Key_Return, Qt::Key_Space}) {
            QQuickItem *license = tk::waitItem("aboutLicense");
            license->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(sw, key);
            QObject *dialog = nullptr;
            QVERIFY(waitUntil([&] { return (dialog = findObject("licenseDialog")) && dialog->property("visible").toBool(); }));
            QTest::keyClick(sw, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 5000);
        }
        closeSettings(app);
    }

    // The search builder names its rule combos and buttons by the rule's
    // path, in a sub-filter too, and groups Match All/Any.
    void searchBuilderNamesRules() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(openBuilder(app));
        QVERIFY(!addRuleRow("Starred", "is").isEmpty());
        QVERIFY(!addRuleRow("Note", "has").isEmpty());
        QVERIFY(pressBuilderForChange("ruleAdd:0", Qt::AltModifier));
        QVERIFY(waitUntil([] { return ruleRowShown("1.0"); }));

        const QList<QPair<QString, QString>> expected{
            {"ruleProp:0", "Rule 1 property"},     {"ruleOp:0", "Rule 1 operator"},
            {"ruleAdd:0", "Add rule after rule 1"}, {"ruleRemove:0", "Remove rule 1"},
            {"ruleProp:1.0", "Rule 2.1 property"}, {"ruleOp:1.0", "Rule 2.1 operator"},
            {"ruleAdd:1.0", "Add rule after rule 2.1"}, {"ruleRemove:1.0", "Remove rule 2.1"},
            {"ruleGroupRemove:1", "Remove rule 2"}};
        for (const auto &[objectName, name] : expected) {
            QVERIFY2(tk::waitItem(objectName), qPrintable("no " + objectName));
            QCOMPARE(axName(objectName), name);
        }
        // A rule with a value combo names it too.
        if (findItem("ruleValue:1.0"))
            QCOMPARE(axName("ruleValue:1.0"), QString("Rule 2.1 value"));

        // Match All/Any: radio buttons inside a group named "Match".
        QCOMPARE(axName("ruleMatch:"), QString("Match"));
        QCOMPARE(axRole("ruleMatch:"), QAccessible::Grouping);
        QCOMPARE(axName("ruleMatch:1"), QString("Match, rule 2"));
        QCOMPARE(axRole("ruleMatch:1"), QAccessible::Grouping);
        for (const QString &path : {QString(), QString("1")}) {
            QQuickItem *all = tk::waitItem("ruleMatchAll:" + path);
            QVERIFY(all);
            QCOMPARE(axRole("ruleMatchAll:" + path), QAccessible::RadioButton);
            QCOMPARE(axRole("ruleMatchAny:" + path), QAccessible::RadioButton);
            QCOMPARE(axName("ruleMatchAll:" + path), QString("All"));
            QCOMPARE(axName("ruleMatchAny:" + path), QString("Any"));
            QVERIFY(iface("ruleMatchAll:" + path)->state().checked);
            QVERIFY(!iface("ruleMatchAny:" + path)->state().checked);
            QCOMPARE(QAccessible::queryAccessibleInterface(all->parentItem())->text(QAccessible::Name),
                     path.isEmpty() ? QString("Match") : QString("Match, rule 2"));
        }
        QVERIFY(closeBuilder(app));
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

        // The command popup (Defer bar), the shared palette behind the
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

    // ------------------------------------------------------- announcements

    // Completing, reopening and completing a whole selection each say what
    // happened, with the task's title or the count.
    void completionAndReopenAreAnnounced() {
        App app;
        QVERIFY(app.ok);
        AnnouncementLog log;
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));

        // One task, completed and reopened.
        app.backend().toggleComplete(app.ids.buyMilk);
        QVERIFY(waitUntil([&] { return log.saw("Completed: Buy milk"); }));
        QCOMPARE(log.entries.last().politeness, QAccessible::AnnouncementPoliteness::Polite);
        app.backend().setCompleted(false);
        QVERIFY(waitUntil([&] { return log.saw("Reopened: Buy milk"); }));

        // Three tasks at once read as a count.
        QVERIFY(tk::selectRow(app, app.ids.buyMilk));
        tk::clickWith(app.row(app.ids.electricity), Qt::ControlModifier);
        tk::clickWith(app.row(app.ids.plants), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 3, 5000);
        app.backend().toggleComplete();
        QVERIFY(waitUntil([&] { return log.saw("Completed 3 tasks"); }));
    }

    // Moving, deleting, copying and pasting name what changed.
    void moveDeleteCopyAndPasteAreAnnounced() {
        App app;
        QVERIFY(app.ok);
        AnnouncementLog log;
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));

        app.backend().moveSelectionToList(app.ids.work);
        QVERIFY(waitUntil([&] { return log.saw("Moved to Work"); }));

        // Back to Home for the delete and paste below. Buy milk has left, so
        // the shelf stands in for it.
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(tk::selectRow(app, app.ids.shelf));
        app.backend().deleteSelection();
        QVERIFY(waitUntil([&] { return log.saw("Deleted: Fix the shelf"); }));

        // Copy two tasks, then paste them back.
        QVERIFY(tk::selectRow(app, app.ids.plants));
        tk::clickWith(app.row(app.ids.electricity), Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().selectionCount(), 2, 5000);
        app.backend().copySelection();
        QVERIFY(waitUntil([&] { return log.saw("Copied 2 tasks"); }));
        app.backend().pasteTasks();
        QVERIFY(waitUntil([&] { return log.saw("Pasted 2 tasks"); }));
    }

    // Undo and redo say the action's own name.
    void undoAndRedoAreAnnounced() {
        App app;
        QVERIFY(app.ok);
        AnnouncementLog log;
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));

        app.backend().toggleComplete(app.ids.buyMilk);
        QVERIFY(waitUntil([&] { return app.task(app.ids.buyMilk).completed; }));
        app.backend().undo();
        QVERIFY(waitUntil([&] { return log.saw("Undone: Mark as Completed"); }));
        QVERIFY(!app.task(app.ids.buyMilk).completed);
        app.backend().redo();
        QVERIFY(waitUntil([&] { return log.saw("Redone: Mark as Completed"); }));
        QVERIFY(app.task(app.ids.buyMilk).completed);
    }

    // A sync that fails is announced assertively, with the provider's reason.
    void syncFailureIsAnnouncedAssertively() {
        auto seed = [](Store &store) {
            // A WebDAV server that will never be reached, so a run fails at once.
            store.setSetting("webdav.url", "ftp://example.invalid/dav/");
            store.setSetting("webdav.user", "alice");
            store.setSetting("sync.auto", false);
            return seedWorld(store);
        };
        App app(seed, QString(), true);
        QVERIFY(app.ok);
        QVERIFY(app.services().webDavSync);
        QVERIFY(app.services().webDavSync->configured());
        AnnouncementLog log;

        app.services().webDavSync->syncNow();
        QVERIFY(waitUntil([&] { return log.saw("Sync failed: "); }));
        for (const auto &e : log.entries)
            if (e.message.startsWith("Sync failed: "))
                QCOMPARE(e.politeness, QAccessible::AnnouncementPoliteness::Assertive);
    }

    // A mail connector error is announced assertively as "Mail: ...".
    void mailErrorIsAnnouncedAssertively() {
        FakeImap server;
        server.add("INBOX", plusMail("One", "m1"));
        App app;
        QVERIFY(app.ok);
        AnnouncementLog log;
        auto *mail = app.services().mail.get();
        QVERIFY(mail);
        mail->setOption("mail.account", "other");
        mail->setOption("mail.address", "jane@example.org");
        mail->setOption("mail.host", "127.0.0.1");
        mail->setOption("mail.port", QString::number(server.port()));
        mail->setOption("mail.security", "none");
        mail->setOption("mail.user", "jane");
        mail->setOption("mail.toFilter", "jane+fairfax@example.org");
        mail->setPassword("wrong");
        mail->setOption("mail.enabled", "true");

        mail->fetchNow();
        QVERIFY(waitUntil([&] { return log.saw("Mail: "); }, 10000));
        for (const auto &e : log.entries)
            if (e.message.startsWith("Mail: "))
                QCOMPARE(e.politeness, QAccessible::AnnouncementPoliteness::Assertive);
    }

    // A proposal applied or dismissed from the review sheet is announced, with
    // the task's title on an apply.
    void proposalReviewIsAnnounced() {
        auto seed = [](Store &store) {
            Seed ids = seedWorld(store);
            store.setSetting("agent.enabled", true);
            return ids;
        };
        App app(seed);
        QVERIFY(app.ok);
        AnnouncementLog log;
        // "Fix the shelf" has no due date yet.
        Proposal p;
        p.kind = "set-due-date";
        p.payload = {{"taskId", app.ids.shelf}, {"due", "2026-08-01"}};
        app.store().begin("add proposal");
        p.id = app.store().nextId("proposals");
        app.store().putProposal(p);
        app.store().commit();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 1, 5000);

        QVERIFY(app.backend().applyProposal(p.id));
        QVERIFY(waitUntil([&] { return log.saw("Applied: Fix the shelf"); }));
        QCOMPARE(log.entries.last().politeness, QAccessible::AnnouncementPoliteness::Polite);

        // Dismissing a second one says so.
        Proposal q;
        q.kind = "set-due-date";
        q.payload = {{"taskId", app.ids.plants}, {"due", "2026-08-02"}};
        app.store().begin("add proposal");
        q.id = app.store().nextId("proposals");
        app.store().putProposal(q);
        app.store().commit();
        app.backend().dismissProposal(q.id);
        QVERIFY(waitUntil([&] { return log.saw("Suggestion dismissed"); }));
    }

    // The proposal sheet's controls are all named: the rows, the per-row Apply
    // and Dismiss buttons, and the sheet's own footer.
    void proposalSheetIsNamed() {
        auto seed = [](Store &store) {
            Seed ids = seedWorld(store);
            store.setSetting("agent.enabled", true);
            return ids;
        };
        App app(seed);
        QVERIFY(app.ok);
        Proposal p;
        p.kind = "set-due-date";
        p.payload = {{"taskId", app.ids.shelf}, {"due", "2026-08-01"}};
        app.store().begin("add proposal");
        p.id = app.store().nextId("proposals");
        app.store().putProposal(p);
        app.store().commit();
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().proposalCount(), 1, 5000);

        QQuickItem *button = tk::waitItem("suggestionsButton");
        QVERIFY(button);
        QTest::mouseMove(app.window, centreOf(button));
        QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centreOf(button));
        QQuickItem *list = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((list = findItem("proposalList")) != nullptr, 5000);
        QCOMPARE(list->property("count").toInt(), 1);
        settleWindow(app.window);
        checkState("proposal sheet");
        QVERIFY(tk::clickNamed("proposalClose"));
    }
};

REGISTER_TEST(E2EAccessibility)

#include "test_accessibility.moc"
