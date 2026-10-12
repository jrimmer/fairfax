#include <QTemporaryDir>
#include <QtTest>

#include "exchange.h"
#include "store.h"
#include "testregistry.h"

// A synthetic backup shaped like TickTick's: BOM, a preamble with a quoted
// multi-line field, UTC instants with a time zone column, and checklists
// packed into the content.
static const char *Backup =
    "\xEF\xBB\xBF\"Date: 2026-10-12+0000\"\n\"Version: 7.2\"\n\"Status: \n0 Normal\n-1 Abandoned \n2 Completed\"\n"
    "\"Folder Name\",\"List Name\",\"Title\",\"Kind\",\"Tags\",\"Content\",\"Is Check list\",\"Start Date\",\"Due Date\","
    "\"Reminder\",\"Repeat\",\"Priority\",\"Status\",\"Created Time\",\"Completed Time\",\"Order\",\"Timezone\",\"Is All Day\","
    "\"Is Floating\",\"Column Name\",\"Column Order\",\"View Mode\",\"taskId\",\"parentId\",\"projectKind\"\n"
    "\"Home\",\"Chores\",\"Change filter\",\"TEXT\",\"house\",\"Line one\nLine two\",\"N\",\"2026-07-01T07:00:00+0000\","
    "\"2026-07-01T07:00:00+0000\",\"-P0DT15H0M0S\",\"FREQ=MONTHLY;INTERVAL=3;BYMONTHDAY=1\",\"5\",\"0\","
    "\"2026-05-08T18:33:06+0000\",\"\",\"-10\",\"America/Los_Angeles\",\"true\",\"false\",,,\"list\",\"a1\",\"\",\"TASK\"\n"
    "\"\",\"Inbox\",\"Call dentist\",\"TEXT\",\"\",\"\",\"N\",\"2026-07-02T16:30:00+0000\",\"2026-07-02T16:30:00+0000\","
    "\"PT0S\",\"\",\"3\",\"0\",\"2026-05-08T18:33:06+0000\",\"\",\"-9\",\"America/Los_Angeles\",\"false\",\"false\",,,\"list\",\"a2\",\"\",\"TASK\"\n"
    "\"Home\",\"Chores\",\"Utilities\",\"CHECKLIST\",\"\",\"Set up:\xE2\x96\xAA" "Electricity\n\xE2\x96\xAB" "Gas\n\xE2\x96\xAB\n\",\"Y\","
    "\"\",\"\",\"\",\"\",\"1\",\"0\",\"2026-05-08T18:33:06+0000\",\"\",\"-8\",\"\",\"true\",\"false\",,,\"list\",\"a3\",\"\",\"TASK\"\n"
    "\"\",\"Inbox\",\"Old idea\",\"TEXT\",\"\",\"\",\"N\",\"\",\"\",\"\",\"\",\"0\",\"-1\",\"2026-05-08T18:33:06+0000\","
    "\"2026-06-01T10:00:00+0000\",\"-7\",\"\",\"true\",\"false\",,,\"list\",\"a4\",\"\",\"TASK\"\n"
    "\"\",\"Inbox\",\"Sub step\",\"TEXT\",\"\",\"\",\"N\",\"\",\"\",\"\",\"\",\"0\",\"0\",\"2026-05-08T18:33:06+0000\","
    "\"\",\"-6\",\"\",\"true\",\"false\",,,\"list\",\"a5\",\"a2\",\"TASK\"\n";

class TickTickTest : public QObject {
    Q_OBJECT

    QString writeBackup(const QTemporaryDir &dir) {
        const QString path = dir.filePath("TickTick-backup.csv");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(Backup);
        return path;
    }

    Task find(const Store &store, const QString &title) {
        for (const auto &t : store.tasks())
            if (t.title == title)
                return t;
        return {};
    }

private slots:
    void importsTasksListsAndFields() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("p.db")));
        const QString path = writeBackup(dir);
        QVERIFY(Exchange::isTickTickBackup(path));
        const ImportResult r = Exchange::importTickTick(store, path);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 5);

        const Task filter = find(store, "Change filter");
        QCOMPARE(filter.dueDate, QDate(2026, 7, 1));  // 07:00 UTC is midnight in Los Angeles.
        QCOMPARE(filter.dueTime, -1);
        QVERIFY(!filter.startDate.isValid());          // Same start and due is just a due date.
        QCOMPARE(filter.priority, int(Priority::High));
        QCOMPARE(filter.recurrence.frequency, int(Repeat::Monthly));
        QCOMPARE(filter.recurrence.interval, 3);
        QCOMPARE(filter.notes, QString("Line one\nLine two"));
        QCOMPARE(store.list(filter.listId).name, QString("Chores"));
        QCOMPARE(store.groups().value(store.list(filter.listId).groupId).name, QString("Home"));
        const auto alarms = store.alarmsFor(filter.id);
        QCOMPARE(alarms.size(), 1);
        QCOMPARE(alarms.first().kind, int(Alarm::Absolute));
        QCOMPARE(alarms.first().at, QDateTime(QDate(2026, 6, 30), QTime(9, 0)));  // 15 h before the day starts.

        const Task dentist = find(store, "Call dentist");
        QCOMPARE(dentist.dueTime, 9 * 60 + 30);  // 16:30 UTC is 9:30 in Los Angeles.
        QCOMPARE(dentist.priority, int(Priority::Medium));
        QCOMPARE(store.alarmsFor(dentist.id).first().offset, 0);
        QCOMPARE(dentist.type, int(TaskType::Project));  // It has a sub-task.
        QCOMPARE(store.task(find(store, "Sub step").id).parentId, dentist.id);

        const Task utilities = find(store, "Utilities");
        QCOMPARE(utilities.type, int(TaskType::Checklist));
        QCOMPARE(utilities.notes, QString("Set up:"));
        int items = 0, done = 0;
        for (const auto &t : store.tasks())
            if (t.parentId == utilities.id) {
                ++items;
                done += t.completed;
            }
        QCOMPARE(items, 2);  // The empty item is skipped.
        QCOMPARE(done, 1);

        const Task idea = find(store, "Old idea");
        QVERIFY(idea.completed);
        QCOMPARE(store.tag(idea.tagIds.value(0)).name, QString("abandoned"));
    }

    void reimportAddsNothingAndUndoesInOneStep() {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.filePath("p.db")));
        const QString path = writeBackup(dir);
        Exchange::importTickTick(store, path);
        const int before = store.tasks().size();
        QCOMPARE(Exchange::importTickTick(store, path).tasks, 0);
        QCOMPARE(int(store.tasks().size()), before);
        QVERIFY(store.undo());  // The empty second import may still be a step; undo until empty.
        while (store.canUndo())
            store.undo();
        QCOMPARE(int(store.tasks().size()), 0);
    }
};

REGISTER_TEST(TickTickTest)
#include "test_ticktick.moc"
