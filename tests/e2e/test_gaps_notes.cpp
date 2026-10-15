// End-to-end gap tests: the notes area of the inline editor. Its right-click
// menu (Cut, Copy, Paste, Select All, Insert Time Stamp, Copy Links from
// Notes), the stamp and link buttons of its left column, Ctrl+click on a link
// and the task-link picker that "@{" opens.

#include <QDesktopServices>

#include "gapskit2.h"
#include "markdown.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::g2;

namespace {

QStringList &seen() {
    static QStringList urls;
    return urls;
}

class UrlSink : public QObject {
    Q_OBJECT
public slots:
    void opened(const QUrl &url) { seen() << QString::fromUtf8(url.toEncoded()); }
};

QQuickItem *notes() { return findItem("notesText"); }
QString notesText() { return notes() ? notes()->property("text").toString() : QString(); }

// Right-clicks the notes and picks an entry of the menu that opens.
bool fromContextMenu(const QString &entry) {
    if (!notes() || !settled("notesText"))
        return false;
    click(notes(), Qt::RightButton);
    return clickMenu(entry);
}

// The point on screen where the character at `position` of the notes is drawn.
QPoint pointOfCharacter(int position) {
    QQuickItem *area = notes();
    QRectF r;
    QMetaObject::invokeMethod(area, "positionToRectangle", Q_RETURN_ARG(QRectF, r), Q_ARG(int, position));
    return area->mapToScene(r.center()).toPoint();
}

// Ctrl+click on the character at `position`.
void ctrlClickCharacter(int position) {
    QQuickWindow *w = notes()->window();
    const QPoint p = pointOfCharacter(position);
    QTest::mouseMove(w, p);
    QTest::mouseClick(w, Qt::LeftButton, Qt::ControlModifier, p);
}

// A menu entry whose text starts with `prefix` (the stamp entries carry a
// tab and today's example after their name).
QString stampEntry(int format) {
    return Markdown::stampNames().at(format) + "\t" + Markdown::stamp(format, QDateTime::currentDateTime());
}

}  // namespace

class E2EGapsNotes : public QObject {
    Q_OBJECT
    UrlSink m_sink;

    static QString homeKey(App &app) { return "list:" + QString::number(app.ids.home); }

private slots:
    void initTestCase() {
        for (const char *scheme : {"mailto", "http", "https"})
            QDesktopServices::setUrlHandler(scheme, &m_sink, "opened");
    }
    void cleanupTestCase() {
        for (const char *scheme : {"mailto", "http", "https"})
            QDesktopServices::unsetUrlHandler(scheme);
    }
    void init() { seen().clear(); }

    // The notes' right-click menu has the usual edit commands: Select All
    // selects the notes, Copy and Cut put the selection on the clipboard (Cut
    // also removes it, and both wait for a selection), Paste puts it back.
    void contextMenuCutCopyPasteSelectAll() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(tk::typeIn("notesText", "alpha beta"));

        // Nothing is selected yet, so there is nothing to cut or copy.
        QVERIFY(settled("notesText"));
        click(notes(), Qt::RightButton);
        QVERIFY(waitFor([] { return findText("Select All", "QQuickMenuItem") != nullptr; }));
        QVERIFY(!entryEnabled("Cut"));
        QVERIFY(!entryEnabled("Copy"));
        QVERIFY(entryEnabled("Paste"));
        QVERIFY(clickMenu("Select All"));
        QTRY_COMPARE_WITH_TIMEOUT(notes()->property("selectedText").toString(), QString("alpha beta"), 5000);

        setClipboard("untouched");
        QVERIFY(fromContextMenu("Copy"));
        QTRY_COMPARE_WITH_TIMEOUT(clipboardText(), QString("alpha beta"), 5000);
        QCOMPARE(notesText(), QString("alpha beta"));   // Copy leaves the text alone.

        setClipboard("untouched");
        QVERIFY(fromContextMenu("Cut"));
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), QString(), 5000);
        QCOMPARE(clipboardText(), QString("alpha beta"));

        QVERIFY(fromContextMenu("Paste"));
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), QString("alpha beta"), 5000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString("alpha beta"), 5000);

        // Cut again to take the notes off the task.
        QVERIFY(openEditor(app, id));
        QVERIFY(fromContextMenu("Select All"));
        QVERIFY(fromContextMenu("Cut"));
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString(), 5000);
    }

    // "Insert Time Stamp…" in the menu (and the clock button beside the notes)
    // offers the stamp formats with today's example; the choice goes in at the
    // caret, and replaces a selection.
    void timeStampFromMenuAndButton() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(tk::typeIn("notesText", "seen "));

        QVERIFY(settled("notesText"));
        click(notes(), Qt::RightButton);
        QVERIFY(pickEntry("Insert Time Stamp…"));
        QVERIFY(clickMenu(stampEntry(4)));   // Universal Date: yyyy-MM-dd.
        const QString today = QDate::currentDate().toString("yyyy-MM-dd");
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), "seen " + today + " ", 5000);

        // The button replaces what is selected with the weekday's name.
        click(notes());
        app.press(Qt::Key_A, Qt::ControlModifier);
        QVERIFY(clickNamed("notesStamp"));
        QVERIFY(clickMenu(stampEntry(8)));
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), Markdown::stamp(8, QDateTime::currentDateTime()) + " ", 5000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, Markdown::stamp(8, QDateTime::currentDateTime()) + " ", 5000);

        // Clear it again.
        QVERIFY(openEditor(app, id));
        QVERIFY(tk::typeIn("notesText", ""));
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString(), 5000);
    }

    // The link button lists the links found in the notes, and choosing one
    // opens it; Copy Links from Notes puts them all on the clipboard.
    void linkMenuOpensAndCopiesLinks() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(!findItem("notesLinks"));   // No links, no button.
        QVERIFY(tk::typeIn("notesText", "Docs https://example.com/a and bob@example.com"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("notesLinks") != nullptr, 5000);

        QVERIFY(clickNamed("notesLinks"));
        QVERIFY2(waitFor([] { return findText("Open Links (2)", "QQuickMenuItem") != nullptr; }), qPrintable(tk::menuEntries()));
        QVERIFY(clickMenu("https://example.com/a"));
        QTRY_COMPARE_WITH_TIMEOUT(seen(), QStringList{"https://example.com/a"}, 5000);

        QVERIFY(clickNamed("notesLinks"));
        QVERIFY(clickMenu("Mail: bob@example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(seen(), (QStringList{"https://example.com/a", "mailto:bob@example.com"}), 5000);

        setClipboard("untouched");
        QVERIFY(fromContextMenu("Copy Links from Notes"));
        QTRY_COMPARE_WITH_TIMEOUT(clipboardText(), QString("https://example.com/a\nbob@example.com"), 5000);

        // Without the links the button goes and the menu entry waits.
        QVERIFY(tk::typeIn("notesText", "nothing to follow"));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("notesLinks"), 5000);
        QVERIFY(settled("notesText"));
        click(notes(), Qt::RightButton);
        QVERIFY(waitFor([] { return findText("Copy Links from Notes", "QQuickMenuItem") != nullptr; }));
        QVERIFY(!entryEnabled("Copy Links from Notes"));
        QVERIFY(dismissMenu());
        QVERIFY(closeEditor(app));
    }

    // "Ctrl+click a link in the notes to open it": a plain click only puts the
    // caret there.
    void ctrlClickOnALinkOpensIt() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        mk::prep(app, id, [](Task &t) { t.notes = "go to https://example.com/x now"; });
        QVERIFY(openEditor(app, id));
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), QString("go to https://example.com/x now"), 5000);
        QVERIFY(settled("notesText"));

        QQuickWindow *w = notes()->window();
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, pointOfCharacter(12));
        QTest::qWait(50);
        QVERIFY(seen().isEmpty());

        ctrlClickCharacter(1);   // On "go", not on the link.
        QTest::qWait(50);
        QVERIFY(seen().isEmpty());

        ctrlClickCharacter(12);
        QTRY_COMPARE_WITH_TIMEOUT(seen(), QStringList{"https://example.com/x"}, 5000);
        QVERIFY(closeEditor(app));
    }

    // Ctrl+click on an @{task link} goes to that task.
    void ctrlClickOnATaskLinkGoesToTheTask() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        mk::prep(app, id, [](Task &t) { t.notes = "after @{Water the plants}"; });
        QVERIFY(openEditor(app, id));
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), QString("after @{Water the plants}"), 5000);
        QVERIFY(settled("notesText"));
        ctrlClickCharacter(10);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), app.ids.plants, 5000);
        QVERIFY(seen().isEmpty());   // Nothing is handed to the desktop.
    }

    // Typing "@{" opens the task picker: Down and Up move through it, Escape
    // closes it (and leaves the editor open), a click on a row finishes the link.
    void taskLinkPicker() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(tk::typeIn("notesText", "see @{"));
        QObject *picker = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((picker = findObject("linkPicker")) != nullptr && picker->property("opened").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("linkPickerRow:1") != nullptr, 5000);
        QCOMPARE(picker->property("index").toInt(), 0);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(picker->property("index").toInt(), 1, 5000);
        app.press(Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(picker->property("index").toInt(), 0, 5000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!picker->property("opened").toBool(), 5000);
        QVERIFY(findItem("editorCard"));
        QCOMPARE(notesText(), QString("see @{"));

        // Typing more brings it back, narrowed to what matches.
        app.type("Wat");
        QTRY_VERIFY_WITH_TIMEOUT(picker->property("opened").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("linkPickerRow:0") && !findItem("linkPickerRow:1"), 5000);
        QVERIFY(textContaining(findItem("linkPickerRow:0"), "Water the plants") != nullptr);
        QVERIFY(settled("linkPickerRow:0"));
        click(findItem("linkPickerRow:0"));
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), QString("see @{Water the plants}"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!picker->property("opened").toBool(), 5000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString("see @{Water the plants}"), 5000);

        // Take the link out again.
        QVERIFY(openEditor(app, id));
        QVERIFY(tk::typeIn("notesText", ""));
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString(), 5000);
    }

    // Return on a picker row finishes the link too, from the keyboard.
    void taskLinkPickerChosenWithReturn() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(tk::typeIn("notesText", "@{Book"));
        QObject *picker = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((picker = findObject("linkPicker")) != nullptr && picker->property("opened").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("linkPickerRow:0") != nullptr, 5000);
        app.press(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), QString("@{Book the studio}"), 5000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString("@{Book the studio}"), 5000);
    }
    // Tab on a picker row finishes the link as Return does, leaving the notes.
    void taskLinkPickerChosenWithTab() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(homeKey(app)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(tk::typeIn("notesText", "@{Book"));
        QObject *picker = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((picker = findObject("linkPicker")) != nullptr && picker->property("opened").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("linkPickerRow:0") != nullptr, 5000);
        app.press(Qt::Key_Tab);
        QTRY_COMPARE_WITH_TIMEOUT(notesText(), QString("@{Book the studio}"), 5000);
        QVERIFY(closeEditor(app));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).notes, QString("@{Book the studio}"), 5000);
    }
};

REGISTER_TEST(E2EGapsNotes)

#include "test_gaps_notes.moc"
