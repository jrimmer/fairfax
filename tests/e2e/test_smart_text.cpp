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

const SL Everything{"Learn a language one day", "Buy a new bike some day", "Find a new flat", "Call: Ana, mention the new flat",
                    "Call: Sam", "Renew passport", "Read the handbook", "Pack the notebook", "Visit the bookshop",
                    "Review the project", "Pick a subject", "Rejected idea", "Call the plumber", "Pay Stephen",
                    "Thank Alexander", "Ring John", "Quiet title", "Email a@b.com", "Tag text #garden thing",
                    "Buy bread and butter", "Walk or run", "Elsewhere flat offer"};

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

    // Full-text operators, with Soundex off: plain words match inside longer
    // words; implicit AND; OR and AND; -word and !word exclude; word* prefix;
    // *word suffix; *word* contains; "exact phrase"; "*" matches every task.
    void fullTextOperators_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("bare word matches inside words") << "ject" << SL{"Review the project", "Pick a subject", "Rejected idea"};
        QTest::newRow("implicit and") << "new flat" << SL{"Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("excluded with dash") << "new flat -find" << SL{"Call: Ana, mention the new flat"};
        QTest::newRow("excluded with bang") << "new flat !find" << SL{"Call: Ana, mention the new flat"};
        QTest::newRow("bang alone") << "!call" << without(Everything, SL{"Call: Ana, mention the new flat", "Call: Sam", "Call the plumber"});
        QTest::newRow("short words") << "a day" << SL{"Learn a language one day", "Buy a new bike some day"};
        QTest::newRow("or") << "Call OR new"
                            << SL{"Buy a new bike some day", "Find a new flat", "Call: Ana, mention the new flat", "Call: Sam",
                                  "Call the plumber", "Renew passport"};
        QTest::newRow("or in lowercase") << "call or new"
                                         << SL{"Buy a new bike some day", "Find a new flat", "Call: Ana, mention the new flat",
                                               "Call: Sam", "Call the plumber", "Renew passport"};
        QTest::newRow("explicit and") << "new AND flat" << SL{"Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("and in lowercase") << "new and flat" << SL{"Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("prefix wildcard") << "new*" << SL{"Buy a new bike some day", "Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("suffix wildcard") << "*book" << SL{"Read the handbook", "Pack the notebook"};
        QTest::newRow("contains wildcard") << "*ject*" << SL{"Review the project", "Pick a subject", "Rejected idea"};
        QTest::newRow("exact phrase") << "\"new flat\"" << SL{"Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("phrase in order") << "\"a new\"" << SL{"Buy a new bike some day", "Find a new flat"};
        QTest::newRow("phrase out of order") << "\"flat new\"" << SL{};
        QTest::newRow("star matches every task") << "*" << Everything;
        QTest::newRow("quoted and") << "\"and\"" << SL{"Read the handbook", "Thank Alexander", "Buy bread and butter"};
        QTest::newRow("quoted or") << "\"or\"" << SL{"Walk or run", "Renew passport"};
        QTest::newRow("case is ignored") << "NEW FLAT" << SL{"Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("negated phrase") << "new -\"new flat\"" << SL{"Buy a new bike some day", "Renew passport"};
        QTest::newRow("brackets") << "new (flat OR bike)"
                                  << SL{"Buy a new bike some day", "Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("prefix and word") << "new* flat" << SL{"Find a new flat", "Call: Ana, mention the new flat"};
        QTest::newRow("notes are searched") << "plumber" << SL{"Call the plumber", "Quiet title"};
        QTest::newRow("tag names are searched") << "tagged" << SL{"Quiet title"};
        QTest::newRow("exclude within matches") << "call -plumber" << SL{"Call: Ana, mention the new flat", "Call: Sam"};
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

    // Free text matches a task's title, notes and searchable metadata. The
    // words and / or alone are not searched as text (quote them), so a lone
    // operator filters nothing.
    void loneOperatorFiltersNothing() {
        App &app = *m_app;
        SMART_QUERY(app, "or", Everything);
        SMART_QUERY(app, "and", Everything);
        SMART_QUERY(app, "\"and\"", (SL{"Read the handbook", "Thank Alexander", "Buy bread and butter"}));
    }

    // Soundex matches sound-alikes despite misspellings and dropped letters:
    // plumver finds plumber, Steven finds Stephen, alexnder finds Alexander, Jon
    // finds John. Full Text on its own does not.
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
        QTRY_VERIFY_WITH_TIMEOUT((box = findItem("searchSoundex")) != nullptr, 5000);
        click(box);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().soundex(), 5000);
        QCOMPARE(app.store().setting("search.soundex").toBool(), true);
        SMART_SHOWS(app, expected);
        // Reversed: back to Full Text, the misspelling finds nothing again.
        click(findItem("searchSoundex"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().soundex(), 5000);
        QCOMPARE(app.store().setting("search.soundex").toBool(), false);
        SMART_SHOWS(app, SL{});
    }

    // A # or @ in the text, or quotes around the whole query, turn Soundex off
    // for that search so the match stays literal.
    void literalSearchesStayLiteralWithSoundex() {
        App &app = *m_app;
        app.backend().setSoundex(true);
        SMART_QUERY(app, "plumver", (SL{"Call the plumber", "Quiet title"}));
        SMART_QUERY(app, "\"plumver\"", SL{});
        SMART_QUERY(app, "#garden", SL{"Tag text #garden thing"});
        SMART_QUERY(app, "#gardn", SL{});
        SMART_QUERY(app, "a@b.com", SL{"Email a@b.com"});
        SMART_QUERY(app, "a@b.cum", SL{});
        SMART_QUERY(app, "new flat -find", (SL{"Call: Ana, mention the new flat"}));
    }

    // The same switch lives in Search Options ("The Soundex switch").
    void soundexSwitchInSearchOptions() {
        App &app = *m_app;
        SMART_QUERY(app, "Steven", SL{});
        app.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        LIST_ITEM(box, findItem("soundexBox"));
        QTRY_VERIFY_WITH_TIMEOUT(!box->property("checked").toBool(), 5000);
        clickSteady(box);
        QTRY_VERIFY_WITH_TIMEOUT(app.backend().soundex(), 5000);
        SMART_SHOWS(app, SL{"Pay Stephen"});
        clickSteady(findItem("soundexBox"));
        QTRY_VERIFY_WITH_TIMEOUT(!app.backend().soundex(), 5000);
        SMART_SHOWS(app, SL{});
        // (Toggling the switch hands the keyboard back to the list, so close
        // the sheet with its own button.)
        QVERIFY(pressBuilder("searchButton"));
        QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
    }

    // Quick Find (F with the task list focused) moves the selection to the first
    // title match and filters nothing; Search (Ctrl+F) filters the list down to
    // the results.
    void quickFindJumpsSearchFilters() {
        App &app = *m_app;
        // Quick Find: the list keeps every row, only the selection moves.
        QVERIFY(selectRowText(app, "Call: Sam"));
        const qint64 sam = textTask(app, "Call: Sam");
        QCOMPARE(app.backend().currentTaskId(), sam);
        parkPointer(app.window);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 5000);
        LIST_ITEM(input, findItem("commandInput"));
        QTRY_VERIFY_WITH_TIMEOUT(input->property("activeFocus").toBool(), 5000);
        app.type("handbook");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Read the handbook"}), 5000);
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), textTask(app, "Read the handbook"), 5000);
        QCOMPARE(app.backend().search(), QString());
        QCOMPARE(paneTitles(app), sorted(Everything));
        // Search: the list collapses to the match, and keeps it until cleared.
        search(app, "handbook");
        SMART_SHOWS(app, SL{"Read the handbook"});
        QVERIFY(escapeSearch(app));
        SMART_SHOWS(app, Everything);
    }

    // Task Find bar: typing highlights matching titles and selects the first
    // match; up and down step through the others, wrapping at the ends. Return
    // lands on the focused match and closes the bar, Esc closes it in place; either
    // way the selection stays put.
    void taskFindBarKeys() {
        App &app = *m_app;
        QVERIFY(selectRowText(app, "Call: Sam"));
        const qint64 start = app.backend().currentTaskId();
        // The keyboard moves the highlight here, so the pointer is out of the way:
        // the bar highlights whatever it rests on.
        parkPointer(app.window);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 5000);
        app.type("flat");
        QTRY_COMPARE_WITH_TIMEOUT(barItems().size(), 3, 5000);
        QCOMPARE(sorted(barItems()), sorted(SL{"Find a new flat", "Call: Ana, mention the new flat", "Elsewhere flat offer"}));
        QCOMPARE(commandBar()->property("highlighted").toInt(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.backend().currentTaskId()).title, barItems().at(0), 5000);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.backend().currentTaskId()).title, barItems().at(1), 5000);
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 2, 5000);
        // Wrapping: down from the last match goes to the first, up from the
        // first goes to the last.
        app.press(Qt::Key_Down);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 0, 5000);
        app.press(Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.backend().currentTaskId()).title, barItems().at(2), 5000);
        app.press(Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(commandBar()->property("highlighted").toInt(), 1, 5000);
        const QString second = barItems().at(1);
        app.press(Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.task(app.backend().currentTaskId()).title, second, 5000);
        // Esc closes the bar where you are: the selection stays on the match.
        QVERIFY(app.backend().currentTaskId() != start);
        parkPointer(app.window);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 5000);
        app.type("sam");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Call: Sam"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), start, 5000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 5000);
        QCOMPARE(app.backend().currentTaskId(), start);
        QCOMPARE(paneTitles(app), sorted(Everything));
    }

    // Typing in the find bar highlights matching titles and moves the selection
    // to the first match.
    void taskFindMovesSelectionWhileTyping() {
        App &app = *m_app;
        QVERIFY(selectRowText(app, "Call: Sam"));
        parkPointer(app.window);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 5000);
        app.type("handbook");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Read the handbook"}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(app.backend().currentTaskId(), textTask(app, "Read the handbook"), 5000);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 5000);
        QCOMPARE(app.backend().currentTaskId(), textTask(app, "Read the handbook"));
    }

    // The bar's exact-match toggle switches between loose and exact word
    // matching, and each bar remembers its own setting.
    void findBarExactMatchToggle() {
        App &app = *m_app;
        QVERIFY(selectRowText(app, "Call: Sam"));
        parkPointer(app.window);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 5000);
        app.type("ject");
        QTRY_COMPARE_WITH_TIMEOUT(barItems().size(), 3, 5000);
        app.press(Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(commandBar()->property("exact").toBool(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(barItems().size(), 0, 5000);
        QCOMPARE(app.store().setting("bar.exact.find").toBool(), true);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 5000);
        // The bar remembers its own setting: reopened, it is still exact.
        parkPointer(app.window);
        app.press(Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(barOpen(), 5000);
        QVERIFY(commandBar()->property("exact").toBool());
        app.type("project");
        QTRY_COMPARE_WITH_TIMEOUT(barItems(), (SL{"Review the project"}), 5000);
        // Reversed: loose again.
        app.press(Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!commandBar()->property("exact").toBool(), 5000);
        QCOMPARE(app.store().setting("bar.exact.find").toBool(), false);
        app.press(Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!barOpen(), 5000);
    }

private:
    // Clicks the row showing this title, which gives the list focus.
    static bool selectRowText(App &app, const QString &title) {
        const qint64 id = textTask(app, title);
        QQuickItem *row = nullptr;
        if (!waitUntil([&] { return (row = app.row(id)) != nullptr; })) {
            qWarning() << "no row for" << title << id << "in" << app.backend().sourceKey() << paneTitles(app, true);
            return false;
        }
        click(row, Qt::LeftButton, pointIn(row, 0.5, 0.5));
        return waitUntil([&] { return app.backend().currentTaskId() == id; });
    }
};

REGISTER_TEST(E2ESmartText)

#include "test_smart_text.moc"
