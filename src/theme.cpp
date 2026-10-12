// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStyleHints>
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

// The built-in light palette, after the reference app's light look, for forcing Light on a
// dark Omarchy theme.
const QHash<QString, QColor> &lightColors() {
    static const QHash<QString, QColor> colors{
        {"selection", QColor("#DCE6F2")},  {"muted", QColor("#7B848C")},
        {"background", QColor("#FFFFFF")}, {"dark_background", QColor("#EEF0F2")},
        {"lighter_background", QColor("#F3F4F6")}, {"foreground", QColor("#1E2328")},
        {"red", QColor("#C2453B")},        {"yellow", QColor("#D9A21B")},
        {"orange", QColor("#E8892B")},     {"green", QColor("#3E9A4A")},
        {"cyan", QColor("#2A9DB0")},       {"blue", QColor("#2F7FD8")},
        {"magenta", QColor("#7A62C7")},    {"brown", QColor("#9A7656")},
        {"bright_red", QColor("#D9665C")}, {"bright_yellow", QColor("#E8B83A")},
        {"bright_green", QColor("#5DB36A")}, {"bright_cyan", QColor("#4CB4C6")},
        {"bright_blue", QColor("#5C9BE2")}, {"bright_magenta", QColor("#9682D6")},
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
    // Without an Omarchy theme (macOS, Windows, other desktops), System
    // follows the operating system's light or dark setting.
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        connect(app->styleHints(), &QStyleHints::colorSchemeChanged, this, &Theme::reload);
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
    // A forced scheme that differs from the theme uses the built-in palette
    // for that scheme, keeping only the theme's accent. With no theme at all
    // the built-in palette for the current scheme is used throughout.
    if (m_colors.isEmpty() && !dark() && name != "accent")
        return lightColors().value(name, fallbackColors().value(name, QColor("#888888")));
    if (dark() != m_dark && name != "accent") {
        const auto &palette = dark() ? fallbackColors() : lightColors();
        return palette.value(name, fallbackColors().value(name, QColor("#888888")));
    }
    const auto it = m_colors.constFind(name);
    return it != m_colors.cend() ? *it : fallbackColors().value(name, QColor("#888888"));
}

void Theme::setScheme(const QString &scheme) {
    const QString next = scheme == "dark" || scheme == "light" ? scheme : QStringLiteral("system");
    if (next == m_scheme)
        return;
    m_scheme = next;
    emit changed();
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
    if (dark() != m_dark || m_colors.contains("dark_background"))
        return color("dark_background");
    return mix(background(), dark() ? QColor(Qt::black) : QColor("#808080"), dark() ? 0.25 : 0.12);
}

// The editor card and Quick Entry panel sit just above the background.
QColor Theme::surface() const {
    if (dark() != m_dark || m_colors.contains("lighter_background"))
        return mix(background(), color("lighter_background"), 0.6);
    return mix(background(), foreground(), 0.05);
}

QColor Theme::raised() const {
    if (dark() != m_dark || m_colors.contains("lighter_background"))
        return color("lighter_background");
    return mix(background(), foreground(), 0.10);
}

QColor Theme::faint() const { return mix(background(), muted(), 0.55); }
QColor Theme::divider() const { return mix(background(), foreground(), dark() ? 0.09 : 0.11); }

// the reference app's notes area is a pale cream; here it is the background warmed toward
// the theme's yellow.
QColor Theme::notes() const { return mix(surface(), color("yellow"), dark() ? 0.05 : 0.10); }
QColor Theme::tagBubble() const { return mix(background(), color("blue"), dark() ? 0.16 : 0.13); }

QString Theme::monoFont() const { return QFontDatabase::systemFont(QFontDatabase::FixedFont).family(); }

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
    if (colors.isEmpty() && qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    if (colors != m_colors || dark != m_dark) {
        m_colors = colors;
        m_dark = dark;
        emit changed();
    }
}
