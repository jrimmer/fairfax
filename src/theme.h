#pragma once

// Follows the Omarchy theme live. Precrime needs more than the accent: the reference app's
// look leans on semantic colors (overdue red, start-date green, duration
// purple, per-list colors), so the whole colors.toml palette is read and
// mapped onto those roles. Light themes are honoured through `mode`.

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    // "system" follows the Omarchy theme; "dark" or "light" force a scheme,
    // using a built-in palette (with the theme's accent) when the theme is
    // the other kind.
    Q_PROPERTY(QString scheme READ scheme WRITE setScheme NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor sidebar READ sidebar NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor raised READ raised NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor faint READ faint NOTIFY changed)
    Q_PROPERTY(QColor divider READ divider NOTIFY changed)
    Q_PROPERTY(QColor overdue READ overdue NOTIFY changed)
    Q_PROPERTY(QColor today READ today NOTIFY changed)
    Q_PROPERTY(QColor start READ start NOTIFY changed)
    Q_PROPERTY(QColor duration READ duration NOTIFY changed)
    Q_PROPERTY(QColor priorityHigh READ priorityHigh NOTIFY changed)
    Q_PROPERTY(QColor priorityMedium READ priorityMedium NOTIFY changed)
    Q_PROPERTY(QColor priorityLow READ priorityLow NOTIFY changed)
    Q_PROPERTY(QColor star READ star NOTIFY changed)
    Q_PROPERTY(QColor notes READ notes NOTIFY changed)
    Q_PROPERTY(QColor tagBubble READ tagBubble NOTIFY changed)
    Q_PROPERTY(QStringList listPalette READ listPalette NOTIFY changed)
    // The system's fixed-width font; "monospace" only resolves on Linux.
    Q_PROPERTY(QString monoFont READ monoFont CONSTANT)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr);

    bool dark() const { return m_scheme == "dark" ? true : m_scheme == "light" ? false : m_dark; }
    QString scheme() const { return m_scheme; }
    void setScheme(const QString &scheme);
    QColor accent() const { return color("accent"); }
    QColor accentForeground() const;
    QColor background() const { return color("background"); }
    QColor sidebar() const;
    QColor surface() const;
    QColor raised() const;
    QColor selection() const { return color("selection"); }
    QColor foreground() const { return color("foreground"); }
    QColor muted() const { return color("muted"); }
    QColor faint() const;
    QColor divider() const;
    QColor overdue() const { return color("red"); }
    QColor today() const { return color("blue"); }
    QColor start() const { return color("green"); }
    QColor duration() const { return color("magenta"); }
    QColor priorityHigh() const { return color("red"); }
    QColor priorityMedium() const { return color("orange"); }
    QColor priorityLow() const { return color("cyan"); }
    QColor star() const { return color("yellow"); }
    QColor notes() const;
    QColor tagBubble() const;
    QStringList listPalette() const;
    QString monoFont() const;

    Q_INVOKABLE QColor listColor(int index) const;
    Q_INVOKABLE QColor mix(const QColor &a, const QColor &b, double amount) const;

    static QHash<QString, QColor> readColors(const QString &path, bool *dark = nullptr);

signals:
    void changed();

private:
    void reload();
    QColor color(const QString &name) const;

    QString m_directory;
    QHash<QString, QColor> m_colors;
    bool m_dark = true;  // What the Omarchy theme itself is.
    QString m_scheme = "system";
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
