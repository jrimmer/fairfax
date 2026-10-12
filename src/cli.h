#pragma once

// Terminal capture: `precrime add …` and the `pc` shorthand.
//
//   pc Call Sam tomorrow 3pm #family !!    add a task (natural language)
//   pc                                    what's on Today, numbered
//   pc ls [all|starred|scheduled|LIST]    another list
//   pc done 12 15                         complete tasks by number
//   pc find dentist                       search everything (the reference app syntax works)
//   pc open                               bring up the app
//
// Changes land in the database the app uses; an open window shows them
// within a couple of seconds.

#include <QStringList>
#include <QTextStream>

class Store;

namespace Cli {

int add(Store &store, const QStringList &words, QTextStream &out, QTextStream &err);
int pc(Store &store, const QStringList &args, QTextStream &out, QTextStream &err);

}  // namespace Cli
