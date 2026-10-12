#pragma once

// A small in-memory log of the warnings the app and its sync and alarm code
// raise, for the Diagnostics pane. It sits in front of Qt's own message
// handler, so nothing stops reaching the terminal or the journal.

#include <QString>
#include <QStringList>

namespace Diagnostics {

// Starts recording warnings and errors; call once, early.
void install();

// Records a line by hand, for failures that never reach qWarning().
void note(const QString &message);

// The most recent lines, oldest first, each stamped with its time.
QStringList recent();

void clear();

}  // namespace Diagnostics
