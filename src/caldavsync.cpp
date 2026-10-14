#include "caldavsync.h"

#include <QDir>
#include <QFileInfo>

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
// snapshot would write the same resource.
QVariantMap projection(QVariantMap map) {
    for (const char *key : {"id", "modified", "created", "sort_order", "collapsed", "series_uid", "deleted", "section_id"})
        map.remove(key);
    for (auto it = map.begin(); it != map.end();) {
        if (it.key().startsWith(QLatin1String("__")))
            it = map.erase(it);
        else
            ++it;
    }
    return map;
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

// The calendar home a created calendar goes into: the deepest directory under
// the base URL that still looks like a collection.
QString homeFromBase(const QUrl &base) {
    QString path = base.path();
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (!path.endsWith('/'))
        path += '/';
    return path;
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

QString CalDavSync::accountNameFor(const QUrl &url) const {
    QString host = url.host();
    if (host.startsWith("www."))
        host = host.mid(4);
    const QString label = host.section('.', 0, 0);
    return label.isEmpty() ? QStringLiteral("CalDAV") : capitalise(label);
}

QString CalDavSync::homeHref() const {
    return homeFromBase(m_url);
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
    emit stateChanged();
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

void CalDavSync::setCalendarForList(qint64 listId, const QString &calendarHref) {
    const TaskList list = m_store->list(listId);
    if (!list.id)
        return;
    if (calendarHref.isEmpty()) {
        m_store->removeSyncLink(Provider, ELists, listId);
        return;
    }
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
    if (!m_cachedPassword.isEmpty() && m_passwordLooked) {
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
    m_localHasTasks = false;
    for (const auto &t : m_store->tasks())
        if (!t.deleted) {
            m_localHasTasks = true;
            break;
        }
    if (!m_localHasTasks) {
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
                m_needsChoice = true;
                m_status = "This machine and the server both have tasks: choose how to start";
                emit stateChanged();
                return;
            }
            if (m_remoteHasTasks && m_mode == SyncTree::FirstSync::ReplaceLocal) {
                const QString name = QStringLiteral("Fairfax_(CalDAV)_before-sync.db");
                const QString path = QFileInfo(m_store->path()).absolutePath() + "/backups/" + name;
                QDir().mkpath(QFileInfo(path).absolutePath());
                m_store->backupTo(path);
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
    QHash<QString, qint64> hrefToList;
    for (const auto &link : listLinks) {
        const TaskList list = m_store->list(link.localId);
        if (!list.id || list.deleted || link.remoteId.isEmpty()) {
            m_store->removeSyncLink(Provider, ELists, link.localId);
            continue;
        }
        hrefToList.insert(link.remoteId, link.localId);
    }

    // Excluded lists never map, and their stale links go.
    const QSet<qint64> excluded = SyncTree::excludedListIds(*m_store);
    for (auto it = hrefToList.begin(); it != hrefToList.end();) {
        if (excluded.contains(it.value())) {
            m_store->removeSyncLink(Provider, ELists, it.value());
            it = hrefToList.erase(it);
        } else {
            ++it;
        }
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
        snap.insert(CtagKey, calendar.ctag);
        snap.insert(TokenKey, calendar.syncToken);
        link.snapshot = snap;
        m_store->setSyncLink(Provider, ELists, link);
        hrefToList.insert(calendar.href, match);
        unmapped.remove(match);
    }

    // Calendars without a list become lists in a group named after the account.
    for (const CalDavClient::Calendar &calendar : m_calendars) {
        if (hrefToList.contains(calendar.href))
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
        snap.insert(CtagKey, calendar.ctag);
        snap.insert(TokenKey, calendar.syncToken);
        link.snapshot = snap;
        m_store->setSyncLink(Provider, ELists, link);
        hrefToList.insert(calendar.href, list.id);
    }

    // Lists still without a calendar: create one when the setting allows.
    m_pendingCreate.clear();
    const bool createCalendars = m_store->setting("caldav.createCalendars", true).toBool();
    for (qint64 listId : unmapped) {
        const TaskList list = m_store->list(listId);
        if (!createCalendars)
            continue;
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
            if (c.href == m_job.href && c.ctag == m_job.ctag && !c.ctag.isEmpty()) {
                // Nothing changed on either side: skip the calendar.
                ++m_stateIndex;
                processNextCalendar();
                return;
            }
    }
    pullRemote();
}

void CalDavSync::pullRemote() {
    if (!m_job.token.isEmpty()) {
        m_client->syncCollection(m_job.href, m_job.token,
                                 [this](const CalDavClient::Outcome &o, const QStringList &ch, const QStringList &rm, const QString &t) {
                                     onSyncCollection(o, ch, rm, t);
                                 });
        return;
    }
    listRemoteEtags();
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
    m_applying = true;
    m_store->begin("CalDAV sync", false, true);
    QSet<QString> got;
    for (auto it = resources.begin(); it != resources.end(); ++it) {
        got.insert(it.key());
        applyRemoteResource(it.key(), it.value().etag, it.value().data);
    }
    for (const QString &href : m_pullRemoved)
        if (!got.contains(href))
            applyRemoteRemoval(href);
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
    if (created) {
        Task t;
        t.id = m_store->nextId("tasks");
        id = t.id;
    }
    Task local = created ? Task() : m_store->task(id);
    if (local.deleted)
        return id;   // Its deletion is waiting to be pushed.

    const Store::SyncLink *link = nullptr;
    for (const auto &l : links)
        if (l.localId == id) {
            link = &l;
            break;
        }
    const bool locallyChanged = created ? false : (!link || projection(local.toMap()) != projection(link->snapshot));
    if (!created && locallyChanged && !remoteWins(local.modified, remote.modified)) {
        // Local wins: it is pushed below.
        m_dirty.insert(m_job.listId);
        return id;
    }

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
    merged.type = remote.type;
    merged.actionType = remote.actionType;
    merged.actionValue = remote.actionValue;
    merged.recurrence = remote.recurrence;
    if (created) {
        merged.created = remote.created.isValid() ? remote.created : QDateTime::currentDateTime();
        merged.parentId = 0;
    }
    for (const auto &t : m_store->tasks())
        if (!t.deleted && !parsed.parentUid.isEmpty() && t.uid == parsed.parentUid)
            merged.parentId = t.id;
    merged.tagIds = tagIdsFor(parsed.tagNames);
    merged.locationIds = locationIdsFor(parsed.locationName);
    merged.modified = remote.modified.isValid() ? remote.modified : QDateTime::currentDateTime();
    merged.deleted = false;
    m_store->putTask(merged);
    applyAlarms(id, parsed.alarms);
    storeTaskLink(id, href, etag, parsed.extra);
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
    const bool locallyChanged = link && projection(task.toMap()) != projection(link->snapshot);
    if (!task.deleted && locallyChanged) {
        // Edited since the last sync: upload it again as a new resource.
        m_store->removeSyncLink(Provider, ETasks, id);
        m_dirty.insert(m_job.listId);
        return;
    }
    if (!task.deleted) {
        task.deleted = true;
        task.modified = QDateTime::currentDateTime();
        m_store->putTask(task);
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
        if (link == byId.constEnd() || projection(t.toMap()) != projection(link->snapshot))
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
            op.kind = Op::Delete;
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
            QString uid = t.uid.isEmpty() ? Store::newUid() : t.uid;
            op.kind = Op::Create;
            op.href = m_job.href + QString::fromUtf8(QUrl::toPercentEncoding(uid)) + ".ics";
            // A re-uploaded resource may still exist under a new uid; the
            // If-None-Match create is retried as an update on 412.
            m_ops.append(op);
            continue;
        }
        if (projection(t.toMap()) == projection(link->snapshot))
            continue;
        op.extra = extraFromMap(link->snapshot.value(ExtraKey).toMap());
        if (link->remoteId.startsWith(m_job.href)) {
            op.kind = Op::Update;
            op.href = link->remoteId;
            op.etag = link->snapshot.value(EtagKey).toString();
        } else {
            op.kind = Op::Move;
            op.oldHref = link->remoteId;
            op.etag = link->snapshot.value(EtagKey).toString();
            const QString uid = t.uid.isEmpty() ? Store::newUid() : t.uid;
            op.href = m_job.href + QString::fromUtf8(QUrl::toPercentEncoding(uid)) + ".ics";
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
    const Task task = m_store->task(op.taskId);
    const QByteArray body = (op.kind == Op::Delete) ? QByteArray() : Vtodo::toIcs(*m_store, task, op.extra);

    if (op.kind == Op::Delete) {
        m_client->remove(op.href, op.etag, [this, op](const CalDavClient::Outcome &o) { onRemove(op, o); });
        return;
    }
    // A linked resource with an unknown etag is not new: learn the etag from
    // the listing first. Never send If-None-Match: * for one.
    if ((op.kind == Op::Update || op.kind == Op::Move) && op.etag.isEmpty()) {
        const QString calendarHref = op.kind == Op::Move ? QUrl(op.oldHref).path().section('/', 0, -2) + '/' : m_job.href;
        const QString href = op.kind == Op::Move ? op.oldHref : op.href;
        m_client->listEtags(calendarHref, [this, op, body, href](const CalDavClient::Outcome &o, const QMap<QString, QString> &etags) {
            if (!o.ok || !etags.contains(href) || etags.value(href).isEmpty()) {
                // The resource is gone, or its etag is still unknown: try the
                // write as a fresh create only when it is really gone.
                if (o.ok && !etags.contains(href)) {
                    Op fresh = op;
                    fresh.kind = Op::Create;
                    fresh.oldHref.clear();
                    fresh.etag.clear();
                    m_client->put(fresh.href, body, QString(), [this, fresh](const CalDavClient::Outcome &o2, const QString &e) {
                        onPut(fresh, o2, e);
                    });
                    return;
                }
                ++m_opIndex;
                nextOp();
                return;
            }
            Op resolved = op;
            resolved.etag = etags.value(href);
            m_client->put(resolved.href, body, resolved.etag, [this, resolved](const CalDavClient::Outcome &o2, const QString &e) {
                onPut(resolved, o2, e);
            });
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
    if (op.kind == Op::Move) {
        // The resource now lives in the new calendar; remove the old one.
        m_client->remove(op.oldHref, op.etag, [this, op](const CalDavClient::Outcome &o) {
            if (o.ok)
                m_store->removeSyncLink(Provider, ETasks, op.taskId);
        });
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
            if (!remoteWins(local.modified, parsed.task.modified)) {
                // The deletion is newer: delete again with the fresh etag.
                m_client->remove(op.href, resource.etag, [](const CalDavClient::Outcome &) {});
                m_store->removeSyncLink(Provider, ETasks, op.taskId);
            } else {
                // The remote edit is newer: it comes back locally.
                local.deleted = false;
                local.modified = parsed.task.modified.isValid() ? parsed.task.modified : QDateTime::currentDateTime();
                m_store->putTask(local);
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
            fresh.kind = Op::Create;
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
        if (remoteWins(local.modified, parsed.task.modified)) {
            // Adopt the server's version and stop.
            applyRemoteResource(op.href, resource.etag, resource.data);
            ++m_opIndex;
            nextOp();
            return;
        }
        // Local is still newer: retry once against the fresh etag.
        Op retry = op;
        retry.etag = resource.etag;
        retry.retried = true;
        const QByteArray body = Vtodo::toIcs(*m_store, local, op.extra);
        m_client->put(retry.href, body, retry.etag, [this, retry](const CalDavClient::Outcome &o2, const QString &e) {
            onPut(retry, o2, e);
        });
    });
}

void CalDavSync::afterCalendar() {
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
    link.snapshot = withMeta(projection(m_store->task(taskId).toMap()), etag, extra);
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
