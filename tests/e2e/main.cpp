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
