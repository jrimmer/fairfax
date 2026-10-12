#include "cli.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <cstdio>
#if defined(Q_OS_WIN)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include "automation.h"
#include "query.h"
#include "schedule.h"
#include "store.h"
#include "views.h"

namespace {

// Colour only when writing to a terminal, so scripts get plain text.
bool colour() { return isatty(fileno(stdout)) && qEnvironmentVariableIsEmpty("NO_COLOR"); }
QString paint(const QString &text, const char *code) {
    return colour() ? QStringLiteral("\x1b[%1m%2\x1b[0m").arg(code, text) : text;
}

ScheduleSettings settingsOf(const Store &store) {
    ScheduleSettings s;
    s.todayIncludesOverdue = store.setting("today.overdue", true).toBool();
    s.todayIncludesStarred = store.setting("today.starred", false).toBool();
    s.todayIncludesStillWorking = store.setting("today.stillWorking", true).toBool();
    s.overdueUsesDueTime = store.setting("tasks.overdueUsesTime", false).toBool();
    return s;
}

// One line per task: number, title, then date, list and markers.
QString line(const Store &store, const Task &t, const QDateTime &now, const ScheduleSettings &s) {
    QString text = paint(QStringLiteral("%1").arg(t.id, 4), "2") + "  ";
    const bool overdue = Schedule::isOverdue(t, now, s);
    QString title = t.title;
    if (t.completed) title = paint("✓ " + title, "2;9");
    else if (overdue) title = paint(title, "31");
    text += title;
    QStringList extra;
    if (t.dueDate.isValid()) {
        const QString due = Schedule::friendlyDateTime(t.dueDate, t.dueTime, now.date());
        extra << paint(due, overdue ? "31" : t.dueDate == now.date() ? "34" : "2");
    }
    if (t.priority > 0) extra << paint(QString(t.priority, '!'), t.priority == 3 ? "31" : "33");
    if (t.starred) extra << paint("★", "33");
    if (t.recurrence.isSet()) extra << paint("↻", "2");
    extra << paint(store.list(t.listId).name, "2");
    return text + "   " + extra.join("  ");
}

int show(Store &store, const Source &source, const QString &search, QTextStream &out) {
    const QDateTime now = QDateTime::currentDateTime();
    const ScheduleSettings s = settingsOf(store);
    const ViewBuilder views(store, now, s);
    ViewOptions options;
    options.search = search;
    options.completedWindowMinutes = 0;
    const SortSpec sort{source.kind == Source::Scheduled ? SortField::StartDate : SortField::DueDate, true, true};
    int shown = 0;
    for (const auto &section : views.build(source, sort, options)) {
        if (section.rows.isEmpty())
            continue;
        out << paint(section.title.toUpper(), section.tone == "overdue" ? "1;31" : "1") << Qt::endl;
        for (const auto &row : section.rows) {
            const Task t = store.task(row.taskId);
            out << QString(row.depth * 4, ' ') << line(store, t, now, s) << Qt::endl;
            ++shown;
        }
    }
    if (!shown)
        out << paint(search.isEmpty() ? "Nothing here." : "No tasks match.", "2") << Qt::endl;
    return 0;
}

}  // namespace

namespace Cli {

int add(Store &store, const QStringList &words, QTextStream &out, QTextStream &err) {
    const QString text = words.join(' ').trimmed();
    if (text.isEmpty()) {
        err << "usage: pc <task, e.g. Call Sam tomorrow 3pm #family !!>" << Qt::endl;
        return 2;
    }
    Automation automation(store);
    const QJsonObject r = automation.run("create_task", {{"title", text}, {"natural_language", true}});
    if (!r.value("ok").toBool()) {
        err << "precrime: " << r.value("error").toObject().value("message").toString() << Qt::endl;
        return 1;
    }
    const QJsonObject task = r.value("task").toObject();
    QStringList details;
    if (!task.value("due_date").toString().isEmpty()) {
        const QDate due = QDate::fromString(task.value("due_date").toString(), Qt::ISODate);
        const QTime time = QTime::fromString(task.value("due_time").toString(), "HH:mm");
        details << "due " + Schedule::friendlyDate(due, QDate::currentDate()).toLower() +
                       (time.isValid() ? " " + Schedule::formatTime(time.hour() * 60 + time.minute()) : QString());
    }
    if (!task.value("repeat").toString().isEmpty()) details << task.value("repeat").toString().toLower();
    if (task.value("priority").toString() != "none" && !task.value("priority").toString().isEmpty())
        details << task.value("priority").toString() + " priority";
    const QJsonArray tags = task.value("tags").toArray();
    for (const auto &tag : tags)
        details << "#" + (tag.isObject() ? tag.toObject().value("name").toString() : tag.toString());
    out << "Added " << paint(task.value("title").toString(), "1") << " to " << task.value("list").toObject().value("name").toString()
        << (details.isEmpty() ? "" : " · " + details.join(", ")) << paint("  (#" + task.value("id").toString() + ")", "2")
        << Qt::endl;
    return 0;
}

int pc(Store &store, const QStringList &args, QTextStream &out, QTextStream &err) {
    const QString command = args.value(0).toLower();
    if (args.isEmpty() || command == "today")
        return show(store, Source{Source::Today, 0}, {}, out);
    if (command == "help" || command == "-h" || command == "--help") {
        out << "pc <task>               add a task: pc Call Sam tomorrow 3pm #family !!\n"
               "pc                      Today, numbered\n"
               "pc ls [all|starred|scheduled|done|<list>]\n"
               "pc done <n> [<n>…]      complete tasks by number\n"
               "pc find <words>         search everything (the reference app search syntax works)\n"
               "pc open                 open Precrime\n"
               "pc add <task>           add a task that starts with one of these words\n";
        return 0;
    }
    if (command == "add")
        return add(store, args.mid(1), out, err);
    if (command == "open") {
        QProcess::startDetached("precrime", {});
        return 0;
    }
    if (command == "ls" || command == "list") {
        const QString which = args.mid(1).join(' ').trimmed();
        static const QHash<QString, int> focus{{"", Source::Today}, {"today", Source::Today}, {"all", Source::All},
                                               {"starred", Source::Starred}, {"scheduled", Source::Scheduled},
                                               {"done", Source::Done}};
        if (focus.contains(which.toLower()))
            return show(store, Source{focus.value(which.toLower()), 0}, {}, out);
        for (const auto &l : store.lists())
            if (!l.deleted && l.name.compare(which, Qt::CaseInsensitive) == 0)
                return show(store, Source{Source::List, l.id}, {}, out);
        err << "pc: no list called “" << which << "”" << Qt::endl;
        return 1;
    }
    if (command == "find" || command == "search")
        return show(store, Source{Source::All, 0}, args.mid(1).join(' '), out);
    if (command == "done" || command == "undone") {
        QJsonArray ids;
        for (const auto &a : args.mid(1)) {
            bool ok = false;
            const qint64 id = QString(a).remove('#').toLongLong(&ok);
            if (!ok || !store.tasks().contains(id)) {
                err << "pc: no task #" << a << Qt::endl;
                return 1;
            }
            ids.append(QString::number(id));
        }
        if (ids.isEmpty()) {
            err << "usage: pc " << command << " <n> [<n>…]   (numbers from pc or pc ls)" << Qt::endl;
            return 2;
        }
        Automation automation(store);
        const QJsonObject r = automation.run(command == "done" ? "complete_task" : "uncomplete_task", {{"task_ids", ids}});
        if (!r.value("ok").toBool()) {
            err << "precrime: " << r.value("error").toObject().value("message").toString() << Qt::endl;
            return 1;
        }
        for (const auto &id : ids)
            out << (command == "done" ? "Completed " : "Reopened ") << paint(store.task(id.toString().toLongLong()).title, "1") << Qt::endl;
        return 0;
    }
    // Anything else is a new task.
    return add(store, args, out, err);
}

}  // namespace Cli
