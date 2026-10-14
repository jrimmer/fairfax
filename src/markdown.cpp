#include "markdown.h"

#include <QFont>
#include <QLocale>
#include <QUrl>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QtQml>
#include <algorithm>

namespace {

using Flags = QRegularExpression::PatternOptions;
constexpr Flags kUni = QRegularExpression::UseUnicodePropertiesOption;

const QRegularExpression &fenceRe() {
    static const QRegularExpression re(R"(^ {0,3}(```|~~~))");
    return re;
}

bool isLineBreak(QChar c) { return c == u'\n' || c == QChar::ParagraphSeparator; }

bool overlaps(const QList<std::pair<int, int>> &claimed, int start, int end) {
    for (const auto &[s, e] : claimed)
        if (start < e && end > s) return true;
    return false;
}

// Punctuation that ends a sentence rather than a URL. A closing bracket only
// counts as part of the URL when it balances one inside it.
int trimmedLength(const QString &s) {
    int n = s.size();
    while (n > 0) {
        const QChar c = s[n - 1];
        if (QStringLiteral(".,;:!?'\"*").contains(c)) {
            --n;
        } else if (c == u')' || c == u']') {
            const QChar open = c == u')' ? u'(' : u'[';
            if (s.left(n).count(open) >= s.left(n).count(c)) break;
            --n;
        } else {
            break;
        }
    }
    return n;
}

QString kindOfTarget(const QString &target) {
    if (target.startsWith(u"mailto:", Qt::CaseInsensitive)) return QStringLiteral("email");
    if (target.startsWith(u'/') || target.startsWith(u"~/") || target.startsWith(u"file:", Qt::CaseInsensitive))
        return QStringLiteral("file");
    return QStringLiteral("url");
}

// Links within one line; `base` is the line's offset in the whole text.
void scanLine(const QString &line, int base, QList<Markdown::Link> &out) {
    static const QRegularExpression taskRe(R"(@\{([^{}\n]+)\})");
    static const QRegularExpression mdRe(R"(\[([^\]\n]+)\]\(([^)\s]+)\))");
    static const QRegularExpression urlRe(R"((?<![\w@])(?:https?://|www\.)[^\s<>"]+)", kUni | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression emailRe(R"((?<![\w.+\-])[\w.+\-]+@[\w\-]+(?:\.[\w\-]+)+)", kUni);

    QList<std::pair<int, int>> claimed;
    auto add = [&](QString text, QString target, QString kind, int start, int length) {
        if (length <= 0 || overlaps(claimed, start, start + length)) return;
        claimed.append({start, start + length});
        out.append({std::move(text), std::move(target), std::move(kind), base + start, length});
    };

    for (auto it = taskRe.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        const QString title = m.captured(1).trimmed();
        if (!title.isEmpty()) add(title, title, QStringLiteral("task"), m.capturedStart(), m.capturedLength());
    }
    for (auto it = mdRe.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        const QString target = m.captured(2);
        add(m.captured(1), target, kindOfTarget(target), m.capturedStart(), m.capturedLength());
    }
    for (auto it = urlRe.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        const QString raw = m.captured(0);
        const QString text = raw.left(trimmedLength(raw));
        const QString target = text.startsWith(u"www.", Qt::CaseInsensitive) ? QStringLiteral("https://") + text : text;
        add(text, target, QStringLiteral("url"), m.capturedStart(), text.size());
    }
    for (auto it = emailRe.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        QString addr = m.captured(0);
        while (addr.endsWith(u'.')) addr.chop(1);
        add(addr, QStringLiteral("mailto:") + addr, QStringLiteral("email"), m.capturedStart(), addr.size());
    }

    // A file link is a whole line: only a line that is nothing but a path (and
    // holds no other link) counts, so prose mentioning "/etc" stays prose.
    int first = 0;
    while (first < line.size() && line[first].isSpace()) ++first;
    const QString path = line.mid(first).trimmed();
    if ((path.startsWith(u"~/") && path.size() > 2) || (path.startsWith(u'/') && path.size() > 1 && !path[1].isSpace() && path[1] != u'/'))
        add(path, path, QStringLiteral("file"), first, path.size());

    // Links were collected by kind; restore text order within the line.
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
}

QVariantMap toMap(const Markdown::Link &l) {
    return {{"text", l.text}, {"target", l.target}, {"kind", l.kind}, {"start", l.start}, {"length", l.length}};
}

}  // namespace

namespace Markdown {

QList<Link> links(const QString &notes) {
    QList<Link> out;
    bool fenced = false;
    int pos = 0;
    while (pos <= notes.size()) {
        int end = pos;
        while (end < notes.size() && !isLineBreak(notes[end])) ++end;
        const QString line = notes.mid(pos, end - pos);
        if (fenceRe().match(line).hasMatch())
            fenced = !fenced;
        else if (!fenced)
            scanLine(line, pos, out);
        pos = end + 1;
    }
    return out;
}

Link linkAt(const QString &notes, int position) {
    const QList<Link> all = links(notes);
    for (const Link &l : all)
        if (position >= l.start && position < l.start + l.length) return l;
    for (const Link &l : all)
        if (position == l.start + l.length) return l;
    return {};
}

ListEdit listContinuation(const QString &notes, int cursor) {
    cursor = std::clamp(cursor, 0, int(notes.size()));
    int start = cursor;
    while (start > 0 && !isLineBreak(notes[start - 1])) --start;
    int end = cursor;
    while (end < notes.size() && !isLineBreak(notes[end])) ++end;
    if (cursor != end) return {};

    // Inside a fenced block (or on a fence line) Return is just a newline.
    bool fenced = false;
    for (int pos = 0; pos < start;) {
        int e = pos;
        while (e < notes.size() && !isLineBreak(notes[e])) ++e;
        if (fenceRe().match(notes.mid(pos, e - pos)).hasMatch()) fenced = !fenced;
        pos = e + 1;
    }
    const QString line = notes.mid(start, end - start);
    if (fenced || fenceRe().match(line).hasMatch()) return {};

    static const QRegularExpression itemRe(R"(^(\s*)(?:([-*+])|(\d{1,9})([.)]))(\s+)(\[[ xX]\](?=\s|$)\s*)?)");
    const auto m = itemRe.match(line);
    if (!m.hasMatch()) return {};

    ListEdit edit;
    if (line.mid(m.capturedEnd()).trimmed().isEmpty()) {
        edit.action = ListEdit::End;
        edit.from = start;
        edit.to = end;
        return edit;
    }
    QString marker = m.captured(2);
    if (marker.isEmpty()) marker = QString::number(m.captured(3).toLongLong() + 1) + m.captured(4);
    edit.action = ListEdit::Continue;
    edit.insert = QStringLiteral("\n") + m.captured(1) + marker + m.captured(5) + (m.capturedStart(6) >= 0 ? QStringLiteral("[ ] ") : QString());
    return edit;
}

QStringList stampNames() {
    return {QStringLiteral("Short Date"),        QStringLiteral("Short Date & Time"), QStringLiteral("Long Date"),
            QStringLiteral("Long Date & Time"),  QStringLiteral("Universal Date"),    QStringLiteral("Universal Date & Time"),
            QStringLiteral("ISO 8601 Date & Time"), QStringLiteral("Time"),           QStringLiteral("Weekday"),
            QStringLiteral("Month")};
}

QString stamp(int format, const QDateTime &moment) {
    const QLocale locale = QLocale::system();
    const QDate date = moment.date();
    const QString time = locale.toString(moment.time(), QLocale::ShortFormat);
    switch (format) {
    case 0: return locale.toString(date, QLocale::ShortFormat);
    case 1: return locale.toString(date, QLocale::ShortFormat) + QStringLiteral(" ") + time;
    case 2: return locale.toString(date, QLocale::LongFormat);
    case 3: return locale.toString(date, QLocale::LongFormat) + QStringLiteral(" ") + time;
    case 4: return date.toString(QStringLiteral("yyyy-MM-dd"));
    case 5: return date.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(" ") + time;
    case 6: return moment.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
    case 7: return time;
    case 8: return locale.dayName(date.dayOfWeek(), QLocale::LongFormat);
    case 9: return locale.monthName(date.month(), QLocale::LongFormat);
    default: return {};
    }
}

QString linksAsText(const QString &notes) {
    QStringList lines;
    for (const Link &l : links(notes)) {
        if (l.kind == QLatin1String("task")) lines << QStringLiteral("@{") + l.target + u'}';
        else if (l.kind == QLatin1String("email")) lines << l.target.mid(l.target.startsWith(u"mailto:", Qt::CaseInsensitive) ? 7 : 0);
        else lines << l.target;
    }
    return lines.join(u'\n');
}

QString clampNotes(const QString &notes, bool *truncated) {
    if (truncated) *truncated = false;
    // A UTF-16 unit is at most 3 bytes of UTF-8, so short notes need no encoding pass.
    if (qint64(notes.size()) * 3 <= kNotesLimit) return notes;
    const QByteArray bytes = notes.toUtf8();
    if (bytes.size() <= kNotesLimit) return notes;
    // Back up to the start of the character the limit falls inside.
    int cut = kNotesLimit;
    while (cut > 0 && (static_cast<uchar>(bytes[cut]) & 0xC0) == 0x80) --cut;
    if (truncated) *truncated = true;
    return QString::fromUtf8(bytes.constData(), cut);
}

QString previewSource(const QString &notes) {
    static const QRegularExpression codeRe(R"((`+)(?!`)(.+?)(?<!`)\1(?!`))");
    static const QRegularExpression markRe(R"((?<![\\:]):{2}(?=[^\s:])(.+?)(?<=[^\s:]):{2}(?!:))");
    static const QRegularExpression underRe(R"(\[u\](.+?)\[/u\])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression taskRe(R"(@\{([^{}\n]+)\})");

    auto convert = [](QString s) {
        s.replace(underRe, QStringLiteral("<u>\\1</u>"));
        s.replace(markRe, QStringLiteral("<mark>\\1</mark>"));
        QString out;
        int last = 0;
        for (auto it = taskRe.globalMatch(s); it.hasNext();) {
            const auto m = it.next();
            const QString title = m.captured(1).trimmed();
            if (title.isEmpty()) continue;
            QString label = title;
            label.replace(u'\\', QStringLiteral("\\\\")).replace(u'[', QStringLiteral("\\[")).replace(u']', QStringLiteral("\\]"));
            out += s.mid(last, m.capturedStart() - last) + u'[' + label + QStringLiteral("](fairfax-task:")
                   + QString::fromLatin1(QUrl::toPercentEncoding(title)) + u')';
            last = m.capturedEnd();
        }
        return out + s.mid(last);
    };

    QStringList out;
    bool fenced = false;
    for (const QString &line : notes.split(u'\n')) {
        if (fenceRe().match(line).hasMatch()) {
            fenced = !fenced;
            out << line;
        } else if (fenced) {
            out << line;
        } else {
            // Code spans keep their text exactly as typed.
            QString result;
            int last = 0;
            for (auto it = codeRe.globalMatch(line); it.hasNext();) {
                const auto m = it.next();
                result += convert(line.mid(last, m.capturedStart() - last)) + m.captured(0);
                last = m.capturedEnd();
            }
            out << result + convert(line.mid(last));
        }
    }
    return out.join(u'\n');
}

}  // namespace Markdown

// ---------------------------------------------------------------------------

namespace {

enum State { Normal = 0, InFence = 1 };

struct Range {
    qsizetype start, length;
};

}  // namespace

MarkdownHighlighter::MarkdownHighlighter(QObject *parent) : QSyntaxHighlighter(parent) {}

void MarkdownHighlighter::setColors(const QColor &text, const QColor &muted, const QColor &accent, const QColor &code,
                                    const QColor &link, const QColor &heading) {
    m_text = text;
    m_muted = muted;
    m_accent = accent;
    m_code = code;
    m_link = link;
    m_heading = heading;
    rehighlight();
}

void MarkdownHighlighter::setUnresolved(const QStringList &titles) {
    if (titles == m_unresolved) return;
    m_unresolved = titles;
    rehighlight();
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const int n = text.size();
    const bool wasFenced = previousBlockState() == InFence;

    // One format per character, merged from several passes and flushed as runs
    // at the end. Markers are applied last so they stay muted whatever else
    // styled them.
    QTextCharFormat base;
    if (m_text.isValid()) base.setForeground(m_text);
    QList<QTextCharFormat> fmt(n, base);
    QList<Range> markers;
    auto apply = [&](int start, int length, const QTextCharFormat &f) {
        for (int i = std::max(0, start); i < std::min(n, start + length); ++i) fmt[i].merge(f);
    };
    auto fg = [](const QColor &c) {
        QTextCharFormat f;
        if (c.isValid()) f.setForeground(c);
        return f;
    };
    auto flush = [&] {
        QTextCharFormat markerFormat;
        if (m_muted.isValid()) markerFormat.setForeground(m_muted);
        for (const Range &r : markers)
            for (int i = std::max<int>(0, r.start); i < std::min<int>(n, r.start + r.length); ++i) fmt[i].merge(markerFormat);
        for (int i = 0; i < n;) {
            int j = i + 1;
            while (j < n && fmt[j] == fmt[i]) ++j;
            setFormat(i, j - i, fmt[i]);
            i = j;
        }
    };
    QTextCharFormat mono;
    mono.setFontFamilies({QStringLiteral("monospace")});
    mono.setFontFixedPitch(true);
    mono.merge(fg(m_code));

    // Fences: the opening and closing lines are markers, the lines between are
    // code. The state carries the open fence across blocks.
    if (fenceRe().match(text).hasMatch()) {
        setCurrentBlockState(wasFenced ? Normal : InFence);
        apply(0, n, mono);
        markers.append({0, n});
        flush();
        return;
    }
    if (wasFenced) {
        setCurrentBlockState(InFence);
        apply(0, n, mono);
        flush();
        return;
    }
    setCurrentBlockState(Normal);

    // Characters that other passes must not see (block markers, code spans,
    // link text) are blanked in this copy, which keeps every offset aligned.
    QString scan = text;
    auto blank = [&](int start, int length, QChar with) {
        for (int i = start; i < start + length && i < n; ++i) scan[i] = with;
    };

    // Block-level structure.
    static const QRegularExpression headingRe(R"(^( {0,3})(#{1,6})(\s+|$))");
    static const QRegularExpression quoteRe(R"(^(\s*)(>+)\s?)");
    static const QRegularExpression listRe(R"(^(\s*)([-*+]|\d+[.)])(\s+)(\[[ xX]\](?=\s|$))?)");
    static const QRegularExpression ruleRe(R"(^\s*(\*{3,}|-{3,}|_{3,})\s*$)");

    if (ruleRe.match(text).hasMatch()) {
        apply(0, n, fg(m_muted));
        flush();
        return;
    }
    if (auto m = headingRe.match(text); m.hasMatch()) {
        const int level = m.captured(2).size();
        // Qt has no relative font size, so scale the document's default.
        static const qreal scales[] = {1.0, 1.5, 1.3, 1.15, 1.05, 1.0, 1.0};
        qreal pt = document() ? document()->defaultFont().pointSizeF() : -1;
        if (pt <= 0 && document() && document()->defaultFont().pixelSize() > 0) pt = document()->defaultFont().pixelSize() * 0.75;
        QTextCharFormat h = fg(m_heading.isValid() ? m_heading : m_text);
        h.setFontWeight(QFont::Bold);
        if (pt > 0) h.setFontPointSize(pt * scales[level]);
        apply(0, n, h);
        markers.append({m.capturedStart(2), m.capturedLength(2)});
        blank(0, m.capturedEnd(), u' ');
    } else if (auto m = quoteRe.match(text); m.hasMatch()) {
        apply(0, n, fg(m_muted));
        QTextCharFormat italic;
        italic.setFontItalic(true);
        apply(0, n, italic);
        blank(0, m.capturedEnd(), u' ');
    } else if (auto m = listRe.match(text); m.hasMatch()) {
        QTextCharFormat marker = fg(m_accent);
        marker.setFontWeight(QFont::Bold);
        apply(m.capturedStart(2), m.capturedLength(2), marker);
        if (m.capturedStart(4) >= 0) apply(m.capturedStart(4), m.capturedLength(4), marker);
        blank(0, m.capturedEnd(), u' ');
    }

    // Inline code first, as nothing inside it is Markdown. An unclosed
    // backtick matches nothing and stays literal.
    static const QRegularExpression codeRe(R"((`+)(?!`)(.+?)(?<!`)\1(?!`))");
    QList<Range> codeSpans;
    for (auto it = codeRe.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        apply(m.capturedStart(), m.capturedLength(), mono);
        markers.append({m.capturedStart(), m.capturedLength(1)});
        markers.append({m.capturedEnd() - m.capturedLength(1), m.capturedLength(1)});
        codeSpans.append({m.capturedStart(), m.capturedLength()});
        blank(m.capturedStart(), m.capturedLength(), u'x');
    }

    // Links, skipping any that sit inside a code span.
    QTextCharFormat linkFormat = fg(m_link);
    linkFormat.setFontUnderline(true);
    for (const Markdown::Link &l : Markdown::links(text)) {
        bool inCode = false;
        for (const Range &c : codeSpans) inCode |= l.start < c.start + c.length && l.start + l.length > c.start;
        if (inCode) continue;
        if (l.text != text.mid(l.start, l.length) && text[l.start] == u'[') {
            // [label](target): the label is the link, the rest is syntax.
            apply(l.start + 1, l.text.size(), linkFormat);
            markers.append({l.start, 1});
            markers.append({l.start + 1 + l.text.size(), l.length - 1 - l.text.size()});
            blank(l.start, l.length, u'x');
        } else if (l.kind == QLatin1String("task") && m_unresolved.contains(l.target)) {
            // A link to nothing: dimmed and struck through so it reads as dead.
            QTextCharFormat dead = fg(m_muted);
            dead.setFontStrikeOut(true);
            apply(l.start, l.length, dead);
            blank(l.start, l.length, u'x');
        } else {
            apply(l.start, l.length, linkFormat);
            blank(l.start, l.length, u'x');
        }
    }

    // Emphasis. Underscores need non-word neighbours so snake_case is left
    // alone; a backslash keeps a marker literal.
    struct Rule {
        QRegularExpression re;
        int marker;  // length of the delimiter on each side
        QTextCharFormat format;
    };
    static const auto rules = [] {
        QTextCharFormat bold, italic, boldItalic, strike, under, mark;
        bold.setFontWeight(QFont::Bold);
        italic.setFontItalic(true);
        boldItalic.setFontWeight(QFont::Bold);
        boldItalic.setFontItalic(true);
        strike.setFontStrikeOut(true);
        under.setFontUnderline(true);
        auto re = [](const char *p) { return QRegularExpression(QString::fromLatin1(p), kUni); };
        return QList<Rule>{
            {re(R"((?<![\\])\*\*\*(?=[^\s*])(.+?)(?<=[^\s*\\])\*\*\*)"), 3, boldItalic},
            {re(R"((?<![\w\\])___(?=[^\s_])(.+?)(?<=[^\s_\\])___(?!\w))"), 3, boldItalic},
            {re(R"((?<![\\*])\*\*(?=[^\s*])(.+?)(?<=[^\s*\\])\*\*(?!\*))"), 2, bold},
            {re(R"((?<![\w\\_])__(?=[^\s_])(.+?)(?<=[^\s_\\])__(?![\w_]))"), 2, bold},
            {re(R"((?<![\\*])\*(?![\s*])(.+?)(?<![\s*\\])\*(?!\*))"), 1, italic},
            {re(R"((?<![\w\\_])_(?![\s_])(.+?)(?<![\s_\\])_(?![\w_]))"), 1, italic},
            {re(R"((?<![\\~])~~(?=\S)(.+?)(?<=\S)~~(?!~))"), 2, strike},
            {re(R"((?<![\\:]):{2}(?=[^\s:])(.+?)(?<=[^\s:]):{2}(?!:))"), 2, mark},
            {re(R"(<u>(.+?)</u>)"), 3, under},
            {re(R"(\[u\](.+?)\[/u\])"), 3, under},
        };
    }();
    QTextCharFormat highlight;
    if (m_accent.isValid()) {
        QColor bg = m_accent;
        bg.setAlphaF(0.3);
        highlight.setBackground(bg);
    }
    for (int r = 0; r < rules.size(); ++r) {
        const Rule &rule = rules[r];
        for (auto it = rule.re.globalMatch(scan); it.hasNext();) {
            const auto m = it.next();
            // The underline tags differ in length: "<u>" and "[u]" open, "</u>" and "[/u]" close.
            const bool tag = r >= 8;
            const int open = rule.marker;
            const int close = tag ? 4 : rule.marker;
            apply(m.capturedStart(), m.capturedLength(), r == 7 ? highlight : rule.format);
            markers.append({m.capturedStart(), open});
            markers.append({m.capturedEnd() - close, close});
        }
    }
    flush();
}

// ---------------------------------------------------------------------------

NotesHighlighter::NotesHighlighter(QObject *parent) : QObject(parent), m_highlighter(new MarkdownHighlighter(this)) {
    connect(this, &NotesHighlighter::colorsChanged, this, [this] {
        m_highlighter->setColors(m_text, m_muted, m_accent, m_code, m_link, m_heading);
    });
}

void NotesHighlighter::registerType() {
    qmlRegisterType<NotesHighlighter>("Fairfax", 1, 0, "NotesHighlighter");
}

void NotesHighlighter::setDocument(QQuickTextDocument *document) {
    if (m_document == document) return;
    m_document = document;
    m_highlighter->setDocument(document ? document->textDocument() : nullptr);
    emit documentChanged();
}

void NotesHighlighter::set(QColor &field, const QColor &value) {
    if (field == value) return;
    field = value;
    emit colorsChanged();
}

void NotesHighlighter::setUnresolved(const QStringList &titles) {
    if (titles == m_unresolved) return;
    m_unresolved = titles;
    m_highlighter->setUnresolved(titles);
    emit unresolvedChanged();
}

QVariantMap NotesHighlighter::listContinuation(const QString &text, int cursor) const {
    const Markdown::ListEdit e = Markdown::listContinuation(text, cursor);
    const char *action = e.action == Markdown::ListEdit::Continue ? "continue" : e.action == Markdown::ListEdit::End ? "end" : "none";
    return {{"action", QString::fromLatin1(action)}, {"insert", e.insert}, {"from", e.from}, {"to", e.to}};
}

QVariantList NotesHighlighter::stampFormats() const {
    QVariantList out;
    const QDateTime now = QDateTime::currentDateTime();
    const QStringList names = Markdown::stampNames();
    for (int i = 0; i < names.size(); ++i) out.append(QVariantMap{{"name", names[i]}, {"example", Markdown::stamp(i, now)}});
    return out;
}

QString NotesHighlighter::stamp(int format) const { return Markdown::stamp(format, QDateTime::currentDateTime()); }
QString NotesHighlighter::linksAsText(const QString &text) const { return Markdown::linksAsText(text); }
QString NotesHighlighter::clamp(const QString &text) const { return Markdown::clampNotes(text); }
QString NotesHighlighter::previewSource(const QString &text) const { return Markdown::previewSource(text); }

QVariantList NotesHighlighter::links(const QString &text) const {
    QVariantList out;
    for (const Markdown::Link &l : Markdown::links(text)) out.append(toMap(l));
    return out;
}

QVariantMap NotesHighlighter::linkAt(const QString &text, int position) const {
    const Markdown::Link l = Markdown::linkAt(text, position);
    return l.length > 0 ? toMap(l) : QVariantMap{};
}
