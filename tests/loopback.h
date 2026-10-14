#pragma once

// What the browser does at the end of an OAuth sign-in: GET the redirect URI
// on the loopback listener. The listener lives in the test's own thread, so
// the waiting runs the event loop.

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QtTest>

inline QString httpGet(quint16 port, const QString &path) {
    QTcpSocket s;
    QByteArray all;
    bool failed = false;
    QObject::connect(&s, &QTcpSocket::readyRead, &s, [&] { all += s.readAll(); });
    QObject::connect(&s, &QTcpSocket::errorOccurred, &s, [&] { failed = true; });
    s.connectToHost(QHostAddress::LocalHost, port);
    QTest::qWaitFor([&] { return failed || s.state() == QAbstractSocket::ConnectedState; }, 3000);
    if (failed || s.state() != QAbstractSocket::ConnectedState)
        return "no connection";
    s.write(("GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n").toUtf8());
    QTest::qWaitFor([&] { return s.state() == QAbstractSocket::UnconnectedState; }, 3000);
    all += s.readAll();
    return QString::fromUtf8(all);
}
