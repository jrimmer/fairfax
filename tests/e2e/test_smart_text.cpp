// End-to-end scenarios for free-text search (smart-lists.md, Full Text Search
// Operators; Two Search Modes; Quick Find vs. Search): every operator in the
// manual's table, Full Text against Sounds Like, and the Task Find bar that
// jumps instead of filtering. A small world holds the manual's own sample
// tasks.

#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

const SL Everything{"Get a haircut one day", "Buy a new car some day", "Find a new job", "Call: Mum, tell about my new job",
                    "Call: Tim", "Renew passport", "Read the handbook", "Pack the notebook", "Visit the bookshop",
                    "Review the project", "Pick a subject", "Rejected idea", "Call the plumber", "Pay Stephen",
                    "Thank Alexander", "Ring John", "Quiet title", "Email a@b.com", "Tag text #garden thing",
                    "Buy bread and butter", "Walk or run", "Elsewhere job offer"};

qint64 textTask(App &app, const QString &title) {
    for (const Task &t : app.store().tasks())
        if (!t.deleted && t.title == title)
            return t.id;
    return 0;
}

QObject *commandBar() { return findObject("commandPopup"); }

bool barOpen() {
    QObject *bar = commandBar();
    return bar && bar->property("visible").toBool();
}

QStringList barItems() {
    QStringList out;
    if (QObject *bar = commandBar())
        for (const QVariant &v : bar->property("items").toList())
            out << v.toMap().value("label").toString();
    return out;
}

}  // namespace

class E2ESmartText : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;

    static void search(App &app, const QString &q) { QVERIFY2(typeSearch(app, q), qPrintable(q)); }

private slots:
    void initTestCase() {
        m_app = std::make_unique<App>(seedText);
        QVERIFY(m_app->ok);
        QVERIFY(waitUntil([&] { return m_app->openSource("all"); }));
    }
    void cleanupTestCase() { m_app.reset(); }
    void cleanup() {
        if (!m_app)
            return;
        closeMenus(*m_app);
        closeBuilder(*m_app);
        escapeSearch(*m_app);
        if (m_app->backend().soundex())
            m_app->backend().setSoundex(false);
    }

    void worldBaseline() { SMART_SHOWS(*m_app, Everything); }

    // The operator table (smart-lists.md, Full Text Search Operators), with
    // Soundex off. "word: A bare word matches anywhere inside a word"; "word1
    // word2: Implicit AND"; "word1 OR word2"; "word1 AND word2"; "word -word2
    // excludes a word"; "!word"; "word*" prefix; "*word" suffix; "*word*"
    // contains; "exact phrase"; "*" matches every task.
    void fullTextOperators_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("bare word matches inside words") << "ject" << SL{"Review the project", "Pick a subject", "Rejected idea"};
        QTest::newRow("implicit and") << "new job" << SL{"Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("excluded with dash") << "new job -find" << SL{"Call: Mum, tell about my new job"};
        QTest::newRow("excluded with bang") << "new job !find" << SL{"Call: Mum, tell about my new job"};
        QTest::newRow("bang alone") << "!call" << without(Everything, SL{"Call: Mum, tell about my new job", "Call: Tim", "Call the plumber"});
        QTest::newRow("short words") << "a day" << SL{"Get a haircut one day", "Buy a new car some day"};
        QTest::newRow("or") << "Call OR new"
                            << SL{"Buy a new car some day", "Find a new job", "Call: Mum, tell about my new job", "Call: Tim",
                                  "Call the plumber", "Renew passport"};
        QTest::newRow("or in lowercase") << "call or new"
                                         << SL{"Buy a new car some day", "Find a new job", "Call: Mum, tell about my new job",
                                               "Call: Tim", "Call the plumber", "Renew passport"};
        QTest::newRow("explicit and") << "new AND job" << SL{"Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("and in lowercase") << "new and job" << SL{"Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("prefix wildcard") << "new*" << SL{"Buy a new car some day", "Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("suffix wildcard") << "*book" << SL{"Read the handbook", "Pack the notebook"};
        QTest::newRow("contains wildcard") << "*ject*" << SL{"Review the project", "Pick a subject", "Rejected idea"};
        QTest::newRow("exact phrase") << "\"new job\"" << SL{"Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("phrase in order") << "\"a new\"" << SL{"Buy a new car some day", "Find a new job"};
        QTest::newRow("phrase out of order") << "\"job new\"" << SL{};
        QTest::newRow("star matches every task") << "*" << Everything;
        QTest::newRow("quoted and") << "\"and\"" << SL{"Read the handbook", "Thank Alexander", "Buy bread and butter"};
        QTest::newRow("quoted or") << "\"or\"" << SL{"Walk or run", "Renew passport"};
        QTest::newRow("case is ignored") << "NEW JOB" << SL{"Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("negated phrase") << "new -\"new job\"" << SL{"Buy a new car some day", "Renew passport"};
        QTest::newRow("brackets") << "new (job OR car)"
                                  << SL{"Buy a new car some day", "Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("prefix and word") << "new* job" << SL{"Find a new job", "Call: Mum, tell about my new job"};
        QTest::newRow("notes are searched") << "plumber" << SL{"Call the plumber", "Quiet title"};
        QTest::newRow("tag names are searched") << "tagged" << SL{"Quiet title"};
        QTest::newRow("exclude within matches") << "call -plumber" << SL{"Call: Mum, tell about my new job", "Call: Tim"};
        QTest::newRow("hash in text") << "#garden" << SL{"Tag text #garden thing"};
        QTest::newRow("hash finds a tag") << "#tagged" << SL{"Quiet title"};
        QTest::newRow("at finds a tag") << "@tagged" << SL{"Quiet title"};
        QTest::newRow("email address") << "a@b.com" << SL{"Email a@b.com"};
        QTest::newRow("nothing") << "zzzz" << SL{};
        QTest::newRow("star with a keyword") << "* tags: tagged" << SL{"Quiet title"};
    }
    void fullTextOperators() {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        QVERIFY(!m_app->backend().soundex());
        SMART_QUERY(*m_app, query, expected);
    }

    // "Free text is matched against each task's title, notes, and searchable
    // metadata" and "You can't free-text-search for the literal words and /
    // or on their own; wrap them in quotes": a lone operator filters nothing.
    void loneOperatorFiltersNothing() {
        App &app = *m_app;
        SMART_QUERY(app, "or", Everything);
        SMART_QUERY(app, "and", Everything);
        SMART_QUERY(app, "\"and\"", (SL{"Read the handbook", "Thank Alexander", "Buy bread and butter"}));
    }

    // "Soundex Search: sound-alike matching. It tolerates common spelling
    // mistakes, missing letters, and pronunciation variants": plumver finds
    // plumber, Steven finds Stephen, alexnder finds Alexander, Jon finds John.
    // Full Text on its own does not.
    void soundsLikeFindsMisspellings_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("plumver") << "plumver" << SL{"Call the plumber", "Quiet title"};
        QTest::newRow("Steven") << "Steven" << SL{"Pay Stephen"};
        QTest::newRow("alexnder") << "alexnder" << SL{"Thank Alexander"};
        QTest::newRow("Jon") << "Jon" << SL{"Ring John"};
    }
    void soundsLikeFindsMisspellings() {
        App &app = *m_app;
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        // Full Text: the misspelling finds nothing.
        SMART_QUERY(app, query, SL{});
        // Sounds Like, from the checkbox next to the query.
        QQuickItem *box = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((box = findItem("searchSoundex")) != nullptr, 3000);
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().soundex(), 3000);
        QCOMPARE(app.store().setting("search.soundex").toBool(), true);
        SMART_SHOWS(app, expected);
        // Reversed: back to Full Text, the misspelling finds nothing again.
        click(findItem("searchSoundex"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().soundex(), 3000);
        QCOMPARE(app.store().setting("search.soundex").toBool(), false);
        SMART_SHOWS(app, SL{});
    }

    // "A few characters change matching mode automatically: including # or @
    // in your text, or wrapping the whole query in "quotes", turns Soundex off
    // for that search so the match stays literal."
    void literalSearchesStayLiteralWithSoundex() {
        App &app = *m_app;
        app.backend().setSoundex(true);
        SMART_QUERY(app, "plumver", (SL{"Call the plumber", "Quiet title"}));
        SMART_QUERY(app, "\"plumver\"", SL{});
        SMART_QUERY(app, "#garden", SL{"Tag text #garden thing"});
        SMART_QUERY(app, "#gardn", SL{});
        SMART_QUERY(app, "a@b.com", SL{"Email a@b.com"});
        SMART_QUERY(app, "a@b.cum", SL{});
        SMART_QUERY(app, "new job -find", (SL{"Call: Mum, tell about my new job"}));
    }

    // The same switch lives in Search Options ("The Soundex switch").
    void soundexSwitchInSearchOptions() {
        App &app = *m_app;
        SMART_QUERY(app, "Steven", SL{});
        app.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        LIST_ITEM(box, findItem("soundexBox"));
        QTRY_VERIFY_WITH_TIMEOUT(!box->property("checked").toBool(), 2000);
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().soundex(), 3000);
        SMART_SHOWS(app, SL{"Pay Stephen"});
        click(findItem("soundexBox"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().soundex(), 3000);
        SMART_SHOWS(app, SL{});
        QVERIFY(closeBuilder(app));
    }

    // "Quick Find (F while focused on the Task List): jumps the selection to
    // the first task whose title matches, without filtering anything out."
    // "Search (⌘F): filters… The list collapses to just the results."
    void quickFindJumpsSearchFilters() {
        App &app = *m_app;
        // Quick Find: the list keeps every row, only the selection moves.
        QVERIFY(selectRowText(app, "Call: Tim"));
        const qint64 tim = textTask(app, "Call: Tim");
        QCOMPARE(app.backend().currentTaskId(), tim);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 3000);
        LIST_ITEM(input, findItem("commandInput"));
        QTRY_VERIFY_WITH_TIMEOUT(input->property("activeFocus").toBool(), 3000);
        app.type("handbook");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Read the handbook"}), 3000);
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), textTask(app, "Read the handbook"), 3000);
        QCOMPARE(app.backend().search(), QString());
        QCOMPARE(paneTitles(app), sorted(Everything));
        // Search: the list collapses to the match, and keeps it until cleared.
        search(app, "handbook");
        SMART_SHOWS(app, SL{"Read the handbook"});
        QVERIFY(escapeSearch(app));
        SMART_SHOWS(app, Everything);
    }

    // Task Find bar: "Type and matching items highlight as you go; the first
    // match is focused right away. ↑ / ↓ move between matches, wrapping at the
    // ends. Return commits the focused match and closes the bar. Esc cancels."
    void taskFindBarKeys() {
        App &app = *m_app;
        QVERIFY(selectRowText(app, "Call: Tim"));
        const qint64 start = app.backend().currentTaskId();
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 3000);
        app.type("job");
        QTRY_COMPARE_WITH_TIMEOUT(barItems().size(), 3, 3000);
        QCOMPARE(sorted(barItems()), sorted(SL{"Find a new job", "Call: Mum, tell about my new job", "Elsewhere job offer"}));
        QCOMPARE(commandBar()->property("highlighted").toInt(), 0);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 1, 2000);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 2, 2000);
        // Wrapping: down from the last match goes to the first, up from the
        // first goes to the last.
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 0, 2000);
        app.press(Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 2, 2000);
        app.press(Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 1, 2000);
        const QString second = barItems().at(1);
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.backend().currentTaskId()).title, second, 3000);
        // Esc cancels: the selection stays where it was.
        const qint64 before = app.backend().currentTaskId();
        QVERIFY(before != start);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 3000);
        app.type("tim");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Call: Tim"}), 3000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 3000);
        QCOMPARE(app.backend().currentTaskId(), before);
        QCOMPARE(paneTitles(app), sorted(Everything));
    }

    // "Type any part of the task title. Matching tasks highlight, and the
    // selection moves to the first match right away."
    void taskFindMovesSelectionWhileTyping() {
        App &app = *m_app;
        QVERIFY(selectRowText(app, "Call: Tim"));
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 3000);
        app.type("handbook");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Read the handbook"}), 3000);
        QEXPECT_FAIL("", "GAP: the Task Find bar lists matches and moves the selection only on Return", Continue);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), textTask(app, "Read the handbook"), 1000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 3000);
    }

    // "An exact-match toggle on the bar switches between loose and exact word
    // matching; each bar remembers its own setting."
    void findBarExactMatchToggle() {
        App &app = *m_app;
        QVERIFY(selectRowText(app, "Call: Tim"));
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 3000);
        app.type("ject");
        QTRY_COMPARE_WITH_TIMEOUT(barItems().size(), 3, 3000);
        app.press(Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(commandBar()->property("exact").toBool(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(barItems().size(), 0, 3000);
        QCOMPARE(app.store().setting("bar.exact.find").toBool(), true);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 3000);
        // The bar remembers its own setting: reopened, it is still exact.
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 3000);
        QVERIFY(commandBar()->property("exact").toBool());
        app.type("project");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Review the project"}), 3000);
        // Reversed: loose again.
        app.press(Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!commandBar()->property("exact").toBool(), 2000);
        QCOMPARE(app.store().setting("bar.exact.find").toBool(), false);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 3000);
    }

private:
    // Clicks the row showing this title, which gives the list focus.
    static bool selectRowText(App &app, const QString &title) {
        const qint64 id = textTask(app, title);
        QQuickItem *row = nullptr;
        if (!waitUntil([&] { return (row = app.row(id)) != nullptr; }))
            return false;
        click(row, Qt::LeftButton, pointIn(row, 0.5, 0.5));
        return waitUntil([&] { return app.backend().currentTaskId() == id; });
    }
};

REGISTER_TEST(E2ESmartText)

#include "test_smart_text.moc"
