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
        QElapsedTimer clock;
        clock.start();
        // PRECRIME_E2E_XMLDIR=dir also writes dir/<class>.xml, which has the time of every test.
        const QByteArray xmlDir = qgetenv("PRECRIME_E2E_XMLDIR");
        QList<QByteArray> args;
        for (int i = 0; i < argc; ++i)
            args << argv[i];
        if (!xmlDir.isEmpty())
            args << "-o" << xmlDir + "/" + test->metaObject()->className() + ".xml,xml" << "-o" << "-,txt";
        QList<char *> argvx;
        for (QByteArray &a : args)
            argvx << a.data();
        failures += QTest::qExec(test.get(), argvx.size(), argvx.data()) != 0;
        fprintf(stderr, "CLASS-TIME %s %lld ms\n", test->metaObject()->className(), static_cast<long long>(clock.elapsed()));
    }
    return failures;
}
