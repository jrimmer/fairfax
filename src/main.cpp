// fairfax — a keyboard-first task manager for Omarchy, modelled on the reference app.

#include <QClipboard>
#include <QBuffer>
#include <QCommandLineParser>
#include <QDataStream>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QDir>
#include <QFontDatabase>
#include <QApplication>
#include <QGuiApplication>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QThread>
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
#include <QSvgRenderer>
#endif
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
#include <QQmlExpression>
#include <QQuickStyle>
#include <QPointer>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <functional>

#include "alarms.h"
#include "appsetup.h"
#include "automation.h"
#include "calendar.h"
#include "diagnostics.h"
#include "cli.h"
#include "exchange.h"
#include "hyprland.h"
#include "markdown.h"
#include "selftest.h"
#include "sounds.h"
#include "views.h"
#include "backend.h"
#include "filepicker.h"
#include "foldersync.h"
#include "gitsync.h"
#include "synchub.h"
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
        if (window->title() == "Fairfax Settings" || window->title() == "Fairfax Quick Entry") {
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
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/fairfax/fairfax.db";
}

// Compiling the version in keeps it in step with fairfax.pro, where the
// build scripts also read it for the app bundle and the installer.
#ifndef FAIRFAX_VERSION
#define FAIRFAX_VERSION "0.0.0"
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
// Renders the SVG built into the binary to an image: a Windows .ico for
// bin/build-windows.ps1, or a PNG for bin/build-macos to turn into the
// .icns. Both scripts call this before the real build; Linux makes its icon
// from the installed SVG with the desktop's own tools.
int renderIcon(const QString &file) {
    QSvgRenderer svg(QStringLiteral(":/icon.svg"));
    if (!svg.isValid()) {
        QTextStream(stderr) << "fairfax: the built-in icon.svg could not be read" << Qt::endl;
        return 1;
    }
#if defined(Q_OS_MACOS)
    // The macOS iconset starts from the largest PNG iconutil accepts.
    constexpr int side = 1024;
#else
    // An ICO entry can only name a size up to 256 (a width/height byte of 0
    // means 256), so render the one frame the header describes exactly.
    constexpr int side = 256;
#endif
    QImage canvas(side, side, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);
        svg.render(&painter, QRectF(0, 0, side, side));
    }
#if defined(Q_OS_MACOS)
    if (!canvas.save(file, "PNG")) {
        QTextStream(stderr) << "fairfax: can't write " << file << Qt::endl;
        return 1;
    }
    return 0;
#else
    // A .ico is an ICONDIR header, one ICONDIRENTRY, then the PNG data.
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!canvas.save(&buffer, "PNG")) {
        QTextStream(stderr) << "fairfax: could not encode the icon as PNG" << Qt::endl;
        return 1;
    }
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << "fairfax: can't write " << file << Qt::endl;
        return 1;
    }
    QDataStream stream(&out);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(1);  // reserved, type=icon, one image
    stream << quint8(0) << quint8(0);                  // 256 px is written as 0
    stream << quint8(0) << quint8(0);                  // palette and reserved
    stream << quint16(1) << quint16(32);               // colour planes and bits per pixel
    stream << quint32(png.size()) << quint32(22);      // data size, offset after the 6 + 16 byte header
    stream.writeRawData(png.constData(), png.size());
    return 0;
#endif
}
#endif

// `fairfax mcp`, `fairfax ctl …` and `fairfax --waybar` run without a
// window against the same database, so scripts, Claude and the bar can use
// Fairfax while the app is open (SQLite's WAL mode keeps that safe).
int runHeadless(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("fairfax");
    QStringList args = app.arguments();
    if (args.value(1) == "setup") {
#if !defined(Q_OS_LINUX)
        QTextStream(stderr) << "fairfax: 'setup omarchy' is only on Linux (Omarchy); Fairfax runs on macOS and Windows without it." << Qt::endl;
        return 2;
#else
        // `fairfax setup omarchy [--remove] [--key …]` runs the packaged
        // script (or the repo's copy when running from a build).
        if (args.value(2) != "omarchy") {
            QTextStream(stderr) << "usage: fairfax setup omarchy [--remove] [--key \"SUPER + SHIFT + T\"]" << Qt::endl;
            return 2;
        }
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString &script : {QStringLiteral("/usr/lib/fairfax/fairfax-omarchy-setup"),
                                      appDir + "/../scripts/fairfax-omarchy-setup"}) {
            if (QFileInfo(script).isExecutable()) {
                QProcess run;
                run.setProcessChannelMode(QProcess::ForwardedChannels);
                run.start(script, args.mid(3));
                run.waitForFinished(-1);
                return run.exitCode();
            }
        }
        QTextStream(stderr) << "fairfax: the Omarchy setup script isn't installed." << Qt::endl;
        return 1;
#endif
    }
    const QString dbPath = databasePath(args);
    // The running window names its socket after the raw --db argument, so ctl
    // must too: capturing it before the argument is removed below.
    const int dbAt = args.indexOf("--db");
    const QString dbKey = (dbAt >= 0 && dbAt + 1 < args.size()) ? args.at(dbAt + 1) : QString();
    if (const int i = args.indexOf("--db"); i >= 0)
        args.remove(i, qMin<qsizetype>(2, args.size() - i));
    Store store;
    QString error;
    QTextStream out(stdout), err(stderr);
    if (!store.open(dbPath, &error)) {
        err << "fairfax: can't open the database: " << error << Qt::endl;
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
    const bool shorthand = Cli::isShorthandName(args.value(0));
    if (shorthand)
        return Cli::shorthand(store, args.mid(1), out, err);
    const QString mode = args.value(1);
    if (mode == "add")
        return Cli::add(store, args.mid(2), out, err);
    if (mode == "--waybar") {
#if !defined(Q_OS_LINUX)
        QTextStream(stderr) << "fairfax: --waybar is only on Linux; on macOS and Windows use the tray badge instead." << Qt::endl;
        return 2;
#else
        // A Waybar custom module: the count the "Badge shows" setting picks
        // (overdue, due today or both), like the reference app's Dock badge.
        const ScheduleSettings settings = ScheduleSettings::fromStore(store);
        const ViewBuilder views(store, QDateTime::currentDateTime(), settings);
        const auto today = views.countsForFocus(Source::Today);
        const QString metric = store.setting("alerts.badge", "both").toString();
        const int count = views.badgeCount(metric);
        const int dueToday = settings.todayIncludesOverdue ? today.undone - today.overdue : today.undone;
        const QString text = metric == "both" && today.overdue ? QStringLiteral("%1 | %2").arg(today.overdue).arg(dueToday)
                                                               : QString::number(count);
        QString tooltip = QStringLiteral("%1 overdue, %2 in Today").arg(today.overdue).arg(today.undone);
        QString cls = today.overdue ? "overdue" : today.undone ? "due" : "clear";
        // Alarms are delivered by the resident app; with alarms waiting and
        // no instance listening, say so rather than let them silently miss.
        if (AlarmTimes::hasPending(store, QDateTime::currentDateTime())) {
            const int dbAt = args.indexOf("--db");
            const QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME", "user"));
            const QString socket = QStringLiteral("fairfax-%1").arg(user)
                                   + (dbAt >= 0 && dbAt + 1 < args.size() ? "-" + QString::number(qHash(args.at(dbAt + 1))) : QString());
            QLocalSocket probe;
            probe.connectToServer(socket);
            if (!probe.waitForConnected(300)) {
                cls = "no-daemon";
                tooltip += QStringLiteral("\nAlarms won't go off: Fairfax isn't running.");
            }
        }
        const QJsonObject json{{"text", count ? text : cls == "no-daemon" ? QStringLiteral("!") : QString()},
                               {"tooltip", tooltip},
                               {"class", cls}};
        out << QJsonDocument(json).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
#endif
    }
    if (mode == "import-mail") {
        // Mail to Fairfax from a pipe: a message on stdin or in files, or a Maildir.
        QFile in;
        in.open(stdin, QIODevice::ReadOnly);
        return Cli::importMail(store, args.mid(2), in, out, err);
    }
    if (mode == "import") {
        // fairfax import FILE: a the reference app database or .taskdb backup, or .ics.
        const QString file = args.value(2);
        if (file.isEmpty()) {
            err << "usage: fairfax import <tasks.db | backup.taskdb | tasks.ics | TickTick-backup.csv | Things.json | OmniFocus.taskpaper> [--db PATH]" << Qt::endl;
            return 2;
        }
        const ImportResult r = file.endsWith(".ics", Qt::CaseInsensitive) ? Exchange::importIcs(store, file, 0)
                               : Exchange::isTickTickBackup(file)          ? Exchange::importTickTick(store, file)
                               : file.endsWith(".json", Qt::CaseInsensitive) && Exchange::isThingsExport(file) ? Exchange::importThings(store, file)
                               : Exchange::isTaskPaper(file)               ? Exchange::importOmniFocus(store, file)
                                                                           : Exchange::importTwoDo(store, file);
        if (!r.error.isEmpty()) {
            err << "fairfax: import failed: " << r.error << Qt::endl;
            return 1;
        }
        out << QStringLiteral("Imported %1 lists, %2 tasks, %3 tags, %4 alerts").arg(r.lists).arg(r.tasks).arg(r.tags).arg(r.alarms)
            << (r.updated ? QStringLiteral("; filled in %1 earlier imports").arg(r.updated) : QString()) << "." << Qt::endl;
        return 0;
    }
    Automation automation(store);
    if (mode == "ctl" && args.value(2) == "myday") {
        // Open the running window in My Day mode, as the sidebar item and
        // Ctrl+Shift+P do. Without an instance there is nowhere to show it.
        QString sendError;
        if (!Cli::sendToRunning(dbKey, "myday\n", &sendError)) {
            err << "fairfax ctl myday: " << sendError << Qt::endl;
            return 1;
        }
        out << "Opening My Day." << Qt::endl;
        return 0;
    }
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
        // Invoked as `frfx` (a symlink), everything is the terminal shorthand.
        const QString self = QString::fromLocal8Bit(argv[0]);
        const QString first = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        if (first == "--render-icon" && argc > 2) {
            QGuiApplication iconApp(argc, argv);
            return renderIcon(QString::fromLocal8Bit(argv[2]));
        }
#endif
        if (Cli::isShorthandName(self) || first == "mcp" || first == "ctl" || first == "--waybar" || first == "import" || first == "import-mail" ||
            first == "add" || first == "setup" || first == "--complete")
            return runHeadless(argc, argv);
    }

    // QApplication rather than QGuiApplication only for the tray icon's
    // menu, which Qt builds with widgets; the window itself is all QML.
    QApplication app(argc, argv);
    Diagnostics::install();
    app.setApplicationName("fairfax");
    app.setApplicationVersion(QStringLiteral(FAIRFAX_VERSION));

    // Matches the window to fairfax.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("fairfax");
    app.setWindowIcon(QIcon::fromTheme("fairfax"));

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
    QCommandLineOption evalOption("shot-eval", "Evaluate this QML expression in the main window before --screenshot.", "js");
    QCommandLineOption shotWindowOption("shot-window", "Capture the window whose title contains this instead of the main one.", "title");
    QCommandLineOption newWindowOption("new-window", "Open another window (the desktop file's New Window action).");
    QCommandLineOption daemonOption("background", "Start without a window, to deliver alarms (for exec-once).");
    cli.addOptions({dbOption, demoOption, shotOption, sizeOption, sourceOption, editOption, themeOption, quickOption,
                    daemonOption, quickTextOption, selfTestOption, openOption, settingsOption, schemeOption,
                    evalOption, shotWindowOption, newWindowOption});
    cli.addPositionalArgument("url", "A fairfax:// link to open.", "[url]");
    cli.process(app);
    const QString openUrl = cli.positionalArguments().value(0);

    // One instance per user and database: a second launch hands its request
    // to the running one (open Quick Entry, or bring the window forward) and
    // exits, so a Hyprland bind on `fairfax --quick` is instant.
    const bool offscreen = cli.isSet(shotOption) || cli.isSet(demoOption);
    const QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME", "user"));
    const QString socketName = QStringLiteral("fairfax-%1").arg(user)
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
                // Only ever stop a process that really is Fairfax.
                if (stillRunning() && QFileInfo(QStringLiteral("/proc/%1/exe").arg(peer)).symLinkTarget().contains("fairfax")) {
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
            if (openUrl.startsWith("fairfax:"))
                probe.write(("url " + openUrl + "\n").toUtf8());
            else if (cli.isSet(openOption))
                probe.write("open\n");
            else if (cli.isSet(settingsOption))
                probe.write("settings\n");
            else if (cli.isSet(newWindowOption))
                probe.write("newwindow\n");
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

    // The demo and the selftest open on Today, as they always did, rather than
    // on the new fresh-database default of My Day. The database is a throwaway
    // one, so seeding the choice there is harmless. A screenshot run can point
    // at a real database, so it is moved to Today without writing a setting:
    // a screenshot must not change the owner's saved Default view.
    if (cli.isSet(demoOption))
        store.setSetting("ui.defaultView", "last");

    AppServices services(store, cli.value(themeOption));
    Theme &theme = services.theme;
    Backend &backend = services.backend;
    Sounds &sounds = services.sounds;
    CalendarFeed &calendar = services.calendar;
    if (cli.isSet(schemeOption))
        theme.setScheme(cli.value(schemeOption));

    // The same command set scripts and Claude use, wired to this window.
    Automation automation(store);
    automation.selectSource = [&backend](const QString &key) { backend.selectSource(key); };
    automation.openTask = [&backend](qint64 id) {
        backend.requestWindow();
        backend.revealInList(id);
    };
    automation.selectedTaskIds = [&backend] { return backend.selectedIds(); };
    automation.clipboardText = [] { return QGuiApplication::clipboard()->text(); };
    auto handleUrl = [&](const QString &url) {
        const QJsonObject result = automation.handleUrl(QUrl(url));
        if (result.contains("clipboard"))
            QGuiApplication::clipboard()->setText(result.value("clipboard").toString());
        // x-success and x-error are other apps' URLs to open; a file: URL is
        // never a callback.
        const QUrl callback(result.value("callback").toString());
        if (callback.isValid() && !callback.scheme().isEmpty() && callback.scheme() != "file")
            QDesktopServices::openUrl(callback);
        if (result.value("quickEntry").toBool())
            backend.requestQuickEntryWith(result.value("text").toString());
        else if (result.contains("search"))
            backend.setSearch(result.value("search").toString());
        backend.requestWindow();
    };

    // Changes written by `fairfax ctl` or the MCP server show up here.
    QTimer externalChanges;
    QObject::connect(&externalChanges, &QTimer::timeout, &store, [&store] { store.reloadIfChangedElsewhere(); });
    externalChanges.start(1500);
    if (opened) {
        if (cli.isSet(demoOption)) {
            backend.seedDemo();
            calendar.setMyEmails(store.setting("calendar.emails").toString());
            calendar.setSources(store.setting("calendar.sources").toString().split('\n', Qt::SkipEmptyParts));
        }
        backend.ensureDefaults();
        // A screenshot of a real database lands on Today without touching the
        // stored Default view choice; --source and --shot-eval can still move it.
        if (cli.isSet(shotOption) && !cli.isSet(demoOption))
            backend.selectSource("today");
        if (cli.isSet(sourceOption))
            backend.selectSource(cli.value(sourceOption));
    }

    services.startSync(opened, offscreen);
    QQmlApplicationEngine engine;
    SyncHub &syncHub = services.syncHub;
    std::unique_ptr<Tray> tray;
    if (opened && !offscreen) {
        tray = std::make_unique<Tray>(&store, &backend);
        tray->canSync = [&] { return syncHub.active(); };
        tray->syncNow = [&] { syncHub.syncAll(); };
        QObject::connect(&backend, &Backend::settingsChanged, tray.get(),
                         [&] { tray->setVisible(store.setting("general.trayIcon", true).toBool()); });
    }
    services.install(engine, opened ? QString() : dbPath + ": " + error,
                     cli.isSet(quickOption) || cli.isSet(daemonOption));
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
                    else if (request == "myday") {
                        backend.selectSource("myday");
                        backend.requestWindow();
                    } else if (request.startsWith("url "))
                        handleUrl(QString::fromUtf8(request.mid(4)));
                    else if (request == "open")
                        backend.requestPanel("open");
                    else if (request == "settings")
                        backend.requestPanel("settings");
                    else if (request == "newwindow")
                        backend.requestNewWindow();
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
    if (openUrl.startsWith("fairfax:"))
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
        QObject::connect(alarms.get(), &AlarmService::performAction, &backend, [&backend](qint64 id) { backend.performAction(id); });
        // The end-of-day reminder opens the shutdown review when clicked.
        QObject::connect(alarms.get(), &AlarmService::myDayReviewRequested, &backend, [&backend] {
            backend.selectSource("myday");
            backend.myDayReview();
            backend.requestWindow();
        });
        app.setQuitOnLastWindowClosed(!store.setting("general.keepRunning", true).toBool());
    }

    // File > New Window: another window over the same store, with its own
    // selection, list and search, as the reference app's multiple windows have. The open
    // windows' lists are remembered, so a relaunch brings them back; closing
    // a window forgets it.
    QList<QPointer<Backend>> extraBackends;
    bool quitting = false;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&quitting] { quitting = true; });
    auto saveWindows = [&extraBackends, &store, &offscreen] {
        if (offscreen)
            return;
        QStringList keys;
        for (const auto &b : extraBackends)
            if (b)
                keys << b->sourceKey();
        store.setSetting("windows.extra", keys.join('\n'));
    };
    std::function<void(const QString &)> openWindow;
    openWindow = [&](const QString &sourceKey) {
        auto *extra = new Backend(&store, &app);
        if (!sourceKey.isEmpty())
            extra->selectSource(sourceKey);
        auto *context = new QQmlContext(engine.rootContext(), extra);
        context->setContextProperty("backend", extra);
        context->setContextProperty("startHidden", false);
        QQmlComponent component(&engine, QUrl("qrc:/qml/Main.qml"));
        QObject *window = component.create(context);
        if (!window) {
            qWarning("fairfax: couldn't open another window: %s", qPrintable(component.errorString()));
            delete extra;
            return;
        }
        extraBackends.append(extra);
        saveWindows();
        QObject::connect(extra, &Backend::viewChanged, extra, saveWindows);
        // Closing the window releases its backend.
        QObject::connect(qobject_cast<QQuickWindow *>(window), &QQuickWindow::visibleChanged, extra,
                         [window, extra, &extraBackends, &quitting, saveWindows](bool shown) {
            if (!shown) {
                if (!quitting) {
                    extraBackends.removeAll(extra);
                    saveWindows();
                }
                window->deleteLater();
                extra->deleteLater();
            }
        });
        QObject::connect(extra, &Backend::newWindowRequested, &app, [&openWindow] { openWindow({}); });
    };
    QObject::connect(&backend, &Backend::newWindowRequested, &app, [&openWindow] { openWindow({}); });
    if (opened && !offscreen && !cli.isSet(quickOption) && !cli.isSet(daemonOption)) {
        const QStringList remembered = store.setting("windows.extra").toString().split('\n', Qt::SkipEmptyParts);
        QTimer::singleShot(0, &app, [&openWindow, remembered] {
            for (const auto &key : remembered)
                openWindow(key);
        });
    }

    // On Hyprland, fit the main window into a free third when the workspace
    // is laid out with floating windows (see hyprland.h). Each time it
    // appears, once it's mapped. Hyprland only exists on Linux.
#if defined(Q_OS_LINUX)
    if (window && !offscreen && Hyprland::available()) {
        QObject::connect(window, &QWindow::visibleChanged, window, [window, &store](bool shown) {
            if (!shown || !store.setting("omarchy.fitThirds", true).toBool())
                return;
            QTimer::singleShot(250, window, [window] {
                Hyprland::fitIntoFreeThird(QCoreApplication::applicationPid(), window->title());
            });
        });
        if (window->isVisible())
            QTimer::singleShot(400, window, [window] { Hyprland::fitIntoFreeThird(QCoreApplication::applicationPid(), window->title()); });
    }
#endif

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
            // Screenshots of menus and dialogs: run an expression in the
            // window's own scope, then capture it or a window it opened.
            if (cli.isSet(evalOption) || cli.isSet(shotWindowOption)) {
                if (cli.isSet(evalOption)) {
                    QQmlExpression expr(qmlContext(window), window, cli.value(evalOption));
                    expr.evaluate();
                    if (expr.hasError())
                        qWarning().noquote() << "shot-eval:" << expr.error().toString();
                }
                QTimer::singleShot(500, &app, [&] {
                    QQuickWindow *target = window;
                    if (cli.isSet(shotWindowOption))
                        for (auto *w : QGuiApplication::topLevelWindows())
                            if (auto *qw = qobject_cast<QQuickWindow *>(w); qw && qw->isVisible() && qw != window
                                && qw->title().contains(cli.value(shotWindowOption)))
                                target = qw;
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
