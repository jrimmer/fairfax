#include "appsetup.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>

#include "markdown.h"
#include "synctree.h"

AppServices::AppServices(Store &store, const QString &themeOverride)
    : store(store), theme(themeOverride), backend(&store)
{
    theme.setScheme(store.setting("appearance.scheme", "system").toString());
    QObject::connect(&backend, &Backend::settingsChanged, &theme,
                     [this] { theme.setScheme(this->store.setting("appearance.scheme", "system").toString()); });
    NotesHighlighter::registerType();
    mail = std::make_unique<MailFetch>(&store);
    // The mail connector reports its own errors as "Mail: ..."; the hub is
    // told not to duplicate them as a sync failure.
    syncHub.add(mail.get(), "Mail", false);
    QObject::connect(mail.get(), &MailFetch::errorOccurred, &backend, [this](const QString &message) {
        backend.screenReaderAnnounceUrgent(QStringLiteral("Mail: %1").arg(message));
    });
    QObject::connect(&syncHub, &SyncHub::syncFailed, &backend, [this](const QString &reason) {
        backend.screenReaderAnnounceUrgent(QStringLiteral("Sync failed: %1").arg(reason));
    });

    sounds.setEnabled(store.setting("general.sounds", true).toBool());
    QObject::connect(&backend, &Backend::settingsChanged, &sounds,
                     [this] { sounds.setEnabled(this->store.setting("general.sounds", true).toBool()); });
    QObject::connect(&backend, &Backend::taskCompleted, &sounds, [this](bool done) { if (done) sounds.complete(); else sounds.uncomplete(); });

    calendar.setPalette(theme.listPalette());
    // My Day's capacity line and next-free-slot need the day's events.
    backend.setCalendar(&calendar);
    // Per-source switches and colours, and the addresses that mark a declined
    // invitation as mine, all set before the sources so the first load is right.
    calendar.setDisabledSources(store.setting("calendar.disabled").toString().split('\n', Qt::SkipEmptyParts));
    calendar.setSourceColors(QJsonDocument::fromJson(store.setting("calendar.colors").toString().toUtf8()).object().toVariantMap());
    calendar.setMyEmails(store.setting("calendar.emails").toString());
    calendar.setSources(store.setting("calendar.sources").toString().split('\n', Qt::SkipEmptyParts));
    QObject::connect(&theme, &Theme::changed, &calendar, [this] { calendar.setPalette(theme.listPalette()); });
}

void AppServices::startSync(bool opened, bool offscreen)
{
    if (!opened || offscreen)
        return;
    QTimer::singleShot(6000, mail.get(), &MailFetch::start);
    // Git sync works on a working copy beside the database; demo and
    // screenshot runs leave it alone.
    gitSync = std::make_unique<GitSync>(&store);
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, gitSync.get(), [this] {
        // One last push so edits made just before quitting reach the remote.
        if (gitSync->configured() && SyncTree::autoSyncEnabled(store))
            gitSync->syncBlocking(8000);
    });
    todoist = std::make_unique<TodoistSync>(&store);
    if (todoist->connected())
        QTimer::singleShot(3000, todoist.get(), &TodoistSync::syncNow);
    folderSync = std::make_unique<FolderSync>(&store);
    webDavSync = std::make_unique<WebDavSync>(&store);
    caldavSync = std::make_unique<CalDavSync>(&store, &theme);
    // The launch sync asks again when it fires, in case automatic sync was
    // switched off in the first few seconds.
    if (SyncTree::autoSyncEnabled(store)) {
        QTimer::singleShot(4000, folderSync.get(), [this] {
            if (SyncTree::autoSyncEnabled(store))
                folderSync->syncNow();
        });
        QTimer::singleShot(5000, webDavSync.get(), [this] {
            if (SyncTree::autoSyncEnabled(store))
                webDavSync->syncNow();
        });
    }
    syncHub.add(gitSync.get(), "Git");
    syncHub.add(todoist.get(), "Todoist");
    syncHub.add(folderSync.get(), "Folder");
    syncHub.add(webDavSync.get(), "WebDAV");
    syncHub.add(caldavSync.get(), "CalDAV");
}

void AppServices::install(QQmlEngine &engine, const QString &storeError, bool startHidden)
{
    QQmlContext *context = engine.rootContext();
    context->setContextProperty("filePicker", &filePicker);
    context->setContextProperty("gitSync", gitSync.get());
    context->setContextProperty("todoist", todoist.get());
    context->setContextProperty("folderSync", folderSync.get());
    context->setContextProperty("webDavSync", webDavSync.get());
    context->setContextProperty("caldavSync", caldavSync.get());
    context->setContextProperty("mailFetch", mail.get());
    context->setContextProperty("syncHub", &syncHub);
    context->setContextProperty("calendar", &calendar);
    context->setContextProperty("sounds", &sounds);
    context->setContextProperty("theme", &theme);
    context->setContextProperty("backend", &backend);
    context->setContextProperty("storeError", storeError);
    context->setContextProperty("startHidden", startHidden);
}
