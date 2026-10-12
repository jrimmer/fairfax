// precrime — a keyboard-first task manager for Omarchy, modelled on the reference app.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QDir>
#include <QFontDatabase>
#include <QApplication>
#include <QGuiApplication>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QProcess>
#include <QThread>
#if defined(Q_OS_LINUX)
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif
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
#include "tray.h"

namespace {

// Settings and Quick Entry close on Escape (or Ctrl+W) whatever control has
// focus inside them. QML shortcuts proved unreliable for this under Hyprland,
// where a focused field or checkbox could swallow the key first; an
// application-level filter sees it before anything else does.
class CloseKeys : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::KeyPress)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        const bool close = key->key() == Qt::Key_Escape ||
                           (key->key() == Qt::Key_W && key->modifiers() == Qt::ControlModifier);
        if (!close)
            return false;
        auto *window = qobject_cast<QWindow *>(watched);
        if (!window || window != QGuiApplication::focusWindow())
            return false;
        if (window->title() == "Precrime Settings" || window->title() == "Precrime Quick Entry") {
            // A window can keep Escape for itself (Quick Entry with an option
            // row open closes the row first).
            const QVariant own = window->property("escapeCloses");
            if (own.isValid() && !own.toBool())
                return false;
            // A popup inside (a menu, a date picker) gets Escape first.
            if (auto *quick = qobject_cast<QQuickWindow *>(window); quick && quick->activeFocusItem() &&
                QString(quick->activeFocusItem()->metaObject()->className()).contains("Popup"))
                return false;
            window->close();
            return true;
        }
        return false;
    }
};

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
    if (args.value(1) == "--complete") {
        // Values for shell completion: list names, open tasks ("id<TAB>title"),
        // or ctl commands. Kept quiet and fast; errors print nothing.
        const QString kind = args.value(2);
        if (kind == "lists") {
            for (const auto &l : store.lists())
                if (!l.deleted && !l.archived && l.kind != ListKind::Smart)
                    out << l.name << '\n';
        } else if (kind == "tasks") {
            for (const auto &t : store.tasks())
                if (!t.deleted && !t.completed && !t.title.isEmpty())
                    out << t.id << '\t' << t.title.left(60) << '\n';
        } else if (kind == "done") {
            for (const auto &t : store.tasks())
                if (!t.deleted && t.completed && !t.title.isEmpty() && t.completedAt.daysTo(QDateTime::currentDateTime()) < 14)
                    out << t.id << '\t' << t.title.left(60) << '\n';
        } else if (kind == "ctl") {
            for (const auto &name : Automation::commandNames())
                out << name << '\n';
        }
        return 0;
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
            first == "add" || first == "setup" || first == "--complete")
            return runHeadless(argc, argv);
    }

    // QApplication rather than QGuiApplication only for the tray icon's
    // menu, which Qt builds with widgets; the window itself is all QML.
    QApplication app(argc, argv);
    app.setApplicationName("precrime");
    app.setApplicationVersion("0.3.0");

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
    QCommandLineOption schemeOption("scheme", "Force system, dark or light (for screenshots).", "scheme");
    QCommandLineOption openOption("open", "Open Quick Open, to find any task or list.");
    QCommandLineOption settingsOption("settings", "Open Settings.");
    QCommandLineOption selfTestOption("selftest", "Drive the window with scripted keys and report QML errors (use with --demo).");
    QCommandLineOption quickTextOption("quick-text", "Type this into Quick Entry (for screenshots).", "text");
    QCommandLineOption daemonOption("background", "Start without a window, to deliver alarms (for exec-once).");
    cli.addOptions({dbOption, demoOption, shotOption, sizeOption, sourceOption, editOption, themeOption, quickOption,
                    daemonOption, quickTextOption, selfTestOption, openOption, settingsOption, schemeOption});
    cli.addPositionalArgument("url", "A precrime:// link to open.", "[url]");
    cli.process(app);
    const QString openUrl = cli.positionalArguments().value(0);

    // One instance per user and database: a second launch hands its request
    // to the running one (open Quick Entry, or bring the window forward) and
    // exits, so a Hyprland bind on `precrime --quick` is instant.
    const bool offscreen = cli.isSet(shotOption) || cli.isSet(demoOption);
    const QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME", "user"));
    const QString socketName = QStringLiteral("precrime-%1").arg(user)
                               + (cli.isSet(dbOption) ? "-" + QString::number(qHash(cli.value(dbOption))) : QString());
    if (!offscreen) {
        QLocalSocket probe;
        probe.connectToServer(socketName);
        // After an update the running instance is still the old build (it
        // stays resident for alarms). If it reports another version, ask it
        // to quit and carry on as the new instance.
        if (probe.waitForConnected(300)) {
            // The peer's pid, read from the kernel now, while connected: an
            // old build closes the connection when it doesn't understand us.
#if defined(Q_OS_LINUX)
            pid_t peer = 0;
            ucred cred{};
            socklen_t len = sizeof(cred);
            if (getsockopt(int(probe.socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0)
                peer = cred.pid;
#endif
            probe.write("version\n");
            QString running;
            if (probe.waitForReadyRead(500))
                running = QString::fromUtf8(probe.readAll()).trimmed();
            if (running != app.applicationVersion()) {
                QLocalSocket quit;
                quit.connectToServer(socketName);
                if (quit.waitForConnected(300)) {
                    quit.write("quit\n");
                    quit.waitForBytesWritten(300);
                }
#if defined(Q_OS_LINUX)
                auto stillRunning = [&] { return peer > 0 && ::kill(peer, 0) == 0; };
                for (int i = 0; i < 15 && stillRunning(); ++i)
                    QThread::msleep(100);
                // Only ever stop a process that really is Precrime.
                if (stillRunning() && QFileInfo(QStringLiteral("/proc/%1/exe").arg(peer)).symLinkTarget().contains("precrime")) {
                    ::kill(peer, SIGTERM);
                    for (int i = 0; i < 20 && stillRunning(); ++i)
                        QThread::msleep(100);
                }
#else
                // Elsewhere the old instance is only asked to quit; give it a
                // moment to let go of the socket.
                QThread::msleep(1500);
#endif
                probe.abort();
            } else {
                probe.abort();
                probe.connectToServer(socketName);
                probe.waitForConnected(300);
            }
        }
        if (probe.state() == QLocalSocket::ConnectedState) {
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
    CloseKeys closeKeys;
    app.installEventFilter(&closeKeys);

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
    theme.setScheme(store.setting("appearance.scheme", "system").toString());
    Backend backend(&store);
    QObject::connect(&backend, &Backend::settingsChanged, &theme,
                     [&] { theme.setScheme(store.setting("appearance.scheme", "system").toString()); });
    if (cli.isSet(schemeOption))
        theme.setScheme(cli.value(schemeOption));
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
    std::unique_ptr<Tray> tray;
    std::unique_ptr<TodoistSync> todoist;
    if (opened && !offscreen) {
        todoist = std::make_unique<TodoistSync>(&store);
        if (todoist->connected())
            QTimer::singleShot(3000, todoist.get(), &TodoistSync::syncNow);
    }
    engine.rootContext()->setContextProperty("todoist", todoist.get());
    if (opened && !offscreen) {
        tray = std::make_unique<Tray>(&store, &backend);
        tray->canSync = [&] { return (gitSync && gitSync->configured()) || (todoist && todoist->connected()); };
        tray->syncNow = [&] {
            if (gitSync && gitSync->configured()) gitSync->syncNow();
            if (todoist && todoist->connected()) todoist->syncNow();
        };
        QObject::connect(&backend, &Backend::settingsChanged, tray.get(),
                         [&] { tray->setVisible(store.setting("general.trayIcon", true).toBool()); });
    }
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
                    if (request == "version") {
                        client->write(QCoreApplication::applicationVersion().toUtf8() + "\n");
                        client->flush();
                        client->waitForBytesWritten(200);
                        client->disconnectFromServer();
                        return;
                    }
                    if (request == "quit") {
                        client->disconnectFromServer();
                        QCoreApplication::quit();
                        return;
                    }
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
        if (tray)
            QObject::connect(alarms.get(), &AlarmService::showFallback, tray.get(), &Tray::showMessage);
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
