#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "../src/sounds.h"
#include "testregistry.h"

class TestSounds : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString player, log, sounds;

    QStringList calls() {
        QFile f(log);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    }

private slots:
    void initTestCase() {
        QVERIFY(dir.isValid());
        sounds = dir.filePath("sounds");
        QDir().mkpath(sounds);
        for (const char *n : {"complete.oga", "bell.oga", "alarm-clock-elapsed.oga"}) {
            QFile f(sounds + "/" + n);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        log = dir.filePath("log");
        player = dir.filePath("fakeplayer.sh");
        QFile p(player);
        QVERIFY(p.open(QIODevice::WriteOnly));
        p.write(QString("#!/bin/sh\necho \"$1\" >> '%1'\n").arg(log).toUtf8());
        p.close();
        p.setPermissions(QFile::permissions(player) | QFile::ExeOwner);
    }

    void playsRightFilesAndThrottles() {
#ifdef Q_OS_WIN
        QSKIP("Windows has no command-line player; Fairfax beeps instead");
#endif
        QFile::remove(log);
        Sounds s(player, sounds);
        s.complete();
        s.uncomplete(); // within 80 ms: dropped
        QTRY_COMPARE(calls(), QStringList{sounds + "/complete.oga"});
        QTest::qWait(100);
        s.uncomplete();
        QTRY_COMPARE(calls().size(), 2);
        QTest::qWait(100);
        s.alarm();
        QTRY_COMPARE(calls().size(), 3);
        QCOMPARE(calls().at(1), sounds + "/bell.oga");
        QCOMPARE(calls().at(2), sounds + "/alarm-clock-elapsed.oga");
    }

    void disabledAndMissing() {
        QFile::remove(log);
        Sounds s(player, sounds);
        QSignalSpy spy(&s, &Sounds::enabledChanged);
        s.setEnabled(false);
        s.setEnabled(false);
        QCOMPARE(spy.count(), 1);
        s.complete();
        Sounds none(player, dir.filePath("nonexistent"));
        none.complete();
        Sounds noPlayer(QString(), sounds);
        noPlayer.complete();
        QTest::qWait(250);
        QVERIFY(calls().isEmpty());
    }
};

REGISTER_TEST(TestSounds)
#include "test_sounds.moc"
