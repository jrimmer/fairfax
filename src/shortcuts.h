#pragma once

// The keyboard command registry: every shortcut the app has, its default
// keys, the context it acts in, and where the user's own binding is kept.
//
// A command's binding lives in the settings table under `shortcuts.<id>` as a
// PortableText list, so Ctrl is written "Ctrl" and maps to Cmd on macOS. A
// missing key means the defaults; an empty string means the command is
// unbound. `sequenceFor` turns a key event into the same canonical string, so
// the task list can look a pressed key up in a sequence -> id map.

#include <QList>
#include <QString>
#include <QStringList>

class Store;

namespace Shortcuts {

enum class Context {
    App,        // Main.qml's application shortcuts.
    List,       // TaskPane's bare keys, active while the task list has focus.
    QuickEntry  // The Quick Entry window.
};

struct Command {
    QString id;
    QString group;
    QString title;
    QStringList defaults;
    Context context = Context::App;
    bool fixed = false;  // Not remappable: navigation, editing and clipboard keys.
};

// Every command, in the order the Settings pane and the overlay show them.
const QList<Command> &commands();

// The group headings, in order.
const QStringList &groups();

// The command with this id, or null.
const Command *command(const QString &id);

QString contextName(Context context);
Context contextFromName(const QString &name, bool *ok = nullptr);

// The canonical string for a key event, in PortableText. A bare modifier key
// (Shift, Control, Alt, Meta) gives an empty string.
QString sequenceFor(int key, int modifiers);

// A binding list as the platform writes it: QKeySequence::NativeText, so
// macOS shows ⌘/⌥ where Linux and Windows show Ctrl/Alt. On Linux the result
// is the same portable text the defaults are written in. Entries are joined
// with " / ".
QString nativeText(const QStringList &sequences);

// The current binding: the stored one when there is one, else the defaults.
QStringList current(const Store &store, const QString &id);
void set(Store &store, const QString &id, const QStringList &sequences);
void reset(Store &store, const QString &id);       // Removes the stored key.
void resetAll(Store &store);

// The id of another command whose binding includes `sequence` in a context
// that overlaps this one, or an empty string.
QString conflict(const Store &store, const QString &id, const QString &sequence);

// Why `sequence` may not be bound to a command in `context`, or an empty
// string when it may.
QString validate(Context context, const QString &sequence);

}  // namespace Shortcuts
