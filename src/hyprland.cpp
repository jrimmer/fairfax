#include "hyprland.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRect>
#include <QVector>
#include <cmath>

namespace {

QJsonDocument hyprctlJson(const QStringList &args) {
    QProcess p;
    p.start("hyprctl", QStringList{"-j"} + args);
    if (!p.waitForFinished(1500))
        return {};
    return QJsonDocument::fromJson(p.readAllStandardOutput());
}

void dispatch(const QString &lua) {
    QProcess::execute("hyprctl", {"dispatch", lua});
}

}  // namespace

namespace Hyprland {

bool available() { return !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"); }

QString fitIntoFreeThird(qint64 pid, const QString &title) {
    if (!available())
        return "not hyprland";
    const QJsonObject workspace = hyprctlJson({"activeworkspace"}).object();
    const int workspaceId = workspace.value("id").toInt(-1);
    const int monitorId = workspace.value("monitorID").toInt(-1);

    QString address;
    bool selfFloating = false;
    QVector<QRect> others;
    for (const auto &value : hyprctlJson({"clients"}).array()) {
        const QJsonObject c = value.toObject();
        if (!c.value("mapped").toBool() || c.value("hidden").toBool())
            continue;
        if (c.value("workspace").toObject().value("id").toInt() != workspaceId)
            continue;
        if (qint64(c.value("pid").toDouble()) == pid) {
            if (c.value("title").toString() == title) {
                address = c.value("address").toString();
                selfFloating = c.value("floating").toBool();
            }
            continue;
        }
        if (!c.value("floating").toBool())
            return "tiled layout: left to Hyprland";
        const QJsonArray at = c.value("at").toArray(), size = c.value("size").toArray();
        others.append(QRect(at.at(0).toInt(), at.at(1).toInt(), size.at(0).toInt(), size.at(1).toInt()));
    }
    if (address.isEmpty())
        return "window not found";
    if (others.isEmpty())
        return "empty workspace: left to Hyprland";

    // Thirds of the monitor's usable area, as the snapping script computes them.
    QJsonObject monitor;
    for (const auto &value : hyprctlJson({"monitors"}).array())
        if (value.toObject().value("id").toInt() == monitorId)
            monitor = value.toObject();
    if (monitor.isEmpty())
        return "monitor not found";
    const double scale = monitor.value("scale").toDouble(1.0);
    const bool rotated = monitor.value("transform").toInt() % 2 == 1;
    const int mw = int(std::floor((rotated ? monitor.value("height") : monitor.value("width")).toInt() / scale));
    const int mh = int(std::floor((rotated ? monitor.value("width") : monitor.value("height")).toInt() / scale));
    const QJsonArray reserved = monitor.value("reserved").toArray();
    const int rl = reserved.at(0).toInt(), rt = reserved.at(1).toInt(), rr = reserved.at(2).toInt(), rb = reserved.at(3).toInt();
    int gap = 0;
    const QJsonObject gaps = hyprctlJson({"getoption", "general:gaps_out"}).object();
    if (gaps.contains("css"))
        gap = gaps.value("css").toString().section(' ', 0, 0).toInt();
    else
        gap = gaps.value("int").toInt();
    const int width = (mw - rl - rr - 4 * gap) / 3;
    const int height = mh - rt - rb - 2 * gap;
    const int mx = monitor.value("x").toInt(), my = monitor.value("y").toInt();

    for (int i = 0; i < 3; ++i) {
        const QRect third(mx + rl + gap + i * (width + gap), my + rt + gap, width, height);
        bool taken = false;
        for (const auto &r : others)
            taken = taken || third.contains(r.center());
        if (taken)
            continue;
        const QString window = "address:" + address;
        if (!selfFloating)
            dispatch(QStringLiteral("hl.dsp.window.float({ window = \"%1\", action = \"toggle\" })").arg(window));
        dispatch(QStringLiteral("hl.dsp.window.resize({ window = \"%1\", x = %2, y = %3 })").arg(window).arg(width).arg(height));
        dispatch(QStringLiteral("hl.dsp.window.move({ window = \"%1\", x = %2, y = %3 })").arg(window).arg(third.x()).arg(third.y()));
        return QStringLiteral("placed in third %1").arg(i);
    }
    return "no free third: left as is";
}

}  // namespace Hyprland
