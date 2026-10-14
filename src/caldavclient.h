#pragma once

// A small asynchronous CalDAV client on QNetworkAccessManager, for the sync
// engine in src/caldavsync.h. It does only what that engine needs: find the
// calendars on a server (well-known, current-user-principal, calendar-home-set,
// then a Depth 1 listing that keeps VTODO collections), list etags, follow the
// RFC 6578 sync-collection token with a plain etag listing as fallback, fetch
// resources in one calendar-multiget, and write them back with PUT, DELETE and
// MKCALENDAR.
//
// Basic auth goes over HTTPS, or over plain HTTP to localhost only: nowhere
// else, so a password never travels in the clear to a real server. Every
// request has a timeout; a timeout, a dropped connection or a 5xx is retried
// with the same backoff the mail connector uses (30 s, doubling to 15 min).
// A 401 means a wrong password and is reported at once, and a 412 means the
// resource changed under us and is left for the engine to resolve.

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class CalDavClient : public QObject {
    Q_OBJECT

public:
    struct Calendar {
        QString href;             // The collection path, e.g. /calendars/jane/tasks/
        QString displayName;
        QString color;
        QString ctag;             // getctag, empty when the server has none.
        QString syncToken;        // Empty when the server offers no sync-collection.
        bool supportsVTodo = false;
    };

    struct Resource {
        QString etag;
        QByteArray data;
    };

    // The answer of one operation, before its parsed payload.
    struct Outcome {
        bool ok = false;
        int status = 0;           // HTTP status, 0 for a network error.
        QString error;            // Human-readable; empty on success.
        bool authFailed = false;  // 401/403: wrong password, do not retry.
        bool conflict = false;    // 412: the resource changed on the server.
        QByteArray body;
    };

    // Retry timings, overridable so tests do not wait half a minute.
    struct Timings {
        int requestTimeout = 30000;   // Per request, milliseconds.
        int backoffBase = 30000;      // First retry delay; doubles up to backoffMax.
        int backoffMax = 15 * 60000;
        int maxAttempts = 6;          // A retriable failure gives up after this many tries.
    };

    using DiscoverDone = std::function<void(const Outcome &, const QList<Calendar> &)>;
    using EtagsDone = std::function<void(const Outcome &, const QMap<QString, QString> &)>;
    using SyncDone = std::function<void(const Outcome &, const QStringList &changed, const QStringList &removed, const QString &newToken)>;
    using MultigetDone = std::function<void(const Outcome &, const QMap<QString, Resource> &)>;
    using PutDone = std::function<void(const Outcome &, const QString &etag)>;
    using RemoveDone = std::function<void(const Outcome &)>;
    using MkCalendarDone = std::function<void(const Outcome &, const QString &href)>;

    explicit CalDavClient(QObject *parent = nullptr);

    void setCredentials(const QString &user, const QString &password);
    void setTimings(const Timings &timings) { m_timings = timings; }
    Timings timings() const { return m_timings; }

    // The URL all relative hrefs are resolved against; discover() sets it from
    // the URL it was given. Exposed for tests and for the engine's own writes.
    void setBaseUrl(const QUrl &base) { m_base = base; }
    QUrl baseUrl() const { return m_base; }

    // Finds the calendars the user can reach from `url`. The URL may be the
    // server root, a calendar home, or a single calendar.
    void discover(const QUrl &url, DiscoverDone done);

    // PROPFIND Depth 1 for getetag, as href -> etag.
    void listEtags(const QString &calendarHref, EtagsDone done);
    // REPORT sync-collection; falls back to listEtags when the server refuses
    // it, in which case `newToken` is empty.
    void syncCollection(const QString &calendarHref, const QString &token, SyncDone done);
    // REPORT calendar-multiget, as href -> (etag, data).
    void multiget(const QString &calendarHref, const QStringList &hrefs, MultigetDone done);

    // Creates or updates a resource. `ifMatch` empty means a new resource
    // (If-None-Match: *); otherwise it is the etag to replace. On success
    // `etag` is the new one, fetched again when the server does not return it.
    void put(const QString &href, const QByteArray &data, const QString &ifMatch, PutDone done);
    void remove(const QString &href, const QString &ifMatch, RemoveDone done);
    void mkcalendar(const QString &homeHref, const QString &name, const QString &color, MkCalendarDone done);

signals:
    // A retriable failure is about to be retried after `delayMs`.
    void retrying(int attempt, int delayMs);

private:
    struct Raw {
        int status = 0;
        QByteArray body;
        QString etag;
        QString location;
        QString error;
        QUrl url;                 // The final URL, after redirects.
        bool timedOut = false;
        bool networkError = false;  // Timeout, dropped connection or 5xx.
    };
    using RawDone = std::function<void(const Raw &)>;

    struct DavItem {
        QString href;
        int status = 0;                   // The response's status, 404 for a removal.
        QMap<QString, QString> text;      // Property local name -> text.
        bool collection = false;
        bool calendar = false;
        QStringList components;           // supported-calendar-component-set.
    };

    void request(const QByteArray &verb, const QUrl &url, const QByteArray &body,
                 const QList<QPair<QByteArray, QByteArray>> &headers, RawDone done, int attempt = 0);
    void propfind(const QUrl &url, int depth, RawDone done);
    void discoverWellKnown(const QUrl &url, DiscoverDone done);
    void discoverPrincipal(const QUrl &url, const QUrl &fallback, DiscoverDone done);
    void discoverHome(const QUrl &url, const QUrl &fallback, DiscoverDone done);
    void discoverDirect(const QUrl &url, DiscoverDone done);
    void fetchEtag(const QString &href, std::function<void(const Outcome &, const QString &)> done);
    QUrl resolve(const QString &href) const;
    int backoffFor(int attempt) const;
    static bool isLoopback(const QString &host);
    static bool supportsVTodo(const DavItem &item);
    static QList<DavItem> parseItems(const QByteArray &body, QString *syncToken = nullptr);
    static QString itemText(const QList<DavItem> &items, const QString &name);
    static Calendar calendarFrom(const DavItem &item);
    static Outcome outcome(const Raw &r, const QString &what);

    QNetworkAccessManager m_network;
    QByteArray m_authorization;
    QUrl m_base;
    Timings m_timings;
};
