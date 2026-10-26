#pragma once

// Mail to Fairfax: turns an RFC 822 message into a task. Fairfax has no
// server to host an address, so the message arrives from an IMAP mailbox you own
// or from `fairfax import-mail`, and both come through here.
//
// parse() reads the message; import() files it. They are separate so a caller
// can look at a message (its recipients, its size) before deciding to import.

#include <QDateTime>
#include <QString>
#include <QStringList>

class Store;

namespace MailTask {

struct Message {
    QString subject, from, to, cc;
    QString deliveredTo, xOriginalTo;  // Set by the mail server, so they name the real recipient behind a plus-address or alias.
    QString messageId;                 // As written in the header, angle brackets included.
    QString dateHeader;                // The Date header as written; the identity of a message that has no Message-ID.
    QDateTime date;
    QString body;                      // The plain-text body: text/plain if there is one, else the HTML as text.
    QStringList attachments;           // File names (or content types). The content is not kept.
    bool valid = false;                // False when the input had no headers at all.
};

// Decodes a message: folded and RFC 2047 headers, multipart bodies, 7bit,
// 8bit, quoted-printable and base64, and charsets (undeclared or unknown ones
// fall back to UTF-8, then Latin-1). Never fails; garbage gives valid == false.
Message parse(const QByteArray &rfc822);

// Decodes bytes in the named charset. Qt knows more charsets on Linux than on
// macOS and Windows; ISO-8859-15 and Windows-1252, both common in mail, are
// decoded here wherever Qt lacks them. A null string means the charset is
// unknown; *ok (when given) is false then and when the bytes are invalid in it.
QString decodeNamedCharset(const QByteArray &bytes, const QString &charset, bool *ok = nullptr);
// The built-in ISO-8859-15 and Windows-1252 decoders alone: a null string for
// any other charset. (decodeNamedCharset prefers Qt's own.)
QString decodeBuiltInCharset(const QByteArray &bytes, const QString &charset);

struct Options {
    qint64 listId = 0;          // Where the task goes; 0 means the Inbox.
    bool naturalLanguage = false;  // Also read dates, tags and so on out of the subject, as Quick Entry does.
    QDateTime now;              // For relative dates; the current time when invalid.
};

struct Result {
    qint64 taskId = 0;  // 0 when nothing was created.
    QString reason;     // Then: "duplicate", "empty" or "no list".
    bool ok() const { return taskId != 0; }
};

// The uid a message's task gets: "mail-" and 32 hex characters of the SHA-256
// of its Message-ID, or of its Date, From, Subject and body when it has none.
// Two machines importing the same message therefore make the same task, which
// git sync merges.
QString uidFor(const Message &message);

// Creates the task (and a checklist's items) as one undoable change named
// "Mail". A message whose uid is already in the store, deleted or not, is a
// duplicate and creates nothing.
Result import(Store &store, const Message &message, const Options &options = {});

}  // namespace MailTask
