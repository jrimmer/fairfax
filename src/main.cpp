// precrime — a keyboard-first task manager for Omarchy, modelled on the reference app.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QFileInfo>
#include <QIcon>
#include <QProcess>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <functional>

#include "alarms.h"
#include "automation.h"
#include "calendar.h"
#include "cli.h"
#include "exchange.h"
#include "markdown.h"
#include "selftest.h"
#include "sounds.h"
#include "views.h"
#include "backend.h"
#include "filepicker.h"
#include "gitsync.h"
#include "store.h"
#include "theme.h"
#include "todoist.h"

namespace {

QString databasePath(const QStringList &args) {
    const int i = args.indexOf("--db");
    if (i >= 0 && i + 1 < args.size())
        return args.at(i + 1);
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/precrime/precrime.db";
}

// `precrime mcp`, `precrime ctl …` and `precrime --waybar` run without a
// window against the same database, so scripts, Claude and the bar can use
// Precrime while the app is open (SQLite's WAL mode keeps that safe).
int runHeadless(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("precrime");
    QStringList args = app.arguments();
    if (args.value(1) == "setup") {
        // `precrime setup omarchy [--remove] [--key …]` runs the packaged
        // script (or the repo's copy when running from a build).
        if (args.value(2) != "omarchy") {
            QTextStream(stderr) << "usage: precrime setup omarchy [--remove] [--key \"SUPER + SHIFT + T\"]" << Qt::endl;
            return 2;
        }
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString &script : {QStringLiteral("/usr/lib/precrime/precrime-omarchy-setup"),
                                      appDir + "/../scripts/precrime-omarchy-setup"}) {
            if (QFileInfo(script).isExecutable()) {
                QProcess run;
                run.setProcessChannelMode(QProcess::ForwardedChannels);
                run.start(script, args.mid(3));
                run.waitForFinished(-1);
                return run.exitCode();
            }
        }
        QTextStream(stderr) << "precrime: the Omarchy setup script isn't installed." << Qt::endl;
        return 1;
    }
    const QString dbPath = databasePath(args);
    if (const int i = args.indexOf("--db"); i >= 0)
        args.remove(i, qMin<qsizetype>(2, args.size() - i));
    Store store;
    QString error;
    QTextStream out(stdout), err(stderr);
    if (!store.open(dbPath, &error)) {
        err << "precrime: can't open the database: " << error << Qt::endl;
        return 1;
    }
    const bool shorthand = args.value(0).section('/', -1) == "pc";
    if (shorthand)
        return Cli::pc(store, args.mid(1), out, err);
    const QString mode = args.value(1);
    if (mode == "add")
        return Cli::add(store, args.mid(2), out, err);
    if (mode == "--waybar") {
        // A Waybar custom module: overdue and due-today counts, like the reference app's
        // Dock badge.
        ScheduleSettings settings;
        settings.todayIncludesOverdue = store.setting("today.overdue", true).toBool();
        settings.todayIncludesStarred = store.setting("today.starred", false).toBool();
        settings.todayIncludesStillWorking = store.setting("today.stillWorking", true).toBool();
        const ViewBuilder views(store, QDateTime::currentDateTime(), settings);
        const auto today = views.countsForFocus(Source::Today);
        const QString text = today.overdue ? QStringLiteral("%1 | %2").arg(today.overdue).arg(today.undone)
                                           : QString::number(today.undone);
        const QJsonObject json{{"text", today.undone ? text : QString()},
                               {"tooltip", QStringLiteral("%1 overdue, %2 in Today").arg(today.overdue).arg(today.undone)},
                               {"class", today.overdue ? "overdue" : today.undone ? "due" : "clear"}};
        out << QJsonDocument(json).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    if (mode == "import") {
        // precrime import FILE: a the reference app database or .taskdb backup, or .ics.
        const QString file = args.value(2);
        if (file.isEmpty()) {
            err << "usage: precrime import <tasks.db | backup.taskdb | tasks.ics | TickTick-backup.csv> [--db PATH]" << Qt::endl;
            return 2;
        }
        const ImportResult r = file.endsWith(".ics", Qt::CaseInsensitive) ? Exchange::importIcs(store, file, 0)
                               : Exchange::isTickTickBackup(file)          ? Exchange::importTickTick(store, file)
                                                                           : Exchange::importTwoDo(store, file);
        if (!r.error.isEmpty()) {
            err << "precrime: import failed: " << r.error << Qt::endl;
            return 1;
        }
        out << QStringLiteral("Imported %1 lists, %2 tasks, %3 tags, %4 alerts").arg(r.lists).arg(r.tasks).arg(r.tags).arg(r.alarms)
            << (r.updated ? QStringLiteral("; filled in %1 earlier imports").arg(r.updated) : QString()) << "." << Qt::endl;
        return 0;
    }
    Automation automation(store);
    int status = 0;
    if (mode == "mcp") {
        McpServer server(automation, "0.1.0");
        QTextStream in(stdin);
        status = server.run(in, out);
    } else {
        status = automation.runCli(args.mid(2), out, err);
    }
    return status;
}

}  // namespace

int main(int argc, char *argv[]) {
    {
        // Invoked as `pc` (a symlink), everything is the terminal shorthand.
        const QString self = QString::fromLocal8Bit(argv[0]).section('/', -1);
        const QString first = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
        if (self == "pc" || first == "mcp" || first == "ctl" || first == "--waybar" || first == "import" ||
            first == "add" || first == "setup")
            return runHeadless(argc, argv);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("precrime");
    app.setApplicationVersion("0.2.0");

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
    QCommandLineOption openOption("open", "Open Quick Open, to find any task or list.");
    QCommandLineOption settingsOption("settings", "Open Settings.");
    QCommandLineOption selfTestOption("selftest", "Drive the window with scripted keys and report QML errors (use with --demo).");
    QCommandLineOption quickTextOption("quick-text", "Type this into Quick Entry (for screenshots).", "text");
    QCommandLineOption daemonOption("background", "Start without a window, to deliver alarms (for exec-once).");
    cli.addOptions({dbOption, demoOption, shotOption, sizeOption, sourceOption, editOption, themeOption, quickOption,
                    daemonOption, quickTextOption, selfTestOption, openOption, settingsOption});
    cli.addPositionalArgument("url", "A precrime:// link to open.", "[url]");
    cli.process(app);
    const QString openUrl = cli.positionalArguments().value(0);

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
            if (openUrl.startsWith("precrime:"))
                probe.write(("url " + openUrl + "\n").toUtf8());
            else if (cli.isSet(openOption))
                probe.write("open\n");
            else if (cli.isSet(settingsOption))
                probe.write("settings\n");
            else
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
        dbPath = databasePath({});

    Store store;
    QString error;
    const bool opened = store.open(dbPath, &error);

    Theme theme(cli.value(themeOption));
    Backend backend(&store);
    NotesHighlighter::registerType();

    Sounds sounds;
    sounds.setEnabled(store.setting("general.sounds", true).toBool());
    QObject::connect(&backend, &Backend::settingsChanged, &sounds, [&] { sounds.setEnabled(store.setting("general.sounds", true).toBool()); });
    QObject::connect(&backend, &Backend::taskCompleted, &sounds, [&sounds](bool done) { if (done) sounds.complete(); else sounds.uncomplete(); });

    CalendarFeed calendar;
    calendar.setPalette(theme.listPalette());
    calendar.setSources(store.setting("calendar.sources").toString().split('\n', Qt::SkipEmptyParts));
    QObject::connect(&theme, &Theme::changed, &calendar, [&] { calendar.setPalette(theme.listPalette()); });

    // The same command set scripts and Claude use, wired to this window.
    Automation automation(store);
    automation.selectSource = [&backend](const QString &key) { backend.selectSource(key); };
    automation.openTask = [&backend](qint64 id) {
        backend.requestWindow();
        backend.revealInList(id);
    };
    automation.selectedTaskIds = [&backend] { return backend.selectedIds(); };
    auto handleUrl = [&](const QString &url) {
        const QJsonObject result = automation.handleUrl(QUrl(url));
        if (result.value("quickEntry").toBool())
            backend.requestQuickEntryWith(result.value("text").toString());
        else if (result.contains("search"))
            backend.setSearch(result.value("search").toString());
        backend.requestWindow();
    };

    // Changes written by `precrime ctl` or the MCP server show up here.
    QTimer externalChanges;
    QObject::connect(&externalChanges, &QTimer::timeout, &store, [&store] { store.reloadIfChangedElsewhere(); });
    externalChanges.start(1500);
    if (opened) {
        if (cli.isSet(demoOption)) {
            backend.seedDemo();
            calendar.setSources(store.setting("calendar.sources").toString().split('\n', Qt::SkipEmptyParts));
        }
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
    std::unique_ptr<TodoistSync> todoist;
    if (opened && !offscreen) {
        todoist = std::make_unique<TodoistSync>(&store);
        if (todoist->connected())
            QTimer::singleShot(3000, todoist.get(), &TodoistSync::syncNow);
    }
    engine.rootContext()->setContextProperty("todoist", todoist.get());
    engine.rootContext()->setContextProperty("calendar", &calendar);
    engine.rootContext()->setContextProperty("sounds", &sounds);
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
                QObject::connect(client, &QLocalSocket::readyRead, client, [client, &backend, handleUrl] {
                    const QByteArray request = client->readAll().trimmed();
                    if (request == "quick")
                        backend.requestQuickEntry();
                    else if (request.startsWith("url "))
                        handleUrl(QString::fromUtf8(request.mid(4)));
                    else if (request == "open")
                        backend.requestPanel("open");
                    else if (request == "settings")
                        backend.requestPanel("settings");
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
    if (openUrl.startsWith("precrime:"))
        QTimer::singleShot(0, &backend, [handleUrl, openUrl] { handleUrl(openUrl); });
    if (cli.isSet(openOption))
        QTimer::singleShot(300, &backend, [&backend] { backend.requestPanel("open"); });
    if (cli.isSet(settingsOption))
        QTimer::singleShot(300, &backend, [&backend] { backend.requestPanel("settings"); });

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

    // File > New Window: another window over the same store, with its own
    // selection, list and search, as the reference app's multiple windows have.
    std::function<void()> openWindow;
    openWindow = [&engine, &store, &app, &openWindow]() {
        auto *extra = new Backend(&store, &app);
        auto *context = new QQmlContext(engine.rootContext(), extra);
        context->setContextProperty("backend", extra);
        context->setContextProperty("startHidden", false);
        QQmlComponent component(&engine, QUrl("qrc:/qml/Main.qml"));
        QObject *window = component.create(context);
        if (!window) {
            qWarning("precrime: couldn't open another window: %s", qPrintable(component.errorString()));
            delete extra;
            return;
        }
        // Closing the window releases its backend.
        QObject::connect(qobject_cast<QQuickWindow *>(window), &QQuickWindow::visibleChanged, extra, [window, extra](bool shown) {
            if (!shown) {
                window->deleteLater();
                extra->deleteLater();
            }
        });
        QObject::connect(extra, &Backend::newWindowRequested, &app, [&openWindow] { openWindow(); });
    };
    QObject::connect(&backend, &Backend::newWindowRequested, &app, [&openWindow] { openWindow(); });

    if (cli.isSet(selfTestOption) && window)
        SelfTest::run(window, &backend);

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
