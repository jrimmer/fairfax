#pragma once

// Rows of the settings table for the General and Appearance panes. Included by settingskit.h once
// SettingCase and the helpers exist; not a header to use on its own.

namespace e2e {

inline void addGeneralAndAppearanceCases(QVector<SettingCase> &v) {
    using K = SettingCase;
        // ----------------------------------------------------------- General
        {
            K c{"general.keepRunning", "General", "setting:general.keepRunning", K::Check, true};
            c.outside = "main() reads it once at launch to decide whether closing the last window quits";
            v << c;
        }
        {
            K c{"quickEntry.clipboard", "General", "setting:quickEntry.clipboard", K::Check, true};
            // "The clipboard's text lands in the notes" (QuickEntry.qml reset()).
            c.effect = SK_EFFECT {
                SK_APP
                const QString clip = "pasted from the clipboard";
                QGuiApplication::clipboard()->setText(clip);
                QQuickWindow *quick = nullptr;
                openQuickEntry(app, quick);
                QVERIFY(quick);
                QQuickItem *notes = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((notes = findItem("quickNotes")) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(notes->property("text").toString(), changed ? QString() : clip, 5000);
                closeQuickEntry(quick);
            };
            v << c;
        }
        {
            K c{"omarchy.fitThirds", "General", "setting:omarchy.fitThirds", K::Check, true};
            c.outside = "only main() on a live Hyprland session reads it, to place the window";
            v << c;
        }
        {
            K c{"general.trayIcon", "General", "setting:general.trayIcon", K::Check, true};
            c.outside = "the tray icon is built by main(), which the harness does not run";
            v << c;
        }
        {
            K c{"general.weekStart", "General", "setting:general.weekStart", K::Combo};
            c.fromIndex = 0;
            c.toIndex = 3;  // Saturday: never what Automatic resolves to.
            c.stored = "6";
            // "the mini calendar" starts its week on the chosen day.
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                const QDate saturday = dateOnWeekday(6);
                if (changed) {
                    QTRY_COMPARE_WITH_TIMEOUT(calendarColumn(saturday), 0, 5000);
                    QCOMPARE(app.backend().firstDayOfWeek(), 6);
                } else {
                    QTRY_VERIFY_WITH_TIMEOUT(calendarColumn(saturday) > 0, 5000);
                }
            };
            v << c;
        }
        {
            K c{"general.sounds", "General", "setting:general.sounds", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                QTRY_COMPARE_WITH_TIMEOUT(app.services().sounds.enabled(), !changed, 5000);
            };
            v << c;
        }
        {
            K c{"general.celebrate", "General", "setting:general.celebrate", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                QQuickItem *confetti = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((confetti = findItem("confetti")) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(confetti->property("enabled").toBool(), !changed, 5000);
            };
            v << c;
        }

        // -------------------------------------------------------- Appearance
        for (const char *scheme : {"dark", "light"}) {
            K c{"appearance.scheme", "Appearance", QString("setting:appearance.scheme:") + scheme, K::Choice};
            c.revert = "setting:appearance.scheme:system";
            c.stored = scheme;
            const bool dark = QString(scheme) == "dark";
            c.effect = SK_EFFECT {
                SK_APP
                Theme &theme = app.services().theme;
                if (!changed) {
                    QTRY_COMPARE_WITH_TIMEOUT(theme.scheme(), QString("system"), 5000);
                    return;
                }
                QTRY_COMPARE_WITH_TIMEOUT(theme.dark(), dark, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(app.window->color(), theme.background(), 5000);
                QVERIFY2((app.window->color().lightness() < 128) == dark, "the window is the wrong shade");
            };
            v << c;
        }
        {
            K c{"appearance.textSize", "Appearance", "setting:appearance.textSize", K::SliderK};
            c.fromValue = 2;
            c.toValue = 6;
            c.stored = "6";
            // The font size changes the row's text and its height.
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                const int size = changed ? 19 : 14;
                QQuickItem *title = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((title = rowTitle(app, s.ids.buyMilk)) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(title->property("font").value<QFont>().pixelSize(), size, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(int(app.row(s.ids.buyMilk)->height()), qRound(size * 2.5), 5000);
            };
            v << c;
        }
        {
            K c{"appearance.contentWidth", "Appearance", "setting:appearance.contentWidth", K::SliderK};
            c.fromValue = 980;
            c.toValue = 640;
            c.stored = "640";
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                widenPane(app);
                QQuickItem *row = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((row = app.row(s.ids.buyMilk)) != nullptr, 5000);
                const qreal expected = qMax<qreal>(28, (row->width() - (changed ? 640 : 980)) / 2);
                QTRY_COMPARE_WITH_TIMEOUT(row->property("margin").toReal(), expected, 5000);
                if (changed)
                    QVERIFY2(expected > 28, "the window is too narrow to tell the widths apart");
            };
            v << c;
        }
        for (int density : {0, 2}) {
            K c{"appearance.density", "Appearance", "setting:appearance.density", K::Combo};
            c.fromIndex = 1;
            c.toIndex = density;
            c.stored = QString::number(density);
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                const int mode = changed ? density : 1;
                static const double factor[] = {2.05, 2.5, 2.8};
                QQuickItem *row = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((row = app.row(s.ids.buyMilk)) != nullptr, 5000);
                QTRY_COMPARE_WITH_TIMEOUT(int(row->height()), qRound(14 * factor[mode]), 5000);
            };
            v << c;
        }
        for (int style : {1, 2}) {
            K c{"appearance.calendarStyle", "Appearance", "setting:appearance.calendarStyle", K::Combo};
            c.fromIndex = 0;  // Heat Map
            c.toIndex = style;
            c.stored = style == 1 ? "dots" : "none";
            // "Busy Days": heat map tint, dots under the date, or nothing.
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                QQuickItem *cal = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((cal = findItem("miniCalendar")) != nullptr, 5000);
                const QString want = changed ? (style == 1 ? "dots" : "none") : "heat";
                QTRY_COMPARE_WITH_TIMEOUT(cal->property("style").toString(), want, 5000);
                if (want == "dots")
                    QTRY_VERIFY_WITH_TIMEOUT(countNamed("calDots") > 0, 5000);
                else
                    QTRY_COMPARE_WITH_TIMEOUT(countNamed("calDots"), 0, 5000);
            };
            v << c;
        }
        {
            K c{"appearance.monthDividers", "Appearance", "setting:appearance.monthDividers", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                focusMain(app);
                if (changed)
                    QTRY_COMPARE_WITH_TIMEOUT(countNamed("calDividerRight") + countNamed("calDividerBottom"), 0, 5000);
                else
                    QTRY_VERIFY_WITH_TIMEOUT(countNamed("calDividerRight") + countNamed("calDividerBottom") > 0, 5000);
            };
            v << c;
        }
        {
            K c{"appearance.friendlyDates", "Appearance", "setting:appearance.friendlyDates", K::Check, true};
            // "Use friendly dates (Today, Tomorrow, In 28 days)".
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.buyMilk) != nullptr, 5000);
                if (changed)
                    QTRY_VERIFY_WITH_TIMEOUT(!textUnder(app.row(s.ids.buyMilk), "Today"), 5000);
                else
                    QTRY_VERIFY_WITH_TIMEOUT(textUnder(app.row(s.ids.buyMilk), "Today") != nullptr, 5000);
            };
            v << c;
        }
        {
            K c{"appearance.notesPreview", "Appearance", "setting:appearance.notesPreview", K::Check, false};
            c.prepare = [](Store &st, Seed &ids) { editTask(st, ids.buyMilk, [](Task &t) { t.notes = "Two cartons of oat milk"; }); };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.buyMilk) != nullptr, 5000);
                const QRegularExpression note("oat milk");
                if (changed)
                    QTRY_VERIFY_WITH_TIMEOUT(textMatching(app.row(s.ids.buyMilk), note) != nullptr, 5000);
                else
                    QTRY_VERIFY_WITH_TIMEOUT(!textMatching(app.row(s.ids.buyMilk), note), 5000);
            };
            v << c;
        }
        {
            K c{"appearance.listNames", "Appearance", "setting:appearance.listNames", K::Check, false};
            // Shown in the Focus lists, where tasks from several lists meet.
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, "all");
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.buyMilk) != nullptr, 5000);
                if (changed)
                    QTRY_VERIFY_WITH_TIMEOUT(textUnder(app.row(s.ids.buyMilk), "Home") != nullptr, 5000);
                else
                    QTRY_VERIFY_WITH_TIMEOUT(!textUnder(app.row(s.ids.buyMilk), "Home"), 5000);
            };
            v << c;
        }
        {
            K c{"appearance.tagNames", "Appearance", "setting:appearance.tagNames", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                widenPane(app);
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.plants) != nullptr, 5000);
                if (changed)
                    QTRY_VERIFY_WITH_TIMEOUT(!textUnder(app.row(s.ids.plants), "garden"), 5000);
                else
                    QTRY_VERIFY_WITH_TIMEOUT(textUnder(app.row(s.ids.plants), "garden") != nullptr, 5000);
            };
            v << c;
        }
        {
            K c{"appearance.overdueRed", "Appearance", "setting:appearance.overdueRed", K::Check, true};
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                QQuickItem *title = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT((title = rowTitle(app, s.ids.electricity)) != nullptr, 5000);
                Theme &theme = app.services().theme;
                if (changed)
                    QTRY_COMPARE_WITH_TIMEOUT(colourOf(title), QColor(theme.foreground()), 5000);
                else
                    QTRY_COMPARE_WITH_TIMEOUT(colourOf(title), QColor(theme.overdue()), 5000);
            };
            v << c;
        }
        {
            K c{"appearance.headingDurations", "Appearance", "setting:appearance.headingDurations", K::Check, true};
            c.prepare = [](Store &st, Seed &ids) { editTask(st, ids.buyMilk, [](Task &t) { t.duration = 30; }); };
            c.effect = SK_EFFECT {
                SK_APP
                showSource(app, listKey(s.ids.home));
                QTRY_VERIFY_WITH_TIMEOUT(app.row(s.ids.buyMilk) != nullptr, 5000);
                if (changed)
                    QTRY_COMPARE_WITH_TIMEOUT(countNamed("sectionDuration"), 0, 5000);
                else
                    QTRY_VERIFY_WITH_TIMEOUT(countNamed("sectionDuration") > 0, 5000);
            };
            v << c;
        }
}

}  // namespace e2e
