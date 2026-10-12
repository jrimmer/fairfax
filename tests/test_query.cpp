#include <QtTest>
#include <functional>
#include <memory>

#include "query.h"
#include "store.h"
#include "testregistry.h"

namespace {

QDateTime at(int y, int m, int d, int hh = 10, int mm = 30) { return QDateTime(QDate(y, m, d), QTime(hh, mm)); }
const QDateTime Now = at(2026, 7, 15);  // Wednesday; the ISO week runs 13 to 19 July.
QDate jul(int d) { return QDate(2026, 7, d); }

QStringList sorted(QStringList l) {
    l.sort();
    return l;
}

}  // namespace

#define EXPECT(query, ...) QCOMPARE(titles(query), sorted(QStringList{__VA_ARGS__}))

class QueryTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Store> m_store;
    double m_order = 0;
    QStringList m_all;
    std::function<bool(const Task &)> m_paused;
    bool m_soundex = false;
    QDate m_from, m_to;

    qint64 add(const QString &table, std::function<void(qint64)> put) {
        const qint64 id = m_store->nextId(table);
        m_store->begin(table);
        put(id);
        m_store->commit();
        return id;
    }

    qint64 addGroup(const QString &name) {
        return add("groups", [&](qint64 id) {
            ListGroup g;
            g.id = id;
            g.name = name;
            g.order = ++m_order;
            m_store->putGroup(g);
        });
    }

    qint64 addList(const QString &name, qint64 group) {
        return add("lists", [&](qint64 id) {
            TaskList l;
            l.id = id;
            l.name = name;
            l.groupId = group;
            l.order = ++m_order;
            m_store->putList(l);
        });
    }

    qint64 addTagGroup(const QString &name) {
        return add("tag_groups", [&](qint64 id) {
            TagGroup g;
            g.id = id;
            g.name = name;
            g.order = ++m_order;
            m_store->putTagGroup(g);
        });
    }

    qint64 addTag(const QString &name, qint64 group, bool paused = false) {
        return add("tags", [&](qint64 id) {
            Tag t;
            t.id = id;
            t.name = name;
            t.groupId = group;
            t.paused = paused;
            t.order = ++m_order;
            m_store->putTag(t);
        });
    }

    qint64 addTask(qint64 list, const QString &title, std::function<void(Task &)> edit = {}) {
        m_all << title;
        return add("tasks", [&](qint64 id) {
            Task t;
            t.id = id;
            t.listId = list;
            t.title = title;
            t.order = ++m_order;
            t.created = Now.addDays(-30);
            t.modified = Now.addDays(-10);
            if (edit) edit(t);
            m_store->putTask(t);
        });
    }

    QueryContext context() const {
        QueryContext c;
        c.store = m_store.get();
        c.now = Now;
        c.soundex = m_soundex;
        c.rangeFrom = m_from;
        c.rangeTo = m_to;
        c.isPaused = m_paused;
        return c;
    }

    QStringList titles(const QString &text, QString *error = nullptr) const {
        const Query q = Query::parse(text, error);
        const QueryContext c = context();
        QStringList out;
        for (const Task &t : m_store->tasks())
            if (q.matches(t, c)) out << t.title;
        out.sort();
        return out;
    }

    QStringList allExcept(const QStringList &no) const {
        QStringList out;
        for (const QString &t : m_all)
            if (!no.contains(t)) out << t;
        return sorted(out);
    }

private slots:
    void init() {
        m_dir = std::make_unique<QTemporaryDir>();
        m_store = std::make_unique<Store>();
        QVERIFY(m_store->open(m_dir->filePath("q.db")));
        m_all.clear();
        m_paused = {};
        m_soundex = false;
        m_from = m_to = QDate();

        const qint64 home = addGroup("Home"), work = addGroup("Work");
        const qint64 errands = addList("Errands", home), office = addList("Office", work), archive = addList("Archive", work);
        const qint64 contexts = addTagGroup("Contexts"), someday = addTagGroup("Someday");
        const qint64 tHome = addTag("home", contexts), tWork = addTag("work", contexts), tPhone = addTag("phone", contexts);
        const qint64 tUrgent = addTag("urgent", 0), tLater = addTag("later", someday, true);

        addTask(errands, "Buy milk", [&](Task &t) {
            t.tagIds = {tHome};
            t.notes = "Semi-skimmed café";
            t.dueDate = jul(15);
            t.created = Now.addSecs(-3600);
            t.modified = Now.addSecs(-30 * 60);
        });
        addTask(errands, "Plumber visit", [&](Task &t) {
            t.tagIds = {tHome};
            t.priority = Priority::High;
            t.dueDate = jul(16);
            t.actionType = ActionType::Visit;
            t.starred = true;
        });
        addTask(office, "Write report", [&](Task &t) {
            t.tagIds = {tWork};
            t.priority = Priority::High;
            t.dueDate = jul(17);
            t.notes = "quarterly";
            t.modified = Now.addSecs(-2 * 3600);
        });
        addTask(errands, "Call Mum", [&](Task &t) {
            t.priority = Priority::Medium;
            t.dueDate = jul(14);
            t.actionType = ActionType::Call;
            t.recurrence.frequency = Repeat::Weekly;
        });
        const qint64 trip = addTask(office, "Plan trip", [](Task &t) { t.type = TaskType::Project; });
        addTask(office, "Book flights", [&](Task &t) {
            t.parentId = trip;
            t.tagIds = {tWork};
            t.priority = Priority::Low;
        });
        addTask(office, "Pack bags", [&](Task &t) {
            t.parentId = trip;
            t.completed = true;
            t.completedAt = Now.addDays(-20);
        });
        addTask(office, "Empty project", [](Task &t) { t.type = TaskType::Project; });
        addTask(errands, "Shopping", [&](Task &t) {
            t.type = TaskType::Checklist;
            t.tagIds = {tLater};
        });
        addTask(office, "Review budget", [](Task &t) {
            t.starred = true;
            t.startDate = jul(16);
            t.duration = 60;
        });
        addTask(archive, "Done today", [&](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 15, 9, 0);
            t.tagIds = {tUrgent};
        });
        addTask(archive, "Done old", [](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 5);
        });
        addTask(office, "Prepare slides", [&](Task &t) {
            t.tagIds = {tWork, tUrgent};
            t.dueDate = jul(20);
            t.created = Now.addDays(-3);
        });
        addTask(office, "Long window", [](Task &t) {
            t.dueDate = jul(17);
            t.duration = 3 * 1440;
        });
        addTask(errands, "Naïve résumé", [](Task &t) { t.notes = "Steven called about the plumber"; });
        addTask(office, "Phone dentist", [&](Task &t) {
            t.tagIds = {tPhone};
            t.priority = Priority::Low;
            t.actionType = ActionType::Mail;
            t.notes = "ask about braces";
        });
        addTask(archive, "Done yesterday", [](Task &t) {
            t.completed = true;
            t.completedAt = at(2026, 7, 14, 18, 0);
        });
    }

    void emptyQuery() {
        QVERIFY(Query::parse("").isEmpty());
        QVERIFY(Query::parse("   \t ").isEmpty());
        QVERIFY(Query().isEmpty());
        QCOMPARE(titles("").size(), 17);
        QCOMPARE(titles("*").size(), 17);
        QVERIFY(!Query::parse("milk").isEmpty());
    }

    void copyIsShared() {
        Query a = Query::parse("milk");
        Query b = a;
        Query c;
        c = a;
        const QueryContext ctx = context();
        const Task t = m_store->tasks().begin().value();
        QCOMPARE(a.matches(t, ctx), b.matches(t, ctx));
        QCOMPARE(a.matches(t, ctx), c.matches(t, ctx));
        QVERIFY(!c.isEmpty());
    }

    // ---- free text

    void wordsMatchTitleNotesAndTags() {
        EXPECT("milk", "Buy milk");
        EXPECT("quarterly", "Write report");
        EXPECT("urgent", "Done today", "Prepare slides");
        EXPECT("nothingmatchesthis");
    }

    void caseAndAccentInsensitive() {
        EXPECT("MILK", "Buy milk");
        EXPECT("cafe", "Buy milk");
        EXPECT("CAFÉ", "Buy milk");
        EXPECT("naive resume", "Naïve résumé");
        EXPECT("résumé", "Naïve résumé");
    }

    void implicitAndExplicitAnd() {
        EXPECT("plumber visit", "Plumber visit");
        EXPECT("plumber AND visit", "Plumber visit");
        EXPECT("plumber and visit", "Plumber visit");
        EXPECT("plumber about", "Naïve résumé");
        EXPECT("milk report");
    }

    void orOperator() {
        EXPECT("plumber OR milk", "Buy milk", "Plumber visit", "Naïve résumé");
        EXPECT("plumber or milk", "Buy milk", "Plumber visit", "Naïve résumé");
        EXPECT("milk OR report OR mum", "Buy milk", "Write report", "Call Mum");
        // AND binds tighter than OR.
        EXPECT("milk OR plumber visit", "Buy milk", "Plumber visit");
    }

    void negation() {
        QCOMPARE(titles("-plumber"), allExcept({"Plumber visit", "Naïve résumé"}));
        EXPECT("plumber -visit", "Naïve résumé");
        EXPECT("plumber !visit", "Naïve résumé");
        QCOMPARE(titles("!plumber"), allExcept({"Plumber visit", "Naïve résumé"}));
        EXPECT("plumber -(visit OR steven)");
        EXPECT("plumber -(visit AND steven)", "Plumber visit", "Naïve résumé");
    }

    void wildcards() {
        EXPECT("mil*", "Buy milk");
        EXPECT("ilk*");
        EXPECT("*ilk", "Buy milk");
        EXPECT("*ila");
        EXPECT("ilk", "Buy milk");
        EXPECT("*ilk*", "Buy milk");
        EXPECT("pack*", "Pack bags");
        EXPECT("ack*");
        EXPECT("*bags", "Pack bags");
        EXPECT("*bag");
        QCOMPARE(titles("-mil*"), allExcept({"Buy milk"}));
    }

    void phrases() {
        EXPECT("\"buy milk\"", "Buy milk");
        EXPECT("\"milk buy\"");
        EXPECT("\"call mum\"", "Call Mum");
        EXPECT("\"CALL  Mum\"", "Call Mum");
        EXPECT("\"\"", "Buy milk", "Plumber visit", "Write report", "Call Mum", "Plan trip", "Book flights", "Pack bags",
               "Empty project", "Shopping", "Review budget", "Done today", "Done old", "Prepare slides", "Long window",
               "Naïve résumé", "Phone dentist", "Done yesterday");
        QVERIFY(!titles("\"or\"").isEmpty());  // The literal text, not the operator: "report", "Shopping"...
        QVERIFY(titles("\"or\"").size() < 17);
    }

    void parentheses() {
        EXPECT("(milk OR report) -call", "Buy milk", "Write report");
        EXPECT("(plumber OR milk) AND visit", "Plumber visit");
        EXPECT("((milk))", "Buy milk");
        EXPECT("(plumber OR milk) (visit OR steven)", "Plumber visit", "Naïve résumé");
    }

    void soundex() {
        EXPECT("plumver");
        EXPECT("stephen");
        m_soundex = true;
        EXPECT("plumver", "Plumber visit", "Naïve résumé");
        EXPECT("stephen", "Naïve résumé");
        EXPECT("milk", "Buy milk");              // Literal matches still count.
        EXPECT("plumver visit", "Plumber visit");
        EXPECT("plumv*");                         // Wildcards stay literal.
        m_soundex = false;
    }

    void malformedNeverCrashes() {
        const QStringList junk = {"(", ")", "((((", "))))", "(()", "OR", "AND", "OR OR", "milk OR", "OR milk", "-", "!", "--", "!!milk",
                                  "\"", "\"milk", "type:", "tags:", "lists:", "groups:", "modified:", "created:", "completed:",
                                  "{datefilter}", "type: (", "type: )", "type: ((hiprio", "type: hiprio))", "type: OR", "type: AND OR",
                                  "tags: ,", "tags: !", "tags: ((", "modified: >", "modified: abc", "created: 5x", "!modified:",
                                  "OR type: hiprio", "type: hiprio OR", "type: bogus", "* *", "** x", "a*b*c", "*a*b", "type: !", "type: !!hiprio",
                                  QString(5000, '('), QString(5000, '!') + "milk", "milk " + QString(300, ')')};
        for (const QString &s : junk) {
            QString err;
            const Query q = Query::parse(s, &err);
            const QueryContext c = context();
            for (const Task &t : m_store->tasks()) q.matches(t, c);
        }
        QVERIFY(true);
    }

    void degradation() {
        EXPECT("(milk", "Buy milk");
        EXPECT("milk)", "Buy milk");
        EXPECT("milk OR", "Buy milk");
        EXPECT("OR milk", "Buy milk");
        EXPECT("\"milk", "Buy milk");
        EXPECT("milk AND", "Buy milk");
        EXPECT("-", "Buy milk", "Plumber visit", "Write report", "Call Mum", "Plan trip", "Book flights", "Pack bags", "Empty project",
               "Shopping", "Review budget", "Done today", "Done old", "Prepare slides", "Long window", "Naïve résumé",
               "Phone dentist", "Done yesterday");
        EXPECT("type: hiprio OR", "Plumber visit", "Write report");
        EXPECT("type: (hiprio", "Plumber visit", "Write report");
        EXPECT("type: hiprio)", "Plumber visit", "Write report");
    }

    void unknownValuesReported() {
        QString err;
        const Query q = Query::parse("type: bogus", &err);
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains("bogus"));
        QVERIFY(q.isEmpty());
        QCOMPARE(titles("type: hiprio bogus"), titles("type: hiprio"));
        QCOMPARE(titles("type: hiprio OR bogus"), titles("type: hiprio"));
        QString none;
        Query::parse("type: hiprio", &none);
        QVERIFY(none.isEmpty());
        QVERIFY(!Query::parse("modified: abc", &err).isEmpty() || !err.isEmpty());
    }

    // ---- type:

    void typeStructure() {
        EXPECT("type: proj", "Plan trip", "Empty project");
        EXPECT("type: project", "Plan trip", "Empty project");
        EXPECT("type: chck", "Shopping");
        EXPECT("type: checklist", "Shopping");
        QCOMPARE(titles("type: task"), allExcept({"Plan trip", "Empty project", "Shopping", "Book flights", "Pack bags"}));
        EXPECT("type: subtasks", "Book flights", "Pack bags");
        EXPECT("type: emptyproj", "Empty project");
        EXPECT("type: emptychck", "Shopping");
        QCOMPARE(titles("type: alltasks").size(), 17);
    }

    void typeActions() {
        EXPECT("type: call", "Call Mum");
        EXPECT("type: visit", "Plumber visit");
        EXPECT("type: email", "Phone dentist");
        EXPECT("type: sms");
        EXPECT("type: message");
        EXPECT("type: browse");
        EXPECT("type: url");
        EXPECT("type: google");
        EXPECT("type: action", "Call Mum", "Plumber visit", "Phone dentist");
    }

    void typeAttachmentsAndNotes() {
        EXPECT("type: note", "Buy milk", "Write report", "Naïve résumé", "Phone dentist");
        EXPECT("type: audio");
        EXPECT("type: pict");
        EXPECT("type: picture");
        EXPECT("type: alarms");
        EXPECT("type: anylocation");
        QCOMPARE(titles("type: nolocation").size(), 17);
    }

    void typePriority() {
        EXPECT("type: hiprio", "Plumber visit", "Write report");
        EXPECT("type: medprio", "Call Mum");
        EXPECT("type: lowprio", "Book flights", "Phone dentist");
        EXPECT("type: prio", "Plumber visit", "Write report", "Call Mum", "Book flights", "Phone dentist");
        QCOMPARE(titles("type: noprio"),
                 allExcept({"Plumber visit", "Write report", "Call Mum", "Book flights", "Phone dentist"}));
    }

    void typeStatus() {
        EXPECT("type: starred", "Plumber visit", "Review budget");
        EXPECT("type: repeat", "Call Mum");
        EXPECT("type: duration", "Review budget", "Long window");
        EXPECT("type: scheduled", "Review budget");
        EXPECT("type: inprogress", "Long window");
        EXPECT("type: paused", "Shopping");
        m_paused = [](const Task &t) { return t.title == "Pack bags"; };
        EXPECT("type: paused", "Pack bags");
    }

    void typeTagsAndDates() {
        EXPECT("type: anytag", "Buy milk", "Plumber visit", "Write report", "Book flights", "Shopping", "Done today", "Prepare slides",
               "Phone dentist");
        QCOMPARE(titles("type: notag"), titles("type: !anytag"));
        EXPECT("type: due", "Buy milk", "Plumber visit", "Write report", "Call Mum", "Prepare slides", "Long window");
        QCOMPARE(titles("type: nodue"), allExcept({"Buy milk", "Plumber visit", "Write report", "Call Mum", "Prepare slides", "Long window"}));
    }

    void typeDates() {
        QCOMPARE(titles("type: nostart"), allExcept({"Review budget"}));
        EXPECT("type: start", "Review budget");
        EXPECT("type: overdue", "Call Mum");
        EXPECT("type: today", "Buy milk", "Long window");
        EXPECT("type: duetoday", "Buy milk", "Long window");
        EXPECT("type: anytoday", "Buy milk", "Long window");
        EXPECT("type: tomorrow", "Plumber visit", "Long window");
        EXPECT("type: duetomorrow", "Plumber visit", "Long window");
        EXPECT("type: anytomorrow", "Plumber visit", "Review budget", "Long window");
        EXPECT("type: starttoday");
        EXPECT("type: starttomorrow", "Review budget");
        EXPECT("type: thisweek", "Buy milk", "Plumber visit", "Write report", "Call Mum", "Review budget", "Long window");
        EXPECT("type: anythisweek", "Buy milk", "Plumber visit", "Write report", "Call Mum", "Review budget", "Long window");
        EXPECT("type: duethisweek", "Buy milk", "Plumber visit", "Write report", "Call Mum", "Long window");
        EXPECT("type: startthisweek", "Review budget");
    }

    void typeDone() {
        EXPECT("type: donetoday", "Done today");
        EXPECT("type: doneyesterday", "Done yesterday");
        EXPECT("type: donethisweek", "Done today", "Done yesterday");
    }

    void weekIsIsoMondayToSunday() {
        // Sunday 19 July is still this week; Monday 20 July is not.
        Task t;
        t.title = "x";
        t.dueDate = jul(19);
        QueryContext c = context();
        const Query q = Query::parse("type: duethisweek");
        QVERIFY(q.matches(t, c));
        t.dueDate = jul(20);
        QVERIFY(!q.matches(t, c));
        t.dueDate = jul(12);  // Previous Sunday.
        QVERIFY(!q.matches(t, c));
        t.dueDate = jul(13);
        QVERIFY(q.matches(t, c));
        // And when "now" is a Sunday the week still starts on Monday.
        c.now = at(2026, 7, 19);
        t.dueDate = jul(13);
        QVERIFY(q.matches(t, c));
        t.dueDate = jul(20);
        QVERIFY(!q.matches(t, c));
    }

    void typeOperators() {
        EXPECT("type: hiprio OR medprio", "Plumber visit", "Write report", "Call Mum");
        EXPECT("type: hiprio, medprio", "Plumber visit", "Write report", "Call Mum");
        EXPECT("type: hiprio AND duethisweek", "Plumber visit", "Write report");
        EXPECT("type: hiprio duethisweek", "Plumber visit", "Write report");
        EXPECT("type: prio AND nodue", "Book flights", "Phone dentist");
        EXPECT("type: (proj OR chck) AND hiprio");
        EXPECT("type: (proj OR chck)", "Plan trip", "Empty project", "Shopping");
        EXPECT("type: starred AND !hiprio", "Review budget");
        EXPECT("type: starred !hiprio", "Review budget");
        EXPECT("type: !(starred OR hiprio) AND due", "Buy milk", "Call Mum", "Prepare slides", "Long window");
        EXPECT("type: overdue OR duetoday", "Call Mum", "Buy milk", "Long window");
        EXPECT("type: (duetoday OR overdue) AND hiprio");
        EXPECT("type: ((starred OR hiprio) AND due) OR today", "Plumber visit", "Write report", "Buy milk", "Long window");
        EXPECT("type: anylocation AND visit");
        EXPECT("type: (call OR email) AND !repeat", "Phone dentist");
        EXPECT("TYPE: HIPRIO", "Plumber visit", "Write report");
        EXPECT("type:hiprio", "Plumber visit", "Write report");
    }

    // ---- tags: / taggroups: / lists: / groups: / locations:

    void tagsKeyword() {
        EXPECT("tags: home", "Buy milk", "Plumber visit");
        EXPECT("tags: HOME", "Buy milk", "Plumber visit");
        EXPECT("tags: home, work", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides");
        EXPECT("tags: home OR work", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides");
        EXPECT("tags: (home OR work) AND urgent", "Prepare slides");
        EXPECT("tags: work AND !urgent", "Write report", "Book flights");
        EXPECT("tags: (work OR urgent) AND !home", "Write report", "Book flights", "Prepare slides", "Done today");
        EXPECT("tags: wo*", "Write report", "Book flights", "Prepare slides");
        EXPECT("tags: *rk", "Write report", "Book flights", "Prepare slides");
        EXPECT("tags: *or*", "Write report", "Book flights", "Prepare slides");
        EXPECT("tags: wor");  // Names match whole, not as substrings.
    }

    void tagsAnyNoneNegated() {
        EXPECT("tags: ?", "Buy milk", "Plumber visit", "Write report", "Book flights", "Shopping", "Done today", "Prepare slides",
               "Phone dentist");
        QCOMPARE(titles("tags: ~"), titles("type: notag"));
        // Has a tag, but not work.
        EXPECT("tags: !work", "Buy milk", "Plumber visit", "Shopping", "Done today", "Phone dentist");
        // Not tagged work, including untagged tasks.
        QCOMPARE(titles("tags: ~ OR !work"), allExcept({"Write report", "Book flights", "Prepare slides"}));
    }

    void tagGroupsKeyword() {
        EXPECT("taggroups: Contexts", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides", "Phone dentist");
        EXPECT("taggroups: contexts", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides", "Phone dentist");
        EXPECT("taggroups: Someday", "Shopping");
        EXPECT("taggroups: Contexts, Someday", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides",
               "Phone dentist", "Shopping");
        EXPECT("taggroups: !Someday", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides", "Phone dentist",
               "Done today");
        EXPECT("taggroups: Contexts tags: !phone", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides");
        EXPECT("taggroups: Cont*", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides", "Phone dentist");
    }

    void listsKeyword() {
        EXPECT("lists: Errands", "Buy milk", "Plumber visit", "Call Mum", "Shopping", "Naïve résumé");
        EXPECT("lists: errands", "Buy milk", "Plumber visit", "Call Mum", "Shopping", "Naïve résumé");
        EXPECT("lists: Err*", "Buy milk", "Plumber visit", "Call Mum", "Shopping", "Naïve résumé");
        EXPECT("lists: Archive", "Done today", "Done old", "Done yesterday");
        QCOMPARE(titles("lists: !Archive"), allExcept({"Done today", "Done old", "Done yesterday"}));
        QCOMPARE(titles("lists: Errands, Archive").size(), 8);
        EXPECT("lists: Errands type: duetoday", "Buy milk");
        EXPECT("lists: Nowhere");
    }

    void groupsKeyword() {
        EXPECT("groups: Home", "Buy milk", "Plumber visit", "Call Mum", "Shopping", "Naïve résumé");
        EXPECT("groups: home", "Buy milk", "Plumber visit", "Call Mum", "Shopping", "Naïve résumé");
        QCOMPARE(titles("groups: !Home"), allExcept({"Buy milk", "Plumber visit", "Call Mum", "Shopping", "Naïve résumé"}));
        QCOMPARE(titles("groups: home, work").size(), 17);
        EXPECT("groups: Home type: hiprio", "Plumber visit");
    }

    void locationsKeyword() {
        EXPECT("locations: home");
        EXPECT("locations: ?");
        EXPECT("locations: !home");
        QCOMPARE(titles("locations: ~").size(), 17);
        QCOMPARE(titles("locations: ~ OR !home").size(), 17);
    }

    void nameKeywordsWithSpaces() {
        // Unquoted multi-word names join into one name; quotes work too.
        const qint64 id = addList("Big Projects", 0);
        addTask(id, "Spaced");
        EXPECT("lists: Big Projects", "Spaced");
        EXPECT("lists: \"Big Projects\"", "Spaced");
        EXPECT("lists: big projects", "Spaced");
    }

    // ---- modified: / created: / completed:

    void modifiedKeyword() {
        // The store stamps `modified` itself, so use free-standing tasks.
        auto ageOf = [](qint64 secs) {
            Task t;
            t.modified = Now.addSecs(-secs);
            return t;
        };
        const QueryContext c = context();
        auto m = [&](const QString &q, qint64 secs) { return Query::parse(q).matches(ageOf(secs), c); };
        QVERIFY(m("modified: 1h", 30 * 60));
        QVERIFY(!m("modified: 1h", 2 * 3600));
        QVERIFY(m("modified: <3h", 2 * 3600));
        QVERIFY(m("modified: 45mins", 30 * 60));
        QVERIFY(!m("modified: 45mins", 50 * 60));
        QVERIFY(m("modified: 2d", 86400));
        QVERIFY(!m("modified: 2d", 3 * 86400));
        QVERIFY(m("modified: >2d", 3 * 86400));
        QVERIFY(!m("modified: >2d", 86400));
        QVERIFY(m("!modified: 2d", 3 * 86400));
        QVERIFY(!m("!modified: 2d", 3600));
        QVERIFY(m("MODIFIED: 3H", 3600));
        QVERIFY(m("modified: 2 hours", 3600));
        QVERIFY(!m("modified: 2 hours", 3 * 3600));
        QVERIFY(m("modified: 0.5d", 11 * 3600));
        QVERIFY(!m("modified: 0.5d", 13 * 3600));
        QVERIFY(m("modified: 1.5h", 80 * 60));
        QVERIFY(!m("modified: 1.5h", 100 * 60));
        QVERIFY(m("modified: 2w", 13 * 86400));
        QVERIFY(!m("modified: 2w", 15 * 86400));
        QVERIFY(m("modified: 1m", 30 * 86400));   // A month is 30.5 days.
        QVERIFY(!m("modified: 1m", 31 * 86400));
        QVERIFY(m("modified: 1y", 364 * 86400));
        QVERIFY(m("modified: 2d, 1h", 86400));    // A comma list is an OR.
        Task never;
        QVERIFY(!Query::parse("modified: 1d").matches(never, c));
    }

    void createdKeyword() {
        EXPECT("created: 2h", "Buy milk");
        EXPECT("created: 2d", "Buy milk");
        EXPECT("created: 4d", "Buy milk", "Prepare slides");
        EXPECT("created: <4d", "Buy milk", "Prepare slides");
        EXPECT("created: 1w", "Buy milk", "Prepare slides");
        QCOMPARE(titles("created: >14d"), allExcept({"Buy milk", "Prepare slides"}));
        QCOMPARE(titles("created: 1 month").size(), 17);  // A month is 30.5 days.
        QCOMPARE(titles("created: >1m").size(), 0);
        QCOMPARE(titles("created: 1y").size(), 17);
    }

    void completedKeyword() {
        EXPECT("completed: 1d", "Done today", "Done yesterday");
        EXPECT("completed: 2h", "Done today");
        EXPECT("completed: 7d", "Done today", "Done yesterday");
        EXPECT("completed: >5d", "Done old", "Pack bags");
        EXPECT("completed: 30d", "Done today", "Done old", "Done yesterday", "Pack bags");
        QCOMPARE(titles("!completed: 30d"), allExcept({"Done today", "Done old", "Done yesterday", "Pack bags"}));
    }

    // ---- combining

    void combiningSections() {
        EXPECT("plumber type: starred", "Plumber visit");
        EXPECT("plumber type: starred tags: home groups: Home", "Plumber visit");
        EXPECT("milk lists: Errands", "Buy milk");
        EXPECT("milk lists: Archive");
        EXPECT("type: duetoday tags: home OR school", "Buy milk");
        EXPECT("type: duetoday tags: home OR school lists: Errands", "Buy milk");
    }

    void orBranching() {
        EXPECT("type: hiprio OR tags: urgent", "Plumber visit", "Write report", "Done today", "Prepare slides");
        EXPECT("type: overdue OR tags: urgent", "Call Mum", "Done today", "Prepare slides");
        // (milk AND hiprio) OR urgent OR completed within a day.
        EXPECT("milk type: hiprio OR tags: urgent OR completed: 2h", "Done today", "Prepare slides");
        EXPECT("plumber type: hiprio OR tags: urgent", "Plumber visit", "Done today", "Prepare slides");
        // A leading OR is ignored; a trailing one too.
        EXPECT("OR type: hiprio", "Plumber visit", "Write report");
        EXPECT("type: hiprio OR tags: urgent OR", "Plumber visit", "Write report", "Done today", "Prepare slides");
    }

    void manualExamples() {
        EXPECT("tags: !work type: (hiprio AND thisweek) OR starred groups: !work", "Plumber visit");
        EXPECT("type: (hiprio AND thisweek) OR starred", "Plumber visit", "Write report", "Review budget");
        EXPECT("tags: !work groups: !work", "Buy milk", "Plumber visit", "Shopping");
        // Section-level checks of the examples from the manual.
        EXPECT("type: overdue OR duetoday", "Call Mum", "Buy milk", "Long window");
        EXPECT("type: prio OR starred", "Plumber visit", "Write report", "Call Mum", "Book flights", "Phone dentist", "Review budget");
        EXPECT("type: ((starred OR hiprio) AND due) OR today", "Plumber visit", "Write report", "Buy milk", "Long window");
        EXPECT("tags: someday type: prio OR starred groups: shopping");
        EXPECT("taggroups: Contexts tags: !phone", "Buy milk", "Plumber visit", "Write report", "Book flights", "Prepare slides");
    }

    void manualTextExamples() {
        // The sample tasks from the full-text table.
        auto fresh = [&]() {
            for (const Task &t : m_store->tasks()) {
                Task d = t;
                d.deleted = true;
                m_store->begin("x");
                m_store->putTask(d);
                m_store->commit();
            }
        };
        fresh();
        const qint64 l = m_store->lists().begin().key();
        for (const char *s : {"Get a haircut one day", "Buy a new car some day", "Find a new job", "Call: Mum, tell about my new job", "Call: Tim"})
            addTask(l, s);
        auto live = [&](const QString &q) {
            const Query query = Query::parse(q);
            const QueryContext c = context();
            QStringList out;
            for (const Task &t : m_store->tasks())
                if (!t.deleted && t.title.size() && query.matches(t, c)) out << t.title;
            out.sort();
            return out;
        };
        QCOMPARE(live("new job"), sorted({"Find a new job", "Call: Mum, tell about my new job"}));
        QCOMPARE(live("new job -find"), sorted({"Call: Mum, tell about my new job"}));
        QCOMPARE(live("a day"), sorted({"Get a haircut one day", "Buy a new car some day"}));
        QCOMPARE(live("Call OR new"),
                 sorted({"Buy a new car some day", "Find a new job", "Call: Mum, tell about my new job", "Call: Tim"}));
        QCOMPARE(live("\"new job\""), sorted({"Find a new job", "Call: Mum, tell about my new job"}));
        QCOMPARE(live("ject"), QStringList());
    }

    // ---- {datefilter}

    void dateFilter() {
        QCOMPARE(titles("{datefilter}").size(), 17);  // Unset range matches everything.
        m_from = jul(16);
        m_to = jul(17);
        EXPECT("{datefilter}", "Plumber visit", "Write report", "Long window");
        EXPECT("type: starred OR {datefilter}", "Plumber visit", "Write report", "Review budget", "Long window");
        EXPECT("type: inprogress OR {datefilter}", "Plumber visit", "Write report", "Long window");
        EXPECT("type: starred {datefilter}", "Plumber visit");
        m_from = jul(14);
        m_to = jul(14);
        EXPECT("{datefilter}", "Call Mum");
        m_from = jul(18);
        m_to = QDate();
        EXPECT("{datefilter}", "Prepare slides");
        m_from = QDate();
        m_to = jul(14);
        EXPECT("{datefilter}", "Call Mum");
    }

    void noStoreIsSafe() {
        QueryContext c;
        c.now = Now;
        Task t;
        t.title = "milk";
        t.tagIds = {1, 2};
        QVERIFY(Query::parse("milk").matches(t, c));
        QVERIFY(!Query::parse("tags: x").matches(t, c));
        QVERIFY(!Query::parse("lists: x").matches(t, c));
        QVERIFY(!Query::parse("groups: x").matches(t, c));
        QVERIFY(!Query::parse("taggroups: x").matches(t, c));
        QVERIFY(!Query::parse("type: emptyproj").matches(t, c));
        QVERIFY(Query::parse("type: notag").matches(t, c));
    }

    void flagsNameCompletedAndScope() {
        QVERIFY(Query::parse("completed: 7d").namesCompleted());
        QVERIFY(Query::parse("type: donetoday OR overdue").namesCompleted());
        QVERIFY(!Query::parse("type: !donetoday").namesCompleted());
        QVERIFY(!Query::parse("!completed: 3").namesCompleted());
        QVERIFY(!Query::parse("type: overdue").namesCompleted());
        QVERIFY(Query::parse("lists: Work").replacesScope());
        QVERIFY(Query::parse("groups: Home").replacesScope());
        QVERIFY(Query::parse("completed: 1d").replacesScope());
        QVERIFY(!Query::parse("tags: x milk").replacesScope());
        QVERIFY(!Query::parse("milk").namesCompleted());
    }

    void keywordGroupsCombineFilters() {
        EXPECT("(tags: work OR type: starred) lists: Office", "Book flights", "Prepare slides", "Review budget", "Write report");
        EXPECT("tags: home OR (lists: Office type: starred)", "Buy milk", "Plumber visit", "Review budget");
        QString error;
        titles("(tags: urgent OR tags: nothing)", &error);
        QVERIFY(error.isEmpty());
        // A group that opens with a plain word is ordinary text grouping.
        QCOMPARE(titles("(milk OR plumber)"), titles("milk OR plumber"));
    }

    void negatedGroupExcludes() {
        EXPECT("lists: Errands !(tags: home)", "Call Mum", "Naïve résumé", "Shopping");
        QVERIFY(Query::parse("(completed: 7d)").namesCompleted());
    }
};

REGISTER_TEST(QueryTest)
#include "test_query.moc"
