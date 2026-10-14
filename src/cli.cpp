#include "cli.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include "mailtask.h"
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

ScheduleSettings settingsOf(const Store &store) { return ScheduleSettings::fromStore(store); }

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
        err << "usage: frfx <task, e.g. Call Sam tomorrow 3pm #family !!>" << Qt::endl;
        return 2;
    }
    Automation automation(store);
    const QJsonObject r = automation.run("create_task", {{"title", text}, {"natural_language", true}});
    if (!r.value("ok").toBool()) {
        err << "fairfax: " << r.value("error").toObject().value("message").toString() << Qt::endl;
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

int importMail(Store &store, const QStringList &args, QIODevice &in, QTextStream &out, QTextStream &err) {
    MailTask::Options options;
    QString listName, maildir;
    QStringList files;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == "--natural-language") {
            options.naturalLanguage = true;
        } else if (a == "--list" || a == "--maildir") {
            if (i + 1 >= args.size()) {
                err << "fairfax import-mail: " << a << " needs a value" << Qt::endl;
                return 2;
            }
            (a == "--list" ? listName : maildir) = args.at(++i);
        } else if (a.startsWith("--") && a.size() > 2) {
            err << "fairfax import-mail: unknown option " << a << Qt::endl;
            return 2;
        } else {
            files << a;
        }
    }
    if (!maildir.isEmpty() && !files.isEmpty()) {
        err << "fairfax import-mail: give message files or --maildir, not both" << Qt::endl;
        return 2;
    }
    if (!listName.isEmpty()) {
        for (const auto &l : store.lists())
            if (!l.deleted && !l.archived && l.kind != ListKind::Smart && l.name.compare(listName, Qt::CaseInsensitive) == 0)
                options.listId = l.id;
        if (!options.listId) {
            err << "fairfax import-mail: no list called \u201c" << listName << "\u201d" << Qt::endl;
            return 2;
        }
    }

    int status = 0;
    // Imports one message and prints its line; false when it could not be read or stored.
    const auto handle = [&](const QByteArray &bytes, const QString &source) {
        const MailTask::Message message = MailTask::parse(bytes);
        if (!message.valid) {
            err << "fairfax: " << source << ": not an email message" << Qt::endl;
            return false;
        }
        const MailTask::Result r = MailTask::import(store, message, options);
        if (r.ok()) {
            const Task task = store.task(r.taskId);
            out << "Added " << paint(task.title, "1") << " to " << store.list(task.listId).name << Qt::endl;
        } else if (r.reason == "duplicate" || r.reason == "empty") {
            out << "Skipped: " << r.reason << Qt::endl;
        } else {
            err << "fairfax: " << source << ": there is no list to put the task in; open Fairfax once to set up the database" << Qt::endl;
            return false;
        }
        return true;
    };

    if (!maildir.isEmpty()) {
        // A Maildir delivers into new/; reading a message moves it to cur/ with the Seen flag.
        const QDir fresh(maildir + "/new");
        if (!fresh.exists()) {
            err << "fairfax: " << maildir << " has no new/ folder" << Qt::endl;
            return 1;
        }
        QDir().mkpath(maildir + "/cur");
        for (const QString &name : fresh.entryList(QDir::Files, QDir::Name)) {
            if (name.startsWith('.'))
                continue;
            QFile file(fresh.filePath(name));
            if (!file.open(QIODevice::ReadOnly)) {
                err << "fairfax: can't read " << file.fileName() << Qt::endl;
                status = 1;
                continue;
            }
            const QByteArray bytes = file.readAll();
            file.close();
            if (!handle(bytes, file.fileName())) {
                status = 1;
                continue;
            }
            const QString seen = maildir + "/cur/" + name.section(':', 0, 0) + ":2,S";
            if (!QFile::rename(file.fileName(), seen)) {
                err << "fairfax: can't move " << file.fileName() << " to " << seen << Qt::endl;
                status = 1;
            }
        }
        return status;
    }
    if (files.isEmpty()) {
        if (!handle(in.readAll(), QStringLiteral("stdin")))
            status = 1;
        return status;
    }
    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            err << "fairfax: can't read " << path << Qt::endl;
            status = 1;
        } else if (!handle(file.readAll(), path)) {
            status = 1;
        }
    }
    return status;
}

int shorthand(Store &store, const QStringList &args, QTextStream &out, QTextStream &err) {
    const QString command = args.value(0).toLower();
    if (args.isEmpty() || command == "today")
        return show(store, Source{Source::Today, 0}, {}, out);
    if (command == "help" || command == "-h" || command == "--help") {
        out << "frfx <task>             add a task: frfx Call Sam tomorrow 3pm #family !!\n"
               "frfx                    Today, numbered\n"
               "frfx ls [all|starred|scheduled|done|<list>]\n"
               "frfx done <n> [<n>…]    complete tasks by number\n"
               "frfx find <words>       search everything (the reference app search syntax works)\n"
               "frfx open               open Fairfax\n"
               "frfx add <task>         add a task that starts with one of these words\n";
        return 0;
    }
    if (command == "add")
        return add(store, args.mid(1), out, err);
    if (command == "open") {
        QProcess::startDetached("fairfax", {});
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
        err << "frfx: no list called “" << which << "”" << Qt::endl;
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
                err << "frfx: no task #" << a << Qt::endl;
                return 1;
            }
            ids.append(QString::number(id));
        }
        if (ids.isEmpty()) {
            err << "usage: frfx " << command << " <n> [<n>…]   (numbers from frfx or frfx ls)" << Qt::endl;
            return 2;
        }
        Automation automation(store);
        const QJsonObject r = automation.run(command == "done" ? "complete_task" : "uncomplete_task", {{"task_ids", ids}});
        if (!r.value("ok").toBool()) {
            err << "fairfax: " << r.value("error").toObject().value("message").toString() << Qt::endl;
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
