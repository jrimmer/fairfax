// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>

namespace {

// Used for any key a theme leaves out, and wholesale when Omarchy isn't
// installed. Dark, with the Omarchy fallback accent.
const QHash<QString, QColor> &fallbackColors() {
    static const QHash<QString, QColor> colors{
        {"accent", QColor("#FFD60A")},     {"selection", QColor("#2E3A46")},
        {"muted", QColor("#8A949C")},      {"background", QColor("#16191D")},
        {"dark_background", QColor("#111316")}, {"lighter_background", QColor("#22272D")},
        {"foreground", QColor("#E6E2DA")}, {"red", QColor("#E5786D")},
        {"yellow", QColor("#E8C66A")},     {"orange", QColor("#EFA05A")},
        {"green", QColor("#9CC48A")},      {"cyan", QColor("#7CC4CC")},
        {"blue", QColor("#78AEE0")},       {"magenta", QColor("#B9A0D4")},
        {"brown", QColor("#B39478")},      {"bright_red", QColor("#F79A90")},
        {"bright_yellow", QColor("#F5DB97")}, {"bright_green", QColor("#BAD9AA")},
        {"bright_cyan", QColor("#A2DDE3")}, {"bright_blue", QColor("#9CC9F0")},
        {"bright_magenta", QColor("#D4BDE8")},
    };
    return colors;
}

// The list palette walks the theme's hues in the order the reference app's own palette
// does: blue first, then purple, green, red, orange and the rest.
const QStringList PaletteKeys{"blue", "magenta", "green", "red", "orange", "cyan", "yellow", "brown",
                              "bright_blue", "bright_magenta", "bright_green", "bright_red", "bright_cyan"};

}  // namespace

Theme::Theme(const QString &directory, QObject *parent)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    reload();
}

QHash<QString, QColor> Theme::readColors(const QString &path, bool *dark) {
    QHash<QString, QColor> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    const QString text = QString::fromUtf8(file.readAll());
    static const QRegularExpression entry(R"re(^\s*([a-z0-9_]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                          QRegularExpression::MultilineOption);
    for (auto it = entry.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        colors.insert(m.captured(1), QColor(m.captured(2)));
    }
    static const QRegularExpression mode(R"re(^\s*mode\s*=\s*["'](\w+)["'])re", QRegularExpression::MultilineOption);
    if (dark) {
        const auto m = mode.match(text);
        if (m.hasMatch())
            *dark = m.captured(1) != "light";
        else if (colors.contains("background"))
            *dark = colors.value("background").lightnessF() < 0.5;
    }
    return colors;
}

QColor Theme::color(const QString &name) const {
    const auto it = m_colors.constFind(name);
    return it != m_colors.cend() ? *it : fallbackColors().value(name, QColor("#888888"));
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QColor Theme::accentForeground() const {
    const QColor c = accent();
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? QColor(Qt::black) : QColor(Qt::white);
}

QColor Theme::mix(const QColor &a, const QColor &b, double t) const {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

// the reference app's sidebar is a shade off the content area; dark themes go darker, light
// themes a touch grayer.
QColor Theme::sidebar() const {
    if (m_colors.contains("dark_background"))
        return color("dark_background");
    return mix(background(), m_dark ? QColor(Qt::black) : QColor("#808080"), m_dark ? 0.25 : 0.12);
}

// The editor card and Quick Entry panel sit just above the background.
QColor Theme::surface() const {
    if (m_colors.contains("lighter_background"))
        return mix(background(), color("lighter_background"), 0.6);
    return mix(background(), foreground(), 0.05);
}

QColor Theme::raised() const {
    if (m_colors.contains("lighter_background"))
        return color("lighter_background");
    return mix(background(), foreground(), 0.10);
}

QColor Theme::faint() const { return mix(background(), muted(), 0.55); }
QColor Theme::divider() const { return mix(background(), foreground(), m_dark ? 0.09 : 0.11); }

// the reference app's notes area is a pale cream; here it is the background warmed toward
// the theme's yellow.
QColor Theme::notes() const { return mix(surface(), color("yellow"), m_dark ? 0.05 : 0.10); }
QColor Theme::tagBubble() const { return mix(background(), color("blue"), m_dark ? 0.16 : 0.13); }

QStringList Theme::listPalette() const {
    QStringList names;
    for (const auto &key : PaletteKeys)
        names << color(key).name();
    return names;
}

QColor Theme::listColor(int index) const {
    return color(PaletteKeys.at(((index % PaletteKeys.size()) + PaletteKeys.size()) % PaletteKeys.size()));
}

void Theme::reload() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    bool dark = true;
    const auto colors = readColors(m_directory + "/theme/colors.toml", &dark);
    if (colors != m_colors || dark != m_dark) {
        m_colors = colors;
        m_dark = dark;
        emit changed();
    }
}
