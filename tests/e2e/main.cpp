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
        failures += QTest::qExec(test.get(), argc, argv) != 0;
    }
    return failures;
}
