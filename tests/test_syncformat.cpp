#include <QtTest>
#include <memory>

#include "gitsync.h"
#include "store.h"
#include "synctree.h"
#include "testregistry.h"

// Keeps docs/sync-format.md honest. Every ```json block that follows a
// "Sample:" line is a record file; the samples are written into a sync tree,
// imported into an empty store the way a sync does it, exported again, and
// compared with what the document claims.

namespace {

const qint64 kNow = 1792900000;  // A little after the samples' `modified`.

const QStringList kFolders{"list-groups", "lists", "sections", "tag-groups", "tags", "locations", "tasks", "alarms", "attachments"};

// path -> file content, for every sample in the document.
QMap<QString, QByteArray> docSamples(QString *error) {
    QMap<QString, QByteArray> samples;
    QFile doc(QStringLiteral(FAIRFAX_ROOT "/docs/sync-format.md"));
    if (!doc.open(QIODevice::ReadOnly)) {
        *error = "cannot open docs/sync-format.md";
        return samples;
    }
    const QStringList lines = QString::fromUtf8(doc.readAll()).split('\n');
    static const QRegularExpression head(R"(^Sample: `([^`]+)`\s*$)");
    for (int i = 0; i < lines.size(); ++i) {
        const auto m = head.match(lines[i]);
        if (!m.hasMatch())
            continue;
        int j = i + 1;
        while (j < lines.size() && lines[j].trimmed().isEmpty())
            ++j;
        if (j >= lines.size() || lines[j].trimmed() != "```json") {
            *error = "no json block after: " + lines[i];
            return {};
        }
        QStringList body;
        for (++j; j < lines.size() && lines[j].trimmed() != "```"; ++j)
            body << lines[j];
        if (j >= lines.size()) {
            *error = "unterminated json block after: " + lines[i];
            return {};
        }
        samples.insert(m.captured(1), (body.join('\n') + "\n").toUtf8());
        i = j;
    }
    return samples;
}

QJsonObject parse(const QByteArray &bytes, bool *ok = nullptr) {
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(bytes, &pe);
    if (ok)
        *ok = pe.error == QJsonParseError::NoError && d.isObject();
    return d.object();
}

QByteArray readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(bytes);
}

// Imports `files` (path -> content) into an empty store through the folder
// sync cycle (the shared tree code), exports the store, and lists everything
// that differs from `samples`. Empty means the samples came through intact.
// `files` is what gets written to the tree; it is `samples` unless a test
// has damaged a copy.
QStringList importAndCompare(const QMap<QString, QByteArray> &samples, const QMap<QString, QByteArray> &files, int *imported = nullptr) {
    QTemporaryDir dir;
    QStringList problems;
    const QString remote = dir.filePath("remote");
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        writeFile(remote + "/" + it.key(), it.value());

    Store store;
    QString err;
    if (!store.open(dir.filePath("fairfax.db"), &err))
        return {"cannot open store: " + err};
    SyncTree::FolderRemote folder(remote);
    const QDateTime now = QDateTime::fromSecsSinceEpoch(kNow);
    const SyncTree::Outcome out = SyncTree::sync(store, folder, dir.filePath("mirror"), SyncTree::FirstSync::Ask, now);
    if (!out.ok)
        return {"sync failed: " + out.error};
    if (imported)
        *imported = out.changedLocally;

    const QString exported = dir.filePath("export");
    if (!SyncTree::writeTree(store, exported, now, &err))
        return {"export failed: " + err};

    QMap<QString, QSet<QString>> documentedKeys;  // folder -> union of keys in the samples
    for (auto it = samples.cbegin(); it != samples.cend(); ++it) {
        const QString &path = it.key();
        bool ok = false;
        const QJsonObject sample = parse(it.value(), &ok);
        if (!ok) {
            problems << path + ": sample is not a JSON object";
            continue;
        }
        const QByteArray back = readFile(exported + "/" + path);
        if (back.isEmpty()) {
            problems << path + ": record did not arrive";
            continue;
        }
        const QJsonObject got = parse(back);
        const QString folder = path.section('/', 0, 0);
        for (auto k = sample.begin(); k != sample.end(); ++k) {
            documentedKeys[folder].insert(k.key());
            if (!got.contains(k.key()))
                problems << QString("%1: field %2 is not exported").arg(path, k.key());
            else if (got.value(k.key()) != k.value())
                problems << QString("%1: field %2 is %3 after import, the sample says %4")
                                .arg(path, k.key(), QString::fromUtf8(QJsonDocument(QJsonArray{got.value(k.key())}).toJson(QJsonDocument::Compact)),
                                     QString::fromUtf8(QJsonDocument(QJsonArray{k.value()}).toJson(QJsonDocument::Compact)));
        }
        if (path == "Store.json" && got != sample)
            problems << "Store.json differs from what Fairfax writes";
    }
    // The document must name every field Fairfax writes for a kind.
    for (const auto &folder : kFolders) {
        const QStringList names = QDir(exported + "/" + folder).entryList({"*.json"}, QDir::Files);
        if (names.isEmpty() || !documentedKeys.contains(folder))
            continue;
        const QJsonObject got = parse(readFile(exported + "/" + folder + "/" + names.first()));
        for (auto k = got.begin(); k != got.end(); ++k)
            if (!documentedKeys[folder].contains(k.key()))
                problems << QString("%1: Fairfax writes field %2, the samples never show it").arg(folder, k.key());
    }
    return problems;
}

}  // namespace

class SyncFormatTest : public QObject {
    Q_OBJECT

    static bool git(const QString &dir, const QStringList &args) {
        QProcess p;
        p.setWorkingDirectory(dir);
        p.start("git", QStringList{"-c", "user.name=agent", "-c", "user.email=agent@localhost"} + args);
        return p.waitForFinished(20000) && p.exitCode() == 0;
    }

private slots:
    void samplesCoverEveryRecordKind() {
        QString err;
        const auto samples = docSamples(&err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(samples.contains("Store.json"));
        for (const auto &folder : kFolders) {
            bool found = false;
            for (auto it = samples.cbegin(); it != samples.cend(); ++it)
                found = found || it.key().startsWith(folder + "/");
            QVERIFY2(found, qPrintable("no sample for " + folder));
        }
        for (auto it = samples.cbegin(); it != samples.cend(); ++it) {
            bool ok = false;
            parse(it.value(), &ok);
            QVERIFY2(ok, qPrintable(it.key() + " is not valid JSON"));
            if (it.key() != "Store.json") {
                const QString uid = QFileInfo(it.key()).completeBaseName();
                QCOMPARE(parse(it.value()).value("uid").toString(), uid);
            }
        }
    }

    void samplesImportIntact() {
        QString err;
        const auto samples = docSamples(&err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        int imported = 0;
        const QStringList problems = importAndCompare(samples, samples, &imported);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
        QCOMPARE(imported, int(samples.size()) - 1);  // everything but Store.json
    }

    // A required field removed from a sample must show up as a difference.
    void missingRequiredFieldIsFlagged_data() {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("field");
        QTest::newRow("task title") << "tasks" << "title";
        QTest::newRow("task list") << "tasks" << "list_uid";
        QTest::newRow("task modified") << "tasks" << "modified";
        QTest::newRow("alarm task") << "alarms" << "task_uid";
        QTest::newRow("list name") << "lists" << "name";
        QTest::newRow("attachment file") << "attachments" << "file_name";
    }
    void missingRequiredFieldIsFlagged() {
        QFETCH(QString, folder);
        QFETCH(QString, field);
        QString err;
        auto samples = docSamples(&err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(importAndCompare(samples, samples).isEmpty());
        int mutated = 0;
        for (auto it = samples.begin(); it != samples.end(); ++it) {
            if (!it.key().startsWith(folder + "/"))
                continue;
            QJsonObject obj = parse(it.value());
            if (!obj.contains(field))
                continue;
            obj.remove(field);
            // Only this file loses the field; the intact samples stay the reference.
            auto broken = samples;
            broken[it.key()] = QJsonDocument(obj).toJson(QJsonDocument::Indented);
            const QStringList problems = importAndCompare(samples, broken);
            QVERIFY2(!problems.isEmpty(), qPrintable("removing " + field + " from " + it.key() + " went unnoticed"));
            ++mutated;
            break;
        }
        QVERIFY2(mutated == 1, "no sample had the field to remove");
    }

    // The same samples, pushed to a git repository by a second "machine" and
    // pulled by a Fairfax store, then an edit and a completion the way the
    // document's "Writing from another program" section does them.
    void gitRoundTrip() {
        QString err;
        const auto samples = docSamples(&err);
        QVERIFY2(err.isEmpty(), qPrintable(err));

        QTemporaryDir dir;
        const QString bare = dir.filePath("remote.git");
        QVERIFY(git(dir.path(), {"init", "--bare", "-q", "-b", "main", bare}));
        const QString agent = dir.filePath("agent");
        QVERIFY(git(dir.path(), {"clone", "-q", bare, agent}));
        QVERIFY(git(agent, {"checkout", "-q", "-B", "main"}));
        for (auto it = samples.cbegin(); it != samples.cend(); ++it)
            writeFile(agent + "/" + it.key(), it.value());
        QVERIFY(git(agent, {"add", "-A"}));
        QVERIFY(git(agent, {"commit", "-q", "-m", "Add samples"}));
        QVERIFY(git(agent, {"push", "-q", "origin", "main"}));

        Store store;
        QVERIFY2(store.open(dir.filePath("fairfax.db"), &err), qPrintable(err));
        GitSync sync(&store, dir.filePath("wc"));
        sync.setRemote(bare);
        QTRY_VERIFY_WITH_TIMEOUT(!sync.busy(), 30000);

        auto titleOf = [&](const QString &uid) {
            for (const auto &t : store.tasks())
                if (t.uid == uid && !t.deleted)
                    return t.title;
            return QString("<none>");
        };
        const QString project = "70000000-0000-4000-8000-000000000001";
        const QString plain = "70000000-0000-4000-8000-000000000004";
        QCOMPARE(titleOf(project), QString("Launch the new site"));
        QCOMPARE(titleOf(plain), QString("Call the plumber"));
        QCOMPARE(titleOf("70000000-0000-4000-8000-000000000005"), QString("<none>"));  // the tombstone

        // Edit, complete and delete the plain task the way the document's
        // writing section does: start from the newest files (Fairfax has
        // pushed its own write-back meanwhile), change the record with a
        // larger `modified`, commit, push.
        const QString file = agent + "/tasks/" + plain + ".json";
        auto change = [&](qint64 modified, const std::function<void(QJsonObject &)> &edit) {
            QVERIFY(git(agent, {"fetch", "-q", "origin"}));
            QVERIFY(git(agent, {"reset", "-q", "--hard", "origin/main"}));
            QJsonObject obj = parse(readFile(file));
            QVERIFY(obj.value("modified").toInteger() < modified);
            edit(obj);
            obj["modified"] = modified;
            writeFile(file, QJsonDocument(obj).toJson(QJsonDocument::Indented));
            QVERIFY(git(agent, {"commit", "-q", "-am", "Change task"}));
            QVERIFY(git(agent, {"push", "-q", "origin", "main"}));
            QVERIFY(sync.syncBlocking());
        };
        change(kNow, [](QJsonObject &o) { o["title"] = "Call the plumber about the tap"; });
        QCOMPARE(titleOf(plain), QString("Call the plumber about the tap"));

        change(kNow + 5, [](QJsonObject &o) {
            o["completed"] = true;
            o["completed_at"] = kNow + 5;
        });
        bool completed = false;
        for (const auto &t : store.tasks())
            if (t.uid == plain)
                completed = t.completed && t.completedAt.toSecsSinceEpoch() == kNow + 5;
        QVERIFY(completed);

        change(kNow + 10, [](QJsonObject &o) { o["deleted"] = true; });
        QCOMPARE(titleOf(plain), QString("<none>"));
    }

    // A writer may commit a task before its list and tag: the task keeps
    // naming them in the repository, and joins them when they arrive.
    void gitKeepsReferencesToRecordsNotCommittedYet() {
        QString err;
        QTemporaryDir dir;
        const QString bare = dir.filePath("remote.git");
        QVERIFY(git(dir.path(), {"init", "--bare", "-q", "-b", "main", bare}));
        const QString agent = dir.filePath("agent");
        QVERIFY(git(dir.path(), {"clone", "-q", bare, agent}));
        QVERIFY(git(agent, {"checkout", "-q", "-B", "main"}));
        const QString task = "70000000-0000-4000-8000-0000000000A1";
        const QString list = "20000000-0000-4000-8000-0000000000A1";
        const QString tag = "50000000-0000-4000-8000-0000000000A1";
        auto commit = [&](const QString &message) {
            QVERIFY(git(agent, {"add", "-A"}));
            QVERIFY(git(agent, {"commit", "-q", "-m", message}));
            QVERIFY(git(agent, {"push", "-q", "origin", "main"}));
        };
        writeFile(agent + "/tasks/" + task + ".json",
                  QJsonDocument(QJsonObject{{"title", "Early bird"}, {"list_uid", list}, {"tag_uids", QJsonArray{tag}}, {"modified", kNow}})
                      .toJson(QJsonDocument::Indented));
        commit("Add a task");

        Store store;
        QVERIFY2(store.open(dir.filePath("fairfax.db"), &err), qPrintable(err));
        TaskList home;
        home.id = store.nextId("lists");
        home.name = "Home";
        store.begin("list");
        store.putList(home);
        store.commit();
        GitSync sync(&store, dir.filePath("wc"));
        sync.setRemote(bare);
        QTRY_VERIFY_WITH_TIMEOUT(!sync.busy(), 30000);

        auto theTask = [&] {
            for (const auto &t : store.tasks())
                if (t.uid == task)
                    return t;
            return Task();
        };
        auto fileUids = [&](const char *key) {
            const QJsonObject o = parse(readFile(sync.directory() + "/tasks/" + task + ".json"));
            QStringList out;
            if (o.value(key).isArray())
                for (const auto &v : o.value(key).toArray())
                    out << v.toString();
            else
                out << o.value(key).toString();
            return out;
        };
        QCOMPARE(theTask().title, QString("Early bird"));
        QCOMPARE(theTask().listId, home.id);
        QVERIFY(theTask().tagIds.isEmpty());
        QCOMPARE(fileUids("list_uid"), QStringList{list});
        QCOMPARE(fileUids("tag_uids"), QStringList{tag});
        QVERIFY(sync.syncBlocking());
        QCOMPARE(fileUids("list_uid"), QStringList{list});
        QCOMPARE(fileUids("tag_uids"), QStringList{tag});

        QVERIFY(git(agent, {"pull", "-q", "--ff-only", "origin", "main"}));
        writeFile(agent + "/lists/" + list + ".json",
                  QJsonDocument(QJsonObject{{"name", "Projects"}, {"modified", kNow}}).toJson(QJsonDocument::Indented));
        writeFile(agent + "/tags/" + tag + ".json",
                  QJsonDocument(QJsonObject{{"name", "errand"}, {"modified", kNow}}).toJson(QJsonDocument::Indented));
        commit("Add the list and the tag");
        QVERIFY(sync.syncBlocking());
        QCOMPARE(store.list(theTask().listId).uid, list);
        QCOMPARE(theTask().tagIds.size(), 1);
        QCOMPARE(store.tag(theTask().tagIds.first()).uid, tag);
        QCOMPARE(fileUids("list_uid"), QStringList{list});
        QCOMPARE(fileUids("tag_uids"), QStringList{tag});
    }
};

REGISTER_TEST(SyncFormatTest)
#include "test_syncformat.moc"
