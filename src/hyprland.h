#pragma once

// Fitting the main window into the user's layout on Hyprland. Many Omarchy
// users keep floating windows snapped into thirds of the screen rather than
// tiling; a tiled window there lands underneath them across the full width.
// So when the main window appears: if the workspace's other windows are all
// floating, Precrime floats itself into the first third none of them
// occupies (same geometry as a thirds-snapping script: reserved bar space
// and outer gaps respected). With tiled windows, or none, Hyprland decides.

#include <QString>

namespace Hyprland {

// True inside a Hyprland session.
bool available();

// Places this process's window titled `title` as described above. Returns
// a short description of what it did, for logs and tests.
QString fitIntoFreeThird(qint64 pid, const QString &title);

}  // namespace Hyprland
