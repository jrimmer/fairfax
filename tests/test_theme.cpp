#include <QTemporaryDir>
#include <QtTest>

#include "theme.h"
#include "testregistry.h"

class ThemeTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;  // No Omarchy theme here, so nothing depends on the machine's.

private slots:
    void settingTheSchemeForcesIt() {
        Theme theme(m_dir.path());
        theme.setScheme("light");
        QCOMPARE(theme.scheme(), QString("light"));
        QVERIFY(!theme.dark());
        theme.setScheme("dark");
        QVERIFY(theme.dark());
        theme.setScheme("anything else");
        QCOMPARE(theme.scheme(), QString("system"));
    }

    // --scheme pins the scheme for the run: the Appearance setting, applied
    // after it at startup and again on every settings change, used to undo it.
    void anOverriddenSchemeIgnoresTheSetting() {
        Theme theme(m_dir.path());
        theme.overrideScheme("light");
        theme.setScheme("system");
        theme.setScheme("dark");
        QCOMPARE(theme.scheme(), QString("light"));
        QVERIFY(!theme.dark());
    }
};

REGISTER_TEST(ThemeTest)
#include "test_theme.moc"
