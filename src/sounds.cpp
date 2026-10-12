#include "sounds.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
constexpr qint64 kThrottleMs = 80;

// Linux plays freedesktop sound-theme files with PipeWire or PulseAudio;
// macOS has afplay and its own system sounds. Windows has no command-line
// player, so there sounds stay off (alarm notifications still chime).
QString findPlayer() {
    for (const char *name : {"pw-play", "paplay", "afplay"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!path.isEmpty())
            return path;
    }
    return {};
}
} // namespace

Sounds::Sounds(QObject *parent)
    : QObject(parent), m_player(findPlayer()),
#if defined(Q_OS_MACOS)
      m_dir(QStringLiteral("/System/Library/Sounds")) {}
#else
      m_dir(QStringLiteral("/usr/share/sounds/freedesktop/stereo")) {}
#endif

Sounds::Sounds(const QString &playerPath, const QString &soundDir, QObject *parent)
    : QObject(parent), m_player(playerPath), m_dir(soundDir) {}

void Sounds::setEnabled(bool on) {
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit enabledChanged();
}

void Sounds::complete() { play({"complete.oga", "message.oga", "bell.oga", "Glass.aiff"}); }
void Sounds::uncomplete() { play({"bell.oga", "message.oga", "Pop.aiff"}); }
void Sounds::alarm() { play({"alarm-clock-elapsed.oga", "phone-incoming-call.oga", "bell.oga", "Sosumi.aiff"}); }

void Sounds::play(const QStringList &candidates) {
    if (!m_enabled || m_player.isEmpty())
        return;
    if (m_last.isValid() && m_last.elapsed() < kThrottleMs)
        return;
    for (const QString &name : candidates) {
        const QString file = m_dir + QLatin1Char('/') + name;
        if (!QFileInfo::exists(file))
            continue;
        m_last.restart();
        QProcess::startDetached(m_player, {file});
        return;
    }
}
