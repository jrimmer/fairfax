#include <QtSql>
#include <QtTest>
#include <memory>

#include "exchange.h"
#include "store.h"
#include "testregistry.h"
#include "ziparchive.h"

#ifndef FAIRFAX_NO_ZLIB
#include <zlib.h>
#endif

// Zipped task-database backups: archives are built here from a small raw
// database, so the reader is checked against what a zip tool writes.
class ZipArchiveTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;
    QString m_raw;

    QString file(const QString &name) { return m_dir.filePath(name); }

    std::unique_ptr<Store> freshStore() {
        auto s = std::make_unique<Store>();
        QString err;
        [&] { QVERIFY2(s->open(file(QString("zstore%1.db").arg(++m_counter)), &err), qPrintable(err)); }();
        return s;
    }

    static QByteArray le(quint32 v, int bytes) {
        QByteArray b;
        for (int i = 0; i < bytes; ++i)
            b.append(char((v >> (8 * i)) & 0xFF));
        return b;
    }

    struct Member {
        QString name;
        QByteArray data;
        quint16 method = 0;
        QByteArray packed;  // What goes in the archive (compressed for method 8).
        quint32 crc = 0;
    };

    static Member storedMember(const QString &name, const QByteArray &data) {
        Member m;
        m.name = name;
        m.data = data;
        m.packed = data;
        m.crc = ZipArchive::crc32Of(data);
        return m;
    }

    static Member deflatedMember(const QString &name, const QByteArray &data) {
        Member m = storedMember(name, data);
#ifndef FAIRFAX_NO_ZLIB
        m.method = 8;
        z_stream s{};
        deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        m.packed.resize(int(deflateBound(&s, uLong(data.size()))));
        s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
        s.avail_in = uInt(data.size());
        s.next_out = reinterpret_cast<Bytef *>(m.packed.data());
        s.avail_out = uInt(m.packed.size());
        deflate(&s, Z_FINISH);
        m.packed.resize(int(s.total_out));
        deflateEnd(&s);
#endif
        return m;
    }

    static QByteArray zip(const QList<Member> &members) {
        QByteArray out, central;
        for (const Member &m : members) {
            const QByteArray name = m.name.toUtf8();
            const quint32 offset = quint32(out.size());
            out += le(0x04034b50, 4) + le(20, 2) + le(0, 2) + le(m.method, 2) + le(0, 2) + le(0x21, 2) + le(m.crc, 4) +
                   le(quint32(m.packed.size()), 4) + le(quint32(m.data.size()), 4) + le(quint32(name.size()), 2) + le(0, 2) + name +
                   m.packed;
            central += le(0x02014b50, 4) + le(20, 2) + le(20, 2) + le(0, 2) + le(m.method, 2) + le(0, 2) + le(0x21, 2) + le(m.crc, 4) +
                       le(quint32(m.packed.size()), 4) + le(quint32(m.data.size()), 4) + le(quint32(name.size()), 2) + le(0, 2) +
                       le(0, 2) + le(0, 2) + le(0, 2) + le(0, 4) + le(offset, 4) + name;
        }
        const quint32 cdOffset = quint32(out.size());
        out += central;
        out += le(0x06054b50, 4) + le(0, 2) + le(0, 2) + le(quint16(members.size()), 2) + le(quint16(members.size()), 2) +
               le(quint32(central.size()), 4) + le(cdOffset, 4) + le(0, 2);
        return out;
    }

    QString writeFile(const QString &name, const QByteArray &bytes) {
        QFile f(file(name));
        [&] { QVERIFY(f.open(QIODevice::WriteOnly)); }();
        f.write(bytes);
        f.close();
        return f.fileName();
    }

    QByteArray rawBytes() {
        QFile f(m_raw);
        [&] { QVERIFY(f.open(QIODevice::ReadOnly)); }();
        return f.readAll();
    }

    static QList<Member> backupMembers(const Member &db) {
        return {storedMember("Backup/", {}), storedMember("BackupPayload/manifest.json", R"({"version":1})"),
                storedMember("BackupPayload/files/", {}), db};
    }

    static QStringList snapshot(const Store &s) {
        QStringList out;
        for (const TaskList &l : s.lists())
            out << QString("list|%1|%2").arg(l.name).arg(l.kind);
        for (const Task &t : s.tasks())
            out << QString("task|%1|%2|%3").arg(t.title, s.list(t.listId).name).arg(t.priority);
        for (const Tag &t : s.tags())
            out << QString("tag|%1").arg(t.name);
        for (const Alarm &a : s.alarms())
            out << QString("alarm|%1|%2").arg(a.kind).arg(a.offset);
        out.sort();
        return out;
    }

    static QString exec(QSqlDatabase &db, const QString &sql) {
        QSqlQuery q(db);
        return q.exec(sql) ? QString() : q.lastError().text() + " in " + sql;
    }

private slots:
    void initTestCase() {
        m_raw = file("raw.db");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "makezipdb");
            db.setDatabaseName(m_raw);
            QVERIFY(db.open());
            const QStringList sql{
                R"(CREATE TABLE calgroups (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(100), groupname VARCHAR(255) NOT NULL,
                   displayorder INTEGER NOT NULL DEFAULT 0, isdeleted INTEGER NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE calendars (primid INTEGER PRIMARY KEY NOT NULL, title TEXT, uid VARCHAR(255) NOT NULL,
                   colorid INTEGER DEFAULT 0, displayorder INTEGER DEFAULT 0, canshowinall INTEGER NOT NULL DEFAULT 1,
                   canshowintoday INTEGER NOT NULL DEFAULT 1, isdeleted INTEGER NOT NULL DEFAULT 0, smartsearch VARCHAR(255),
                   showindone INTEGER NOT NULL DEFAULT 1, starred INTEGER NOT NULL DEFAULT 0, donecal INTEGER NOT NULL DEFAULT 0,
                   isscheduled INTEGER NOT NULL DEFAULT 0, isallcal INTEGER NOT NULL DEFAULT 0, istodaycal INTEGER NOT NULL DEFAULT 0,
                   parentuid VARCHAR(100), isarchived INTEGER NOT NULL DEFAULT 0, isinboxcal INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE taggroups (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(100), groupname VARCHAR(255) NOT NULL,
                   displayorder INTEGER NOT NULL DEFAULT 0, isdeleted INTEGER NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE tags (primid INTEGER PRIMARY KEY NOT NULL, tag VARCHAR(255) NOT NULL, displayorder INTEGER NOT NULL DEFAULT 0,
                   isdeleted INTEGER NOT NULL DEFAULT 0, uid VARCHAR(100), parentuid VARCHAR(100), isheld INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE tasks (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(255) NOT NULL, calendaruid VARCHAR(255) NOT NULL,
                   title TEXT NOT NULL, notes TEXT, datestamp DOUBLE NOT NULL DEFAULT 0, duedate DOUBLE DEFAULT 0,
                   priority INTEGER NOT NULL DEFAULT 0, iscompleted INTEGER NOT NULL DEFAULT 0, completeddate INTEGER DEFAULT 0,
                   isdeleted INTEGER NOT NULL DEFAULT 0, tags TEXT, actionvalue TEXT, actiontype INTEGER NOT NULL DEFAULT -1,
                   displayorder INTEGER NOT NULL DEFAULT 0, recurrence INTEGER NOT NULL DEFAULT 0, repeatvalue INTEGER NOT NULL DEFAULT 0,
                   repeattype INTEGER NOT NULL DEFAULT 0, parent VARCHAR(255), tasktype INTEGER NOT NULL DEFAULT 0,
                   startdate DOUBLE DEFAULT 0, duetime DOUBLE DEFAULT 999999, archived INTEGER DEFAULT 0,
                   starred INTEGER NOT NULL DEFAULT 0, recurrenceendtype INTEGER NOT NULL DEFAULT 0,
                   recurrenceendrepeats INTEGER NOT NULL DEFAULT 0, recurrenceenddate DOUBLE NOT NULL DEFAULT 0,
                   creationstamp DOUBLE NOT NULL DEFAULT 0, iscollapsed INTEGER NOT NULL DEFAULT 0,
                   duration INTEGER NOT NULL DEFAULT 0))",
                R"(CREATE TABLE alarms (primid INTEGER PRIMARY KEY NOT NULL, uid VARCHAR(255) NOT NULL, taskuid VARCHAR(255) NOT NULL,
                   action TEXT NOT NULL, relativetrigger DOUBLE DEFAULT 0, absolutetrigger DOUBLE DEFAULT 0,
                   isdeleted INTEGER NOT NULL DEFAULT 0))",
                "INSERT INTO calgroups(uid,groupname,displayorder) VALUES('g1','Personal',1)",
                "INSERT INTO calendars(title,uid,colorid,displayorder,isinboxcal) VALUES('Inbox','c0',0,0,1)",
                "INSERT INTO calendars(title,uid,colorid,displayorder,parentuid) VALUES('Errands','c1',14,1,'g1')",
                "INSERT INTO calendars(title,uid,displayorder,smartsearch) VALUES('Hot','c2',2,'priority:high')",
                "INSERT INTO tags(tag,uid) VALUES('phone','t1')",
                "INSERT INTO tasks(uid,calendaruid,title,notes,priority,tags,displayorder) VALUES('k1','c1','Buy milk','2%',3,'phone, newtag',1)",
                "INSERT INTO tasks(uid,calendaruid,title,parent,displayorder) VALUES('k2','c1','Sub item','k1',2)",
                "INSERT INTO alarms(uid,taskuid,action,relativetrigger) VALUES('a1','k1','n',-900)",
            };
            for (const QString &q : sql) {
                const QString e = exec(db, q);
                QVERIFY2(e.isEmpty(), qPrintable(e));
            }
            db.close();
        }
        QSqlDatabase::removeDatabase("makezipdb");
    }

    void rawStillImports() {
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, m_raw);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 2);
        QCOMPARE(r.alarms, 1);
        QVERIFY(snapshot(*s).contains("list|Errands|0"));
    }

    void storedZipMatchesRaw() {
        const QString z = writeFile("stored.zip", zip(backupMembers(storedMember("data.db", rawBytes()))));
        auto want = freshStore();
        QVERIFY(Exchange::importTaskDatabase(*want, m_raw).error.isEmpty());
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, z);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(snapshot(*s), snapshot(*want));
        QCOMPARE(r.tasks, 2);
        QCOMPARE(r.alarms, 1);
    }

    void deflatedZipMatchesRaw() {
#ifdef FAIRFAX_NO_ZLIB
        QSKIP("built without zlib");
#else
        const Member db = deflatedMember("data.db", rawBytes());
        QCOMPARE(db.method, quint16(8));
        QVERIFY(db.packed.size() < db.data.size());
        const QString z = writeFile("deflated.zip", zip(backupMembers(db)));
        auto want = freshStore();
        QVERIFY(Exchange::importTaskDatabase(*want, m_raw).error.isEmpty());
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, z);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(snapshot(*s), snapshot(*want));
        QCOMPARE(r.tasks, 2);
#endif
    }

    void memberChosenByContentNotName() {
        // The database is not named .db; a decoy .db member is not SQLite; __MACOSX is skipped.
        const QList<Member> members{storedMember("notes.db", "just text"), storedMember("__MACOSX/._x.db", rawBytes()),
                                    storedMember("payload/tasks.bin", rawBytes())};
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, writeFile("odd.zip", zip(members)));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tasks, 2);
    }

    void zipWithoutDatabaseFails() {
        auto s = freshStore();
        const ImportResult r =
            Exchange::importTaskDatabase(*s, writeFile("none.zip", zip({storedMember("a.txt", "hello"), storedMember("b/", {})})));
        QVERIFY2(r.error.contains("does not contain a task database"), qPrintable(r.error));
        QVERIFY(!s->canUndo());
        QCOMPARE(r.tasks, 0);
    }

    void truncatedZipFails() {
        QByteArray bytes = zip(backupMembers(storedMember("data.db", rawBytes())));
        bytes.chop(30);
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, writeFile("cut.zip", bytes));
        QVERIFY2(r.error.contains("damaged"), qPrintable(r.error));
        QVERIFY(!s->canUndo());
        QCOMPARE(r.tasks, 0);
    }

    void corruptedCrcFails() {
        QList<Member> members = backupMembers(storedMember("data.db", rawBytes()));
        members.last().crc ^= 0xDEADBEEF;
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, writeFile("crc.zip", zip(members)));
        QVERIFY2(r.error.contains("checksum"), qPrintable(r.error));
        QVERIFY(!s->canUndo());
        QCOMPARE(r.tasks, 0);
    }

    void unsupportedMethodFails() {
        QList<Member> members = backupMembers(storedMember("data.db", rawBytes()));
        members.last().method = 12;  // bzip2
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, writeFile("bz.zip", zip(members)));
        QVERIFY2(r.error.contains("unzip it and import the .db inside"), qPrintable(r.error));
        QVERIFY(!s->canUndo());
        QCOMPARE(r.tasks, 0);
    }

    void notADatabaseOrZip() {
        auto s = freshStore();
        const ImportResult r = Exchange::importTaskDatabase(*s, writeFile("plain.txt", "hello, this is plain text"));
        QVERIFY2(r.error.contains("not a task database"), qPrintable(r.error));
        QVERIFY(!s->canUndo());
    }
};

REGISTER_TEST(ZipArchiveTest)
#include "test_ziparchive.moc"
