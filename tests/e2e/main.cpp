#include <QApplication>
#include <QQuickStyle>
#include <QtTest>
#include <memory>

#include "../testregistry.h"

QList<TestFactory> &testRegistry() {
    static QList<TestFactory> registry;
    return registry;
}

int main(int argc, char *argv[]) {
    // No session bus: the file chooser portal would otherwise put a real dialog on the
    // desktop of whoever runs the tests; with none, FilePicker reports that it failed.
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/precrime-e2e");
    QQuickStyle::setStyle("Material");
    QApplication app(argc, argv);
    int failures = 0;
    for (const auto &factory : testRegistry()) {
        std::unique_ptr<QObject> test(factory());
        // PRECRIME_E2E_CLASS=Tasks runs only the test classes whose name contains it,
        // so a function name given as an argument is not looked up in the others.
        const QByteArray only = qgetenv("PRECRIME_E2E_CLASS");
        if (!only.isEmpty() && !QByteArray(test->metaObject()->className()).contains(only))
            continue;
        failures += QTest::qExec(test.get(), argc, argv) != 0;
    }
    return failures;
}
