#include <QFile>
#include <QKeySequence>
#include <QSet>
#include <QtTest>
#include <memory>

#include "shortcuts.h"
#include "store.h"
#include "testregistry.h"

namespace {

// A command's default binding is exactly what the overlay and the README say:
// every default must parse as PortableText and round-trip through it.
QString checkRounds(const QString &s) {
    const QKeySequence ks = QKeySequence::fromString(s, QKeySequence::PortableText);
    if (ks.isEmpty() && !s.isEmpty())
        return "does not parse";
    if (ks.toString(QKeySequence::PortableText) != s)
        return "does not round-trip";
    return {};
}

}  // namespace

class ShortcutsTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<Store> m_store;

    std::unique_ptr<Store> open(const QString &name) {
        auto s = std::make_unique<Store>();
        QString err;
        [&] { QVERIFY2(s->open(m_dir.filePath(name + ".db"), &err), qPrintable(err)); }();
        return s;
    }

    static Shortcuts::Context contextOf(const QString &id) { return Shortcuts::command(id)->context; }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        m_store = open("conflicts");
    }

    void idsAreUniqueAndNamed() {
        QSet<QString> ids;
        for (const Shortcuts::Command &c : Shortcuts::commands()) {
            QVERIFY2(!ids.contains(c.id), qPrintable("duplicate id " + c.id));
            ids.insert(c.id);
            QVERIFY2(!c.id.isEmpty(), "a command has no id");
            QVERIFY2(!c.group.isEmpty(), qPrintable("no group for " + c.id));
            QVERIFY2(!c.title.isEmpty(), qPrintable("no title for " + c.id));
            QVERIFY2(c.id.contains('.'), qPrintable("not a dotted id: " + c.id));
        }
        QVERIFY(ids.size() > 100);
        QVERIFY(Shortcuts::groups().contains("Task list (bare keys)"));
        QVERIFY(Shortcuts::groups().contains("New & edit"));
        QVERIFY(Shortcuts::groups().contains("View"));
        QVERIFY(Shortcuts::groups().contains("Quick Entry"));
    }

    void defaultsRoundTrip() {
        for (const Shortcuts::Command &c : Shortcuts::commands()) {
            QVERIFY2(!c.defaults.isEmpty(), qPrintable("no default for " + c.id));
            for (const QString &s : c.defaults)
                QVERIFY2(checkRounds(s).isEmpty(), qPrintable(c.id + ": " + s + " " + checkRounds(s)));
        }
    }

    void defaultsDoNotConflict() {
        // conflict() skips fixed commands: their keys are refused by validate(),
        // and a fixed default may share a key with a window-scoped one (as
        // Ctrl+Return finishes the editor and saves Quick Entry).
        for (const Shortcuts::Command &c : Shortcuts::commands())
            for (const QString &s : c.defaults) {
                const QString other = Shortcuts::conflict(*m_store, c.id, s);
                QVERIFY2(other.isEmpty(),
                         qPrintable(QString("%1 (%2) default %3 conflicts with %4")
                                        .arg(c.id, Shortcuts::contextName(c.context), s, other)));
            }
    }

    void bindingLifecycleThroughStore() {
        auto s = open("lifecycle");
        const QString id = "task.new";
        // Absent means the defaults.
        QCOMPARE(Shortcuts::current(*s, id), Shortcuts::command(id)->defaults);
        // A set binding overrides them.
        Shortcuts::set(*s, id, {"Ctrl+Shift+Y"});
        QCOMPARE(Shortcuts::current(*s, id), QStringList{"Ctrl+Shift+Y"});
        // Several keys are kept as a list.
        Shortcuts::set(*s, id, {"Ctrl+Shift+Y", "Ctrl+Alt+Y"});
        QCOMPARE(Shortcuts::current(*s, id), (QStringList{"Ctrl+Shift+Y", "Ctrl+Alt+Y"}));
        // An empty set is "Not set", not the defaults.
        Shortcuts::set(*s, id, {});
        QVERIFY(Shortcuts::current(*s, id).isEmpty());
        // Reset removes the key, bringing the defaults back.
        Shortcuts::reset(*s, id);
        QCOMPARE(Shortcuts::current(*s, id), Shortcuts::command(id)->defaults);
        // Reset all clears every override.
        Shortcuts::set(*s, "view.today", {"Ctrl+Shift+1"});
        Shortcuts::set(*s, "list.dueToday", {"g"});
        Shortcuts::resetAll(*s);
        QCOMPARE(Shortcuts::current(*s, id), Shortcuts::command(id)->defaults);
        QCOMPARE(Shortcuts::current(*s, "view.today"), Shortcuts::command("view.today")->defaults);
        QCOMPARE(Shortcuts::current(*s, "list.dueToday"), Shortcuts::command("list.dueToday")->defaults);
    }

    void bindingSurvivesReopen() {
        const QString path = m_dir.filePath("reopen.db");
        {
            auto s = std::make_unique<Store>();
            QString err;
            QVERIFY2(s->open(path, &err), qPrintable(err));
            Shortcuts::set(*s, "task.new", {"Ctrl+Shift+Y"});
            Shortcuts::set(*s, "list.dueToday", {});
        }
        auto s = std::make_unique<Store>();
        QString err;
        QVERIFY2(s->open(path, &err), qPrintable(err));
        QCOMPARE(Shortcuts::current(*s, "task.new"), QStringList{"Ctrl+Shift+Y"});
        QVERIFY(Shortcuts::current(*s, "list.dueToday").isEmpty());
        QCOMPARE(Shortcuts::current(*s, "view.today"), Shortcuts::command("view.today")->defaults);
    }

    void conflictRules() {
        auto s = open("conflict");
        // App against App.
        QCOMPARE(Shortcuts::conflict(*s, "task.new", "Ctrl+D"), QStringLiteral("task.duplicate"));
        // App against List (an application shortcut fires with the list focused).
        QCOMPARE(Shortcuts::conflict(*s, "task.new", "T"), QStringLiteral("list.dueToday"));
        // App against Quick Entry.
        QCOMPARE(Shortcuts::conflict(*s, "task.new", "Ctrl+W"), QStringLiteral("quickEntry.hide"));
        // List against List.
        QCOMPARE(Shortcuts::conflict(*s, "list.dueToday", "Y"), QStringLiteral("list.dueTomorrow"));
        // List against App.
        QCOMPARE(Shortcuts::conflict(*s, "list.dueToday", "Ctrl+N"), QStringLiteral("task.new"));
        // List against Quick Entry: no overlap.
        QVERIFY(Shortcuts::conflict(*s, "list.dueToday", "Ctrl+W").isEmpty());
        // Quick Entry against List: no overlap.
        QVERIFY(Shortcuts::conflict(*s, "quickEntry.hide", "T").isEmpty());
        // The command itself is not a conflict.
        QVERIFY(Shortcuts::conflict(*s, "task.new", "Ctrl+N").isEmpty());
    }

    void validateRules() {
        // A bare key is refused for App, allowed for List.
        QVERIFY(!Shortcuts::validate(Shortcuts::Context::App, "Y").isEmpty());
        QVERIFY(Shortcuts::validate(Shortcuts::Context::List, "Y").isEmpty());
        // F5 and Ctrl+Y are fine for App.
        QVERIFY(Shortcuts::validate(Shortcuts::Context::App, "F5").isEmpty());
        QVERIFY(Shortcuts::validate(Shortcuts::Context::App, "Ctrl+Y").isEmpty());
        // Quick Entry needs the same guard as App.
        QVERIFY(!Shortcuts::validate(Shortcuts::Context::QuickEntry, "Y").isEmpty());
        QVERIFY(Shortcuts::validate(Shortcuts::Context::QuickEntry, "Ctrl+Y").isEmpty());
        // Fixed keys are refused, in every context.
        for (auto context : {Shortcuts::Context::App, Shortcuts::Context::List, Shortcuts::Context::QuickEntry}) {
            QVERIFY(!Shortcuts::validate(context, "Return").isEmpty());
            QVERIFY(!Shortcuts::validate(context, "Escape").isEmpty());
            QVERIFY(!Shortcuts::validate(context, "Up").isEmpty());
            QVERIFY(!Shortcuts::validate(context, "Ctrl+A").isEmpty());
        }
        // An empty sequence is refused.
        QVERIFY(!Shortcuts::validate(Shortcuts::Context::List, "").isEmpty());
    }

    void sequenceCanonicalisation() {
        // Shifted punctuation is the shifted character Qt reports.
        QCOMPARE(Shortcuts::sequenceFor(Qt::Key_BraceRight, Qt::ShiftModifier), QStringLiteral("Shift+}"));
        QCOMPARE(Shortcuts::sequenceFor(Qt::Key_BraceLeft, Qt::ShiftModifier), QStringLiteral("Shift+{"));
        // A keypad digit is the plain digit.
        QCOMPARE(Shortcuts::sequenceFor(Qt::Key_5, Qt::KeypadModifier), QStringLiteral("5"));
        // A bare modifier is nothing.
        QCOMPARE(Shortcuts::sequenceFor(Qt::Key_Shift, Qt::ShiftModifier), QString());
        // Ctrl+Alt+N is written as the README does.
        QCOMPARE(Shortcuts::sequenceFor(Qt::Key_N, Qt::ControlModifier | Qt::AltModifier), QStringLiteral("Ctrl+Alt+N"));
        // A plain letter is upper case, as PortableText writes it.
        QCOMPARE(Shortcuts::sequenceFor(Qt::Key_T, Qt::NoModifier), QStringLiteral("T"));
    }

    void qmlHasNoLiteralAppSequences() {
#ifdef FAIRFAX_ROOT
        const QString root = QStringLiteral(FAIRFAX_ROOT);
        const QString main = read(QString(root + "/src/qml/Main.qml"));
        const QString taskPane = read(QString(root + "/src/qml/TaskPane.qml"));
        const QString quick = read(QString(root + "/src/qml/QuickEntry.qml"));
        QVERIFY2(!main.isEmpty() && !taskPane.isEmpty() && !quick.isEmpty(), "a QML file could not be read");
        // Every AppShortcut takes its sequences from the registry; a literal
        // sequence would freeze a default in place.
        QVERIFY2(!main.contains("sequence:"), "Main.qml still has a literal sequence");
        // Every command id is named where its context acts. Fixed commands
        // are the hard-coded keys in TaskPane and the editor, so they have no
        // id in the QML.
        QStringList missing;
        for (const Shortcuts::Command &c : Shortcuts::commands()) {
            if (c.fixed)
                continue;
            const QString &where = c.context == Shortcuts::Context::List ? taskPane
                                   : c.context == Shortcuts::Context::QuickEntry ? quick
                                                                                 : main;
            if (!where.contains("\"" + c.id + "\""))
                missing << c.id;
        }
        QVERIFY2(missing.isEmpty(), qPrintable("QML does not mention: " + missing.join(", ")));
#else
        QSKIP("FAIRFAX_ROOT not defined");
#endif
    }

private:
    static QString read(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
};

REGISTER_TEST(ShortcutsTest)

#include "test_shortcuts.moc"
