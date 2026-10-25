// Performance: how long the interactions a user feels take, on a realistic
// database in a tall window (1300x2100, so about sixty task rows are visible).
//
// Each interaction is measured by the CPU time of the GUI thread
// (CLOCK_THREAD_CPUTIME_ID) from the input event until the screen would show the
// result, so a busy shared machine does not make the suite flaky; the wall time
// is printed beside it. Every interaction has a CPU budget, and a table of
// measured against budget is printed at the end so a regression shows in the log.
//
// To re-baseline: run `FAIRFAX_E2E_CLASS=Performance bin/e2e` a few times on an
// otherwise idle machine, take the largest CPU figure of each row from the table,
// and set its budget (the numbers in budget() below) to about twice that. Raise a
// budget only for a change that is meant to cost more; otherwise find the cause.

#include <time.h>

#include "harness.h"
#include "../testregistry.h"
#include "settingskit.h"

using namespace e2e;

namespace {

double threadCpuMs() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

struct Result {
    QString name;
    double cpu, wall, budget;
};
QList<Result> &results() {
    static QList<Result> r;
    return r;
}

// CPU budgets in milliseconds; see the note at the top to re-baseline them.
double budget(const QString &name) {
    static const QHash<QString, double> b{
        {"switch empty list to 80 tasks", 450}, {"open Today", 320},        {"open All", 450},
        {"search: type a query", 600},          {"open Quick Entry", 40},   {"open the task editor", 400},
        {"open Settings", 130},                 {"switch all Settings panes", 220}, {"complete a task", 100}};
    return b.value(name, 100);
}

struct Ids {
    qint64 empty = 0, big = 0, errands = 0;
    QList<qint64> bigTasks;
};
Ids g_ids;

// A realistic world: an empty list, a list of 80 tasks (due dates and times, two
// or three tags each of fifteen, notes, alarms, durations, two projects with
// sub-tasks, a few completed), and a second list with enough tasks that Today
// and All are full.
Seed seedBusy(Store &store) {
    Seed s;
    const QDate today = QDate::currentDate();
    g_ids = Ids();
    store.begin("Seed", false);
    ListGroup group;
    group.id = store.nextId("list_groups");
    group.name = "Personal";
    group.order = 1;
    store.putGroup(group);
    auto list = [&](const QString &name, int color, double order) {
        TaskList l;
        l.id = store.nextId("lists");
        l.name = name;
        l.groupId = group.id;
        l.color = color;
        l.order = order;
        store.putList(l);
        return l.id;
    };
    g_ids.empty = list("Empty", 0, 1);
    g_ids.big = list("Big", 2, 2);
    g_ids.errands = list("Errands", 4, 3);
    s.home = g_ids.empty;
    QList<qint64> tags;
    for (const char *n : {"home", "work", "garden", "errands", "phone", "email", "waiting", "urgent", "ideas", "money", "health",
                          "family", "reading", "travel", "someday"}) {
        Tag t;
        t.id = store.nextId("tags");
        t.name = n;
        store.putTag(t);
        tags << t.id;
    }
    int n = 0;
    auto make = [&](qint64 listId, qint64 parent, int type, const QString &title, int due) {
        Task t;
        t.id = store.nextId("tasks");
        t.listId = listId;
        t.parentId = parent;
        t.type = type;
        t.title = title;
        if (due != INT_MIN) {
            t.dueDate = today.addDays(due);
            if (n % 3 == 0)
                t.dueTime = 8 * 60 + (n % 10) * 60;
        }
        t.priority = n % 4;
        t.starred = n % 11 == 0;
        t.duration = n % 5 == 0 ? 30 + 15 * (n % 4) : 0;
        if (n % 3 != 2)
            t.notes = "Remember to check the details first.\nSecond line of notes for task " + QString::number(n);
        for (int k = 0; k < 2 + n % 2; ++k)
            t.tagIds << tags[(n * 3 + k * 5) % tags.size()];
        t.order = t.id;
        store.putTask(t);
        if (n % 6 == 0) {
            Alarm a;
            a.id = store.nextId("alarms");
            a.taskId = t.id;
            a.kind = Alarm::Relative;
            a.offset = 15;
            store.putAlarm(a);
        }
        ++n;
        return t.id;
    };
    for (int i = 0; i < 80; ++i) {
        const bool project = i == 5 || i == 40;
        const qint64 id = make(g_ids.big, 0, project ? TaskType::Project : TaskType::Task,
                               QString("Big task %1: %2").arg(i + 1).arg(i % 2 ? "write the quarterly report" : "call the plumber about the garden"),
                               i % 9 == 0 ? INT_MIN : (i % 13) - 3);
        g_ids.bigTasks << id;
        if (project)
            for (int c = 0; c < 4; ++c)
                make(g_ids.big, id, TaskType::Task, QString("Step %1 of project %2").arg(c + 1).arg(i + 1), c);
    }
    for (int i = 0; i < 70; ++i)
        make(g_ids.errands, 0, TaskType::Task, QString("Errand %1: pick up the parcel").arg(i + 1), (i % 5) - 1);
    for (qint64 id : g_ids.bigTasks.mid(60, 4)) {
        Task t = store.task(id);
        t.completed = true;
        t.completedAt = QDateTime::currentDateTime();
        store.putTask(t);
    }
    store.commit();
    return s;
}

int visibleRows(QQuickWindow *w) {
    int count = 0;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        if (item->objectName().startsWith("taskRow:"))
            ++count;
        for (QQuickItem *c : item->childItems())
            walk(c);
    };
    walk(w->contentItem());
    return count;
}

}  // namespace

class E2EPerformance : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;

    // Runs `act`, then the event loop until `done` holds and the layout has
    // settled; records the GUI thread's CPU time and the wall time.
    bool measure(const QString &name, const std::function<void()> &act, const std::function<bool()> &done) {
        QElapsedTimer wall;
        wall.start();
        const double cpu0 = threadCpuMs();
        act();
        bool ok = false;
        while (wall.elapsed() < 15000) {
            if (done()) {
                ok = true;
                break;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        for (QQuickWindow *w : quickWindows())
            settleWindow(w);
        const double cpu = threadCpuMs() - cpu0, ms = wall.nsecsElapsed() / 1e6;
        results() << Result{name, cpu, ms, budget(name)};
        qInfo("PERF %-32s cpu %7.1f ms  wall %7.1f ms  budget %5.0f ms", qPrintable(name), cpu, ms, budget(name));
        return ok;
    }
#define CHECK_BUDGET(name) \
    QVERIFY2(results().last().cpu <= results().last().budget, \
             qPrintable(QString("%1 took %2 ms of CPU, budget %3 ms").arg(name).arg(results().last().cpu, 0, 'f', 1).arg(results().last().budget)))

private slots:
    void init() {
        m_app = std::make_unique<App>(seedBusy);
        QVERIFY(m_app->ok);
        m_app->window->resize(1300, 2100);
        settleWindow(m_app->window);
        QTest::qWait(50);
    }
    void cleanup() { m_app.reset(); }

    void switchEmptyListToBigList() {
        App &app = *m_app;
        QVERIFY(app.openSource("list:" + QString::number(g_ids.empty)));
        QTest::qWait(50);
        QCOMPARE(visibleRows(app.window), 0);
        QQuickItem *row = app.sidebarRow("list:" + QString::number(g_ids.big));
        QVERIFY(row);
        QVERIFY(measure("switch empty list to 80 tasks", [&] { click(row); },
                        [&] { return app.backend().sourceKey() == "list:" + QString::number(g_ids.big) && visibleRows(app.window) >= 40; }));
        qInfo("PERF rows built: %d", visibleRows(app.window));
        CHECK_BUDGET("switch empty list to 80 tasks");
    }

    void openToday() {
        App &app = *m_app;
        QVERIFY(app.openSource("list:" + QString::number(g_ids.empty)));
        QTest::qWait(50);
        QQuickItem *today = app.sidebarRow("today");
        QVERIFY(today);
        QVERIFY(measure("open Today", [&] { click(today); },
                        [&] { return app.backend().sourceKey() == "today" && visibleRows(app.window) >= 10; }));
        CHECK_BUDGET("open Today");
    }

    void openAll() {
        App &app = *m_app;
        QVERIFY(app.openSource("list:" + QString::number(g_ids.empty)));
        QTest::qWait(50);
        QQuickItem *all = app.sidebarRow("all");
        QVERIFY(all);
        QVERIFY(measure("open All", [&] { click(all); },
                        [&] { return app.backend().sourceKey() == "all" && visibleRows(app.window) >= 40; }));
        CHECK_BUDGET("open All");
    }

    void searchAsYouType() {
        App &app = *m_app;
        QVERIFY(app.openSource("all"));
        QTRY_VERIFY_WITH_TIMEOUT(visibleRows(app.window) >= 40, 5000);
        app.press(Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("searchField") && findItem("searchField")->property("activeFocus").toBool(), 5000);
        const int before = visibleRows(app.window);
        QVERIFY(measure("search: type a query", [&] { app.type("plumber"); },
                        [&] { return app.backend().search() == "plumber" && visibleRows(app.window) < before; }));
        CHECK_BUDGET("search: type a query");
    }

    void openEditor() {
        App &app = *m_app;
        QVERIFY(app.openSource("list:" + QString::number(g_ids.big)));
        QTRY_VERIFY_WITH_TIMEOUT(visibleRows(app.window) >= 40, 5000);
        settleWindow(app.window);
        // The editor opens in the main window.
        QQuickItem *row = findItem(QString("taskRow:%1").arg(g_ids.bigTasks.at(2)));
        QVERIFY(row);
        QVERIFY(measure("open the task editor", [&] { doubleClick(row); }, [&] { return findItem("editorTitle") != nullptr; }));
        CHECK_BUDGET("open the task editor");
    }

    void quickEntry() {
        App &app = *m_app;
        QVERIFY(app.openSource("list:" + QString::number(g_ids.big)));
        QTRY_VERIFY_WITH_TIMEOUT(visibleRows(app.window) >= 40, 5000);
        settleWindow(app.window);
        QVERIFY(measure("open Quick Entry", [&] { app.press(Qt::Key_2, Qt::ShiftModifier | Qt::ControlModifier); },
                        [&] { QQuickWindow *q = windowTitled("Quick Entry"); return q && q->isExposed() && findItem("quickTitle"); }));
        CHECK_BUDGET("open Quick Entry");
    }

    void settingsWindowAndPanes() {
        App &app = *m_app;
        QQuickWindow *sw = nullptr;
        QVERIFY(measure("open Settings", [&] { QTest::keyClick(app.window, Qt::Key_Comma, Qt::ControlModifier); },
                        [&] { sw = settingsWindow(); return sw && sw->isExposed(); }));
        CHECK_BUDGET("open Settings");
        sw->resize(720, 1100);
        settleWindow(sw);
        activate(sw);
        QVERIFY(measure("switch all Settings panes", [&] {
            for (const QString &pane : paneNames()) {
                QQuickItem *tab = findItem("settingsPane:" + pane);
                if (!tab)
                    continue;
                click(tab);
                const int idx = int(paneNames().indexOf(pane));
                QElapsedTimer t;
                t.start();
                while (sw->property("pane").toInt() != idx && t.elapsed() < 3000)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                settleWindow(sw);
            }
        }, [] { return true; }));
        CHECK_BUDGET("switch all Settings panes");
        closeSettings(app);
    }

    void completeATask() {
        App &app = *m_app;
        QVERIFY(app.openSource("list:" + QString::number(g_ids.big)));
        QTRY_VERIFY_WITH_TIMEOUT(visibleRows(app.window) >= 40, 5000);
        settleWindow(app.window);
        const qint64 id = g_ids.bigTasks.at(3);
        QQuickItem *box = findItem(QString("taskCheck:%1").arg(id));
        QVERIFY(box);
        QVERIFY(measure("complete a task", [&] { click(box); }, [&] { return app.task(id).completed; }));
        CHECK_BUDGET("complete a task");
    }

    void cleanupTestCase() {
        qInfo("PERF ---- measured (GUI thread CPU) against budget ----");
        for (const Result &r : results())
            qInfo("PERF %-32s %7.1f / %5.0f ms CPU   %7.1f ms wall   %s", qPrintable(r.name), r.cpu, r.budget, r.wall,
                  r.cpu <= r.budget ? "ok" : "OVER");
    }
};

REGISTER_TEST(E2EPerformance)

#include "test_performance.moc"
