#pragma once

// A small CalDAV server for the tests, on 127.0.0.1 over plain HTTP: a
// principal, a calendar home, one VTODO calendar and one VEVENT-only calendar
// (which the client must ignore), for src/caldavclient.cpp to talk to. It
// answers the parts of CalDAV the client uses and nothing more: Basic auth,
// the well-known redirect, PROPFIND for the principal, the home set, the
// calendar listing and etags, REPORT calendar-multiget and sync-collection
// (with a switch to turn sync-collection off), GET, PUT, DELETE and
// MKCALENDAR. Every write bumps the resource's etag and the collection's
// ctag, so a client can tell that something changed.
//
// It lives in the test's event loop; wait for results with QTRY_*.

#include <QByteArray>
#include <QHostAddress>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

class FakeCalDav : public QObject {
public:
    struct Collection {
        QString href;          // Always ends with a slash, e.g. /calendars/jane/tasks/
        QString displayName;
        QString color;
        QStringList components;  // "VTODO", "VEVENT" ...
        QString ctag = QStringLiteral("ctag-1");
        QMap<QString, QByteArray> resources;  // href (path) -> body
        QMap<QString, QString> etags;         // href -> etag
    };

    explicit FakeCalDav(QObject *parent = nullptr) : QObject(parent) {
        Collection tasks;
        tasks.href = QStringLiteral("/calendars/jane/tasks/");
        tasks.displayName = QStringLiteral("Tasks");
        tasks.color = QStringLiteral("#00ff00ff");
        tasks.components = {QStringLiteral("VTODO")};
        m_collections.append(tasks);

        Collection events;
        events.href = QStringLiteral("/calendars/jane/events/");
        events.displayName = QStringLiteral("Events");
        events.color = QStringLiteral("#ff0000ff");
        events.components = {QStringLiteral("VEVENT")};
        m_collections.append(events);

        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                accept(socket);
        });
        const bool ok = m_server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(ok);
        Q_UNUSED(ok);
    }

    quint16 port() const { return m_server.serverPort(); }
    QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port())); }

    // What the server advertises and accepts.
    QString user = QStringLiteral("jane");
    QString password = QStringLiteral("app-password");
    QString principal = QStringLiteral("/principals/jane/");
    QString home = QStringLiteral("/calendars/jane/");
    bool syncCollection = true;   // Off: REPORT sync-collection is refused, forcing the etag fallback.
    QString silentOn;             // A method (PROPFIND, REPORT, PUT...) the server never answers, to provoke a timeout.

    // What happened, for assertions.
    QMap<QString, int> requests;   // Method -> count.
    int connections = 0;

    QList<Collection> &collections() { return m_collections; }

    Collection *tasks() {
        for (Collection &c : m_collections)
            if (c.components.contains(QStringLiteral("VTODO")))
                return &c;
        return nullptr;
    }

    // Adds or replaces a resource "from another client": the etag and the
    // collection's ctag change, as they would on a real write.
    void editFromAnotherClient(const QString &collectionHref, const QString &name, const QByteArray &data) {
        Collection *c = collection(collectionHref);
        if (!c)
            return;
        const QString href = c->href + name;
        c->resources.insert(href, data);
        c->etags.insert(href, nextEtag());
        bumpCtag(*c);
        noteChange(href, false);
    }

    void removeFromAnotherClient(const QString &collectionHref, const QString &name) {
        Collection *c = collection(collectionHref);
        if (!c)
            return;
        const QString href = c->href + name;
        c->resources.remove(href);
        c->etags.remove(href);
        bumpCtag(*c);
        noteChange(href, true);
    }

    // The whole connection is dropped, as a network failure would.
    void dropConnections() {
        for (Session *s : QList<Session *>(m_sessions))
            s->socket->abort();
    }

    int countRequests(const QString &method) const { return requests.value(method.toUpper()); }

private:
    struct Session {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        QByteArray method, path;
        QMap<QByteArray, QByteArray> headers;   // Lower-case names.
        void send(const QByteArray &data) { socket->write(data); }
    };

    void accept(QTcpSocket *socket) {
        ++connections;
        auto *s = new Session;
        s->socket = socket;
        m_sessions.append(s);
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, s] {
            s->buffer.append(s->socket->readAll());
            feed(s);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, s] {
            m_sessions.removeAll(s);
            s->socket->deleteLater();
            delete s;
        });
    }

    void feed(Session *s) {
        while (true) {
            const int headerEnd = s->buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QByteArray head = s->buffer.left(headerEnd);
            const QList<QByteArray> lines = head.split('\n');
            if (lines.isEmpty())
                return;
            const QList<QByteArray> request = lines.first().trimmed().split(' ');
            if (request.size() < 2)
                return;
            s->method = request[0].trimmed();
            s->path = request[1].trimmed();
            s->headers.clear();
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray line = lines[i].trimmed();
                const int colon = line.indexOf(':');
                if (colon > 0)
                    s->headers.insert(line.left(colon).toLower(), line.mid(colon + 1).trimmed());
            }
            int length = s->headers.value("content-length").toInt();
            if (s->headers.value("expect").toLower().contains("100-continue")) {
                s->send("HTTP/1.1 100 Continue\r\n\r\n");
                s->headers.remove("expect");
            }
            if (s->buffer.size() < headerEnd + 4 + length)
                return;
            const QByteArray body = s->buffer.mid(headerEnd + 4, length);
            s->buffer.remove(0, headerEnd + 4 + length);
            requests[s->method.toUpper()] += 1;
            if (!silentOn.isEmpty() && s->method.toUpper() == silentOn.toUpper())
                continue;
            handle(s, body);
        }
    }

    void handle(Session *s, const QByteArray &body) {
        const QByteArray method = s->method.toUpper();
        const QString path = QString::fromUtf8(s->path);

        if (!authorized(s)) {
            respond(s, 401, {}, {{"WWW-Authenticate", "Basic realm=\"Fairfax\""}});
            return;
        }

        if (path == QLatin1String("/.well-known/caldav")) {
            respond(s, 301, {}, {{"Location", principal.toUtf8()}});
            return;
        }

        if (method == "PROPFIND") {
            propfind(s, path, body);
            return;
        }
        if (method == "REPORT") {
            report(s, path, body);
            return;
        }
        if (method == "MKCALENDAR") {
            mkcalendar(s, path, body);
            return;
        }

        Collection *c = collectionForResource(path);
        if (method == "GET") {
            const QString resource = findResource(path);
            if (!resource.isEmpty()) {
                respond(s, 200, c->resources.value(resource), {{"ETag", c->etags.value(resource).toUtf8()}});
            } else if (isCollection(path)) {
                respond(s, 200, "<html><body>collection</body></html>");
            } else {
                respond(s, 404, {});
            }
            return;
        }
        if (method == "PUT") {
            if (!c) {
                respond(s, 409, {});
                return;
            }
            const bool exists = c->resources.contains(path);
            const QByteArray ifNoneMatch = s->headers.value("if-none-match");
            const QByteArray ifMatch = s->headers.value("if-match");
            if (ifNoneMatch == "*" && exists) {
                respond(s, 412, {});
                return;
            }
            if (!ifMatch.isEmpty() && (!exists || c->etags.value(path).toUtf8() != ifMatch)) {
                respond(s, 412, {});
                return;
            }
            if (ifNoneMatch.isEmpty() && ifMatch.isEmpty() && !exists) {
                // A plain create still works.
            }
            c->resources.insert(path, body);
            const QString etag = nextEtag();
            c->etags.insert(path, etag);
            bumpCtag(*c);
            noteChange(path, false);
            if (exists)
                respond(s, 204, {}, {{"ETag", etag.toUtf8()}});
            else
                respond(s, 201, {}, {{"ETag", etag.toUtf8()}, {"Location", path.toUtf8()}});
            return;
        }
        if (method == "DELETE") {
            const QString resource = findResource(path);
            if (resource.isEmpty()) {
                respond(s, 404, {});
                return;
            }
            const QByteArray ifMatch = s->headers.value("if-match");
            if (!ifMatch.isEmpty() && c->etags.value(resource).toUtf8() != ifMatch) {
                respond(s, 412, {});
                return;
            }
            c->resources.remove(resource);
            c->etags.remove(resource);
            bumpCtag(*c);
            noteChange(resource, true);
            respond(s, 204, {});
            return;
        }
        respond(s, 405, {});
    }

    // ------------------------------------------------------------ methods

    void propfind(Session *s, const QString &path, const QByteArray &body) {
        const QString want = QString::fromUtf8(body);
        const QString normalized = path.endsWith('/') ? path : path + '/';

        if (want.contains(QLatin1String("current-user-principal"))) {
            const QString xml = QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>%1</d:href>"
                "<d:propstat><d:prop><d:current-user-principal><d:href>%1</d:href></d:current-user-principal></d:prop>"
                "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>").arg(path);
            respond(s, 207, xml.toUtf8());
            return;
        }
        if (want.contains(QLatin1String("calendar-home-set"))) {
            const QString xml = QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<d:multistatus xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
                "<d:response><d:href>%1</d:href><d:propstat><d:prop>"
                "<c:calendar-home-set><d:href>%2</d:href></c:calendar-home-set>"
                "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>")
                                    .arg(path, home);
            respond(s, 207, xml.toUtf8());
            return;
        }

        // Depth 1 listing of a calendar home, or Depth 0/1 of a single calendar.
        const int depth = s->headers.value("depth").toInt();
        QStringList responses;
        if (normalized == home) {
            responses << collectionResponse(QStringLiteral("home"), QLatin1String("/calendars/jane/"));
            for (Collection &c : m_collections) {
                if (depth >= 1)
                    responses << collectionResponse(c.displayName, c.href, c.color, c.components, c.ctag);
            }
        } else if (Collection *c = collection(normalized)) {
            if (depth >= 1) {
                responses << collectionResponse(c->displayName, c->href, c->color, c->components, c->ctag);
                for (auto it = c->resources.constBegin(); it != c->resources.constEnd(); ++it) {
                    const QString href = it.key();
                    responses << QStringLiteral(
                        "<d:response><d:href>%1</d:href><d:propstat><d:prop><d:getetag>%2</d:getetag></d:prop>"
                        "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>")
                                     .arg(href, c->etags.value(href));
                }
            } else {
                responses << collectionResponse(c->displayName, c->href, c->color, c->components, c->ctag);
            }
        } else {
            respond(s, 404, {});
            return;
        }
        const QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<d:multistatus xmlns:d=\"DAV:\" "
                                           "xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
                                           "xmlns:cs=\"http://calendarserver.org/ns/\" "
                                           "xmlns:ical=\"http://apple.com/ns/ical/\">%1</d:multistatus>")
                                .arg(responses.join(QString()));
        respond(s, 207, xml.toUtf8());
    }

    void report(Session *s, const QString &path, const QByteArray &body) {
        const QString want = QString::fromUtf8(body);
        const QString normalized = path.endsWith('/') ? path : path + '/';
        Collection *c = collection(normalized);
        if (!c) {
            respond(s, 404, {});
            return;
        }
        if (want.contains(QLatin1String("sync-collection"))) {
            if (!syncCollection) {
                respond(s, 403, {});
                return;
            }
            syncReport(s, *c, want);
            return;
        }
        multigetReport(s, *c, want);
    }

    void syncReport(Session *s, Collection &c, const QString &body) {
        int since = 0;
        const QRegularExpression tokenRe(QStringLiteral("<d:sync-token>sync-(\\d+)</d:sync-token>"));
        const auto match = tokenRe.match(body);
        if (match.hasMatch())
            since = match.captured(1).toInt();

        QStringList responses;
        QStringList changed = c.resources.keys();
        for (const QString &href : changed) {
            const int version = m_changeVersions.value(href, 0);
            if (version <= since)
                continue;
            responses << QStringLiteral("<d:response><d:href>%1</d:href><d:propstat><d:prop><d:getetag>%2</d:getetag></d:prop>"
                                        "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>")
                             .arg(href, c.etags.value(href));
        }
        for (const auto &removed : m_removed) {
            if (removed.first <= since)
                continue;
            if (!removed.second.startsWith(c.href))
                continue;
            responses << QStringLiteral("<d:response><d:href>%1</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>")
                             .arg(removed.second);
        }
        const QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                           "<d:multistatus xmlns:d=\"DAV:\">%1<d:sync-token>sync-%2</d:sync-token></d:multistatus>")
                                .arg(responses.join(QString()))
                                .arg(m_version);
        respond(s, 207, xml.toUtf8());
    }

    void multigetReport(Session *s, Collection &c, const QString &body) {
        QStringList responses;
        const QRegularExpression hrefRe(QStringLiteral("<[^>]*href>([^<]+)</[^>]*href>"));
        auto it = hrefRe.globalMatch(body);
        while (it.hasNext()) {
            const QString href = it.next().captured(1).trimmed();
            if (!c.resources.contains(href))
                continue;
            const QByteArray data = c.resources.value(href);
            responses << QStringLiteral("<d:response><d:href>%1</d:href><d:propstat><d:prop><d:getetag>%2</d:getetag>"
                                        "<c:calendar-data>%3</c:calendar-data></d:prop>"
                                        "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>")
                             .arg(href, c.etags.value(href), QString::fromUtf8(data).toHtmlEscaped());
        }
        const QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                           "<d:multistatus xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">%1</d:multistatus>")
                                .arg(responses.join(QString()));
        respond(s, 207, xml.toUtf8());
    }

    void mkcalendar(Session *s, const QString &path, const QByteArray &body) {
        const QString href = path.endsWith('/') ? path : path + '/';
        if (collection(href)) {
            respond(s, 405, {});
            return;
        }
        Collection c;
        c.href = href;
        const QRegularExpression nameRe(QStringLiteral("<[^>]*displayname[^>]*>([^<]*)<"));
        const QRegularExpression colorRe(QStringLiteral("<[^>]*calendar-color[^>]*>([^<]*)<"));
        const auto name = nameRe.match(QString::fromUtf8(body));
        const auto color = colorRe.match(QString::fromUtf8(body));
        c.displayName = name.hasMatch() ? name.captured(1) : QString();
        c.color = color.hasMatch() ? color.captured(1) : QString();
        c.components = {QStringLiteral("VTODO")};
        c.ctag = QStringLiteral("ctag-1");
        m_collections.append(c);
        respond(s, 201, {}, {{"Location", href.toUtf8()}});
    }

    // ------------------------------------------------------------- helpers

    static QString collectionResponse(const QString &name, const QString &href, const QString &color = QString(),
                                      const QStringList &components = {}, const QString &ctag = QString()) {
        const bool isCalendar = !color.isEmpty() || !components.isEmpty() || !ctag.isEmpty();
        QString props = QStringLiteral("<d:resourcetype><d:collection/>");
        if (isCalendar)
            props += QStringLiteral("<c:calendar/>");
        props += QStringLiteral("</d:resourcetype>");
        if (!name.isEmpty())
            props += QStringLiteral("<d:displayname>%1</d:displayname>").arg(name.toHtmlEscaped());
        if (!color.isEmpty())
            props += QStringLiteral("<ical:calendar-color>%1</ical:calendar-color>").arg(color.toHtmlEscaped());
        if (isCalendar) {
            if (components.isEmpty()) {
                props += QStringLiteral("<c:supported-calendar-component-set><c:comp name=\"VEVENT\"/><c:comp name=\"VTODO\"/>"
                                        "</c:supported-calendar-component-set>");
            } else {
                QString comps;
                for (const QString &comp : components)
                    comps += QStringLiteral("<c:comp name=\"%1\"/>").arg(comp);
                props += QStringLiteral("<c:supported-calendar-component-set>%1</c:supported-calendar-component-set>").arg(comps);
            }
            if (!ctag.isEmpty())
                props += QStringLiteral("<cs:getctag>%1</cs:getctag>").arg(ctag.toHtmlEscaped());
            if (components.contains(QStringLiteral("VTODO")))
                props += QStringLiteral("<d:sync-token>sync-%1</d:sync-token>").arg(0);
        }
        return QStringLiteral("<d:response><d:href>%1</d:href><d:propstat><d:prop>%2</d:prop>"
                              "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>")
            .arg(href, props);
    }

    bool authorized(const Session *s) const {
        const QByteArray header = s->headers.value("authorization");
        if (header.isEmpty())
            return false;
        const QByteArray expected = "Basic " + QStringLiteral("%1:%2").arg(user, password).toUtf8().toBase64();
        return header == expected;
    }

    static bool isCollection(const QString &path) { return path.endsWith('/'); }

    Collection *collection(const QString &href) {
        const QString normalized = href.endsWith('/') ? href : href + '/';
        for (Collection &c : m_collections)
            if (c.href == normalized)
                return &c;
        return nullptr;
    }

    Collection *collectionForResource(const QString &path) const {
        for (const Collection &c : m_collections)
            if (path.startsWith(c.href))
                return const_cast<Collection *>(&c);
        return nullptr;
    }

    QString findResource(const QString &path) const {
        for (const Collection &c : m_collections)
            if (c.resources.contains(path))
                return path;
        return QString();
    }

    QString nextEtag() { return QStringLiteral("\"etag-%1\"").arg(++m_version); }
    void bumpCtag(Collection &c) { c.ctag = QStringLiteral("ctag-%1").arg(++m_version); }
    void noteChange(const QString &href, bool removed) {
        if (removed)
            m_removed.append({m_version, href});
        else
            m_changeVersions.insert(href, m_version);
    }

    void respond(Session *s, int status, const QByteArray &body,
                 const QList<QPair<QByteArray, QByteArray>> &headers = {}, const QByteArray &contentType = "application/xml; charset=utf-8") {
        static const QMap<int, QByteArray> reasons{{200, "OK"}, {201, "Created"}, {204, "No Content"},
                                                   {207, "Multi-Status"}, {301, "Moved Permanently"},
                                                   {400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"},
                                                   {404, "Not Found"}, {405, "Method Not Allowed"}, {409, "Conflict"},
                                                   {412, "Precondition Failed"}, {500, "Internal Server Error"},
                                                   {503, "Service Unavailable"}};
        QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " " + reasons.value(status, "Status") + "\r\n";
        if (!body.isEmpty())
            out += "Content-Type: " + contentType + "\r\n";
        out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        out += "Connection: keep-alive\r\n";
        for (const auto &h : headers)
            out += h.first + ": " + h.second + "\r\n";
        out += "\r\n" + body;
        s->send(out);
    }

    QTcpServer m_server;
    QList<Session *> m_sessions;
    QList<Collection> m_collections;
    int m_version = 0;
    QMap<QString, int> m_changeVersions;
    QList<QPair<int, QString>> m_removed;
};
