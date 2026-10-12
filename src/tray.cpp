#include "tray.h"

#include <QAction>
#include <QCoreApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

#include "backend.h"
#include "schedule.h"
#include "store.h"
#include "views.h"

namespace {

constexpr int MaxTasks = 12;

ScheduleSettings settingsOf(const Store &store) {
    ScheduleSettings s;
    s.todayIncludesOverdue = store.setting("today.overdue", true).toBool();
    s.todayIncludesStarred = store.setting("today.starred", false).toBool();
    s.todayIncludesStillWorking = store.setting("today.stillWorking", true).toBool();
    s.overdueUsesDueTime = store.setting("tasks.overdueUsesTime", false).toBool();
    return s;
}

}  // namespace

Tray::Tray(Store *store, Backend *backend, QObject *parent) : QObject(parent), m_store(store), m_backend(backend) {
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_menu = new QMenu;
    m_icon = new QSystemTrayIcon(QIcon::fromTheme("precrime", QIcon(":/icon.svg")), this);
    m_icon->setContextMenu(m_menu);
    // A plain click opens Precrime, as tray icons usually do.
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            m_backend->requestWindow();
    });
    connect(m_icon, &QSystemTrayIcon::messageClicked, this, [this] {
        m_backend->requestWindow();
        if (m_messageTask)
            m_backend->revealInList(m_messageTask);
    });
    // The day changes as tasks do and as the clock moves.
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(300);
    connect(&m_refresh, &QTimer::timeout, this, &Tray::rebuild);
    connect(m_store, &Store::changed, &m_refresh, qOverload<>(&QTimer::start));
    connect(m_menu, &QMenu::aboutToShow, this, &Tray::rebuild);
    auto *minute = new QTimer(this);
    connect(minute, &QTimer::timeout, this, &Tray::rebuild);
    minute->start(60 * 1000);
    rebuild();
}

Tray::~Tray() { delete m_menu; }

void Tray::showMessage(const QString &title, const QString &body, qint64 taskId) {
    if (!m_icon)
        return;
    m_messageTask = taskId;
    m_icon->showMessage(title, body, QSystemTrayIcon::Information, 15000);
}

void Tray::setVisible(bool visible) {
    if (m_icon)
        m_icon->setVisible(visible);
}

void Tray::rebuild() {
    if (!m_menu)
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const ScheduleSettings settings = settingsOf(*m_store);
    const ViewBuilder views(*m_store, now, settings);
    ViewOptions options;
    options.completedWindowMinutes = 0;
    const auto sections = views.build(Source{Source::Today, 0}, SortSpec{SortField::DueDate, true, true}, options);
    const ListCounts counts = views.countsForFocus(Source::Today);

    m_menu->clear();
    QString heading = QStringLiteral("Today — ") + QLocale(QLocale::English).toString(now.date(), "ddd d MMM");
    m_menu->addSection(heading);

    int shown = 0, total = 0;
    for (const auto &section : sections) {
        for (const auto &row : section.rows) {
            if (row.depth != 0)
                continue;
            ++total;
            if (shown >= MaxTasks)
                continue;
            const Task t = m_store->task(row.taskId);
            QString label = t.title;
            if (Schedule::isOverdue(t, now, settings))
                label = QStringLiteral("⚠ %1   ·  %2").arg(t.title, Schedule::friendlyDate(t.dueDate, now.date()).toLower());
            else if (t.dueTime >= 0)
                label = QStringLiteral("%1   ·  %2").arg(t.title, Schedule::formatTime(t.dueTime));
            if (t.starred)
                label = "★ " + label;
            label.replace('&', "&&");  // Menus treat & as a mnemonic.
            const qint64 id = t.id;
            m_menu->addAction(label, this, [this, id] { m_backend->requestWindow(); m_backend->revealInList(id); });
            ++shown;
        }
    }
    if (total == 0)
        m_menu->addAction("Nothing left for today")->setEnabled(false);
    else if (total > shown)
        m_menu->addAction(QStringLiteral("… and %1 more").arg(total - shown), this, [this] {
            m_backend->selectSource("today");
            m_backend->requestWindow();
        });

    m_menu->addSeparator();
    m_menu->addAction("New Task…", this, [this] { m_backend->requestQuickEntry(); });
    m_menu->addAction("Open Precrime", this, [this] { m_backend->requestWindow(); });
    m_menu->addAction("Find a Task…", this, [this] { m_backend->requestPanel("open"); });
    if (canSync && canSync())
        m_menu->addAction("Sync Now", this, [this] { if (syncNow) syncNow(); });
    m_menu->addAction("Settings…", this, [this] { m_backend->requestPanel("settings"); });
    m_menu->addSeparator();
    m_menu->addAction("Quit Precrime", qApp, &QCoreApplication::quit);

    QString tip = QStringLiteral("Precrime — %1 today").arg(counts.undone);
    if (counts.overdue)
        tip += QStringLiteral(", %1 overdue").arg(counts.overdue);
    m_icon->setToolTip(tip);
    m_icon->show();
    if (!m_store->setting("general.trayIcon", true).toBool())
        m_icon->hide();
}
