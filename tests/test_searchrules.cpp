#include <QtTest>
#include <memory>

#include "query.h"
#include "searchrules.h"
#include "store.h"
#include "testregistry.h"

namespace {

using SearchRules::fromType;
using SearchRules::toType;

QVariantMap clause(const QString &prop, bool neg, const QString &value) {
    return {{"prop", prop}, {"neg", neg}, {"value", value}};
}

QVariantMap group(const QString &match, const QVariantList &items) { return {{"match", match}, {"items", items}}; }

QVariantMap all(const QVariantList &items) { return group("all", items); }
QVariantMap any(const QVariantList &items) { return group("any", items); }

// A compact form for asserting trees: all(a b any(c d)) with a leading "!"
// for a negative clause, writing the property key and value token.
QString show(const QVariantMap &g) {
    QStringList parts;
    for (const QVariant &v : g.value("items").toList()) {
        const QVariantMap m = v.toMap();
        if (m.contains("items")) parts << show(m);
        else parts << QString(m.value("neg").toBool() ? "!" : "") + m.value("prop").toString() + "=" + m.value("value").toString();
    }
    return g.value("match").toString() + "(" + parts.join(" ") + ")";
}

QString parsed(const QString &type) {
    bool ok = false;
    const QVariantMap t = fromType(type, &ok);
    return ok ? show(t) : QStringLiteral("NOT OK");
}

QDateTime at(int y, int m, int d, int hh = 10, int mm = 30) { return QDateTime(QDate(y, m, d), QTime(hh, mm)); }
const QDateTime Now = at(2026, 7, 15);

}  // namespace

class SearchRulesTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Store> m_store;

    void addTask(const QString &title, std::function<void(Task &)> edit = {}) {
        const qint64 id = m_store->nextId("tasks");
        m_store->begin("tasks");
        Task t;
        t.id = id;
        t.listId = 0;
        t.title = title;
        t.order = double(id);
        t.created = Now.addDays(-30);
        t.modified = Now.addDays(-10);
        if (edit) edit(t);
        m_store->putTask(t);
        m_store->commit();
    }

    QStringList titles(const QString &text) const {
        const Query q = Query::parse(text);
        QueryContext c;
        c.store = m_store.get();
        c.now = Now;
        QStringList out;
        for (const Task &t : m_store->tasks())
            if (q.matches(t, c)) out << t.title;
        out.sort();
        return out;
    }

private slots:
    void init() {
        m_dir = std::make_unique<QTemporaryDir>();
        m_store = std::make_unique<Store>();
        QVERIFY(m_store->open(m_dir->filePath("r.db")));
        addTask("Due today", [](Task &t) { t.dueDate = Now.date(); });
        addTask("Overdue", [](Task &t) { t.dueDate = Now.date().addDays(-2); });
        addTask("Starred today", [](Task &t) { t.dueDate = Now.date(); t.starred = true; });
        addTask("Starred later", [](Task &t) { t.dueDate = Now.date().addDays(3); t.starred = true; });
        addTask("Low today", [](Task &t) { t.dueDate = Now.date(); t.priority = Priority::Low; });
        addTask("High", [](Task &t) { t.priority = Priority::High; });
        addTask("Plain");
    }

    // ---- every property, value and sign

    void everyPropertyRoundTrips() {
        const QVariantList props = SearchRules::properties();
        QVERIFY(props.size() >= 16);
        int clauses = 0;
        for (const QVariant &pv : props) {
            const QVariantMap p = pv.toMap();
            QCOMPARE(p.value("ops").toStringList().size(), 2);
            const QVariantList values = p.value("values").toList();
            QCOMPARE(p.value("single").toBool(), values.size() == 1);
            for (const QVariant &vv : values) {
                const QString token = vv.toMap().value("token").toString();
                QVERIFY(!vv.toMap().value("label").toString().isEmpty());
                for (bool neg : {false, true}) {
                    const QVariantMap tree = all({clause(p.value("key").toString(), neg, token)});
                    const QString text = toType(tree);
                    QVERIFY2(!text.isEmpty(), qPrintable(token));
                    bool ok = false;
                    const QVariantMap back = fromType(text, &ok);
                    QVERIFY2(ok, qPrintable(text));
                    QCOMPARE(back, tree);
                    ++clauses;
                }
            }
        }
        QVERIFY(clauses > 60);
    }

    void writtenForms() {
        QCOMPARE(toType(all({clause("due", false, "duetoday")})), QString("duetoday"));
        QCOMPARE(toType(all({clause("priority", true, "lowprio")})), QString("!lowprio"));
        QCOMPARE(toType(any({clause("due", false, "duetoday"), clause("due", false, "overdue")})),
                 QString("duetoday OR overdue"));
        QCOMPARE(toType(all({clause("starred", false, "starred"), any({clause("due", false, "duetoday"), clause("due", false, "overdue")}),
                             clause("priority", true, "lowprio")})),
                 QString("starred AND (duetoday OR overdue) AND !lowprio"));
        // The negative tag and location are written as their own tokens.
        QCOMPARE(toType(all({clause("tag", true, "anytag")})), QString("notag"));
        QCOMPARE(toType(all({clause("tag", false, "anytag")})), QString("anytag"));
        QCOMPARE(toType(all({clause("location", true, "anylocation")})), QString("nolocation"));
        QCOMPARE(toType(all({clause("location", false, "anylocation")})), QString("anylocation"));
    }

    void emptyAndOddTrees() {
        QCOMPARE(toType({}), QString());
        QCOMPARE(toType(all({})), QString());
        QCOMPARE(toType(all({all({}), any({})})), QString());
        // An empty nested group disappears; a single-item group is not wrapped.
        QCOMPARE(toType(all({clause("starred", false, "starred"), any({})})), QString("starred"));
        QCOMPARE(toType(all({clause("starred", false, "starred"), any({clause("note", false, "note")})})),
                 QString("starred AND note"));
        // An unknown value falls back to the property's default, an unknown property is skipped.
        QCOMPARE(toType(all({clause("due", false, "bogus")})), QString("due"));
        QCOMPARE(toType(all({clause("nonsense", false, "x"), clause("note", false, "note")})), QString("note"));
    }

    // ---- aliases

    void aliasesFold() {
        const QList<QPair<QString, QString>> pairs = {
            {"today", "due=duetoday"},       {"tomorrow", "due=duetomorrow"}, {"thisweek", "any=anythisweek"},
            {"tasksoverdue", "due=overdue"}, {"proj", "task=project"},         {"chck", "task=checklist"},
            {"message", "action=sms"},       {"mail", "action=email"},         {"browse", "action=url"},
            {"pict", "picture=picture"}};
        for (const auto &p : pairs) QCOMPARE(parsed(p.first), QString("all(%1)").arg(p.second));
        QCOMPARE(toType(fromType("today")), QString("duetoday"));
    }

    void tagAndLocationNegatives() {
        QCOMPARE(parsed("notag"), QString("all(!tag=anytag)"));
        QCOMPARE(parsed("!notag"), QString("all(tag=anytag)"));
        QCOMPARE(parsed("!anytag"), QString("all(!tag=anytag)"));
        QCOMPARE(parsed("anytag"), QString("all(tag=anytag)"));
        QCOMPARE(parsed("nolocation"), QString("all(!location=anylocation)"));
        QCOMPARE(parsed("!nolocation"), QString("all(location=anylocation)"));
        QCOMPARE(parsed("!anylocation"), QString("all(!location=anylocation)"));
    }

    // ---- nesting and flattening

    void flatteningAndNesting() {
        QCOMPARE(parsed("a"), QString("NOT OK"));
        QCOMPARE(parsed("(starred AND note) AND duration"), QString("all(starred=starred note=note duration=duration)"));
        QCOMPARE(parsed("starred AND (note AND duration)"), QString("all(starred=starred note=note duration=duration)"));
        QCOMPARE(parsed("(starred OR note) OR duration"), QString("any(starred=starred note=note duration=duration)"));
        QCOMPARE(parsed("((starred))"), QString("all(starred=starred)"));
        QCOMPARE(parsed("(starred OR note)"), QString("any(starred=starred note=note)"));
        // AND binds tighter than OR.
        QCOMPARE(parsed("starred OR note AND duration"), QString("any(starred=starred all(note=note duration=duration))"));
        QCOMPARE(parsed("starred AND note OR duration"), QString("any(all(starred=starred note=note) duration=duration)"));
        // Three levels.
        QCOMPARE(parsed("starred AND (note OR (duration AND (repeat OR audio)))"),
                 QString("all(starred=starred any(note=note all(duration=duration any(repeat=repeat audio=audio))))"));
        // Implicit AND between neighbours.
        QCOMPARE(parsed("starred note"), QString("all(starred=starred note=note)"));
    }

    void threeLevelsRoundTrip() {
        const QVariantMap tree = all({clause("starred", false, "starred"),
                                      any({clause("note", false, "note"),
                                           all({clause("duration", false, "duration"),
                                                any({clause("repeat", false, "repeat"), clause("audio", true, "audio")})})})});
        const QString text = toType(tree);
        QCOMPARE(text, QString("starred AND (note OR (duration AND (repeat OR !audio)))"));
        bool ok = false;
        QCOMPARE(fromType(text, &ok), tree);
        QVERIFY(ok);
    }

    void deepestNestingLimit() {
        QString deep = "starred";
        for (int i = 0; i < 30; ++i) deep = QString("(note OR (%1 AND duration))").arg(deep);
        QVERIFY(parsed(deep) != "NOT OK");
        QString tooDeep = "starred";
        for (int i = 0; i < 70; ++i) tooDeep = "(" + tooDeep + ")";
        QCOMPARE(parsed(tooDeep), QString("NOT OK"));
    }

    // ---- the chip builder's old output

    void oldChipForms() {
        QCOMPARE(parsed("duetoday, overdue"), QString("any(due=duetoday due=overdue)"));
        QCOMPARE(parsed("duetoday OR overdue"), QString("any(due=duetoday due=overdue)"));
        QCOMPARE(parsed("(hiprio OR medprio) AND !starred"), QString("all(any(priority=hiprio priority=medprio) !starred=starred)"));
        QCOMPARE(parsed("hiprio AND medprio"), QString("all(priority=hiprio priority=medprio)"));
        QCOMPARE(parsed("hiprio AND !starred AND !note"), QString("all(priority=hiprio !starred=starred !note=note)"));
        QCOMPARE(parsed("!hiprio"), QString("all(!priority=hiprio)"));
        QCOMPARE(parsed("(overdue OR duetoday)"), QString("any(due=overdue due=duetoday)"));
    }

    void commaIsOr() {
        QCOMPARE(parsed("starred, note"), QString("any(starred=starred note=note)"));
        QCOMPARE(parsed("starred,note,duration"), QString("any(starred=starred note=note duration=duration)"));
        QCOMPARE(parsed("starred AND note, duration"), QString("any(all(starred=starred note=note) duration=duration)"));
    }

    void keywordsAndCaseAndWhitespace() {
        QCOMPARE(parsed("STARRED and NOTE"), QString("all(starred=starred note=note)"));
        QCOMPARE(parsed("starred Or note"), QString("any(starred=starred note=note)"));
        QCOMPARE(parsed("  starred \t AND\n note  "), QString("all(starred=starred note=note)"));
        QCOMPARE(parsed("(starred OR note)AND(duration)"), QString("all(any(starred=starred note=note) duration=duration)"));
        QCOMPARE(parsed("!HiPrio"), QString("all(!priority=hiprio)"));
        QCOMPARE(parsed("Today"), QString("all(due=duetoday)"));
    }

    // ---- what stays as text

    void unrepresentable() {
        for (const QString &s : QStringList{"", "   ", "alltasks", "capturedmail", "starred AND alltasks", "bogus", "starred OR bogus",
                                            "!(starred OR note)", "!!starred", "!", "starred !", "(starred", "starred)", "()", "starred OR",
                                            "OR starred", "starred OR OR note", "\"starred\"", "starred AND \"note\"", "tags: x"}) {
            bool ok = true;
            const QVariantMap t = fromType(s, &ok);
            QVERIFY2(!ok, qPrintable(s));
            QVERIFY2(t.isEmpty(), qPrintable(s));
        }
    }

    // ---- the real query engine agrees

    void generatedQueriesMatchHandWritten() {
        struct Case {
            QVariantMap tree;
            QString hand;
        };
        const QList<Case> cases = {
            {all({clause("due", false, "duetoday")}), "type: duetoday"},
            {any({clause("due", false, "duetoday"), clause("due", false, "overdue")}), "type: duetoday OR overdue"},
            {all({clause("starred", false, "starred"), any({clause("due", false, "duetoday"), clause("due", false, "overdue")}),
                  clause("priority", true, "lowprio")}),
             "type: starred AND (duetoday OR overdue) AND !lowprio"},
            {all({clause("tag", true, "anytag")}), "type: notag"},
            {any({clause("starred", false, "starred"), all({clause("priority", false, "hiprio"), clause("due", true, "due")})}),
             "type: starred OR (hiprio AND !due)"},
        };
        for (const Case &c : cases) {
            const QString generated = "type: " + toType(c.tree);
            QCOMPARE(generated, c.hand);
            QCOMPARE(titles(generated), titles(c.hand));
            QVERIFY2(!titles(generated).isEmpty(), qPrintable(generated));
            // And the same tasks come back after a parse and re-write.
            bool ok = false;
            const QVariantMap back = fromType(toType(c.tree), &ok);
            QVERIFY(ok);
            QCOMPARE(titles("type: " + toType(back)), titles(generated));
        }
        // The recipe from the manual: Today and Overdue.
        QCOMPARE(titles("type: " + toType(any({clause("due", false, "duetoday"), clause("due", false, "overdue")}))),
                 (QStringList{"Due today", "Low today", "Overdue", "Starred today"}));
        // Same tasks as the hand-written all-of-them form.
        QCOMPARE(titles("type: " + toType(all({clause("starred", false, "starred"), clause("due", false, "duetoday")}))),
                 (QStringList{"Starred today"}));
    }
};

REGISTER_TEST(SearchRulesTest)
#include "test_searchrules.moc"
