// End-to-end scenarios for the rule builder in the Search Options sheet
// (smart-lists.md, "The Rule Builder"): a row per clause (Property, is / is
// not, value), "all" or "any" of the following at the top, [+] to add a
// clause, [-] to remove one, and Option-click on [+] to add a sub-filter. In
// Fairfax Option is Alt, and a right-click on [+] offers both choices.
//
// The rows write the `type:` section of the search language; each test
// asserts the rows on the sheet, the query they write, and the tasks that
// query finds.

#include "smartkit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

using SL = QStringList;

QString previewText() {
    QQuickItem *p = findItem("preview");
    return p ? p->property("text").toString() : QString();
}

}  // namespace

class E2ESmartRules : public QObject {
    Q_OBJECT

    std::unique_ptr<App> m_app;

    // Opens the sheet on All, from a clean slate.
    void openFresh() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        QVERIFY(escapeSearch(app));
        QVERIFY(openBuilder(app));
        QCOMPARE(builderQuery(), QString());
    }

    // Clicks "Search" and checks what the pane lists.
    void applyAndExpect(const QString &query, const QStringList &expected) {
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), query, 5000);
        QVERIFY(pressBuilder("searchButton"));
        QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
        QCOMPARE(m_app->backend().search(), query);
        SMART_SHOWS(*m_app, expected);
    }

    // Opens Edit Search… on a Smart List.
    void editSearch(qint64 id) {
        QVERIFY(listMenu(*m_app, id, "Edit Search…"));
        QTRY_VERIFY_WITH_TIMEOUT(builderOpen(), 5000);
    }

private slots:
    void init() {
        m_app = std::make_unique<App>(seedSmart);
        QVERIFY(m_app->ok);
    }
    void cleanup() { m_app.reset(); }

    // "Click [+] to add a clause… Choose Any or All at the top": Combine Today
    // and Overdue with the rows. Each [+] press makes a row, set with the
    // drop-downs; Any joins them with OR.
    void combineTodayAndOverdueWithAny() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(addRuleRow("Due Date", "is", "Today") == "0");
        QVERIFY(pressBuilderForChange("ruleAdd:0"));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1"), 5000);
        QVERIFY(setRuleRow("1", "Due Date", "is", "Overdue"));
        QCOMPARE(ruleText("0"), QString("Due Date / is / Today"));
        QCOMPARE(ruleText("1"), QString("Due Date / is / Overdue"));
        // All is the default, and nothing is both due today and overdue.
        QCOMPARE(builderRules().value("match").toString(), QString("all"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: duetoday AND overdue"), 5000);
        QVERIFY(setMatch("", true));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: duetoday OR overdue"), 5000);
        QCOMPARE(previewText(), QString("type: duetoday OR overdue"));
        // Two-day windows that cover today count as due today, as in the older recipes.
        const SL expected{"Milk today", "Dentist today", "Two day window", "Fresh window", "Daily repeat", "Repeating window",
                          "Rent overdue", "Invoice overdue"};
        applyAndExpect("type: duetoday OR overdue", expected);
    }

    // The same rows for is / is not on several properties: the visible row, the
    // query, and the tasks.
    void isAndIsNotOnProperties_data() {
        QTest::addColumn<QString>("prop");
        QTest::addColumn<QString>("op");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("shown");
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        auto row = [](const char *name, const char *prop, const char *op, const char *value, const char *query,
                      std::function<bool(const Spec &)> pred) {
            const SL expected = specTitles([&](const Spec &s) { return shownInAll(s) && pred(s); });
            QTest::newRow(name) << prop << op << value << (QString(prop) + " / " + op + (*value ? QString(" / ") + value : QString()))
                                << query << expected;
        };
        row("priority is high", "Priority", "is", "High", "type: hiprio", [](const Spec &s) { return s.prio == 3; });
        row("priority is not low", "Priority", "is not", "Low", "type: !lowprio", [](const Spec &s) { return s.prio != 1; });
        row("starred is", "Starred", "is", "", "type: starred", [](const Spec &s) { return s.star; });
        row("starred is not", "Starred", "is not", "", "type: !starred", [](const Spec &s) { return !s.star; });
        row("due is no date", "Due Date", "is", "No Date", "type: nodue", [](const Spec &s) { return s.due == NoDay; });
        row("due is not any date", "Due Date", "is not", "Any Date", "type: !due", [](const Spec &s) { return s.due == NoDay; });
        row("start is today", "Start Date", "is", "Today", "type: starttoday", [](const Spec &s) { return s.start == 0; });
        row("tag is set", "Tag", "is set", "", "type: anytag", [](const Spec &s) { return !s.tags.isEmpty(); });
        row("tag is not set", "Tag", "is not set", "", "type: notag", [](const Spec &s) { return s.tags.isEmpty(); });
        row("location is set", "Location", "is set", "", "type: anylocation", [](const Spec &s) { return !s.locs.isEmpty(); });
        row("location is not set", "Location", "is not set", "", "type: nolocation", [](const Spec &s) { return s.locs.isEmpty(); });
        row("note has", "Note", "has", "", "type: note", [](const Spec &s) { return !s.notes.isEmpty(); });
        row("note doesn't have", "Note", "doesn't have", "", "type: !note", [](const Spec &s) { return s.notes.isEmpty(); });
        row("repeating is", "Repeating", "is", "", "type: repeat", [](const Spec &s) { return s.repeat != 0; });
        row("duration has", "Duration", "has", "", "type: duration", [](const Spec &s) { return s.duration > 0; });
        row("alarm has", "Alarm", "has", "", "type: alarms", [](const Spec &s) { return s.alarm; });
        row("picture attached", "Picture", "attached", "", "type: picture", [](const Spec &s) { return s.attach == Attachment::Image; });
        row("audio not attached", "Audio", "not attached", "", "type: !audio", [](const Spec &s) { return s.attach != Attachment::Audio; });
        row("action is call", "Action", "is", "Call", "type: call", [](const Spec &s) { return s.action == ActionType::Call; });
        row("action is not any action", "Action", "is not", "Any Action", "type: !action", [](const Spec &s) { return s.action == 0; });
        row("task is project", "Task", "is", "Project", "type: project", [](const Spec &s) { return s.type == TaskType::Project; });
    }
    void isAndIsNotOnProperties() {
        QFETCH(QString, prop);
        QFETCH(QString, op);
        QFETCH(QString, value);
        QFETCH(QString, shown);
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY2(addRuleRow(prop, op, value) == "0", qPrintable(shown));
        QCOMPARE(ruleText("0"), shown);
        QCOMPARE(ruleRowCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), query, 5000);
        applyAndExpect(query, expected);
        if (QTest::currentTestFailed())
            return;
        // Opened again, the sheet reads the same row back out of the search.
        QVERIFY(openBuilder(*m_app));
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), shown, 5000);
        QCOMPARE(ruleRowCount(), 1);
        QCOMPARE(builderQuery(), query);
        QVERIFY(closeBuilder(*m_app));
    }

    // Choosing another property starts it at that property's first value and
    // keeps the sign of the operator: "is not" stays "is not".
    void changingThePropertyResetsTheValue() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(addRuleRow("Priority", "is not", "High") == "0");
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: !hiprio"), 5000);
        QVERIFY(chooseInBuilder("ruleProp:0", "Due Date"));
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Due Date / is not / Any Date"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: !due"), 5000);
        QVERIFY(chooseInBuilder("ruleValue:0", "Tomorrow"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: !duetomorrow"), 5000);
        // A property with one value has no value box; the sign still holds.
        QVERIFY(chooseInBuilder("ruleProp:0", "Starred"));
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Starred / is not"), 5000);
        QVERIFY(findItem("ruleValue:0") == nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: !starred"), 5000);
        // And back to a property with several values: the value box returns at its first.
        QVERIFY(chooseInBuilder("ruleProp:0", "Priority"));
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Priority / is not / Any"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: !prio"), 5000);
        // Flipping the operator back leaves the value alone.
        QVERIFY(chooseInBuilder("ruleOp:0", "is"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: prio"), 5000);
        QCOMPARE(ruleText("0"), QString("Priority / is / Any"));
    }

    // "[-] removes a clause": the rows below move up, the query follows, and
    // with no rows left only Add Rule remains.
    void removingClauses() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(addRuleRow("Priority", "is", "High") == "0");
        QVERIFY(addRuleRow("Starred", "is") == "1");
        QVERIFY(addRuleRow("Note", "has") == "2");
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio AND starred AND note"), 5000);
        QVERIFY(pressBuilderForChange("ruleRemove:1"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio AND note"), 5000);
        QCOMPARE(ruleRowCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("1"), QString("Note / has"), 5000);
        QVERIFY(!ruleRowShown("2"));
        QVERIFY(pressBuilderForChange("ruleRemove:0"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: note"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Note / has"), 5000);
        QVERIFY(pressBuilderForChange("ruleRemove:0"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(ruleRowCount(), 0, 5000);
        QVERIFY(findItem("ruleAdd") != nullptr);
        QVERIFY(findItem("ruleMatchAll:") == nullptr);
        // The rows come back when one is added again.
        QVERIFY(addRuleRow("Starred", "is") == "0");
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred"), 5000);
        QVERIFY(findItem("ruleMatchAll:") != nullptr);
    }

    // "Hold the Option key and click [+] to create a sub-filter": the manual's
    // "require high priority as well" as a nested group, "type: (duetoday OR
    // overdue) AND hiprio". The sub-filter has its own Match All / Any and rows;
    // its [-] takes the whole group out again.
    void subFilterByAltClick() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(addRuleRow("Priority", "is", "High") == "0");
        // A plain click adds a row; Alt+click a sub-filter that starts with one row.
        QVERIFY(pressBuilderForChange("ruleAdd:0", Qt::AltModifier));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1.0"), 5000);
        QVERIFY(findItem("ruleGroupRemove:1") != nullptr);
        QVERIFY(findItem("ruleMatchAll:1") && findItem("ruleMatchAny:1"));
        QVERIFY(!ruleRowShown("1"));
        QVERIFY(setRuleRow("1.0", "Due Date", "is", "Today"));
        QVERIFY(pressBuilderForChange("ruleAdd:1.0"));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1.1"), 5000);
        QVERIFY(setRuleRow("1.1", "Due Date", "is", "Overdue"));
        QVERIFY(setMatch("1", true));
        QCOMPARE(builderRules().value("match").toString(), QString("all"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio AND (duetoday OR overdue)"), 5000);
        QCOMPARE(ruleRowCount(), 3);
        applyAndExpect("type: hiprio AND (duetoday OR overdue)", SL{"Rent overdue"});
        if (QTest::currentTestFailed())
            return;
        // Read back out of the search, nested.
        QVERIFY(openBuilder(*m_app));
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Priority / is / High"), 5000);
        QCOMPARE(ruleText("1.0"), QString("Due Date / is / Today"));
        QCOMPARE(ruleText("1.1"), QString("Due Date / is / Overdue"));
        QVERIFY(findItem("ruleGroupRemove:1") != nullptr);
        // The sub-filter's [-] removes the whole group.
        QVERIFY(pressBuilderForChange("ruleGroupRemove:1"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: hiprio"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!ruleRowShown("1.0") && !ruleRowShown("1.1"), 5000);
        QVERIFY(findItem("ruleGroupRemove:1") == nullptr);
        QCOMPARE(ruleRowCount(), 1);
        applyAndExpect("type: hiprio", specTitles([](const Spec &s) { return shownInAll(s) && s.prio == 3; }));
    }

    // Right-click on [+] opens "Add Rule" / "Add Sub-filter", for those who do
    // not know the Alt-click.
    void subFilterByPlusMenu() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(addRuleRow("Starred", "is") == "0");
        auto plusMenu = [&](const QString &entry) {
            const QString before = QString::fromUtf8(QJsonDocument::fromVariant(builderRules()).toJson(QJsonDocument::Compact));
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (!pressBuilder("ruleAdd:0", Qt::RightButton) || !clickMenuEntry(entry))
                    return false;
                if (waitUntil([&] {
                        return QString::fromUtf8(QJsonDocument::fromVariant(builderRules()).toJson(QJsonDocument::Compact)) != before;
                    }, 2000))
                    return true;
                closeMenus(*m_app);
            }
            return false;
        };
        QVERIFY(findItem("ruleAddMenuRule") == nullptr);
        QVERIFY(plusMenu("Add Sub-filter"));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1.0"), 5000);
        QVERIFY(setRuleRow("1.0", "Note", "has"));
        QVERIFY(setMatch("", true));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred OR note"), 5000);
        // "Add Rule" from the menu adds a plain row below the one it was opened on.
        QVERIFY(plusMenu("Add Rule"));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1"), 5000);
        QCOMPARE(ruleRowCount(), 3);
        QVERIFY(setRuleRow("1", "Duration", "has"));
        // The sub-filter moved down a place, rows and all.
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("2.0"), QString("Note / has"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred OR duration OR note"), 5000);
        applyAndExpect("type: starred OR duration OR note",
                       specTitles([](const Spec &s) { return shownInAll(s) && (s.star || s.duration > 0 || !s.notes.isEmpty()); }));
        if (QTest::currentTestFailed())
            return;
        // A sub-filter of one row reads back as a plain row: the query has no brackets to tell them apart.
        QVERIFY(openBuilder(*m_app));
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("2"), QString("Note / has"), 5000);
        QVERIFY(findItem("ruleGroupRemove:2") == nullptr);
        QVERIFY(pressBuilderForChange("ruleRemove:2"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred OR duration"), 5000);
        QVERIFY(closeBuilder(*m_app));
    }

    // Sub-filters nest: a group inside a group inside the sheet, each with its
    // own Match. Removing the last row of a sub-filter takes the empty group
    // with it.
    void subFiltersNestThreeDeep() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(addRuleRow("Starred", "is") == "0");
        QVERIFY(pressBuilderForChange("ruleAdd:0", Qt::AltModifier));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1.0"), 5000);
        QVERIFY(setRuleRow("1.0", "Note", "has"));
        QVERIFY(pressBuilderForChange("ruleAdd:1.0", Qt::AltModifier));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1.1.0"), 5000);
        QVERIFY(setRuleRow("1.1.0", "Duration", "has"));
        QVERIFY(pressBuilderForChange("ruleAdd:1.1.0"));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("1.1.1"), 5000);
        QVERIFY(setRuleRow("1.1.1", "Repeating", "is"));
        QVERIFY(setMatch("1", true));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred AND (note OR (duration AND repeat))"), 5000);
        QCOMPARE(ruleRowCount(), 4);
        // Take the inner group's rows out one by one.
        QVERIFY(pressBuilderForChange("ruleRemove:1.1.1"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred AND (note OR duration)"), 5000);
        QVERIFY(findItem("ruleGroupRemove:1.1") != nullptr);
        QVERIFY(pressBuilderForChange("ruleRemove:1.1.0"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred AND note"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!ruleRowShown("1.1.0"), 5000);
        // The emptied inner group is gone, header and all, not left as a bare "Match" row.
        QTRY_VERIFY_WITH_TIMEOUT(findItem("ruleGroupRemove:1.1") == nullptr && findItem("ruleMatchAll:1.1") == nullptr, 5000);
        QCOMPARE(ruleRowCount(), 2);
        applyAndExpect("type: starred AND note", specTitles([](const Spec &s) { return shownInAll(s) && s.star && !s.notes.isEmpty(); }));
    }

    // "Edit Search…" reads an existing Smart List's rule back into rows, nested
    // groups included, and the other options stay where they were.
    void editSearchLoadsNestedRulesIntoRows() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const QString query = "type: starred AND (duetoday OR overdue) AND !lowprio";
        const qint64 id = saveSearchAsSmart(app, query);
        QVERIFY(id);
        editSearch(id);
        if (QTest::currentTestFailed())
            return;
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Starred / is"), 5000);
        QCOMPARE(ruleText("1.0"), QString("Due Date / is / Today"));
        QCOMPARE(ruleText("1.1"), QString("Due Date / is / Overdue"));
        QCOMPARE(ruleText("2"), QString("Priority / is not / Low"));
        QCOMPARE(ruleRowCount(), 4);
        QCOMPARE(builderRules().value("match").toString(), QString("all"));
        QVERIFY(findItem("ruleMatchAny:1") != nullptr);
        QCOMPARE(builderQuery(), query);
        QCOMPARE(freeInputText(), QString());
        // A rule that is a plain OR list loads as Any.
        QVERIFY(closeBuilder(app));
        QVERIFY(pane(app, "all"));
        const qint64 any = saveSearchAsSmart(app, "type: hiprio, medprio");
        QVERIFY(any);
        editSearch(any);
        if (QTest::currentTestFailed())
            return;
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("1"), QString("Priority / is / Medium"), 5000);
        QCOMPARE(builderRules().value("match").toString(), QString("any"));
        QCOMPARE(builderQuery(), QString("type: hiprio OR medprio"));
        QVERIFY(closeBuilder(app));
    }

    // A rule the rows cannot say (alltasks, a negated bracket, an unknown
    // name) is kept as text rather than lost, and rows added beside it write
    // their own section.
    void unrepresentableRuleStaysAsText_data() {
        QTest::addColumn<QString>("query");
        QTest::newRow("alltasks") << "type: alltasks";
        QTest::newRow("capturedmail") << "type: capturedmail";
        QTest::newRow("negated bracket") << "type: !(starred OR note)";
        QTest::newRow("unknown name") << "type: starred AND nonsense";
    }
    void unrepresentableRuleStaysAsText() {
        App &app = *m_app;
        QFETCH(QString, query);
        QVERIFY(pane(app, "all"));
        QVERIFY(typeSearch(app, query));
        QVERIFY(openBuilder(app));
        QCOMPARE(ruleRowCount(), 0);
        QCOMPARE(freeInputText(), query);
        QCOMPARE(builderQuery(), query);
        // Nothing is lost on the way out, either.
        QVERIFY(pressBuilder("searchButton"));
        QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
        QCOMPARE(app.backend().search(), query);
        // A row beside it writes a type: section of its own.
        QVERIFY(openBuilder(app));
        QVERIFY(addRuleRow("Starred", "is") == "0");
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), query + " type: starred", 5000);
        QVERIFY(closeBuilder(app));
    }

    // The QUERY box can be typed into and the rows follow, nested ones too;
    // editing the rows rewrites the box.
    void typingTheQueryBuildsRows() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        LIST_ITEM(box, findItem("preview"));
        QVERIFY(focusQueryBox(box));
        m_app->type("type: starred AND (note OR duration)");
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Starred / is"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("1.0"), QString("Note / has"), 5000);
        QCOMPARE(ruleText("1.1"), QString("Duration / has"));
        QVERIFY(findItem("ruleMatchAny:1") != nullptr);
        QCOMPARE(builderRules().value("match").toString(), QString("all"));
        QCOMPARE(previewText(), QString("type: starred AND (note OR duration)"));
        // A row edit rewrites the box.
        QVERIFY(chooseInBuilder("ruleOp:0", "is not"));
        QTRY_COMPARE_WITH_TIMEOUT(previewText(), QString("type: !starred AND (note OR duration)"), 5000);
        // Taking the text away takes the rows away.
        click(box);
        m_app->press(Qt::Key_A, Qt::ControlModifier);
        m_app->press(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(ruleRowCount(), 0, 5000);
        QVERIFY(closeBuilder(*m_app));
    }

    // "Update Smart List" writes the changed rows back to the list, and the
    // list shows the new tasks straight away.
    void updateWritesTheChangedRowsBack() {
        App &app = *m_app;
        QVERIFY(pane(app, "all"));
        const qint64 id = saveSearchAsSmart(app, "type: starred");
        QVERIFY(id);
        const SL starred = specTitles([](const Spec &s) { return shownInAll(s) && s.star; });
        SMART_SHOWS(app, starred);
        editSearch(id);
        if (QTest::currentTestFailed())
            return;
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("0"), QString("Starred / is"), 5000);
        // Starred and not high priority, and a sub-filter of two dates.
        QVERIFY(addRuleRow("Priority", "is not", "High") == "1");
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred AND !hiprio"), 5000);
        QVERIFY(pressBuilderForChange("ruleAdd:1", Qt::AltModifier));
        QTRY_VERIFY_WITH_TIMEOUT(ruleRowShown("2.0"), 5000);
        QVERIFY(setRuleRow("2.0", "Note", "doesn't have"));
        QVERIFY(pressBuilder("updateButton"));
        QTRY_VERIFY_WITH_TIMEOUT(!builderOpen(), 5000);
        const QString saved = "type: starred AND !hiprio AND !note";
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).smartQuery, saved, 5000);
        QCOMPARE(smartLists(app).size(), 1);
        const SL expected = specTitles([](const Spec &s) { return shownInAll(s) && s.star && s.prio != 3 && s.notes.isEmpty(); });
        QVERIFY(expected.size() < starred.size());
        SMART_SHOWS(app, expected);
        QTRY_COMPARE_WITH_TIMEOUT(badgeOf(app, lk(id)).undone, expected.size(), 5000);
        // Reversed: take the new rows out again and update.
        editSearch(id);
        if (QTest::currentTestFailed())
            return;
        QTRY_COMPARE_WITH_TIMEOUT(ruleText("2"), QString("Note / doesn't have"), 5000);
        QVERIFY(pressBuilderForChange("ruleRemove:2"));
        QVERIFY(pressBuilderForChange("ruleRemove:1"));
        QTRY_COMPARE_WITH_TIMEOUT(builderQuery(), QString("type: starred"), 5000);
        QVERIFY(pressBuilder("updateButton"));
        QTRY_COMPARE_WITH_TIMEOUT(app.store().list(id).smartQuery, QString("type: starred"), 5000);
        SMART_SHOWS(app, starred);
    }

    // The tooltip on [+] names the Alt shortcut, so it can be found without the manual.
    void plusButtonSaysAboutAltClick() {
        openFresh();
        if (QTest::currentTestFailed())
            return;
        QVERIFY(addRuleRow("Starred", "is") == "0");
        QQuickItem *plus = findItem("ruleAdd:0");
        QVERIFY(plus);
        scrollAll(plus);
        QTest::mouseMove(plus->window(), centreOf(plus) + QPoint(2, 0));
        QTest::mouseMove(plus->window(), centreOf(plus));
        QTRY_VERIFY_WITH_TIMEOUT(findIn(plus->window()->contentItem(), [](QQuickItem *i) {
            return i->width() > 0 && i->property("text").toString().contains("Alt+click");
        }) != nullptr, 5000);
    }

private:
    static QString freeInputText() {
        QQuickItem *f = findItem("freeInput");
        return f ? f->property("text").toString() : QString();
    }
};

REGISTER_TEST(E2ESmartRules)

#include "test_smart_rules.moc"
