#include <QtTest>
#include <QTextDocument>
#include <QTextLayout>

#include "markdown.h"
#include "testregistry.h"

namespace {

// The format covering the character at `pos` of the document's first block.
QTextCharFormat formatAt(const QTextBlock &block, int pos) {
    for (const auto &r : block.layout()->formats())
        if (pos >= r.start && pos < r.start + r.length) return r.format;
    return {};
}

QTextBlock blockN(const QTextDocument &doc, int n) {
    QTextBlock b = doc.firstBlock();
    while (n-- > 0) b = b.next();
    return b;
}

}  // namespace

class MarkdownTest : public QObject {
    Q_OBJECT

    const QColor text{"#101010"}, muted{"#202020"}, accent{"#303030"}, code{"#404040"}, link{"#505050"},
        heading{"#606060"};

    // Highlights `source` in a fresh document and returns it; the highlighter lives as long as the document.
    std::unique_ptr<QTextDocument> highlight(const QString &source) {
        auto doc = std::make_unique<QTextDocument>();
        auto *h = new MarkdownHighlighter(doc.get());
        h->setColors(text, muted, accent, code, link, heading);
        h->setDocument(doc.get());
        doc->setPlainText(source);
        h->rehighlight();
        return doc;
    }

private slots:
    void headingIsBoldAndLarger() {
        auto doc = highlight("# Title\nplain");
        doc->setDefaultFont(QFont("sans", 10));
        auto *h = doc->findChild<MarkdownHighlighter *>();
        h->rehighlight();
        const auto f = formatAt(doc->firstBlock(), 4);
        QCOMPARE(f.fontWeight(), int(QFont::Bold));
        QCOMPARE(f.foreground().color(), heading);
        QVERIFY(f.fontPointSize() > 10);
        QCOMPARE(formatAt(doc->firstBlock(), 0).foreground().color(), muted);  // the # marker
        QVERIFY(formatAt(blockN(*doc, 1), 1).fontWeight() != QFont::Bold);
    }

    void boldItalicAndMarkers() {
        auto doc = highlight("a **bold** and *it* and __b2__ and _i2_");
        const auto b = doc->firstBlock();
        QCOMPARE(formatAt(b, 5).fontWeight(), int(QFont::Bold));
        QCOMPARE(formatAt(b, 2).foreground().color(), muted);
        QVERIFY(formatAt(b, 0).fontWeight() != QFont::Bold);
        QVERIFY(formatAt(b, 15).fontItalic());
        QVERIFY(formatAt(b, 24).fontWeight() == QFont::Bold);
        QVERIFY(formatAt(b, 36).fontItalic());
    }

    void nestedEmphasis() {
        auto doc = highlight("**bold *both* bold** and ***all***");
        const auto b = doc->firstBlock();
        QVERIFY(formatAt(b, 3).fontWeight() == QFont::Bold && !formatAt(b, 3).fontItalic());
        QVERIFY(formatAt(b, 9).fontWeight() == QFont::Bold && formatAt(b, 9).fontItalic());
        QVERIFY(formatAt(b, 29).fontWeight() == QFont::Bold && formatAt(b, 29).fontItalic());
    }

    void snakeCaseIsNotItalic() {
        auto doc = highlight("use snake_case_name here and _real_");
        const auto b = doc->firstBlock();
        QVERIFY(!formatAt(b, 10).fontItalic());
        QVERIFY(!formatAt(b, 16).fontItalic());
        QVERIFY(formatAt(b, 30).fontItalic());
    }

    void unclosedMarkersStayPlain() {
        auto doc = highlight("**not closed and `no code and *star");
        const auto b = doc->firstBlock();
        for (int i : {3, 20, 33}) {
            QVERIFY(formatAt(b, i).fontWeight() != QFont::Bold);
            QVERIFY(!formatAt(b, i).fontItalic());
            QVERIFY(formatAt(b, i).fontFamilies().isNull() || formatAt(b, i).fontFamilies().toStringList().isEmpty());
        }
    }

    void escapedMarkerIsLiteral() {
        auto doc = highlight("\\*nope\\* and *yes*");
        const auto b = doc->firstBlock();
        QVERIFY(!formatAt(b, 3).fontItalic());
        QVERIFY(formatAt(b, 16).fontItalic());
    }

    void inlineCode() {
        auto doc = highlight("run `a *b* c` now");
        const auto b = doc->firstBlock();
        const auto f = formatAt(b, 8);
        QCOMPARE(f.foreground().color(), code);
        QVERIFY(f.fontFixedPitch());
        QVERIFY(!f.fontItalic());  // emphasis inside code is literal
        QCOMPARE(formatAt(b, 4).foreground().color(), muted);
        QCOMPARE(formatAt(b, 0).foreground().color(), text);
    }

    void fencedCodeSpansBlocks() {
        auto doc = highlight("before\n```\n# not a heading\n**no**\n```\nafter **b**");
        QCOMPARE(blockN(*doc, 1).userState(), 1);
        QCOMPARE(blockN(*doc, 2).userState(), 1);
        QCOMPARE(blockN(*doc, 3).userState(), 1);
        QCOMPARE(blockN(*doc, 4).userState(), 0);
        QCOMPARE(blockN(*doc, 5).userState(), 0);
        const auto inner = formatAt(blockN(*doc, 2), 3);
        QCOMPARE(inner.foreground().color(), code);
        QVERIFY(inner.fontWeight() != QFont::Bold);
        QVERIFY(formatAt(blockN(*doc, 3), 3).fontWeight() != QFont::Bold);
        QCOMPARE(formatAt(blockN(*doc, 5), 9).fontWeight(), int(QFont::Bold));
    }

    void unclosedFenceRunsToEnd() {
        auto doc = highlight("```\ncode\nmore");
        QCOMPARE(blockN(*doc, 2).userState(), 1);
    }

    void quoteAndLists() {
        auto doc = highlight("> quoted\n- item\n3. num\n- [x] done");
        QCOMPARE(formatAt(blockN(*doc, 0), 4).foreground().color(), muted);
        QCOMPARE(formatAt(blockN(*doc, 1), 0).foreground().color(), accent);
        QCOMPARE(formatAt(blockN(*doc, 1), 3).foreground().color(), text);
        QCOMPARE(formatAt(blockN(*doc, 2), 0).foreground().color(), accent);
        QCOMPARE(formatAt(blockN(*doc, 3), 3).foreground().color(), accent);  // the [x] box
    }

    void linksAreUnderlined() {
        auto doc = highlight("see https://a.com/x_y_z and @{Other Task} and [lbl](http://b.org)");
        const auto b = doc->firstBlock();
        auto url = formatAt(b, 10);
        QCOMPARE(url.foreground().color(), link);
        QVERIFY(url.fontUnderline());
        QVERIFY(!url.fontItalic());  // underscores inside a URL are not emphasis
        auto task = formatAt(b, 36);
        QCOMPARE(task.foreground().color(), link);
        QVERIFY(task.fontUnderline());
        QVERIFY(formatAt(b, 47).fontUnderline());   // the label
        QVERIFY(!formatAt(b, 53).fontUnderline());  // the (url) syntax
        QCOMPARE(formatAt(b, 53).foreground().color(), muted);
        QVERIFY(!formatAt(b, 0).fontUnderline());
    }

    void linkInsideCodeIsPlain() {
        auto doc = highlight("`http://x.com`");
        QVERIFY(!formatAt(doc->firstBlock(), 5).fontUnderline());
    }

    void linksAllKinds() {
        const QString t = "web http://a.com/p?q=1, www.b.org. mail me@x.io\n"
                          "~/docs/a b.txt\n"
                          "task @{Buy milk} and [site](https://c.net) done";
        const auto ls = Markdown::links(t);
        QCOMPARE(ls.size(), 6);
        QCOMPARE(ls[0].kind, QString("url"));
        QCOMPARE(ls[0].target, QString("http://a.com/p?q=1"));  // trailing comma is not part of it
        QCOMPARE(ls[1].kind, QString("url"));
        QCOMPARE(ls[1].text, QString("www.b.org"));
        QCOMPARE(ls[1].target, QString("https://www.b.org"));
        QCOMPARE(ls[2].kind, QString("email"));
        QCOMPARE(ls[2].target, QString("mailto:me@x.io"));
        QCOMPARE(ls[3].kind, QString("file"));
        QCOMPARE(ls[3].target, QString("~/docs/a b.txt"));
        QCOMPARE(ls[4].kind, QString("task"));
        QCOMPARE(ls[4].target, QString("Buy milk"));
        QCOMPARE(t.mid(ls[4].start, ls[4].length), QString("@{Buy milk}"));
        QCOMPARE(ls[5].text, QString("site"));
        QCOMPARE(ls[5].target, QString("https://c.net"));
        QCOMPARE(t.mid(ls[5].start, ls[5].length), QString("[site](https://c.net)"));
    }

    void filePathsMustBeWholeLine() {
        QCOMPARE(Markdown::links("look in /etc/hosts please").size(), 0);
        const auto ls = Markdown::links("  /var/log/syslog  ");
        QCOMPARE(ls.size(), 1);
        QCOMPARE(ls[0].target, QString("/var/log/syslog"));
        QCOMPARE(ls[0].start, 2);
        QCOMPARE(Markdown::links("// comment").size(), 0);
    }

    void noOverlapsAndFencesSkipped() {
        QCOMPARE(Markdown::links("[x](http://a.com)").size(), 1);
        QCOMPARE(Markdown::links("@{mail a@b.com}").size(), 1);
        QCOMPARE(Markdown::links("```\nhttp://a.com\n```\nhttp://b.com").size(), 1);
        QCOMPARE(Markdown::links("(see http://a.com/x_(y))").first().target, QString("http://a.com/x_(y)"));
        QCOMPARE(Markdown::links("(http://a.com)").first().target, QString("http://a.com"));
        QCOMPARE(Markdown::links("@{unclosed and plain").size(), 0);
        QCOMPARE(Markdown::links("").size(), 0);
    }

    void linkAtPositions() {
        const QString t = "go http://a.com now";
        QCOMPARE(Markdown::linkAt(t, 0).length, 0);
        QCOMPARE(Markdown::linkAt(t, 3).kind, QString("url"));
        QCOMPARE(Markdown::linkAt(t, 10).kind, QString("url"));
        QCOMPARE(Markdown::linkAt(t, 15).kind, QString("url"));  // just past the last character
        QCOMPARE(Markdown::linkAt(t, 16).length, 0);
        const QString multi = "a\n@{T}";
        QCOMPARE(Markdown::linkAt(multi, 3).target, QString("T"));
        QCOMPARE(Markdown::linkAt(multi, 3).start, 2);
    }

    void qmlWrapper() {
        NotesHighlighter n;
        const auto l = n.links("x @{A}");
        QCOMPARE(l.size(), 1);
        QCOMPARE(l[0].toMap()["kind"].toString(), QString("task"));
        QVERIFY(n.linkAt("x @{A}", 0).isEmpty());
        QCOMPARE(n.linkAt("x @{A}", 3)["target"].toString(), QString("A"));
    }
};

REGISTER_TEST(MarkdownTest)
#include "test_markdown.moc"
