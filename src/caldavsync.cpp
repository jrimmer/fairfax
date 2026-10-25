#include "caldavsync.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonObject>
#include <QThread>

#include "diagnostics.h"
#include "keyring.h"
#include "store.h"
#include "theme.h"

namespace {

constexpr auto Provider = "caldav";
const QString ETasks = QStringLiteral("tasks");
const QString ELists = QStringLiteral("lists");
const QString KeyringService = QStringLiteral("fairfax");
const QString KeyringAccount = QStringLiteral("caldav");

// Keys in a link's snapshot beside the record's own fields. They carry the
// bookkeeping and are ignored when a snapshot is compared with its record.
const QString ExtraKey = QStringLiteral("__extra");
const QString EtagKey = QStringLiteral("__etag");
const QString CtagKey = QStringLiteral("__ctag");
const QString TokenKey = QStringLiteral("__token");
const QString MappedKey = QStringLiteral("__mapped");

QVariantMap extraToMap(const Vtodo::Extra &e) {
    QVariantMap m;
    m.insert("properties", e.properties);
    QVariantList components;
    for (const QStringList &lines : e.components)
        components.append(lines);
    m.insert("components", components);
    QVariantList alarms;
    for (const QStringList &lines : e.alarmProperties)
        alarms.append(lines);
    m.insert("alarmProperties", alarms);
    m.insert("meta", e.meta);
    m.insert("sequence", e.sequence);
    return m;
}

Vtodo::Extra extraFromMap(const QVariantMap &m) {
    Vtodo::Extra e;
    e.properties = m.value("properties").toStringList();
    for (const QVariant &v : m.value("components").toList())
        e.components << v.toStringList();
    for (const QVariant &v : m.value("alarmProperties").toList())
        e.alarmProperties << v.toStringList();
    e.meta = m.value("meta").toString();
    e.sequence = m.value("sequence", -1).toInt();
    return e;
}

// What a VTODO carries, without the parts that only order the local list and
// without the link's own bookkeeping. Equal projections mean a task and its
// snapshot would write the same resource. The map is normalised the way the
// link snapshot is (through JSON), so a null date compares the same either
// way.
QVariantMap projection(QVariantMap map) {
    // Snapshots from before tasks could be abandoned have no such field.
    map.insert(QStringLiteral("abandoned"), map.value(QStringLiteral("abandoned")).toBool());
    for (const char *key : {"id", "modified", "created", "sort_order", "collapsed", "series_uid", "deleted", "section_id"})
        map.remove(key);
    for (auto it = map.begin(); it != map.end();) {
        if (it.key().startsWith(QLatin1String("__")))
            it = map.erase(it);
        else
            ++it;
    }
    return QJsonObject::fromVariantMap(map).toVariantMap();
}

// A task as the resource would carry it: its fields plus its live alerts, as
// a sorted list so their order does not matter. Alerts live in their own
// table, so without them an alert-only change would look like no change.
QVariantMap localState(const Store &store, const Task &task) {
    QVariantMap map = projection(task.toMap());
    QStringList alarms;
    for (const Alarm &a : store.alarmsFor(task.id))
        alarms << QStringLiteral("%1|%2|%3").arg(a.kind).arg(a.offset).arg(a.at.isValid() ? a.at.toString(Qt::ISODate) : QString());
    alarms.sort();
    map.insert(QStringLiteral("alarms"), alarms);
    return QJsonObject::fromVariantMap(map).toVariantMap();
}

// The time of the newest local edit, counting alerts, which keep their own.
QDateTime localModified(const Store &store, const Task &task) {
    QDateTime newest = task.modified;
    for (const Alarm &a : store.alarmsFor(task.id))
        if (a.modified.isValid() && (!newest.isValid() || a.modified > newest))
            newest = a.modified;
    return newest;
}

void noteConflict(const QString &uid, const QDateTime &local, const QDateTime &remote, bool remoteWon) {
    Diagnostics::note(QStringLiteral("CalDAV conflict on %1: local %2, remote %3, %4 wins")
                          .arg(uid, local.toString(Qt::ISODate), remote.toString(Qt::ISODate), remoteWon ? "remote" : "local"));
}

QVariantMap withMeta(const QVariantMap &projected, const QString &etag, const Vtodo::Extra &extra) {
    QVariantMap map = projected;
    map.insert(EtagKey, etag);
    map.insert(ExtraKey, extraToMap(extra));
    return map;
}

QString capitalise(const QString &s) {
    if (s.isEmpty())
        return s;
    QString out = s;
    out[0] = out[0].toUpper();
    return out;
}

bool remoteWins(const QDateTime &local, const QDateTime &remote) {
    if (!remote.isValid())
        return false;
    if (!local.isValid())
        return true;
    return remote > local;
}

QString parentPath(const QString &href) {
    QString path = href;
    if (path.endsWith('/'))
        path.chop(1);
    return path.section('/', 0, -2) + '/';
}

}  // namespace

CalDavSync::CalDavSync(Store *store, Theme *theme, QObject *parent)
    : QObject(parent), m_store(store), m_theme(theme), m_client(new CalDavClient(this)) {
    m_periodic.setInterval(5 * 60 * 1000);
    connect(&m_periodic, &QTimer::timeout, this, [this] {
        if (configured() && SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    m_periodic.start();
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(20 * 1000);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (configured() && SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    connect(store, &Store::changed, this, [this] {
        if (m_applying || !configured() || !SyncTree::autoSyncEnabled(*m_store) || m_needsChoice)
            return;
        if (m_busy)
            m_again = true;
        else
            m_debounce.start();
    });
}

bool CalDavSync::configured() const {
    return !m_store->setting("caldav.url").toString().isEmpty()
           && !Keyring::lookup(KeyringService, KeyringAccount).isEmpty();
}

QDateTime CalDavSync::lastSync() const {
    return QDateTime::fromString(m_store->setting("caldav.lastSync").toString(), Qt::ISODate);
}

QString CalDavSync::accountName() const {
    const QString stored = m_store->setting("caldav.name").toString();
    return stored.isEmpty() ? accountNameFor(QUrl(m_store->setting("caldav.url").toString())) : stored;
}

QString CalDavSync::url() const { return m_store->setting("caldav.url").toString(); }

QString CalDavSync::user() const { return m_store->setting("caldav.user").toString(); }

QVariantList CalDavSync::calendars() const {
    QVariantList out;
    for (const CalDavClient::Calendar &c : m_calendars) {
        QVariantMap m;
        m.insert("href", c.href);
        m.insert("name", c.displayName.isEmpty() ? c.href : c.displayName);
        m.insert("color", c.color);
        out.append(m);
    }
    return out;
}

QVariantList CalDavSync::mappings() const {
    QVariantList out;
    const auto links = m_store->syncLinks(Provider, ELists);
    for (const auto &link : links) {
        const TaskList list = m_store->list(link.localId);
        if (!list.id || list.deleted)
            continue;
        QVariantMap m;
        m.insert("listId", list.id);
        m.insert("listName", list.name);
        m.insert("calendarHref", link.remoteId);
        out.append(m);
    }
    return out;
}

void CalDavSync::setTimings(const CalDavClient::Timings &timings) {
    m_client->setTimings(timings);
}

CalDavSync::Result CalDavSync::syncBlocking(SyncTree::FirstSync first) {
    m_finished = false;
    m_needsChoice = false;
    m_mode = m_store->setting("caldav.mapped", false).toBool() ? SyncTree::FirstSync::Merge : first;
    m_busy = false;
    syncNow();
    QElapsedTimer timer;
    timer.start();
    while (!m_finished && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return m_lastResult;
}

QString CalDavSync::accountNameFor(const QUrl &url) const {
    QString host = url.host();
    if (host.startsWith("www."))
        host = host.mid(4);
    const QString label = host.section('.', 0, 0);
    return label.isEmpty() ? QStringLiteral("CalDAV") : capitalise(label);
}

QString CalDavSync::homeHref() const {
    // A created calendar belongs beside the ones discovery found; without
    // any, the account's own root is the best guess.
    for (const CalDavClient::Calendar &c : m_calendars)
        if (!c.href.isEmpty())
            return parentPath(c.href);
    QString path = m_url.path();
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (!path.endsWith('/'))
        path += '/';
    return path;
}

QString CalDavSync::calendarColor(const TaskList &list) const {
    if (m_theme)
        return m_theme->listColor(list.color).name();
    static const char *const palette[] = {"#2F7FD8", "#7A62C7", "#3E9A4A", "#C2453B", "#E8892B", "#2A9DB0"};
    return QString::fromLatin1(palette[list.id % 6]);
}

// ------------------------------------------------------------------ account

void CalDavSync::connectAccount(const QString &url, const QString &user, const QString &password) {
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != "http" && parsed.scheme() != "https")) {
        m_status = "The CalDAV address must start with http:// or https://";
        emit stateChanged();
        return;
    }
    if (user.isEmpty() || password.isEmpty()) {
        m_status = "Enter the user name and app password";
        emit stateChanged();
        return;
    }
    QString error;
    if (!Keyring::store(KeyringService, KeyringAccount, password, &error)) {
        m_status = error;
        emit stateChanged();
        return;
    }
    m_cachedPassword = password;
    m_passwordLooked = true;
    m_store->setSetting("caldav.url", parsed.toString());
    m_store->setSetting("caldav.user", user);
    m_store->setSetting("caldav.name", accountNameFor(parsed));
    m_store->setSetting("caldav.lastSync", QString());
    m_store->removeSetting("caldav.mapped");
    m_store->removeSetting("caldav.replaceDone");
    m_store->clearSyncLinks(Provider);
    m_status = "Connected";
    emit stateChanged();
    if (!m_quietConnect)
        syncNow();
}

void CalDavSync::disconnectAccount() {
    Keyring::clear(KeyringService, KeyringAccount);
    m_cachedPassword.clear();
    m_passwordLooked = true;
    m_store->setSetting("caldav.url", QString());
    m_store->setSetting("caldav.user", QString());
    m_store->setSetting("caldav.name", QString());
    m_store->setSetting("caldav.lastSync", QString());
    m_store->removeSetting("caldav.mapped");
    m_store->removeSetting("caldav.replaceDone");
    // Forgetting the account never deletes a task on either side.
    m_store->clearSyncLinks(Provider);
    m_status = "Not connected";
    emit stateChanged();
}

// Lists set to "Don't sync" in the CalDAV mapping, by uid. This is CalDAV's
// own choice: the other providers keep syncing the list. A list excluded from
// every provider ("Don't Sync This List", SyncTree) is skipped here as well,
// and mapping it to a calendar never lifts that.
static const char *const CalDavExcludedSetting = "caldav.excludedLists";

static QSet<QString> calDavExcludedUids(const Store &store) {
    QSet<QString> out;
    for (const auto &u : store.setting(CalDavExcludedSetting).toString().split(',', Qt::SkipEmptyParts))
        out.insert(u.trimmed());
    return out;
}

static void setCalDavExcluded(Store &store, qint64 listId, bool excluded) {
    const QString uid = store.list(listId).uid;
    if (uid.isEmpty())
        return;
    QSet<QString> uids = calDavExcludedUids(store);
    if (excluded)
        uids.insert(uid);
    else
        uids.remove(uid);
    QStringList sorted(uids.begin(), uids.end());
    sorted.sort();
    store.setSetting(CalDavExcludedSetting, sorted.join(','));
}

// Every list this provider skips: excluded everywhere, or just from CalDAV.
static QSet<qint64> calDavSkippedListIds(const Store &store) {
    QSet<qint64> out = SyncTree::excludedListIds(store);
    const QSet<QString> uids = calDavExcludedUids(store);
    if (!uids.isEmpty())
        for (const auto &l : store.lists())
            if (uids.contains(l.uid))
                out.insert(l.id);
    return out;
}

bool CalDavSync::isListSkipped(qint64 listId) const { return calDavSkippedListIds(*m_store).contains(listId); }

void CalDavSync::setCalendarForList(qint64 listId, const QString &calendarHref) {
    const TaskList list = m_store->list(listId);
    if (!list.id)
        return;
    if (calendarHref.isEmpty()) {
        // "Don't sync": unmapped and skipped by CalDAV only, so no sync
        // recreates the calendar for it; other providers are unaffected.
        m_store->removeSyncLink(Provider, ELists, listId);
        setCalDavExcluded(*m_store, listId, true);
        emit stateChanged();
        return;
    }
    setCalDavExcluded(*m_store, listId, false);
    // One calendar to one list: drop any other list pointed at it.
    const auto links = m_store->syncLinks(Provider, ELists);
    for (const auto &link : links)
        if (link.remoteId == calendarHref && link.localId != listId)
            m_store->removeSyncLink(Provider, ELists, link.localId);
    Store::SyncLink link;
    link.localId = listId;
    link.remoteId = calendarHref;
    QVariantMap snap;
    snap.insert(MappedKey, true);
    for (const CalDavClient::Calendar &c : m_calendars)
        if (c.href == calendarHref) {
            snap.insert(CtagKey, c.ctag);
            snap.insert(TokenKey, c.syncToken);
        }
    link.snapshot = snap;
    m_store->setSyncLink(Provider, ELists, link);
    emit stateChanged();
}

void CalDavSync::refreshCalendars() {
    if (!configured())
        return;
    m_url = QUrl(m_store->setting("caldav.url").toString());
    m_cachedPassword = Keyring::lookup(KeyringService, KeyringAccount);
    m_passwordLooked = true;
    m_client->setCredentials(m_store->setting("caldav.user").toString(), m_cachedPassword);
    m_client->discover(m_url, [this](const CalDavClient::Outcome &o, const QList<CalDavClient::Calendar> &c) {
        if (o.ok)
            m_calendars = c;
        emit stateChanged();
    });
}

void CalDavSync::createCalendarForList(qint64 listId) {
    const TaskList list = m_store->list(listId);
    if (!list.id || !configured())
        return;
    m_url = QUrl(m_store->setting("caldav.url").toString());
    m_cachedPassword = Keyring::lookup(KeyringService, KeyringAccount);
    m_passwordLooked = true;
    m_client->setCredentials(m_store->setting("caldav.user").toString(), m_cachedPassword);
    // Discover first when the calendar list is not known yet, so homeHref()
    // points beside the calendars the account has.
    auto make = [this, list] {
        const QString name = list.name;
        const QString color = calendarColor(list);
        m_client->mkcalendar(homeHref(), name, color, [this, listId = list.id, name, color](const CalDavClient::Outcome &o, const QString &href) {
            if (!o.ok) {
                m_status = o.error.isEmpty() ? QStringLiteral("Could not create the calendar") : o.error;
                emit stateChanged();
                return;
            }
            CalDavClient::Calendar calendar;
            calendar.href = href;
            calendar.displayName = name;
            calendar.color = color;
            calendar.supportsVTodo = true;
            m_calendars.append(calendar);
            setCalendarForList(listId, href);
            m_status = QStringLiteral("Created %1").arg(name);
            emit stateChanged();
        });
    };
    if (!m_calendars.isEmpty()) {
        make();
        return;
    }
    m_client->discover(m_url, [this, make](const CalDavClient::Outcome &o, const QList<CalDavClient::Calendar> &c) {
        if (o.ok)
            m_calendars = c;
        make();
    });
}

// ------------------------------------------------------------------ the run

void CalDavSync::syncNow() {
    if (!configured()) {
        m_status = "Not connected to CalDAV";
        emit stateChanged();
        return;
    }
    if (m_busy) {
        m_again = true;
        return;
    }
    if (m_needsChoice)
        return;
    m_debounce.stop();
    m_busy = true;
    m_status = "Syncing…";
    emit stateChanged();
    QTimer::singleShot(0, this, [this] { startRun(); });
}

void CalDavSync::chooseFirstSync(const QString &choice) {
    if (!m_needsChoice)
        return;
    if (choice == "local")
        m_mode = SyncTree::FirstSync::ReplaceLocal;
    else if (choice == "server")
        m_mode = SyncTree::FirstSync::ReplaceServer;
    else
        m_mode = SyncTree::FirstSync::Merge;
    m_needsChoice = false;
    syncNow();
}

void CalDavSync::startRun() {
    m_applying = true;
    m_in = m_out = 0;
    m_writeError.clear();
    m_url = QUrl(m_store->setting("caldav.url").toString());
    m_accountName = accountName();
    m_client->setBaseUrl(m_url);
    if (m_passwordLooked && !m_cachedPassword.isEmpty()) {
        m_client->setCredentials(m_store->setting("caldav.user").toString(), m_cachedPassword);
    } else {
        m_cachedPassword = Keyring::lookup(KeyringService, KeyringAccount);
        m_passwordLooked = true;
        m_client->setCredentials(m_store->setting("caldav.user").toString(), m_cachedPassword);
    }
    m_client->discover(m_url, [this](const CalDavClient::Outcome &o, const QList<CalDavClient::Calendar> &c) {
        onDiscovered(o, c);
    });
}

void CalDavSync::onDiscovered(const CalDavClient::Outcome &outcome, const QList<CalDavClient::Calendar> &calendars) {
    if (outcome.authFailed && outcome.status == 401) {
        fail(QStringLiteral("The CalDAV server rejected the user name or password"));
        return;
    }
    if (!outcome.ok) {
        fail(outcome.error.isEmpty() ? QStringLiteral("CalDAV discovery failed") : outcome.error);
        return;
    }
    m_calendars = calendars;
    if (m_store->setting("caldav.mapped", false).toBool()) {
        buildMapping();
        return;
    }
    startFirstCheck();
}

// When both sides already hold tasks the first sync asks how to start, the
// same question the folder and WebDAV providers ask.
void CalDavSync::startFirstCheck() {
    if (m_mode == SyncTree::FirstSync::ReplaceServer) {
        buildMapping();
        return;
    }
    bool localHasTasks = false;
    for (const auto &t : m_store->tasks())
        if (!t.deleted) {
            localHasTasks = true;
            break;
        }
    if (!localHasTasks) {
        buildMapping();
        return;
    }
    m_probeIndex = 0;
    m_remoteHasTasks = false;
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, step] {
        if (m_remoteHasTasks || m_probeIndex >= m_calendars.size()) {
            if (m_remoteHasTasks && m_mode == SyncTree::FirstSync::Ask) {
                m_applying = false;
                m_busy = false;
                m_finished = true;
                m_lastResult = {false, true, 0, QString()};
                m_needsChoice = true;
                m_status = "This machine and the server both have tasks: choose how to start";
                emit stateChanged();
                return;
            }
            if (m_remoteHasTasks && m_mode == SyncTree::FirstSync::ReplaceLocal) {
                const QString dir = QFileInfo(m_store->path()).absolutePath() + "/backups/";
                const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd-HH_mm_ss");
                QString backupPath = dir + QStringLiteral("Fairfax_(CalDAV)_%1_before-sync.db").arg(stamp);
                // VACUUM INTO refuses an existing file, so two within a second get a suffix.
                for (int n = 2; QFileInfo::exists(backupPath); ++n)
                    backupPath = dir + QStringLiteral("Fairfax_(CalDAV)_%1-%2_before-sync.db").arg(stamp).arg(n);
                QString backupError;
                if (!m_store->backupTo(backupPath, &backupError)) {
                    // Without a copy, Replace Local would lose this machine's tasks.
                    m_mode = SyncTree::FirstSync::Ask;
                    fail(QStringLiteral("Could not back up before replacing local tasks, nothing changed: %1").arg(backupError));
                    return;
                }
                SyncTree::wipeLocal(*m_store);
            }
            buildMapping();
            return;
        }
        const QString href = m_calendars.at(m_probeIndex).href;
        m_client->listEtags(href, [this, step](const CalDavClient::Outcome &, const QMap<QString, QString> &etags) {
            if (!etags.isEmpty())
                m_remoteHasTasks = true;
            ++m_probeIndex;
            (*step)();
        });
    };
    (*step)();
}

void CalDavSync::buildMapping() {
    const auto listLinks = m_store->syncLinks(Provider, ELists);
    const QSet<qint64> excluded = calDavSkippedListIds(*m_store);

    QSet<QString> excludedNames;
    for (const auto &l : m_store->lists())
        if (!l.deleted && excluded.contains(l.id))
            excludedNames.insert(l.name.toLower());

    QHash<QString, qint64> hrefToList;
    QSet<QString> excludedHrefs;
    for (const auto &link : listLinks) {
        const TaskList list = m_store->list(link.localId);
        if (!list.id || list.deleted || link.remoteId.isEmpty()) {
            m_store->removeSyncLink(Provider, ELists, link.localId);
            continue;
        }
        if (excluded.contains(link.localId)) {
            excludedHrefs.insert(link.remoteId);
            m_store->removeSyncLink(Provider, ELists, link.localId);
            continue;
        }
        hrefToList.insert(link.remoteId, link.localId);
    }

    auto accountGroup = [&]() -> qint64 {
        for (const auto &g : m_store->groups())
            if (!g.deleted && g.name.compare(m_accountName, Qt::CaseInsensitive) == 0)
                return g.id;
        ListGroup g;
        g.id = m_store->nextId("list_groups");
        g.name = m_accountName;
        g.order = g.id;
        m_store->begin("CalDAV list group", false);
        m_store->putGroup(g);
        m_store->commit();
        return g.id;
    };

    QSet<qint64> unmapped;
    const QSet<qint64> mappedIds(hrefToList.cbegin(), hrefToList.cend());
    for (const auto &l : m_store->lists())
        if (!l.deleted && l.kind == ListKind::Normal && !excluded.contains(l.id) && !mappedIds.contains(l.id))
            unmapped.insert(l.id);

    // Pair lists and calendars by name, case-insensitively, before creating.
    for (const CalDavClient::Calendar &calendar : m_calendars) {
        if (hrefToList.contains(calendar.href) || calendar.displayName.isEmpty())
            continue;
        qint64 match = 0;
        for (const auto &l : m_store->lists())
            if (unmapped.contains(l.id) && l.name.compare(calendar.displayName, Qt::CaseInsensitive) == 0) {
                match = l.id;
                break;
            }
        if (!match)
            continue;
        Store::SyncLink link{match, calendar.href, {}};
        QVariantMap snap;
        snap.insert(MappedKey, true);
        link.snapshot = snap;
        m_store->setSyncLink(Provider, ELists, link);
        hrefToList.insert(calendar.href, match);
        unmapped.remove(match);
    }

    // Calendars without a list become lists in a group named after the
    // account, unless their name belongs to an excluded list.
    for (const CalDavClient::Calendar &calendar : m_calendars) {
        if (hrefToList.contains(calendar.href))
            continue;
        if (excludedHrefs.contains(calendar.href) || excludedNames.contains(calendar.displayName.toLower()))
            continue;
        TaskList list;
        list.id = m_store->nextId("lists");
        list.name = calendar.displayName.isEmpty() ? calendar.href : calendar.displayName;
        list.color = int(m_calendars.size()) % 13;
        list.order = list.id;
        list.groupId = accountGroup();
        m_store->begin("CalDAV list", false);
        m_store->putList(list);
        m_store->commit();
        Store::SyncLink link{list.id, calendar.href, {}};
        QVariantMap snap;
        snap.insert(MappedKey, true);
        link.snapshot = snap;
        m_store->setSyncLink(Provider, ELists, link);
        hrefToList.insert(calendar.href, list.id);
    }

    // Lists still without a calendar: create one when the setting allows.
    m_pendingCreate.clear();
    const bool createCalendars = m_store->setting("caldav.createCalendars", true).toBool();
    for (qint64 listId : unmapped) {
        if (!createCalendars)
            continue;
        const TaskList list = m_store->list(listId);
        if (list.groupId == 0) {
            TaskList updated = list;
            updated.groupId = accountGroup();
            m_store->begin("CalDAV list", false);
            m_store->putList(updated);
            m_store->commit();
        }
        m_pendingCreate << listId;
    }

    mapCalendarsIntoStore();
    m_store->setSetting("caldav.mapped", true);

    if (!m_pendingCreate.isEmpty())
        createNextCalendar();
    else
        afterMapping();
}

void CalDavSync::mapCalendarsIntoStore() {
    m_states.clear();
    const auto links = m_store->syncLinks(Provider, ELists);
    for (const auto &link : links) {
        const TaskList list = m_store->list(link.localId);
        if (!list.id || list.deleted || link.remoteId.isEmpty())
            continue;
        ListState state;
        state.listId = link.localId;
        state.href = link.remoteId;
        state.ctag = link.snapshot.value(CtagKey).toString();
        state.token = link.snapshot.value(TokenKey).toString();
        m_states.append(state);
    }
}

void CalDavSync::createNextCalendar() {
    if (m_pendingCreate.isEmpty()) {
        mapCalendarsIntoStore();
        afterMapping();
        return;
    }
    const qint64 listId = m_pendingCreate.takeFirst();
    const TaskList list = m_store->list(listId);
    const QString name = list.name;
    const QString color = calendarColor(list);
    m_client->mkcalendar(homeHref(), name, color, [this, listId, name, color](const CalDavClient::Outcome &o, const QString &href) {
        if (!o.ok) {
            // A calendar that cannot be created leaves the list local; the
            // other calendars still sync.
            createNextCalendar();
            return;
        }
        CalDavClient::Calendar calendar;
        calendar.href = href;
        calendar.displayName = name;
        calendar.color = color;
        calendar.supportsVTodo = true;
        m_calendars.append(calendar);
        Store::SyncLink link;
        link.localId = listId;
        link.remoteId = href;
        QVariantMap snap;
        snap.insert(MappedKey, true);
        link.snapshot = snap;
        m_store->setSyncLink(Provider, ELists, link);
        createNextCalendar();
    });
}

void CalDavSync::afterMapping() {
    if (m_mode == SyncTree::FirstSync::ReplaceServer && !m_store->setting("caldav.replaceDone", false).toBool()) {
        m_store->setSetting("caldav.replaceDone", true);
        wipeRemote();
        return;
    }
    m_mode = SyncTree::FirstSync::Ask;
    m_store->removeSetting("caldav.replaceDone");
    computeDirty();
    m_stateIndex = 0;
    processNextCalendar();
}

// "Replace Server" empties every mapped calendar before the store is pushed.
void CalDavSync::wipeRemote() {
    m_wipeHrefs.clear();
    m_wipeIndex = 0;
    m_probeIndex = 0;
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, step] {
        if (m_probeIndex >= m_states.size()) {
            m_wipeIndex = 0;
            auto removeNext = std::make_shared<std::function<void()>>();
            *removeNext = [this, removeNext] {
                if (m_wipeIndex >= m_wipeHrefs.size()) {
                    afterMapping();
                    return;
                }
                const QString href = m_wipeHrefs.at(m_wipeIndex++);
                m_client->remove(href, QString(), [this, removeNext](const CalDavClient::Outcome &) { (*removeNext)(); });
            };
            (*removeNext)();
            return;
        }
        const QString href = m_states.at(m_probeIndex).href;
        m_client->listEtags(href, [this, step](const CalDavClient::Outcome &, const QMap<QString, QString> &etags) {
            m_wipeHrefs += etags.keys();
            ++m_probeIndex;
            (*step)();
        });
    };
    (*step)();
}

// ------------------------------------------------------------ calendar cycle

void CalDavSync::processNextCalendar() {
    if (m_stateIndex >= m_states.size()) {
        refreshCtags();
        return;
    }
    m_job = m_states.at(m_stateIndex);
    m_pullChanged.clear();
    m_pullRemoved.clear();
    m_ops.clear();
    m_opIndex = 0;
    const bool dirty = m_dirty.contains(m_job.listId);
    if (!dirty && !m_job.ctag.isEmpty()) {
        for (const CalDavClient::Calendar &c : m_calendars)
            if (c.href == m_job.href && !c.ctag.isEmpty() && c.ctag == m_job.ctag) {
                // Nothing changed on either side: skip the calendar.
                ++m_stateIndex;
                processNextCalendar();
                return;
            }
    }
    pullRemote();
}

void CalDavSync::pullRemote() {
    bool supportsSync = false;
    for (const CalDavClient::Calendar &c : m_calendars)
        if (c.href == m_job.href && !c.syncToken.isEmpty())
            supportsSync = true;
    if (!supportsSync) {
        listRemoteEtags();
        return;
    }
    m_client->syncCollection(m_job.href, m_job.token,
                             [this](const CalDavClient::Outcome &o, const QStringList &ch, const QStringList &rm, const QString &t) {
                                 onSyncCollection(o, ch, rm, t);
                             });
}

void CalDavSync::onSyncCollection(const CalDavClient::Outcome &outcome, const QStringList &changed,
                                  const QStringList &removed, const QString &newToken) {
    if (!outcome.ok) {
        fail(outcome.error);
        return;
    }
    if (newToken.isEmpty()) {
        listRemoteEtags();
        return;
    }
    m_job.token = newToken;
    m_pullChanged = changed;
    m_pullRemoved = removed;
    fetchResources(changed);
}

void CalDavSync::listRemoteEtags() {
    m_client->listEtags(m_job.href, [this](const CalDavClient::Outcome &o, const QMap<QString, QString> &etags) {
        onEtags(o, etags);
    });
}

void CalDavSync::onEtags(const CalDavClient::Outcome &outcome, const QMap<QString, QString> &etags) {
    if (!outcome.ok) {
        fail(outcome.error);
        return;
    }
    QHash<QString, Store::SyncLink> byHref;
    const auto links = m_store->syncLinks(Provider, ETasks);
    for (const auto &link : links)
        if (link.remoteId.startsWith(m_job.href))
            byHref.insert(link.remoteId, link);

    for (auto it = etags.begin(); it != etags.end(); ++it) {
        const auto link = byHref.constFind(it.key());
        if (link == byHref.constEnd() || link->snapshot.value(EtagKey).toString() != it.value())
            m_pullChanged << it.key();
    }
    for (auto it = byHref.begin(); it != byHref.end(); ++it)
        if (!etags.contains(it.key()))
            m_pullRemoved << it.key();
    fetchResources(m_pullChanged);
}

void CalDavSync::fetchResources(const QStringList &hrefs) {
    if (hrefs.isEmpty()) {
        for (const QString &href : m_pullRemoved)
            applyRemoteRemoval(href);
        pushLocal();
        return;
    }
    m_client->multiget(m_job.href, hrefs,
                       [this](const CalDavClient::Outcome &o, const QMap<QString, CalDavClient::Resource> &resources) {
                           onMultiget(o, resources);
                       });
}

void CalDavSync::onMultiget(const CalDavClient::Outcome &outcome, const QMap<QString, CalDavClient::Resource> &resources) {
    if (!outcome.ok) {
        fail(outcome.error);
        return;
    }
    QSet<QString> got;
    for (auto it = resources.begin(); it != resources.end(); ++it) {
        got.insert(it.key());
        applyRemoteResource(it.key(), it.value().etag, it.value().data);
    }
    for (const QString &href : m_pullRemoved)
        if (!got.contains(href))
            applyRemoteRemoval(href);

    // Parents after every task exists, so a child can point at its parent.
    m_applying = true;
    m_store->begin("CalDAV sync", false, true);
    for (auto it = resources.begin(); it != resources.end(); ++it) {
        const Vtodo::Parsed parsed = Vtodo::fromIcs(it.value().data);
        if (parsed.parentUid.isEmpty())
            continue;
        qint64 childId = 0, parentId = 0;
        for (const auto &t : m_store->tasks()) {
            if (t.deleted)
                continue;
            if (t.uid == parsed.uid)
                childId = t.id;
            if (t.uid == parsed.parentUid)
                parentId = t.id;
        }
        if (childId && parentId && m_store->task(childId).parentId != parentId) {
            Task child = m_store->task(childId);
            child.parentId = parentId;
            m_store->putTask(child);
        }
    }
    m_store->commit();
    m_applying = false;
    pushLocal();
}

qint64 CalDavSync::applyRemoteResource(const QString &href, const QString &etag, const QByteArray &data) {
    const Vtodo::Parsed parsed = Vtodo::fromIcs(data);
    const Task remote = parsed.task;
    const auto links = m_store->syncLinks(Provider, ETasks);

    qint64 id = 0;
    for (const auto &link : links)
        if (link.remoteId == href) {
            id = link.localId;
            break;
        }
    if (!id && !parsed.uid.isEmpty())
        for (const auto &t : m_store->tasks())
            if (!t.deleted && t.uid == parsed.uid) {
                id = t.id;
                break;
            }

    const bool created = !id;
    if (created)
        id = m_store->nextId("tasks");
    Task local = created ? Task() : m_store->task(id);
    if (local.deleted)
        return id;   // Its deletion is waiting to be pushed.

    const Store::SyncLink *link = nullptr;
    for (const auto &l : links)
        if (l.localId == id) {
            link = &l;
            break;
        }
    const bool locallyChanged = created ? false : (!link || localState(*m_store, local) != projection(link->snapshot));
    if (!created && locallyChanged) {
        const bool remoteWon = remoteWins(localModified(*m_store, local), remote.modified);
        noteConflict(local.uid, localModified(*m_store, local), remote.modified, remoteWon);
        if (!remoteWon)
            return id;   // Local wins: it is pushed below.
    }

    m_applying = true;
    m_store->begin("CalDAV sync", false, true);
    Task merged = created ? Task() : local;
    merged.id = id;
    merged.uid = local.uid.isEmpty() ? (parsed.uid.isEmpty() ? Store::newUid() : parsed.uid) : local.uid;
    merged.listId = m_job.listId;
    merged.title = remote.title;
    merged.notes = remote.notes;
    merged.priority = remote.priority;
    merged.starred = remote.starred;
    merged.startDate = remote.startDate;
    merged.startTime = remote.startTime;
    merged.dueDate = remote.dueDate;
    merged.dueTime = remote.dueTime;
    merged.duration = remote.duration;
    merged.completed = remote.completed;
    merged.completedAt = remote.completedAt;
    merged.abandoned = remote.abandoned;
    merged.type = remote.type;
    merged.actionType = remote.actionType;
    merged.actionValue = remote.actionValue;
    merged.recurrence = remote.recurrence;
    if (created) {
        merged.created = remote.created.isValid() ? remote.created : QDateTime::currentDateTime();
        merged.parentId = 0;
    }
    if (!parsed.parentUid.isEmpty())
        for (const auto &t : m_store->tasks())
            if (!t.deleted && t.uid == parsed.parentUid)
                merged.parentId = t.id;
    merged.tagIds = tagIdsFor(parsed.tagNames);
    merged.locationIds = locationIdsFor(parsed.locationName);
    merged.modified = remote.modified.isValid() ? remote.modified : QDateTime::currentDateTime();
    merged.deleted = false;
    m_store->putTask(merged);
    applyAlarms(id, parsed.alarms);
    storeTaskLink(id, href, etag, parsed.extra);
    m_store->commit();
    m_applying = false;
    ++m_in;
    return id;
}

void CalDavSync::applyRemoteRemoval(const QString &href) {
    const auto links = m_store->syncLinks(Provider, ETasks);
    qint64 id = 0;
    for (const auto &link : links)
        if (link.remoteId == href) {
            id = link.localId;
            break;
        }
    if (!id)
        return;
    Task task = m_store->task(id);
    if (!task.id)
        return;
    const Store::SyncLink *link = nullptr;
    for (const auto &l : links)
        if (l.localId == id) {
            link = &l;
            break;
        }
    const bool locallyChanged = link && localState(*m_store, task) != projection(link->snapshot);
    if (!task.deleted && locallyChanged) {
        // Edited since the last sync: upload it again as a new resource.
        m_store->removeSyncLink(Provider, ETasks, id);
        return;
    }
    if (!task.deleted) {
        m_applying = true;
        m_store->begin("CalDAV sync", false, true);
        task.deleted = true;
        task.modified = QDateTime::currentDateTime();
        m_store->putTask(task);
        m_store->commit();
        m_applying = false;
        ++m_in;
    }
    m_store->removeSyncLink(Provider, ETasks, id);
}

void CalDavSync::computeDirty() {
    m_dirty.clear();
    QSet<qint64> mapped;
    for (const ListState &state : m_states)
        mapped.insert(state.listId);
    QHash<qint64, Store::SyncLink> byId;
    const auto links = m_store->syncLinks(Provider, ETasks);
    for (const auto &link : links)
        byId.insert(link.localId, link);
    for (const auto &t : m_store->tasks()) {
        if (t.deleted) {
            if (byId.contains(t.id))
                m_dirty.insert(t.listId);
            continue;
        }
        if (!mapped.contains(t.listId))
            continue;
        const auto link = byId.constFind(t.id);
        if (link == byId.constEnd() || localState(*m_store, t) != projection(link->snapshot))
            m_dirty.insert(t.listId);
    }
}

void CalDavSync::pushLocal() {
    m_ops.clear();
    m_opIndex = 0;
    QHash<qint64, Store::SyncLink> byId;
    const auto links = m_store->syncLinks(Provider, ETasks);
    for (const auto &link : links)
        byId.insert(link.localId, link);

    // Deletions first, so a freed href can be reused.
    for (const auto &link : links) {
        if (!link.remoteId.startsWith(m_job.href))
            continue;
        const Task task = m_store->task(link.localId);
        if (task.id && task.deleted) {
            Op op;
            op.remove = true;
            op.taskId = task.id;
            op.href = link.remoteId;
            op.etag = link.snapshot.value(EtagKey).toString();
            m_ops.append(op);
        }
    }

    for (const auto &t : m_store->tasks()) {
        if (t.listId != m_job.listId || t.deleted)
            continue;
        const auto link = byId.constFind(t.id);
        Op op;
        op.taskId = t.id;
        if (link == byId.constEnd()) {
            const QString uid = t.uid.isEmpty() ? Store::newUid() : t.uid;
            op.href = m_job.href + QString::fromUtf8(QUrl::toPercentEncoding(uid)) + ".ics";
            m_ops.append(op);
            continue;
        }
        if (localState(*m_store, t) == projection(link->snapshot))
            continue;
        op.extra = extraFromMap(link->snapshot.value(ExtraKey).toMap());
        if (link->remoteId.startsWith(m_job.href)) {
            op.update = true;
            op.href = link->remoteId;
            op.etag = link->snapshot.value(EtagKey).toString();
        } else {
            // A move: create the resource in this calendar, then delete the
            // one in the old calendar.
            const QString uid = t.uid.isEmpty() ? Store::newUid() : t.uid;
            op.href = m_job.href + QString::fromUtf8(QUrl::toPercentEncoding(uid)) + ".ics";
            op.oldHref = link->remoteId;
            op.oldEtag = link->snapshot.value(EtagKey).toString();
        }
        m_ops.append(op);
    }
    nextOp();
}

void CalDavSync::nextOp() {
    if (m_opIndex >= m_ops.size()) {
        afterCalendar();
        return;
    }
    runOp(m_ops.at(m_opIndex));
}

void CalDavSync::runOp(const Op &op) {
    if (op.remove) {
        m_client->remove(op.href, op.etag, [this, op](const CalDavClient::Outcome &o) { onRemove(op, o); });
        return;
    }
    const Task task = m_store->task(op.taskId);
    const QByteArray body = Vtodo::toIcs(*m_store, task, op.extra);

    // A linked resource with an unknown etag is not new: learn the etag from
    // the listing first. Never send If-None-Match: * for one.
    if (op.update && op.etag.isEmpty()) {
        m_client->listEtags(m_job.href, [this, op, body](const CalDavClient::Outcome &o, const QMap<QString, QString> &etags) {
            if (o.ok && etags.contains(op.href) && !etags.value(op.href).isEmpty()) {
                Op resolved = op;
                resolved.etag = etags.value(op.href);
                m_client->put(resolved.href, body, resolved.etag, [this, resolved](const CalDavClient::Outcome &o2, const QString &e) {
                    onPut(resolved, o2, e);
                });
                return;
            }
            if (o.ok && !etags.contains(op.href)) {
                // The resource is gone: upload it fresh.
                Op fresh = op;
                fresh.update = false;
                m_client->put(fresh.href, body, QString(), [this, fresh](const CalDavClient::Outcome &o2, const QString &e) {
                    onPut(fresh, o2, e);
                });
                return;
            }
            // The etag is still unknown: leave it for the next sync rather
            // than risk creating a duplicate.
            ++m_opIndex;
            nextOp();
        });
        return;
    }
    m_client->put(op.href, body, op.etag, [this, op](const CalDavClient::Outcome &outcome, const QString &etag) {
        onPut(op, outcome, etag);
    });
}

void CalDavSync::onPut(const Op &op, const CalDavClient::Outcome &outcome, const QString &etag) {
    if (!outcome.ok) {
        if (outcome.conflict) {
            resolvePutConflict(op);
            return;
        }
        if (outcome.authFailed) {
            if (m_writeError.isEmpty())
                m_writeError = QStringLiteral("No permission to write to %1").arg(m_job.href);
            ++m_opIndex;
            nextOp();
            return;
        }
        fail(outcome.error);
        return;
    }
    storeTaskLink(op.taskId, op.href, etag, op.extra);
    if (!op.oldHref.isEmpty()) {
        // The resource now lives in the new calendar; remove the old one.
        m_client->remove(op.oldHref, op.oldEtag, [](const CalDavClient::Outcome &) {});
    }
    ++m_out;
    ++m_opIndex;
    nextOp();
}

void CalDavSync::onRemove(const Op &op, const CalDavClient::Outcome &outcome) {
    if (outcome.ok) {
        m_store->removeSyncLink(Provider, ETasks, op.taskId);
        ++m_out;
        ++m_opIndex;
        nextOp();
        return;
    }
    if (outcome.conflict) {
        m_client->multiget(m_job.href, {op.href}, [this, op](const CalDavClient::Outcome &o, const QMap<QString, CalDavClient::Resource> &resources) {
            if (!o.ok || resources.isEmpty()) {
                m_store->removeSyncLink(Provider, ETasks, op.taskId);
                ++m_opIndex;
                nextOp();
                return;
            }
            const CalDavClient::Resource resource = resources.begin().value();
            const Vtodo::Parsed parsed = Vtodo::fromIcs(resource.data);
            Task local = m_store->task(op.taskId);
            if (!remoteWins(localModified(*m_store, local), parsed.task.modified)) {
                // The deletion is newer: delete again with the fresh etag.
                m_client->remove(op.href, resource.etag, [](const CalDavClient::Outcome &) {});
                m_store->removeSyncLink(Provider, ETasks, op.taskId);
            } else {
                // The remote edit is newer: it comes back locally.
                m_applying = true;
                m_store->begin("CalDAV sync", false, true);
                local.deleted = false;
                local.modified = parsed.task.modified.isValid() ? parsed.task.modified : QDateTime::currentDateTime();
                m_store->putTask(local);
                m_store->commit();
                m_applying = false;
                storeTaskLink(op.taskId, op.href, resource.etag, parsed.extra);
            }
            ++m_opIndex;
            nextOp();
        });
        return;
    }
    if (outcome.authFailed) {
        if (m_writeError.isEmpty())
            m_writeError = QStringLiteral("No permission to write to %1").arg(m_job.href);
        ++m_opIndex;
        nextOp();
        return;
    }
    fail(outcome.error);
}

void CalDavSync::resolvePutConflict(const Op &op) {
    if (op.retried) {
        fail(QStringLiteral("A CalDAV resource kept changing while Fairfax wrote it"));
        return;
    }
    m_client->multiget(m_job.href, {op.href}, [this, op](const CalDavClient::Outcome &o, const QMap<QString, CalDavClient::Resource> &resources) {
        if (!o.ok) {
            fail(o.error);
            return;
        }
        Task local = m_store->task(op.taskId);
        if (resources.isEmpty()) {
            // Deleted on the server meanwhile: upload it fresh.
            Op fresh = op;
            fresh.update = false;
            fresh.etag.clear();
            fresh.retried = true;
            const QByteArray body = Vtodo::toIcs(*m_store, local, op.extra);
            m_client->put(fresh.href, body, QString(), [this, fresh](const CalDavClient::Outcome &o2, const QString &e) {
                onPut(fresh, o2, e);
            });
            return;
        }
        const CalDavClient::Resource resource = resources.begin().value();
        const Vtodo::Parsed parsed = Vtodo::fromIcs(resource.data);
        if (remoteWins(localModified(*m_store, local), parsed.task.modified)) {
            // Adopt the server's version and stop.
            applyRemoteResource(op.href, resource.etag, resource.data);
            ++m_opIndex;
            nextOp();
            return;
        }
        // Local is still newer: retry once against the fresh etag.
        noteConflict(local.uid, localModified(*m_store, local), parsed.task.modified, false);
        Op retry = op;
        retry.update = true;
        retry.etag = resource.etag;
        retry.retried = true;
        const QByteArray body = Vtodo::toIcs(*m_store, local, op.extra);
        m_client->put(retry.href, body, retry.etag, [this, retry](const CalDavClient::Outcome &o2, const QString &e) {
            onPut(retry, o2, e);
        });
    });
}

void CalDavSync::afterCalendar() {
    if (m_stateIndex < m_states.size())
        m_states[m_stateIndex] = m_job;
    ++m_stateIndex;
    processNextCalendar();
}

// ------------------------------------------------------------------ wrapping

void CalDavSync::refreshCtags() {
    m_client->discover(m_url, [this](const CalDavClient::Outcome &o, const QList<CalDavClient::Calendar> &calendars) {
        onCtagsRefreshed(o, calendars);
    });
}

void CalDavSync::onCtagsRefreshed(const CalDavClient::Outcome &outcome, const QList<CalDavClient::Calendar> &calendars) {
    if (outcome.ok) {
        for (ListState &state : m_states) {
            for (const CalDavClient::Calendar &c : calendars)
                if (c.href == state.href && !c.ctag.isEmpty())
                    state.ctag = c.ctag;
            updateListLink(state);
        }
    }
    finishOk();
}

void CalDavSync::finishOk() {
    m_applying = false;
    m_busy = false;
    m_finished = true;
    m_lastResult = {true, false, m_in + m_out, QString()};
    m_store->setSetting("caldav.lastSync", QDateTime::currentDateTime().toString(Qt::ISODate));
    const QString summary = QStringLiteral("Synced %1 calendars, %2 tasks changed").arg(m_states.size()).arg(m_in + m_out);
    m_status = m_writeError.isEmpty() ? summary : m_writeError;
    emit stateChanged();
    emit synced(summary);
    if (m_again) {
        m_again = false;
        syncNow();
    }
}

void CalDavSync::fail(const QString &error) {
    m_applying = false;
    m_busy = false;
    m_finished = true;
    m_lastResult = {false, false, 0, error};
    m_status = error.isEmpty() ? QStringLiteral("CalDAV sync failed") : error;
    emit stateChanged();
    if (m_again) {
        m_again = false;
        syncNow();
    }
}

// ------------------------------------------------------------------- helpers

void CalDavSync::storeTaskLink(qint64 taskId, const QString &href, const QString &etag, const Vtodo::Extra &extra) {
    Store::SyncLink link;
    link.localId = taskId;
    link.remoteId = href;
    link.snapshot = withMeta(localState(*m_store, m_store->task(taskId)), etag, extra);
    m_store->setSyncLink(Provider, ETasks, link);
}

void CalDavSync::updateListLink(const ListState &state) {
    Store::SyncLink link;
    link.localId = state.listId;
    link.remoteId = state.href;
    QVariantMap snap;
    snap.insert(MappedKey, true);
    snap.insert(CtagKey, state.ctag);
    snap.insert(TokenKey, state.token);
    link.snapshot = snap;
    m_store->setSyncLink(Provider, ELists, link);
}

QList<qint64> CalDavSync::tagIdsFor(const QStringList &names) {
    QList<qint64> ids;
    for (const QString &raw : names) {
        const QString name = raw.trimmed();
        if (name.isEmpty())
            continue;
        qint64 id = 0;
        for (const auto &t : m_store->tags())
            if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0) {
                id = t.id;
                break;
            }
        if (!id) {
            Tag tag;
            tag.id = m_store->nextId("tags");
            tag.name = name;
            m_store->putTag(tag);
            id = tag.id;
        }
        if (!ids.contains(id))
            ids << id;
    }
    return ids;
}

QList<qint64> CalDavSync::locationIdsFor(const QString &name) {
    QList<qint64> ids;
    for (const QString &raw : name.split(',', Qt::SkipEmptyParts)) {
        const QString place = raw.trimmed();
        if (place.isEmpty())
            continue;
        qint64 id = 0;
        for (const auto &l : m_store->locations())
            if (!l.deleted && l.name.compare(place, Qt::CaseInsensitive) == 0) {
                id = l.id;
                break;
            }
        if (!id) {
            Location location;
            location.id = m_store->nextId("locations");
            location.name = place;
            m_store->putLocation(location);
            id = location.id;
        }
        ids << id;
    }
    return ids;
}

void CalDavSync::applyAlarms(qint64 taskId, const QList<Alarm> &alarms) {
    const QVector<Alarm> existing = m_store->alarmsFor(taskId);
    bool same = existing.size() == alarms.size();
    if (same)
        for (int i = 0; i < alarms.size(); ++i)
            if (existing[i].kind != alarms[i].kind || existing[i].offset != alarms[i].offset || existing[i].at != alarms[i].at)
                same = false;
    if (same)
        return;
    for (Alarm a : existing) {
        a.deleted = true;
        m_store->putAlarm(a);
    }
    for (Alarm a : alarms) {
        a.id = m_store->nextId("alarms");
        a.taskId = taskId;
        m_store->putAlarm(a);
    }
}
