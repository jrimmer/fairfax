// End-to-end scenarios for the `type:` presets of the search language
// (smart-lists.md): every preset the manual lists, typed into the toolbar's
// search field over the fixture world, with the exact set of rows the pane
// shows. One window serves every row of a data-driven test; each row types
// its query and clears it again.

#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

// The days of this week as offsets from today (Monday to Sunday).
bool dueWindowInWeek(const Spec &s) {
    if (s.due == NoDay)
        return false;
    const int dow = QDate::currentDate().dayOfWeek();
    const int lo = -(dow - 1), hi = lo + 6;
    const int first = s.due - s.duration / 1440;
    return first <= hi && s.due >= lo;
}

bool startInWeek(const Spec &s) { return s.start != NoDay && inThisWeek(s.start); }

const SL Hi{"Rent overdue", "Fence in 6 days", "Starred urgent", "Launch project"};
const SL Med{"Milk today", "Passport in 14 days", "Shopping checklist", "Medium plain"};
const SL Low{"Dentist today", "Weekly repeat", "Low plain"};
const SL Tagged{"Rent overdue", "Invoice overdue", "Milk today", "Parcel tomorrow", "Call mum", "Email boss",
                "Fix plumber leak", "Plumber for garden", "Plumber for office", "Blocked work", "Play tagged work",
                "Call work", "Someday idea", "Deep focus"};
const SL Located{"Milk today", "Parcel tomorrow", "Visit shop", "Office desk", "Office and playground"};
const SL DueToday{"Milk today", "Dentist today", "Two day window", "Fresh window", "Daily repeat", "Repeating window"};
const SL DueTomorrow{"Parcel tomorrow", "Launch step one", "Archive item", "Repeating window"};
const SL Repeating{"Weekly repeat", "Daily repeat", "Repeating window"};
const SL Structure{"Launch project", "Launch step one", "Launch step later", "Empty project", "Finished project", "Shopping checklist", "Empty checklist"};
const SL Dated{"Rent overdue", "Invoice overdue", "Milk today", "Dentist today", "Parcel tomorrow", "Report in 3 days",
               "Fence in 6 days", "Taxes in 9 days", "Passport in 14 days", "Began yesterday due in 2", "Two day window",
               "Fresh window", "Repeating window", "Starred urgent", "Weekly repeat", "Daily repeat", "Launch step one", "Launch step later",
               "Archive item"};
const SL Started{"Starts today", "Began yesterday due in 2", "Began 5 days ago"};

SL doneThisWeek() {
    return specTitles([](const Spec &s) { return s.completed() && parentOk(s) && inThisWeek(doneOffset(s)); });
}

}  // namespace

class E2ESmartPresets : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;

private slots:
    void initTestCase() {
        m_app = std::make_unique<App>(seedSmart);
        QVERIFY(m_app->ok);
        QVERIFY(waitUntil([&] { return m_app->openSource("all"); }));
    }
    void cleanupTestCase() { m_app.reset(); }
    void cleanup() {
        if (!m_app)
            return;
        // The next row types over this one's query; the list is only rebuilt
        // when something else (Include Scheduled, another list) was changed.
        showScheduled(*m_app, false);
        if (m_app->backend().sourceKey() != "all") {
            escapeSearch(*m_app);
            m_app->openSource("all");
        }
    }

    // The fixture itself: with nothing typed, All lists what the spec table says.
    void fixtureShowsExpectedBaseline() {
        App &app = *m_app;
        QCOMPARE(app.backend().search(), QString());
        SMART_SHOWS(app, allShown());
        QVERIFY(allShown().size() > 50);
    }

    // Structure presets: `alltasks` is every task in a flat view, `task` only
    // top-level plain tasks, `emptychck` checklists with no active sub-tasks.
    // Checklist items and sub-tasks of plain tasks nest under their parent in
    // Fairfax, so `subtasks` lists project children only.
    void typeStructure_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("alltasks") << "type: alltasks" << allShown();
        QTest::newRow("task") << "type: task" << without(allShown(), Structure);
        QTest::newRow("subtasks") << "type: subtasks" << SL{"Launch step one", "Launch step later"};
        QTest::newRow("proj") << "type: proj" << SL{"Launch project", "Empty project", "Finished project"};
        QTest::newRow("project") << "type: project" << SL{"Launch project", "Empty project", "Finished project"};
        QTest::newRow("chck") << "type: chck" << SL{"Shopping checklist", "Empty checklist"};
        QTest::newRow("checklist") << "type: checklist" << SL{"Shopping checklist", "Empty checklist"};
        QTest::newRow("emptyproj") << "type: emptyproj" << SL{"Empty project", "Finished project"};
        QTest::newRow("emptychck") << "type: emptychck" << SL{"Empty checklist"};
    }
    void typeStructure() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // Action presets: `action` for any action, then `call`, `sms`, `email`,
    // `browse`, `visit` and `google` for one kind each.
    void typeActions_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("action") << "type: action"
                                << SL{"Call mum", "Text Alex", "Email boss", "Browse docs", "Visit shop", "Google it"};
        QTest::newRow("call") << "type: call" << SL{"Call mum"};
        QTest::newRow("sms") << "type: sms" << SL{"Text Alex"};
        QTest::newRow("message") << "type: message" << SL{"Text Alex"};
        QTest::newRow("email") << "type: email" << SL{"Email boss"};
        QTest::newRow("mail") << "type: mail" << SL{"Email boss"};
        QTest::newRow("browse") << "type: browse" << SL{"Browse docs"};
        QTest::newRow("url") << "type: url" << SL{"Browse docs"};
        QTest::newRow("visit") << "type: visit" << SL{"Visit shop"};
        QTest::newRow("google") << "type: google" << SL{"Google it"};
    }
    void typeActions() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // Attachment and note presets: `audio`, `pict`, `note` and `capturedmail`.
    // Fairfax has no mail capture, so that preset matches nothing.
    void typeAttachmentsNotesCapture_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("audio") << "type: audio" << SL{"Voice memo job"};
        QTest::newRow("pict") << "type: pict" << SL{"Picture job"};
        QTest::newRow("picture") << "type: picture" << SL{"Picture job"};
        QTest::newRow("note") << "type: note" << SL{"Noted chore"};
        QTest::newRow("capturedmail") << "type: capturedmail" << SL{};
    }
    void typeAttachmentsNotesCapture() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // "`prio` With any priority; `noprio` Without; `hiprio` High; `medprio`
    // Medium; `lowprio` Low priority" (type: Presets for Priority).
    void typePriority_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        SL any = Hi + Med + Low;
        QTest::newRow("prio") << "type: prio" << any;
        QTest::newRow("noprio") << "type: noprio" << without(allShown(), any);
        QTest::newRow("hiprio") << "type: hiprio" << Hi;
        QTest::newRow("medprio") << "type: medprio" << Med;
        QTest::newRow("lowprio") << "type: lowprio" << Low;
    }
    void typePriority() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // Status presets: `starred`, `alarms`, `repeat`, `duration` and
    // `inprogress` (active start or duration window), `scheduled` (future start
    // date) and `paused` (paused tag). Scheduled and paused tasks are hidden by
    // default, but a query that asks for them by name shows them.
    void typeStatusTracking_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("starred") << "type: starred"
                                 << SL{"Starred plain", "Starred urgent", "Fix plumber leak", "Plumber quote",
                                       "Plumber for garden", "Plumber for office"};
        QTest::newRow("alarms") << "type: alarms" << SL{"Dentist today", "Alarm without date"};
        QTest::newRow("repeat") << "type: repeat" << Repeating;
        QTest::newRow("duration") << "type: duration" << SL{"Quick win", "Two day window", "Fresh window", "Repeating window"};
        QTest::newRow("inprogress") << "type: inprogress"
                                    << SL{"Two day window", "Fresh window", "Repeating window", "Began yesterday due in 2"};
        QTest::newRow("scheduled") << "type: scheduled" << SL{"Starts tomorrow", "Starts in 3 due in 5"};
        QTest::newRow("paused") << "type: paused" << SL{"Paused tag task"};
    }
    void typeStatusTracking() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // "`anytag` Tasks with at least one tag; `notag` without any; `anylocation`
    // at least one location; `nolocation` without" (type: Presets for Tags and
    // Locations).
    void typeTagsLocations_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("anytag") << "type: anytag" << Tagged;
        QTest::newRow("notag") << "type: notag" << without(allShown(), Tagged);
        QTest::newRow("anylocation") << "type: anylocation" << Located;
        QTest::newRow("nolocation") << "type: nolocation" << without(allShown(), Located);
    }
    void typeTagsLocations() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // Date presets: `due`, `nodue`, `start` and `nostart`. A future start date
    // makes a task scheduled, which is hidden unless asked for.
    void typeHasDate_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("due") << "type: due" << Dated;
        QTest::newRow("nodue") << "type: nodue" << without(allShown(), Dated);
        QTest::newRow("start") << "type: start" << Started;
        QTest::newRow("nostart") << "type: nostart" << without(allShown(), Started);
    }
    void typeHasDate() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // Due presets: `duetoday`/`today`, `duetomorrow`/`tomorrow` and
    // `duethisweek`. Tasks whose duration window covers that day or week match
    // too.
    void typeDueDate_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("duetoday") << "type: duetoday" << DueToday;
        QTest::newRow("today") << "type: today" << DueToday;
        QTest::newRow("duetomorrow") << "type: duetomorrow" << DueTomorrow;
        QTest::newRow("tomorrow") << "type: tomorrow" << DueTomorrow;
        QTest::newRow("duethisweek") << "type: duethisweek"
                                     << specTitles([](const Spec &s) { return shownInAll(s) && dueWindowInWeek(s); });
    }
    void typeDueDate() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // "`starttoday` Starts today; `starttomorrow` Starts tomorrow;
    // `startthisweek` Starts this week" (type: Presets for Start Date). A task
    // starting tomorrow is scheduled, so it needs Include Scheduled (Ctrl+K).
    void typeStartDate() {
        App &app = *m_app;
        SMART_QUERY(app, "type: starttoday", SL{"Starts today"});
        SMART_QUERY(app, "type: starttomorrow", SL{});
        SMART_QUERY(app, "type: startthisweek",
                    specTitles([](const Spec &s) { return shownInAll(s) && startInWeek(s); }));
        QVERIFY(showScheduled(app, true));
        SMART_SHOWS(app, specTitles([](const Spec &s) { return shownInAll(s, true) && startInWeek(s); }));
        SMART_QUERY(app, "type: starttomorrow", SL{"Starts tomorrow"});
        SMART_QUERY(app, "type: starttoday", SL{"Starts today"});
        // Reversed: scheduled tasks hide again.
        QVERIFY(showScheduled(app, false));
        SMART_SHOWS(app, SL{"Starts today"});
    }

    // Start-or-due presets: `anytoday`, `anytomorrow`, `anythisweek` and its
    // alias `thisweek`. Careful: bare `today` and `tomorrow` look at the due date
    // only, while bare `thisweek` looks at start or due.
    void typeStartOrDue() {
        App &app = *m_app;
        SMART_QUERY(app, "type: anytoday", DueToday + SL{"Starts today"});
        SMART_QUERY(app, "type: today", DueToday);
        SMART_QUERY(app, "type: anytomorrow", DueTomorrow);
        SMART_QUERY(app, "type: tomorrow", DueTomorrow);
        const auto week = [](bool inc) {
            return specTitles([inc](const Spec &s) { return shownInAll(s, inc) && (dueWindowInWeek(s) || startInWeek(s)); });
        };
        SMART_QUERY(app, "type: anythisweek", week(false));
        SMART_QUERY(app, "type: thisweek", week(false));
        QVERIFY(showScheduled(app, true));
        SMART_QUERY(app, "type: anytomorrow", DueTomorrow + SL{"Starts tomorrow"});
        SMART_QUERY(app, "type: anythisweek", week(true));
        SMART_QUERY(app, "type: thisweek", week(true));
        QVERIFY(showScheduled(app, false));
        SMART_SHOWS(app, week(false));
    }

    // "`overdue` Past-due, across every list; `tasksoverdue` Past-due, but
    // respecting the lists you've excluded from All; `donetoday`,
    // `doneyesterday`, `donethisweek`: Completed today / yesterday / this
    // week" (type: Presets for Overdue and Completed). Searching for completed
    // tasks needs no unhiding.
    void typeOverdueCompleted_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("overdue") << "type: overdue" << SL{"Rent overdue", "Invoice overdue"};
        QTest::newRow("tasksoverdue") << "type: tasksoverdue" << SL{"Rent overdue", "Invoice overdue"};
        QTest::newRow("donetoday") << "type: donetoday" << SL{"Done just now", "Launch step done"};
        QTest::newRow("doneyesterday") << "type: doneyesterday" << SL{"Done yesterday", "Finished step"};
        QTest::newRow("donethisweek") << "type: donethisweek" << doneThisWeek();
    }
    void typeOverdueCompleted() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }

    // "Use AND, OR, brackets, and `!` to combine presets" (type:).
    void typeCombinators_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("brackets and not")
            << "type: (proj OR chck) AND hiprio AND !repeat" << SL{"Launch project"};
        QTest::newRow("nested brackets")
            << "type: ((starred OR hiprio) AND due) OR today"
            << (SL{"Starred urgent", "Rent overdue", "Fence in 6 days"} + DueToday);
        QTest::newRow("or") << "type: hiprio OR medprio" << (Hi + Med);
        QTest::newRow("comma is or") << "type: hiprio, medprio" << (Hi + Med);
        QTest::newRow("explicit and")
            << "type: hiprio AND duethisweek"
            << specTitles([](const Spec &s) { return shownInAll(s) && s.prio == 3 && dueWindowInWeek(s); });
        QTest::newRow("implicit and") << "type: hiprio due" << SL{"Rent overdue", "Fence in 6 days", "Starred urgent"};
        QTest::newRow("not first") << "type: !hiprio AND prio" << (Med + Low);
        QTest::newRow("not group") << "type: prio AND !(hiprio OR medprio)" << Low;
        QTest::newRow("lowercase operators") << "type: hiprio or medprio" << (Hi + Med);
        QTest::newRow("uppercase keyword and preset") << "TYPE: HIPRIO" << Hi;
        QTest::newRow("unknown preset is ignored") << "type: nosuchpreset" << allShown();
    }
    void typeCombinators() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        SMART_QUERY(*m_app, query, expected);
    }
};

REGISTER_TEST(E2ESmartPresets)

#include "test_smart_presets.moc"
