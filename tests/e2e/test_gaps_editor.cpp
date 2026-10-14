// End-to-end gap tests: the editor's action, attachment, copy and alert
// controls, Quick Look's copy menu, action bar and audio notes, and the
// record-audio button. Everything the app hands to the desktop (a tel: or
// mailto: link, a web page, a file) is caught by a URL handler instead of
// being opened.

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>

#include "gapskit2.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::g2;

namespace {

// Everything the app asked the desktop to open, in order.
QStringList &seen() {
    static QStringList urls;
    return urls;
}

class UrlSink : public QObject {
    Q_OBJECT
public slots:
    void opened(const QUrl &url) { seen() << QString::fromUtf8(url.toEncoded()); }
};

// A stand-in for PipeWire's pw-record: writes `bytes` into the file it is told
// to record to and then idles until it is terminated.
QString fakeRecorder(const QString &dir, int bytes) {
    const QString path = dir + "/pw-record";
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QString("#!/bin/sh\n%1exec sleep 60\n")
                .arg(bytes > 0 ? QString("head -c %1 /dev/zero > \"$1\"\n").arg(bytes) : QString())
                .toUtf8());
    f.close();
    f.setPermissions(f.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    return path;
}

// Puts a directory in front of PATH (or replaces it) for the length of a test.
class PathGuard {
public:
    // `keepRest` keeps the usual PATH behind the directory; without it the
    // directory is all there is (so no recorder at all).
    PathGuard(const QString &dir, bool keepRest) : m_old(qgetenv("PATH")) {
        qputenv("PATH", keepRest ? dir.toUtf8() + ":" + m_old : dir.toUtf8());
    }
    ~PathGuard() { qputenv("PATH", m_old); }

private:
    QByteArray m_old;
};

}  // namespace

class E2EGapsEditor : public QObject {
    Q_OBJECT
    UrlSink m_sink;

    static void setAction(App &app, qint64 id, int type, const QString &value) {
        mk::prep(app, id, [&](Task &t) { t.actionType = type; t.actionValue = value; });
    }

private slots:
    void initTestCase() {
        for (const char *scheme : {"tel", "sms", "mailto", "http", "https", "file"})
            QDesktopServices::setUrlHandler(scheme, &m_sink, "opened");
    }
    void cleanupTestCase() {
        for (const char *scheme : {"tel", "sms", "mailto", "http", "https", "file"})
            QDesktopServices::unsetUrlHandler(scheme);
    }
    void init() { seen().clear(); }

    // ---------------------------------------------------------- Perform Action

    void performActionOpensTheRightLink_data() {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("call") << int(ActionType::Call) << "5551234" << "tel:5551234";
        QTest::newRow("message") << int(ActionType::Message) << "5551234" << "sms:5551234";
        QTest::newRow("mail") << int(ActionType::Mail) << "alex@example.com" << "mailto:alex@example.com";
        QTest::newRow("visit") << int(ActionType::Visit) << "1 Main St" << "https://www.openstreetmap.org/search?query=1%20Main%20St";
        QTest::newRow("url") << int(ActionType::Url) << "example.com/milk" << "http://example.com/milk";
        QTest::newRow("google") << int(ActionType::Google) << "pocket watch" << "/search?q=pocket%20watch";
    }

    // "Actions: ... tapping the action icon performs it" (task-editor.md): the
    // action icon in the editor ribbon and the action bar in Quick Look each
    // hand the matching link to the desktop (a phone number, a mail address, a
    // map search, a web address or a web search).
    void performActionOpensTheRightLink() {
        QFETCH(int, type);
        QFETCH(QString, value);
        QFETCH(QString, expected);
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        const qint64 id = app.ids.buyMilk;
        setAction(app, id, type, value);
        QTRY_VERIFY_WITH_TIMEOUT(tk::badge("rowAction", id), 5000);
        auto check = [&](const QString &where) {
            QTRY_COMPARE_WITH_TIMEOUT(seen().size(), 1, 5000);
            const QString url = seen().first();
            if (type == int(ActionType::Google))
                QVERIFY2(url.startsWith("https://www.google.") && url.endsWith(expected), qPrintable(where + ": " + url));
            else
                QVERIFY2(url == expected, qPrintable(where + ": " + url));
            seen().clear();
        };

        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorActionRun"));
        check("editor");
        QVERIFY(closeEditor(app));

        QVERIFY(mk::homeWith(app, id));
        app.press(Qt::Key_Space);
        QVERIFY(clickNamed("quickLookAction"));
        check("Quick Look");
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("quickLook")->property("visible").toBool(), 5000);

        // Take the action off: nothing is left to perform.
        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorAction"));
        QVERIFY(clickMenu("None"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(id).actionType, int(ActionType::None), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("editorActionRun"), 5000);
        QVERIFY(closeEditor(app));
        QVERIFY(mk::homeWith(app, id));
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(findObject("quickLook")->property("visible").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("quickLookAction"), 5000);
        QVERIFY(seen().isEmpty());
    }

    // A task without an action says so and opens nothing.
    void performActionWithoutOneOpensNothing() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        app.press(Qt::Key_P);
        QTRY_COMPARE_WITH_TIMEOUT(statusMessage(app), QString("This task has no action."), 5000);
        QVERIFY(seen().isEmpty());
    }

    // ------------------------------------------------------------ attachments

    // "Click an attachment to open it; the small cross removes it" (task-editor.md).
    // The chip hands the file inside the attachments folder to the desktop, the
    // cross takes the attachment off the task, and Undo brings it back.
    void attachmentChipOpensAndCrossRemoves() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        const qint64 id = app.ids.buyMilk;
        const Attachment memo = attach(app, id, Attachment::Audio, "memo.flac", QByteArray(2048, 'x'));
        const Attachment doc = attach(app, id, Attachment::File, "plan.txt");
        QVERIFY(openEditor(app, id));
        const QString chipName = QString("editorAttachment:%1").arg(memo.id);
        QVERIFY(waitItem(chipName));
        QVERIFY(waitItem(QString("editorAttachment:%1").arg(doc.id)));

        QVERIFY(settled(chipName));
        QQuickItem *chip = findItem(chipName);
        click(chip, Qt::LeftButton, pointIn(chip, 0.3, 0.6));
        const QString expected = QString::fromUtf8(QUrl::fromLocalFile(app.store().attachmentsFolder() + "/" + memo.fileName).toEncoded());
        QTRY_COMPARE_WITH_TIMEOUT(seen(), QStringList{expected}, 5000);

        // The cross on the audio chip removes it and leaves the other.
        const QString crossName = QString("editorAttachmentRemove:%1").arg(memo.id);
        QVERIFY(tk::clickUntilGone(crossName, chipName));
        QTRY_COMPARE_WITH_TIMEOUT(attachmentsOf(app, id).size(), 1, 5000);
        QCOMPARE(attachmentsOf(app, id).first().id, doc.id);
        QVERIFY(findItem(QString("editorAttachment:%1").arg(doc.id)));
        QCOMPARE(seen().size(), 1);   // The cross did not open the file.
        QVERIFY(closeEditor(app));

        // Undo gives it back.
        QVERIFY(mk::homeWith(app, id));
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(attachmentsOf(app, id).size(), 2, 5000);
        QVERIFY(openEditor(app, id));
        QVERIFY(waitItem(chipName));
        QVERIFY(closeEditor(app));
    }

    // Quick Look lists a task's audio notes and plays one when it is clicked.
    void quickLookAudioNoteOpens() {
        App app;
        QVERIFY(app.ok);
        const qint64 id = app.ids.buyMilk;
        const Attachment memo = attach(app, id, Attachment::Audio, "memo.flac", QByteArray(2048, 'x'));
        QVERIFY(mk::homeWith(app, id));
        app.press(Qt::Key_Space);
        const QString name = QString("quickLookAudio:%1").arg(memo.id);
        QVERIFY(waitItem(name));
        QVERIFY(settled(name));
        click(findItem(name));
        const QString expected = QString::fromUtf8(QUrl::fromLocalFile(app.store().attachmentsFolder() + "/" + memo.fileName).toEncoded());
        QTRY_COMPARE_WITH_TIMEOUT(seen(), QStringList{expected}, 5000);

        // Without the audio note the row is gone.
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("quickLook")->property("visible").toBool(), 5000);
        Attachment gone = app.store().attachments().value(memo.id);
        gone.deleted = true;
        app.store().begin("Prepare", false, true);
        app.store().putAttachment(gone);
        app.store().commit();
        app.press(Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(findObject("quickLook")->property("visible").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem(name), 5000);
    }

    // --------------------------------------------------------------- copy menus

    // The editor's copy button: Copy Title, Copy Notes, Copy Links from Notes,
    // Copy Embeddable Link and Copy Shareable Link each put their text on the
    // clipboard (task-editor.md, "Sharing").
    void editorCopyMenuPutsEachFormOnTheClipboard() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        const qint64 id = app.ids.plants;
        mk::prep(app, id, [](Task &t) { t.notes = "Mist them, see https://example.com/ferns first"; });
        const Task t = app.task(id);
        QVERIFY(openEditor(app, id));
        struct Row { QString entry, text; };
        const QList<Row> rows = {
            {"Copy Title", "Water the plants"},
            {"Copy Notes", "Mist them, see https://example.com/ferns first"},
            {"Copy Links from Notes", "https://example.com/ferns"},
            {"Copy Embeddable Link", "@{Water the plants}"},
            {"Copy Shareable Link", "fairfax://x-callback-url/showTask?uid=" + QString::fromUtf8(QUrl::toPercentEncoding(t.uid))},
        };
        for (const Row &row : rows) {
            setClipboard("untouched");
            QVERIFY(clickNamed("editorCopy"));
            QVERIFY2(clickMenu(row.entry), qPrintable(row.entry));
            QTRY_COMPARE_WITH_TIMEOUT(clipboardText().trimmed(), row.text, 5000);
        }
        QVERIFY(closeEditor(app));
    }

    // A task without notes has nothing to copy from them.
    void editorCopyMenuDisablesWhatTheTaskLacks() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        QVERIFY(openEditor(app, app.ids.buyMilk));
        QVERIFY(clickNamed("editorCopy"));
        QVERIFY(waitFor([] { return findText("Copy Title", "QQuickMenuItem") != nullptr; }));
        QVERIFY(entryEnabled("Copy Title"));
        QVERIFY(!entryEnabled("Copy Notes"));
        QVERIFY(!entryEnabled("Copy Links from Notes"));
        QVERIFY(dismissMenu());
        QVERIFY(closeEditor(app));
    }

    // Quick Look's copy menu: Copy Title, Copy Notes, Copy as Text, Copy Link.
    void quickLookCopyMenuPutsEachFormOnTheClipboard() {
        App app;
        QVERIFY(app.ok);
        const qint64 id = app.ids.plants;
        mk::prep(app, id, [](Task &t) { t.notes = "Mist them first"; });
        const Task t = app.task(id);
        QVERIFY(mk::homeWith(app, id));
        app.press(Qt::Key_Space);
        QVERIFY(waitItem("quickLookCopy"));
        struct Row { QString entry, text, status; };
        const QList<Row> rows = {
            {"Copy Title", "Water the plants", "Title copied."},
            {"Copy Notes", "Mist them first", "Notes copied."},
            {"Copy as Text", "Water the plants\nMist them first", "Task copied."},
            {"Copy Link", "fairfax://x-callback-url/showTask?uid=" + QString::fromUtf8(QUrl::toPercentEncoding(t.uid)), "Link copied."},
        };
        for (const Row &row : rows) {
            setClipboard("untouched");
            QVERIFY(settled("quickLookCopy"));
            click(findItem("quickLookCopy"));
            QVERIFY2(clickMenu(row.entry), qPrintable(row.entry));
            QTRY_COMPARE_WITH_TIMEOUT(clipboardText().trimmed(), row.text, 5000);
            QTRY_COMPARE_WITH_TIMEOUT(statusMessage(app), row.status, 5000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(findObject("quickLook")->property("visible").toBool(), 5000);   // The preview stays up.
    }

    // With no notes there are none to copy; the title and the text are the same.
    void quickLookCopyMenuWithoutNotes() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(mk::homeWith(app, app.ids.buyMilk));
        app.press(Qt::Key_Space);
        QVERIFY(waitItem("quickLookCopy"));
        QVERIFY(settled("quickLookCopy"));
        click(findItem("quickLookCopy"));
        QVERIFY(waitFor([] { return findText("Copy as Text", "QQuickMenuItem") != nullptr; }));
        QVERIFY(!entryEnabled("Copy Notes"));
        setClipboard("untouched");
        QVERIFY(clickMenu("Copy as Text"));
        QTRY_COMPARE_WITH_TIMEOUT(clipboardText(), QString("Buy milk"), 5000);
    }

    // ----------------------------------------------------------------- alerts

    // An alert chip: clicking it chooses its style ("Message with Sound" or
    // "Message"), and its cross removes that one alert and no other.
    void alertChipStyleAndCross() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        const qint64 id = app.ids.buyMilk;   // Due today.
        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("15 minutes before"));
        QTRY_COMPARE_WITH_TIMEOUT(liveAlarms(app, id).size(), 1, 5000);
        QVERIFY(clickNamed("editorAlarm"));
        QVERIFY(clickMenu("1 hour before"));
        QTRY_COMPARE_WITH_TIMEOUT(liveAlarms(app, id).size(), 2, 5000);
        qint64 first = 0, second = 0;
        for (const Alarm &a : liveAlarms(app, id)) {
            QVERIFY(a.sound);
            (a.offset == 15 ? first : second) = a.id;
        }
        QVERIFY(first && second);
        const QString firstChip = QString("editorAlarmChip:%1").arg(first);
        QVERIFY(waitItem(firstChip));

        // Message without sound: the chip says so.
        QVERIFY(settled(firstChip));
        click(findItem(firstChip), Qt::LeftButton, pointIn(findItem(firstChip), 0.3, 0.5));
        QVERIFY(clickMenu("Message"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().alarms().value(first).sound, 5000);
        QVERIFY(app.store().alarms().value(second).sound);
        QTRY_VERIFY_WITH_TIMEOUT(textContaining(findItem(firstChip), "(message)") != nullptr, 5000);
        QVERIFY(!textContaining(findItem(QString("editorAlarmChip:%1").arg(second)), "(message)"));

        // And back to sound.
        QVERIFY(settled(firstChip));
        click(findItem(firstChip), Qt::LeftButton, pointIn(findItem(firstChip), 0.3, 0.5));
        QVERIFY(clickMenu("Message with Sound"));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().alarms().value(first).sound, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!textContaining(findItem(firstChip), "(message)"), 5000);

        // The cross removes just that alert.
        QVERIFY(tk::clickUntilGone(QString("editorAlarmRemove:%1").arg(first), firstChip));
        QTRY_COMPARE_WITH_TIMEOUT(liveAlarms(app, id).size(), 1, 5000);
        QCOMPARE(liveAlarms(app, id).first().id, second);
        QVERIFY(findItem(QString("editorAlarmChip:%1").arg(second)));
        QVERIFY(tk::clickUntilGone(QString("editorAlarmRemove:%1").arg(second), QString("editorAlarmChip:%1").arg(second)));
        QTRY_COMPARE_WITH_TIMEOUT(liveAlarms(app, id).size(), 0, 5000);
        QVERIFY(closeEditor(app));
        QTRY_VERIFY_WITH_TIMEOUT(!tk::badge("rowAlarm", id), 5000);
    }

    // -------------------------------------------------------- record audio note

    // "Record an audio note": the microphone button starts a recording (its tip
    // turns to Stop Recording) and a second click stops it and attaches the
    // audio to the task. pw-record is a stub that writes a few kilobytes.
    void recordButtonStartsAndStopsARecording() {
        QTemporaryDir bin;
        fakeRecorder(bin.path(), 4096);
        PathGuard path(bin.path(), true);
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QCOMPARE(app.backend().recordingTaskId(), qint64(0));
        QCOMPARE(prop("editorRecord", "tip").toString(), QString("Record an Audio Note"));
        QVERIFY(clickNamed("editorRecord"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().recordingTaskId(), id, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(prop("editorRecord", "tip").toString(), QString("Stop Recording"), 5000);
        // The recorder has written its file before it is stopped.
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const auto files = QDir(app.store().attachmentsFolder()).entryInfoList({"*.flac"}, QDir::Files);
            return !files.isEmpty() && files.first().size() >= 1024;
        }(), 5000);
        QVERIFY(clickNamed("editorRecord"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().recordingTaskId(), qint64(0), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(attachmentsOf(app, id).size(), 1, 5000);
        const Attachment note = attachmentsOf(app, id).first();
        QCOMPARE(note.kind, int(Attachment::Audio));
        QVERIFY(note.name.startsWith("Audio note "));
        QVERIFY(QFileInfo(app.store().attachmentsFolder() + "/" + note.fileName).size() >= 1024);
        QVERIFY(waitItem(QString("editorAttachment:%1").arg(note.id)));
        QCOMPARE(prop("editorRecord", "tip").toString(), QString("Record an Audio Note"));

        // Take it off again: the audio note's cross.
        QVERIFY(tk::clickUntilGone(QString("editorAttachmentRemove:%1").arg(note.id), QString("editorAttachment:%1").arg(note.id)));
        QTRY_COMPARE_WITH_TIMEOUT(attachmentsOf(app, id).size(), 0, 5000);
        QVERIFY(closeEditor(app));
    }

    // A recording that produced nothing is not attached.
    void emptyRecordingIsNotAttached() {
        QTemporaryDir bin;
        fakeRecorder(bin.path(), 0);
        PathGuard path(bin.path(), true);
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        const qint64 id = app.ids.buyMilk;
        QVERIFY(openEditor(app, id));
        QVERIFY(clickNamed("editorRecord"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().recordingTaskId(), id, 5000);
        QVERIFY(clickNamed("editorRecord"));
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().recordingTaskId(), qint64(0), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(statusMessage(app), QString("Nothing was recorded."), 5000);
        QCOMPARE(attachmentsOf(app, id).size(), 0);
        QVERIFY(closeEditor(app));
    }

    // Without PipeWire's recorder the button says what is missing.
    void recordButtonWithoutARecorderSaysSo() {
        QTemporaryDir bin;   // Empty: no pw-record on the path.
        PathGuard path(bin.path(), false);
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        QVERIFY(openEditor(app, app.ids.buyMilk));
        QVERIFY(clickNamed("editorRecord"));
        QTRY_VERIFY_WITH_TIMEOUT(statusMessage(app).contains("pw-record"), 5000);
        QCOMPARE(app.backend().recordingTaskId(), qint64(0));
        QCOMPARE(prop("editorRecord", "tip").toString(), QString("Record an Audio Note"));
        QVERIFY(closeEditor(app));
    }
};

REGISTER_TEST(E2EGapsEditor)

#include "test_gaps_editor.moc"
