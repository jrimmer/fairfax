#pragma once

// Helpers for the editor, Quick Look, notes and settings gap tests
// (test_gaps_editor.cpp, test_gaps_notes.cpp, test_gaps_settings.cpp): reading
// the clipboard, picking a menu entry that opens another menu, putting
// attachments on a task without the file chooser, and the typed-in field of
// the notes.

#include <QClipboard>
#include <QDesktopServices>
#include <QFile>

#include "misckit.h"

namespace e2e {
namespace g2 {

using tk::clickMenu;
using tk::clickNamed;
using tk::clickSub;
using tk::closeEditor;
using tk::openEditor;
using tk::prop;
using tk::settled;
using tk::steadyMenuItem;
using tk::waitFor;
using tk::waitItem;

inline QString clipboardText() { return QGuiApplication::clipboard()->text(); }
inline void setClipboard(const QString &text) { QGuiApplication::clipboard()->setText(text); }

// A menu entry that opens another menu (or a dialog) instead of closing the
// menu: clickMenu would wait for no menu to be open at all.
inline bool pickEntry(const QString &text) {
    QQuickItem *item = steadyMenuItem(text);
    if (!item) {
        qWarning().noquote() << "no menu entry" << text << "among:" << tk::menuEntries();
        return false;
    }
    click(item);
    return true;
}

// Closes the open menu with Escape, sent to the window that holds it.
inline bool dismissMenu() {
    QQuickItem *entry = nullptr;
    for (QQuickWindow *w : quickWindows())
        if ((entry = findIn(w->contentItem(), [](QQuickItem *i) { return i->inherits("QQuickMenuItem") && i->width() > 0; })))
            break;
    if (!entry)
        return true;
    QTest::keyClick(entry->window(), Qt::Key_Escape);
    return waitFor([] { return tk::noMenuOpen(); });
}

// Whether a menu entry is on screen and can be chosen.
inline bool entryEnabled(const QString &text) {
    QQuickItem *item = steadyMenuItem(text);
    return item && item->isEnabled();
}

// An attachment record with its file in the store's attachments folder, as the
// app would have made it, so no file chooser is needed.
inline Attachment attach(App &app, qint64 taskId, int kind, const QString &name, const QByteArray &bytes = "data") {
    Attachment a;
    a.id = app.store().nextId("attachments");
    a.uid = Store::newUid();
    a.taskId = taskId;
    a.kind = kind;
    a.name = name;
    const QString suffix = QFileInfo(name).suffix();
    a.fileName = a.uid + (suffix.isEmpty() ? QString() : "." + suffix);
    a.bytes = bytes.size();
    a.created = a.modified = QDateTime::currentDateTime();
    QDir().mkpath(app.store().attachmentsFolder());
    QFile f(app.store().attachmentsFolder() + "/" + a.fileName);
    f.open(QIODevice::WriteOnly);
    if (kind == Attachment::Image) {
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::darkGreen);
        image.save(&f, "PNG");
    } else {
        f.write(bytes);
    }
    f.close();
    app.store().begin("Prepare", false, true);
    app.store().putAttachment(a);
    app.store().commit();
    return a;
}

inline QVector<Attachment> attachmentsOf(App &app, qint64 task) {
    QVector<Attachment> out;
    for (const Attachment &a : app.store().attachmentsFor(task))
        if (!a.deleted)
            out << a;
    return out;
}

inline QVector<Alarm> liveAlarms(App &app, qint64 task) {
    QVector<Alarm> out;
    for (const Alarm &a : app.store().alarmsFor(task))
        if (!a.deleted)
            out << a;
    return out;
}

// The text of a visible item anywhere under `root` that contains `part`.
inline QQuickItem *textContaining(QQuickItem *root, const QString &part) {
    return root ? findIn(root, [&](QQuickItem *i) {
        return i->width() > 0 && i->metaObject()->indexOfProperty("text") >= 0 && i->property("text").toString().contains(part);
    }) : nullptr;
}

inline QString statusMessage(App &app) { return app.backend().statusMessage(); }

}  // namespace g2
}  // namespace e2e
