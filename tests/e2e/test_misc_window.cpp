// End-to-end tests: the rest of the window. The title and About block, the
// Lists and Tags panels' show/hide toggles, the Calendar Day Bar, the mini
// calendar's wheel, Today link and Alt-click, Copy To and Move to Section,
// attachments (files dropped on a task, a picture, an audio note), the Focus
// Filter and the button for scheduled tasks beside the sort, and Ctrl+Alt+W.

#include <QDesktopServices>
#include <QImage>
#include <QMimeData>
#include <QWheelEvent>

#include "misckit.h"
#include "../testregistry.h"

using namespace e2e;
using namespace e2e::mk;

namespace {

class UrlSink : public QObject {
    Q_OBJECT
public slots:
    void opened(const QUrl &url) { counters().urls << url.toString(); }
};

// An .ics file with some events today and one tomorrow.
QString writeCalendar(const QString &dir) {
    const QString path = dir + "/day.ics";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    const QString today = QDate::currentDate().toString("yyyyMMdd");
    const QString tomorrow = QDate::currentDate().addDays(1).toString("yyyyMMdd");
    auto event = [](const QString &uid, const QString &title, const QString &start, const QString &end, const QString &extra = QString()) {
        return QString("BEGIN:VEVENT\r\nUID:%1@e2e\r\nDTSTAMP:20270101T000000Z\r\nDTSTART:%2\r\nDTEND:%3\r\nSUMMARY:%4\r\n%5END:VEVENT\r\n")
            .arg(uid, start, end, title, extra);
    };
    QString text = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//e2e//EN\r\n";
    text += event("a", "Standup", today + "T100000", today + "T103000");
    text += event("b", "Design review", today + "T140000", today + "T153000",
                  "LOCATION:Room 4\r\nDESCRIPTION:Bring the mockups https://example.com/review\r\n");
    text += QString("BEGIN:VEVENT\r\nUID:c@e2e\r\nDTSTAMP:20270101T000000Z\r\nDTSTART;VALUE=DATE:%1\r\nDTEND;VALUE=DATE:%2\r\nSUMMARY:Offsite\r\nEND:VEVENT\r\n")
                .arg(today, QDate::currentDate().addDays(1).toString("yyyyMMdd"));
    text += event("d", "Dentist", tomorrow + "T090000", tomorrow + "T100000");
    text += "END:VCALENDAR\r\n";
    f.write(text.toUtf8());
    return path;
}

QQuickItem *dayBar() { return findItem("dayBar"); }

bool wheelOver(QQuickItem *item, int delta) {
    QQuickWindow *w = item->window();
    const QPointF p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QWheelEvent ev(p, w->mapToGlobal(p), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    return QCoreApplication::sendEvent(w, &ev);
}

// Drops files on the window the way a file manager does: enter, move, drop.
bool dropFiles(QQuickWindow *w, const QPoint &at, const QStringList &paths) {
    QMimeData data;
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls << QUrl::fromLocalFile(p);
    data.setUrls(urls);
    QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &enter);
    QDragMoveEvent move(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &move);
    QDropEvent drop(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &drop);
    return drop.isAccepted();
}

QString makePicture(const QString &dir, const QString &name) {
    const QString path = dir + "/" + name;
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(Qt::darkGreen);
    image.save(path);
    return path;
}

QString makeFile(const QString &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir + "/" + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    return path;
}

QVector<Attachment> attachmentsOf(App &app, qint64 task) {
    QVector<Attachment> out;
    for (const Attachment &a : app.store().attachmentsFor(task))
        if (!a.deleted) out << a;
    return out;
}

}  // namespace

class E2EMiscWindow : public QObject {
    Q_OBJECT
    UrlSink m_sink;

private slots:
    void initTestCase() { QDesktopServices::setUrlHandler("file", &m_sink, "opened"); }
    void cleanupTestCase() { QDesktopServices::unsetUrlHandler("file"); }

    // The window is named for what it shows.
    void windowTitleNamesTheList() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QTRY_COMPARE_WITH_TIMEOUT(app.window->title(), QString("Home — Precrime"), 3000);
        QVERIFY(app.openSource(lk(app.ids.work)));
        QTRY_COMPARE_WITH_TIMEOUT(app.window->title(), QString("Work — Precrime"), 3000);
        app.press(Qt::Key_5, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.window->title(), QString("Done — Precrime"), 3000);
    }

    // Settings carries the About block: name, version, project link, copyright,
    // and the MIT licence text behind its link.
    void aboutBlockAndLicenceText() {
        App app;
        QVERIFY(app.ok);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        QQuickItem *name = nullptr, *version = nullptr, *link = nullptr, *licence = nullptr;
        control("aboutName", name);
        control("aboutVersion", version);
        control("aboutLink", link);
        control("aboutLicense", licence);
        QCOMPARE(name->property("text").toString(), QString("Precrime"));
        QVERIFY(version->property("text").toString().startsWith("Version "));
        QCOMPARE(link->property("text").toString(), QString("github.com/jrimmer/precrime"));
        QVERIFY(textUnder(sw->contentItem(), QStringLiteral("© 2026 Jason Rimmer")) != nullptr);
        QObject *dialog = findObject("licenseDialog");
        QVERIFY(dialog);
        QVERIFY(!dialog->property("visible").toBool());
        click(licence);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 3000);
        QQuickItem *text = nullptr;
        QTRY_VERIFY2_WITH_TIMEOUT((text = findItem("licenseText")) != nullptr && text->property("text").toString().contains("Permission is hereby granted"),
                                  qPrintable(text ? "the licence text reads: " + text->property("text").toString().left(60) : QString("no licence text")), 3000);
        QVERIFY(text->property("text").toString().startsWith("MIT License"));
        QVERIFY(clickDialogButton("Close"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 3000);
    }

    // The panels: each has a button that hides it and one that brings it back,
    // narrow windows tuck them away, and what was chosen is remembered.
    void panelTogglesHideShowAndPersist() {
        Keep keep;
        QVERIFY(keep.start());
        {
            App &app = *keep;
            QVERIFY(app.openSource(lk(app.ids.home)));
            auto sidebar = [] { return findItem("sidebarAdd") != nullptr; };
            auto utility = [] { return findItem("tagsManualToggle") != nullptr; };
            QTRY_VERIFY_WITH_TIMEOUT(sidebar() && utility(), 3000);
            QVERIFY(!findItem("showSidebar"));
            QVERIFY(tk::clickNamed("hideSidebar"));
            QTRY_VERIFY_WITH_TIMEOUT(!sidebar() && findItem("showSidebar") != nullptr, 3000);
            QVERIFY(tk::clickNamed("showSidebar"));
            QTRY_VERIFY_WITH_TIMEOUT(sidebar() && !findItem("showSidebar"), 3000);
            QVERIFY(tk::clickNamed("toggleUtility"));
            QTRY_VERIFY_WITH_TIMEOUT(!utility(), 3000);
            QVERIFY(tk::clickNamed("toggleUtility"));
            QTRY_VERIFY_WITH_TIMEOUT(utility(), 3000);

            // A narrow window tucks both away and gives them back with room.
            app.window->resize(780, 760);
            QTRY_VERIFY_WITH_TIMEOUT(!sidebar() && !utility(), 3000);
            app.window->resize(1180, 760);
            QTRY_VERIFY_WITH_TIMEOUT(sidebar() && utility(), 3000);

            // Hidden by hand stays hidden when there is room, and after a restart.
            QVERIFY(tk::clickNamed("hideSidebar"));
            QVERIFY(tk::clickNamed("toggleUtility"));
            QTRY_VERIFY_WITH_TIMEOUT(!sidebar() && !utility(), 3000);
            app.window->resize(1300, 760);
            QTest::qWait(100);
            QVERIFY(!sidebar() && !utility());
        }
        QVERIFY(keep.start());
        App &app = *keep;
        QTest::qWait(200);
        QVERIFY(!findItem("sidebarAdd") && !findItem("tagsManualToggle"));
        QVERIFY(tk::clickNamed("showSidebar"));
        QVERIFY(tk::clickNamed("toggleUtility"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("sidebarAdd") && findItem("tagsManualToggle"), 3000);
        QVERIFY(keep.start());
        QTRY_VERIFY_WITH_TIMEOUT(findItem("sidebarAdd") && findItem("tagsManualToggle"), 3000);
    }

    // Ctrl+Alt+W asks for another window; the window itself is made by main().
    void newWindowIsRequested() {
        App app;
        QVERIFY(app.ok);
        QSignalSpy spy(&app.backend(), &Backend::newWindowRequested);
        QVERIFY(homeWith(app, app.ids.plants));
        app.press(Qt::Key_W, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
        app.press(Qt::Key_W, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 3000);
    }

    // "Calendar day bar": today's events above Today; the caption names what is
    // next, clicking an event opens its details, the chevron opens the hour
    // grid, the arrows browse other days and Today comes back.
    void dayBarShowsEventsAndBrowses() {
        QTemporaryDir dir;
        App app;
        QVERIFY(app.ok);
        const QString path = writeCalendar(dir.path());
        QVERIFY(!path.isEmpty());
        // 08:00 today, so the next event is the 10:00 stand-up whatever the real time is.
        app.services().calendar.setClock([] { return QDateTime(QDate::currentDate(), QTime(8, 0)); });
        app.press(Qt::Key_2, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().sourceKey(), QString("today"), 3000);
        QVERIFY(!dayBar());
        app.services().calendar.setSources({path});
        QTRY_VERIFY_WITH_TIMEOUT(dayBar() != nullptr, 3000);
        QQuickItem *bar = dayBar();
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("captionText").toString().startsWith("Standup · upcoming"), 3000);
        QVERIFY(findItem("dayBarPill:Offsite") != nullptr);     // All-day events are pills.
        QVERIFY(findItem("dayBarEvent:Standup") != nullptr);    // The others sit on the strip.
        QVERIFY(findItem("dayBarEvent:Design review") != nullptr);
        QVERIFY(!findItem("dayBarEvent:Dentist"));
        QVERIFY(!bar->property("expanded").toBool());

        // Clicking an event shows what it is.
        QQuickItem *review = findItem("dayBarEvent:Design review");
        click(review);
        QObject *details = findObject("dayBarDetails");
        QVERIFY(details);
        QTRY_VERIFY_WITH_TIMEOUT(details->property("visible").toBool(), 3000);
        QQuickItem *title = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((title = findItem("dayBarDetailTitle")) != nullptr && title->property("text").toString() == "Design review", 3000);
        QVERIFY(findText("Room 4") != nullptr);
        QVERIFY(textMatching(popupSurface(details), QRegularExpression("2:00 PM . 3:30 PM")) != nullptr);
        QVERIFY(findText("Bring the mockups https://example.com/review") != nullptr);
        QQuickItem *blank = findItem("taskList");   // A click elsewhere closes the details.
        click(blank, Qt::LeftButton, pointIn(blank, 0.5, 0.97));
        QTRY_VERIFY_WITH_TIMEOUT(!details->property("visible").toBool(), 3000);

        // The all-day pill opens its details too.
        click(findItem("dayBarPill:Offsite"));
        QTRY_VERIFY_WITH_TIMEOUT(details->property("visible").toBool() && findItem("dayBarDetailTitle")->property("text").toString() == "Offsite", 3000);
        click(blank, Qt::LeftButton, pointIn(blank, 0.5, 0.97));
        QTRY_VERIFY_WITH_TIMEOUT(!details->property("visible").toBool(), 3000);

        // Expand: the hour grid shows each event; collapse puts it away.
        QVERIFY(!findItem("dayBarGridEvent:Standup"));
        QVERIFY(tk::clickNamed("dayBarToggle"));
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("expanded").toBool() && findItem("dayBarGridEvent:Standup") != nullptr, 3000);
        QVERIFY(findItem("dayBarGridEvent:Design review") != nullptr);
        QVERIFY(tk::clickNamed("dayBarToggle"));
        QTRY_VERIFY_WITH_TIMEOUT(!bar->property("expanded").toBool() && !findItem("dayBarGridEvent:Standup"), 3000);

        // Tomorrow, with its own caption; Today comes back.
        QVERIFY(!findItem("dayBarToday"));
        QVERIFY(tk::clickNamed("dayBarStep:1"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->property("dayOffset").toInt(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("captionText").toString() == "Tomorrow · 1 event" && findItem("dayBarEvent:Dentist") != nullptr
                                     && !findItem("dayBarEvent:Standup"), 3000);
        QVERIFY(tk::clickNamed("dayBarStep:1"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->property("captionText").toString(), QString(QDate::currentDate().addDays(2).toString("ddd, MMM d")) + " · no events", 3000);
        QVERIFY(tk::clickNamed("dayBarStep:-1"));
        QVERIFY(tk::clickNamed("dayBarStep:-1"));
        QVERIFY(tk::clickNamed("dayBarStep:-1"));
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("captionText").toString().startsWith("Yesterday"), 3000);
        QVERIFY(tk::clickNamed("dayBarToday"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->property("dayOffset").toInt(), 0, 3000);
        QVERIFY(!findItem("dayBarToday"));

        // A search hides the bar; clearing it brings it back.
        app.press(Qt::Key_F, Qt::ControlModifier);
        typeInto(app.window, "milk");
        QTRY_VERIFY_WITH_TIMEOUT(!dayBar(), 3000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(dayBar() != nullptr, 3000);
    }

    // "Drag a task onto the bar to block out time for it": dropped on the hour grid it
    // is due when the block ends, takes the block's half hour, and is dated today.
    void dayBarTimeBlocksADroppedTask() {
        QTemporaryDir dir;
        App app;
        QVERIFY(app.ok);
        const QString path = writeCalendar(dir.path());
        app.services().calendar.setClock([] { return QDateTime(QDate::currentDate(), QTime(8, 0)); });
        app.services().calendar.setSources({path});
        QVERIFY(app.openSource(lk(app.ids.home)));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("dayBar") == nullptr, 1000);   // Only Today shows it, unless set otherwise.
        app.backend().setSetting("calendar.inLists", true);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("dayBarToggle") != nullptr, 3000);
        QVERIFY(selectRow(app, app.ids.plants));
        QVERIFY(tk::clickNamed("dayBarToggle"));   // The hour grid, 7:00 to 22:00, 48 pixels an hour.
        QQuickItem *standup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((standup = findItem("dayBarGridEvent:Standup")) != nullptr, 3000);
        QVERIFY(tk::settled("dayBarGridEvent:Standup"));
        // A spot in the free time after the stand-up: about 12:30.
        QQuickItem *dropPoint = new QQuickItem(standup);
        dropPoint->setPosition(QPointF(standup->width() / 2, standup->height() + 100));
        QVERIFY2(dragShowingPill(app.row(app.ids.plants), dropPoint, "dragProxy"), "no pill followed the pointer");
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.plants).dueTime >= 0, 3000);
        const Task t = app.task(app.ids.plants);
        QCOMPARE(t.dueDate, QDate::currentDate());
        QVERIFY2(qAbs(t.dueTime - (13 * 60)) <= 30 && t.dueTime % 15 == 0, qPrintable(QString::number(t.dueTime)));
        QCOMPARE(t.duration, 30);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(app.ids.plants).dueTime == -1 && app.task(app.ids.plants).dueDate == QDate::currentDate().addDays(1), 3000);
    }

    // The mini calendar: the wheel pages by month, Today (the link, or the month
    // name) comes back, Alt-click on a day starts a task due that day, and a task
    // dropped on a day is deferred to it.
    void miniCalendarWheelTodayAltClickAndDrop() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QQuickItem *cal = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((cal = findItem("miniCalendar")) != nullptr, 3000);
        const QDate today = QDate::currentDate();
        auto shown = [&] { return QDate(cal->property("year").toInt(), cal->property("month").toInt() + 1, 1); };
        const QDate thisMonth(today.year(), today.month(), 1);
        QCOMPARE(shown(), thisMonth);
        QVERIFY(!findItem("calToday"));

        // The wheel: up goes back a month, down goes forward.
        QVERIFY(wheelOver(cal, 120));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth.addMonths(-1), 3000);
        QVERIFY(findItem("calToday") != nullptr);
        QVERIFY(wheelOver(cal, -120));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth, 3000);
        QVERIFY(wheelOver(cal, -120));
        QVERIFY(wheelOver(cal, -120));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth.addMonths(2), 3000);
        // The link takes it home; so does the month name.
        QVERIFY(tk::clickNamed("calToday"));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth, 3000);
        QVERIFY(!findItem("calToday"));
        QVERIFY(tk::clickNamed("calNext"));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth.addMonths(1), 3000);
        QVERIFY(tk::clickNamed("calTitle"));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth, 3000);
        QVERIFY(tk::clickNamed("calPrevious"));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth.addMonths(-1), 3000);
        QVERIFY(tk::clickNamed("calToday"));
        QTRY_COMPARE_WITH_TIMEOUT(shown(), thisMonth, 3000);

        // Alt-click on a day: a new task due that day, in the editor.
        const QDate target = today.day() < today.daysInMonth() ? today.addDays(1) : today.addDays(-1);
        QQuickItem *cell = findItem("calDay:" + target.toString(Qt::ISODate));
        QVERIFY(cell);
        const QPoint at = centreOf(cell);
        QTest::mouseMove(app.window, at);
        QTest::mouseClick(app.window, Qt::LeftButton, Qt::AltModifier, at);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().editingTaskId() != 0, 3000);
        const qint64 created = app.backend().editingTaskId();
        QCOMPARE(app.task(created).dueDate, target);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("editorTitle") != nullptr, 3000);
        typeInto(app.window, "Alt-clicked");
        app.press(Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(created).title == "Alt-clicked", 3000);
        QCOMPARE(app.task(created).dueDate, target);

        // Without a title it never happened.
        QTest::mouseClick(app.window, Qt::LeftButton, Qt::AltModifier, at);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().editingTaskId() != 0, 3000);
        const qint64 blank = app.backend().editingTaskId();
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().editingTaskId() == 0 && !app.store().tasks().contains(blank), 3000);

        // A task dropped on a day is deferred to it.
        const QDate drop = today.addDays(today.day() + 3 <= today.daysInMonth() ? 3 : -3);
        QVERIFY(selectRow(app, app.ids.buyMilk));
        QQuickItem *dropCell = findItem("calDay:" + drop.toString(Qt::ISODate));
        QVERIFY(dropCell);
        tk::dragSlowly(app.row(app.ids.buyMilk), dropCell);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.buyMilk).dueDate, drop, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.buyMilk).dueDate, today, 3000);
    }

    // Copy To (in the task menu) leaves the original and puts a copy last in
    // the other list; Move to Section files the task under a heading, and
    // No Section takes it out again.
    void copyToAndMoveToSection() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(contextMenuOn(app, app.ids.plants));
        QVERIFY(clickSub("Copy To", "Work"));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            int copies = 0;
            for (const Task &t : app.store().tasks())
                copies += !t.deleted && t.title == "Water the plants" && t.listId == app.ids.work;
            return copies == 1;
        }(), 3000);
        QCOMPARE(app.task(app.ids.plants).listId, app.ids.home);   // The original stays.
        QVERIFY(!app.task(app.ids.plants).deleted);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().statusMessage() == "Copied to Work", 3000);
        qint64 copy = 0;
        for (const Task &t : app.store().tasks())
            if (!t.deleted && t.title == "Water the plants" && t.listId == app.ids.work) copy = t.id;
        QCOMPARE(tk::tagNames(app, app.store().task(copy)), (QStringList{"garden"}));   // Tags travel with it.
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.store().tasks().contains(copy) || app.store().task(copy).deleted, 3000);

        // Sections: none, so the entry is disabled; add two and file a task.
        QVERIFY(contextMenuOn(app, app.ids.plants));
        QQuickItem *entry = findText("Move to Section", "QQuickMenuItem");
        QVERIFY(entry && !entry->isEnabled());
        closeMenus(app);
        app.store().begin("Prepare", false);
        ListSection first, second;
        first.id = app.store().nextId("sections");
        first.listId = app.ids.home;
        first.name = "Morning";
        first.order = 1;
        second = first;
        second.id = app.store().nextId("sections");
        second.name = "Evening";
        second.order = 2;
        app.store().putSection(first);
        app.store().putSection(second);
        app.store().commit();
        QVERIFY(selectRow(app, app.ids.plants));
        QVERIFY(contextMenuOn(app, app.ids.plants));
        QVERIFY(clickSub("Move to Section", "Evening"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).sectionId, second.id, 3000);
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).sectionId, qint64(0), 3000);
        QVERIFY(contextMenuOn(app, app.ids.plants));
        QVERIFY(clickSub("Move to Section", "Morning"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).sectionId, first.id, 3000);
        QVERIFY(contextMenuOn(app, app.ids.plants));
        QVERIFY(clickSub("Move to Section", "No Section"));
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.ids.plants).sectionId, qint64(0), 3000);
    }

    // Files dropped on a task: a picture becomes its picture (the first wins),
    // an audio file an audio note, anything else a link in the notes; the editor
    // shows them, clicking one opens it and its cross removes it.
    void filesDroppedOnATaskAreAttached() {
        QTemporaryDir files;
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const QString picture = makePicture(files.path(), "view.png");
        const QString second = makePicture(files.path(), "other.png");
        const QString audio = makeFile(files.path(), "memo.wav", QByteArray("RIFF\x24\x00\x00\x00WAVEfmt ", 16) + QByteArray(28, '\0'));
        const QString doc = makeFile(files.path(), "plan.txt", "the plan");
        QTRY_VERIFY_WITH_TIMEOUT(tk::settled(QString("taskRow:%1").arg(app.ids.plants)), 3000);
        QQuickItem *row = app.row(app.ids.plants);
        QVERIFY(dropFiles(app.window, centreOf(row), {picture, second, audio, doc}));
        QTRY_VERIFY_WITH_TIMEOUT(attachmentsOf(app, app.ids.plants).size() == 2, 3000);
        int pictures = 0, audios = 0;
        for (const Attachment &a : attachmentsOf(app, app.ids.plants)) {
            pictures += a.kind == Attachment::Image;
            audios += a.kind == Attachment::Audio;
            QVERIFY(QFileInfo(app.store().attachmentsFolder() + "/" + a.fileName).size() > 0);   // Copied into the store.
        }
        QCOMPARE(pictures, 1);   // One picture per task: the first.
        QCOMPARE(audios, 1);
        const Task t = app.task(app.ids.plants);
        QVERIFY(t.notes.contains("other.png") && t.notes.contains("plan.txt"));   // The rest are linked in the notes.

        // In the editor.
        QVERIFY(openEditor(app, app.ids.plants));
        QString audioName;
        for (const Attachment &a : attachmentsOf(app, app.ids.plants))
            if (a.kind == Attachment::Audio) audioName = a.name;
        QQuickItem *chip = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((chip = findText(audioName)) != nullptr, 3000);
        counters().urls.clear();
        click(chip);
        QTRY_VERIFY_WITH_TIMEOUT(counters().urls.size() == 1 && counters().urls.first().contains(app.store().attachmentsFolder()), 3000);
        // Its cross removes it.
        const qint64 id = [&] { for (const Attachment &a : attachmentsOf(app, app.ids.plants)) if (a.kind == Attachment::Audio) return a.id; return qint64(0); }();
        QQuickItem *parent = chip->parentItem() ? chip->parentItem()->parentItem() : nullptr;
        QVERIFY(parent);
        QQuickItem *cross = findIn(parent, [&](QQuickItem *i) { return i != chip && i->width() == 14 && i->height() == 14 && i->childItems().size() >= 1; });
        QVERIFY(cross);
        click(cross, Qt::LeftButton, centreOf(cross));
        QTRY_VERIFY_WITH_TIMEOUT(app.store().attachments().value(id).deleted, 3000);
        QVERIFY(closeEditor(app));
    }

    // Dropping files on empty space makes a task named for the first; the
    // Attach button asks the chooser, whose answer attaches the file.
    void droppedOnEmptySpaceMakesATaskAndAttachButtonAsksTheChooser() {
        QTemporaryDir files;
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource(lk(app.ids.home)));
        const QString note = makeFile(files.path(), "Ideas for later.txt", "x");
        QQuickItem *list = findItem("taskList");
        QVERIFY(list);
        QVERIFY(dropFiles(app.window, pointIn(list, 0.5, 0.95), {note}));
        QTRY_VERIFY_WITH_TIMEOUT(app.taskTitled("Ideas for later").id != 0, 3000);
        const Task made = app.taskTitled("Ideas for later");
        QCOMPARE(made.listId, app.ids.home);
        QVERIFY(made.notes.contains("Ideas for later.txt"));
        app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.task(made.id).deleted, 3000);

        // The editor's Attach button.
        QVERIFY(openEditor(app, app.ids.shelf));
        QQuickItem *attach = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((attach = findIn(findItem("editorCard"), [](QQuickItem *i) { return i->property("tip").toString() == "Attach a File"; })) != nullptr, 3000);
        int asks = 0;
        QObject::connect(&app.services().filePicker, &FilePicker::failed, &app.backend(), [&] { ++asks; });
        click(attach);
        QTRY_COMPARE_WITH_TIMEOUT(asks, 1, 3000);
        QCOMPARE(app.services().filePicker.lastPurpose(), QString("attach:%1").arg(app.ids.shelf));
        const QString picture = makePicture(files.path(), "shelf.png");
        emit app.services().filePicker.picked(picture, app.services().filePicker.lastPurpose());
        QTRY_VERIFY_WITH_TIMEOUT(attachmentsOf(app, app.ids.shelf).size() == 1 && attachmentsOf(app, app.ids.shelf).first().kind == Attachment::Image, 3000);
        QVERIFY(closeEditor(app));
    }

    // The clock button beside the sort shows scheduled tasks (Ctrl+K), and the
    // Focus Filter's sun and its conditions narrow the list; each remembers
    // its state per list.
    void scheduledButtonAndFocusFilter() {
        Keep keep(seedMisc);
        QVERIFY(keep.start());
        App &app = *keep;
        QVERIFY(app.openSource(lk(mw().sorting)));
        QVERIFY(setSeparated(app, false));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
        // Hide one task by scheduling it: the clock appears.
        prep(app, mw().delta, [](Task &t) { t.startDate = day(6); });
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 4, 3000);
        QQuickItem *clock = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((clock = findItem("showScheduledButton")) != nullptr, 3000);
        click(clock);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
        QVERIFY(app.backend().showScheduled());
        click(findItem("showScheduledButton"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 4, 3000);
        prep(app, mw().delta, [](Task &t) { t.startDate = QDate(); });
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("showScheduledButton"), 3000);

        // Focus Filter: the sun turns it on, starting from Overdue and Today.
        QVERIFY(!app.backend().focusOn());
        QVERIFY(tk::clickNamed("focusToggle"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().focusOn(), 3000);
        QCOMPARE(app.backend().focusConditions(), (QStringList{"overdue", "today"}));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("E"), 3000);   // Echo is overdue; nothing is due today.
        QVERIFY(findItem("focusToggle")->property("checked").toBool());
        QVERIFY(tk::clickNamed("focusToggle"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().focusOn(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);

        // Its conditions are ticked in the menu next to the sun and combine with "or".
        auto condition = [&](const QString &key) {
            if (!tk::clickNamed("focusConditions")) return false;
            QQuickItem *item = nullptr;
            if (!tk::waitFor([&] { return (item = findItem("focusCondition:" + key)) != nullptr; })) return false;
            clickSteady(item);
            return tk::waitFor([&] { return findItem("focusCondition:" + key) == nullptr; });
        };
        QVERIFY(condition("tomorrow"));
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().focusOn() && app.backend().focusConditions() == QStringList{"tomorrow"}, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("B"), 3000);
        QVERIFY(condition("hiprio"));
        QVERIFY(condition("nodue"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("BC"), 3000);   // Bravo is both; Charlie has no date.
        QVERIFY(condition("overdue"));
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBC"), 3000);
        QVERIFY(tk::clickNamed("focusConditions"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("focusCondition:nodue") && findItem("focusCondition:nodue")->property("checked").toBool()
                                     && !findItem("focusCondition:today")->property("checked").toBool(), 3000);
        closeMenus(app);

        // Each list keeps its own: Home is not filtered.
        QVERIFY(app.openSource(lk(app.ids.home)));
        QVERIFY(!app.backend().focusOn());
        QTRY_VERIFY_WITH_TIMEOUT(app.row(app.ids.shelf) != nullptr, 3000);
        QVERIFY(app.openSource(lk(mw().sorting)));
        QVERIFY(app.backend().focusOn());

        // It is remembered, and unticking every condition turns it off.
        QVERIFY(keep.start());
        App &again = *keep;
        QVERIFY(again.openSource(lk(mw().sorting)));
        QVERIFY(again.backend().focusOn());
        QTRY_COMPARE_WITH_TIMEOUT(orderNow(), QString("EBC"), 3000);
        for (const char *key : {"overdue", "hiprio", "nodue", "tomorrow"}) {
            auto again_condition = [&](const QString &k) {
                if (!tk::clickNamed("focusConditions")) return false;
                QQuickItem *item = nullptr;
                if (!tk::waitFor([&] { return (item = findItem("focusCondition:" + k)) != nullptr; })) return false;
                clickSteady(item);
                return tk::waitFor([&] { return findItem("focusCondition:" + k) == nullptr; });
            };
            QVERIFY(again_condition(key));
        }
        QTRY_VERIFY_WITH_TIMEOUT(!again.backend().focusOn() && again.backend().focusConditions().isEmpty(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(orderNow().size(), 5, 3000);
    }
};

REGISTER_TEST(E2EMiscWindow)

#include "test_misc_window.moc"
