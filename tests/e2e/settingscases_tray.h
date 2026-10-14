#pragma once

// Rows of the settings table for the tray menu group in the General pane.
// Included by settingskit.h once SettingCase and the helpers exist; not a
// header to use on its own.

#include "tray.h"

namespace e2e {

// The tray menu, as the store's settings build it right now.
inline QList<TrayEntry> trayEntries(Store &store) {
    return TrayMenu::entries(store, QDateTime::currentDateTime(), true);
}

inline QString trayHeading(Store &store) {
    const QList<TrayEntry> list = trayEntries(store);
    return list.isEmpty() ? QString() : list.first().label;
}

inline int trayTaskCount(Store &store) {
    int n = 0;
    for (const TrayEntry &e : trayEntries(store))
        if (e.kind == TrayEntry::Task)
            ++n;
    return n;
}

inline QStringList trayCommands(Store &store) {
    QStringList out;
    for (const TrayEntry &e : trayEntries(store))
        if (e.kind == TrayEntry::Command)
            out << e.command;
    return out;
}

inline void addTrayCases(QVector<SettingCase> &v) {
    using K = SettingCase;
    {
        K c{"tray.source", "General", "setting:tray.source", K::Combo};
        c.fromIndex = 0;  // Today
        c.toIndex = 1;    // Starred
        c.stored = "starred";
        c.effect = SK_EFFECT {
            SK_APP
            Q_UNUSED(app)
            QCOMPARE(trayHeading(s.app->store()), changed ? QStringLiteral("Starred") : QStringLiteral("Today — ") + QLocale(QLocale::English).toString(QDate::currentDate(), "ddd d MMM"));
        };
        v << c;
    }
    {
        K c{"tray.count", "General", "setting:tray.count", K::Spin};
        c.fromValue = 12;
        c.toValue = 3;
        c.stored = "3";
        c.prepare = [](Store &st, Seed &ids) {
            for (int i = 0; i < 5; ++i)
                addTask(st, ids.home, QString("Tray task %1").arg(i), [](Task &t) { t.dueDate = QDate::currentDate(); });
        };
        c.effect = SK_EFFECT {
            SK_APP
            Q_UNUSED(app)
            QCOMPARE(trayTaskCount(s.app->store()), changed ? 3 : 7);
        };
        v << c;
    }
    {
        K c{"tray.click", "General", "setting:tray.click", K::Combo};
        c.fromIndex = 0;  // Opens it in Fairfax
        c.toIndex = 1;    // Completes it
        c.stored = "complete";
        c.effect = SK_EFFECT {
            SK_APP
            focusMain(app);
            Store &st = app.store();
            Tray tray(&st, &app.backend());
            TrayEntry task;
            for (const TrayEntry &e : TrayMenu::entries(st, QDateTime::currentDateTime(), true))
                if (e.kind == TrayEntry::Task) {
                    task = e;
                    break;
                }
            QVERIFY2(task.taskId, "no today task in the tray menu");
            QVERIFY(!st.task(task.taskId).completed);
            tray.activate(task);
            QTRY_COMPARE_WITH_TIMEOUT(st.task(task.taskId).completed, changed, 5000);
            if (changed) {
                // Ctrl+Z in the window undoes the tray's completion.
                QTRY_VERIFY_WITH_TIMEOUT(app.backend().canUndo(), 5000);
                app.press(Qt::Key_Z, Qt::ControlModifier);
                QTRY_VERIFY_WITH_TIMEOUT(!st.task(task.taskId).completed, 5000);
            }
        };
        v << c;
    }
    // The one "tray.commands" key behind five boxes; the combined row also
    // covers the key for the settings guard.
    {
        K c{"tray.commands", "General", "setting:tray.commands.new", K::Check, true};
        c.stored = "open,find,sync,settings";
        c.effect = SK_EFFECT {
            SK_APP
            Q_UNUSED(app)
            QCOMPARE(trayCommands(s.app->store()).contains("new"), !changed);
        };
        v << c;
    }
    const QList<QPair<QString, QString>> boxes{
        {"open", "new,find,sync,settings"},
        {"find", "new,open,sync,settings"},
        {"sync", "new,open,find,settings"},
        {"settings", "new,open,find,sync"},
    };
    for (const auto &box : boxes) {
        K c{"tray.commands", "General", QString("setting:tray.commands.") + box.first, K::Check, true};
        c.stored = box.second;
        const QString command = box.first;
        c.effect = SK_EFFECT {
            SK_APP
            Q_UNUSED(app)
            QCOMPARE(trayCommands(s.app->store()).contains(command), !changed);
        };
        v << c;
    }
    // The swatch opens the colour picker, which the dedicated test drives.
    {
        K c{"tray.badgeColor", "General", "setting:tray.badgeColor", K::Check, true};
        c.stored = "*";  // The swatch opens the colour picker; a dedicated test drives it.
        c.slot = "E2ESettings::badgeColourIsPicked";
        v << c;
    }
}

}  // namespace e2e
