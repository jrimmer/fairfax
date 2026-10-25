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
        // A complete sample must also be laid out exactly as Fairfax writes
        // it, so a writer copying the samples' style is never reformatted.
        if (got == sample && back.trimmed() != it.value().trimmed())
            problems << path + ": Fairfax writes this record in a different layout than the sample shows";
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

    // The base is T0, a Fairfax edit comes at T0 + 5 and another writer's at
    // T0 + 10 unless a test says otherwise, so which side is newer is never
    // a matter of timing.
    static constexpr qint64 T0 = 1700000000;

    // Fairfax (a store syncing through git) and another writer's clone of the
    // same repository, sharing one list and one task "Base".
    struct Rig {
        QTemporaryDir dir;
        QString bare, agent, list = "20000000-0000-4000-8000-0000000000C1", task = "70000000-0000-4000-8000-0000000000C1";
        Store store;
        std::unique_ptr<GitSync> sync;
        qint64 listId = 0;

        QString taskFile() const { return agent + "/tasks/" + task + ".json"; }
    };

    static QJsonObject baseTask(const QString &list, const QString &uid) {
        return {{"uid", uid}, {"title", "Base"}, {"notes", "base notes"}, {"list_uid", list}, {"created", T0}, {"modified", T0}};
    }

    // `synced` leaves Fairfax synced at T0; otherwise the repository holds the
    // task but Fairfax has never seen it.
    std::unique_ptr<Rig> rig(bool synced = true) {
        auto r = std::make_unique<Rig>();
        [&] { QVERIFY(r->dir.isValid()); }();
        r->bare = r->dir.filePath("remote.git");
        r->agent = r->dir.filePath("agent");
        [&] {
            QVERIFY(git(r->dir.path(), {"init", "--bare", "-q", "-b", "main", r->bare}));
            QVERIFY(git(r->dir.path(), {"clone", "-q", r->bare, r->agent}));
            QVERIFY(git(r->agent, {"checkout", "-q", "-B", "main"}));
        }();
        writeFile(r->agent + "/lists/" + r->list + ".json",
                  QJsonDocument(QJsonObject{{"name", "L"}, {"modified", T0}}).toJson(QJsonDocument::Indented));
        writeFile(r->taskFile(), QJsonDocument(baseTask(r->list, r->task)).toJson(QJsonDocument::Indented));
        [&] {
            QVERIFY(git(r->agent, {"add", "-A"}));
            QVERIFY(git(r->agent, {"commit", "-q", "-m", "Base"}));
            QVERIFY(git(r->agent, {"push", "-q", "origin", "main"}));
        }();
        QString err;
        [&] { QVERIFY2(r->store.open(r->dir.filePath("fairfax.db"), &err), qPrintable(err)); }();
        if (!synced) {
            // Fairfax already has the task under the same uid, from somewhere else.
            TaskList l;
            l.id = r->store.nextId("lists");
            l.uid = r->list;
            l.name = "L";
            l.modified = QDateTime::fromSecsSinceEpoch(T0);
            Task t;
            t.id = r->store.nextId("tasks");
            t.uid = r->task;
            t.listId = l.id;
            t.title = "Base";
            t.notes = "base notes";
            t.modified = QDateTime::fromSecsSinceEpoch(T0);
            r->store.begin("seed", false, true);
            r->store.putList(l);
            r->store.putTask(t);
            r->store.commit();
            r->listId = l.id;
        }
        r->sync = std::make_unique<GitSync>(&r->store, r->dir.filePath("wc"));
        if (synced) {
            r->sync->setRemote(r->bare);
            [&] { QTRY_VERIFY_WITH_TIMEOUT(!r->sync->busy(), 30000); }();
            for (const auto &t : r->store.tasks())
                if (t.uid == r->task)
                    return r;
            [&] { QFAIL("the task did not arrive"); }();
        }
        return r;
    }

    static Task fairfaxTask(Rig &r) {
        for (const auto &t : r.store.tasks())
            if (t.uid == r.task)
                return t;
        return Task();
    }

    static void editFairfax(Rig &r, const std::function<void(Task &)> &edit, qint64 modified) {
        Task t = fairfaxTask(r);
        edit(t);
        t.modified = QDateTime::fromSecsSinceEpoch(modified);
        r.store.begin("edit", false, true);
        r.store.putTask(t);
        r.store.commit();
    }

    // Another writer: start from the newest files, change the record, push.
    static void editAgent(Rig &r, const std::function<void(QJsonObject &)> &edit, qint64 modified) {
        [&] {
            QVERIFY(git(r.agent, {"fetch", "-q", "origin"}));
            QVERIFY(git(r.agent, {"reset", "-q", "--hard", "origin/main"}));
        }();
        QJsonObject o = parse(readFile(r.taskFile()));
        edit(o);
        o["modified"] = modified;
        writeFile(r.taskFile(), QJsonDocument(o).toJson(QJsonDocument::Indented));
        [&] {
            QVERIFY(git(r.agent, {"commit", "-q", "-am", "Change task"}));
            QVERIFY(git(r.agent, {"push", "-q", "origin", "main"}));
        }();
    }

    // The task file as the repository now holds it.
    static QJsonObject repoTask(Rig &r) {
        git(r.agent, {"fetch", "-q", "origin"});
        git(r.agent, {"reset", "-q", "--hard", "origin/main"});
        return parse(readFile(r.taskFile()));
    }

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

    void gitCompletionRaceKeepsTheNotesEdit_data() {
        QTest::addColumn<qint64>("localAt");
        QTest::addColumn<qint64>("remoteAt");
        QTest::newRow("remote newer") << T0 + 5 << T0 + 10;
        QTest::newRow("remote older") << T0 + 10 << T0 + 5;
    }
    void gitCompletionRaceKeepsTheNotesEdit() {
        QFETCH(qint64, localAt);
        QFETCH(qint64, remoteAt);
        auto r = rig();
        QVERIFY(r);
        editFairfax(*r, [&](Task &t) { t.completed = true; t.completedAt = QDateTime::fromSecsSinceEpoch(localAt); }, localAt);
        editAgent(*r, [](QJsonObject &o) { o["notes"] = "edited by another writer"; }, remoteAt);
        QVERIFY(r->sync->syncBlocking());
        const Task t = fairfaxTask(*r);
        QVERIFY(t.completed);
        QCOMPARE(t.notes, QString("edited by another writer"));
        QCOMPARE(t.modified.toSecsSinceEpoch(), qMax(localAt, remoteAt));
        const QJsonObject o = repoTask(*r);
        QVERIFY(o.value("completed").toBool());
        QCOMPARE(o.value("notes").toString(), QString("edited by another writer"));
    }

    void gitEditsToDifferentFieldsAreBothKept() {
        auto r = rig();
        QVERIFY(r);
        editFairfax(*r, [](Task &t) { t.title = "Local title"; t.starred = true; }, T0 + 5);
        editAgent(*r, [](QJsonObject &o) { o["priority"] = 3; }, T0 + 10);
        QVERIFY(r->sync->syncBlocking());
        const Task t = fairfaxTask(*r);
        QCOMPARE(t.title, QString("Local title"));
        QVERIFY(t.starred);
        QCOMPARE(t.priority, 3);
        QCOMPARE(repoTask(*r).value("title").toString(), QString("Local title"));
        QCOMPARE(repoTask(*r).value("priority").toInt(), 3);
    }

    // `abandoned` is one more field: abandoning here and a title edit there
    // are both kept, and the file carries completed and abandoned together.
    void gitAbandonedMergesFieldByField() {
        auto r = rig();
        QVERIFY(r);
        editFairfax(*r, [](Task &t) { t.completed = true; t.abandoned = true; t.completedAt = QDateTime::fromSecsSinceEpoch(T0 + 5); }, T0 + 5);
        editAgent(*r, [](QJsonObject &o) { o["title"] = "Renamed elsewhere"; }, T0 + 10);
        QVERIFY(r->sync->syncBlocking());
        const Task t = fairfaxTask(*r);
        QVERIFY(t.completed);
        QVERIFY(t.abandoned);
        QCOMPARE(t.title, QString("Renamed elsewhere"));
        const QJsonObject o = repoTask(*r);
        QVERIFY(o.value("completed").toBool());
        QVERIFY(o.value("abandoned").toBool());
        QCOMPARE(o.value("title").toString(), QString("Renamed elsewhere"));
    }

    // Another writer abandons, and Fairfax edits the notes meanwhile.
    void gitAbandonedByAnotherWriterArrives() {
        auto r = rig();
        QVERIFY(r);
        editFairfax(*r, [](Task &t) { t.notes = "my notes"; }, T0 + 5);
        editAgent(*r, [](QJsonObject &o) { o["completed"] = true; o["completed_at"] = T0 + 10; o["abandoned"] = true; }, T0 + 10);
        QVERIFY(r->sync->syncBlocking());
        const Task t = fairfaxTask(*r);
        QVERIFY(t.completed);
        QVERIFY(t.abandoned);
        QCOMPARE(t.notes, QString("my notes"));
        QVERIFY(repoTask(*r).value("abandoned").toBool());
    }

    // A writer that knows nothing of `abandoned` completes a task: it is done.
    void gitReaderWithoutAbandonedSeesAClosedTask() {
        auto r = rig();
        QVERIFY(r);
        editAgent(*r, [](QJsonObject &o) { o["completed"] = true; o["completed_at"] = T0 + 10; }, T0 + 10);
        QVERIFY(r->sync->syncBlocking());
        const Task t = fairfaxTask(*r);
        QVERIFY(t.completed);
        QVERIFY(!t.abandoned);
    }

    void gitTheNewerSideWinsAFieldBothChanged_data() {
        QTest::addColumn<qint64>("localAt");
        QTest::addColumn<qint64>("remoteAt");
        QTest::addColumn<QString>("title");
        QTest::newRow("remote newer") << T0 + 5 << T0 + 10 << "Remote title";
        QTest::newRow("local newer") << T0 + 10 << T0 + 5 << "Local title";
    }
    void gitTheNewerSideWinsAFieldBothChanged() {
        QFETCH(qint64, localAt);
        QFETCH(qint64, remoteAt);
        QFETCH(QString, title);
        auto r = rig();
        QVERIFY(r);
        editFairfax(*r, [](Task &t) { t.title = "Local title"; t.notes = "local notes"; }, localAt);
        editAgent(*r, [](QJsonObject &o) { o["title"] = "Remote title"; }, remoteAt);
        QVERIFY(r->sync->syncBlocking());
        QCOMPARE(fairfaxTask(*r).title, title);
        QCOMPARE(fairfaxTask(*r).notes, QString("local notes"));
        QCOMPARE(repoTask(*r).value("title").toString(), title);
    }

    void gitTagSetsMergeAddsAndRemoves() {
        auto r = rig();
        QVERIFY(r);
        const QString tagA = "50000000-0000-4000-8000-0000000000C1", tagB = "50000000-0000-4000-8000-0000000000C2",
                      tagC = "50000000-0000-4000-8000-0000000000C3";
        // Three tags, and the task carries A and B.
        QVERIFY(git(r->agent, {"fetch", "-q", "origin"}));
        QVERIFY(git(r->agent, {"reset", "-q", "--hard", "origin/main"}));
        for (const auto &tag : {tagA, tagB, tagC})
            writeFile(r->agent + "/tags/" + tag + ".json",
                      QJsonDocument(QJsonObject{{"name", tag.right(2)}, {"modified", T0}}).toJson(QJsonDocument::Indented));
        QJsonObject withTags = parse(readFile(r->taskFile()));
        withTags["tag_uids"] = QJsonArray{tagA, tagB};
        withTags["modified"] = T0 + 2;
        writeFile(r->taskFile(), QJsonDocument(withTags).toJson(QJsonDocument::Indented));
        QVERIFY(git(r->agent, {"add", "-A"}));
        QVERIFY(git(r->agent, {"commit", "-q", "-m", "Tags"}));
        QVERIFY(git(r->agent, {"push", "-q", "origin", "main"}));
        QVERIFY(r->sync->syncBlocking());
        QCOMPARE(fairfaxTask(*r).tagIds.size(), 2);
        // Fairfax adds C; the other writer removes A.
        QList<qint64> all;
        for (auto it = r->store.tags().cbegin(); it != r->store.tags().cend(); ++it)
            all << it->id;
        editFairfax(*r, [&](Task &t) { t.tagIds = all; }, T0 + 5);
        editAgent(*r, [&](QJsonObject &o) { o["tag_uids"] = QJsonArray{tagB}; }, T0 + 10);
        QVERIFY(r->sync->syncBlocking());
        QSet<QString> got;
        for (qint64 id : fairfaxTask(*r).tagIds)
            got.insert(r->store.tag(id).uid);
        QCOMPARE(got, (QSet<QString>{tagB, tagC}));
        QSet<QString> inRepo;
        for (const auto &v : repoTask(*r).value("tag_uids").toArray())
            inRepo.insert(v.toString());
        QCOMPARE(inRepo, got);
    }

    void gitTombstoneAgainstEdit_data() {
        QTest::addColumn<bool>("fairfaxDeletes");
        QTest::addColumn<qint64>("deleteAt");
        QTest::addColumn<qint64>("editAt");
        QTest::addColumn<bool>("endsDeleted");
        QTest::newRow("remote delete newer than local edit") << false << T0 + 10 << T0 + 5 << true;
        QTest::newRow("remote delete older than local edit") << false << T0 + 5 << T0 + 10 << false;
        QTest::newRow("local delete newer than remote edit") << true << T0 + 10 << T0 + 5 << true;
        QTest::newRow("local delete older than remote edit") << true << T0 + 5 << T0 + 10 << false;
    }
    void gitTombstoneAgainstEdit() {
        QFETCH(bool, fairfaxDeletes);
        QFETCH(qint64, deleteAt);
        QFETCH(qint64, editAt);
        QFETCH(bool, endsDeleted);
        auto r = rig();
        QVERIFY(r);
        if (fairfaxDeletes) {
            editFairfax(*r, [](Task &t) { t.deleted = true; }, deleteAt);
            editAgent(*r, [](QJsonObject &o) { o["notes"] = "edited"; }, editAt);
        } else {
            editFairfax(*r, [](Task &t) { t.notes = "edited"; }, editAt);
            editAgent(*r, [](QJsonObject &o) { o["deleted"] = true; }, deleteAt);
        }
        QVERIFY(r->sync->syncBlocking());
        QCOMPARE(fairfaxTask(*r).deleted, endsDeleted);
        // (a tombstone this old is pruned from the files on write)
        QVERIFY(repoTask(*r).value("deleted").toBool() == endsDeleted || (endsDeleted && repoTask(*r).isEmpty()));
        if (!endsDeleted)
            QCOMPARE(fairfaxTask(*r).notes, QString("edited"));
    }

    void gitWithoutABaseTheWholeRecordRuleApplies() {
        auto r = rig(false);  // never synced: no commit to take a base from
        QVERIFY(r);
        editFairfax(*r, [](Task &t) { t.title = "Local title"; }, T0 + 5);
        editAgent(*r, [](QJsonObject &o) { o["priority"] = 3; }, T0 + 10);
        r->sync->setRemote(r->bare);
        QTRY_VERIFY_WITH_TIMEOUT(!r->sync->busy(), 30000);
        QCOMPARE(fairfaxTask(*r).priority, 3);
        QCOMPARE(fairfaxTask(*r).title, QString("Base"));
    }

    void gitFilesWithoutTheNewFieldsMerge() {
        auto r = rig();
        QVERIFY(r);
        editFairfax(*r, [](Task &t) { t.priority = Priority::High; }, T0 + 5);
        // A writer that leaves out every field it does not know.
        git(r->agent, {"fetch", "-q", "origin"});
        git(r->agent, {"reset", "-q", "--hard", "origin/main"});
        writeFile(r->taskFile(), QJsonDocument(QJsonObject{{"title", "Edited by a small tool"}, {"list_uid", r->list}, {"modified", T0 + 10}}).toJson(QJsonDocument::Indented));
        QVERIFY(git(r->agent, {"commit", "-q", "-am", "Small tool"}));
        QVERIFY(git(r->agent, {"push", "-q", "origin", "main"}));
        QVERIFY(r->sync->syncBlocking());
        const Task t = fairfaxTask(*r);
        QCOMPARE(t.title, QString("Edited by a small tool"));
        QCOMPARE(t.priority, int(Priority::High));
        QCOMPARE(t.notes, QString("base notes"));
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
