#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <memory>

#include "restorelists.h"
#include "store.h"
#include "synctree.h"
#include "testregistry.h"
#include "todoist.h"
#include "webdav.h"

namespace {

// A tiny WebDAV server: files and folders in memory, answering PROPFIND
// (Depth 1), GET, PUT, DELETE and MKCOL, with basic authentication.
class FakeDav : public QTcpServer {
public:
    explicit FakeDav(const QString &root = "/dav/Fairfax/") : m_root(root) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handle(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }

    QUrl url() const { return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(m_root)); }
    QMap<QString, QByteArray> files;  // Path below the root -> content.
    QSet<QString> dirs;               // Paths below the root, without a trailing slash.
    QString user = "me", password = "secret";
    int requests = 0;

private:
    static QByteArray tag(const QByteArray &body, int version) {
        return "\"" + QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex() + "-" + QByteArray::number(version) + "\"";
    }

    void reply(QTcpSocket *s, int status, const QByteArray &body = {}) {
        const QByteArray text = status == 207 ? "Multi-Status" : status == 201 ? "Created" : status == 401 ? "Unauthorized"
                                : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "OK";
        s->write("HTTP/1.1 " + QByteArray::number(status) + " " + text + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                 "\r\nConnection: close\r\nContent-Type: application/xml\r\n\r\n" + body);
        s->disconnectFromHost();
    }

    void handle(QTcpSocket *s) {
        m_buffer[s].append(s->readAll());
        const QByteArray &buf = m_buffer[s];
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
        const QList<QByteArray> start = lines.first().trimmed().split(' ');
        int length = 0;
        QByteArray auth;
        for (const auto &line : lines.mid(1)) {
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
            if (line.toLower().startsWith("authorization:"))
                auth = line.mid(14).trimmed();
        }
        if (buf.size() < headerEnd + 4 + length)
            return;
        const QByteArray body = buf.mid(headerEnd + 4, length);
        m_buffer.remove(s);
        ++requests;

        const QByteArray expected = "Basic " + QString("%1:%2").arg(user, password).toUtf8().toBase64();
        if (auth != expected)
            return reply(s, 401);
        const QByteArray method = start.value(0);
        QString path = QUrl::fromPercentEncoding(start.value(1));
        if (!path.startsWith(m_root))
            return reply(s, 404);
        QString rel = path.mid(m_root.size());
        if (rel.endsWith('/'))
            rel.chop(1);

        if (method == "MKCOL") {
            if (rel.isEmpty() || dirs.contains(rel))
                return reply(s, 405);
            dirs.insert(rel);
            return reply(s, 201);
        }
        if (method == "PUT") {
            const bool existed = files.contains(rel);
            files.insert(rel, body);
            ++m_versions[rel];
            return reply(s, existed ? 204 : 201);
        }
        if (method == "GET")
            return files.contains(rel) ? reply(s, 200, files.value(rel)) : reply(s, 404);
        if (method == "DELETE") {
            if (!files.remove(rel))
                return reply(s, 404);
            return reply(s, 204);
        }
        if (method == "PROPFIND") {
            QByteArray xml = "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\">";
            auto entry = [&](const QString &href, bool collection, const QByteArray &etag, int size) {
                xml += "<d:response><d:href>" + QUrl::toPercentEncoding(href, "/") + "</d:href><d:propstat><d:prop>";
                xml += collection ? "<d:resourcetype><d:collection/></d:resourcetype>"
                                  : "<d:getetag>" + etag + "</d:getetag><d:getcontentlength>" + QByteArray::number(size) +
                                        "</d:getcontentlength><d:resourcetype/>";
                xml += "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
            };
            if (!rel.isEmpty() && !dirs.contains(rel))
                return reply(s, 404);
            entry(m_root + (rel.isEmpty() ? QString() : rel + "/"), true, {}, 0);
            const QString prefix = rel.isEmpty() ? QString() : rel + "/";
            for (const auto &d : std::as_const(dirs))
                if (d.startsWith(prefix) && !d.mid(prefix.size()).contains('/'))
                    entry(m_root + d + "/", true, {}, 0);
            for (auto it = files.cbegin(); it != files.cend(); ++it)
                if (it.key().startsWith(prefix) && !it.key().mid(prefix.size()).contains('/'))
                    entry(m_root + it.key(), false, tag(it.value(), m_versions.value(it.key())), it.value().size());
            xml += "</d:multistatus>";
            return reply(s, 207, xml);
        }
        reply(s, 405);
    }

    QString m_root;
    QHash<QTcpSocket *, QByteArray> m_buffer;
    QHash<QString, int> m_versions;
};

}  // namespace

class FolderSyncTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_n = 0;

    struct Machine {
        std::unique_ptr<Store> store;
        QString mirror;
    };

    Machine machine(const QString &name) {
        Machine m;
        m.store = std::make_unique<Store>();
        QString err;
        const QString base = m_dir.filePath(QString("%1%2").arg(name).arg(++m_n));
        [&] { QVERIFY2(m.store->open(base + "/fairfax.db", &err), qPrintable(err)); }();
        m.mirror = base + "/mirror";
        return m;
    }

    static qint64 addList(Store &s, const QString &name) {
        TaskList l;
        l.id = s.nextId("lists");
        l.name = name;
        s.begin("add");
        s.putList(l);
        s.commit();
        return l.id;
    }

    static qint64 addTask(Store &s, const QString &title, qint64 listId = 0, qint64 parent = 0) {
        Task t;
        t.id = s.nextId("tasks");
        t.listId = listId;
        t.parentId = parent;
        t.title = title;
        s.begin("add");
        s.putTask(t);
        s.commit();
        return t.id;
    }

    static qint64 find(const Store &s, const QString &title) {
        for (const auto &t : s.tasks())
            if (t.title == title && !t.deleted)
                return t.id;
        return 0;
    }

    static int liveTasks(const Store &s) {
        int n = 0;
        for (const auto &t : s.tasks())
            if (!t.deleted)
                ++n;
        return n;
    }

    static QJsonObject readJson(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }

    static void writeJson(const QString &path, const QJsonObject &obj) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }

    static QString taskFile(const QString &folder, const Store &s, qint64 id) {
        return folder + "/tasks/" + s.task(id).uid + ".json";
    }

    static bool syncs(Store &s, SyncTree::Remote &r, const QString &mirror, SyncTree::FirstSync first = SyncTree::FirstSync::Merge) {
        const auto out = SyncTree::sync(s, r, mirror, first);
        if (!out.ok)
            qWarning("sync failed: %s", qPrintable(out.error));
        return out.ok;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void folderRoundTripBetweenTwoStores() {
        const QString folder = m_dir.filePath("shared1");
        QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a"), b = machine("b");
        const qint64 list = addList(*a.store, "Errands");
        const qint64 parent = addTask(*a.store, "Parent", list);
        addTask(*a.store, "Child", list, parent);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(QFileInfo::exists(folder + "/Store.json"));
        QVERIFY(QDir(folder + "/tasks").entryList({"*.json"}).size() == 2);

        QVERIFY(syncs(*b.store, remote, b.mirror));
        QCOMPARE(liveTasks(*b.store), 2);
        const qint64 child = find(*b.store, "Child");
        QVERIFY(child);
        QCOMPARE(b.store->task(child).parentId, find(*b.store, "Parent"));
        QCOMPARE(b.store->list(b.store->task(child).listId).name, QString("Errands"));

        // An edit on b reaches a, and a deletion on a reaches b.
        Task t = b.store->task(child);
        t.title = "Child, edited";
        QTest::qWait(1100);  // `modified` has one-second resolution.
        b.store->begin("edit");
        b.store->putTask(t);
        b.store->commit();
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(find(*a.store, "Child, edited"));

        Task gone = a.store->task(find(*a.store, "Parent"));
        gone.deleted = true;
        QTest::qWait(1100);
        a.store->begin("delete");
        a.store->putTask(gone);
        a.store->commit();
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QVERIFY(!find(*b.store, "Parent"));
    }

    void extIsKeptVerbatimAndWrittenBack() {
        const QString folder = m_dir.filePath("ext1"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a"), b = machine("b");
        const qint64 list = addList(*a.store, "L");
        const qint64 id = addTask(*a.store, "Task", list);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        // Another writer adds its data to the file, with a larger `modified`.
        const QString file = taskFile(folder, *a.store, id);
        QJsonObject obj = readJson(file);
        QVERIFY(!obj.contains("ext"));  // omitted when empty
        obj["ext"] = QJsonObject{{"zeta", QJsonObject{{"b", 2}, {"a", QJsonArray{1, "two"}}}}, {"acme", QJsonObject{{"id", "X-1"}}}};
        obj["modified"] = obj.value("modified").toInteger() + 10;
        obj["unknown_key"] = "dropped";
        writeJson(file, obj);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QCOMPARE(a.store->task(id).ext, QString(R"({"acme":{"id":"X-1"},"zeta":{"a":[1,"two"],"b":2}})"));
        // It comes back unchanged, still under both writer names; other unknown keys are gone.
        const QJsonObject back = readJson(file);
        QCOMPARE(back.value("ext"), obj.value("ext"));
        QVERIFY(!back.contains("unknown_key"));
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QCOMPARE(b.store->task(find(*b.store, "Task")).ext, a.store->task(id).ext);
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QCOMPARE(readJson(file).value("ext"), obj.value("ext"));
        // An ext that is not an object is not kept.
        obj["ext"] = "text";
        obj["modified"] = obj.value("modified").toInteger() + 10;
        writeJson(file, obj);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(a.store->task(id).ext.isEmpty());
        QVERIFY(!readJson(file).contains("ext"));
    }

    void modifiedByNamesTheLastWriter() {
        const QString folder = m_dir.filePath("by1"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a"), b = machine("b");
        const QString me = "fairfax@" + QSysInfo::machineHostName();
        const qint64 id = addTask(*a.store, "Task");
        QVERIFY(syncs(*a.store, remote, a.mirror));
        const QString file = taskFile(folder, *a.store, id);
        QCOMPARE(readJson(file).value("modified_by").toString(), me);
        // Another writer's edit keeps its name, here and on the next machine, and
        // writing the file back in canonical form does not sign it.
        QJsonObject obj = readJson(file);
        obj["title"] = "Edited elsewhere";
        obj["modified_by"] = "acme-sync@laptop";
        obj["modified"] = obj.value("modified").toInteger() + 10;
        writeJson(file, obj);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QCOMPARE(a.store->task(id).modifiedBy, QString("acme-sync@laptop"));
        QCOMPARE(readJson(file).value("modified_by").toString(), QString("acme-sync@laptop"));
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QCOMPARE(b.store->task(find(*b.store, "Edited elsewhere")).modifiedBy, QString("acme-sync@laptop"));
        // A file without it stays without it.
        obj.remove("modified_by");
        obj["modified"] = obj.value("modified").toInteger() + 10;
        writeJson(file, obj);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(!readJson(file).contains("modified_by"));
        // An edit here signs it.
        Task t = a.store->task(id);
        t.title = "Edited here";
        a.store->begin("edit");
        a.store->putTask(t);
        a.store->commit();
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QCOMPARE(readJson(file).value("modified_by").toString(), me);
    }

    void everyRecordKindKeepsExt() {
        const QString folder = m_dir.filePath("ext2"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a"), b = machine("b");
        const QJsonObject ext{{"acme", QJsonObject{{"k", 1}}}};
        const QMap<QString, QString> files{{"list-groups", "G1"}, {"lists", "L1"}, {"sections", "S1"}, {"tag-groups", "TG1"},
                                           {"tags", "T1"}, {"locations", "LOC1"}, {"tasks", "K1"}, {"alarms", "A1"},
                                           {"attachments", "AT1"}};
        const qint64 base = 1792000000;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QJsonObject o{{"modified", base}, {"name", "n"}, {"title", "n"}, {"ext", ext}};
            if (it.key() == "sections")
                o["list_uid"] = "L1";
            if (it.key() == "tasks")
                o["list_uid"] = "L1";
            if (it.key() == "alarms" || it.key() == "attachments")
                o["task_uid"] = "K1";
            writeJson(folder + "/" + it.key() + "/" + it.value() + ".json", o);
        }
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(syncs(*b.store, remote, b.mirror));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            QCOMPARE(readJson(b.mirror + "/" + it.key() + "/" + it.value() + ".json").value("ext").toObject(), ext);
    }

    // The other writer's files name records that have not been committed yet.
    static void writeRefFiles(const QString &folder, qint64 base, bool dependencies) {
        auto put = [&](const QString &kind, const QString &uid, QJsonObject o) {
            o["modified"] = base;
            writeJson(folder + "/" + kind + "/" + uid + ".json", o);
        };
        if (!dependencies) {
            put("tasks", "K1", {{"title", "Waiting"}, {"list_uid", "L1"}, {"section_uid", "S1"}, {"parent_uid", "P1"},
                                {"tag_uids", QJsonArray{"T1", "T2"}}, {"location_uids", QJsonArray{"LOC1"}}});
            put("alarms", "A1", {{"task_uid", "K9"}, {"offset_minutes", 5}});
            put("attachments", "AT1", {{"task_uid", "K9"}, {"name", "a.png"}, {"file_name", "AT1.png"}});
            return;
        }
        put("lists", "L1", {{"name", "Projects"}});
        put("tags", "T1", {{"name", "first"}});
        put("sections", "S1", {{"name", "Phase"}, {"list_uid", "L1"}});
        put("tasks", "P1", {{"title", "Parent"}, {"list_uid", "L1"}});
        put("locations", "LOC1", {{"name", "Shop"}});
        put("tasks", "K9", {{"title", "Late"}, {"list_uid", "L1"}});
    }

    void referencesToMissingRecordsAreKeptUntilTheyArrive() {
        const QString folder = m_dir.filePath("refs1"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a");
        const qint64 home = addList(*a.store, "Home");
        const qint64 base = 1700000000;
        writeRefFiles(folder, base, false);
        QVERIFY(syncs(*a.store, remote, a.mirror));

        // The task shows in the default list, and its file keeps naming everything.
        const qint64 id = find(*a.store, "Waiting");
        QVERIFY(id);
        QCOMPARE(a.store->task(id).listId, home);
        QVERIFY(a.store->task(id).tagIds.isEmpty());
        auto expectFile = [&](const QStringList &tags) {
            const QJsonObject o = readJson(folder + "/tasks/K1.json");
            QCOMPARE(o.value("list_uid").toString(), QString("L1"));
            QCOMPARE(o.value("section_uid").toString(), QString("S1"));
            QCOMPARE(o.value("parent_uid").toString(), QString("P1"));
            QStringList got;
            for (const auto &v : o.value("tag_uids").toArray())
                got << v.toString();
            QCOMPARE(got, tags);
            QCOMPARE(o.value("location_uids").toArray().size(), 1);
            QCOMPARE(o.value("location_uids").toArray().at(0).toString(), QString("LOC1"));
        };
        expectFile({"T1", "T2"});
        QCOMPARE(a.store->alarms().size(), 1);
        QCOMPARE(a.store->alarms().begin()->taskId, qint64(0));
        QCOMPARE(readJson(folder + "/alarms/A1.json").value("task_uid").toString(), QString("K9"));
        QCOMPARE(a.store->attachments().size(), 1);
        QCOMPARE(readJson(folder + "/attachments/AT1.json").value("task_uid").toString(), QString("K9"));
        QVERIFY(syncs(*a.store, remote, a.mirror));
        expectFile({"T1", "T2"});

        // The list, one tag, the section, the parent, the place and the alarm's task arrive.
        writeRefFiles(folder, base, true);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        const Task t = a.store->task(id);
        QCOMPARE(a.store->list(t.listId).uid, QString("L1"));
        QCOMPARE(t.tagIds.size(), 1);
        QCOMPARE(a.store->tag(t.tagIds.first()).uid, QString("T1"));
        QCOMPARE(a.store->sections().value(t.sectionId).uid, QString("S1"));
        QCOMPARE(a.store->task(t.parentId).uid, QString("P1"));
        QCOMPARE(t.locationIds.size(), 1);
        QCOMPARE(a.store->alarms().begin()->taskId, find(*a.store, "Late"));
        QCOMPARE(a.store->attachments().begin()->taskId, find(*a.store, "Late"));
        expectFile({"T1", "T2"});  // the second tag is still not here

        // The second tag arrives later still.
        QJsonObject tag{{"name", "second"}, {"modified", base}};
        writeJson(folder + "/tags/T2.json", tag);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QCOMPARE(a.store->task(id).tagIds.size(), 2);
        expectFile({"T1", "T2"});
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QCOMPARE(a.store->task(id).tagIds.size(), 2);
        expectFile({"T1", "T2"});
    }

    void movingATaskEndsItsPendingList() {
        const QString folder = m_dir.filePath("refs2"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a");
        const qint64 home = addList(*a.store, "Home");
        const qint64 other = addList(*a.store, "Other");
        QCOMPARE(home + 1, other);
        const qint64 base = 1700000000;
        writeRefFiles(folder, base, false);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        const qint64 id = find(*a.store, "Waiting");
        Task t = a.store->task(id);
        t.listId = other;
        a.store->begin("move");
        a.store->putTask(t);
        a.store->commit();
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QCOMPARE(readJson(folder + "/tasks/K1.json").value("list_uid").toString(), a.store->list(other).uid);
        // The list it used to wait for turns up; the task stays where the user put it.
        writeRefFiles(folder, base, true);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QCOMPARE(a.store->task(id).listId, other);
        QCOMPARE(readJson(folder + "/tasks/K1.json").value("list_uid").toString(), a.store->list(other).uid);
    }

    void junkFilesInTheFolderAreIgnored() {
        const QString folder = m_dir.filePath("shared2"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a"), b = machine("b");
        addTask(*a.store, "Real");
        QVERIFY(syncs(*a.store, remote, a.mirror));
        const auto files = QDir(folder + "/tasks").entryList({"*.json"});
        QCOMPARE(files.size(), 1);
        // A Syncthing conflict copy must not turn into a second task.
        QVERIFY(QFile::copy(folder + "/tasks/" + files.first(), folder + "/tasks/" + files.first().chopped(5) + ".sync-conflict-20260101-ABC.json"));
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QCOMPARE(liveTasks(*b.store), 1);
    }

    void excludedListsStayLocal() {
        const QString folder = m_dir.filePath("shared3"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a"), b = machine("b");
        const qint64 shared = addList(*a.store, "Shared");
        const qint64 secret = addList(*a.store, "Private");
        addTask(*a.store, "Visible", shared);
        const qint64 hidden = addTask(*a.store, "Hidden", secret);
        Alarm al;
        al.id = a.store->nextId("alarms");
        al.taskId = hidden;
        a.store->begin("alarm");
        a.store->putAlarm(al);
        a.store->commit();
        SyncTree::setListExcluded(*a.store, secret, true);
        QVERIFY(SyncTree::isListExcluded(*a.store, secret));
        QVERIFY(!SyncTree::isListExcluded(*a.store, shared));

        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QVERIFY(find(*b.store, "Visible"));
        QVERIFY(!find(*b.store, "Hidden"));
        for (const auto &l : b.store->lists())
            QVERIFY(l.name != "Private");
        QVERIFY(b.store->alarms().isEmpty());

        // The excluded task survives a sync on its own machine, and a remote
        // record of the same list is not merged in.
        QVERIFY(find(*a.store, "Hidden"));
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(find(*a.store, "Hidden"));

        // Excluding a list that was already shared leaves the shared copy
        // for the other machines.
        const qint64 later = addList(*a.store, "Later");
        addTask(*a.store, "Early", later);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        SyncTree::setListExcluded(*a.store, later, true);
        QVERIFY(syncs(*a.store, remote, a.mirror));
        QVERIFY(syncs(*b.store, remote, b.mirror));
        QVERIFY(find(*b.store, "Early"));
    }

    void firstSyncAsksWhenBothSidesHaveTasks() {
        const QString folder = m_dir.filePath("shared4"); QDir().mkpath(folder);
        SyncTree::FolderRemote remote(folder);
        Machine a = machine("a"), b = machine("b"), c = machine("c");
        addTask(*a.store, "From A");
        QVERIFY(syncs(*a.store, remote, a.mirror));

        addTask(*b.store, "From B");
        auto out = SyncTree::sync(*b.store, remote, b.mirror);
        QVERIFY(out.needsChoice);
        QVERIFY(!out.ok);
        QVERIFY(find(*b.store, "From B"));
        QVERIFY(!find(*b.store, "From A"));

        // Merge keeps both.
        QVERIFY(syncs(*b.store, remote, b.mirror, SyncTree::FirstSync::Merge));
        QVERIFY(find(*b.store, "From A") && find(*b.store, "From B"));

        // Replace Local drops c's own task for the server's two.
        addTask(*c.store, "From C");
        QVERIFY(SyncTree::sync(*c.store, remote, c.mirror).needsChoice);
        QVERIFY(syncs(*c.store, remote, c.mirror, SyncTree::FirstSync::ReplaceLocal));
        QVERIFY(!find(*c.store, "From C"));
        QVERIFY(find(*c.store, "From A") && find(*c.store, "From B"));

        // Replace Server makes the folder hold only this machine's tasks.
        Machine d = machine("d");
        addTask(*d.store, "From D");
        QVERIFY(syncs(*d.store, remote, d.mirror, SyncTree::FirstSync::ReplaceServer));
        Machine e = machine("e");
        QVERIFY(syncs(*e.store, remote, e.mirror));
        QVERIFY(find(*e.store, "From D"));
        QVERIFY(!find(*e.store, "From A"));
    }

    void webDavRoundTripAgainstAFakeServer() {
        FakeDav dav;
        QVERIFY(dav.isListening());
        Machine a = machine("a"), b = machine("b");
        const qint64 list = addList(*a.store, "Over HTTP");
        addTask(*a.store, "One", list);
        addTask(*a.store, "Two", list);

        WebDavRemote remoteA(dav.url(), "me", "secret");
        QVERIFY(syncs(*a.store, remoteA, a.mirror));
        QVERIFY(dav.files.contains("Store.json"));
        int taskFiles = 0;
        for (auto it = dav.files.cbegin(); it != dav.files.cend(); ++it)
            if (it.key().startsWith("tasks/"))
                ++taskFiles;
        QCOMPARE(taskFiles, 2);
        QVERIFY(dav.dirs.contains("tasks"));

        WebDavRemote remoteB(dav.url(), "me", "secret");
        QVERIFY(syncs(*b.store, remoteB, b.mirror));
        QCOMPARE(liveTasks(*b.store), 2);

        // b edits, a pulls the change; a second sync with nothing new uploads nothing.
        Task t = b.store->task(find(*b.store, "One"));
        t.title = "One, edited";
        QTest::qWait(1100);
        b.store->begin("edit");
        b.store->putTask(t);
        b.store->commit();
        QVERIFY(syncs(*b.store, remoteB, b.mirror));
        QVERIFY(syncs(*a.store, remoteA, a.mirror));
        QVERIFY(find(*a.store, "One, edited"));
        const auto idle = SyncTree::sync(*a.store, remoteA, a.mirror);
        QVERIFY(idle.ok);
        QCOMPARE(idle.pushed, 0);
        QCOMPARE(idle.changedLocally, 0);

        // A deletion removes the server's file once the tombstone is pruned...
        // and plain deletion keeps the tombstone, so the other side sees it.
        Task gone = a.store->task(find(*a.store, "Two"));
        gone.deleted = true;
        QTest::qWait(1100);
        a.store->begin("delete");
        a.store->putTask(gone);
        a.store->commit();
        QVERIFY(syncs(*a.store, remoteA, a.mirror));
        QVERIFY(syncs(*b.store, remoteB, b.mirror));
        QVERIFY(!find(*b.store, "Two"));
    }

    void webDavReportsBadCredentialsAndUnreachableServers() {
        FakeDav dav;
        Machine a = machine("a");
        addTask(*a.store, "x");
        WebDavRemote wrong(dav.url(), "me", "nope");
        auto out = SyncTree::sync(*a.store, wrong, a.mirror, SyncTree::FirstSync::Merge);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains("password"), qPrintable(out.error));

        const QUrl dead = dav.url();
        dav.close();
        WebDavRemote unreachable(dead, "me", "secret", 3000);
        out = SyncTree::sync(*a.store, unreachable, a.mirror, SyncTree::FirstSync::Merge);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains("reach"), qPrintable(out.error));
    }

    void todoistLeavesExcludedListsAlone() {
        Machine a = machine("a");
        const qint64 shared = addList(*a.store, "Shared");
        const qint64 secret = addList(*a.store, "Private");
        addTask(*a.store, "Visible", shared);
        addTask(*a.store, "Hidden", secret);
        SyncTree::setListExcluded(*a.store, secret, true);
        const QJsonArray commands = TodoistMapping::buildCommands(*a.store);
        QStringList text;
        for (const auto &c : commands)
            text << QString::fromUtf8(QJsonDocument(c.toObject().value("args").toObject()).toJson(QJsonDocument::Compact));
        const QString all = text.join('\n');
        QVERIFY(all.contains("Visible"));
        QVERIFY(all.contains("Shared"));
        QVERIFY(!all.contains("Hidden"));
        QVERIFY(!all.contains("Private"));
    }

    void restoreSelectedListsMergesAsNewRecords() {
        Machine live = machine("live");
        const qint64 keep = addList(*live.store, "Keep");
        addTask(*live.store, "Existing", keep);
        Tag home;
        home.id = live.store->nextId("tags");
        home.name = "home";
        live.store->begin("tag");
        live.store->putTag(home);
        live.store->commit();

        // The "backup": another database with two lists worth of data.
        Machine old = machine("old");
        Store &o = *old.store;
        const qint64 work = addList(o, "Work");
        const qint64 other = addList(o, "Other");
        Tag oldHome, oldRare;
        oldHome.id = o.nextId("tags");
        oldHome.name = "Home";
        oldRare.id = o.nextId("tags");
        oldRare.name = "rare";
        ListSection sec;
        sec.id = o.nextId("sections");
        sec.listId = work;
        sec.name = "Phase 1";
        Task parent, child, elsewhere, deleted;
        parent.id = o.nextId("tasks");
        parent.listId = work;
        parent.title = "Parent";
        parent.sectionId = sec.id;
        child.id = o.nextId("tasks");
        child.listId = work;
        child.parentId = parent.id;
        child.title = "Child";
        child.tagIds = {oldHome.id, oldRare.id};
        elsewhere.id = o.nextId("tasks");
        elsewhere.listId = other;
        elsewhere.title = "Not selected";
        deleted.id = o.nextId("tasks");
        deleted.listId = work;
        deleted.title = "Trashed";
        deleted.deleted = true;
        Alarm al;
        al.id = o.nextId("alarms");
        al.taskId = child.id;
        al.offset = 30;
        o.begin("data");
        o.putTag(oldHome);
        o.putTag(oldRare);
        o.putSection(sec);
        o.putTask(parent);
        o.putTask(child);
        o.putTask(elsewhere);
        o.putTask(deleted);
        o.putAlarm(al);
        o.commit();

        const auto lists = RestoreLists::listsIn(o);
        QCOMPARE(lists.size(), 2);
        QString workUid;
        for (const auto &l : lists)
            if (l.name == "Work") {
                workUid = l.uid;
                QCOMPARE(l.tasks, 2);
            }
        QVERIFY(!workUid.isEmpty());

        const int tasksBefore = liveTasks(*live.store);
        const auto result = RestoreLists::merge(*live.store, o, {workUid}, QString());
        QCOMPARE(result.lists, 1);
        QCOMPARE(result.tasks, 2);
        QCOMPARE(liveTasks(*live.store), tasksBefore + 2);
        QVERIFY(find(*live.store, "Existing"));
        QVERIFY(!find(*live.store, "Not selected"));
        QVERIFY(!find(*live.store, "Trashed"));

        const Task c = live.store->task(find(*live.store, "Child"));
        const Task p = live.store->task(find(*live.store, "Parent"));
        QCOMPARE(c.parentId, p.id);
        QCOMPARE(c.listId, p.listId);
        QVERIFY(c.listId != keep);
        QCOMPARE(live.store->list(c.listId).name, QString("Work"));
        QVERIFY(live.store->list(c.listId).uid != lists.first().uid);
        QVERIFY(live.store->groups().contains(live.store->list(c.listId).groupId));
        QCOMPARE(live.store->groups().value(live.store->list(c.listId).groupId).name, QString("Restored"));
        QCOMPARE(live.store->sectionsOf(c.listId).size(), 1);
        QCOMPARE(live.store->sectionsOf(c.listId).first().name, QString("Phase 1"));
        QCOMPARE(live.store->alarmsFor(c.id).size(), 1);
        // "Home" matched the existing "home" tag; "rare" was added.
        QCOMPARE(c.tagIds.size(), 2);
        QVERIFY(c.tagIds.contains(home.id));
        int tagCount = 0;
        for (const auto &t : live.store->tags())
            if (!t.deleted)
                ++tagCount;
        QCOMPARE(tagCount, 2);

        // One undo takes all of it away again.
        QVERIFY(live.store->canUndo());
        QVERIFY(live.store->undo());
        QCOMPARE(liveTasks(*live.store), tasksBefore);
        QVERIFY(!find(*live.store, "Child"));
    }
};

REGISTER_TEST(FolderSyncTest)
#include "test_foldersync.moc"
