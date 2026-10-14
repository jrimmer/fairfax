#pragma once

// The objects behind the QML context properties, and the code that wires them
// together. main() and the end-to-end test harness both build the app through
// this, so the tests drive exactly what ships.

#include <memory>

#include <QString>

#include "backend.h"
#include "caldavsync.h"
#include "calendar.h"
#include "filepicker.h"
#include "foldersync.h"
#include "mailfetch.h"
#include "gitsync.h"
#include "sounds.h"
#include "store.h"
#include "synchub.h"
#include "theme.h"
#include "todoist.h"
#include "webdav.h"

class QQmlEngine;

class AppServices {
public:
    // `themeOverride` is the --theme path (empty for the Omarchy theme).
    AppServices(Store &store, const QString &themeOverride);

    // Creates the sync providers and starts the mail connector. Called after the first-run defaults are in
    // place; offscreen runs (screenshots, demo, tests) have none.
    void startSync(bool opened, bool offscreen);

    // Publishes everything to QML; loading Main.qml is up to the caller.
    void install(QQmlEngine &engine, const QString &storeError, bool startHidden);

    Store &store;
    Theme theme;
    Backend backend;
    Sounds sounds;
    CalendarFeed calendar;
    FilePicker filePicker;
    std::unique_ptr<GitSync> gitSync;
    std::unique_ptr<TodoistSync> todoist;
    std::unique_ptr<FolderSync> folderSync;
    std::unique_ptr<WebDavSync> webDavSync;
    std::unique_ptr<CalDavSync> caldavSync;
    // Always there (the Mail pane works anywhere), but it only connects once
    // started: startSync() does that at launch, tests call fetchNow().
    std::unique_ptr<MailFetch> mail;
    SyncHub syncHub;
};
