#include <QtTest>
#include <functional>
#include <memory>

#include "store.h"
#include "testregistry.h"
#include "traymenu.h"
#include "views.h"

namespace {

QDateTime at(int y, int m, int d, int hh = 10, int mm = 30) { return QDateTime(QDate(y, m, d), QTime(hh, mm)); }
const QDateTime Now = at(2026, 7, 15);  // Wednesday
const QDate Today(2026, 7, 15);

QDate day(int offset) { return Today.addDays(offset); }

}  // namespace

class TrayMenuTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Store> m_store;
    double m_order = 0;

    qint64 addList(const QString &name, std::function<void(TaskList &)> edit = {}) {
        TaskList l;
        l.id = m_store->nextId("lists");
        l.name = name;
        l.order = ++m_order;
        if (edit)
            edit(l);
        m_store->begin("list");
        m_store->putList(l);
        m_store->commit();
        return l.id;
    }

    qint64 addTask(qint64 listId, const QString &title, std::function<void(Task &)> edit = {}) {
        Task t;
        t.id = m_store->nextId("tasks");
        t.listId = listId;
        t.title = title;
        t.order = ++m_order;
        t.created = Now.addDays(-30);
        if (edit)
            edit(t);
        m_store->begin("task");
        m_store->putTask(t);
        m_store->commit();
        return t.id;
    }

    QList<TrayEntry> entries(bool canSync = false) { return TrayMenu::entries(*m_store, Now, canSync); }

    static QStringList labels(const QList<TrayEntry> &list) {
        QStringList out;
        for (const auto &e : list)
            out << e.label;
        return out;
    }

    static QStringList taskLabels(const QList<TrayEntry> &list) {
        QStringList out;
        for (const auto &e : list)
            if (e.kind == TrayEntry::Task)
                out << e.label;
        return out;
    }

    static QStringList commands(const QList<TrayEntry> &list) {
        QStringList out;
        for (const auto &e : list)
            if (e.kind == TrayEntry::Command)
                out << e.command;
        return out;
    }

private slots:
    void init() {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_store = std::make_unique<Store>();
        QVERIFY(m_store->open(m_dir->filePath("tray.db")));
        m_order = 0;
    }

    void cleanup() {
        m_store.reset();
        m_dir.reset();
    }

    // The defaults give exactly today's entries: the heading, the overdue ⚠
    // label with its date, the timed label, the star, and the everyday
    // commands with Sync present only when a provider is set up.
    void defaultsReproduceTodaysMenu() {
        const auto home = addList("Home");
        addTask(home, "Buy milk", [](Task &t) { t.dueDate = Today; });
        addTask(home, "Pay bill", [](Task &t) { t.dueDate = day(-1); });
        addTask(home, "Standup", [](Task &t) { t.dueDate = Today; t.dueTime = 9 * 60 + 30; });
        addTask(home, "Fix shelf", [](Task &t) { t.dueDate = Today; t.starred = true; });

        const auto list = entries(false);
        QCOMPARE(list.first().kind, int(TrayEntry::Section));
        QCOMPARE(list.first().label, QStringLiteral("Today — Wed 15 Jul"));
        QCOMPARE(taskLabels(list), QStringList({"⚠ Pay bill   ·  yesterday", "Standup   ·  9:30 AM", "Buy milk", "★ Fix shelf"}));
        QCOMPARE(commands(list), QStringList({"new", "open", "find", "settings", "quit"}));  // no sync set up

        const auto withSync = entries(true);
        QCOMPARE(commands(withSync), QStringList({"new", "open", "find", "sync", "settings", "quit"}));
        // Quit is always last.
        QCOMPARE(withSync.last().command, QStringLiteral("quit"));
    }

    // The empty state says "Nothing left for today" for Today.
    void emptyTodayIsDisabledInfo() {
        addList("Home");
        const auto list = entries();
        QCOMPARE(taskLabels(list), QStringList({}));
        bool found = false;
        for (const auto &e : list)
            if (e.kind == TrayEntry::Info) {
                found = true;
                QCOMPARE(e.label, QStringLiteral("Nothing left for today"));
                QCOMPARE(e.enabled, false);
            }
        QVERIFY(found);
    }

    // The 12 cap holds and the "… and N more" count is right.
    void capAtTwelveWithMoreCount() {
        const auto home = addList("Home");
        for (int i = 0; i < 15; ++i)
            addTask(home, QString("Task %1").arg(i), [](Task &t) { t.dueDate = Today; });
        const auto list = entries();
        QCOMPARE(taskLabels(list).size(), 12);
        bool more = false;
        for (const auto &e : list)
            if (e.kind == TrayEntry::More) {
                more = true;
                QCOMPARE(e.label, QStringLiteral("… and 3 more"));
                QCOMPARE(e.sourceKey, QStringLiteral("today"));
            }
        QVERIFY(more);
    }

    // Each source names itself in the heading and in "… and N more".
    void starredSource() {
        const auto home = addList("Home");
        addTask(home, "Plain", [](Task &t) { t.dueDate = Today; });
        addTask(home, "Star one", [](Task &t) { t.starred = true; });
        m_store->setSetting("tray.source", "starred");
        const auto list = entries();
        QCOMPARE(list.first().label, QStringLiteral("Starred"));
        QCOMPARE(taskLabels(list), QStringList({"★ Star one"}));
    }

    void scheduledSource() {
        const auto home = addList("Home");
        addTask(home, "Starts soon", [](Task &t) { t.startDate = day(3); });
        addTask(home, "Today task", [](Task &t) { t.dueDate = Today; });
        m_store->setSetting("tray.source", "scheduled");
        const auto list = entries();
        QCOMPARE(list.first().label, QStringLiteral("Scheduled"));
        QCOMPARE(taskLabels(list), QStringList({"Starts soon"}));
    }

    void listSource() {
        const auto home = addList("Home");
        const auto work = addList("Work");
        addTask(home, "Home thing", [](Task &t) { t.dueDate = Today; });
        addTask(work, "Work thing", [](Task &t) { t.dueDate = Today; });
        m_store->setSetting("tray.source", QStringLiteral("list:%1").arg(work));
        const auto list = entries();
        QCOMPARE(list.first().label, QStringLiteral("Work"));
        QCOMPARE(taskLabels(list), QStringList({"Work thing"}));
    }

    void smartListSource() {
        const auto home = addList("Home");
        const auto smart = addList("Hot", [](TaskList &l) {
            l.kind = ListKind::Smart;
            l.smartQuery = "type: hiprio";
        });
        addTask(home, "High one", [](Task &t) { t.priority = Priority::High; });
        addTask(home, "Low one", [](Task &t) { t.priority = Priority::Low; });
        m_store->setSetting("tray.source", QStringLiteral("list:%1").arg(smart));
        const auto list = entries();
        QCOMPARE(list.first().label, QStringLiteral("Hot"));
        QCOMPARE(taskLabels(list), QStringList({"High one"}));
    }

    // The empty state for a source other than Today reads "Nothing here".
    void emptyNonTodaySource() {
        addList("Home");
        m_store->setSetting("tray.source", "starred");
        const auto list = entries();
        for (const auto &e : list)
            if (e.kind == TrayEntry::Info)
                QCOMPARE(e.label, QStringLiteral("Nothing here"));
        QVERIFY(!labels(list).contains(QStringLiteral("Nothing left for today")));
    }

    // Commands only: no section and no tasks.
    void noneSourceHasCommandsOnly() {
        const auto home = addList("Home");
        addTask(home, "Today task", [](Task &t) { t.dueDate = Today; });
        m_store->setSetting("tray.source", "none");
        const auto list = entries(true);
        QCOMPARE(taskLabels(list), QStringList({}));
        for (const auto &e : list)
            QVERIFY(e.kind != TrayEntry::Section);
        QCOMPARE(commands(list), QStringList({"new", "open", "find", "sync", "settings", "quit"}));
    }

    // A deleted list falls back to Today.
    void deletedListFallsBackToToday() {
        const auto home = addList("Home");
        const auto work = addList("Work");
        addTask(home, "Today task", [](Task &t) { t.dueDate = Today; });
        m_store->setSetting("tray.source", QStringLiteral("list:%1").arg(work));
        TaskList l = m_store->list(work);
        l.deleted = true;
        m_store->begin("delete");
        m_store->putList(l);
        m_store->commit();
        const auto list = entries();
        QCOMPARE(list.first().label, QStringLiteral("Today — Wed 15 Jul"));
        QCOMPARE(taskLabels(list), QStringList({"Today task"}));
    }

    // The count caps the rows, and values outside 3-30 are clamped.
    void countCapsAndClamps() {
        const auto home = addList("Home");
        for (int i = 0; i < 20; ++i)
            addTask(home, QString("Task %1").arg(i), [](Task &t) { t.dueDate = Today; });

        m_store->setSetting("tray.count", 3);
        QCOMPARE(taskLabels(entries()).size(), 3);
        m_store->setSetting("tray.count", 5);
        QCOMPARE(taskLabels(entries()).size(), 5);
        bool more = false;
        for (const auto &e : entries())
            if (e.kind == TrayEntry::More) {
                more = true;
                QCOMPARE(e.label, QStringLiteral("… and 15 more"));
            }
        QVERIFY(more);

        m_store->setSetting("tray.count", 1);   // below the floor
        QCOMPARE(taskLabels(entries()).size(), 3);
        m_store->setSetting("tray.count", 99);  // above the ceiling
        QCOMPARE(taskLabels(entries()).size(), 20);
    }

    // Subsets of the commands, always ending in Quit.
    void commandSubsets() {
        addList("Home");
        m_store->setSetting("tray.commands", "new,find");
        auto list = entries(true);
        QCOMPARE(commands(list), QStringList({"new", "find", "quit"}));
        // Command labels carry the binding in the platform's own spelling
        // (QKeySequence::NativeText), after a tab QMenu right-aligns.
#ifdef Q_OS_MACOS
        QCOMPARE(list.last().label, QStringLiteral("Quit Fairfax\t⌘Q"));
#else
        QCOMPARE(list.last().label, QStringLiteral("Quit Fairfax\tCtrl+Q"));
#endif

        m_store->setSetting("tray.commands", "settings");
        list = entries(true);
        QCOMPARE(commands(list), QStringList({"settings", "quit"}));

        m_store->setSetting("tray.commands", "");
        list = entries(true);
        QCOMPARE(commands(list), QStringList({"quit"}));
    }

    // Sync is dropped without a provider, even when the setting keeps it.
    void syncDroppedWithoutProvider() {
        addList("Home");
        m_store->setSetting("tray.commands", "sync");
        QCOMPARE(commands(entries(false)), QStringList({"quit"}));
        QCOMPARE(commands(entries(true)), QStringList({"sync", "quit"}));
    }

    // A task title's & is escaped as &&, as menus read it.
    void ampersandIsEscaped() {
        const auto home = addList("Home");
        addTask(home, "Fish & chips", [](Task &t) { t.dueDate = Today; });
        QCOMPARE(taskLabels(entries()), QStringList({"Fish && chips"}));
    }

    // badgeIcon paints the requested colour, and nothing for a zero count.
    void badgeIconPaintsTheColour() {
        const QColor colour(0x33, 0xAA, 0x55);
        const QIcon icon = TrayMenu::badgeIcon(5, colour);
        const QPixmap pixmap = icon.pixmap(64, 64);
        QVERIFY(!pixmap.isNull());
        const QImage image = pixmap.toImage();
        QCOMPARE(image.pixelColor(55, 45), colour);

        // A different colour paints differently.
        const QImage other = TrayMenu::badgeIcon(5, QColor(0x12, 0x34, 0x56)).pixmap(64, 64).toImage();
        QCOMPARE(other.pixelColor(55, 45), QColor(0x12, 0x34, 0x56));
    }

    void badgeColourDefaultsAndReadsSetting() {
        QCOMPARE(TrayMenu::badgeColour(*m_store), QColor(0xE5, 0x48, 0x4D));
        m_store->setSetting("tray.badgeColor", "#33AA55");
        QCOMPARE(TrayMenu::badgeColour(*m_store), QColor(0x33, 0xAA, 0x55));
    }
};

REGISTER_TEST(TrayMenuTest)

#include "test_traymenu.moc"
