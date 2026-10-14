#pragma once

// WebDAV as a home for the sync tree (synctree.h): PROPFIND lists the files,
// GET and PUT move them, DELETE removes them, MKCOL makes folders. Works with
// Nextcloud, ownCloud, Apache mod_dav and the like. Requests block on a local
// event loop so the sync cycle can stay a plain sequence of steps while the
// window keeps repainting.

#include <QNetworkAccessManager>
#include <QSet>
#include <QUrl>

#include "synctree.h"

class WebDavRemote : public SyncTree::Remote {
public:
    // `baseUrl` is the collection that holds the tree, e.g.
    // https://cloud.example.com/remote.php/dav/files/me/Fairfax/
    WebDavRemote(const QUrl &baseUrl, const QString &user, const QString &password, int timeoutMs = 30000);

    bool list(QMap<QString, QString> *signatures, QString *error) override;
    bool get(const QString &path, QByteArray *bytes, QString *error) override;
    bool put(const QString &path, const QByteArray &bytes, QString *error) override;
    bool remove(const QString &path, QString *error) override;

private:
    struct Response {
        int status = 0;  // 0 when the server could not be reached.
        QByteArray body;
        QString error;
    };
    Response request(const QByteArray &verb, const QUrl &url, const QByteArray &body = {},
                     const QList<QPair<QByteArray, QByteArray>> &headers = {});
    QUrl urlFor(const QString &path) const;
    bool ensureCollection(const QString &relativeDir, QString *error);
    bool listDir(const QString &relativeDir, QMap<QString, QString> *signatures, QString *error, int depth);
    static QString describe(const Response &r);

    QNetworkAccessManager m_network;
    QUrl m_base;
    QByteArray m_authorization;
    int m_timeoutMs;
    QSet<QString> m_known;  // Collections known to exist.
};
