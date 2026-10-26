#include <QDir>
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
    // FAIRFAX_TEST_LOG_DIR also writes each class's results to <dir>/<class>.txt,
    // for hosts whose console output is hard to capture (the Windows build).
    const QString logDir = qEnvironmentVariable("FAIRFAX_TEST_LOG_DIR");
    int failures = 0;
    for (const auto &factory : testRegistry()) {
        std::unique_ptr<QObject> test(factory());
        QStringList args = app.arguments();
        if (!logDir.isEmpty())
            args << "-o" << QDir(logDir).filePath(QString::fromLatin1(test->metaObject()->className()) + ".txt") + ",txt"
                 << "-o" << "-,txt";
        failures += QTest::qExec(test.get(), args) != 0;
    }
    return failures;
}
