#include "imapclient.h"

#include <QRegularExpression>
#include <QSslError>
#include <QTimer>

namespace {

QByteArray join(const QList<qint64> &uids) {
    QByteArray out;
    for (qint64 uid : uids)
        out += (out.isEmpty() ? "" : ",") + QByteArray::number(uid);
    return out;
}

Q_GLOBAL_STATIC_WITH_ARGS(QRegularExpression, uidRe, (QStringLiteral(R"(\bUID (\d+))")))
Q_GLOBAL_STATIC_WITH_ARGS(QRegularExpression, sizeRe, (QStringLiteral(R"(\bRFC822\.SIZE (\d+))")))
Q_GLOBAL_STATIC_WITH_ARGS(QRegularExpression, existsRe, (QStringLiteral(R"(^(\d+) EXISTS\b)")))

}  // namespace

QByteArray ImapClient::quote(const QString &text) {
    QByteArray in = text.toUtf8(), out = "\"";
    for (char c : in) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + '"';
}

ImapClient::ImapClient(QObject *parent) : QObject(parent), m_socket(this), m_timer(this) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { fail(QStringLiteral("The server did not answer in time."), true); });
    connect(&m_socket, &QSslSocket::readyRead, this, &ImapClient::onReadyRead);
    connect(&m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_closed)
            fail(m_socket.errorString(), false);
    });
    connect(&m_socket, &QSslSocket::disconnected, this, [this] {
        if (!m_closed)
            fail(QStringLiteral("The server closed the connection."), false);
    });
    connect(&m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        QStringList texts;
        for (const QSslError &e : errors)
            texts << e.errorString();
        fail(QStringLiteral("TLS: ") + texts.join("; "), false);
    });
}

ImapClient::~ImapClient() {
    // Silent: callbacks must not run into a half-destroyed owner.
    m_closed = true;
    m_queue.clear();
    m_greetingDone = {};
    m_socket.abort();
}

void ImapClient::later(const std::function<void()> &fn) { QTimer::singleShot(0, this, fn); }

void ImapClient::armTimer(int ms) {
    if (ms > 0)
        m_timer.start(ms);
    else
        m_timer.stop();
}

void ImapClient::parseCapabilities(const QByteArray &text, QStringList &out) {
    // "CAPABILITY IMAP4rev1 IDLE ..." or "OK [CAPABILITY IMAP4rev1 IDLE ...] ready".
    const QString s = QString::fromLatin1(text);
    const int at = s.indexOf(QStringLiteral("CAPABILITY "), 0, Qt::CaseInsensitive);
    if (at < 0)
        return;
    QString rest = s.mid(at + 11);
    const int close = rest.indexOf(']');
    if (close >= 0)
        rest = rest.left(close);
    out = rest.toUpper().split(' ', Qt::SkipEmptyParts);
}

void ImapClient::connectToServer(const QString &host, quint16 port, Security security, Done done) {
    m_closed = false;
    m_greeted = false;
    m_greetingDone = std::move(done);
    m_buffer.clear();
    m_current = {};
    m_inLiteral = false;
    m_queue.clear();
    m_running = false;
    m_idling = false;
    m_capabilities.clear();
    armTimer(m_timeout);
    if (security == Security::Tls)
        m_socket.connectToHostEncrypted(host, port);
    else
        m_socket.connectToHost(host, port);
}

void ImapClient::close() {
    if (m_closed)
        return;
    m_closed = true;
    m_timer.stop();
    m_socket.abort();
    // Whatever was waiting learns of it, once, from the event loop.
    const QList<Pending> waiting = m_queue;
    m_queue.clear();
    m_running = false;
    m_idling = false;
    Response r;
    r.status = QStringLiteral("BYE");
    r.text = QStringLiteral("Closed.");
    r.disconnected = true;
    later([waiting, r] {
        for (const Pending &p : waiting)
            if (p.done)
                p.done(r);
    });
    emit closed();
}

void ImapClient::fail(const QString &why, bool timedOut) {
    if (m_closed)
        return;
    m_closed = true;
    m_timer.stop();
    m_socket.abort();
    Response r;
    r.status = QStringLiteral("BYE");
    r.text = why;
    r.disconnected = true;
    r.timedOut = timedOut;
    const QList<Pending> waiting = m_queue;
    Done greeting = std::move(m_greetingDone);
    m_greetingDone = {};
    m_queue.clear();
    m_running = false;
    m_idling = false;
    if (greeting && !m_greeted)
        greeting(r);
    for (const Pending &p : waiting)
        if (p.done)
            p.done(r);
    emit closed();
}

void ImapClient::command(const QByteArray &line, Done done, Continue cont, int timeoutMs) {
    Pending p;
    p.tag = "A" + QByteArray::number(m_nextTag++).rightJustified(3, '0');
    p.line = line;
    p.done = std::move(done);
    p.cont = std::move(cont);
    p.timeoutMs = timeoutMs >= 0 ? timeoutMs : m_timeout;
    if (m_closed) {
        Response r;
        r.status = QStringLiteral("BYE");
        r.text = QStringLiteral("Not connected.");
        r.disconnected = true;
        later([p, r] { if (p.done) p.done(r); });
        return;
    }
    m_queue.append(p);
    pump();
}

void ImapClient::pump() {
    if (m_running || m_queue.isEmpty() || m_closed || !m_greeted || m_upgrading)
        return;
    Pending &p = m_queue.first();
    m_socket.write(p.tag + " " + p.line + "\r\n");
    m_running = true;
    if (p.isIdle)
        armTimer(m_timeout);   // Until the server's "+ idling".
    else
        armTimer(p.timeoutMs);
}

void ImapClient::onReadyRead() {
    m_buffer.append(m_socket.readAll());
    feed();
}

void ImapClient::feed() {
    while (!m_closed) {
        if (m_inLiteral) {
            const qint64 take = qMin<qint64>(m_literalLeft, m_buffer.size());
            m_current.literals.last().append(m_buffer.left(take));
            m_buffer.remove(0, take);
            m_literalLeft -= take;
            if (m_literalLeft > 0)
                return;
            m_inLiteral = false;
        }
        const int eol = m_buffer.indexOf("\r\n");
        if (eol < 0)
            return;
        QByteArray line = m_buffer.left(eol);
        m_buffer.remove(0, eol + 2);
        // A line that ends in {n} is followed by n bytes of literal, and then
        // by the rest of the same response line.
        if (line.endsWith('}')) {
            const int brace = line.lastIndexOf('{');
            bool numeric = false;
            QByteArray digits = line.mid(brace + 1, line.size() - brace - 2);
            if (digits.endsWith('+'))
                digits.chop(1);
            const qint64 n = brace >= 0 ? digits.toLongLong(&numeric) : 0;
            if (numeric && n >= 0) {
                m_current.line += line.left(brace) + "{#" + QByteArray::number(m_current.literals.size()) + "}";
                m_current.literals.append(QByteArray());
                m_literalLeft = n;
                m_inLiteral = n > 0;
                continue;
            }
        }
        m_current.line += line;
        const Untagged whole = m_current;
        m_current = {};
        dispatch(whole);
    }
}

void ImapClient::dispatch(const Untagged &u) {
    const QByteArray &text = u.line;
    if (!m_greeted) {
        if (!text.startsWith("* "))
            return;
        m_greeted = true;
        m_timer.stop();
        const QByteArray rest = text.mid(2);
        Response r;
        r.untagged.append({rest, u.literals});
        r.status = rest.left(rest.indexOf(' ')).toUpper();
        r.ok = r.status == "OK" || r.status == "PREAUTH";
        r.text = QString::fromUtf8(rest);
        parseCapabilities(rest, m_capabilities);
        Done d = std::move(m_greetingDone);
        m_greetingDone = {};
        if (d)
            d(r);
        pump();
        return;
    }
    if (text.startsWith("+")) {
        if (m_queue.isEmpty())
            return;
        Pending &p = m_queue.first();
        if (p.isIdle) {
            m_idling = true;
            m_timer.stop();
            if (m_stopPending) {
                m_stopPending = false;
                stopIdle();
            }
            return;
        }
        const QByteArray reply = p.cont ? p.cont(text.mid(1).trimmed()) : QByteArray();
        m_socket.write(reply + "\r\n");
        return;
    }
    if (text.startsWith("* ")) {
        const QByteArray rest = text.mid(2);
        if (rest.startsWith("CAPABILITY "))
            parseCapabilities(rest, m_capabilities);
        if (m_queue.isEmpty() || !m_running)
            return;
        Pending &p = m_queue.first();
        p.response.untagged.append({rest, u.literals});
        if (p.onExists) {
            const auto m = existsRe->match(QString::fromLatin1(rest));
            if (m.hasMatch())
                p.onExists(m.captured(1).toLongLong());
        }
        return;
    }
    // Tagged.
    if (m_queue.isEmpty())
        return;
    const int space = text.indexOf(' ');
    if (space < 0 || text.left(space) != m_queue.first().tag)
        return;
    const QByteArray rest = text.mid(space + 1);
    const int sp2 = rest.indexOf(' ');
    Response r = m_queue.first().response;
    r.status = QString::fromLatin1(sp2 < 0 ? rest : rest.left(sp2)).toUpper();
    r.text = sp2 < 0 ? QString() : QString::fromUtf8(rest.mid(sp2 + 1));
    r.ok = r.status == QLatin1String("OK");
    if (r.ok)
        parseCapabilities(rest.mid(sp2 + 1), m_capabilities);   // OK [CAPABILITY ...] after login.
    finish(r);
}

void ImapClient::finish(const Response &response) {
    const Pending p = m_queue.takeFirst();
    m_running = false;
    m_idling = false;
    m_stopPending = false;
    m_timer.stop();
    if (p.done)
        p.done(response);
    pump();
}

// ------------------------------------------------------------ commands

void ImapClient::capability(Done done) {
    command("CAPABILITY", [this, done](const Response &r) {
        if (r.ok) {
            for (const Untagged &u : r.untagged)
                if (u.line.startsWith("CAPABILITY "))
                    parseCapabilities(u.line, m_capabilities);
        }
        if (done)
            done(r);
    });
}

void ImapClient::startTls(Done done) {
    command("STARTTLS", [this, done](const Response &r) {
        if (!r.ok || m_closed) {
            if (done)
                done(r);
            return;
        }
        m_upgrading = true;
        m_capabilities.clear();   // What was advertised in the clear is not to be trusted.
        armTimer(m_timeout);
        auto *conn = new QMetaObject::Connection;
        *conn = connect(&m_socket, &QSslSocket::encrypted, this, [this, done, r, conn] {
            disconnect(*conn);
            delete conn;
            m_upgrading = false;
            m_timer.stop();
            if (done)
                done(r);
            pump();
        });
        m_socket.startClientEncryption();
    });
}

void ImapClient::login(const QString &user, const QString &password, Done done) {
    bool plain = hasCapability("LOGINDISABLED");
    for (const QString *s : {&user, &password})
        for (const QChar c : *s)
            if (c.unicode() < 0x20 || c.unicode() > 0x7e)
                plain = true;
    if (plain) {
        command("AUTHENTICATE PLAIN", done, [user, password](const QByteArray &) {
            return (QByteArray(1, '\0') + user.toUtf8() + QByteArray(1, '\0') + password.toUtf8()).toBase64();
        });
        return;
    }
    command("LOGIN " + quote(user) + " " + quote(password), done);
}

void ImapClient::authenticateXoauth2(const QString &user, const QString &accessToken, Done done) {
    // The first continuation asks for the initial response; if the token is
    // refused the server sends a second one with the error as JSON, which is
    // answered with an empty line so that the tagged NO follows.
    auto sent = std::make_shared<bool>(false);
    command("AUTHENTICATE XOAUTH2", done, [user, accessToken, sent](const QByteArray &) {
        if (*sent)
            return QByteArray();
        *sent = true;
        return (QByteArray("user=") + user.toUtf8() + "\x01" + "auth=Bearer " + accessToken.toUtf8() + "\x01\x01").toBase64();
    });
}

void ImapClient::select(const QString &folder, std::function<void(const Selection &)> done) {
    command("SELECT " + quote(folder), [done](const Response &r) {
        Selection s;
        s.ok = r.ok;
        s.error = r.ok ? QString() : (r.text.isEmpty() ? QStringLiteral("Couldn't open the folder.") : r.text);
        static const QRegularExpression validity(R"(UIDVALIDITY (\d+))");
        static const QRegularExpression exists(R"(^(\d+) EXISTS)");
        for (const Untagged &u : r.untagged) {
            const QString line = QString::fromLatin1(u.line);
            if (auto m = validity.match(line); m.hasMatch())
                s.uidValidity = m.captured(1).toLongLong();
            if (auto m = exists.match(line); m.hasMatch())
                s.exists = m.captured(1).toLongLong();
        }
        done(s);
    });
}

void ImapClient::uidSearch(const QByteArray &criteria, std::function<void(bool, const QList<qint64> &, const QString &)> done) {
    command("UID SEARCH " + criteria, [done](const Response &r) {
        QList<qint64> uids;
        for (const Untagged &u : r.untagged)
            if (u.line.startsWith("SEARCH"))
                for (const QByteArray &n : u.line.mid(6).split(' '))
                    if (!n.trimmed().isEmpty())
                        uids.append(n.trimmed().toLongLong());
        std::sort(uids.begin(), uids.end());
        done(r.ok, uids, r.ok ? QString() : r.text);
    });
}

void ImapClient::uidFetchSizes(const QList<qint64> &uids, std::function<void(bool, const QHash<qint64, qint64> &, const QString &)> done) {
    if (uids.isEmpty()) {
        later([done] { done(true, {}, QString()); });
        return;
    }
    command("UID FETCH " + join(uids) + " (RFC822.SIZE)", [done](const Response &r) {
        QHash<qint64, qint64> sizes;
        for (const Untagged &u : r.untagged) {
            const QString line = QString::fromLatin1(u.line);
            const auto uid = uidRe->match(line), size = sizeRe->match(line);
            if (uid.hasMatch() && size.hasMatch())
                sizes.insert(uid.captured(1).toLongLong(), size.captured(1).toLongLong());
        }
        done(r.ok, sizes, r.ok ? QString() : r.text);
    });
}

void ImapClient::uidFetchMessage(qint64 uid, std::function<void(bool, const QByteArray &, const QString &)> done) {
    command("UID FETCH " + QByteArray::number(uid) + " (BODY.PEEK[])", [done, uid](const Response &r) {
        if (!r.ok) {
            done(false, {}, r.text);
            return;
        }
        for (const Untagged &u : r.untagged)
            if (u.line.contains(" FETCH ") && !u.literals.isEmpty()) {
                done(true, u.literals.first(), QString());
                return;
            }
        done(false, {}, QStringLiteral("Message %1 is gone.").arg(uid));
    }, {}, m_timeout * 4);
}

void ImapClient::uidStoreSeen(qint64 uid, Done done) {
    command("UID STORE " + QByteArray::number(uid) + " +FLAGS (\\Seen)", done);
}

void ImapClient::createFolder(const QString &folder, Done done) { command("CREATE " + quote(folder), done); }

void ImapClient::uidMove(qint64 uid, const QString &folder, Done done) {
    const QByteArray id = QByteArray::number(uid);
    if (hasCapability("MOVE")) {
        command("UID MOVE " + id + " " + quote(folder), done);
        return;
    }
    command("UID COPY " + id + " " + quote(folder), [this, id, done](const Response &copied) {
        if (!copied.ok) {
            done(copied);
            return;
        }
        command("UID STORE " + id + " +FLAGS (\\Deleted)", [this, id, done](const Response &flagged) {
            if (!flagged.ok || !hasCapability("UIDPLUS")) {
                done(flagged);
                return;
            }
            command("UID EXPUNGE " + id, done);
        });
    });
}

void ImapClient::idle(std::function<void(qint64)> onExists, Done done) {
    Pending p;
    p.tag = "A" + QByteArray::number(m_nextTag++).rightJustified(3, '0');
    p.line = "IDLE";
    p.done = std::move(done);
    p.timeoutMs = m_timeout;
    p.isIdle = true;
    p.onExists = std::move(onExists);
    if (m_closed) {
        Response r;
        r.status = QStringLiteral("BYE");
        r.text = QStringLiteral("Not connected.");
        r.disconnected = true;
        later([p, r] { if (p.done) p.done(r); });
        return;
    }
    m_queue.append(p);
    pump();
}

void ImapClient::stopIdle() {
    if (m_closed || m_queue.isEmpty() || !m_queue.first().isIdle)
        return;
    if (!m_idling) {
        m_stopPending = true;   // The server has not said "+ idling" yet; DONE goes out when it does.
        return;
    }
    m_socket.write("DONE\r\n");
    armTimer(m_timeout);
}

void ImapClient::logout(Done done) {
    command("LOGOUT", [this, done](const Response &r) {
        if (done)
            done(r);
        close();
    });
}
