#include "traymenu.h"

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QPixmap>

#include "model.h"
#include "schedule.h"
#include "store.h"
#include "views.h"

namespace {

const QColor DefaultBadgeColour(0xE5, 0x48, 0x4D);

// The badge sits in the bottom-right corner of the icon, drawn over whichever
// base icon the theme provides.
QIcon paintBadge(int count, const QColor &colour) {
    const QIcon themed = QIcon::fromTheme("fairfax");
    const QIcon base = themed.isNull() ? QIcon(":/icon.svg") : themed;
    if (count <= 0)
        return base;
    QPixmap pixmap = base.pixmap(64, 64);
    if (pixmap.isNull()) {
        pixmap = QPixmap(64, 64);
        pixmap.fill(Qt::transparent);
    }
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QString text = count > 99 ? QStringLiteral("99+") : QString::number(count);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(text.size() > 2 ? 24 : 30);
    painter.setFont(font);
    const int width = qMax(34, painter.fontMetrics().horizontalAdvance(text) + 14);
    const QRect badge(pixmap.width() - width, pixmap.height() - 34, width, 34);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(badge, 17, 17);
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, text);
    return QIcon(pixmap);
}

// The Source the "tray.source" setting names; a list that has since gone falls
// back to Today.
Source sourceFor(const Store &store, const QString &setting) {
    const Source fallback{Source::Today, 0};
    if (setting == "starred")
        return Source{Source::Starred, 0};
    if (setting == "scheduled")
        return Source{Source::Scheduled, 0};
    if (setting.startsWith("list:")) {
        const qint64 id = setting.mid(5).toLongLong();
        const auto it = store.lists().constFind(id);
        if (id && it != store.lists().cend() && !it->deleted)
            return Source{Source::List, id};
        return fallback;
    }
    return fallback;
}

bool isToday(const Source &source) { return source.kind == Source::Today; }

}  // namespace

namespace TrayMenu {

QIcon badgeIcon(int count, const QColor &colour) { return paintBadge(count, colour); }

QColor badgeColour(const Store &store) {
    const QColor chosen(store.setting("tray.badgeColor", QStringLiteral("#E5484D")).toString());
    return chosen.isValid() ? chosen : DefaultBadgeColour;
}

QList<TrayEntry> entries(const Store &store, const QDateTime &now, bool canSync) {
    const ScheduleSettings settings = ScheduleSettings::fromStore(store);
    const ViewBuilder views(store, now, settings);
    const QString sourceSetting = store.setting("tray.source", "today").toString();

    QList<TrayEntry> out;
    if (sourceSetting != "none") {
        const Source source = sourceFor(store, sourceSetting);
        ViewOptions options;
        options.completedWindowMinutes = 0;
        const auto sections = views.build(source, SortSpec{SortField::DueDate, true, true}, options);

        TrayEntry heading;
        heading.kind = TrayEntry::Section;
        if (isToday(source))
            heading.label = QStringLiteral("Today — ") + QLocale(QLocale::English).toString(now.date(), "ddd d MMM");
        else
            heading.label = views.sourceTitle(source);
        out.append(heading);

        const int cap = qBound(3, store.setting("tray.count", 12).toInt(), 30);
        int shown = 0, total = 0;
        for (const auto &section : sections) {
            for (const auto &row : section.rows) {
                if (row.depth != 0)
                    continue;
                ++total;
                if (shown >= cap)
                    continue;
                const Task t = store.task(row.taskId);
                QString label = t.title;
                if (Schedule::isOverdue(t, now, settings))
                    label = QStringLiteral("⚠ %1   ·  %2").arg(t.title, Schedule::friendlyDate(t.dueDate, now.date()).toLower());
                else if (t.dueTime >= 0)
                    label = QStringLiteral("%1   ·  %2").arg(t.title, Schedule::formatTime(t.dueTime));
                if (t.starred)
                    label = "★ " + label;
                label.replace('&', "&&");  // Menus treat & as a mnemonic.
                TrayEntry entry;
                entry.kind = TrayEntry::Task;
                entry.label = label;
                entry.taskId = t.id;
                out.append(entry);
                ++shown;
            }
        }
        if (total == 0) {
            TrayEntry info;
            info.kind = TrayEntry::Info;
            info.label = isToday(source) ? QStringLiteral("Nothing left for today") : QStringLiteral("Nothing here");
            info.enabled = false;
            out.append(info);
        } else if (total > shown) {
            TrayEntry more;
            more.kind = TrayEntry::More;
            more.label = QStringLiteral("… and %1 more").arg(total - shown);
            more.sourceKey = source.key();
            out.append(more);
        }

        TrayEntry separator;
        separator.kind = TrayEntry::Separator;
        out.append(separator);
    }

    const QStringList wanted = store.setting("tray.commands", QStringLiteral("new,open,find,sync,settings")).toString().split(',');
    auto has = [&](const QString &name) {
        for (const QString &w : wanted)
            if (w.trimmed() == name)
                return true;
        return false;
    };
    const QList<QPair<QString, QString>> commands{
        {"new", QStringLiteral("New Task…")},      {"open", QStringLiteral("Open Fairfax")},
        {"find", QStringLiteral("Find a Task…")},  {"sync", QStringLiteral("Sync Now")},
        {"settings", QStringLiteral("Settings…")},
    };
    int added = 0;
    for (const auto &command : commands) {
        if (!has(command.first))
            continue;
        if (command.first == "sync" && !canSync)
            continue;
        TrayEntry entry;
        entry.kind = TrayEntry::Command;
        entry.command = command.first;
        entry.label = command.second;
        out.append(entry);
        ++added;
    }
    if (added) {
        TrayEntry separator;
        separator.kind = TrayEntry::Separator;
        out.append(separator);
    }
    TrayEntry quit;
    quit.kind = TrayEntry::Command;
    quit.command = "quit";
    quit.label = QStringLiteral("Quit Fairfax");
    out.append(quit);
    return out;
}

}  // namespace TrayMenu
