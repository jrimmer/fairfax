// precrime — a keyboard-first task manager for Omarchy, modelled on the reference app.

#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "store.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("precrime");
    app.setApplicationVersion("0.1.0");

    // Matches the window to precrime.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("precrime");
    app.setWindowIcon(QIcon::fromTheme("precrime"));

    QCommandLineParser cli;
    cli.setApplicationDescription("A keyboard-first task manager for Omarchy.");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption dbOption("db", "Use this database file.", "path");
    QCommandLineOption demoOption("demo", "Open a throwaway database filled with sample tasks.");
    QCommandLineOption shotOption("screenshot", "Render the window offscreen, save it as a PNG and quit.", "png");
    QCommandLineOption sizeOption("size", "Window size for --screenshot, as WIDTHxHEIGHT.", "size", "1180x760");
    QCommandLineOption sourceOption("source", "Open this list first (today, all, list:3, …).", "key");
    QCommandLineOption editOption("edit", "Open the editor on this task id (for screenshots).", "id");
    QCommandLineOption themeOption("theme-dir", "Read the theme from this directory instead of Omarchy's.", "dir");
    cli.addOptions({dbOption, demoOption, shotOption, sizeOption, sourceOption, editOption, themeOption});
    cli.process(app);

    QQuickStyle::setStyle("Material");

    QTemporaryDir demoDir;
    QString dbPath = cli.value(dbOption);
    if (cli.isSet(demoOption))
        dbPath = demoDir.filePath("demo.db");
    if (dbPath.isEmpty())
        dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/precrime.db";

    Store store;
    QString error;
    const bool opened = store.open(dbPath, &error);

    Theme theme(cli.value(themeOption));
    Backend backend(&store);
    if (opened) {
        if (cli.isSet(demoOption))
            backend.seedDemo();
        backend.ensureDefaults();
        if (cli.isSet(sourceOption))
            backend.selectSource(cli.value(sourceOption));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("storeError", opened ? QString() : dbPath + ": " + error);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    if (cli.isSet(editOption))
        backend.beginEdit(cli.value(editOption).toLongLong());

    if (cli.isSet(shotOption) && window) {
        const auto size = cli.value(sizeOption).split('x');
        window->resize(size.value(0).toInt(), size.value(1).toInt());
        // Give QML a moment to lay out and load images before grabbing.
        QTimer::singleShot(700, &app, [&] {
            window->grabWindow().save(cli.value(shotOption));
            app.quit();
        });
    }

    return app.exec();
}
