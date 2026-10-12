// precrime — a keyboard-first task manager for Omarchy, modelled on the reference app.

#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "alarms.h"
#include "backend.h"
#include "filepicker.h"
#include "gitsync.h"
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
    QCommandLineOption quickOption("quick", "Open Quick Entry (bind this to a key in Hyprland).");
    QCommandLineOption quickTextOption("quick-text", "Type this into Quick Entry (for screenshots).", "text");
    QCommandLineOption daemonOption("background", "Start without a window, to deliver alarms (for exec-once).");
    cli.addOptions({dbOption, demoOption, shotOption, sizeOption, sourceOption, editOption, themeOption, quickOption,
                    daemonOption, quickTextOption});
    cli.process(app);

    // One instance per user and database: a second launch hands its request
    // to the running one (open Quick Entry, or bring the window forward) and
    // exits, so a Hyprland bind on `precrime --quick` is instant.
    const bool offscreen = cli.isSet(shotOption) || cli.isSet(demoOption);
    const QString socketName = QStringLiteral("precrime-%1").arg(qgetenv("USER").isEmpty() ? "user" : qgetenv("USER"))
                               + (cli.isSet(dbOption) ? "-" + QString::number(qHash(cli.value(dbOption))) : QString());
    if (!offscreen) {
        QLocalSocket probe;
        probe.connectToServer(socketName);
        if (probe.waitForConnected(300)) {
            if (cli.isSet(daemonOption))
                return 0;
            probe.write(cli.isSet(quickOption) ? "quick\n" : "show\n");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

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

    FilePicker filePicker;
    // Git sync works on a working copy beside the database; demo and
    // screenshot runs leave it alone.
    std::unique_ptr<GitSync> gitSync;
    if (opened && !offscreen) {
        gitSync = std::make_unique<GitSync>(&store);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, gitSync.get(), [&gitSync] {
            // One last push so edits made just before quitting reach the remote.
            if (gitSync->configured())
                gitSync->syncBlocking(8000);
        });
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("filePicker", &filePicker);
    engine.rootContext()->setContextProperty("gitSync", gitSync.get());
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("storeError", opened ? QString() : dbPath + ": " + error);
    engine.rootContext()->setContextProperty("startHidden", cli.isSet(quickOption) || cli.isSet(daemonOption));
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    QLocalServer server;
    if (!offscreen) {
        QLocalServer::removeServer(socketName);  // A stale socket from a crash.
        server.listen(socketName);
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (QLocalSocket *client = server.nextPendingConnection()) {
                QObject::connect(client, &QLocalSocket::readyRead, client, [client, &backend] {
                    const QByteArray request = client->readAll().trimmed();
                    if (request == "quick")
                        backend.requestQuickEntry();
                    else
                        backend.requestWindow();
                    client->disconnectFromServer();
                });
                QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            }
        });
    }
    if (cli.isSet(quickOption))
        QTimer::singleShot(0, &backend, &Backend::requestQuickEntry);

    // Alarms are delivered by this process, so like the reference app's Alarm Helper it
    // keeps running after the window closes; Q or Ctrl+Q quits for real.
    std::unique_ptr<AlarmService> alarms;
    if (!offscreen && opened) {
        alarms = std::make_unique<AlarmService>(&store);
        QObject::connect(alarms.get(), &AlarmService::openTask, &backend, [&backend](qint64 id) {
            backend.requestWindow();
            backend.revealInList(id);
        });
        QObject::connect(alarms.get(), &AlarmService::completeTask, &backend, [&backend](qint64 id) { backend.toggleComplete(id); });
        app.setQuitOnLastWindowClosed(!store.setting("general.keepRunning", true).toBool());
    }

    if (cli.isSet(editOption))
        backend.beginEdit(cli.value(editOption).toLongLong());

    if (cli.isSet(shotOption) && window) {
        const auto size = cli.value(sizeOption).split('x');
        window->resize(size.value(0).toInt(), size.value(1).toInt());
        // Give QML a moment to lay out and load images before grabbing.
        QTimer::singleShot(700, &app, [&] {
            QQuickWindow *target = window;
            if (cli.isSet(quickOption)) {
                for (auto *w : window->findChildren<QQuickWindow *>())
                    if (w->title().contains("Quick Entry")) target = w;
                if (cli.isSet(quickTextOption))
                    if (auto *field = target->findChild<QObject *>("quickTitle"))
                        field->setProperty("text", cli.value(quickTextOption));
                QTimer::singleShot(300, &app, [&, target] {
                    target->grabWindow().save(cli.value(shotOption));
                    app.quit();
                });
                return;
            }
            window->grabWindow().save(cli.value(shotOption));
            app.quit();
        });
    }

    return app.exec();
}
