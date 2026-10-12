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
    QDateTime modified;
    bool deleted = false;

    QVariantMap toMap() const;
    static ListGroup fromMap(const QVariantMap &map);
};

namespace ListKind {
enum { Normal = 0, Inbox = 1, Smart = 2 };
}

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
