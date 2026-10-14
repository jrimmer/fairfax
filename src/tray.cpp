#include "tray.h"

#include <QAction>
#include <QCoreApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

#include "backend.h"
#include "schedule.h"
#include "store.h"
#include "traymenu.h"
#include "views.h"

namespace {

ScheduleSettings settingsOf(const Store &store) { return ScheduleSettings::fromStore(store); }

}  // namespace

Tray::Tray(Store *store, Backend *backend, QObject *parent) : QObject(parent), m_store(store), m_backend(backend) {
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_menu = new QMenu;
    m_icon = new QSystemTrayIcon(QIcon::fromTheme("fairfax", QIcon(":/icon.svg")), this);
    m_icon->setContextMenu(m_menu);
    // A plain click opens Fairfax, as tray icons usually do.
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
    const ViewBuilder views(*m_store, now, settingsOf(*m_store));

    m_menu->clear();
    for (const TrayEntry &entry : TrayMenu::entries(*m_store, now, canSync && canSync())) {
        switch (entry.kind) {
        case TrayEntry::Section:
            m_menu->addSection(entry.label);
            break;
        case TrayEntry::Task:
            m_menu->addAction(entry.label, this, [this, entry] { activate(entry); });
            break;
        case TrayEntry::Info:
            m_menu->addAction(entry.label)->setEnabled(entry.enabled);
            break;
        case TrayEntry::More:
            m_menu->addAction(entry.label, this, [this, entry] { activate(entry); });
            break;
        case TrayEntry::Separator:
            m_menu->addSeparator();
            break;
        case TrayEntry::Command:
            m_menu->addAction(entry.label, this, [this, entry] { activate(entry); });
            break;
        }
    }

    // The "Badge shows" setting picks the number drawn on the icon; the
    // "Badge colour" setting picks its shade.
    const int badge = views.badgeCount(m_store->setting("alerts.badge", "both").toString());
    m_icon->setIcon(TrayMenu::badgeIcon(badge, TrayMenu::badgeColour(*m_store)));
    const ListCounts counts = views.countsForFocus(Source::Today);
    QString tip = QStringLiteral("Fairfax — %1 today").arg(counts.undone);
    if (counts.overdue)
        tip += QStringLiteral(", %1 overdue").arg(counts.overdue);
    m_icon->setToolTip(tip);
    m_icon->show();
    if (!m_store->setting("general.trayIcon", true).toBool())
        m_icon->hide();
}

void Tray::activate(const TrayEntry &entry) {
    if (entry.kind == TrayEntry::Task) {
        const QDateTime now = QDateTime::currentDateTime();
        const Task t = m_store->task(entry.taskId);
        if (!t.id)
            return;
        if (m_store->setting("tray.click", "open").toString() == "complete") {
            m_backend->toggleComplete(entry.taskId);
            showMessage(QStringLiteral("Completed: %1").arg(t.title), QString(), entry.taskId);
        } else {
            m_backend->requestWindow();
            m_backend->revealInList(entry.taskId);
        }
        return;
    }
    if (entry.kind == TrayEntry::More) {
        m_backend->selectSource(entry.sourceKey);
        m_backend->requestWindow();
        return;
    }
    if (entry.command == "new")
        m_backend->requestQuickEntry();
    else if (entry.command == "open")
        m_backend->requestWindow();
    else if (entry.command == "find")
        m_backend->requestPanel("open");
    else if (entry.command == "sync") {
        if (syncNow)
            syncNow();
    } else if (entry.command == "settings")
        m_backend->requestPanel("settings");
    else if (entry.command == "quit")
        QCoreApplication::quit();
}
