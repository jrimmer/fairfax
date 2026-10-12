#pragma once

// The icon in the Omarchy bar's tray (a StatusNotifierItem, through Qt's
// system tray support). Its menu is a quick view of the day, overdue first,
// each task opening in the app, followed by the everyday commands: New Task,
// Open, Find, Sync and Quit. It's there because Precrime keeps running in
// the background for alarms; Settings → General turns it off.

#include <QObject>
#include <QTimer>
#include <functional>

class QMenu;
class QSystemTrayIcon;
class Store;
class Backend;

class Tray : public QObject {
    Q_OBJECT

public:
    Tray(Store *store, Backend *backend, QObject *parent = nullptr);
    ~Tray() override;

    // Called for "Sync Now"; empty when no sync is set up.
    std::function<void()> syncNow;
    std::function<bool()> canSync;

    void setVisible(bool visible);
    // A notification from the tray icon, for alarms where the desktop has
    // no freedesktop notification service; clicking it opens the task.
    void showMessage(const QString &title, const QString &body, qint64 taskId);

private:
    void rebuild();

    Store *m_store;
    Backend *m_backend;
    QSystemTrayIcon *m_icon = nullptr;
    QMenu *m_menu = nullptr;
    QTimer m_refresh;
    qint64 m_messageTask = 0;
};
