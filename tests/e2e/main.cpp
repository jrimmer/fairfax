#include <QApplication>
#include <QQuickStyle>
#include <QtTest>
#include <algorithm>
#include <memory>

#include "../../src/keyring.h"
#include "../testregistry.h"

QList<TestFactory> &testRegistry() {
    static QList<TestFactory> registry;
    return registry;
}

namespace {

// Roughly how long each of the longer classes runs (seconds). Used only for the
// order: when several processes share the work, the longest classes start first
// so none is left running alone at the end.
int weight(const QString &name) {
    static const QHash<QString, int> seconds{
        {"E2ESmartBuilder", 80}, {"E2ESettings", 60}, {"E2ESmartRecipes", 55}, {"E2EMiscKeys", 45}, {"E2EMiscSort", 40},
        {"E2ESmartKeywords", 38}, {"E2ESmartPresets", 35}, {"E2ELists", 35}, {"E2ESmartBar", 30}, {"E2ESmartText", 28},
        {"E2ETasksQuick", 25}, {"E2EMiscTags", 25}, {"E2ESettingsLists", 24}, {"E2ESettingsSystem", 24}, {"E2ESmartNav", 24},
        {"E2ESmartLive", 20}, {"E2ETasksLifecycle", 18}, {"E2ETasksEditor", 18}, {"E2EMiscWindow", 17}, {"E2EListGroups", 16}};
    return seconds.value(name, 5);
}

}  // namespace

int main(int argc, char *argv[]) {
    Keyring::useMemoryStore(true);   // No test reads or writes the real keyring.
    // No session bus: the file chooser portal would otherwise put a real dialog on the
    // desktop of whoever runs the tests; with none, FilePicker reports that it failed.
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/precrime-e2e");
    QQuickStyle::setStyle("Material");
    QApplication app(argc, argv);

    std::vector<std::unique_ptr<QObject>> tests;
    for (const auto &factory : testRegistry())
        tests.emplace_back(factory());
    // PRECRIME_E2E_CLAIMDIR=dir: several processes (bin/e2e starts them) share one
    // directory, and a class belongs to whichever process makes its folder first.
    const QString claims = qEnvironmentVariable("PRECRIME_E2E_CLAIMDIR");
    if (!claims.isEmpty())
        std::stable_sort(tests.begin(), tests.end(), [](const auto &a, const auto &b) {
            return weight(a->metaObject()->className()) > weight(b->metaObject()->className());
        });

    int failures = 0;
    for (const auto &test : tests) {
        const QString name = test->metaObject()->className();
        // PRECRIME_E2E_CLASS=Tasks runs only the test classes whose name contains it,
        // so a function name given as an argument is not looked up in the others.
        const QByteArray only = qgetenv("PRECRIME_E2E_CLASS");
        if (!only.isEmpty() && !name.toUtf8().contains(only))
            continue;
        if (!claims.isEmpty() && !QDir(claims).mkdir(name))
            continue;
        QElapsedTimer clock;
        clock.start();
        // PRECRIME_E2E_XMLDIR=dir also writes dir/<class>.xml, which has the time of every test.
        const QByteArray xmlDir = qgetenv("PRECRIME_E2E_XMLDIR");
        QList<QByteArray> args;
        for (int i = 0; i < argc; ++i)
            args << argv[i];
        if (!xmlDir.isEmpty())
            args << "-o" << xmlDir + "/" + name.toUtf8() + ".xml,xml" << "-o" << "-,txt";
        QList<char *> argvx;
        for (QByteArray &a : args)
            argvx << a.data();
        failures += QTest::qExec(test.get(), argvx.size(), argvx.data()) != 0;
        fprintf(stderr, "CLASS-TIME %s %lld ms\n", qPrintable(name), static_cast<long long>(clock.elapsed()));
    }
    return failures;
}
