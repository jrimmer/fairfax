#pragma once

// Per-task iCalendar VTODO conversion, shared by the whole-file .ics exporter
// and importer in exchange.cpp and by the CalDAV client, which stores one task
// per resource. toIcs() writes a single VTODO with its own VCALENDAR wrapper;
// fromIcs() reads it back. Anything Fairfax does not map is kept in Extra and
// written again, so properties and sub-components belonging to other clients
// (the reference app's X-EXAMPLE-* and <App Meta> notes block, Apple's X-APPLE-*, unknown
// VALARMs) survive a round trip without being interpreted.

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "model.h"

class Store;
struct Task;
struct Alarm;

namespace Vtodo {

// An unfolded iCalendar content line, e.g. "X-EXAMPLE-FOO;X=1:bar".
struct Prop {
    QString name;
    QMap<QString, QString> params;
    QString value;
    QString raw;  // The original line, as read.
};

// What Fairfax does not understand, held aside so it can be written back.
struct Extra {
    QStringList properties;              // Unknown VTODO properties, verbatim content lines.
    QList<QStringList> components;       // Unknown sub-components, each its BEGIN..END lines.
    QList<QStringList> alarmProperties;  // Unknown properties of each mapped VALARM, in order.
    QString meta;                        // The <App Meta> block, without a trailing newline.
    int sequence = -1;                   // The SEQUENCE last seen, or -1 for none.
};

// The mapped fields of one VTODO, before they are resolved against a Store.
// Tag and location names replace the ids the store would need to hand out.
struct Parsed {
    Task task;
    Extra extra;
    QString uid;
    QString parentUid;    // RELATED-TO;RELTYPE=PARENT.
    QString listName;     // X-FAIRFAX-LIST.
    QString locationName; // LOCATION.
    QStringList tagNames; // CATEGORIES, unescaped.
    QList<Alarm> alarms;
};

// How appendTodo() writes one VTODO. The defaults match the whole-file .ics
// exporter; toIcs() turns on `caldav` for TZID dates with a VTIMEZONE, LOCATION,
// URL and the carried-through extras.
struct WriteOptions {
    Extra extra;
    QString parentUid;      // Written as RELATED-TO;RELTYPE=PARENT when set.
    QString stamp;          // DTSTAMP; the current time when empty.
    bool parentIncluded = true;  // Derive RELATED-TO from task.parentId.
    bool caldav = false;
};

// Appends one VTODO to `out`, without a VCALENDAR wrapper, for the whole-file
// exporter. This is the CalDAV-shaped writer with `caldav` off.
void appendTodo(QByteArray &out, const Store &store, const Task &task, const WriteOptions &options = {});

// One VTODO in a VCALENDAR resource. `extra` carries through what fromIcs()
// set aside; an empty Extra writes just the fields Fairfax knows.
QByteArray toIcs(const Store &store, const Task &task, const Extra &extra = {});

// The first VTODO in `ics`, or a default Parsed when there is none.
Parsed fromIcs(const QByteArray &ics);

// Every VTODO in `ics`, in file order, for whole-file imports.
QList<Parsed> allFromIcs(const QByteArray &ics);

// ------------------------------------------------------------------- shared

// The low-level iCalendar helpers, shared with the whole-file importer and
// exporter in exchange.cpp.

QString escapeText(const QString &s);
QString unescapeText(const QString &s);
void appendFolded(QByteArray &out, const QString &line);
QString icsDate(const QDate &d);
QString icsLocal(const QDate &d, int minutes);
QString icsUtc(const QDateTime &dt);
QString triggerText(int minutes);
QString rruleText(const Recurrence &r);

struct IcsDate {
    QDate date;
    int minutes = -1;
    bool valid() const { return date.isValid(); }
};

IcsDate parseIcsDate(const QString &raw, const QString &valueParam = {});
QDateTime parseIcsStamp(const QString &raw);
bool parseDurationSeconds(const QString &raw, qint64 *seconds);
Recurrence parseRrule(const QString &rule);
bool truthy(const QString &v);

}  // namespace Vtodo
