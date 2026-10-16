#pragma once

// A small IMAP server for the tests, on 127.0.0.1 over plain TCP: mailboxes of
// real RFC 822 messages with UIDs, flags and keywords, and enough of IMAP4rev1
// for src/imapclient.cpp and src/mailfetch.cpp to talk to it. It answers like a
// real server where that matters to the tests: SEARCH matches by substring (so
// a search for jane+fairfax@gmail.com also finds jane+fairfax@gmail.com.au),
// CREATE of an existing folder is NO, MOVE gives the message a new UID, and
// X-GM-RAW exists only when the server pretends to be Gmail.
//
// It lives in the test's event loop; wait for results with QTRY_*.

#include <QBuffer>
#include <QByteArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>

class FakeImap : public QObject {
public:
    struct Msg {
        qint64 uid = 0;
        QByteArray raw;
        QSet<QString> flags;      // "\\Seen", "\\Deleted", ...
        QSet<QString> keywords;   // IMAP keywords; on the Gmail variant these are the labels.
        qint64 sizeOverride = 0;  // What RFC822.SIZE claims, to fake a huge message.
    };
    struct Folder {
        qint64 validity = 1;
        qint64 nextUid = 1;
        QList<Msg> messages;
    };

    explicit FakeImap(QObject *parent = nullptr) : QObject(parent) {
        folders["INBOX"];
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                accept(s);
        });
        const bool ok = m_server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(ok);
        Q_UNUSED(ok);
    }

    quint16 port() const { return m_server.serverPort(); }

    // What the server advertises and accepts.
    QStringList capabilities{"IMAP4rev1", "IDLE", "MOVE", "UIDPLUS"};
    QString user = "jane", password = "app-password";
    QString accessToken;                 // When set, XOAUTH2 with this bearer token is accepted.
    QMap<QString, Folder> folders;
    QSet<QString> refuse;                // Commands (like "UID MOVE") the server answers NO to.
    QString silentOn;                    // A command name (SELECT, SEARCH...) the server never answers, to provoke a timeout.
    bool gmail = false;                  // Adds X-GM-EXT-1.

    // What happened, for assertions.
    QStringList log;                     // Every command received, without its tag.
    QStringList mechanisms;              // The login method used: "LOGIN", "PLAIN" or "XOAUTH2".
    int connections = 0;
    int idlers = 0;                      // Clients idling right now.

    qint64 add(const QString &folder, const QByteArray &raw, const QSet<QString> &flags = {}, const QSet<QString> &keywords = {}) {
        Folder &f = folders[folder];
        Msg m;
        m.uid = f.nextUid++;
        m.raw = raw;
        m.flags = flags;
        m.keywords = keywords;
        f.messages.append(m);
        return m.uid;
    }

    // Tells idling clients that mail has arrived in INBOX.
    void announce() {
        for (Session *s : m_sessions)
            if (!s->idleTag.isEmpty())
                s->send("* " + QByteArray::number(folders["INBOX"].messages.size()) + " EXISTS\r\n");
    }

    void setSize(const QString &folder, qint64 uid, qint64 size) {
        for (Msg &m : folders[folder].messages)
            if (m.uid == uid)
                m.sizeOverride = size;
    }

    const Msg *find(const QString &folder, qint64 uid) const {
        // QMap::value() returns a copy; pointing into the temporary dangles,
        // which macOS's allocator turns into wrong answers (Linux's happened
        // to leave the bytes intact). Walk the map's own entry instead.
        const auto it = folders.constFind(folder);
        if (it == folders.constEnd())
            return nullptr;
        for (const Msg &m : it->messages)
            if (m.uid == uid)
                return &m;
        return nullptr;
    }
    int count(const QString &folder) const { return folders.value(folder).messages.size(); }
    bool hasFlag(const QString &folder, qint64 uid, const QString &flag) const {
        const Msg *m = find(folder, uid);
        return m && m->flags.contains(flag);
    }

    // The whole connection is dropped by the server, as a network failure would.
    void dropConnections() {
        for (Session *s : QList<Session *>(m_sessions))
            s->socket->abort();
    }

    // A plain message for tests.
    static QByteArray mail(const QString &to, const QString &subject, const QString &body = "Body text", const QString &id = QString(),
                           const QString &extraHeaders = QString()) {
        static int counter = 0;
        return QStringLiteral("Message-ID: <%1@fake.example>\r\nDate: Tue, 13 Oct 2026 09:30:00 +0200\r\nFrom: Sam <sam@example.com>\r\n"
                              "To: %2\r\nSubject: %3\r\n%4Content-Type: text/plain; charset=utf-8\r\n\r\n%5\r\n")
            .arg(id.isEmpty() ? QString("m%1").arg(++counter) : id, to, subject, extraHeaders, body).toUtf8();
    }

private:
    struct Session {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        bool authed = false;
        QString selected;
        QByteArray authTag, authMech;
        int authStep = 0;
        QByteArray idleTag;
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
            int eol;
            while ((eol = s->buffer.indexOf("\r\n")) >= 0) {
                const QByteArray line = s->buffer.left(eol);
                s->buffer.remove(0, eol + 2);
                handle(s, line);
            }
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, s] {
            if (!s->idleTag.isEmpty())
                --idlers;
            m_sessions.removeAll(s);
            s->socket->deleteLater();
            delete s;
        });
        QByteArray caps;
        for (const QString &c : currentCaps())
            caps += " " + c.toLatin1();
        s->send("* OK [CAPABILITY" + caps + "] fake IMAP ready\r\n");
    }

    QStringList currentCaps() const {
        QStringList c = capabilities;
        if (gmail)
            c << "X-GM-EXT-1";
        return c;
    }

    static QByteArray lastHeaders(const QByteArray &raw, const QString &name) {
        // Every value of the header, joined; case-insensitive on the name.
        const int end = raw.indexOf("\r\n\r\n");
        const QByteArray head = end < 0 ? raw : raw.left(end);
        QByteArray out;
        for (const QByteArray &line : head.split('\n')) {
            const QByteArray l = line.trimmed();
            if (l.toLower().startsWith(name.toLower().toLatin1() + ":"))
                out += l.mid(name.size() + 1).trimmed() + " ";
        }
        return out;
    }

    static QStringList tokens(const QString &text) {
        QStringList out;
        QString cur;
        bool quoted = false, any = false;
        for (int i = 0; i < text.size(); ++i) {
            const QChar c = text[i];
            if (quoted) {
                if (c == '\\' && i + 1 < text.size())
                    cur += text[++i];
                else if (c == '"')
                    quoted = false;
                else
                    cur += c;
            } else if (c == '"') {
                quoted = true;
                any = true;
            } else if (c == ' ') {
                if (any || !cur.isEmpty())
                    out << cur;
                cur.clear();
                any = false;
            } else {
                cur += c;
            }
        }
        if (any || !cur.isEmpty())
            out << cur;
        return out;
    }

    // ----------------------------------------------------------- SEARCH

    // X-GM-RAW: terms, OR, and parentheses.
    bool gmailMatch(const Msg &m, const QString &query) const {
        QStringList parts;   // Split on spaces outside parentheses.
        int depth = 0;
        QString cur;
        for (const QChar c : query) {
            if (c == '(') ++depth;
            if (c == ')') --depth;
            if (c == ' ' && depth == 0) {
                if (!cur.isEmpty()) parts << cur;
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.isEmpty()) parts << cur;
        // Groups of terms joined by OR are alternatives; everything else must hold.
        bool all = true;
        for (int i = 0; i < parts.size(); ++i) {
            bool one = gmailTerm(m, parts[i]);
            while (i + 2 < parts.size() && parts[i + 1] == "OR") {
                one = one || gmailTerm(m, parts[i + 2]);
                i += 2;
            }
            all = all && one;
        }
        return all;
    }
    bool gmailTerm(const Msg &m, QString term) const {
        if (term.startsWith('(') && term.endsWith(')'))
            return gmailMatch(m, term.mid(1, term.size() - 2));
        const int colon = term.indexOf(':');
        const QString key = term.left(colon).toLower(), value = term.mid(colon + 1).toLower();
        if (key == "deliveredto")
            return lastHeaders(m.raw, "Delivered-To").toLower().contains(value.toLatin1());
        if (key == "to")
            return lastHeaders(m.raw, "To").toLower().contains(value.toLatin1());
        if (key == "label")
            return m.keywords.contains(value) || m.keywords.contains(term.mid(colon + 1));
        return false;
    }

    // Matches one search key starting at tokens[i], moving i past it.
    bool key(const Folder &f, const Msg &m, const QStringList &t, int &i) const {
        const QString k = t.value(i++).toUpper();
        auto header = [&](const QString &name, const QString &value) {
            return lastHeaders(m.raw, name).toLower().contains(value.toLower().toLatin1());
        };
        if (k == "ALL") return true;
        if (k == "NOT") return !key(f, m, t, i);
        if (k == "OR") { const bool a = key(f, m, t, i); const bool b = key(f, m, t, i); return a || b; }
        if (k == "UNSEEN") return !m.flags.contains("\\Seen");
        if (k == "SEEN") return m.flags.contains("\\Seen");
        if (k == "DELETED") return m.flags.contains("\\Deleted");
        if (k == "KEYWORD") return m.keywords.contains(t.value(i++));
        if (k == "TO") return header("To", t.value(i++));
        if (k == "CC") return header("Cc", t.value(i++));
        if (k == "FROM") return header("From", t.value(i++));
        if (k == "HEADER") { const QString n = t.value(i++); return header(n, t.value(i++)); }
        if (k == "X-GM-RAW") return gmail && gmailMatch(m, t.value(i++));
        if (k == "UID") return inSet(t.value(i++), m.uid, f);
        return false;   // Unknown keys match nothing: a typo in the client shows as no results.
    }

    static bool inSet(const QString &set, qint64 uid, const Folder &f) {
        const qint64 last = f.nextUid - 1;
        for (const QString &part : set.split(',')) {
            const QStringList ends = part.split(':');
            auto num = [&](const QString &s) { return s == "*" ? last : s.toLongLong(); };
            const qint64 a = num(ends[0]), b = ends.size() > 1 ? num(ends[1]) : a;
            if (uid >= qMin(a, b) && uid <= qMax(a, b))
                return true;
        }
        return false;
    }

    // ----------------------------------------------------------- commands

    void handle(Session *s, const QByteArray &line) {
        if (s->authStep > 0) {
            handleAuthLine(s, line);
            return;
        }
        if (!s->idleTag.isEmpty()) {
            if (line.trimmed().toUpper() == "DONE") {
                s->send(s->idleTag + " OK IDLE terminated\r\n");
                s->idleTag.clear();
                --idlers;
            }
            return;
        }
        const int sp = line.indexOf(' ');
        const QByteArray tag = line.left(sp);
        QString rest = QString::fromUtf8(line.mid(sp + 1));
        log << rest;
        const QString cmd = rest.section(' ', 0, 0).toUpper();
        if (!silentOn.isEmpty() && (cmd == silentOn || (cmd == "UID" && rest.section(' ', 1, 1).toUpper() == silentOn)))
            return;
        auto ok = [&](const QString &text = "done") { s->send(tag + " OK " + text.toUtf8() + "\r\n"); };
        auto no = [&](const QString &text) { s->send(tag + " NO " + text.toUtf8() + "\r\n"); };
        auto bad = [&](const QString &text) { s->send(tag + " BAD " + text.toUtf8() + "\r\n"); };
        const QStringList t = tokens(rest);

        if (cmd == "CAPABILITY") {
            s->send("* CAPABILITY " + currentCaps().join(' ').toLatin1() + "\r\n");
            ok("CAPABILITY completed");
        } else if (cmd == "NOOP") {
            ok();
        } else if (cmd == "LOGOUT") {
            s->send("* BYE bye\r\n");
            ok("LOGOUT completed");
            s->socket->disconnectFromHost();
        } else if (cmd == "LOGIN") {
            mechanisms << "LOGIN";
            if (t.value(1) == user && t.value(2) == password) {
                s->authed = true;
                ok("[CAPABILITY " + currentCaps().join(' ') + "] logged in");
            } else {
                no("[AUTHENTICATIONFAILED] Invalid credentials");
            }
        } else if (cmd == "AUTHENTICATE") {
            s->authTag = tag;
            s->authMech = t.value(1).toUpper().toLatin1();
            s->authStep = 1;
            s->send("+ \r\n");
        } else if (!s->authed) {
            bad("Log in first");
        } else if (cmd == "SELECT") {
            const QString name = t.value(1);
            if (!folders.contains(name)) {
                no("[NONEXISTENT] no such folder");
                return;
            }
            s->selected = name;
            const Folder &f = folders[name];
            s->send("* " + QByteArray::number(f.messages.size()) + " EXISTS\r\n* OK [UIDVALIDITY " + QByteArray::number(f.validity) + "] ok\r\n");
            ok("[READ-WRITE] SELECT completed");
        } else if (cmd == "CREATE") {
            if (folders.contains(t.value(1)))
                no("[ALREADYEXISTS] folder exists");
            else {
                folders[t.value(1)];
                ok("CREATE completed");
            }
        } else if (cmd == "IDLE") {
            s->idleTag = tag;
            ++idlers;
            s->send("+ idling\r\n");
        } else if (cmd == "UID") {
            handleUid(s, tag, t, rest);
        } else {
            bad("Unknown command");
        }
    }

    void handleAuthLine(Session *s, const QByteArray &line) {
        const QByteArray decoded = QByteArray::fromBase64(line.trimmed());
        if (s->authMech == "PLAIN") {
            mechanisms << "PLAIN";
            const QList<QByteArray> parts = decoded.split('\0');
            const bool good = parts.size() == 3 && parts[1] == user.toUtf8() && parts[2] == password.toUtf8();
            s->authStep = 0;
            s->authed = good;
            s->send(s->authTag + (good ? " OK [CAPABILITY " + currentCaps().join(' ').toLatin1() + "] logged in\r\n" : QByteArray(" NO [AUTHENTICATIONFAILED] bad\r\n")));
            return;
        }
        // XOAUTH2.
        if (s->authStep == 1) {
            mechanisms << "XOAUTH2";
            const QByteArray expected = "user=" + user.toUtf8() + "\x01" + "auth=Bearer " + accessToken.toUtf8() + "\x01\x01";
            if (!accessToken.isEmpty() && decoded == expected) {
                s->authStep = 0;
                s->authed = true;
                s->send(s->authTag + " OK [CAPABILITY " + currentCaps().join(' ').toLatin1() + "] logged in\r\n");
            } else {
                s->authStep = 2;
                s->send("+ " + QByteArray("{\"status\":\"401\"}").toBase64() + "\r\n");
            }
            return;
        }
        // After the error JSON the client must answer with an empty line; anything else is a protocol error.
        s->authStep = 0;
        if (line.trimmed().isEmpty())
            s->send(s->authTag + " NO [AUTHENTICATIONFAILED] bad token\r\n");
        else
            s->send(s->authTag + " BAD expected an empty response\r\n");
    }

    void handleUid(Session *s, const QByteArray &tag, const QStringList &t, const QString &rest) {
        auto ok = [&](const QString &text = "done") { s->send(tag + " OK " + text.toUtf8() + "\r\n"); };
        auto no = [&](const QString &text) { s->send(tag + " NO " + text.toUtf8() + "\r\n"); };
        if (s->selected.isEmpty()) {
            no("Select a folder first");
            return;
        }
        Folder &f = folders[s->selected];
        const QString sub = t.value(1).toUpper();
        if (refuse.contains("UID " + sub)) {
            no("[CANNOT] refused");
            return;
        }
        if (sub == "SEARCH") {
            QByteArray out = "* SEARCH";
            QStringList keys = t.mid(2);
            for (const Msg &m : f.messages) {
                int i = 0;
                bool all = true;
                while (i < keys.size() && all)
                    all = key(f, m, keys, i);
                if (all)
                    out += " " + QByteArray::number(m.uid);
            }
            s->send(out + "\r\n");
            ok("SEARCH completed");
        } else if (sub == "FETCH") {
            const QString items = rest.mid(rest.indexOf('(')).toUpper();
            int index = 0;
            for (const Msg &m : f.messages) {
                ++index;
                if (!inSet(t.value(2), m.uid, f))
                    continue;
                QByteArray line = "* " + QByteArray::number(index) + " FETCH (UID " + QByteArray::number(m.uid);
                if (items.contains("RFC822.SIZE"))
                    line += " RFC822.SIZE " + QByteArray::number(sizeOf(m));
                if (items.contains("BODY.PEEK[]") || items.contains("BODY[]"))
                    line += " BODY[] {" + QByteArray::number(m.raw.size()) + "}\r\n" + m.raw;
                s->send(line + ")\r\n");
            }
            ok("FETCH completed");
        } else if (sub == "STORE") {
            const QString flag = rest.mid(rest.indexOf('(') + 1).section(')', 0, 0);
            for (Msg &m : f.messages)
                if (inSet(t.value(2), m.uid, f))
                    m.flags.insert(flag);
            ok("STORE completed");
        } else if (sub == "MOVE" || sub == "COPY") {
            if (sub == "MOVE" && !capabilities.contains("MOVE")) {
                s->send(tag + " BAD MOVE is not supported\r\n");
                return;
            }
            const QString dest = t.value(3);
            if (!folders.contains(dest)) {
                no("[TRYCREATE] no such folder");
                return;
            }
            for (int i = 0; i < f.messages.size(); ++i) {
                if (!inSet(t.value(2), f.messages[i].uid, f))
                    continue;
                Msg copy = f.messages[i];
                Folder &d = folders[dest];
                copy.uid = d.nextUid++;
                d.messages.append(copy);
                if (sub == "MOVE") {
                    s->send("* " + QByteArray::number(i + 1) + " EXPUNGE\r\n");
                    f.messages.removeAt(i--);
                }
            }
            ok(sub + " completed");
        } else if (sub == "EXPUNGE") {
            for (int i = 0; i < f.messages.size(); ++i)
                if (inSet(t.value(2), f.messages[i].uid, f) && f.messages[i].flags.contains("\\Deleted"))
                    f.messages.removeAt(i--);
            ok("EXPUNGE completed");
        } else {
            s->send(tag + " BAD Unknown UID command\r\n");
        }
    }

    static qint64 sizeOf(const Msg &m) { return m.sizeOverride > 0 ? m.sizeOverride : m.raw.size(); }

    QTcpServer m_server;
    QList<Session *> m_sessions;
};
