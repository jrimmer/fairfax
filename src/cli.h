#pragma once

// Terminal capture: `fairfax add …` and the `frfx` shorthand.
//
//   frfx Call Sam tomorrow 3pm #family !!  add a task (natural language)
//   frfx                                  what's on Today, numbered
//   frfx ls [all|starred|scheduled|LIST]  another list
//   frfx done 12 15                       complete tasks by number
//   frfx find dentist                     search everything (search syntax works)
//   frfx open                             bring up the app
//   fairfax import-mail [FILE.eml …]      tasks from emails (see mailtask.h)
//   fairfax ctl myday                     open My Day in the running window
//
// Changes land in the database the app uses; an open window shows them
// within a couple of seconds.

#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTextStream>

class Store;

namespace Cli {

int add(Store &store, const QStringList &words, QTextStream &out, QTextStream &err);
// `fairfax import-mail [--list NAME] [--natural-language] [FILE …]` with the
// arguments after "import-mail": one message from `in` when no file is given,
// or `--maildir DIR` for every message in DIR/new, moved to DIR/cur as seen.
// Prints a line per message. Returns 0 when everything was added or was a
// duplicate, 1 on an unreadable message or a store error, 2 on bad usage.
int importMail(Store &store, const QStringList &args, QIODevice &in, QTextStream &out, QTextStream &err);
// `fairfax import FILE` (a task database, .ics, or another app's export), with
// `file` the path. Like Settings > Import & Export it takes a "before-import"
// backup first, through Store::takeBackup, and prints its name after the
// usual "Imported ..." line. The undo history lives in the running app's
// memory, so the import can't be undone with Ctrl+Z from here; the output says
// to restore the backup in Settings > Backups instead. A failed import removes
// its backup again, since nothing changed. Returns 0, or 1 on any failure.
int importFile(Store &store, const QString &file, QTextStream &out, QTextStream &err);
// True when the program was started under the shorthand name: `frfx` on
// Linux and macOS (usually a symlink); on Windows also `frfx.exe`, with
// backslash paths and any letter case. `windowsStyle` picks the Windows
// rules; it defaults to the platform being built for.
#if defined(Q_OS_WIN)
constexpr bool kWindowsNames = true;
#else
constexpr bool kWindowsNames = false;
#endif
bool isShorthandName(const QString &argv0, bool windowsStyle = kWindowsNames);
int shorthand(Store &store, const QStringList &args, QTextStream &out, QTextStream &err);

// The local socket the running window listens on, one per user and database.
// `dbKey` is the raw --db argument main() was given (empty for the default
// database), because the running instance hashes exactly that.
QString socketName(const QString &dbKey);

// Sends one request to the running window ("myday", "show", "open", …).
// Returns false with `error` set when no instance is listening, so a headless
// `fairfax ctl myday` can say so instead of doing nothing.
bool sendToRunning(const QString &dbKey, const QString &request, QString *error = nullptr);

}  // namespace Cli
