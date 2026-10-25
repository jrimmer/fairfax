#include <QCryptographicHash>
#include <QtTest>
#include <memory>

#include "mailtask.h"
#include "query.h"
#include "store.h"
#include "testregistry.h"

// Mail to Fairfax's converter. The comments describe the rule each test covers.

#ifndef MAIL_FIXTURES
#error "MAIL_FIXTURES must name tests/fixtures/mail"
#endif

namespace {

const QDateTime Now(QDate(2026, 10, 13), QTime(10, 0));  // A Tuesday.

QByteArray fixture(const QString &name) {
    QFile f(QStringLiteral(MAIL_FIXTURES "/") + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A one-part message with a fresh Message-ID.
QByteArray mail(const QString &subject, const QString &body, const QString &extraHeaders = {}) {
    static int counter = 0;
    return QStringLiteral("Message-ID: <t%1@test.example>\nDate: Tue, 13 Oct 2026 09:30:00 +0200\nFrom: Sam <sam@example.com>\n"
                          "Subject: %2\n%3Content-Type: text/plain; charset=utf-8\n\n%4")
        .arg(++counter).arg(subject, extraHeaders, body).toUtf8();
}

QString sha(const QString &text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

}  // namespace

class MailTaskTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<Store> m_store;
    qint64 m_inbox = 0, m_work = 0, m_errands = 0, m_home = 0;

    qint64 addList(const QString &name, int kind = ListKind::Normal, bool archived = false, bool deleted = false) {
        TaskList l;
        l.id = m_store->nextId("lists");
        l.name = name;
        l.kind = kind;
        l.archived = archived;
        l.deleted = deleted;
        l.order = l.id;
        m_store->begin("setup");
        m_store->putList(l);
        m_store->commit();
        return l.id;
    }

    MailTask::Options options() const {
        MailTask::Options o;
        o.now = Now;
        return o;
    }

    MailTask::Result go(const QByteArray &raw, MailTask::Options o = {}) {
        if (!o.now.isValid())
            o.now = Now;
        const MailTask::Message message = MailTask::parse(raw);
        return MailTask::import(*m_store, message, o);
    }

    // Imports a message and returns the new task.
    Task made(const QString &subject, const QString &body = QStringLiteral("body"), MailTask::Options o = {}) {
        const MailTask::Result r = go(mail(subject, body), o);
        if (!r.ok())
            qWarning() << "import failed:" << r.reason;
        return m_store->task(r.taskId);
    }

    QList<Task> childrenOf(qint64 id) const {
        QList<Task> kids;
        for (const Task &t : m_store->tasks())
            if (t.parentId == id)
                kids << t;
        std::sort(kids.begin(), kids.end(), [](const Task &a, const Task &b) { return a.order < b.order; });
        return kids;
    }

    int liveTasks() const {
        int n = 0;
        for (const Task &t : m_store->tasks())
            n += !t.deleted && !t.parentId;
        return n;
    }

    QStringList tagNames(const Task &t) const {
        QStringList names;
        for (qint64 id : t.tagIds)
            names << m_store->tag(id).name;
        names.sort();
        return names;
    }

private slots:
    void init() {
        m_store = std::make_unique<Store>();
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath(QStringLiteral("m%1.db").arg(QTest::currentTestFunction()));
        QFile::remove(path);
        QVERIFY(m_store->open(path));
        m_inbox = addList("Inbox", ListKind::Inbox);
        m_work = addList("Work");
        m_errands = addList("Errands");
        m_home = addList("Home");
    }

    // ------------------------------------------------------------ parsing

    void parsePlainText() {
        const MailTask::Message m = MailTask::parse(fixture("plain.eml"));
        QVERIFY(m.valid);
        QCOMPARE(m.subject, "Buy milk");
        QCOMPARE(m.from, "Sam Carter <sam@example.com>");
        QCOMPARE(m.to, "jane+fairfax@example.com");
        QCOMPARE(m.messageId, "<plain-1@example.com>");
        QCOMPARE(m.body, "Two litres, semi-skimmed.\nThe corner shop closes at six.");
        QVERIFY(m.date.isValid());
        QCOMPARE(m.date.toUTC(), QDateTime(QDate(2026, 10, 13), QTime(7, 30), QTimeZone::UTC));
        QVERIFY(m.attachments.isEmpty());
    }

    void parseHeadersUnfoldedAndRecipients() {
        const QByteArray raw =
            "Message-ID: <h1@x>\nDate: Tue, 13 Oct 2026 09:30:00 +0200 (CEST)\nFrom: a@x.example\n"
            "To: jane+fairfax@gmail.com,\n bob@example.com\nCc: carol@example.com\n"
            "Delivered-To: jane+fairfax@gmail.com\nX-Original-To: jane+fairfax@gmail.com\n"
            "Subject: A very long subject\n that continues here\n\tand here\n\nbody\n";
        const MailTask::Message m = MailTask::parse(raw);
        QCOMPARE(m.subject, "A very long subject that continues here and here");
        QCOMPARE(m.to, "jane+fairfax@gmail.com, bob@example.com");
        QCOMPARE(m.cc, "carol@example.com");
        QCOMPARE(m.deliveredTo, "jane+fairfax@gmail.com");
        QCOMPARE(m.xOriginalTo, "jane+fairfax@gmail.com");
        QVERIFY2(m.date.isValid(), "a trailing (CEST) comment must not spoil the Date");
        QCOMPARE(m.date.toUTC().time(), QTime(7, 30));
    }

    void parseQuotedPrintableUtf8() {
        const MailTask::Message m = MailTask::parse(fixture("quoted-printable-utf8.eml"));
        QCOMPARE(m.subject, QString::fromUtf8("Café visit"));
        QVERIFY2(m.body.contains(QString::fromUtf8("Meet at the café on Rue Cler.")), qPrintable(m.body));
        QVERIFY2(m.body.contains("soft-wrapped by the mail client"), "a soft line break (=) joins the lines");
        QVERIFY(m.body.contains(QString::fromUtf8("euro sign is €5.")));
        QVERIFY(m.body.endsWith("Second line"));
    }

    void parseBase64Latin1() {
        const MailTask::Message m = MailTask::parse(fixture("base64-latin1.eml"));
        QCOMPARE(m.subject, QString::fromUtf8("Grüße"));
        QCOMPARE(m.body, QString::fromUtf8("Grüße aus München\nZweite Zeile"));
    }

    void parseAlternativePrefersPlainWhateverTheOrder() {
        // The fixture lists the HTML part first.
        const MailTask::Message m = MailTask::parse(fixture("alternative.eml"));
        QCOMPARE(m.body, "Book the table for Friday.");
    }

    void parseHtmlOnlyBecomesText() {
        const MailTask::Message m = MailTask::parse(fixture("html-only.eml"));
        QCOMPARE(m.body, "Order shipped\n\nYour parcel is on its way & due Friday.\n\n- Socks\n- Shoes (2)\n\nLine one\nLine two");
        for (const char *junk : {"color", "alert", "Ignored title", "comment", "<"})
            QVERIFY2(!m.body.contains(junk), junk);
    }

    void parseMixedKeepsTextAndListsAttachment() {
        // "Attachments and HTML formatting are not captured."
        const MailTask::Message m = MailTask::parse(fixture("mixed-attachment.eml"));
        QCOMPARE(m.body, "Please review the attached report.");
        QCOMPARE(m.attachments, QStringList{"report.pdf"});
    }

    void parseTextAttachmentIsNotTheBody() {
        // A text file attached to a message is an attachment, not part of what was written.
        const MailTask::Message m = MailTask::parse(
            "Subject: x\nContent-Type: multipart/mixed; boundary=B\n\n--B\nContent-Type: text/plain\n\nThe message\n--B\n"
            "Content-Type: text/plain; name=\"=?UTF-8?Q?n=C3=B6tes?=.txt\"\nContent-Disposition: attachment; filename=\"notes.txt\"\n\n"
            "SECRET FILE CONTENT\n--B\nContent-Type: text/html; name=\"page.html\"\n\n<p>HTML FILE</p>\n--B--\n");
        QCOMPARE(m.body, "The message");
        QCOMPARE(m.attachments, (QStringList{"notes.txt", "page.html"}));
    }

    void parseForwardedMessageContributesItsText() {
        const MailTask::Message m = MailTask::parse(fixture("forwarded.eml"));
        QCOMPARE(m.body, "Can you check this in for me?\n\nFlight BA117 departs at 18:45 from gate 12.");
        QVERIFY2(m.attachments.isEmpty(), "the forwarded message is text, not an attachment");
    }

    void parseRfc2047Headers() {
        const MailTask::Message m = MailTask::parse(fixture("rfc2047-subject.eml"));
        // Encoded words separated only by folding whitespace join up; "_" in a Q word is a space.
        QCOMPARE(m.subject, QString::fromUtf8("Renew passport –café €20"));
        QCOMPARE(m.from, QString::fromUtf8("Jürgen Müller <jm@example.com>"));
    }

    void parseCrlfAndMboxFromLine() {
        const MailTask::Message m = MailTask::parse(fixture("crlf.eml"));
        QCOMPARE(m.subject, "CRLF message");
        QCOMPARE(m.body, "First line\nSecond line");
        QVERIFY(!m.body.contains('\r'));
        const MailTask::Message mbox = MailTask::parse("From sam@example.com Tue Oct 13 09:30:00 2026\n" + fixture("plain.eml"));
        QVERIFY(mbox.valid);
        QCOMPARE(mbox.subject, "Buy milk");
    }

    void parseNestedMultipartAndCharsetFallbacks() {
        const QByteArray nested =
            "Subject: nested\nContent-Type: multipart/mixed; boundary=\"outer\"\n\n--outer\n"
            "Content-Type: multipart/related; boundary=\"inner\"\n\n--inner\n"
            "Content-Type: multipart/alternative; boundary=\"alt\"\n\n--alt\nContent-Type: text/plain\n\nDeep plain text\n--alt\n"
            "Content-Type: text/html\n\n<p>Deep html</p>\n--alt--\n--inner\nContent-Type: image/png\nContent-Disposition: inline\n"
            "Content-ID: <logo>\n\nPNGDATA\n--inner--\n--outer\nContent-Type: text/plain; charset=utf-8\n\nAfter\n--outer--\n";
        const MailTask::Message m = MailTask::parse(nested);
        QCOMPARE(m.body, "Deep plain text\n\nAfter");
        QCOMPARE(m.attachments, QStringList{"image/png"});

        // A declared charset is honoured: 0xA4 is the euro sign in ISO-8859-15 (Latin-1 would read it as a currency sign).
        QCOMPARE(MailTask::parse("Subject: x\nContent-Type: text/plain; charset=iso-8859-15\n\nprice \xa4" "5").body, QString::fromUtf8("price €5"));
        QCOMPARE(MailTask::parse("Subject: x\nContent-Type: text/plain; charset=windows-1252\n\n\x93quoted\x94").body, QString::fromUtf8("“quoted”"));

        // An unknown charset falls back to UTF-8; undeclared invalid UTF-8 falls back to Latin-1.
        QCOMPARE(MailTask::parse("Subject: x\nContent-Type: text/plain; charset=x-no-such\n\nna\xc3\xafve").body, QString::fromUtf8("naïve"));
        QCOMPARE(MailTask::parse("Subject: x\n\nna\xefve").body, QString::fromUtf8("naïve"));
    }

    void parseGarbageIsInvalid() {
        QVERIFY(!MailTask::parse("").valid);
        QVERIFY(!MailTask::parse("just some text, not a message\nwith two lines").valid);
    }

    // ------------------------------------------------------------ title

    void importPlainTextMakesATask() {
        // "The subject becomes the task's title... The plain-text body becomes the task's notes."
        const MailTask::Result r = go(fixture("plain.eml"), options());
        QVERIFY(r.ok());
        const Task t = m_store->task(r.taskId);
        QCOMPARE(t.title, "Buy milk");
        QCOMPARE(t.notes, "Two litres, semi-skimmed.\nThe corner shop closes at six.");
        QCOMPARE(t.listId, m_inbox);
        QCOMPARE(t.type, int(TaskType::Task));
        QCOMPARE(t.uid, "mail-" + QString("84b6ea72aa5d7f15817967b4443af266"));
        QVERIFY(!t.dueDate.isValid());
        QCOMPARE(m_store->undoLabel(), "Mail");
    }

    void importIsOneUndoStep() {
        const Task t = made("Undo me", "1. a\n2. b");
        QCOMPARE(childrenOf(t.id).size(), 2);
        const QString label = m_store->undoLabel();
        QCOMPARE(label, "Mail");
        QVERIFY(m_store->undo());
        QVERIFY(!m_store->tasks().contains(t.id));
        QVERIFY2(childrenOf(t.id).isEmpty(), "the checklist items go with the task");
        QVERIFY(m_store->redo());
        QVERIFY(m_store->tasks().contains(t.id));
    }

    void replyAndForwardPrefixesAreTrimmed_data() {
        QTest::addColumn<QString>("subject");
        QTest::addColumn<QString>("title");
        QTest::newRow("re") << "Re: Call mum" << "Call mum";
        QTest::newRow("fwd") << "Fwd: Call mum" << "Call mum";
        QTest::newRow("fw") << "FW: Call mum" << "Call mum";
        QTest::newRow("aw") << "AW: Call mum" << "Call mum";
        QTest::newRow("wg") << "WG: Call mum" << "Call mum";
        QTest::newRow("sv") << "SV: Call mum" << "Call mum";
        QTest::newRow("vs") << "VS: Call mum" << "Call mum";
        QTest::newRow("case") << "rE: fWd: Call mum" << "Call mum";
        QTest::newRow("chain") << "Re: Re: Fwd: AW: Fw: Re:Call mum" << "Call mum";
        QTest::newRow("spaces") << "  Re :   Call mum  " << "Call mum";
        QTest::newRow("only a prefix word") << "Reply to Bob" << "Reply to Bob";
        QTest::newRow("forward word") << "Forward the keys" << "Forward the keys";
        QTest::newRow("prefix mid title") << "Call mum re: the keys" << "Call mum re: the keys";
        QTest::newRow("filing after prefixes") << "Re: Fwd: Pay rent priority(!!)" << "Pay rent";
    }
    void replyAndForwardPrefixesAreTrimmed() {
        QFETCH(QString, subject);
        QFETCH(QString, title);
        QCOMPARE(made(subject).title, title);
    }

    void emptyTitleFallsBackToTheBodyThenToNoSubject() {
        QCOMPARE(made("", "\n\nPick up the parcel\nsecond line").title, "Pick up the parcel");
        QCOMPARE(made("Re:", "Water the plants").title, "Water the plants");
        QCOMPARE(made("due(tomorrow)", "Pay rent\nbefore Friday").title, "Pay rent");
        QCOMPARE(made("due(tomorrow)", "").title, "(no subject)");
        QCOMPARE(made("   ", "x").title, "x");
        // The long first line is cut to a sensible title.
        QCOMPARE(made("", QString(500, 'w')).title.size(), 200);
    }

    void emptyMessageCreatesNothing() {
        const int before = liveTasks();
        const MailTask::Result r = go(mail("", ""));
        QVERIFY(!r.ok());
        QCOMPARE(r.reason, "empty");
        QCOMPARE(liveTasks(), before);
        QVERIFY(!m_store->canUndo() || m_store->undoLabel() != "Mail");
    }

    // ------------------------------------------------------------ filing instructions

    void manualExample() {
        // "Subject: Pick up the dry cleaning due(tomorrow) list(Errands)" ->
        // "a task titled 'Pick up the dry cleaning' in your Errands list, due tomorrow."
        const Task t = made("Pick up the dry cleaning due(tomorrow) list(Errands)");
        QCOMPARE(t.title, "Pick up the dry cleaning");
        QCOMPARE(t.listId, m_errands);
        QCOMPARE(t.dueDate, QDate(2026, 10, 14));
        QCOMPARE(t.dueTime, -1);
    }

    void startAndDue() {
        // "start(...) Sets a start date. due(...) Sets a due date, with a time when you include one."
        const Task t = made("Plan trip start(today) due(tomorrow 3pm)");
        QCOMPARE(t.title, "Plan trip");
        QCOMPARE(t.startDate, QDate(2026, 10, 13));
        QCOMPARE(t.startTime, -1);
        QCOMPARE(t.dueDate, QDate(2026, 10, 14));
        QCOMPARE(t.dueTime, 15 * 60);
        const Task typed = made("Dentist due(2026-11-05 9:30am)");
        QCOMPARE(typed.dueDate, QDate(2026, 11, 5));
        QCOMPARE(typed.dueTime, 9 * 60 + 30);
        const Task timeOnly = made("Call back due(4pm)");
        QCOMPARE(timeOnly.dueDate, QDate(2026, 10, 13));
        QCOMPARE(timeOnly.dueTime, 16 * 60);
        QCOMPARE(made("Kick-off START(tomorrow)").startDate, QDate(2026, 10, 14));
    }

    void priorityBangs() {
        // "priority(!) (!!) (!!!) Sets low, medium, or high priority."
        QCOMPARE(made("a priority(!)").priority, int(Priority::Low));
        QCOMPARE(made("b priority(!!)").priority, int(Priority::Medium));
        const Task high = made("c priority(!!!)");
        QCOMPARE(high.priority, int(Priority::High));
        QCOMPARE(high.title, "c");
        QCOMPARE(made("d priority(!!!!)").title, "d priority(!!!!)");  // Not a priority; stays as text.
        QCOMPARE(made("e").priority, int(Priority::None));
    }

    void listIsMatchedIgnoringCase() {
        // "list(...) Files the task in an existing list."
        const Task t = made("Review deck list(wORK)");
        QCOMPARE(t.listId, m_work);
        QCOMPARE(t.title, "Review deck");
    }

    void badListFallsBackToTheDestination() {
        // "If a named list doesn't exist, the task simply lands in your chosen destination list."
        MailTask::Options o;
        o.listId = m_home;
        Task t = made("Mow lawn list(Nowhere)", "x", o);
        QCOMPARE(t.listId, m_home);
        QCOMPARE(t.title, "Mow lawn");  // The instruction is still taken out.
        t = made("Mow lawn list(Nowhere)");
        QCOMPARE(t.listId, m_inbox);
        // Smart, archived and deleted lists are not places a task can go.
        addList("Urgent", ListKind::Smart);
        addList("Old", ListKind::Normal, true);
        addList("Gone", ListKind::Normal, false, true);
        for (const char *name : {"Urgent", "Old", "Gone"})
            QCOMPARE(made(QStringLiteral("x list(%1)").arg(name)).listId, m_inbox);
    }

    void destinationListOption() {
        MailTask::Options o;
        o.listId = m_work;
        QCOMPARE(made("To work", "x", o).listId, m_work);
        o.listId = 999999;  // Unknown: the Inbox.
        QCOMPARE(made("Lost", "x", o).listId, m_inbox);
        QCOMPARE(made("Default").listId, m_inbox);
    }

    void tagsAreCreatedAndReused() {
        // "tag(...) Adds one or more tags. Example: tag(home, diy)"
        Task t = made("Fix shelf tag(home, diy)");
        QCOMPARE(t.title, "Fix shelf");
        QCOMPARE(tagNames(t), (QStringList{"diy", "home"}));
        const int tagsBefore = m_store->tags().size();
        t = made("Paint fence tag(DIY) tag(#garden)");
        QCOMPARE(m_store->tags().size(), tagsBefore + 1);  // "DIY" reused ignoring case; "garden" is new.
        QCOMPARE(tagNames(t), (QStringList{"diy", "garden"}));
        QCOMPARE(t.title, "Paint fence");
    }

    void actions() {
        // "Supported action types are call, message, url, visit, and google." (email and mail also.)
        struct Case { const char *text; int type; const char *value; };
        const Case cases[] = {{"action(call, 0793273373)", ActionType::Call, "0793273373"},
                              {"action(message, +44 7700 900123)", ActionType::Message, "+44 7700 900123"},
                              {"action(url, https://example.com/a?b=1,2)", ActionType::Url, "https://example.com/a?b=1,2"},
                              {"action(visit, 10 Downing Street)", ActionType::Visit, "10 Downing Street"},
                              {"action(google, best bakery)", ActionType::Google, "best bakery"},
                              {"ACTION(Email, sam@example.com)", ActionType::Mail, "sam@example.com"},
                              {"action(mail,sam@example.com)", ActionType::Mail, "sam@example.com"}};
        for (const Case &c : cases) {
            const Task t = made(QStringLiteral("Do it %1").arg(c.text));
            QCOMPARE(t.title, "Do it");
            QCOMPARE(t.actionType, c.type);
            QCOMPARE(t.actionValue, c.value);
        }
    }

    void unknownOrUnreadableInstructionsStayInTheTitle() {
        const Task t = made("Book flat colour(red) due(tomorrow) action(teleport, 5) start(soonish) priority(urgent) tag()");
        QCOMPARE(t.title, "Book flat colour(red) action(teleport, 5) start(soonish) priority(urgent) tag()");
        QCOMPARE(t.dueDate, QDate(2026, 10, 14));  // The good one still works.
        QVERIFY(!t.startDate.isValid());
        QCOMPARE(t.actionType, int(ActionType::None));
        QCOMPARE(t.priority, int(Priority::None));
        // The keyword must be a word of its own.
        QCOMPARE(made("overdue(soon) mylist(x)").title, "overdue(soon) mylist(x)");
    }

    void everyInstructionTogether() {
        const Task t = made("Re: Fwd: Ship order start(today) due(tomorrow 9am) priority(!!!) list(work) tag(a, b) action(url, example.com)");
        QCOMPARE(t.title, "Ship order");
        QCOMPARE(t.startDate, QDate(2026, 10, 13));
        QCOMPARE(t.dueDate, QDate(2026, 10, 14));
        QCOMPARE(t.dueTime, 9 * 60);
        QCOMPARE(t.priority, int(Priority::High));
        QCOMPARE(t.listId, m_work);
        QCOMPARE(tagNames(t), (QStringList{"a", "b"}));
        QCOMPARE(t.actionType, int(ActionType::Url));
    }

    // ------------------------------------------------------------ checklists

    void checklistOnlyNumberedLines() {
        // "a body that is only numbered items becomes just the checklist, with no notes."
        const Task t = made("Saturday shopping list(Home)", "\n1. Milk\n\n2. Coffee beans\n\n3. Bird seed\n");
        QCOMPARE(t.type, int(TaskType::Checklist));
        QCOMPARE(t.listId, m_home);
        QCOMPARE(t.title, "Saturday shopping");
        QCOMPARE(t.notes, "");
        const QList<Task> items = childrenOf(t.id);
        QCOMPARE(items.size(), 3);
        QCOMPARE(items[0].title, "Milk");
        QCOMPARE(items[1].title, "Coffee beans");
        QCOMPARE(items[2].title, "Bird seed");
        for (const Task &i : items) {
            QCOMPARE(i.listId, m_home);
            QCOMPARE(i.type, int(TaskType::Task));
        }
    }

    void textBeforeTheFirstNumberKeepsANormalTask() {
        // "Start the body with 1.: text before the first numbered line keeps the message a normal task."
        const QString body = "Things I need:\n1. Milk\n2. Eggs";
        const Task t = made("Shopping", body);
        QCOMPARE(t.type, int(TaskType::Task));
        QCOMPARE(t.notes, body);
        QVERIFY(childrenOf(t.id).isEmpty());
        // Starting at 2 is not a checklist either.
        QCOMPARE(made("Late start", "2. Milk\n3. Eggs").type, int(TaskType::Task));
    }

    void mixedBodyKeepsTheFullTextInTheNotes() {
        // "When the body mixes numbered lines with other text, the full message text is kept in the checklist's notes."
        const QString body = "1. Milk\n2. Eggs\nThanks, Sam";
        const Task t = made("Shopping", body);
        QCOMPARE(t.type, int(TaskType::Checklist));
        QCOMPARE(t.notes, body);
        const QList<Task> items = childrenOf(t.id);
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[1].title, "Eggs");
    }

    void signatureEndsTheChecklist_data() {
        QTest::addColumn<QString>("signature");
        QTest::newRow("dash dash") << "-- \nSam Carter\n3. Not an item";
        QTest::newRow("sent from my") << "Sent from my iPhone\n3. Not an item";
        QTest::newRow("sent from my lower") << "sent from my Galaxy\n3. Not an item";
        QTest::newRow("get outlook") << "Get Outlook for Android\n3. Not an item";
    }
    void signatureEndsTheChecklist() {
        // "A signature ('Sent from my iPhone' and the like) ends the checklist rather than becoming
        // an item, and stays in the notes with the rest of the message."
        QFETCH(QString, signature);
        const QString body = "1. Milk\n2. Eggs\n\n" + signature;
        const Task t = made("Shopping", body);
        QCOMPARE(t.type, int(TaskType::Checklist));
        const QList<Task> items = childrenOf(t.id);
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].title, "Milk");
        QCOMPARE(items[1].title, "Eggs");
        QCOMPARE(t.notes, body.trimmed());
    }

    void numberedLinesNeedTheirOwnDot() {
        QCOMPARE(made("Amounts", "1.5 litres of milk\n2.5 of eggs").type, int(TaskType::Task));
        QCOMPARE(made("No items", "1.\n2.").type, int(TaskType::Task));
    }

    // ------------------------------------------------------------ notes

    void notesAreCappedAt10000Characters() {
        // "The plain-text body becomes the task's notes, up to 10,000 characters."
        QCOMPARE(made("Long", QString(10000, 'a')).notes.size(), 10000);
        const Task t = made("Longer", QString(12000, 'b'));
        QCOMPARE(t.notes.size(), 10000);
        QVERIFY(t.notes.startsWith("bbb"));
    }

    void notesAreNormalisedAndTrimmed() {
        const Task t = made("Lines", "\r\n\r\n  First\r\nSecond\rThird  \r\n\r\n");
        QCOMPARE(t.notes, "First\nSecond\nThird");
    }

    void htmlOnlyMessageStillMakesATask() {
        const MailTask::Result r = go(fixture("html-only.eml"), options());
        QVERIFY(r.ok());
        const Task t = m_store->task(r.taskId);
        QCOMPARE(t.title, "Newsletter signup");
        QVERIFY(t.notes.contains("Order shipped"));
    }

    // ------------------------------------------------------------ identity and duplicates

    void uidFromMessageId() {
        MailTask::Message m;
        m.messageId = "<abc@example.com>";
        m.subject = "ignored";
        QCOMPARE(MailTask::uidFor(m), "mail-" + sha("<abc@example.com>"));
        QCOMPARE(MailTask::uidFor(m).size(), 5 + 32);
        // Another Date, From or body changes nothing while the Message-ID is the same.
        m.body = "changed";
        QCOMPARE(MailTask::uidFor(m), "mail-" + sha("<abc@example.com>"));
    }

    void uidWithoutMessageIdUsesDateFromSubjectAndBody() {
        const MailTask::Message m = MailTask::parse(fixture("no-message-id.eml"));
        QVERIFY(m.messageId.isEmpty());
        const QString expect = "mail-" + sha("Tue, 13 Oct 2026 09:30:00 +0200\nSam Carter <sam@example.com>\nWater the plants\nThe ferns need it most.");
        QCOMPARE(MailTask::uidFor(m), expect);
        QCOMPARE(m_store->task(go(fixture("no-message-id.eml")).taskId).uid, expect);
        // Each ingredient matters.
        for (int i = 0; i < 4; ++i) {
            MailTask::Message other = m;
            (i == 0 ? other.dateHeader : i == 1 ? other.from : i == 2 ? other.subject : other.body) += "!";
            QVERIFY2(MailTask::uidFor(other) != expect, qPrintable(QString::number(i)));
        }
    }

    void sameMessageTwiceIsADuplicate() {
        const QByteArray raw = fixture("plain.eml");
        const MailTask::Result first = go(raw);
        QVERIFY(first.ok());
        const int tags = m_store->tags().size();
        const int tasks = m_store->tasks().size();
        const MailTask::Result second = go(raw);
        QVERIFY(!second.ok());
        QCOMPARE(second.taskId, qint64(0));
        QCOMPARE(second.reason, "duplicate");
        QCOMPARE(m_store->tasks().size(), tasks);
        QCOMPARE(m_store->tags().size(), tags);
        // Another message, even with the same subject, is its own task.
        QVERIFY(go(mail("Buy milk", "again")).ok());
    }

    void duplicateCreatesNoTagsOrChecklistItems() {
        const QByteArray raw = mail("Shop tag(newtag)", "1. a\n2. b");
        QVERIFY(go(raw).ok());
        const int tasks = m_store->tasks().size(), tags = m_store->tags().size();
        QCOMPARE(go(raw).reason, "duplicate");
        QCOMPARE(m_store->tasks().size(), tasks);
        QCOMPARE(m_store->tags().size(), tags);
    }

    void duplicateEvenAfterTheTaskIsDeleted() {
        // "If any task with that uid exists, deleted or not" the message is a duplicate, so a task the
        // user deleted does not come back at the next fetch.
        const QByteArray raw = fixture("plain.eml");
        Task t = m_store->task(go(raw).taskId);
        QVERIFY(t.id);
        t.deleted = true;
        m_store->begin("delete");
        m_store->putTask(t);
        m_store->commit();
        const int tasks = m_store->tasks().size();
        QCOMPARE(go(raw).reason, "duplicate");
        QCOMPARE(m_store->tasks().size(), tasks);
    }

    void completedTaskStillCountsAsDuplicate() {
        const QByteArray raw = fixture("plain.eml");
        Task t = m_store->task(go(raw).taskId);
        t.completed = true;
        m_store->begin("done");
        m_store->putTask(t);
        m_store->commit();
        QCOMPARE(go(raw).reason, "duplicate");
    }

    void noListAtAll() {
        auto empty = std::make_unique<Store>();
        QVERIFY(empty->open(m_dir.filePath("nolist.db")));
        const MailTask::Result r = MailTask::import(*empty, MailTask::parse(mail("x", "y")), options());
        QVERIFY(!r.ok());
        QCOMPARE(r.reason, "no list");
        QVERIFY(empty->tasks().isEmpty());
    }

    void withoutAnInboxTheFirstListIsUsed() {
        auto other = std::make_unique<Store>();
        QVERIFY(other->open(m_dir.filePath("noinbox.db")));
        TaskList b;
        b.id = other->nextId("lists");
        b.name = "B";
        b.order = 2;
        TaskList a;
        a.id = other->nextId("lists");
        a.name = "A";
        a.order = 1;
        other->begin("setup");
        other->putList(b);
        other->putList(a);
        other->commit();
        const MailTask::Result r = MailTask::import(*other, MailTask::parse(mail("x", "y")), options());
        QVERIFY(r.ok());
        QCOMPARE(other->task(r.taskId).listId, a.id);
    }

    // ------------------------------------------------------------ natural language

    void naturalLanguageOffLeavesTheSubjectAlone() {
        const Task t = made("Call Sam tomorrow #family !!");
        QCOMPARE(t.title, "Call Sam tomorrow #family !!");
        QVERIFY(!t.dueDate.isValid());
        QVERIFY(t.tagIds.isEmpty());
        QCOMPARE(t.priority, int(Priority::None));
    }

    void naturalLanguageOnReadsTheSubject() {
        MailTask::Options o;
        o.naturalLanguage = true;
        const Task t = made("Call Sam tomorrow 3pm #family !!", "x", o);
        QCOMPARE(t.title, "Call Sam");
        QCOMPARE(t.dueDate, QDate(2026, 10, 14));
        QCOMPARE(t.dueTime, 15 * 60);
        QCOMPARE(t.priority, int(Priority::Medium));
        QCOMPARE(tagNames(t), QStringList{"family"});
    }

    void naturalLanguageRunsAfterTheFilingInstructions() {
        MailTask::Options o;
        o.naturalLanguage = true;
        // Filing instructions are removed first and win over the free text.
        const Task t = made("Pay rent due(2026-11-01) friday #bills tag(home) priority(!!!) list(work)", "x", o);
        QCOMPARE(t.dueDate, QDate(2026, 11, 1));
        QCOMPARE(tagNames(t), (QStringList{"bills", "home"}));
        QCOMPARE(t.priority, int(Priority::High));
        QCOMPARE(t.listId, m_work);
        QVERIFY2(!t.title.contains("due("), qPrintable(t.title));
        QVERIFY(!t.title.contains("#bills"));
    }

    void naturalLanguageCanChooseTheList() {
        MailTask::Options o;
        o.naturalLanguage = true;
        o.listId = m_home;
        // Quick Entry's /List moves the task out of the destination, unless list() was given.
        const Task t = made("Call Sam /Work", "x", o);
        QCOMPARE(t.listId, m_work);
        QCOMPARE(t.title, "Call Sam");
        QCOMPARE(made("Call Sam /Work list(Errands)", "x", o).listId, m_errands);
        QCOMPARE(made("Call Sam", "x", o).listId, m_home);
    }

    void naturalLanguageSubjectThatIsAllDatesFallsBackToTheBody() {
        MailTask::Options o;
        o.naturalLanguage = true;
        QCOMPARE(made("tomorrow", "Renew the licence", o).title, "Renew the licence");
    }

    // ------------------------------------------------------------ the capturedmail preset

    void capturedMailMatchesMailTasksOnly() {
        // A smart list's `type: capturedmail` finds the tasks that arrived by mail.
        const Task mailed = made("From mail");
        Task manual;
        manual.id = m_store->nextId("tasks");
        manual.listId = m_inbox;
        manual.title = "By hand";
        manual.order = 99;
        m_store->begin("add");
        m_store->putTask(manual);
        m_store->commit();
        Task lookalike = manual;
        lookalike.id = m_store->nextId("tasks");
        lookalike.title = "Not mail";
        lookalike.uid = "xmail-123";
        m_store->begin("add");
        m_store->putTask(lookalike);
        m_store->commit();

        QString error;
        const Query q = Query::parse("type: capturedmail", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QueryContext c;
        c.store = m_store.get();
        c.now = Now;
        QStringList hits;
        for (const Task &t : m_store->tasks())
            if (q.matches(t, c))
                hits << t.title;
        QCOMPARE(hits, QStringList{mailed.title});
    }
};

REGISTER_TEST(MailTaskTest)

#include "test_mailtask.moc"
