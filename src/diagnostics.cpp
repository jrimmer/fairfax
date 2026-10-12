#include "diagnostics.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <cstdio>

namespace {

constexpr int Capacity = 100;

QMutex mutex;
QStringList lines;
QtMessageHandler previous = nullptr;

void append(const QString &message) {
    QMutexLocker lock(&mutex);
    lines.append(QDateTime::currentDateTime().toString(Qt::ISODate).replace('T', ' ') + "  " + message);
    while (lines.size() > Capacity)
        lines.removeFirst();
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    // Debug chatter would drown out the failures this log is for.
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        append(message);
    if (previous)
        previous(type, context, message);
    else
        fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
}

}  // namespace

namespace Diagnostics {

void install() { previous = qInstallMessageHandler(handler); }

void note(const QString &message) { append(message); }

QStringList recent() {
    QMutexLocker lock(&mutex);
    return lines;
}

void clear() {
    QMutexLocker lock(&mutex);
    lines.clear();
}

}  // namespace Diagnostics
