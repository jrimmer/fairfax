#include "selftest.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <functional>

#include "backend.h"

namespace {

int warnings = 0;
QtMessageHandler previous = nullptr;

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    // Count QML problems (they carry a qrc: location or a JS error name);
    // pass everything through so the log stays readable.
    if (type != QtDebugMsg && type != QtInfoMsg &&
        (message.contains("qrc:") || message.contains("TypeError") || message.contains("ReferenceError")))
        ++warnings;
    if (previous)
        previous(type, context, message);
}

struct Step {
    QString keys;  // A key sequence understood by QKeySequence, or "type:<text>".
    int waitMs = 120;
};

void press(QQuickWindow *window, const QString &spec) {
    QWindow *target = QGuiApplication::focusWindow() ? QGuiApplication::focusWindow() : window;
    if (spec.startsWith("type:")) {
        for (const QChar c : spec.mid(5)) {
            QKeyEvent down(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QKeyEvent up(QEvent::KeyRelease, 0, Qt::NoModifier, QString(c));
            QCoreApplication::sendEvent(target, &down);
            QCoreApplication::sendEvent(target, &up);
        }
        return;
    }
    const QKeySequence sequence(spec);
    if (sequence.isEmpty())
        return;
    const QKeyCombination combo = sequence[0];
    const QString text = combo.keyboardModifiers() & ~Qt::ShiftModifier ? QString() : QKeySequence(combo.key()).toString().toLower();
    QKeyEvent down(QEvent::KeyPress, combo.key(), combo.keyboardModifiers(), text.size() == 1 ? text : QString());
    QKeyEvent up(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers(), text.size() == 1 ? text : QString());
    QCoreApplication::sendEvent(target, &down);
    QCoreApplication::sendEvent(target, &up);
}

}  // namespace

void SelfTest::run(QQuickWindow *window, Backend *backend) {
    previous = qInstallMessageHandler(handler);
    window->resize(1180, 760);
    window->show();
    window->requestActivate();

    // A tour of the interactions, in the order a person might try them.
    static const QList<Step> script{
        {"Ctrl+1"}, {"Down"}, {"Down"}, {"Shift+Down"}, {"Up"}, {"Space", 300}, {"Down"}, {"Escape", 400},
        {"Return", 300}, {"Escape", 400}, {"Down"}, {"t"}, {"y"}, {"]"}, {"["}, {"Shift+]"}, {"."}, {","}, {"Alt+["}, {"Alt+,"},
        {"0"}, {"3"}, {"="}, {"-"}, {"s"}, {"Ctrl+S"}, {"c"}, {"c"},
        {"d", 250}, {"type:tom 3pm"}, {"Escape", 400}, {"k", 250}, {"Escape", 400}, {"e", 250}, {"Escape", 400},
        {"x", 250}, {"Escape", 400}, {"l", 250}, {"type:urgent"}, {"Escape", 400}, {"m", 250}, {"Escape", 400},
        {"j", 250}, {"type:work"}, {"Escape", 400}, {"f", 250}, {"Escape", 400}, {"/", 250}, {"Escape", 400},
        {"Ctrl+Shift+O", 250}, {"type:call"}, {"Escape", 400}, {"?", 300}, {"Escape", 400},
        {"Ctrl+2"}, {"Ctrl+3"}, {"Ctrl+4"}, {"Ctrl+5"}, {"Ctrl+Down"}, {"Ctrl+Down"}, {"Ctrl+Up"},
        {"Ctrl+K"}, {"Ctrl+K"}, {"Ctrl+U"}, {"Ctrl+U"}, {"Ctrl+O"}, {"Ctrl+O"},
        {"Ctrl+1"}, {"Down"}, {"Ctrl+C"}, {"Ctrl+D"}, {"Ctrl+Z"}, {"Ctrl+Shift+Z"}, {"Ctrl+Z"},
        {"Ctrl+N", 250}, {"type:Call Sam tomorrow at 3pm #family !!"}, {"Return", 250},
        {"Ctrl+Alt+N", 250}, {"type:Big project"}, {"Escape", 400}, {"Alt+N", 250}, {"type:A step"}, {"Escape", 400},
        {"Ctrl+="}, {"Ctrl+-"}, {"Ctrl+Alt+Up"}, {"Ctrl+Alt+Down"}, {"Shift+F5"}, {"Ctrl+Alt+F"},
        {"Ctrl+F"}, {"type:type: hiprio"}, {"Escape", 400}, {"Ctrl+Shift+2", 400}, {"type:Quick thing friday"},
        {"Escape", 400}, {"Ctrl+.", 200}, {"Ctrl+Alt+."}, {"Delete"}, {"Ctrl+Z"},
        {"Ctrl+Shift+Alt+F", 400}, {"Escape", 400}, {"Ctrl+1"}, {"Down"}, {"Return", 400}, {"Tab"}, {"Tab"},
        {"Tab"}, {"Down", 300}, {"Escape", 400}, {"Escape", 400}, {"Ctrl+,", 500}, {"Escape", 400},
        {"Ctrl+Alt+W", 600},
    };

    static int changes = 0;
    QObject::connect(backend, &Backend::dataChanged, window, [] { ++changes; });
    auto *index = new int(0);
    auto *timer = new QTimer(window);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, window, [=] {
        if (*index >= script.size()) {
            std::fprintf(stderr, "selftest: %lld steps, %d QML warning(s); %d changes made, last \"%s\"\n",
                         qlonglong(script.size()), warnings, changes, qPrintable(backend->undoLabel()));
            QCoreApplication::exit(warnings ? 1 : 0);
            return;
        }
        const Step step = script.at((*index)++);
        if (qEnvironmentVariableIsSet("PRECRIME_SELFTEST_TRACE")) {
            QQuickItem *focus = window->activeFocusItem();
            QString chain;
            for (QQuickItem *i = focus; i; i = i->parentItem())
                chain += QString(i->metaObject()->className()).section('_', 0, 0) + (i->objectName().isEmpty() ? "" : "#" + i->objectName()) + " < ";
            std::fprintf(stderr, "step %-24s changes=%d focus=%s\n", qPrintable(step.keys), changes, qPrintable(chain.left(220)));
        }
        press(window, step.keys);
        timer->start(step.waitMs);
    });
    timer->start(800);
}
