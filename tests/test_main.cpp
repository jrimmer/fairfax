#include <QGuiApplication>
#include <QtTest>
#include <memory>

#include "../src/keyring.h"
#include "testregistry.h"

QList<TestFactory> &testRegistry() {
    static QList<TestFactory> registry;
    return registry;
}

int main(int argc, char *argv[]) {
    Keyring::useMemoryStore(true);   // No test reads or writes the real keyring.
    QGuiApplication app(argc, argv);
    int failures = 0;
    for (const auto &factory : testRegistry()) {
        std::unique_ptr<QObject> test(factory());
        failures += QTest::qExec(test.get(), argc, argv) != 0;
    }
    return failures;
}
