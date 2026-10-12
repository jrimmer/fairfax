#pragma once

// The typeable Start and Due fields. the reference app's date fields accept a terse grammar
// ("tom mor", "in 3w 3pm", "18d", "w19", "9p45", "eod") and drop down a short
// list of suggestions as you type; this produces that list.

#include <optional>

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

struct DateSuggestion {
    QDate date;        // Invalid when the input names only a time.
    int minutes = -1;  // Minutes after midnight, or -1 when no time was given.
    QString label;     // What the dropdown shows, e.g. "Tomorrow, 9:00 AM" or "Sat, 18 Jul".

    bool operator==(const DateSuggestion &) const = default;
};

namespace DateParse {

// Whether a numeric date such as "9/5" reads day first (5 September) rather
// than month first. Follows the system locale's short date format unless
// forced; locales that cannot say (C/POSIX) read month first.
bool numericDayFirst();

// Forces the order for tests and for a future setting; nullopt follows the locale.
void setNumericDayFirst(std::optional<bool> dayFirst);

// Suggestions for `text`, best first, at most `limit`. `now` is passed in so
// tests are deterministic. Empty or unrecognised input gives an empty list.
QList<DateSuggestion> suggest(const QString &text, const QDateTime &now, int limit = 6);

// The single best reading of `text`, or an invalid suggestion.
DateSuggestion best(const QString &text, const QDateTime &now);

// Parses a time on its own ("742", "2pm", "9p45", "14:30", "1630", "noon"),
// returning minutes after midnight or -1.
int parseTime(const QString &text, const QDateTime &now);

}  // namespace DateParse
