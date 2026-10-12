#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

// Short feedback sounds from the freedesktop sound theme, played by handing
// the file to pw-play or paplay. No QtMultimedia; missing files or players
// are silently skipped. At most one sound starts per 80 ms.
class Sounds : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit Sounds(QObject *parent = nullptr);
    // Tests inject a fake player and a directory holding the sound files.
    Sounds(const QString &playerPath, const QString &soundDir, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    Q_INVOKABLE void complete();
    Q_INVOKABLE void uncomplete();
    Q_INVOKABLE void alarm();

signals:
    void enabledChanged();

private:
    void play(const QStringList &candidates);

    bool m_enabled = true;
    QString m_player;
    QString m_dir;
    QElapsedTimer m_last;
};
