#include "caldavclient.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QXmlStreamReader>
#include <memory>

namespace {

constexpr auto PropFindBody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\" "
    "xmlns:ical=\"http://apple.com/ns/ical/\">"
    "<d:prop>"
    "<d:resourcetype/><d:displayname/><d:getetag/>"
    "<ical:calendar-color/><cs:getctag/><d:sync-token/>"
    "<d:supported-calendar-component-set/>"
    "</d:prop></d:propfind>";

constexpr auto PrincipalBody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:current-user-principal/></d:prop></d:propfind>";

constexpr auto HomeBody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:prop><c:calendar-home-set/></d:prop></d:propfind>";

constexpr auto EtagBody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/></d:prop></d:propfind>";

constexpr auto SyncBody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:sync-collection xmlns:d=\"DAV:\">"
    "<d:sync-token>%1</d:sync-token>"
    "<d:sync-level>1</d:sync-level>"
    "<d:prop><d:getetag/></d:prop>"
    "</d:sync-collection>";

constexpr auto MultigetBody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:prop><d:getetag/><c:calendar-data/></d:prop>%1"
    "</c:calendar-multiget>";

}  // namespace

CalDavClient::CalDavClient(QObject *parent) : QObject(parent) {}

void CalDavClient::setCredentials(const QString &user, const QString &password) {
    if (user.isEmpty())
        m_authorization.clear();
    else
        m_authorization = "Basic " + QString("%1:%2").arg(user, password).toUtf8().toBase64();
}

bool CalDavClient::isLoopback(const QString &host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost";
}

int CalDavClient::backoffFor(int attempt) const {
    // 30 s, 1 min, 2 min ... up to a quarter of an hour, like the mail connector.
    const qint64 delay = qMin<qint64>(qint64(m_timings.backoffBase) * (1LL << qMin(attempt, 10)),
                                      qMax(m_timings.backoffBase, m_timings.backoffMax));
    return int(qMin<qint64>(delay, m_timings.backoffMax));
}

QUrl CalDavClient::resolve(const QString &href) const {
    return m_base.resolved(QUrl(href));
}

QString CalDavClient::itemText(const QList<DavItem> &items, const QString &name) {
    for (const DavItem &item : items)
        if (item.text.contains(name))
            return item.text.value(name);
    return QString();
}

bool CalDavClient::supportsVTodo(const DavItem &item) {
    return item.components.isEmpty() || item.components.contains(QStringLiteral("VTODO"), Qt::CaseInsensitive);
}

CalDavClient::Calendar CalDavClient::calendarFrom(const DavItem &item) {
    Calendar c;
    c.href = item.href;
    c.displayName = item.text.value(QStringLiteral("displayname"));
    c.color = item.text.value(QStringLiteral("calendar-color"));
    c.ctag = item.text.value(QStringLiteral("getctag"));
    c.syncToken = item.text.value(QStringLiteral("sync-token"));
    c.supportsVTodo = supportsVTodo(item);
    return c;
}

CalDavClient::Outcome CalDavClient::outcome(const Raw &r, const QString &what) {
    Outcome o;
    o.status = r.status;
    o.body = r.body;
    if (r.status == 401 || r.status == 403) {
        o.authFailed = true;
        o.error = QStringLiteral("The CalDAV server rejected the user name or password (%1).").arg(r.status);
    } else if (r.status == 412) {
        o.conflict = true;
        o.error = QStringLiteral("The resource changed on the server.");
    } else if (r.status == 0) {
        o.error = QStringLiteral("Cannot reach the CalDAV server: ") + r.error;
    } else if (r.status >= 200 && r.status < 300) {
        o.ok = true;
    } else {
        o.error = QStringLiteral("The CalDAV server answered %1 for %2.").arg(r.status).arg(what);
    }
    return o;
}

void CalDavClient::request(const QByteArray &verb, const QUrl &url, const QByteArray &body,
                           const QList<QPair<QByteArray, QByteArray>> &headers, RawDone done, int attempt) {
    Raw raw;
    raw.url = url;
    if (url.scheme() == QLatin1String("http") && !isLoopback(url.host())) {
        raw.error = QStringLiteral("Refusing to send a password over plain HTTP to ") + url.host();
        done(raw);
        return;
    }

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Fairfax");
    if (!m_authorization.isEmpty())
        req.setRawHeader("Authorization", m_authorization);
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QNetworkReply *reply = m_network.sendCustomRequest(req, verb, body);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    auto timedOut = std::make_shared<bool>(false);
    QObject::connect(timer, &QTimer::timeout, reply, [reply, timedOut] {
        *timedOut = true;
        reply->abort();
    });
    timer->start(m_timings.requestTimeout);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, done, attempt, timedOut, raw, verb, url, body, headers]() mutable {
        Raw r = raw;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.etag = reply->rawHeader("ETag");
        r.location = reply->rawHeader("Location");
        r.body = reply->readAll();
        r.timedOut = *timedOut;
        if (r.status == 0)
            r.error = r.timedOut ? QStringLiteral("the server did not answer in time") : reply->errorString();
        reply->deleteLater();

        // Follow a redirect by hand: QNetworkAccessManager skips them for the
        // custom verbs PROPFIND and REPORT. GET is left alone so discovery can
        // read the Location of a well-known redirect itself.
        if (verb != "GET" && r.status >= 300 && r.status < 400 && !r.location.isEmpty() && attempt < 5) {
            const QUrl next = url.resolved(QUrl(r.location));
            r.url = next;
            request(verb, next, body, headers, done, attempt + 1);
            return;
        }

        const bool retriable = r.status == 0 || r.status >= 500;
        if (retriable && attempt + 1 < m_timings.maxAttempts) {
            const int delay = backoffFor(attempt);
            emit retrying(attempt + 1, delay);
            QTimer::singleShot(delay, this, [this, verb, url, body, headers, done, attempt] {
                request(verb, url, body, headers, done, attempt + 1);
            });
            return;
        }
        done(r);
    });
}

void CalDavClient::propfind(const QUrl &url, int depth, RawDone done) {
    request("PROPFIND", url, PropFindBody, {{"Depth", QByteArray::number(depth)}}, std::move(done));
}

QList<CalDavClient::DavItem> CalDavClient::parseItems(const QByteArray &body, QString *syncToken) {
    QList<DavItem> items;
    DavItem item;
    QStringList stack;
    QXmlStreamReader xml(body);
    auto close = [&] {
        if (!stack.isEmpty())
            stack.removeLast();
    };
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            const QString parent = stack.isEmpty() ? QString() : stack.last();
            stack.append(name);
            if (name == QLatin1String("response")) {
                item = DavItem();
            } else if (name == QLatin1String("href")) {
                const QString text = xml.readElementText().trimmed();
                close();
                if (parent == QLatin1String("response"))
                    item.href = text;
                else
                    item.text.insert(parent + "#href", text);
            } else if (name == QLatin1String("collection")) {
                item.collection = true;
            } else if (name == QLatin1String("calendar")) {
                item.calendar = true;
            } else if (name == QLatin1String("comp")) {
                const QString comp = xml.attributes().value(QLatin1String("name")).toString();
                if (!comp.isEmpty())
                    item.components << comp;
            } else if (name == QLatin1String("displayname") || name == QLatin1String("calendar-color")
                       || name == QLatin1String("getctag") || name == QLatin1String("getetag")
                       || name == QLatin1String("sync-token") || name == QLatin1String("calendar-data")) {
                const QString text = xml.readElementText().trimmed();
                close();
                item.text.insert(name, text);
                if (name == QLatin1String("sync-token") && syncToken)
                    *syncToken = text;
            } else if (name == QLatin1String("status")) {
                const QString text = xml.readElementText();
                close();
                if (text.contains(QLatin1String(" 404")))
                    item.status = 404;
                else if (text.contains(QLatin1String(" 200")) && item.status == 0)
                    item.status = 200;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("response")) {
                if (!item.href.isEmpty())
                    items.append(item);
                item = DavItem();
            }
            close();
        }
    }
    return items;
}

void CalDavClient::discover(const QUrl &url, DiscoverDone done) {
    m_base = url;
    // A calendar home or a single calendar is accepted as it is: Depth 0 first
    // (a calendar answers with its own resourcetype), then a Depth 1 listing
    // for a home. Only when neither fits does the well-known handshake run.
    discoverDirect(url, [this, url, done](const Outcome &o, const QList<Calendar> &direct) {
        if (!direct.isEmpty()) {
            done(o, direct);
            return;
        }
        if (o.authFailed) {
            done(o, {});
            return;
        }
        propfind(url, 1, [this, url, done](const Raw &r) {
            const Outcome listOutcome = outcome(r, QStringLiteral("the calendar list"));
            if (listOutcome.authFailed) {
                done(listOutcome, {});
                return;
            }
            QList<Calendar> calendars;
            for (const DavItem &item : parseItems(r.body)) {
                if (!item.calendar || !supportsVTodo(item))
                    continue;
                calendars.append(calendarFrom(item));
            }
            if (!calendars.isEmpty()) {
                Outcome good = listOutcome;
                good.ok = true;
                if (m_base.path().isEmpty() && !calendars.first().href.isEmpty())
                    m_base = resolve(calendars.first().href);
                done(good, calendars);
                return;
            }
            discoverWellKnown(url, done);
        });
    });
}

void CalDavClient::discoverWellKnown(const QUrl &url, DiscoverDone done) {
    // Try the well-known path first, following a redirect by hand; a server
    // that has none answers 404 and the URL itself is the probe.
    QUrl wellKnown;
    wellKnown.setScheme(url.scheme());
    wellKnown.setAuthority(url.authority());
    wellKnown.setPath(QStringLiteral("/.well-known/caldav"));
    request("GET", wellKnown, {}, {}, [this, wellKnown, url, done](const Raw &r) {
        QUrl probe = url;
        if (r.status >= 300 && r.status < 400 && !r.location.isEmpty())
            probe = wellKnown.resolved(QUrl(r.location));
        else if (r.status == 200 && !r.body.isEmpty())
            probe = r.url;   // The well-known path answered itself.
        if (r.status == 401 || r.status == 403) {
            done(outcome(r, QStringLiteral("discovery")), {});
            return;
        }
        discoverPrincipal(probe, url, done);
    });
}

void CalDavClient::discoverPrincipal(const QUrl &url, const QUrl &fallback, DiscoverDone done) {
    request("PROPFIND", url, PrincipalBody, {{"Depth", "0"}}, [this, fallback, done](const Raw &r) {
        const Outcome o = outcome(r, QStringLiteral("discovery"));
        if (o.authFailed) {
            done(o, {});
            return;
        }
        const QList<DavItem> items = parseItems(r.body);
        const QString principal = itemText(items, QStringLiteral("current-user-principal#href"));
        if (principal.isEmpty()) {
            // Not a principal: the URL may be the home or a calendar already.
            discoverHome(fallback, fallback, done);
            return;
        }
        discoverHome(resolve(principal), fallback, done);
    });
}

void CalDavClient::discoverHome(const QUrl &url, const QUrl &fallback, DiscoverDone done) {
    request("PROPFIND", url, HomeBody, {{"Depth", "0"}}, [this, url, fallback, done](const Raw &r) {
        const QList<DavItem> items = parseItems(r.body);
        const QString home = itemText(items, QStringLiteral("calendar-home-set#href"));
        // The principal itself may be a collection that holds the calendars.
        const QUrl list = home.isEmpty() ? url : resolve(home);
        propfind(list, 1, [this, list, fallback, done](const Raw &r2) {
            const Outcome o = outcome(r2, QStringLiteral("the calendar list"));
            QList<Calendar> calendars;
            if (o.ok || o.status == 207) {
                for (const DavItem &item : parseItems(r2.body)) {
                    if (!item.calendar || !supportsVTodo(item))
                        continue;
                    calendars.append(calendarFrom(item));
                    if (!calendars.last().href.isEmpty())
                        m_base = resolve(calendars.last().href);
                }
            }
            if (!calendars.isEmpty()) {
                Outcome good = o;
                good.ok = true;
                done(good, calendars);
                return;
            }
            discoverDirect(fallback, done);
        });
    });
}

void CalDavClient::discoverDirect(const QUrl &url, DiscoverDone done) {
    // Depth 0 on the URL itself: a single calendar answers with its own
    // resourcetype and is accepted as-is.
    propfind(url, 0, [this, done](const Raw &r) {
        const Outcome o = outcome(r, QStringLiteral("the calendar"));
        QList<Calendar> calendars;
        for (const DavItem &item : parseItems(r.body)) {
            if (!item.calendar || !supportsVTodo(item))
                continue;
            calendars.append(calendarFrom(item));
        }
        if (calendars.isEmpty()) {
            done(o, {});
            return;
        }
        m_base = resolve(calendars.first().href);
        Outcome good = o;
        good.ok = true;
        done(good, calendars);
    });
}

void CalDavClient::listEtags(const QString &calendarHref, EtagsDone done) {
    propfind(resolve(calendarHref), 1, [this, calendarHref, done](const Raw &r) {
        const Outcome o = outcome(r, QStringLiteral("the etag list"));
        if (!o.ok && o.status != 207) {
            done(o, {});
            return;
        }
        QMap<QString, QString> etags;
        const QString base = resolve(calendarHref).path();
        for (const DavItem &item : parseItems(r.body)) {
            const QString path = resolve(item.href).path();
            if (path == base || !item.text.contains(QStringLiteral("getetag")))
                continue;
            etags.insert(item.href, item.text.value(QStringLiteral("getetag")));
        }
        Outcome good = o;
        good.ok = true;
        done(good, etags);
    });
}

void CalDavClient::syncCollection(const QString &calendarHref, const QString &token, SyncDone done) {
    const QByteArray body = QString::fromUtf8(SyncBody).arg(token).toUtf8();
    const QUrl url = resolve(calendarHref);
    request("REPORT", url, body, {{"Depth", "1"}}, [this, calendarHref, token, done](const Raw &r) {
        if (r.status == 400 || r.status == 403 || r.status == 404 || r.status == 501) {
            // The server knows no sync-collection: fall back to a plain etag list.
            listEtags(calendarHref, [done](const Outcome &o, const QMap<QString, QString> &etags) {
                if (!o.ok) {
                    done(o, {}, {}, QString());
                    return;
                }
                done(o, etags.keys(), {}, QString());
            });
            return;
        }
        const Outcome o = outcome(r, QStringLiteral("sync-collection"));
        if (!o.ok && o.status != 207) {
            done(o, {}, {}, QString());
            return;
        }
        QString newToken;
        const QList<DavItem> items = parseItems(r.body, &newToken);
        const QString base = resolve(calendarHref).path();
        QStringList changed, removed;
        // Removed resources come back as status 404 in their own response.
        for (const DavItem &item : items) {
            const QString path = resolve(item.href).path();
            if (path == base)
                continue;
            if (item.status == 404)
                removed << item.href;
            else
                changed << item.href;
        }
        Outcome good = o;
        good.ok = true;
        done(good, changed, removed, newToken);
    });
}

void CalDavClient::multiget(const QString &calendarHref, const QStringList &hrefs, MultigetDone done) {
    if (hrefs.isEmpty()) {
        Outcome o;
        o.ok = true;
        done(o, {});
        return;
    }
    QString refs;
    for (const QString &href : hrefs)
        refs += "<d:href>" + href.toHtmlEscaped() + "</d:href>";
    const QByteArray body = QString::fromUtf8(MultigetBody).arg(refs).toUtf8();
    request("REPORT", resolve(calendarHref), body, {{"Depth", "1"}}, [this, done](const Raw &r) {
        const Outcome o = outcome(r, QStringLiteral("calendar-multiget"));
        if (!o.ok && o.status != 207) {
            done(o, {});
            return;
        }
        QMap<QString, Resource> resources;
        for (const DavItem &item : parseItems(r.body)) {
            if (!item.text.contains(QStringLiteral("calendar-data")))
                continue;
            Resource res;
            res.etag = item.text.value(QStringLiteral("getetag"));
            res.data = item.text.value(QStringLiteral("calendar-data")).toUtf8();
            resources.insert(item.href, res);
        }
        Outcome good = o;
        good.ok = true;
        done(good, resources);
    });
}

void CalDavClient::fetchEtag(const QString &href, std::function<void(const Outcome &, const QString &)> done) {
    request("PROPFIND", resolve(href), EtagBody, {{"Depth", "0"}}, [done](const Raw &r) {
        const Outcome o = outcome(r, QStringLiteral("the new etag"));
        QString etag;
        for (const DavItem &item : parseItems(r.body))
            etag = item.text.value(QStringLiteral("getetag"), etag);
        done(o, etag);
    });
}

void CalDavClient::put(const QString &href, const QByteArray &data, const QString &ifMatch, PutDone done) {
    QList<QPair<QByteArray, QByteArray>> headers;
    if (ifMatch.isEmpty())
        headers << qMakePair(QByteArray("If-None-Match"), QByteArray("*"));
    else
        headers << qMakePair(QByteArray("If-Match"), ifMatch.toUtf8());
    request("PUT", resolve(href), data, headers, [this, href, done](const Raw &r) {
        const Outcome o = outcome(r, QStringLiteral("the write"));
        if (!o.ok) {
            done(o, QString());
            return;
        }
        const QString etag = r.etag;
        if (!etag.isEmpty()) {
            done(o, etag);
            return;
        }
        // The server did not return an ETag; ask for it.
        fetchEtag(href, [done](const Outcome &e, const QString &fetched) {
            Outcome out = e;
            out.ok = true;
            done(out, fetched);
        });
    });
}

void CalDavClient::remove(const QString &href, const QString &ifMatch, RemoveDone done) {
    QList<QPair<QByteArray, QByteArray>> headers;
    if (!ifMatch.isEmpty())
        headers << qMakePair(QByteArray("If-Match"), ifMatch.toUtf8());
    request("DELETE", resolve(href), {}, headers, [done](const Raw &r) {
        Outcome o = outcome(r, QStringLiteral("the delete"));
        if (o.ok || r.status == 404)   // Already gone is fine.
            o.ok = true;
        done(o);
    });
}

void CalDavClient::mkcalendar(const QString &homeHref, const QString &name, const QString &color, MkCalendarDone done) {
    QUrl home = resolve(homeHref);
    QString path = home.path();
    if (!path.endsWith('/'))
        path += '/';
    const QString segment = QString::fromUtf8(QUrl::toPercentEncoding(name)) + '/';
    home.setPath(path + segment);
    m_base = home.resolved(QUrl(QStringLiteral("../")));   // Keep the home as the base.

    QString body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<c:mkcalendar xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
        "xmlns:ical=\"http://apple.com/ns/ical/\">"
        "<d:set><d:prop>"
        "<d:displayname>%1</d:displayname>"
        "<ical:calendar-color>%2</ical:calendar-color>"
        "<c:supported-calendar-component-set><c:comp name=\"VTODO\"/></c:supported-calendar-component-set>"
        "</d:prop></d:set></c:mkcalendar>").arg(name.toHtmlEscaped(), color.toHtmlEscaped());
    request("MKCALENDAR", home, body.toUtf8(), {}, [home, done](const Raw &r) {
        Outcome o = outcome(r, QStringLiteral("MKCALENDAR"));
        const QString href = r.location.isEmpty() ? home.path() : r.location;
        done(o, href);
    });
}
