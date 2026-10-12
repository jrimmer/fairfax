#pragma once

// Repeating tasks, following the reference app's rules: completing an occurrence keeps a
// completed copy and moves the task to its next occurrence; "from completion"
// restarts the cycle from the day it was done; the gap between start and due
// is preserved; and an end rule makes the last occurrence simply complete.

#include <optional>

#include <QDate>
#include <QString>

#include "model.h"

namespace Recurrences {

// The first occurrence strictly after `anchor` for `rule`.
QDate next(const Recurrence &rule, const QDate &anchor);

// The task moved to its next occurrence after being completed on
// `completedOn`, or nothing when the end rule says this was the last one.
// The returned task is not completed and has occurrencesDone incremented.
std::optional<Task> advance(const Task &task, const QDate &completedOn);

// A short summary for the editor and Quick Look, such as "Daily",
// "Every 2 weeks on Mon, Wed", "Monthly on the last Fri", "Yearly · after completion".
QString describe(const Recurrence &rule);

}  // namespace Recurrences
