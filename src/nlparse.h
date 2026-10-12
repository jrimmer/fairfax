#pragma once

// Natural-language input for new tasks, after the reference app 2026.3.4: details typed into
// a title ("Call Sam tomorrow at 3pm #family !! every Monday [30m]") fill the
// matching fields and come out of the title. Quick Entry and the editor use
// the spans to draw the recognised words as capsules while you type.

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "model.h"

struct ParsedEntry {
    struct Span {
        int start = 0;
        int length = 0;
        QString kind;  // "date", "start", "time", "repeat", "alert", "duration", "priority", "star", "tag", "list", "location"
        bool operator==(const Span &) const = default;
    };

    QString title;  // The input with every recognised phrase removed and whitespace tidied.
    QDate startDate;
    int startTime = -1;
    QDate dueDate;
    int dueTime = -1;
    int duration = 0;   // Minutes.
    int priority = -1;  // -1 when not given, else 1–3.
    bool starred = false;
    QStringList tags;   // Without the '#'.
    QString list;       // The matched name from `listNames`, or empty.
    QString location;   // The matched name from `locationNames`, or empty.
    Recurrence recurrence;
    QList<int> alertOffsets;  // Minutes before the due moment.
    QList<Span> spans;        // Positions in the original text.
};

namespace NaturalLanguage {

ParsedEntry parse(const QString &text, const QDateTime &now, const QStringList &listNames = {},
                  const QStringList &locationNames = {});

}  // namespace NaturalLanguage
