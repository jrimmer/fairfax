#pragma once

// The tray menu's content, decided without a system tray so it can be tested
// offscreen: the quick view chosen by "tray.source", capped by "tray.count",
// followed by the commands "tray.commands" keeps. Tray::rebuild turns these
// into QActions and Tray::activate carries them out.

#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QList>
#include <QString>

class Store;

struct TrayEntry {
    enum Kind { Section, Task, Info, More, Separator, Command };
    Kind kind = Section;
    QString label;
    qint64 taskId = 0;
    QString command;     // Command: "new", "open", "find", "sync", "settings", "quit"; More: "open-source".
    bool enabled = true;
    QString sourceKey;   // More: the source "… and N more" opens.

    bool operator==(const TrayEntry &) const = default;
};

namespace TrayMenu {

// The entries for the store's current settings, at `now`. `canSync` says
// whether a sync provider is set up (the Sync command is dropped when not).
QList<TrayEntry> entries(const Store &store, const QDateTime &now, bool canSync);

// The colour the unfinished-count badge is drawn in, as the "tray.badgeColor"
// setting holds it.
QColor badgeColour(const Store &store);

// The app icon with a count badge in `colour`; the plain icon for a count of
// zero or less. `alerts.badge` chooses the count, this the colour.
QIcon badgeIcon(int count, const QColor &colour);

}  // namespace TrayMenu
