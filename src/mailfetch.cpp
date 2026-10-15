#include "mailfetch.h"

#include <QFile>
#include <QGuiApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "keyring.h"
#include "store.h"

namespace {

constexpr auto KeyringService = "fairfax";
constexpr auto PasswordAccount = "mail";
constexpr auto ClientAccount = "mail-client";
constexpr auto RefreshAccount = "mail-oauth";

bool truthy(const QVariant &v) {
    const QString s = v.toString();
    return s == QLatin1String("true") || s == QLatin1String("1");
}

// The bare, lower-cased addresses in a header value ("Jane <Jane@x.org>, bob@y.org").
QStringList addressesIn(const QString &text) {
    static const QRegularExpression re(R"([A-Za-z0-9._%+'\-]+@[A-Za-z0-9.\-]+[A-Za-z0-9])");
    QStringList out;
    for (auto it = re.globalMatch(text); it.hasNext();)
        out << it.next().captured(0).toLower();
    return out;
}

// What the to-filter asks for: the addresses in it, or (if it holds no
// address) its comma-separated words, which then match nothing exactly rather
// than everything.
QStringList filterTerms(const QString &filter) {
    QStringList terms = addressesIn(filter);
    if (terms.isEmpty())
        for (const QString &w : filter.toLower().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts))
            terms << w;
    return terms;
}

QByteArray imapString(const QString &text) { return ImapClient::quote(text); }

}  // namespace

MailFetch::MailFetch(Store *store, QObject *parent) : QObject(parent), m_store(store), m_flow(this), m_refresh(this) {
    for (QTimer *t : {&m_retry, &m_poll, &m_idleRenew, &m_reconfigure})
        t->setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { if (enabled()) startSession(); });
    connect(&m_poll, &QTimer::timeout, this, [this] { if (enabled()) startSession(); });
    connect(&m_idleRenew, &QTimer::timeout, this, [this] {
        // IDLE is re-issued before servers give up on it (they may after 30 minutes); a pass over the mailbox comes with it.
        if (m_inIdle)
            leaveIdle([this] { runCycle(); });
    });
    connect(&m_reconfigure, &QTimer::timeout, this, [this] { if (enabled()) startSession(); });

    connect(&m_flow, &OAuth::Flow::finished, this, [this](const OAuth::Tokens &t) {
        Keyring::store(KeyringService, RefreshAccount, t.refreshToken);
        m_accessToken = t.accessToken;
        m_accessExpires = t.expires;
        const QString provider = option("mail.account") == QLatin1String("outlook") ? "microsoft" : "google";
        put("mail.oauth", provider);
        m_oauthWaiting = false;
        m_oauthError.clear();
        changed();
        emit oauthChanged();
        if (enabled())
            startSession();
    });
    connect(&m_flow, &OAuth::Flow::failed, this, [this](const QString &why) {
        m_oauthWaiting = false;
        m_oauthError = why;
        emit oauthChanged();
    });
}

MailFetch::~MailFetch() { dropSession(); }

// ----------------------------------------------------------------- options

QString MailFetch::plusAddressOf(const QString &address) {
    const int at = address.indexOf('@');
    if (at <= 0 || address.left(at).contains('+'))
        return QString();
    return address.left(at) + "+fairfax" + address.mid(at);
}

bool MailFetch::enabled() const { return truthy(m_store->setting("mail.enabled")); }

QStringList MailFetch::optionKeys() {
    return {"mail.enabled", "mail.account", "mail.address", "mail.host", "mail.port", "mail.security", "mail.user", "mail.folder",
            "mail.toFilter", "mail.labelFilter", "mail.after", "mail.moveTo", "mail.listId", "mail.naturalLanguage", "mail.interval",
            "mail.googleClientId", "mail.msClientId", "mail.oauth"};
}

QString MailFetch::option(const QString &key) const {
    const QVariant stored = m_store->setting(key);
    const QString value = stored.isValid() ? stored.toString() : QString();
    const QString account = m_store->setting("mail.account").isValid() ? m_store->setting("mail.account").toString() : QStringLiteral("gmail");
    const QString address = m_store->setting("mail.address").toString();
    if (key == "mail.enabled")
        return enabled() ? "true" : "false";
    if (key == "mail.account")
        return account;
    if (key == "mail.host") {
        if (!value.isEmpty())
            return value;
        return account == "outlook" ? "outlook.office365.com" : account == "gmail" ? "imap.gmail.com" : QString();
    }
    if (key == "mail.port")
        return value.isEmpty() ? "993" : value;
    if (key == "mail.security")
        return value.isEmpty() ? "tls" : value;
    if (key == "mail.user")
        return !value.isEmpty() ? value : (account == "other" ? QString() : address);
    if (key == "mail.folder")
        return value.isEmpty() ? "INBOX" : value;
    if (key == "mail.toFilter")
        return stored.isValid() ? value : (account == "gmail" ? plusAddressOf(address) : QString());
    if (key == "mail.after")
        return value.isEmpty() ? (account == "gmail" ? "move" : "leave") : value;
    if (key == "mail.moveTo")
        return value.isEmpty() ? "Fairfax" : value;
    if (key == "mail.listId")
        return value.isEmpty() ? "0" : value;
    if (key == "mail.naturalLanguage")
        return truthy(stored) ? "true" : "false";
    if (key == "mail.interval")
        return value.isEmpty() ? "5" : value;
    return value;   // address, labelFilter, googleClientId, msClientId, oauth.
}

void MailFetch::put(const QString &key, const QVariant &value) { m_store->setSetting(key, value); }

void MailFetch::changed() {
    ++m_revision;
    emit optionsChanged();
}

void MailFetch::setOption(const QString &key, const QVariant &value) {
    if (key == "mail.address") {
        // The Gmail to-filter and the user name follow the address until they
        // have been edited, and so does the user name of an Outlook account.
        const QString before = option("mail.address"), after = value.toString().trimmed();
        if (option("mail.account") == "gmail" && option("mail.toFilter") == plusAddressOf(before))
            put("mail.toFilter", plusAddressOf(after));
        if (option("mail.account") != "other" && option("mail.user") == before)
            put("mail.user", after);
        put(key, after);
    } else if (key == "mail.enabled") {
        put(key, truthy(value) || value.toBool());
    } else {
        put(key, value);
    }
    changed();
    if (key == "mail.enabled") {
        m_reconfigure.stop();
        if (enabled()) {
            m_oauthError.clear();
            startSession();
        } else {
            dropSession();
            m_retry.stop();
            m_poll.stop();
            setState(State::Off);
        }
    } else if (enabled() && key != "mail.googleClientId" && key != "mail.msClientId") {
        m_reconfigure.start(800);   // Typing in a field is not a reason to reconnect per letter.
    }
}

void MailFetch::setAccount(const QString &account) {
    const QString previous = option("mail.account");
    const QString address = option("mail.address");
    const QString oldDefaultAfter = option("mail.after");
    const bool wasPlain = option("mail.toFilter") == plusAddressOf(address) || option("mail.toFilter").isEmpty();
    put("mail.account", account);
    if (account == "gmail" || account == "outlook") {
        put("mail.host", account == "gmail" ? "imap.gmail.com" : "outlook.office365.com");
        put("mail.port", 993);
        put("mail.security", "tls");
        put("mail.user", address);
    }
    if (account == "gmail") {
        if (wasPlain)
            put("mail.toFilter", plusAddressOf(address));
        put("mail.after", "move");
        put("mail.moveTo", "Fairfax");
    } else {
        // Leaving Gmail takes its automatic filter and its move-out-of-the-inbox default with it.
        if (previous == "gmail") {
            if (option("mail.toFilter") == plusAddressOf(address))
                put("mail.toFilter", QString());
            if (oldDefaultAfter == "move")
                put("mail.after", "leave");
        }
    }
    if (account != previous && m_oauthWaiting)
        cancelSignIn();
    changed();
    emit oauthChanged();
    if (enabled())
        m_reconfigure.start(800);
}

void MailFetch::useDavMail() {
    setAccount("other");
    put("mail.host", "127.0.0.1");
    put("mail.port", 1143);
    put("mail.security", "none");
    changed();
    if (enabled())
        m_reconfigure.start(800);
}

bool MailFetch::unfiltered() const {
    return filterTerms(option("mail.toFilter")).isEmpty() && option("mail.labelFilter").trimmed().isEmpty();
}

void MailFetch::copyText(const QString &text) {
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(text);
}

MailFetch::Config MailFetch::config() const {
    Config c;
    c.account = option("mail.account");
    c.host = option("mail.host").trimmed();
    c.port = option("mail.port").toUShort();
    const QString security = option("mail.security");
    c.security = security == "none" ? ImapClient::Security::None : security == "starttls" ? ImapClient::Security::StartTls : ImapClient::Security::Tls;
    c.user = option("mail.user").trimmed();
    c.folder = option("mail.folder");
    c.toFilter = option("mail.toFilter").trimmed();
    c.labelFilter = option("mail.labelFilter").trimmed();
    c.after = option("mail.after");
    c.moveTo = option("mail.moveTo");
    c.listId = option("mail.listId").toLongLong();
    c.naturalLanguage = option("mail.naturalLanguage") == "true";
    return c;
}

// ----------------------------------------------------------------- secrets

bool MailFetch::hasPassword() const {
    if (m_havePassword < 0)
        m_havePassword = Keyring::lookup(KeyringService, PasswordAccount).isEmpty() ? 0 : 1;
    return m_havePassword == 1;
}

void MailFetch::setPassword(const QString &password) {
    if (password.isEmpty()) {
        Keyring::clear(KeyringService, PasswordAccount);
        m_havePassword = 0;
    } else {
        QString error;
        if (!Keyring::store(KeyringService, PasswordAccount, password, &error)) {
            setState(State::Error, error);
            return;
        }
        m_havePassword = 1;
    }
    changed();
    if (enabled())
        m_reconfigure.start(800);
}

bool MailFetch::hasGoogleClient() const { return !option("mail.googleClientId").isEmpty(); }

QString MailFetch::importClientJson(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("Couldn't read ") + path;
    QJsonParseError parse;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &parse).object();
    if (parse.error != QJsonParseError::NoError || root.isEmpty())
        return QStringLiteral("That isn't a Google client JSON file.");
    // A "Desktop app" client is under "installed"; a "web" client has no loopback redirect to offer.
    const QJsonObject client = root.value("installed").toObject();
    const QString id = client.value("client_id").toString(), secret = client.value("client_secret").toString();
    if (id.isEmpty() || secret.isEmpty())
        return root.contains("web") ? QStringLiteral("That is a Web client. Create a Desktop app client in Google Cloud and download its JSON.")
                                    : QStringLiteral("No client_id and client_secret in that file.");
    QString error;
    if (!Keyring::store(KeyringService, ClientAccount, secret, &error))
        return error;
    put("mail.googleClientId", id);
    m_oauthError.clear();   // Whatever the last attempt complained about, there is a client now.
    changed();
    emit oauthChanged();
    return QString();
}

// ------------------------------------------------------------------- OAuth

bool MailFetch::usesOAuth() const {
    const QString account = option("mail.account"), signedIn = option("mail.oauth");
    return (account == "gmail" && signedIn == "google") || (account == "outlook" && signedIn == "microsoft");
}

QString MailFetch::signedInAs() const {
    return usesOAuth() ? option("mail.address") : QString();
}

OAuth::Provider MailFetch::oauthProvider() const {
    if (option("mail.account") == "outlook")
        return OAuth::microsoft(option("mail.msClientId"));
    return OAuth::google(option("mail.googleClientId"), Keyring::lookup(KeyringService, ClientAccount));
}

void MailFetch::setHttpTransport(OAuth::Transport transport) {
    m_transport = transport;
    m_flow.setTransport(transport);
    m_refresh.setTransport(transport);
}

void MailFetch::signIn() {
    const bool outlook = option("mail.account") == "outlook";
    if (option("mail.account") == "other") {
        m_oauthError = "Signing in is for Gmail and Outlook accounts.";
        emit oauthChanged();
        return;
    }
    if (outlook ? option("mail.msClientId").isEmpty() : !hasGoogleClient()) {
        m_oauthError = outlook ? "Paste your Microsoft application (client) ID first." : "Import your Google client JSON first.";
        emit oauthChanged();
        return;
    }
    m_oauthError.clear();
    m_oauthWaiting = true;
    emit oauthChanged();
    m_flow.start(oauthProvider());
}

void MailFetch::cancelSignIn() {
    m_flow.cancel();
    m_oauthWaiting = false;
    m_oauthError.clear();
    emit oauthChanged();
}

void MailFetch::signOut() {
    m_flow.cancel();
    Keyring::clear(KeyringService, RefreshAccount);
    m_accessToken.clear();
    m_accessExpires = QDateTime();
    put("mail.oauth", QString());
    m_oauthWaiting = false;
    m_oauthError.clear();
    changed();
    emit oauthChanged();
    if (enabled())
        startSession();
}

void MailFetch::ensureAccessToken(std::function<void(bool, const QString &)> done) {
    if (!m_accessToken.isEmpty() && m_accessExpires > QDateTime::currentDateTime().addSecs(60)) {
        done(true, m_accessToken);
        return;
    }
    const QString refreshToken = Keyring::lookup(KeyringService, RefreshAccount);
    if (refreshToken.isEmpty()) {
        done(false, "Signed out. Sign in again in Settings.");
        return;
    }
    // One refresh at a time; the connection that asked waits on it.
    auto *once = new QMetaObject::Connection[2];
    once[0] = connect(&m_refresh, &OAuth::Flow::finished, this, [this, done, once](const OAuth::Tokens &t) {
        disconnect(once[0]);
        disconnect(once[1]);
        delete[] once;
        if (t.refreshToken != Keyring::lookup(KeyringService, RefreshAccount))
            Keyring::store(KeyringService, RefreshAccount, t.refreshToken);   // Microsoft rotates it.
        m_accessToken = t.accessToken;
        m_accessExpires = t.expires;
        done(true, t.accessToken);
    });
    once[1] = connect(&m_refresh, &OAuth::Flow::failed, this, [this, done, once](const QString &why) {
        disconnect(once[0]);
        disconnect(once[1]);
        delete[] once;
        done(false, "Couldn't renew the sign-in: " + why);
    });
    m_refresh.refresh(oauthProvider(), refreshToken);
}

// ------------------------------------------------------------------ status

QString MailFetch::stateName() const {
    switch (m_state) {
    case State::Off: return "off";
    case State::Connecting: return "connecting";
    case State::Idle: return "idle";
    case State::Fetching: return "fetching";
    case State::Error: return "error";
    }
    return "off";
}

QString MailFetch::hubStatus() const {
    switch (m_state) {
    case State::Off: return QString();
    case State::Connecting: return "Connecting…";
    case State::Fetching: return "Connecting…";   // Not a failure, whatever the hub's other states say.
    case State::Error: return m_lastError.isEmpty() ? QStringLiteral("Mail failed") : m_lastError;
    case State::Idle:
        return m_lastCheck.isValid() ? "Synced " + m_lastCheck.toString("h:mm AP") + (m_imported ? QStringLiteral(" · %1 imported").arg(m_imported) : QString())
                                     : QStringLiteral("Not synced yet");
    }
    return QString();
}

QString MailFetch::statusLine() const {
    QString line;
    switch (m_state) {
    case State::Off: return "Off";
    case State::Connecting: return "Connecting…";
    case State::Fetching: line = "Fetching…"; break;
    case State::Error: line = m_lastError.isEmpty() ? QStringLiteral("Couldn't connect.") : m_lastError; break;
    case State::Idle: line = "Connected"; break;
    }
    if (m_state == State::Idle && m_lastCheck.isValid())
        line += " · checked " + m_lastCheck.toString("h:mm AP");
    if (m_state == State::Idle || (m_state == State::Error && m_imported))
        line += QStringLiteral(" · %1 imported").arg(m_imported);
    if (m_skipped)
        line += QStringLiteral(" · %1 over 25 MB skipped").arg(m_skipped);
    return line;
}

void MailFetch::setState(State state, const QString &error) {
    const bool newError = state == State::Error && !error.isEmpty() && error != m_lastError;
    m_state = state;
    if (state == State::Error)
        m_lastError = error;
    else if (state != State::Off)
        m_lastError.clear();
    emit stateChanged();
    if (newError)
        emit errorOccurred(error);
}

// -------------------------------------------------------------- scheduling

void MailFetch::setTimings(const Timings &timings) { m_timings = timings; }

void MailFetch::start() {
    if (enabled())
        startSession();
}

void MailFetch::fetchNow() {
    if (!enabled())
        return;
    if (m_cycleBusy) {
        m_again = true;
        return;
    }
    if (m_client && !m_client->isClosed() && m_inIdle) {
        leaveIdle([this] { runCycle(); });
        return;
    }
    if (m_state == State::Connecting)
        return;
    startSession();
}

void MailFetch::dropSession() {
    ++m_session;
    m_idleRenew.stop();
    m_cycleBusy = false;
    m_again = false;
    m_inIdle = false;
    m_stopRequested = false;
    m_afterIdle = {};
    m_todo.clear();
    m_batch.clear();
    if (m_client) {
        m_client->disconnect(this);
        m_client->close();
        m_client->deleteLater();
        m_client = nullptr;
    }
}

void MailFetch::scheduleRetry() {
    // 30 s, 1 min, 2 min ... up to a quarter of an hour.
    const int delay = qMin<qint64>(m_timings.backoffBase * (1LL << qMin(m_failures, 10)), qMax(m_timings.backoffBase, 15 * 60000));
    ++m_failures;
    m_retry.start(delay);
}

void MailFetch::failSession(const QString &message, bool retry) {
    dropSession();
    m_poll.stop();
    setState(State::Error, message);
    if (retry && enabled())
        scheduleRetry();
}

void MailFetch::startSession() {
    dropSession();
    m_retry.stop();
    m_poll.stop();
    m_reconfigure.stop();
    m_rejected.clear();
    m_moveFolderReady = false;
    m_config = config();
    const Config &c = m_config;
    if (c.host.isEmpty() || c.user.isEmpty()) {
        setState(State::Error, option("mail.account") == "other" ? "Fill in the server and your user name." : "Enter your email address.");
        return;
    }
    setState(State::Connecting);
    const quint64 session = m_session;
    m_client = new ImapClient(this);
    m_client->setTimeout(m_timings.imapTimeout);
    connect(m_client, &ImapClient::closed, this, [this, session] {
        if (session == m_session)
            failSession("The connection to the mail server was lost.", true);
    });
    m_client->connectToServer(c.host, c.port, c.security, [this, session](const ImapClient::Response &greeting) {
        if (session != m_session)
            return;
        if (!greeting.ok) {
            failSession(greeting.disconnected ? QStringLiteral("Couldn't connect to %1: %2").arg(m_config.host, greeting.text)
                                              : QStringLiteral("The mail server refused the connection: ") + greeting.text, true);
            return;
        }
        auto proceed = [this, session] {
            if (session != m_session)
                return;
            authenticate();
        };
        m_client->capability([this, session, proceed](const ImapClient::Response &caps) {
            if (session != m_session)
                return;
            if (!caps.ok) {
                failSession("The mail server didn't answer CAPABILITY: " + caps.text, true);
                return;
            }
            if (m_config.security != ImapClient::Security::StartTls) {
                proceed();
                return;
            }
            if (!m_client->hasCapability("STARTTLS")) {
                failSession("The server doesn't offer STARTTLS. Choose TLS or None.", false);
                return;
            }
            m_client->startTls([this, session, proceed](const ImapClient::Response &tls) {
                if (session != m_session)
                    return;
                if (!tls.ok) {
                    failSession("Couldn't start TLS: " + tls.text, true);
                    return;
                }
                m_client->capability([this, session, proceed](const ImapClient::Response &) { proceed(); });
            });
        });
    });
}

void MailFetch::authenticate() {
    const quint64 session = m_session;
    auto done = [this, session](const ImapClient::Response &r) {
        if (session != m_session)
            return;
        if (!r.ok) {
            // A refused login will not fix itself: wait for the user to change something.
            failSession(r.disconnected ? QStringLiteral("The connection was lost while signing in.")
                                       : QStringLiteral("The mail server refused the sign-in: ") + r.text, r.disconnected);
            return;
        }
        afterAuth();
    };
    if (usesOAuth()) {
        ensureAccessToken([this, session, done](bool ok, const QString &tokenOrError) {
            if (session != m_session)
                return;
            if (!ok) {
                failSession(tokenOrError, false);
                return;
            }
            m_client->authenticateXoauth2(m_config.user, tokenOrError, done);
        });
        return;
    }
    const QString password = Keyring::lookup(KeyringService, PasswordAccount);
    if (password.isEmpty()) {
        failSession(m_config.account == "gmail" ? "Enter your Gmail app password." : "Enter your password.", false);
        return;
    }
    m_client->login(m_config.user, password, done);
}

void MailFetch::afterAuth() {
    const quint64 session = m_session;
    // Capabilities can differ after sign-in (Gmail adds X-GM-EXT-1, for one).
    m_client->capability([this, session](const ImapClient::Response &) {
        if (session != m_session)
            return;
        m_client->select(m_config.folder, [this, session](const ImapClient::Selection &s) {
            if (session != m_session)
                return;
            if (!s.ok) {
                failSession(QStringLiteral("Couldn't open the folder %1: %2").arg(m_config.folder, s.error), false);
                return;
            }
            m_validity = s.uidValidity;
            runCycle();
        });
    });
}

// ----------------------------------------------------------------- a pass

QString MailFetch::markKey(const Config &c) const { return "mail.mark." + c.folder; }

QByteArray MailFetch::searchCriteria(const Config &c) const {
    const bool gmail = m_client && m_client->hasCapability("X-GM-EXT-1");
    QList<QByteArray> keys{"NOT DELETED"};
    if (c.after == "read")
        keys << "UNSEEN";
    if (c.after == "leave") {
        // Mail that was there before the last pass is not looked at again.
        const QStringList mark = m_store->setting(markKey(c)).toString().split(':');
        if (mark.size() == 2 && mark[0].toLongLong() == m_validity && mark[1].toLongLong() > 0)
            keys << "UID " + QByteArray::number(mark[1].toLongLong() + 1) + ":*";
    }
    const QStringList terms = filterTerms(c.toFilter);
    if (gmail) {
        QStringList raw;
        const bool hasLabel = !c.labelFilter.isEmpty();
        if (!terms.isEmpty()) {
            QStringList alternatives;
            for (const QString &a : terms)
                alternatives << "deliveredto:" + a << "to:" + a;
            const QString any = alternatives.join(" OR ");
            raw << (terms.size() > 1 || hasLabel ? "(" + any + ")" : any);
        }
        if (hasLabel)
            raw << "label:" + QString(c.labelFilter).replace(' ', '-');
        if (!raw.isEmpty())
            keys << "X-GM-RAW " + imapString(raw.join(' '));
    } else {
        // To and Cc, plus the headers a mail server adds when it delivers to an alias or a redirect.
        QByteArray any;
        for (const QString &a : terms) {
            const QByteArray q = imapString(a);
            const QByteArray one = "OR OR OR TO " + q + " CC " + q + " HEADER Delivered-To " + q + " HEADER X-Original-To " + q;
            any = any.isEmpty() ? one : "OR " + any + " " + one;
        }
        if (!any.isEmpty())
            keys << any;
        if (!c.labelFilter.isEmpty())
            keys << "KEYWORD " + c.labelFilter.toUtf8().replace(' ', '_');
    }
    return keys.join(' ');
}

bool MailFetch::toFilterMatches(const MailTask::Message &message, const QString &filter) const {
    const QStringList wanted = filterTerms(filter);
    if (wanted.isEmpty())
        return true;
    // Server search is by substring (jane+fairfax@gmail.com also finds
    // jane+fairfax@gmail.com.au), so the headers are compared whole.
    QStringList have;
    for (const QString &h : {message.to, message.cc, message.deliveredTo, message.xOriginalTo})
        have << addressesIn(h);
    for (const QString &w : wanted)
        if (have.contains(w))
            return true;
    return false;
}

void MailFetch::runCycle() {
    const quint64 session = m_session;
    m_inIdle = false;
    m_cycleBusy = true;
    setState(State::Fetching);
    const QByteArray criteria = searchCriteria(m_config);
    m_client->uidSearch(criteria, [this, session](bool ok, const QList<qint64> &uids, const QString &error) {
        if (session != m_session)
            return;
        if (!ok) {
            failSession("Searching the mailbox failed: " + error, true);
            return;
        }
        m_todo.clear();
        qint64 floor = 0;
        if (m_config.after == "leave") {
            const QStringList mark = m_store->setting(markKey(m_config)).toString().split(':');
            if (mark.size() == 2 && mark[0].toLongLong() == m_validity)
                floor = mark[1].toLongLong();
        }
        for (qint64 uid : uids)
            if (uid > floor && !m_rejected.contains(uid))
                m_todo.append(uid);
        nextBatch();
    });
}

void MailFetch::nextBatch() {
    const quint64 session = m_session;
    if (m_todo.isEmpty()) {
        finishCycle();
        return;
    }
    m_batch = m_todo.mid(0, 100);
    m_todo = m_todo.mid(m_batch.size());
    m_client->uidFetchSizes(m_batch, [this, session](bool ok, const QHash<qint64, qint64> &sizes, const QString &error) {
        if (session != m_session)
            return;
        if (!ok) {
            failSession("Couldn't read message sizes: " + error, true);
            return;
        }
        m_sizes = sizes;
        processNext();
    });
}

void MailFetch::processNext() {
    const quint64 session = m_session;
    if (m_batch.isEmpty()) {
        nextBatch();
        return;
    }
    const qint64 uid = m_batch.takeFirst();
    if (m_sizes.value(uid) > MaxMessageSize) {
        ++m_skipped;
        m_rejected.insert(uid);
        advanceMark(uid);
        emit stateChanged();
        QMetaObject::invokeMethod(this, [this, session] { if (session == m_session) processNext(); }, Qt::QueuedConnection);
        return;
    }
    m_client->uidFetchMessage(uid, [this, session, uid](bool ok, const QByteArray &bytes, const QString &error) {
        if (session != m_session)
            return;
        if (!ok) {
            if (error.endsWith("is gone.")) {   // Deleted since the search: nothing to do.
                processNext();
                return;
            }
            failSession("Couldn't fetch a message: " + error, true);
            return;
        }
        const MailTask::Message message = MailTask::parse(bytes);
        if (!toFilterMatches(message, m_config.toFilter)) {
            m_rejected.insert(uid);
            advanceMark(uid);
            processNext();
            return;
        }
        MailTask::Options options;
        options.listId = m_config.listId;
        options.naturalLanguage = m_config.naturalLanguage;
        const MailTask::Result result = MailTask::import(*m_store, message, options);
        if (result.reason == "no list") {
            failSession("There is no list for new mail tasks. Choose one in Settings.", false);
            return;
        }
        if (result.ok()) {
            ++m_imported;
            emit stateChanged();
        }
        afterImport(uid);   // Duplicates and empty messages get the same treatment, or they would be found again.
    });
}

void MailFetch::afterImport(qint64 uid) {
    const quint64 session = m_session;
    auto next = [this, session, uid](const ImapClient::Response &r, const QString &what) {
        if (session != m_session)
            return;
        if (!r.ok) {
            failSession(what + ": " + r.text, r.disconnected);
            return;
        }
        advanceMark(uid);
        processNext();
    };
    if (m_config.after == "read") {
        m_client->uidStoreSeen(uid, [next](const ImapClient::Response &r) { next(r, "Couldn't mark a message as read"); });
        return;
    }
    if (m_config.after == "move" && !m_config.moveTo.isEmpty() && m_config.moveTo != m_config.folder) {
        auto move = [this, uid, next] {
            m_client->uidMove(uid, m_config.moveTo, [next](const ImapClient::Response &r) { next(r, "Couldn't move a message"); });
        };
        if (m_moveFolderReady) {
            move();
            return;
        }
        // CREATE answers NO when the folder is already there, which is fine.
        m_client->createFolder(m_config.moveTo, [this, session, move](const ImapClient::Response &r) {
            if (session != m_session)
                return;
            if (r.disconnected) {
                failSession("The connection was lost.", true);
                return;
            }
            m_moveFolderReady = true;
            move();
        });
        return;
    }
    advanceMark(uid);
    processNext();
}

void MailFetch::advanceMark(qint64 uid) {
    if (m_config.after != "leave")
        return;
    const QString key = markKey(m_config);
    const QStringList mark = m_store->setting(key).toString().split(':');
    const qint64 current = mark.size() == 2 && mark[0].toLongLong() == m_validity ? mark[1].toLongLong() : 0;
    if (uid > current)
        put(key, QString::number(m_validity) + ":" + QString::number(uid));
}

void MailFetch::finishCycle() {
    m_cycleBusy = false;
    m_lastCheck = QDateTime::currentDateTime();
    m_failures = 0;
    setState(State::Idle);
    emit checked(m_imported);
    if (m_again) {
        m_again = false;
        runCycle();
        return;
    }
    if (m_client->hasCapability("IDLE")) {
        enterIdle();
        return;
    }
    // No IDLE: say goodbye and look again in a while.
    ImapClient *client = m_client;
    m_client = nullptr;
    client->disconnect(this);
    client->logout([client](const ImapClient::Response &) { client->deleteLater(); });
    ++m_session;
    const int ms = m_timings.pollOverride ? m_timings.pollOverride : qMax(1, option("mail.interval").toInt()) * 60000;
    m_poll.start(ms);
}

void MailFetch::enterIdle() {
    const quint64 session = m_session;
    m_inIdle = true;
    m_stopRequested = false;
    m_idleRenew.start(m_timings.idleRenew);
    m_client->idle([this, session](qint64) {
        if (session == m_session && m_inIdle && !m_stopRequested)
            leaveIdle([this] { runCycle(); });
    }, [this, session](const ImapClient::Response &r) {
        if (session != m_session)
            return;
        if (m_stopRequested) {
            m_stopRequested = false;
            m_inIdle = false;
            const auto then = m_afterIdle;
            m_afterIdle = {};
            if (then)
                then();
            return;
        }
        // The server ended it by itself.
        failSession(r.ok ? QStringLiteral("The mail server ended the connection.") : QStringLiteral("The connection to the mail server was lost."), true);
    });
}

void MailFetch::leaveIdle(std::function<void()> then) {
    if (!m_inIdle || m_stopRequested)
        return;
    m_stopRequested = true;
    m_afterIdle = std::move(then);
    m_idleRenew.stop();
    m_client->stopIdle();
}
