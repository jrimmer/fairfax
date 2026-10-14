// End-to-end tests for every setting: each is changed through the Settings
// window the way a user does it, its effect is checked in the main window,
// Quick Entry or the editor, the app is restarted on the same database to
// prove it persisted (as text), and the setting is changed back.

#include <QDirIterator>

#include "settingskit.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

void settingState(const SettingCase &c, QString &out) {
    QQuickItem *item = nullptr;
    control(c.control, item);
    if (!item)
        return;
    switch (c.kind) {
    case SettingCase::Check:
    case SettingCase::Choice: out = isChecked(item) ? "true" : "false"; break;
    case SettingCase::Combo: out = QString::number(item->property("currentIndex").toInt()); break;
    case SettingCase::SliderK:
    case SettingCase::Spin: out = QString::number(item->property("value").toDouble()); break;
    case SettingCase::Text: out = item->property("text").toString(); break;
    }
}

void applyChange(const SettingCase &c, QQuickWindow *sw, bool toChanged) {
    switch (c.kind) {
    case SettingCase::Check: setCheck(c.control, toChanged ? !c.defaultOn : c.defaultOn); break;
    case SettingCase::Combo: chooseCombo(c.control, toChanged ? c.toIndex : c.fromIndex); break;
    case SettingCase::SliderK: setSlider(sw, c.control, toChanged ? c.toValue : c.fromValue); break;
    case SettingCase::Spin: setSpin(sw, c.control, int(toChanged ? c.toValue : c.fromValue)); break;
    case SettingCase::Text: setText(sw, c.control, toChanged ? c.toText : QString()); break;
    case SettingCase::Choice: {
        QQuickItem *button = nullptr;
        control(toChanged ? c.control : c.revert, button);
        click(button);
        break;
    }
    }
}

QString expectedState(const SettingCase &c, bool changed) {
    switch (c.kind) {
    case SettingCase::Check: return (changed ? !c.defaultOn : c.defaultOn) ? "true" : "false";
    case SettingCase::Choice: return changed ? "true" : "false";
    case SettingCase::Combo: return QString::number(changed ? c.toIndex : c.fromIndex);
    case SettingCase::SliderK:
    case SettingCase::Spin: return QString::number(changed ? c.toValue : c.fromValue);
    case SettingCase::Text: return changed ? c.toText : QString();
    }
    return {};
}

QString expectedStored(const SettingCase &c) {
    if (!c.stored.isEmpty())
        return c.stored;
    return c.defaultOn ? "false" : "true";
}

}  // namespace

class E2ESettings : public QObject {
    Q_OBJECT

    // Runs one row of the table end to end on a started session whose database
    // is in its seeded state; leaves the session running, with the setting
    // back at its default.
    void runRow(Session &s, const SettingCase &c) {
        auto effect = [&](bool changed) {
            if (c.effect)
                c.effect(s, changed);
        };

        // The default, and no effect yet.
        effect(false);
        if (QTest::currentTestFailed())
            return;

        // Change it in Settings; the effect appears.
        QQuickWindow *sw = nullptr;
        openSettings(*s.app, sw);
        QVERIFY(sw);
        selectPane(sw, c.pane);
        QString state;
        settingState(c, state);
        QCOMPARE(state, expectedState(c, false));
        applyChange(c, sw, true);
        if (QTest::currentTestFailed())
            return;
        if (!c.key.isEmpty() && c.stored != "*")
            QCOMPARE(s.app->store().setting(c.key).toString(), expectedStored(c));
        effect(true);
        if (QTest::currentTestFailed())
            return;

        // Quit and relaunch on the same database: still changed, in the
        // control and in the effect. Stored values come back as text, and
        // "false" must still read as off.
        QVERIFY(s.restart());
        if (!c.key.isEmpty())
            QCOMPARE(s.app->store().setting(c.key).typeId(), int(QMetaType::QString));
        effect(true);
        if (QTest::currentTestFailed())
            return;
        openSettings(*s.app, sw);
        QVERIFY(sw);
        selectPane(sw, c.pane);
        settingState(c, state);
        QCOMPARE(state, expectedState(c, true));

        // Change it back; the control agrees, and the effect is gone.
        applyChange(c, sw, false);
        if (QTest::currentTestFailed())
            return;
        settingState(c, state);
        QCOMPARE(state, expectedState(c, false));
        effect(false);
    }

    // Rows that need no extra data share one running session, one relaunch per
    // row, so the whole table stays quick; rows with their own data get a
    // session each. Only one app is ever alive at a time.
    std::unique_ptr<Session> m_chain;
    bool m_chainDirty = false;

private slots:
    // The guard that keeps this suite honest: every settings key the Settings
    // window binds a control to, and every key the rest of the QML and the
    // C++ read or write, has a test (a row of the table, a dedicated test) or
    // is named as bookkeeping. A setting added without one fails here.
    void everySettingHasATest() {
        QSet<QString> table;
        QSet<QString> names;
        for (const SettingCase &c : settingCases()) {
            if (!c.key.isEmpty())
                table.insert(c.key);
            QVERIFY2(!names.contains(c.name()), qPrintable("two table rows are called " + c.name()));
            names.insert(c.name());
        }
        auto covered = [&](const QString &key) {
            if (table.contains(key) || dedicatedKeys().contains(key) || internalKeys().contains(key) || untestedKeys().contains(key))
                return true;
            for (const auto *map : {&dedicatedKeys(), &internalKeys(), &untestedKeys()})
                for (auto it = map->begin(); it != map->end(); ++it)
                    if (it.key().endsWith('.') && key.startsWith(it.key()))
                        return true;
            return false;
        };
        const QRegularExpression literal(R"re(\b(?:setSetting|settingFlag|setting|value|bool)\(\s*"([A-Za-z][A-Za-z0-9_.]*)")re");
        const QRegularExpression toggle(R"re(\bkey:\s*"([A-Za-z][A-Za-z0-9_.]*)")re");
        const QRegularExpression named(R"re(objectName:\s*"setting:([A-Za-z0-9_.:]+)")re");

        // The Settings window itself.
        QFile window(":/qml/SettingsWindow.qml");
        QVERIFY(window.open(QIODevice::ReadOnly));
        const QString qml = QString::fromUtf8(window.readAll());
        QSet<QString> bound, controls;
        for (auto it = literal.globalMatch(qml); it.hasNext();)
            bound.insert(it.next().captured(1));
        for (auto it = toggle.globalMatch(qml); it.hasNext();) {
            const QString key = it.next().captured(1);
            bound.insert(key);
            controls.insert("setting:" + key);
        }
        for (auto it = named.globalMatch(qml); it.hasNext();)
            controls.insert("setting:" + it.next().captured(1));
        controls.insert("setting:appearance.scheme:dark");
        controls.insert("setting:appearance.scheme:light");
        controls.insert("setting:appearance.scheme:system");
        bound.insert("inbox");   // The Inbox box has a control but no literal key.
        QVERIFY2(bound.size() > 60, qPrintable(QString("only %1 keys found in SettingsWindow.qml").arg(bound.size())));
        QStringList missing;
        for (const QString &key : bound)
            if (!covered(key))
                missing << key;
        missing.sort();
        QVERIFY2(missing.isEmpty(), qPrintable("Settings window keys with no test: " + missing.join(", ")));

        // Every control the table drives is really in the window.
        QStringList ghosts;
        for (const SettingCase &c : settingCases())
            if (!controls.contains(c.control))
                ghosts << c.control;
        QVERIFY2(ghosts.isEmpty(), qPrintable("table controls missing from SettingsWindow.qml: " + ghosts.join(", ")));

        // Every other QML file.
        QStringList elsewhere;
        QDirIterator files(":/qml", {"*.qml"}, QDir::Files);
        while (files.hasNext()) {
            QFile f(files.next());
            if (!f.open(QIODevice::ReadOnly) || f.fileName().endsWith("SettingsWindow.qml"))
                continue;
            const QString text = QString::fromUtf8(f.readAll());
            for (auto it = literal.globalMatch(text); it.hasNext();) {
                const QString key = it.next().captured(1);
                if (key.contains('.') && !covered(key))
                    elsewhere << f.fileName() + ": " + key;
            }
        }
        QVERIFY2(elsewhere.isEmpty(), qPrintable("keys used in QML with no test: " + elsewhere.join(", ")));

        // The C++ (when the source tree is there to read).
#ifdef SRCDIR
        QStringList code;
        QDirIterator sources(SRCDIR, {"*.cpp", "*.h"}, QDir::Files);
        const QRegularExpression cpp(R"re(\b(?:setSetting|settingFlag|setting)\(\s*"([A-Za-z][A-Za-z0-9_.]*)")re");
        while (sources.hasNext()) {
            QFile f(sources.next());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QString text = QString::fromUtf8(f.readAll());
            for (auto it = cpp.globalMatch(text); it.hasNext();) {
                const QString key = it.next().captured(1);
                if (key.contains('.') && !covered(key))
                    code << QFileInfo(f.fileName()).fileName() + ": " + key;
            }
        }
        QVERIFY2(code.isEmpty(), qPrintable("keys used in C++ with no test: " + code.join(", ")));
#endif

        // Every dedicated test named above exists.
        QStringList needed;
        for (const QString &how : dedicatedKeys().values())
            needed << how;
        needed.removeDuplicates();
        QStringList absent = needed;
        for (const auto &factory : testRegistry()) {
            std::unique_ptr<QObject> test(factory());
            for (const QString &slot : needed)
                if (slot.section("::", 0, 0) == test->metaObject()->className()
                    && test->metaObject()->indexOfSlot(qPrintable(slot.section("::", 1) + "()")) >= 0)
                    absent.removeAll(slot);
        }
        QVERIFY2(absent.isEmpty(), qPrintable("dedicated tests that do not exist: " + absent.join(", ")));
    }

    void everySetting_data() {
        QTest::addColumn<int>("index");
        const auto &cases = settingCases();
        // FAIRFAX_E2E_ONLY=<text> runs just the rows whose name contains it.
        const QString only = qEnvironmentVariable("FAIRFAX_E2E_ONLY");
        for (int pass = 0; pass < 2; ++pass)   // Rows without their own data first.
            for (int i = 0; i < cases.size(); ++i)
                if (cases[i].slot.isEmpty() && bool(cases[i].prepare) == (pass == 1) && (only.isEmpty() || cases[i].name().contains(only)))
                    QTest::newRow(qPrintable(cases[i].name())) << i;
    }
    void everySetting() {
        QFETCH(int, index);
        const SettingCase &c = settingCases().at(index);
        if (c.prepare) {
            m_chain.reset();
            Session s;
            s.seed = [&c](Store &st) {
                Seed ids = seedWorld(st);
                c.prepare(st, ids);
                return ids;
            };
            QVERIFY(s.start());
            runRow(s, c);
            return;
        }
        if (!m_chain || m_chainDirty) {
            m_chain = std::make_unique<Session>();
            m_chainDirty = false;
            QVERIFY(m_chain->start());
        }
        runRow(*m_chain, c);
        if (QTest::currentTestFailed())
            m_chainDirty = true;
        else
            m_chain->dropProbeData();
    }

    void cleanupTestCase() { m_chain.reset(); }

    // The badge colour swatch opens the app's HSV picker; the chosen shade is
    // stored as #RRGGBB and paints the tray badge.
    void badgeColourIsPicked() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "General");
        QCOMPARE(TrayMenu::badgeColour(app->store()), QColor(0xE5, 0x48, 0x4D));
        QQuickItem *swatch = nullptr;
        control("setting:tray.badgeColor", swatch);
        click(swatch);
        QQuickItem *hex = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((hex = findItem("colorHex")) != nullptr, 5000);
        click(hex);
        QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
        typeInto(sw, "#2299CC");
        QVERIFY(clickDialogButtonIn("Custom Color", "OK"));
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("colorHex"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(TrayMenu::badgeColour(app->store()), QColor(0x22, 0x99, 0xCC), 5000);
        QCOMPARE(app->store().setting("tray.badgeColor").toString(), QString("#2299CC"));

        // Reset puts the red back.
        QQuickItem *reset = nullptr;
        control("setting:tray.badgeColorReset", reset);
        click(reset);
        QTRY_COMPARE_WITH_TIMEOUT(TrayMenu::badgeColour(app->store()), QColor(0xE5, 0x48, 0x4D), 5000);
    }

    // The Tray menu group is enabled only while the tray icon is on.
    void trayGroupFollowsTheToggle() {
        Session s;
        QVERIFY(s.start());
        App *app = s.app.get();
        QQuickWindow *sw = nullptr;
        openSettings(*app, sw);
        selectPane(sw, "General");
        QQuickItem *source = nullptr;
        control("setting:tray.source", source);
        QVERIFY(source->isEnabled());
        setCheck("setting:general.trayIcon", false);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem("setting:tray.source")->isEnabled(), 5000);
        setCheck("setting:general.trayIcon", true);
        QTRY_VERIFY_WITH_TIMEOUT(findItem("setting:tray.source")->isEnabled(), 5000);
    }
};

REGISTER_TEST(E2ESettings)

#include "test_settings.moc"
