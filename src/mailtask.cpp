#include "mailtask.h"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <array>

#include "alarms.h"
#include "dateparse.h"
#include "exchange.h"
#include "model.h"
#include "nlparse.h"
#include "recurrence.h"
#include "store.h"

namespace MailTask {

namespace {

constexpr int kMaxNotes = 10000;  // "Up to 10,000 characters of notes per task."
constexpr int kMaxTitle = 1000;
constexpr int kMaxDepth = 12;     // Multiparts nested deeper than this are junk.

// ------------------------------------------------------------------ parsing

struct Header {
    QString name;  // Lower case.
    QByteArray value;
};

struct Part {
    QList<Header> headers;
    QByteArray body;
    bool hasHeaders = false;

    QByteArray header(const QString &name) const {
        for (const auto &h : headers)
            if (h.name == name)
                return h.value;
        return {};
    }
};

QString text8(const QByteArray &bytes) {
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString s = utf8.decode(bytes);
    return utf8.hasError() ? QString::fromLatin1(bytes) : s;
}

// Splits a part at its first blank line and unfolds the header lines, which
// continue on lines that start with whitespace.
Part splitPart(QByteArray raw) {
    Part part;
    // An mbox "From " line, as procmail and friends write it, is not a header.
    if (raw.startsWith("From ")) {
        const qsizetype eol = raw.indexOf('\n');
        raw = eol < 0 ? QByteArray() : raw.mid(eol + 1);
    }
    qsizetype end = 0, bodyStart = 0;
    if (raw.startsWith("\r\n")) {
        bodyStart = 2;
    } else if (raw.startsWith('\n')) {
        bodyStart = 1;
    } else {
        const qsizetype crlf = raw.indexOf("\r\n\r\n"), lf = raw.indexOf("\n\n");
        if (crlf >= 0 && (lf < 0 || crlf < lf)) {
            end = crlf;
            bodyStart = crlf + 4;
        } else if (lf >= 0) {
            end = lf;
            bodyStart = lf + 2;
        } else {
            end = bodyStart = raw.size();
        }
    }
    part.body = raw.mid(bodyStart);
    for (const QByteArray &rawLine : raw.left(end).split('\n')) {
        QByteArray line = rawLine;
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if ((line.startsWith(' ') || line.startsWith('\t')) && !part.headers.isEmpty()) {
            part.headers.last().value += ' ' + line.trimmed();
            continue;
        }
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0 || line.left(colon).contains(' ')) {
            // Not a header line, so this is not a message with headers.
            part.headers.clear();
            part.body = raw;
            return part;
        }
        part.headers.append({QString::fromLatin1(line.left(colon)).toLower(), line.mid(colon + 1).trimmed()});
    }
    part.hasHeaders = !part.headers.isEmpty();
    return part;
}

QString headerText(const Part &part, const QString &name) {
    return Exchange::decodeMimeWords(text8(part.header(name))).trimmed();
}

// Every value of a header that can repeat, joined as one address list. Each
// delivery hop adds its own Delivered-To, so the first is not always the one.
QString allHeaderText(const Part &part, const QString &name) {
    QStringList values;
    for (const auto &h : part.headers)
        if (h.name == name)
            values << Exchange::decodeMimeWords(text8(h.value)).trimmed();
    return values.join(", ");
}

// A Content-Type or Content-Disposition value: the main token and its
// parameters, with quoted values unquoted and RFC 2231 extended ones decoded.
struct Field {
    QString value;  // Lower case: "text/plain", "attachment".
    QHash<QString, QString> params;
};

Field parseField(const QByteArray &raw) {
    Field field;
    QStringList pieces;
    QString current;
    bool quoted = false;
    for (const QChar c : text8(raw)) {
        if (c == '"')
            quoted = !quoted;
        if (c == ';' && !quoted) {
            pieces << current;
            current.clear();
        } else {
            current += c;
        }
    }
    pieces << current;
    field.value = pieces.takeFirst().trimmed().toLower();
    for (const QString &piece : pieces) {
        const qsizetype eq = piece.indexOf('=');
        if (eq <= 0)
            continue;
        QString key = piece.left(eq).trimmed().toLower();
        QString value = piece.mid(eq + 1).trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2).replace("\\\"", "\"");
        if (key.endsWith('*')) {
            // filename*=utf-8''%E2%82%AC.txt
            key.chop(1);
            const QStringList bits = value.split('\'');
            if (bits.size() >= 3) {
                QStringDecoder decoder(bits.at(0).toLatin1().constData());
                const QByteArray bytes = QByteArray::fromPercentEncoding(bits.mid(2).join('\'').toLatin1());
                value = decoder.isValid() ? QString(decoder.decode(bytes)) : text8(bytes);
            }
        }
        field.params.insert(key, value);
    }
    return field;
}

QByteArray decodeQuotedPrintable(const QByteArray &in) {
    QByteArray out;
    out.reserve(in.size());
    const QList<QByteArray> lines = in.split('\n');
    for (qsizetype n = 0; n < lines.size(); ++n) {
        QByteArray line = lines.at(n);
        if (line.endsWith('\r'))
            line.chop(1);
        // Whitespace before a line break is not part of the text (RFC 2045).
        while (line.endsWith(' ') || line.endsWith('\t'))
            line.chop(1);
        const bool soft = line.endsWith('=');
        if (soft)
            line.chop(1);
        for (qsizetype i = 0; i < line.size(); ++i) {
            bool ok = false;
            const int v = line[i] == '=' && i + 2 < line.size() ? line.mid(i + 1, 2).toInt(&ok, 16) : 0;
            if (ok) {
                out.append(char(v));
                i += 2;
            } else {
                out.append(line[i]);
            }
        }
        if (!soft && n + 1 < lines.size())
            out.append('\n');
    }
    return out;
}

QByteArray decodeTransfer(const QByteArray &body, const QString &encoding) {
    const QString e = encoding.trimmed().toLower();
    if (e == "base64")
        return QByteArray::fromBase64(body);
    if (e == "quoted-printable")
        return decodeQuotedPrintable(body);
    return body;
}

// The charset when it names one Qt knows and the bytes are valid in it, else
// UTF-8, else Latin-1 (which accepts anything).
QString decodeCharset(const QByteArray &bytes, const QString &charset) {
    if (!charset.isEmpty()) {
        QStringDecoder decoder(charset.toLatin1().constData());
        if (decoder.isValid()) {
            const QString s = decoder.decode(bytes);
            if (!decoder.hasError())
                return s;
        }
    }
    return text8(bytes);
}

QString normaliseNewlines(QString s) {
    return s.replace("\r\n", "\n").replace('\r', '\n');
}

QString decodeEntities(const QString &s) {
    static const QHash<QString, QString> named = {{"nbsp", " "}, {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""},
                                                  {"apos", "'"}, {"copy", "©"}, {"hellip", "…"},
                                                  {"ndash", "–"}, {"mdash", "—"}, {"rsquo", "’"},
                                                  {"lsquo", "‘"}, {"rdquo", "”"}, {"ldquo", "“"}};
    static const QRegularExpression entity("&(#x[0-9a-fA-F]+|#[0-9]+|[a-zA-Z]+);");
    QString out;
    qsizetype last = 0;
    auto it = entity.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString name = m.captured(1);
        QString repl;
        if (name.startsWith("#x") || name.startsWith('#')) {
            const bool hex = name.startsWith("#x");
            const char32_t code = char32_t(name.mid(hex ? 2 : 1).toUInt(nullptr, hex ? 16 : 10));
            repl = QString::fromUcs4(&code, 1);
        } else if (named.contains(name)) {
            repl = named.value(name);
        } else {
            continue;  // An entity we don't know stays as it is.
        }
        out += s.mid(last, m.capturedStart() - last) + repl;
        last = m.capturedEnd();
    }
    return out + s.mid(last);
}

// Enough HTML-to-text for mail. Not QTextDocumentFragment: that needs a GUI
// application for its fonts, and `precrime import-mail` runs headless.
QString htmlToText(QString html) {
    static const QRegularExpression comments("<!--.*?-->", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression blocks("<(style|script|head|title)\\b.*?</\\1\\s*>",
                                           QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression space("\\s+");
    static const QRegularExpression br("<br\\b[^>]*>", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression blockEdge("</?(p|div|tr|table|ul|ol|blockquote|h[1-6]|pre)\\b[^>]*>",
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression li("<li\\b[^>]*>", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cell("</t[dh]\\s*>", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tag("<[^>]*>");
    html.remove(comments).remove(blocks).replace(space, " ");
    html.replace(br, "\n").replace(blockEdge, "\n").replace(li, "\n- ").replace(cell, " ");
    html.remove(tag);
    QStringList kept;
    int blanks = 0;
    for (const QString &line : decodeEntities(html).split('\n')) {
        const QString t = line.trimmed();
        blanks = t.isEmpty() ? blanks + 1 : 0;
        if (blanks <= 1)
            kept << t;
    }
    return kept.join('\n').trimmed();
}

struct Text {
    QString text;
    bool plain = false;  // Written as plain text, not converted from HTML.
};

// The parts between the boundary lines of a multipart body.
QList<QByteArray> splitMultipart(const QByteArray &body, const QByteArray &boundary) {
    const QByteArray delimiter = "--" + boundary;
    QList<QByteArray> parts;
    qsizetype pos = 0, partStart = -1;
    for (;;) {
        const qsizetype at = body.indexOf(delimiter, pos);
        if (at < 0)
            break;
        if (at != 0 && body[at - 1] != '\n') {
            pos = at + delimiter.size();
            continue;
        }
        if (partStart >= 0) {
            QByteArray chunk = body.mid(partStart, at - partStart);
            if (chunk.endsWith("\r\n"))
                chunk.chop(2);
            else if (chunk.endsWith('\n'))
                chunk.chop(1);
            parts.append(chunk);
        }
        const qsizetype after = at + delimiter.size();
        if (body.mid(after, 2) == "--")
            return parts;
        const qsizetype eol = body.indexOf('\n', after);
        if (eol < 0)
            return parts;
        partStart = eol + 1;
        pos = partStart;
    }
    // A missing closing delimiter still leaves a last part.
    if (partStart >= 0)
        parts.append(body.mid(partStart));
    return parts;
}

QString attachmentName(const Field &disposition, const Field &type) {
    QString name = disposition.params.value("filename");
    if (name.isEmpty())
        name = type.params.value("name");
    return Exchange::decodeMimeWords(name).trimmed();
}

Text walk(const Part &part, Message &message, int depth) {
    const Field type = parseField(part.header("content-type"));
    const Field disposition = parseField(part.header("content-disposition"));
    const QString mime = type.value.isEmpty() ? QStringLiteral("text/plain") : type.value;

    if (mime.startsWith("multipart/")) {
        const QByteArray boundary = type.params.value("boundary").toLatin1();
        if (depth >= kMaxDepth || boundary.isEmpty())
            return {};
        QList<Text> children;
        for (const QByteArray &raw : splitMultipart(part.body, boundary))
            children.append(walk(splitPart(raw), message, depth + 1));
        if (mime == "multipart/alternative") {
            // The same content in several forms: plain text if there is any, else the first form with text.
            for (const Text &c : children)
                if (c.plain && !c.text.isEmpty())
                    return c;
            for (const Text &c : children)
                if (!c.text.isEmpty())
                    return c;
            return {};
        }
        Text joined;
        joined.plain = true;
        QStringList texts;
        for (const Text &c : children)
            if (!c.text.isEmpty()) {
                texts << c.text;
                joined.plain = joined.plain && c.plain;
            }
        joined.text = texts.join("\n\n");
        return joined;
    }

    const QByteArray decoded = decodeTransfer(part.body, text8(part.header("content-transfer-encoding")));

    // A forwarded message contributes its text, even when it comes as an attachment.
    if (mime == "message/rfc822") {
        if (depth >= kMaxDepth)
            return {};
        return walk(splitPart(decoded), message, depth + 1);
    }

    const QString name = attachmentName(disposition, type);
    const bool attached = disposition.value == "attachment" || (!name.isEmpty() && disposition.value != "inline");
    if ((mime == "text/plain" || mime == "text/html") && !attached) {
        const QString text = normaliseNewlines(decodeCharset(decoded, type.params.value("charset")));
        if (mime == "text/html")
            return {htmlToText(text), false};
        return {text.trimmed(), true};
    }
    message.attachments << (name.isEmpty() ? mime : name);
    return {};
}

}  // namespace

Message parse(const QByteArray &rfc822) {
    Message m;
    const Part top = splitPart(rfc822);
    if (!top.hasHeaders)
        return m;
    m.valid = true;
    m.subject = headerText(top, "subject");
    m.from = headerText(top, "from");
    m.to = headerText(top, "to");
    m.cc = headerText(top, "cc");
    m.deliveredTo = allHeaderText(top, "delivered-to");
    m.xOriginalTo = allHeaderText(top, "x-original-to");
    m.messageId = headerText(top, "message-id");
    m.dateHeader = headerText(top, "date");
    static const QRegularExpression comment("\\s*\\([^)]*\\)\\s*$");
    QString dateText = m.dateHeader;
    dateText.remove(comment);
    m.date = QDateTime::fromString(dateText, Qt::RFC2822Date);
    m.body = walk(top, m, 0).text;
    return m;
}

QString uidFor(const Message &message) {
    const QString basis = message.messageId.isEmpty()
                              ? QStringList{message.dateHeader, message.from, message.subject, message.body}.join('\n')
                              : message.messageId;
    return QStringLiteral("mail-") +
           QString::fromLatin1(QCryptographicHash::hash(basis.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

// --------------------------------------------------------------- importing

namespace {

// The manual: "Re:" and "Fwd:" are trimmed off. Other mail clients' words for
// the same thing (German AW and WG, Scandinavian SV, Spanish and Dutch VS) too.
QString stripReplyPrefixes(QString subject) {
    static const QRegularExpression prefix("^\\s*(re|fwd?|aw|wg|sv|vs)\\s*:\\s*", QRegularExpression::CaseInsensitiveOption);
    for (;;) {
        const auto m = prefix.match(subject);
        if (!m.hasMatch())
            return subject.trimmed();
        subject.remove(0, m.capturedLength());
    }
}

struct Filing {
    QDate startDate, dueDate;
    int startTime = -1, dueTime = -1;
    int priority = 0;
    QString list;
    QStringList tags;
    int actionType = ActionType::None;
    QString actionValue;
};

// Takes the filing instructions out of `title` and returns what is left. An
// instruction that makes no sense (an unreadable date, an unknown action type)
// and an unknown one stay in the title, so nothing is lost silently.
QString extractFiling(const QString &title, const QDateTime &now, Filing &filing) {
    static const QRegularExpression instruction("\\b(start|due|priority|list|tag|action)\\s*\\(([^()]*)\\)",
                                                QRegularExpression::CaseInsensitiveOption);
    static const QHash<QString, int> actionTypes = {{"call", ActionType::Call}, {"message", ActionType::Message},
                                                    {"url", ActionType::Url}, {"visit", ActionType::Visit},
                                                    {"google", ActionType::Google}, {"email", ActionType::Mail},
                                                    {"mail", ActionType::Mail}};
    QList<QPair<qsizetype, qsizetype>> cuts;
    auto it = instruction.globalMatch(title);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString key = m.captured(1).toLower();
        const QString arg = m.captured(2).trimmed();
        bool understood = false;
        if (key == "start" || key == "due") {
            const DateSuggestion d = DateParse::best(arg, now);
            if (d.date.isValid() || d.minutes >= 0) {
                // A time on its own means today.
                const QDate date = d.date.isValid() ? d.date : now.date();
                (key == "due" ? filing.dueDate : filing.startDate) = date;
                (key == "due" ? filing.dueTime : filing.startTime) = d.minutes;
                understood = true;
            }
        } else if (key == "priority") {
            static const QRegularExpression bangs("^!{1,3}$");
            if (bangs.match(arg).hasMatch()) {
                filing.priority = int(arg.size());
                understood = true;
            }
        } else if (key == "list") {
            // A name that matches no list is understood too: the task then goes to the destination.
            if (!arg.isEmpty()) {
                filing.list = arg;
                understood = true;
            }
        } else if (key == "tag") {
            for (QString name : arg.split(',')) {
                name = name.trimmed();
                if (name.startsWith('#'))
                    name.remove(0, 1);
                if (name.isEmpty())
                    continue;
                understood = true;
                if (!filing.tags.contains(name, Qt::CaseInsensitive))
                    filing.tags << name;
            }
        } else if (key == "action") {
            const qsizetype comma = arg.indexOf(',');
            const QString value = comma < 0 ? QString() : arg.mid(comma + 1).trimmed();
            const int type = comma < 0 ? 0 : actionTypes.value(arg.left(comma).trimmed().toLower(), 0);
            if (type && !value.isEmpty()) {
                filing.actionType = type;
                filing.actionValue = value;
                understood = true;
            }
        }
        if (understood)
            cuts.append({m.capturedStart(), m.capturedLength()});
    }
    QString out = title;
    for (auto c = cuts.rbegin(); c != cuts.rend(); ++c)
        out.remove(c->first, c->second);
    static const QRegularExpression runs("\\s{2,}");
    return out.replace(runs, " ").trimmed();
}

struct Checklist {
    QStringList items;
    bool mixed = false;  // Other text than the numbered lines (a signature counts), so the notes keep the whole message.
};

// The manual: a body that starts with "1." and has one item per numbered line
// is a checklist; a signature ends the items.
Checklist readChecklist(const QString &body) {
    static const QRegularExpression first("^1\\.(?!\\d)");
    static const QRegularExpression numbered("^\\d+\\.(?!\\d)\\s*(.*)$");
    static const QRegularExpression signature("^(--|Sent from my\\b.*|Get Outlook for\\b.*)$",
                                              QRegularExpression::CaseInsensitiveOption);
    Checklist list;
    if (!first.match(body).hasMatch())
        return list;
    bool signedOff = false;
    for (const QString &raw : body.split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        signedOff = signedOff || signature.match(line).hasMatch();
        if (signedOff) {
            list.mixed = true;
            continue;
        }
        const auto m = numbered.match(line);
        if (m.hasMatch() && !m.captured(1).trimmed().isEmpty())
            list.items << m.captured(1).trimmed();
        else
            list.mixed = true;
    }
    return list;
}

QString firstLine(const QString &body) {
    for (const QString &line : body.split('\n'))
        if (!line.trimmed().isEmpty())
            return line.trimmed().left(200);
    return {};
}

bool usable(const TaskList &l) { return !l.deleted && !l.archived && l.kind != ListKind::Smart; }

}  // namespace

Result import(Store &store, const Message &message, const Options &options) {
    Result result;
    const QString subject = message.subject.trimmed();
    if (subject.isEmpty() && message.body.trimmed().isEmpty()) {
        result.reason = QStringLiteral("empty");
        return result;
    }
    const QString uid = uidFor(message);
    for (const auto &t : store.tasks())
        if (t.uid == uid) {
            result.reason = QStringLiteral("duplicate");
            return result;
        }

    const QDateTime now = options.now.isValid() ? options.now : QDateTime::currentDateTime();
    Filing filing;
    QString title = extractFiling(stripReplyPrefixes(subject), now, filing);

    // Where it goes: list(Name) when that list exists, else the destination, else the Inbox.
    qint64 listId = 0;
    if (options.listId && store.lists().contains(options.listId) && usable(store.list(options.listId)))
        listId = options.listId;
    if (!listId)
        for (const auto &l : store.lists())
            if (l.kind == ListKind::Inbox && usable(l)) {
                listId = l.id;
                break;
            }
    if (!listId) {
        double lowest = 0;
        for (const auto &l : store.lists())
            if (usable(l) && (!listId || l.order < lowest)) {
                listId = l.id;
                lowest = l.order;
            }
    }
    bool listNamed = false;
    if (!filing.list.isEmpty())
        for (const auto &l : store.lists())
            if (usable(l) && l.name.compare(filing.list, Qt::CaseInsensitive) == 0) {
                listId = l.id;
                listNamed = true;
                break;
            }

    Task task;
    task.uid = uid;
    task.startDate = filing.startDate;
    task.startTime = filing.startTime;
    task.dueDate = filing.dueDate;
    task.dueTime = filing.dueTime;
    task.priority = filing.priority;
    task.actionType = filing.actionType;
    task.actionValue = filing.actionValue;
    QStringList tagNames = filing.tags;
    QList<int> alertOffsets;

    // Quick Entry's own parser, for those who want "tomorrow" and "#home" to
    // work in a subject. The explicit instructions above win over it.
    if (options.naturalLanguage && !title.isEmpty()) {
        QStringList names;
        for (const auto &l : store.lists())
            if (usable(l))
                names << l.name;
        const ParsedEntry parsed = NaturalLanguage::parse(title, now, names);
        title = parsed.title;
        if (parsed.startDate.isValid() && !task.startDate.isValid()) {
            task.startDate = parsed.startDate;
            task.startTime = parsed.startTime;
        }
        if (!task.dueDate.isValid()) {
            if (parsed.dueDate.isValid()) {
                task.dueDate = parsed.dueDate;
                task.dueTime = parsed.dueTime;
            } else if (parsed.dueTime >= 0) {
                task.dueDate = now.date();
                task.dueTime = parsed.dueTime;
            }
        }
        if (parsed.duration > 0)
            task.duration = parsed.duration;
        if (parsed.priority > 0 && !task.priority)
            task.priority = parsed.priority;
        if (parsed.starred)
            task.starred = true;
        if (parsed.recurrence.isSet()) {
            task.recurrence = parsed.recurrence;
            if (!task.dueDate.isValid() && !task.startDate.isValid())
                task.dueDate = Recurrences::next(parsed.recurrence, now.date().addDays(-1));
        }
        alertOffsets = parsed.alertOffsets;
        if (!alertOffsets.isEmpty() && !task.dueDate.isValid())
            task.dueDate = now.date();
        for (const QString &t : parsed.tags)
            if (!tagNames.contains(t, Qt::CaseInsensitive))
                tagNames << t;
        if (!parsed.list.isEmpty() && !listNamed)
            for (const auto &l : store.lists())
                if (usable(l) && l.name.compare(parsed.list, Qt::CaseInsensitive) == 0) {
                    listId = l.id;
                    break;
                }
    }
    if (!listId) {
        result.reason = QStringLiteral("no list");
        return result;
    }

    const QString body = normaliseNewlines(message.body).trimmed();
    const Checklist checklist = readChecklist(body);
    const bool isChecklist = !checklist.items.isEmpty();
    task.notes = isChecklist && !checklist.mixed ? QString() : body.left(kMaxNotes);
    if (title.isEmpty())
        title = firstLine(body);
    if (title.isEmpty())
        title = QStringLiteral("(no subject)");
    task.title = title.left(kMaxTitle);
    task.type = isChecklist ? TaskType::Checklist : TaskType::Task;
    task.listId = listId;
    for (const auto &t : store.tasks())
        if (t.listId == listId && t.parentId == 0)
            task.order = qMax(task.order, t.order + 1);

    store.begin("Mail");
    // Tags are matched by name, ignoring case; missing ones are created, as Automation's create_task does.
    for (const QString &name : tagNames) {
        qint64 id = 0;
        for (const auto &t : store.tags())
            if (!t.deleted && t.name.compare(name, Qt::CaseInsensitive) == 0) {
                id = t.id;
                break;
            }
        if (!id) {
            Tag tag;
            tag.id = store.nextId("tags");
            tag.name = name;
            tag.order = tag.id;
            store.putTag(tag);
            id = tag.id;
        }
        if (!task.tagIds.contains(id))
            task.tagIds.append(id);
    }
    task.id = store.nextId("tasks");
    store.putTask(task);
    for (int offset : alertOffsets) {
        Alarm alarm;
        alarm.id = store.nextId("alarms");
        alarm.taskId = task.id;
        alarm.offset = offset;
        store.putAlarm(alarm);
    }
    AlarmTimes::applyDefaultReminder(store, store.task(task.id));
    double order = 0;
    for (const QString &item : checklist.items) {
        Task child;
        child.id = store.nextId("tasks");
        child.listId = listId;
        child.parentId = task.id;
        child.title = item.left(kMaxTitle);
        child.order = ++order;
        store.putTask(child);
    }
    store.commit();
    result.taskId = task.id;
    return result;
}

}  // namespace MailTask
