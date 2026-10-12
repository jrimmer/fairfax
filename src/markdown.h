#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
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

}  // namespace Markdown

// Styles Markdown source as it is typed. The markers stay in the text (the
// saved note is always the source), drawn muted; only the formatting changes.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MarkdownHighlighter(QObject *parent = nullptr);

    void setColors(const QColor &text, const QColor &muted, const QColor &accent, const QColor &code,
                   const QColor &link, const QColor &heading);

protected:
    void highlightBlock(const QString &text) override;

private:
    QColor m_text, m_muted, m_accent, m_code, m_link, m_heading;
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
public:
    explicit NotesHighlighter(QObject *parent = nullptr);

    // Registers the type as Precrime 1.0 NotesHighlighter; call before loading QML.
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

    // [{text, target, kind, start, length}] for every link in `text`.
    Q_INVOKABLE QVariantList links(const QString &text) const;
    // The link at a cursor position as {text, target, kind, start, length};
    // an empty map when there is none.
    Q_INVOKABLE QVariantMap linkAt(const QString &text, int position) const;

signals:
    void documentChanged();
    void colorsChanged();

private:
    void set(QColor &field, const QColor &value);

    QQuickTextDocument *m_document = nullptr;
    MarkdownHighlighter *m_highlighter;
    QColor m_text, m_muted, m_accent, m_code, m_link, m_heading;
};
