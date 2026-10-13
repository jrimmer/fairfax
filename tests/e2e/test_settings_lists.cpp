// Settings whose story is longer than one switch: the Inbox, hidden lists,
// the completed-tasks window, default due dates, and the options that live
// outside the Settings window (the sidebar's calendar menu, the sort menu).

#include "settingskit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

// Clicks Cancel in whichever dialog is open (Escape would also close Settings).
void clickCancel() {
    QQuickItem *cancel = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((cancel = findText("Cancel")) != nullptr, 5000);
    click(cancel);
}

// Clicks the OK button of whichever dialog is open.
void acceptDialog() {
    QQuickItem *ok = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((ok = findText("OK")) != nullptr, 5000);
    click(ok);
}

qint64 inboxId(App &app) {
    for (const TaskList &l : app.store().lists())
        if (!l.deleted && l.kind == ListKind::Inbox)
            return l.id;
    return 0;
}

// A menu entry by its label, in whichever window the menu opened in.
void clickMenuItem(const QString &label) {
    QQuickItem *item = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((item = findText(label, "QQuickMenuItem")) != nullptr, 5000);
    click(item);
}

}  // namespace

class E2ESettingsLists : public QObject {
    Q_OBJECT

private slots:
    // "Getting Things Done® Inbox" (Settings > Tasks): switching it on adds an
    // Inbox to the sidebar that Quick Entry drops into; switching it off asks
    // first, removes it, and Ctrl+Z brings it back.
    void inboxSwitchAddsAndRemovesTheInbox() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Tasks");
        QQuickItem *box = nullptr, *defaultList = nullptr;
        control("setting:inbox", box);
        QVERIFY(!isChecked(box));
        QVERIFY(!inboxId(*app));
        control("setting:tasks.defaultList", defaultList);
        QVERIFY(defaultList->isEnabled());

        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(inboxId(*app) != 0, 5000);
        const qint64 inbox = inboxId(*app);
        focusMain(*app);
        QTRY_VERIFY_WITH_TIMEOUT(app->sidebarRow(listKey(inbox)) != nullptr, 5000);
        // With an Inbox the default list is the Inbox, so the picker is off.
        QTRY_VERIFY_WITH_TIMEOUT(!defaultList->isEnabled(), 5000);
        Task t;
        quickAdd(*app, unique("Zebra"), "Zebra", t);
        QCOMPARE(t.listId, inbox);

        // It survives a restart.
        QVERIFY(s.restart());
        app = s.app.get();
        QCOMPARE(inboxId(*app), inbox);
        openSettings(*app, sw);
        selectPane(sw, "Tasks");
        control("setting:inbox", box);
        QVERIFY(isChecked(box));

        // Switching it off is confirmed first; cancelling keeps it.
        click(box);
        QObject *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = findObject("confirmInbox")) != nullptr && dialog->property("visible").toBool(), 5000);
        clickCancel();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 5000);
        QVERIFY(inboxId(*app));
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 5000);
        acceptDialog();
        QTRY_COMPARE_WITH_TIMEOUT(inboxId(*app), qint64(0), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!isChecked(box), 5000);
        focusMain(*app);
        QTRY_VERIFY_WITH_TIMEOUT(!app->sidebarRow(listKey(inbox)), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app->store().task(t.id).deleted, 5000);

        // "Ctrl+Z brings them back."
        app->press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(inboxId(*app), inbox, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!app->store().task(t.id).deleted, 5000);
    }

    // "Hide lists" (Settings > Today & Lists): a hidden list vanishes from the
    // sidebar and from All, stays hidden after a restart, and comes back.
    void hiddenListsLeaveTheSidebarAndAll() {
        Session s;
        QVERIFY(s.start());
        App &app = *s.app;
        showSource(app, "all");
        QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.report) != nullptr, 5000);
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Today & Lists");
        QQuickItem *box = nullptr;
        const QString name = "hideList:" + QString::number(s.ids.work);
        control(name, box);
        QVERIFY(!isChecked(box));
        click(box);
        // The list of boxes is rebuilt when a list is hidden: look it up afresh.
        QTRY_VERIFY_WITH_TIMEOUT(findItem(name) && isChecked(findItem(name)), 5000);
        QVERIFY(app.store().setting("lists.hidden").toString().contains(app.store().list(s.ids.work).uid));
        focusMain(app);
        QTRY_VERIFY_WITH_TIMEOUT(!app.sidebarRow(listKey(s.ids.work)), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!app.row(s.ids.report), 5000);
        QVERIFY(app.row(s.ids.buyMilk));

        QVERIFY(s.restart());
        showSource(*s.app, "all");
        QTRY_VERIFY_WITH_TIMEOUT(s.app->row(s.ids.buyMilk) != nullptr, 5000);
        QVERIFY(!s.app->sidebarRow(listKey(s.ids.work)));
        QVERIFY(!s.app->row(s.ids.report));
        openSettings(*s.app, sw);
        selectPane(sw, "Today & Lists");
        control(name, box);
        QVERIFY(isChecked(box));
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(findItem(name) && !isChecked(findItem(name)), 5000);
        focusMain(*s.app);
        QTRY_VERIFY_WITH_TIMEOUT(s.app->sidebarRow(listKey(s.ids.work)) != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(s.app->row(s.ids.report) != nullptr, 5000);
    }

    // "Show completed tasks for": each choice keeps a finished task in view
    // for that long and no longer, and the choice survives a restart.
    void completedWindowChoices() {
        struct Done { const char *title; qint64 secsAgo; };
        static const Done done[] = {{"Done30s", 30}, {"Done30m", 30 * 60}, {"Done5h", 5 * 3600}, {"Done3d", 3 * 86400},
                                    {"Done20d", 20 * 86400}, {"Done60d", 60 * 86400}};
        // Never, 1 minute, 1 hour, 1 day, 1 week, 1 month, Always: how many of the above stay.
        static const int shown[] = {0, 1, 2, 3, 4, 5, 6};
        Session s;
        s.seed = [](Store &st) {
            Seed ids = seedWorld(st);
            for (const Done &d : done)
                addTask(st, ids.work, d.title, [&d](Task &t) {
                    t.completed = true;
                    t.completedAt = QDateTime::currentDateTime().addSecs(-d.secsAgo);
                });
            editTask(st, ids.oldThing, [](Task &t) { t.completedAt = QDateTime::currentDateTime().addDays(-100); });
            return ids;
        };
        QVERIFY(s.start());
        App *app = s.app.get();
        showSource(*app, listKey(s.ids.work));
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Tasks");
        for (int choice : {0, 1, 2, 3, 4, 5, 6, 3}) {
            chooseCombo("setting:tasks.completedWindow", choice);
            focusMain(*app);
            QTRY_VERIFY_WITH_TIMEOUT(app->row(s.ids.report) != nullptr, 5000);
            for (int i = 0; i < 6; ++i)
                QTRY_COMPARE_WITH_TIMEOUT(app->row(app->taskTitled(done[i].title).id) != nullptr, i < shown[choice], 5000);
            // The task finished long ago only shows under Always.
            QTRY_COMPARE_WITH_TIMEOUT(app->row(s.ids.oldThing) != nullptr, choice == 6, 5000);
            openSettings(*app, sw);
        }
        chooseCombo("setting:tasks.completedWindow", 5);
        QVERIFY(s.restart());
        app = s.app.get();
        showSource(*app, listKey(s.ids.work));
        QTRY_VERIFY_WITH_TIMEOUT(app->row(app->taskTitled("Done20d").id) != nullptr, 5000);
        QVERIFY(!app->row(app->taskTitled("Done60d").id));
        openSettings(*app, sw);
        selectPane(sw, "Tasks");
        QQuickItem *combo = nullptr;
        control("setting:tasks.completedWindow", combo);
        QCOMPARE(combo->property("currentIndex").toInt(), 5);
    }

    // "Default due date for new tasks": every choice, from None to Next Month.
    void defaultDueDateChoices() {
        Session s;
        QVERIFY(s.start());
        App &app = *s.app;
        showSource(app, listKey(s.ids.home));
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "Tasks");
        const int days[] = {-1, 0, 1, 2, 3, 7, 14, 30};
        for (int index : {1, 2, 3, 4, 5, 6, 7, 0}) {
            chooseCombo("setting:tasks.defaultDueOffset", index);
            Task t;
            newTaskByKeyboard(app, unique("Due"), t);
            const QDate today = QDate::currentDate();
            if (days[index] < 0)
                QVERIFY(!t.dueDate.isValid());
            else
                QCOMPARE(t.dueDate, days[index] == 30 ? today.addMonths(1) : today.addDays(days[index]));
            openSettings(app, sw);
        }
    }

    // The two Quick Entry boxes switch each other off, they are only usable
    // with a default due date, and they come back right after a restart (the
    // stored "false" must not read as ticked).
    void quickEntryDueBoxesAreExclusive() {
        Session s;
        QVERIFY(s.start());
        QQuickWindow *sw = nullptr;
        openSettings(*s.app, sw);
        selectPane(sw, "Tasks");
        QQuickItem *only = nullptr, *not_ = nullptr;
        control("setting:tasks.defaultDueQuickEntryOnly", only);
        control("setting:tasks.defaultDueNotQuickEntry", not_);
        QVERIFY(!only->isEnabled());
        QVERIFY(!not_->isEnabled());
        chooseCombo("setting:tasks.defaultDueOffset", 1);
        QTRY_VERIFY_WITH_TIMEOUT(only->isEnabled() && not_->isEnabled(), 5000);
        click(only);
        QTRY_VERIFY_WITH_TIMEOUT(isChecked(only), 5000);
        QVERIFY(!isChecked(not_));
        click(not_);
        QTRY_VERIFY_WITH_TIMEOUT(isChecked(not_) && !isChecked(only), 5000);

        QVERIFY(s.restart());
        QCOMPARE(s.app->store().setting("tasks.defaultDueQuickEntryOnly").toString(), QString("false"));
        openSettings(*s.app, sw);
        selectPane(sw, "Tasks");
        control("setting:tasks.defaultDueQuickEntryOnly", only);
        control("setting:tasks.defaultDueNotQuickEntry", not_);
        QVERIFY(!isChecked(only));
        QVERIFY(isChecked(not_));
        click(not_);
        QTRY_VERIFY_WITH_TIMEOUT(!isChecked(not_) && !isChecked(only), 5000);
    }

    // "Week starts on" Monday and Sunday: the mini calendar's first column,
    // and Automatic follows the system (named in the entry).
    void weekStartMondayAndSunday() {
        Session s;
        QVERIFY(s.start());
        App &app = *s.app;
        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "General");
        QQuickItem *combo = nullptr;
        control("setting:general.weekStart", combo);
        QVERIFY(combo->property("currentText").toString().startsWith("Automatic ("));
        const struct { int index; int dayOfWeek; const char *stored; } weeks[] = {{1, 1, "1"}, {2, 7, "7"}, {1, 1, "1"}};
        for (const auto &w : weeks) {
            chooseCombo("setting:general.weekStart", w.index);
            QCOMPARE(app.store().setting("general.weekStart").toString(), QString(w.stored));
            focusMain(app);
            QTRY_COMPARE_WITH_TIMEOUT(calendarColumn(dateOnWeekday(w.dayOfWeek)), 0, 5000);
            openSettings(app, sw);
        }
        QVERIFY(s.restart());
        focusMain(*s.app);
        QTRY_COMPARE_WITH_TIMEOUT(calendarColumn(dateOnWeekday(1)), 0, 5000);
    }

    // "Celebrate when a list is cleared": finishing the last open task throws
    // confetti, and with the setting off it does not.
    void celebrateThrowsConfettiOnlyWhenOn() {
        Session s;
        QVERIFY(s.start());
        App &app = *s.app;
        showSource(app, listKey(s.ids.work));
        QQuickItem *confetti = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((confetti = findItem("confetti")) != nullptr, 5000);
        const qint64 open[] = {s.ids.report, s.ids.studio, s.ids.callAlex};
        auto finishAll = [&] {
            for (qint64 id : open) {
                QQuickItem *box = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((box = findItem(QString("taskCheck:%1").arg(id))) != nullptr, 5000);
                click(box);
                QTRY_VERIFY_WITH_TIMEOUT(app.task(id).completed, 5000);
            }
        };
        finishAll();
        QTRY_VERIFY_WITH_TIMEOUT(confetti->childItems().size() > 20, 5000);
        // The pieces free themselves once they have fallen.
        QTRY_COMPARE_WITH_TIMEOUT(confetti->childItems().size(), 0, 6000);
        for (int i = 0; i < 3; ++i)
            app.press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.task(s.ids.report).completed && !app.task(s.ids.callAlex).completed, 5000);

        QQuickWindow *sw = nullptr;
        openSettings(app, sw);
        selectPane(sw, "General");
        setCheck("setting:general.celebrate", false);
        focusMain(app);
        finishAll();
        QTest::qWait(400);
        QCOMPARE(confetti->childItems().size(), 0);
    }

    // The sidebar's calendar controls: hide/show the mini calendar, shade
    // weekends, pick the busy-days style. Each is remembered across restarts
    // (stored as text) and mirrored in Settings > Appearance.
    void sidebarCalendarMenu() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        focusMain(*app);
        QQuickItem *toggle = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(findItem("miniCalendar") != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT((toggle = findItem("calendarToggle")) != nullptr, 5000);
        click(toggle);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("miniCalendar"), 5000);
        QCOMPARE(app->store().setting("appearance.showCalendar").toString(), QString("false"));

        QVERIFY(s.restart());
        app = s.app.get();
        focusMain(*app);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("calendarToggle") != nullptr, 5000);
        QVERIFY(!findItem("miniCalendar"));
        click(findItem("calendarToggle"));
        QTRY_VERIFY_WITH_TIMEOUT(findItem("miniCalendar") != nullptr, 5000);

        // Display style and weekend shading, from the chevron beside the button.
        QQuickItem *cal = findItem("miniCalendar");
        click(findItem("calendarStyle"));
        clickMenuItem("Dots");
        QTRY_COMPARE_WITH_TIMEOUT(cal->property("style").toString(), QString("dots"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(countNamed("calDots") > 0, 5000);
        QCOMPARE(countNamed("calWeekendShade"), 0);
        click(findItem("calendarStyle"));
        clickMenuItem("Shade Weekends");
        QTRY_VERIFY_WITH_TIMEOUT(countNamed("calWeekendShade") > 0, 5000);
        QCOMPARE(app->store().setting("appearance.calendarWeekends").toString(), QString("true"));

        QVERIFY(s.restart());
        app = s.app.get();
        focusMain(*app);
        QTRY_VERIFY_WITH_TIMEOUT((cal = findItem("miniCalendar")) != nullptr, 5000);
        QCOMPARE(cal->property("style").toString(), QString("dots"));
        QTRY_VERIFY_WITH_TIMEOUT(countNamed("calWeekendShade") > 0, 5000);
        // The same style shows in Settings > Appearance.
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Appearance");
        QQuickItem *combo = nullptr;
        control("setting:appearance.calendarStyle", combo);
        QCOMPARE(combo->property("currentIndex").toInt(), 1);

        // And back: weekends unshaded, heat map.
        focusMain(*app);
        click(findItem("calendarStyle"));
        clickMenuItem("Shade Weekends");
        QTRY_COMPARE_WITH_TIMEOUT(countNamed("calWeekendShade"), 0, 5000);
        click(findItem("calendarStyle"));
        clickMenuItem("Heat Map");
        QTRY_COMPARE_WITH_TIMEOUT(cal->property("style").toString(), QString("heat"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(countNamed("calDots"), 0, 5000);
    }

    // The sort menu above the task list: the choice is stored for the view
    // (not in a list's own record for Focus views), applies at once, and is
    // there after a restart. "Separated" switches the headings off.
    void sortMenuPersists() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        showSource(*app, "all");
        QTRY_VERIFY_WITH_TIMEOUT(app->row(s.ids.report) != nullptr, 5000);
        QVERIFY(textUnder(findItem("taskList"), "NEXT 7 DAYS") == nullptr);
        QQuickItem *button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("sortButton")) != nullptr, 5000);
        QVERIFY(textUnder(button, "Due Date"));
        click(button);
        clickMenuItem("Priority");
        QTRY_COMPARE_WITH_TIMEOUT(app->backend().sortField(), int(SortField::Priority), 5000);
        QCOMPARE(app->store().setting("sort.all.field").toString(), QString::number(SortField::Priority));
        QTRY_VERIFY_WITH_TIMEOUT(sectionTitles().contains("HIGH"), 5000);
        const QStringList byPriority = sectionTitles();

        QVERIFY(s.restart());
        app = s.app.get();
        showSource(*app, "all");
        QTRY_COMPARE_WITH_TIMEOUT(app->backend().sortField(), int(SortField::Priority), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(textUnder(findItem("sortButton"), "Priority") != nullptr, 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(sectionTitles() == byPriority, qPrintable(sectionTitles().join(",") + " vs " + byPriority.join(",")), 5000);

        // "Separated" off: one run of tasks instead of a heading per group; on again: they return.
        QVERIFY(byPriority.size() > 1);
        click(findItem("sortButton"));
        clickMenuItem("Separated");
        QTRY_VERIFY_WITH_TIMEOUT(sectionTitles().size() <= 1, 5000);
        QCOMPARE(app->store().setting("sort.all.separated").toString(), QString("false"));
        QVERIFY(s.restart());
        app = s.app.get();
        showSource(*app, "all");
        QTRY_VERIFY_WITH_TIMEOUT(app->row(s.ids.report) != nullptr, 5000);
        QVERIFY(sectionTitles().size() <= 1);
        click(findItem("sortButton"));
        clickMenuItem("Separated");
        QTRY_COMPARE_WITH_TIMEOUT(sectionTitles(), byPriority, 5000);
    }

    // Ctrl+O hides the completed tasks of the view and shows them again.
    void hideCompletedToggle() {
        Session s;
        QVERIFY(s.start());
        App &app = *s.app;
        showSource(app, listKey(s.ids.work));
        QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.oldThing) != nullptr, 5000);
        app.press(Qt::Key_O, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!app.row(s.ids.oldThing), 5000);
        QVERIFY(app.row(s.ids.report));
        app.press(Qt::Key_O, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.oldThing) != nullptr, 5000);
    }

    // The "Exact words" switch of the Quick Open bar is remembered per bar:
    // "plant" finds "Water the plants" until exact words is on, and the switch
    // is still on after a restart.
    void quickOpenExactWordsIsRemembered() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        auto matches = [&](bool &found) {
            focusMain(*app);
            app->press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
            QObject *bar = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((bar = findObject("commandPopup")) != nullptr && bar->property("visible").toBool(), 5000);
            app->type("plant");
            QTRY_VERIFY_WITH_TIMEOUT(!bar->property("items").toList().isEmpty() || bar->property("exact").toBool(), 5000);
            found = false;
            for (const QVariant &item : bar->property("items").toList())
                found = found || item.toMap().value("label").toString() == "Water the plants";
        };
        bool found = false;
        matches(found);
        QVERIFY(found);
        QObject *bar = findObject("commandPopup");
        QVERIFY(!bar->property("exact").toBool());
        click(findText("Exact words  ⌥E"));
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("exact").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const QVariant &item : bar->property("items").toList())
                if (item.toMap().value("label").toString() == "Water the plants")
                    return false;
            return true;
        }(), 5000);
        QCOMPARE(app->store().setting("bar.exact.open").toString(), QString("true"));
        app->press(Qt::Key_Escape);

        QVERIFY(s.restart());
        app = s.app.get();
        matches(found);
        QVERIFY(findObject("commandPopup")->property("exact").toBool());
        QVERIFY(!found);
        click(findText("Exact words  ⌥E"));
        QTRY_VERIFY_WITH_TIMEOUT(!findObject("commandPopup")->property("exact").toBool(), 5000);
        QCOMPARE(app->store().setting("bar.exact.open").toString(), QString("false"));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const QVariant &item : findObject("commandPopup")->property("items").toList())
                if (item.toMap().value("label").toString() == "Water the plants")
                    return true;
            return false;
        }(), 5000);
    }

    // Search Options > "Sounds like": a misspelt word finds the task only
    // while it is ticked, and the choice outlives a restart.
    void soundsLikeSearchIsRemembered() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        auto search = [&](const QString &text) {
            focusMain(*app);
            app->press(Qt::Key_F, Qt::ControlModifier);
            app->press(Qt::Key_A, Qt::ControlModifier);
            app->type(text);
            app->press(Qt::Key_Return);
        };
        showSource(*app, "all");
        search("wayter");
        QTRY_COMPARE_WITH_TIMEOUT(app->backend().search(), QString("wayter"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!app->row(s.ids.plants), 5000);
        // Tick it in the search options sheet.
        app->press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QQuickItem *box = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((box = findItem("soundexBox")) != nullptr, 5000);
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(app->backend().soundex(), 5000);
        QCOMPARE(app->store().setting("search.soundex").toString(), QString("true"));
        app->press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(app->row(s.ids.plants) != nullptr, 5000);

        QVERIFY(s.restart());
        app = s.app.get();
        QVERIFY(app->backend().soundex());
        showSource(*app, "all");
        search("wayter");
        QTRY_VERIFY_WITH_TIMEOUT(app->row(s.ids.plants) != nullptr, 5000);
        app->press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT((box = findItem("soundexBox")) != nullptr, 5000);
        QVERIFY(isChecked(box));
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(!app->backend().soundex(), 5000);
        QCOMPARE(app->store().setting("search.soundex").toString(), QString("false"));
        app->press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!app->row(s.ids.plants), 5000);
        QVERIFY(s.restart());
        QVERIFY(!s.app->backend().soundex());
    }

    // The tag panel's sort switch (alphabetical or dragged order) is stored,
    // and "false" still reads as off after a restart.
    void tagSortingSwitchIsRemembered() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        focusMain(*app);
        QQuickItem *button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("tagsManualToggle")) != nullptr, 5000);
        QVERIFY(!app->backend().tagsManual());
        click(button);
        QTRY_VERIFY_WITH_TIMEOUT(app->backend().tagsManual(), 5000);
        QCOMPARE(app->store().setting("tags.manual").toString(), QString("true"));
        QVERIFY(s.restart());
        app = s.app.get();
        QVERIFY(app->backend().tagsManual());
        focusMain(*app);
        QTRY_VERIFY_WITH_TIMEOUT((button = findItem("tagsManualToggle")) != nullptr, 5000);
        QVERIFY(button->property("checked").toBool());
        click(button);
        QTRY_VERIFY_WITH_TIMEOUT(!app->backend().tagsManual(), 5000);
        QVERIFY(s.restart());
        QVERIFY(!s.app->backend().tagsManual());
    }

    // Text size from the keyboard (Ctrl+= / Ctrl+-) is the same setting as the
    // slider in Settings > Appearance: both show it, and it persists.
    void textSizeKeysMoveTheSettingsSlider() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        showSource(*app, listKey(s.ids.home));
        focusMain(*app);
        app->press(Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(app->backend().textSize(), 3, 5000);
        QCOMPARE(app->store().setting("appearance.textSize").toString(), QString("3"));
        QVERIFY(s.restart());
        app = s.app.get();
        QCOMPARE(app->backend().fontSize(), 15);
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "Appearance");
        QQuickItem *slider = nullptr;
        control("setting:appearance.textSize", slider);
        QCOMPARE(slider->property("value").toInt(), 3);
        setSlider(sw, "setting:appearance.textSize", 1);
        QTRY_COMPARE_WITH_TIMEOUT(app->backend().textSize(), 1, 5000);
        QCOMPARE(app->backend().fontSize(), 13);
    }
};

REGISTER_TEST(E2ESettingsLists)

#include "test_settings_lists.moc"
