#pragma once

// Terminal capture: `precrime add …` and the `pc` shorthand.
//
//   pc Call Sam tomorrow 3pm #family !!    add a task (natural language)
//   pc                                    what's on Today, numbered
//   pc ls [all|starred|scheduled|LIST]    another list
//   pc done 12 15                         complete tasks by number
//   pc find dentist                       search everything (the reference app syntax works)
//   pc open                               bring up the app
//   precrime import-mail [FILE.eml …]     tasks from emails (see mailtask.h)
//
// Changes land in the database the app uses; an open window shows them
// within a couple of seconds.

#include <QIODevice>
#include <QStringList>
#include <QTextStream>

class Store;

namespace Cli {

int add(Store &store, const QStringList &words, QTextStream &out, QTextStream &err);
// `precrime import-mail [--list NAME] [--natural-language] [FILE …]` with the
// arguments after "import-mail": one message from `in` when no file is given,
// or `--maildir DIR` for every message in DIR/new, moved to DIR/cur as seen.
// Prints a line per message. Returns 0 when everything was added or was a
// duplicate, 1 on an unreadable message or a store error, 2 on bad usage.
int importMail(Store &store, const QStringList &args, QIODevice &in, QTextStream &out, QTextStream &err);
int pc(Store &store, const QStringList &args, QTextStream &out, QTextStream &err);

}  // namespace Cli
