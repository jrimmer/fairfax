#pragma once

// Helpers shared by the task end-to-end tests (test_tasks_*.cpp): finding and
// clicking menu entries, opening and closing the inline editor, typing into a
// field the way a user focuses it, reading a row's section and badges, and
// driving the Quick Entry window. Everything waits on a condition rather than
// sleeping, and returns false (with a note on stderr) instead of hanging.

#include "harness.h"

namespace e2e {
namespace tk {

inline bool waitFor(const std::function<bool()> &pred, int ms = 5000) { return QTest::qWaitFor(pred, ms); }

// A property of a named item, or an invalid variant when the item is not on screen.
inline QVariant prop(const QString &name, const char *property) {
    QQuickItem *i = findItem(name);
    return i ? i->property(property) : QVariant();
}

inline QQuickItem *waitItem(const QString &name, int ms = 5000) {
    QQuickItem *item = nullptr;
    waitFor([&] { return (item = findItem(name)) != nullptr; }, ms);
    return item;
}

// Waits until a named item has stopped moving, so a click is aimed at where it
// really is and not where it was a moment ago.
inline bool settled(const QString &name, int ms = 5000) {
    QPointF last(-1, -1);
    int same = 0;
    // Positions only mean something once the layout that has been asked for has run.
    waitFor([&] { return findItem(name) != nullptr; }, ms);
    settleLayout(findItem(name));
    return waitFor([&] {
        QQuickItem *i = findItem(name);
        if (!i) return false;
        const QPointF now = i->mapToScene(QPointF(0, 0));
        same = (now == last) ? same + 1 : 0;
        last = now;
        return same >= 3;
    }, ms);
}

// Scrolls a long menu (the task menu is taller than the window) until the
// entry is on screen, with the mouse wheel over the menu.
inline void scrollIntoView(QQuickItem *entry) {
    QQuickWindow *w = entry->window();
    for (int i = 0; i < 40; ++i) {
        const QPointF p = entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2));
        if (p.y() > 6 && p.y() < w->height() - 6) return;
        const bool down = p.y() >= w->height() - 6;
        QWheelEvent ev(QPointF(p.x(), w->height() / 2), w->mapToGlobal(QPointF(p.x(), w->height() / 2)), QPoint(), QPoint(0, down ? -120 : 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(w, &ev);
        QTest::qWait(30);
    }
}

// The visible entries of every open menu, for failure messages.
inline QString menuEntries() {
    QStringList seen;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if (i->inherits("QQuickMenuItem")) seen << i->property("text").toString();
            return false;
        });
    return seen.join(" | ");
}

inline bool noMenuOpen() {
    bool any = false;
    for (QQuickWindow *w : quickWindows())
        findIn(w->contentItem(), [&](QQuickItem *i) {
            if (i->inherits("QQuickMenuItem") && i->width() > 0) any = true;
            return false;
        });
    return !any;
}

// Rows and section headers currently on screen, top to bottom.
struct Placed { QString name; qreal y; };
inline QList<Placed> listLayout() {
    QList<Placed> out;
    QQuickItem *list = findItem("taskList");
    if (!list) return out;
    findIn(list, [&](QQuickItem *i) {
        const QString n = i->objectName();
        if (n.startsWith("taskRow:") || n.startsWith("section:"))
            out.append({n, i->mapToScene(QPointF(0, 0)).y()});
        return false;
    });
    std::sort(out.begin(), out.end(), [](const Placed &a, const Placed &b) { return a.y < b.y; });
    return out;
}

// Who would take a press at the centre of a named item, for failure messages:
// the item the mouse grab lands on, and its parents.
inline QString pressTarget(const QString &name) {
    QQuickItem *i = findItem(name);
    if (!i) return "no such item: " + name;
    QQuickWindow *w = i->window();
    const QPoint p = centreOf(i);
    QTest::mouseMove(w, p);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, p);
    QStringList chain;
    for (QQuickItem *x = w->mouseGrabberItem(); x && chain.size() < 6; x = x->parentItem())
        chain << className(x) + "[" + x->objectName() + "]";
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, p);
    return QString("press at (%1,%2) of %3 goes to ").arg(p.x()).arg(p.y()).arg(name) + chain.join(" < ");
}

inline bool clickNamed(const QString &name, Qt::MouseButton b = Qt::LeftButton, int ms = 5000) {
    if (!waitItem(name, ms) || !settled(name, ms)) {
        qWarning().noquote() << "no item named" << name;
        return false;
    }
    click(findItem(name), b);
    return true;
}

// A slower drag than the harness's: many small moves, then a few at the drop
// point so the drag proxy has caught up with the pointer before the release.
inline void dragSlowly(QQuickItem *from, QQuickItem *to) {
    QQuickWindow *w = from->window();
    const QPoint a = centreOf(from), b = centreOf(to);
    QTest::mouseMove(w, a);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, a);
    const int steps = 40;
    for (int i = 1; i <= steps; ++i) {
        QTest::mouseMove(w, a + (b - a) * i / steps);
        QTest::qWait(5);
    }
    for (int i = 0; i < 3; ++i) {
        QTest::mouseMove(w, b);
        QTest::qWait(20);
    }
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, b);
}

// A click with a keyboard modifier held (Shift or Ctrl-click to extend a selection).
inline void clickWith(QQuickItem *item, Qt::KeyboardModifiers mods, qreal fx = 0.4, qreal fy = 0.5) {
    QQuickWindow *w = item->window();
    const QPoint p = pointIn(item, fx, fy);
    QTest::mouseMove(w, p);
    QTest::mouseClick(w, Qt::LeftButton, mods, p);
}

// Selects a row with a click on its title and waits for the list to hold the
// keyboard focus, as a user who is about to press a bare key would.
inline bool selectRow(App &app, qint64 id) {
    const QString name = QString("taskRow:%1").arg(id);
    if (!waitItem(name) || !settled(name)) return false;
    QQuickItem *row = app.row(id);
    click(row, Qt::LeftButton, pointIn(row, 0.4, 0.5));
    return waitFor([&] { return app.backend().currentTaskId() == id; })
           && waitFor([&] { QQuickItem *l = findItem("taskList"); return l && l->property("activeFocus").toBool(); });
}

inline bool contextMenuOn(App &app, qint64 id) {
    // The task menu has thirty entries, more than the window is tall; a taller
    // window lets every entry (and submenu) be reached with the mouse.
    if (app.window->height() < 1300) {
        app.window->resize(app.window->width(), 1300);
        waitFor([&] { return app.window->height() >= 1300; });
    }
    const QString name = QString("taskRow:%1").arg(id);
    if (!waitItem(name) || !settled(name)) return false;
    QQuickItem *row = app.row(id);
    click(row, Qt::RightButton, pointIn(row, 0.4, 0.5));
    return waitFor([] { return findText("Duplicate", "QQuickMenuItem") != nullptr; });
}

// Picks an entry of the menu that is open and waits for the menu to close.
inline bool clickMenu(const QString &text, int ms = 5000) {
    QQuickItem *item = nullptr;
    if (!waitFor([&] { return (item = findText(text, "QQuickMenuItem")) != nullptr; }, ms)) {
        qWarning().noquote() << "no menu entry" << text << "among:" << menuEntries();
        return false;
    }
    // The popup that has just opened is laid out on the next polish; until then
    // its entries sit at stale positions.
    settleLayout(item);
    if (!(item = findText(text, "QQuickMenuItem"))) return false;
    scrollIntoView(item);
    if (!(item = findText(text, "QQuickMenuItem"))) return false;
    click(item);
    if (waitFor([&] { return noMenuOpen(); }, ms / 3)) return true;
    // The click can land while the menu is still opening; once more.
    if ((item = findText(text, "QQuickMenuItem"))) click(item);
    return waitFor([&] { return noMenuOpen(); }, ms);
}

// Opens a submenu by hovering its entry (clicking if hovering did not do it),
// then picks `child` from it.
inline bool clickSub(const QString &parentText, const QString &child, int ms = 5000) {
    QQuickItem *entry = nullptr;
    if (!waitFor([&] { return (entry = findText(parentText, "QQuickMenuItem")) != nullptr; }, ms)) {
        qWarning().noquote() << "no menu entry" << parentText << "among:" << menuEntries();
        return false;
    }
    QQuickItem *target = nullptr;
    auto found = [&] { return (target = findText(child, "QQuickMenuItem")) != nullptr; };
    auto parentEntry = [&] { return entry = findText(parentText, "QQuickMenuItem"); };
    settleLayout(entry);
    if (!parentEntry()) return false;
    scrollIntoView(entry);
    if (!parentEntry()) return false;
    QTest::mouseMove(entry->window(), centreOf(entry));
    // Hovering does not always open it (offscreen there is no real pointer); a
    // click on the entry does, and does not close it if the hover was merely late.
    if (!waitFor(found, 400)) {
        if (!parentEntry()) return false;
        click(entry);
        if (!waitFor(found, 3000)) {
            qWarning().noquote() << "submenu" << parentText << "has no" << child << "among:" << menuEntries();
            return false;
        }
    }
    // The submenu that has just opened is laid out on the next polish.
    settleLayout(target);
    if (!found()) return false;
    scrollIntoView(target);
    if (!found()) return false;
    click(target);
    return waitFor([&] { return noMenuOpen(); }, ms);
}

// Every Return is a plain key press, so a multi-line text types with it.
inline void typeLine(QWindow *w, const QString &text) {
    for (const QChar c : text) {
        if (c == '\n') QTest::keyClick(w, Qt::Key_Return);
        else QTest::keyClick(w, c.toLatin1());
    }
}

// Clicks a text field (the way a user puts the caret in it), clears it when
// asked and types.
inline bool typeIn(const QString &name, const QString &text, bool replace = true) {
    if (!waitItem(name) || !settled(name)) {
        qWarning().noquote() << "no field named" << name;
        return false;
    }
    click(findItem(name));
    if (!waitFor([&] { return prop(name, "activeFocus").toBool(); }, 1500)) {
        qWarning().noquote() << name << "did not take focus";
        return false;
    }
    QWindow *w = findItem(name)->window();
    if (replace) {
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        if (text.isEmpty()) QTest::keyClick(w, Qt::Key_Delete);
    }
    typeLine(w, text);
    return true;
}

inline QStringList tagNames(App &app, const Task &t) {
    QStringList out;
    for (qint64 id : t.tagIds) out << app.store().tag(id).name;
    out.sort();
    return out;
}

inline QVector<Alarm> alarmsOf(App &app, qint64 id) { return app.store().alarmsFor(id); }

// The next given weekday (Qt: Monday = 1) strictly after today, as the date parser reads "fri".
inline QDate nextWeekday(int dayOfWeek) {
    const QDate today = QDate::currentDate();
    int delta = (dayOfWeek - today.dayOfWeek() + 7) % 7;
    return today.addDays(delta == 0 ? 7 : delta);
}

inline QString isoOf(const QDate &d) { return d.toString(Qt::ISODate); }

// ---------------------------------------------------------------- the list

// The key of the section a task row sits under ("overdue", "d:2026-10-12",
// "nodue", ...), or an empty string when the row is not on screen.
inline QString sectionOf(qint64 id) {
    QString section;
    const QString want = QString("taskRow:%1").arg(id);
    for (const Placed &p : listLayout()) {
        if (p.name.startsWith("section:")) section = p.name.mid(8);
        else if (p.name == want) return section;
    }
    return {};
}

inline bool rowVisible(App &app, qint64 id) { return app.row(id) != nullptr; }
inline bool rowGone(App &app, qint64 id) { return app.row(id) == nullptr; }

// A named badge on a task row ("rowAlarm", "rowDuration", ...).
inline bool badge(const QString &kind, qint64 id) { return findItem(QString("%1:%2").arg(kind).arg(id)) != nullptr; }
inline QString rowText(const QString &kind, qint64 id) {
    QQuickItem *i = findItem(QString("%1:%2").arg(kind).arg(id));
    return i ? i->property("text").toString() : QString();
}

// ------------------------------------------------------------- the editor

inline bool openEditor(App &app, qint64 id) {
    const QString name = QString("taskRow:%1").arg(id);
    if (!waitItem(name) || !settled(name)) {
        qWarning().noquote() << "no row for task" << id << "titles:" << app.titles();
        return false;
    }
    QQuickItem *row = app.row(id);
    doubleClick(row, pointIn(row, 0.4, 0.5));
    const bool opened = waitFor([&] {
        QQuickItem *t = findItem("editorTitle");
        return t && app.backend().editor().value("id").toLongLong() == id;
    });
    return opened;
}

// Leaves the editor the way the reference app's Ctrl+Return does.
inline bool closeEditor(App &app) {
    app.press(Qt::Key_Return, Qt::ControlModifier);
    return waitFor([&] { return !findItem("editorCard"); });
}

// Puts the caret into a date field the way a keyboard user does: in the title
// and Tab along until the field has it. (A click would also open the calendar
// under the field, which is what pickDay is for.)
inline bool focusDateField(const QString &name) {
    auto focused = [&] { return prop(name, "activeFocus").toBool(); };
    const QString titleName = findItem("editorTitle") ? "editorTitle" : "quickTitle";
    // A field that has only just changed (the editor refreshes after every
    // edit) can lose the caret to the rebuild; the walk is repeated from the
    // title when it has not arrived.
    for (int round = 0; round < 3 && !focused(); ++round) {
        QQuickItem *f = waitItem(name, 5000);
        if (!f) return false;
        settleLayout(f);
        if (!waitItem(titleName, 5000)) return false;
        settled(titleName, 5000);
        click(findItem(titleName));
        waitFor([&] { return prop(titleName, "activeFocus").toBool(); }, 3000);
        f = findItem(name);
        if (!f) return false;
        QQuickWindow *w = f->window();
        for (int i = 0; i < 25 && !focused(); ++i) {
            QTest::keyClick(w, Qt::Key_Tab);
            waitFor(focused, 40);
        }
    }
    return focused();
}

// Types a date or time into one of the editor's (or Quick Entry's) fields and
// confirms it with Return, as the manual describes.
inline bool typeDate(const QString &name, const QString &text) {
    if (!focusDateField(name)) {
        qWarning().noquote() << name << "did not take focus";
        return false;
    }
    QWindow *w = findItem(name)->window();
    QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
    typeLine(w, text);
    QTest::keyClick(w, Qt::Key_Return);
    return true;
}

// Opens the calendar by clicking a field. A click outside an open popup first
// closes that popup (a date suggestion list, say), so it is tried again.
inline bool openCalendar(const QString &fieldName) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!clickNamed(fieldName)) return false;
        if (waitFor([] { return findItem("pickerChip:Today") != nullptr; }, 1000)) return true;
    }
    return false;
}

// Picks a day in the calendar that a click on the field opens.
inline bool pickDay(const QString &fieldName, const QDate &day) {
    if (!openCalendar(fieldName)) return false;
    QQuickItem *cell = waitItem("pickerDay:" + isoOf(day));
    if (!cell) return false;
    click(cell);
    return waitFor([&] { return !findItem("pickerChip:Today"); });
}

inline bool pickChip(const QString &fieldName, const QString &chip) {
    if (!openCalendar(fieldName)) return false;
    QQuickItem *c = waitItem("pickerChip:" + chip);
    if (!c) return false;
    click(c);
    return waitFor([&] { return !findItem("pickerChip:Today"); });
}

// Clicks a small control until the thing it removes is gone (the first click
// may only close a popup that was open).
inline bool clickUntilGone(const QString &control, const QString &gone) {
    for (int attempt = 0; attempt < 3 && findItem(gone); ++attempt) {
        if (QQuickItem *c = findItem(control)) click(c);
        waitFor([&] { return !findItem(gone); }, 2000);
    }
    return !findItem(gone);
}

// ------------------------------------------------------------ Quick Entry

inline QQuickWindow *openQuickEntry(App &app) {
    app.press(Qt::Key_2, Qt::ShiftModifier | Qt::ControlModifier);
    QQuickWindow *quick = nullptr;
    if (!waitFor([&] { return (quick = windowTitled("Quick Entry")) != nullptr && quick->isExposed(); }))
        return nullptr;
    quick->requestActivate();
    if (!waitItem("quickTitle")) return nullptr;
    return quick;
}

}  // namespace tk
}  // namespace e2e
