#pragma once

// Plain records for everything Precrime stores. They mirror the reference app's own model:
// lists live in groups, tasks live in lists, projects and checklists are tasks
// with children, and tags live in tag groups. Each record converts to and from
// a QVariantMap so the store and the undo journal can treat them uniformly.

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace TaskType {
enum { Task = 0, Project = 1, Checklist = 2 };
}

namespace Priority {
enum { None = 0, Low = 1, Medium = 2, High = 3 };
}

// the reference app's action types, in the order of its ⌥1–⌥6 presets.
namespace ActionType {
enum { None = 0, Call = 1, Message = 2, Mail = 3, Visit = 4, Url = 5, Google = 6 };
}

namespace Repeat {
enum Frequency { None = 0, Daily = 1, Weekly = 2, Monthly = 3, Yearly = 4, Weekdays = 5 };
enum From { FromDue = 0, FromCompletion = 1 };
enum End { Never = 0, AfterCount = 1, OnDate = 2 };
}

// How a list orders its tasks; the names follow the reference app's sort menu.
namespace SortField {
enum {
    DueDate = 0,
    StartDate = 1,
    Priority = 2,
    List = 3,
    Title = 4,
    Creation = 5,
    Modified = 6,
    Duration = 7,
    Manual = 8,
    Tag = 9,
    Completion = 10,
    Status = 11,
    Section = 12,
    Note = 13,
    Location = 14,
};
}

struct Recurrence {
    int frequency = Repeat::None;
    int interval = 1;
    int from = Repeat::FromDue;
    int weekdays = 0;      // Bit 0 is Monday … bit 6 is Sunday; used by weekly rules.
    int monthOrdinal = 0;  // 1–5, or -1 for "last"; 0 means "same day of month".
    int monthWeekday = 0;  // 1 (Monday) … 7 (Sunday) when monthOrdinal is set.
    int endType = Repeat::Never;
    int endCount = 0;
    QDate endDate;
    int occurrencesDone = 0;

    bool isSet() const { return frequency != Repeat::None; }
    bool operator==(const Recurrence &) const = default;
};

struct Task {
    qint64 id = 0;
    QString uid;
    qint64 listId = 0;
    qint64 parentId = 0;
    qint64 sectionId = 0;  // A heading within the list (the reference app's task sections, TickTick's columns).
    int type = TaskType::Task;
    QString title;
    QString notes;
    int priority = Priority::None;
    bool starred = false;
    QDate startDate;
    int startTime = -1;  // Minutes after midnight, or -1 for a date-only start.
    QDate dueDate;
    int dueTime = -1;    // Minutes after midnight, or -1 for a date-only deadline.
    int duration = 0;    // Minutes.
    bool completed = false;
    QDateTime completedAt;
    int actionType = ActionType::None;
    QString actionValue;
    Recurrence recurrence;
    QString seriesUid;   // Shared by a repeating task and the completed copies it leaves behind.
    double order = 0;
    bool collapsed = false;
    QList<qint64> tagIds;
    QList<qint64> locationIds;
    QDateTime created;
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static Task fromMap(const QVariantMap &map);
};

struct ListGroup {
    qint64 id = 0;
    QString uid;
    QString name;
    double order = 0;
    bool collapsed = false;
    int color = -1;           // Palette index its lists take on ("Color grouped lists"); -1 for none.
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static ListGroup fromMap(const QVariantMap &map);
};

namespace ListKind {
enum { Normal = 0, Inbox = 1, Smart = 2 };
}

// TaskList.color is a palette index (below 64) or, with the flag set, a custom
// 0xRRGGBB picked in the colour dialog.
namespace ListColor {
constexpr int CustomFlag = 0x1000000;
inline bool isCustom(int color) { return color >= CustomFlag; }
inline int custom(unsigned rgb) { return CustomFlag | int(rgb & 0xFFFFFF); }
inline unsigned rgb(int color) { return unsigned(color) & 0xFFFFFF; }
}  // namespace ListColor

struct TaskList {
    qint64 id = 0;
    QString uid;
    int kind = ListKind::Normal;
    qint64 groupId = 0;
    QString name;
    int color = 0;            // Index into the theme's list palette.
    double order = 0;
    int sortField = SortField::DueDate;
    bool sortAscending = true;
    bool separated = true;    // the reference app's "Separated" switch: section headers on or off.
    bool showInAll = true;
    bool showInToday = true;
    bool showInDone = true;
    bool archived = false;
    QString smartQuery;
    // A Smart List's date filter, after the reference app's Search Options: "" (none),
    // "next:N:unit" with unit d/w/m/y ("In the next 3 days"),
    // "fixed:yyyy-MM-dd:yyyy-MM-dd", or "nodue".
    QString smartRange;
    // Whether completed tasks show here: the list default, always, or never.
    // Normal lists default to showing them (subject to the completed-window
    // setting); Smart Lists default to hiding them, as in the reference app.
    int completedMode = 0;    // 0 default, 1 include, 2 exclude.
    bool includeScheduled = false;  // Smart Lists: also match scheduled tasks.
    bool muteAlerts = false;  // Alerts for this list's tasks stay silent.
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static TaskList fromMap(const QVariantMap &map);
};

struct TagGroup {
    qint64 id = 0;
    QString uid;
    QString name;
    double order = 0;
    bool collapsed = false;
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static TagGroup fromMap(const QVariantMap &map);
};

struct Tag {
    qint64 id = 0;
    QString uid;
    qint64 groupId = 0;
    QString name;
    bool paused = false;
    double order = 0;
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static Tag fromMap(const QVariantMap &map);
};

// An alert on a task, after the reference app's alarms: relative to the due moment (an
// offset in minutes before it), a number of days before the due date at a
// time of day, or at a fixed date and time.
struct Alarm {
    enum Kind { Relative = 0, Absolute = 1, DaysBefore = 2 };

    qint64 id = 0;
    QString uid;
    qint64 taskId = 0;
    int kind = Relative;
    int offset = 0;           // Minutes before the due moment (Relative), or whole days before the date (DaysBefore).
    QDateTime at;             // The moment for Absolute alarms; only the time of day counts for DaysBefore.
    bool sound = true;        // "Message with Sound" rather than "Message".
    QDateTime snoozedUntil;   // A snoozed alarm fires again at this moment.
    QDateTime firedFor;       // The fire moment it last went off for, so a moved due date re-arms it.
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static Alarm fromMap(const QVariantMap &map);
};

// A saved place, after the reference app's Locations: tasks can carry one or more, and a
// location can be paused like a tag, hiding its tasks.
struct Location {
    qint64 id = 0;
    QString uid;
    QString name;
    QString address;
    double latitude = 0;
    double longitude = 0;
    bool paused = false;
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static Location fromMap(const QVariantMap &map);
};

// A picture, audio note or file kept with a task. The file itself lives in
// the attachments folder beside the database, named by the attachment's uid.
struct Attachment {
    enum Kind { Image = 0, Audio = 1, File = 2 };

    qint64 id = 0;
    QString uid;
    qint64 taskId = 0;
    int kind = File;
    QString name;       // The original file name, for display.
    QString fileName;   // <uid>.<ext> inside the attachments folder.
    qint64 bytes = 0;
    QDateTime created;
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static Attachment fromMap(const QVariantMap &map);
};

// A heading inside a list that groups its tasks, after the reference app's task sections
// (and TickTick's sections).
struct ListSection {
    qint64 id = 0;
    QString uid;
    qint64 listId = 0;
    QString name;
    double order = 0;
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static ListSection fromMap(const QVariantMap &map);
};

