#pragma once

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QVariantList>
#include <QVariantMap>

#include <QQuickTextDocument>

namespace Markdown {

// A link found in notes. `start` and `length` are UTF-16 offsets into the notes
// text, matching QML's TextEdit positions. `kind` is "url", "email", "file" or
// "task"; `target` is what to open (the task title for task links).
struct Link {
    QString text;
    QString target;
    QString kind;
    int start = 0;
    int length = 0;
};

// Every link in the notes, in text order. Fenced code blocks are skipped, and a
// range is never reported twice (a URL inside `[text](url)` is part of that link).
QList<Link> links(const QString &notes);

// The link covering a cursor position, or an empty Link (length 0) if none.
// The end of a link counts as inside it, as a click on a link's last
// character resolves to the position just after it.
Link linkAt(const QString &notes, int position);

// What Return does in a notes editor with the caret at `cursor`.
struct ListEdit {
    enum Action { None, Continue, End };
    Action action = None;
    QString insert;  // Continue: text to insert at the caret (newline plus the next marker).
    int from = 0;    // End: the range holding the empty marker, to be removed.
    int to = 0;
};

// Return at the end of a list line (-, *, +, 1. or 1), optionally followed by a
// "[ ]" checkbox) starts the next item with the same marker; numbers count up.
// Return on an item with no text ends the list instead. Lines inside fenced
// code, and a caret that is not at the end of its line, leave Return alone.
ListEdit listContinuation(const QString &notes, int cursor);

// The date and time stamps of Insert Date & Time, in the manual's order.
QStringList stampNames();
QString stamp(int format, const QDateTime &moment);

// The "Copy Links from Notes" text: one link per line. Task links come back as
// their @{Title} token, mail links as the bare address, everything else as the target.
QString linksAsText(const QString &notes);

// Notes are capped at 500 KB of UTF-8. Returns `notes` cut to fit (never in the
// middle of a character), and sets `*truncated` when something was cut.
constexpr int kNotesLimit = 500 * 1024;
QString clampNotes(const QString &notes, bool *truncated = nullptr);

// Notes ready for a Markdown renderer that only knows CommonMark: ::highlight::
// and underline tags become inline HTML, and @{Title} links become
// fairfax-task: links the viewer can resolve.
QString previewSource(const QString &notes);

}  // namespace Markdown

// Styles Markdown source as it is typed. The markers stay in the text (the
// saved note is always the source), drawn muted; only the formatting changes.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MarkdownHighlighter(QObject *parent = nullptr);

    void setColors(const QColor &text, const QColor &muted, const QColor &accent, const QColor &code,
                   const QColor &link, const QColor &heading);
    // Task links whose title is listed here match no task and are drawn dimmed and struck through.
    void setUnresolved(const QStringList &titles);

protected:
    void highlightBlock(const QString &text) override;

private:
    QColor m_text, m_muted, m_accent, m_code, m_link, m_heading;
    QStringList m_unresolved;
};

// The QML face of the highlighter: attach it to a TextEdit's textDocument and
// set the theme colours. Also exposes link detection to QML.
class NotesHighlighter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QColor text READ text WRITE setText NOTIFY colorsChanged)
    Q_PROPERTY(QColor muted READ muted WRITE setMuted NOTIFY colorsChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY colorsChanged)
    Q_PROPERTY(QColor code READ code WRITE setCode NOTIFY colorsChanged)
    Q_PROPERTY(QColor link READ link WRITE setLink NOTIFY colorsChanged)
    Q_PROPERTY(QColor heading READ heading WRITE setHeading NOTIFY colorsChanged)
    // Titles of @{task links} that resolve to no task.
    Q_PROPERTY(QStringList unresolved READ unresolved WRITE setUnresolved NOTIFY unresolvedChanged)
public:
    explicit NotesHighlighter(QObject *parent = nullptr);

    // Registers the type as Fairfax 1.0 NotesHighlighter; call before loading QML.
    static void registerType();

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);

    QColor text() const { return m_text; }
    QColor muted() const { return m_muted; }
    QColor accent() const { return m_accent; }
    QColor code() const { return m_code; }
    QColor link() const { return m_link; }
    QColor heading() const { return m_heading; }
    void setText(const QColor &c) { set(m_text, c); }
    void setMuted(const QColor &c) { set(m_muted, c); }
    void setAccent(const QColor &c) { set(m_accent, c); }
    void setCode(const QColor &c) { set(m_code, c); }
    void setLink(const QColor &c) { set(m_link, c); }
    void setHeading(const QColor &c) { set(m_heading, c); }

    QStringList unresolved() const { return m_unresolved; }
    void setUnresolved(const QStringList &titles);

    // What Return should do at `cursor`: {action: "none"|"continue"|"end", insert, from, to}.
    Q_INVOKABLE QVariantMap listContinuation(const QString &text, int cursor) const;
    // Insert Date & Time: each format's name and an example of it, and a stamp for now.
    Q_INVOKABLE QVariantList stampFormats() const;
    Q_INVOKABLE QString stamp(int format) const;
    Q_INVOKABLE QString linksAsText(const QString &text) const;
    // The text cut to the 500 KB notes limit; unchanged when it already fits.
    Q_INVOKABLE QString clamp(const QString &text) const;
    Q_INVOKABLE QString previewSource(const QString &text) const;

    // [{text, target, kind, start, length}] for every link in `text`.
    Q_INVOKABLE QVariantList links(const QString &text) const;
    // The link at a cursor position as {text, target, kind, start, length};
    // an empty map when there is none.
    Q_INVOKABLE QVariantMap linkAt(const QString &text, int position) const;

signals:
    void documentChanged();
    void colorsChanged();
    void unresolvedChanged();

private:
    void set(QColor &field, const QColor &value);

    QQuickTextDocument *m_document = nullptr;
    MarkdownHighlighter *m_highlighter;
    QColor m_text, m_muted, m_accent, m_code, m_link, m_heading;
    QStringList m_unresolved;
};
