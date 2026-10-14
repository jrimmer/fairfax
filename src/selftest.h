#pragma once

// `fairfax --demo --selftest`: drives the real window offscreen with a
// scripted series of key presses (navigation, editing, every bare-key
// command and popup) and fails if any QML warning or error is logged.
// Errors in QML handlers only surface when the handler runs, so this is the
// cheapest way to exercise them all without a person at the keyboard.

class QQuickWindow;
class Backend;

namespace SelfTest {

// Installs a message handler counting QML warnings, then runs the script on
// `window`, quitting the app with exit code 0 (clean) or 1 (warnings seen).
void run(QQuickWindow *window, Backend *backend);

}  // namespace SelfTest
