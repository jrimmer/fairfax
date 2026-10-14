#include "webdav.h"

#include <QEventLoop>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QXmlStreamReader>

WebDavRemote::WebDavRemote(const QUrl &baseUrl, const QString &user, const QString &password, int timeoutMs)
    : m_base(baseUrl), m_timeoutMs(timeoutMs) {
    const QString path = m_base.path();
    if (!path.endsWith('/'))
        m_base.setPath(path + '/');
    if (!user.isEmpty())
        m_authorization = "Basic " + QString("%1:%2").arg(user, password).toUtf8().toBase64();
    m_base.setUserInfo(QString());
}

QUrl WebDavRemote::urlFor(const QString &path) const {
    QUrl url = m_base;
    url.setPath(m_base.path() + path, QUrl::DecodedMode);
    return url;
}

WebDavRemote::Response WebDavRemote::request(const QByteArray &verb, const QUrl &url, const QByteArray &body,
                                             const QList<QPair<QByteArray, QByteArray>> &headers) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Fairfax");
    if (!m_authorization.isEmpty())
        req.setRawHeader("Authorization", m_authorization);
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      verb == "PUT" ? "application/octet-stream" : "application/xml; charset=utf-8");
    QNetworkReply *reply = m_network.sendCustomRequest(req, verb, body);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(m_timeoutMs);
    loop.exec();

    Response r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body = reply->readAll();
    if (r.status == 0)
        r.error = timedOut ? "the server did not answer in time" : reply->errorString();
    reply->deleteLater();
    return r;
}

QString WebDavRemote::describe(const Response &r) {
    if (r.status == 0)
        return "Cannot reach the WebDAV server: " + r.error;
    if (r.status == 401 || r.status == 403)
        return QString("The WebDAV server rejected the user name or password (%1)").arg(r.status);
    return QString("The WebDAV server answered %1").arg(r.status);
}

bool WebDavRemote::ensureCollection(const QString &relativeDir, QString *error) {
    // Walks down from the base, creating each missing folder. MKCOL answers
    // 405 for one that already exists.
    auto make = [&](const QString &key, const QUrl &url) {
        if (m_known.contains(key))
            return true;
        const Response r = request("MKCOL", url);
        if (r.status != 201 && r.status != 405 && r.status != 200 && r.status != 301) {
            if (error)
                *error = describe(r);
            return false;
        }
        m_known.insert(key);
        return true;
    };
    if (!make(QString(), m_base))
        return false;
    QString current;
    for (const auto &part : relativeDir.split('/', Qt::SkipEmptyParts)) {
        current += part + '/';
        if (!make(current, urlFor(current)))
            return false;
    }
    return true;
}

bool WebDavRemote::listDir(const QString &relativeDir, QMap<QString, QString> *signatures, QString *error, int depth) {
    const QUrl dir = relativeDir.isEmpty() ? m_base : urlFor(relativeDir + '/');
    const Response r = request("PROPFIND", dir,
                               "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop>"
                               "<d:getetag/><d:getlastmodified/><d:getcontentlength/><d:resourcetype/>"
                               "</d:prop></d:propfind>",
                               {{"Depth", "1"}});
    if (r.status == 404)
        return true;  // Nothing there yet.
    if (r.status != 207) {
        if (error)
            *error = describe(r);
        return false;
    }

    struct Entry {
        QString href, etag, modified, length;
        bool collection = false;
    };
    QVector<Entry> entries;
    QXmlStreamReader xml(r.body);
    Entry entry;
    bool inResponse = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == u"response") {
                entry = Entry();
                inResponse = true;
            } else if (inResponse && name == u"href") {
                entry.href = xml.readElementText().trimmed();
            } else if (inResponse && name == u"getetag") {
                entry.etag = xml.readElementText().trimmed();
            } else if (inResponse && name == u"getlastmodified") {
                entry.modified = xml.readElementText().trimmed();
            } else if (inResponse && name == u"getcontentlength") {
                entry.length = xml.readElementText().trimmed();
            } else if (inResponse && name == u"collection") {
                entry.collection = true;
            }
        } else if (xml.isEndElement() && xml.name() == u"response") {
            entries.append(entry);
            inResponse = false;
        }
    }
    if (xml.hasError()) {
        if (error)
            *error = "The WebDAV server sent a listing Fairfax cannot read";
        return false;
    }

    const QString basePath = m_base.path();
    for (const auto &e : std::as_const(entries)) {
        const QString path = QUrl(e.href).path();  // Decoded; the href may be absolute or just a path.
        if (!path.startsWith(basePath))
            continue;
        QString rel = path.mid(basePath.size());
        if (rel.endsWith('/'))
            rel.chop(1);
        if (rel.isEmpty() || rel == relativeDir)
            continue;
        if (e.collection) {
            if (depth > 0 && !listDir(rel, signatures, error, depth - 1))
                return false;
            continue;
        }
        const QString signature = !e.etag.isEmpty() ? e.etag : e.modified + ":" + e.length;
        if (SyncTree::isTreePath(rel))
            signatures->insert(rel, signature);
    }
    return true;
}

bool WebDavRemote::list(QMap<QString, QString> *signatures, QString *error) {
    return listDir(QString(), signatures, error, 2);
}

bool WebDavRemote::get(const QString &path, QByteArray *bytes, QString *error) {
    const Response r = request("GET", urlFor(path));
    if (r.status != 200) {
        if (error)
            *error = describe(r);
        return false;
    }
    *bytes = r.body;
    return true;
}

bool WebDavRemote::put(const QString &path, const QByteArray &bytes, QString *error) {
    const int slash = path.lastIndexOf('/');
    if (!ensureCollection(slash < 0 ? QString() : path.left(slash), error))
        return false;
    const Response r = request("PUT", urlFor(path), bytes);
    if (r.status != 200 && r.status != 201 && r.status != 204) {
        if (error)
            *error = describe(r);
        return false;
    }
    return true;
}

bool WebDavRemote::remove(const QString &path, QString *error) {
    const Response r = request("DELETE", urlFor(path));
    if (r.status != 200 && r.status != 204 && r.status != 404) {
        if (error)
            *error = describe(r);
        return false;
    }
    return true;
}
