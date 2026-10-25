#pragma once

// The Calendar Day Bar's data source. On Linux
// the source is iCalendar, either local .ics files or http(s)/webcal URLs
// (the "secret address" every Google, Fastmail and Nextcloud calendar offers).
//
// Ics:: is a pure parser and recurrence expander with no Qt Quick or network
// dependencies, so it is unit-testable on its own. CalendarFeed wraps it with
// fetching, caching and the day-shaped data the QML bar wants.
//
// Integration: create ONE CalendarFeed in main.cpp and expose it to QML as the
// context property `calendar` (CalendarFeed::registerType() is also provided
// for anyone who prefers a declarative instance).

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QTimeZone>
#include <QTimer>
#include <QVariantList>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace Ics {

// One concrete event instance. start and end are in local time; for all-day
// events they are local midnights and end is exclusive (a one-day event ends
// at the next midnight), as in iCalendar itself.
struct Event {
    QString uid, title, location, calendar, description, url;
    QStringList declinedBy;  // Lower-cased addresses of attendees whose PARTSTAT is DECLINED.
    QColor color;
    QDateTime start, end;
    bool allDay = false;
};

// A parsed RRULE. Kept structured so expansion never re-reads text.
struct Rule {
    enum Frequency { None, Daily, Weekly, Monthly, Yearly };
    Frequency frequency = None;
    int interval = 1;
    int count = 0;       // 0 means unbounded.
    QDateTime until;     // Invalid means unbounded.
    int weekStart = 1;   // Qt::DayOfWeek, Monday unless WKST says otherwise.
    QList<QPair<int, int>> byDay;  // (ordinal, Qt::DayOfWeek); ordinal 0 means every one.
    QList<int> byMonthDay, byMonth;
};

// A VEVENT as written, before recurrence expansion. Recurring events must be
// expanded in the zone they were written in so "9 AM every Monday" stays at
// 9 AM across a daylight saving change; hence the wall-clock fields and zone
// alongside the already-converted `event` for the first occurrence.
struct Component {
    Event event;
    QTimeZone zone;
    QDate startDate;
    QTime startTime;
    qint64 durationSecs = 0;  // Timed events.
    int durationDays = 1;     // All-day events.
    Rule rule;
    QList<QDateTime> exdates;  // Instants; all-day ones are local midnights.
    bool hasRecurrenceId = false;
    QDateTime recurrenceId;    // The occurrence this component replaces.
    bool cancelled = false;    // Kept only for overrides, which must still hide their occurrence.
};

// The calendar's own name from X-WR-CALNAME (Google, Apple and Nextcloud all
// write it), or an empty string when the feed has none.
QString calendarName(const QByteArray &ics);

// Parses every VEVENT, dropping cancelled ones (except cancelled overrides).
QList<Component> parseComponents(const QByteArray &ics, const QString &calendarName);

// Convenience for callers that only want the events as written: each VEVENT's
// first occurrence, without expansion.
QList<Event> parse(const QByteArray &ics, const QString &calendarName);

// Every occurrence overlapping the local dates from..to inclusive, with
// RECURRENCE-ID overrides and EXDATEs applied, sorted by start.
QList<Event> occurrences(const QList<Component> &components, QDate from, QDate to);

}  // namespace Ics

class CalendarFeed : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList sources READ sources WRITE setSources NOTIFY sourcesChanged)
    Q_PROPERTY(QStringList palette READ palette WRITE setPalette NOTIFY paletteChanged)
    Q_PROPERTY(QStringList disabledSources READ disabledSources WRITE setDisabledSources NOTIFY sourceInfoChanged)
    Q_PROPERTY(QVariantMap sourceColors READ sourceColors WRITE setSourceColors NOTIFY sourceInfoChanged)
    Q_PROPERTY(QString myEmails READ myEmails WRITE setMyEmails NOTIFY changed)
    // Per source: {source, name, color, customColor, enabled}, for the settings pane.
    Q_PROPERTY(QVariantList sourceInfo READ sourceInfo NOTIFY sourceInfoChanged)
    Q_PROPERTY(QVariantList today READ today NOTIFY changed)
    Q_PROPERTY(QString caption READ caption NOTIFY changed)
    Q_PROPERTY(QColor captionColor READ captionColor NOTIFY changed)
    Q_PROPERTY(int nowMinute READ nowMinute NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit CalendarFeed(QObject *parent = nullptr);

    static void registerType();

    QStringList sources() const { return m_sources; }
    void setSources(const QStringList &sources);
    QStringList palette() const { return m_palette; }
    void setPalette(const QStringList &palette);
    // Sources switched off are kept configured but contribute no events.
    QStringList disabledSources() const { return m_disabled; }
    void setDisabledSources(const QStringList &sources);
    // Source -> colour name, overriding the palette colour a source would get.
    QVariantMap sourceColors() const { return m_colors; }
    void setSourceColors(const QVariantMap &colors);
    // Comma-separated addresses that count as "me" when deciding whether an
    // invitation was declined.
    QString myEmails() const { return m_myEmails; }
    void setMyEmails(const QString &emails);
    QVariantList sourceInfo() const;
    QVariantList today() const;
    QString caption() const;
    QColor captionColor() const;
    int nowMinute() const;
    QString error() const;

    // Events on one day as {title, start, end, allDay, color, location,
    // calendar}; start and end are minutes since midnight, clamped to the day.
    Q_INVOKABLE QVariantList eventsFor(const QString &isoDate) const;
    // Gaps of at least 30 minutes between timed events inside the window, as
    // {start, end, label}.
    Q_INVOKABLE QVariantList freeSlots(const QString &isoDate, int fromMinute, int toMinute) const;
    // Total free minutes in the window: the same timed events as freeSlots,
    // with overlapping ones merged and clipped, but no minimum gap, so short
    // openings still count. Used by My Day's capacity line.
    int freeMinutes(const QString &isoDate, int fromMinute, int toMinute) const;
    Q_INVOKABLE void refresh();

    // Feeds already-fetched iCalendar text for a source. refresh() uses it, and
    // tests call it directly to avoid touching disk or network.
    void loadData(const QString &source, const QByteArray &ics);
    // Replaces the clock so tests can pin "now".
    void setClock(std::function<QDateTime()> clock);

signals:
    void sourcesChanged();
    void paletteChanged();
    void sourceInfoChanged();
    void changed();
    void errorChanged();

private:
    QDateTime now() const;
    QList<Ics::Component> components() const;
    QVariantList eventMaps(const QDate &day) const;
    QVariantMap eventMap(const Ics::Event &e, const QDate &day) const;
    QStringList myAddresses() const;
    QColor colorFor(int index) const;
    void tick();
    void fetch(const QString &source);
    void fail(const QString &source, const QString &message);
    QString cachePath(const QString &source) const;

    QStringList m_sources, m_palette, m_disabled;
    QVariantMap m_colors;
    QString m_myEmails;
    QHash<QString, QString> m_names;
    QHash<QString, QList<Ics::Component>> m_data;
    QHash<QString, QString> m_errors;
    QHash<QString, QNetworkReply *> m_pending;
    QNetworkAccessManager *m_network = nullptr;
    QTimer m_refreshTimer, m_tickTimer;
    std::function<QDateTime()> m_clock;
    QDate m_lastDay;
};
