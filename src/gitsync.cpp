#include "gitsync.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSysInfo>
#include <memory>

#include "store.h"

// ---------------------------------------------------------------------------

namespace {

using GitDone = std::function<void(int code, const QString &out, const QString &err)>;

QString lastLine(const QString &text) {
    const auto lines = text.split('\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

void runGit(QObject *owner, const QString &dir, const QStringList &args, GitDone done) {
    auto *p = new QProcess(owner);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GIT_TERMINAL_PROMPT", "0");
    env.insert("GIT_SSH_COMMAND", "ssh -o BatchMode=yes");
    env.insert("LC_ALL", "C");
    p->setProcessEnvironment(env);
    if (!dir.isEmpty())
        p->setWorkingDirectory(dir);
    QObject::connect(p, &QProcess::errorOccurred, owner, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            done(-1, {}, "git could not be started");
        }
    });
    QObject::connect(p, &QProcess::finished, owner, [p, done](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        done(st == QProcess::NormalExit ? code : -1, out, err);
    });
    p->start("git", args);
}

}  // namespace

GitSync::GitSync(Store *store, const QString &directory, QObject *parent)
    : QObject(parent), m_store(store), m_directory(directory) {
    if (m_directory.isEmpty())
        m_directory = QFileInfo(store->path()).absolutePath() + "/sync";
    m_status = configured() ? "Not synced yet" : "Not set up";

    m_periodic.setInterval(5 * 60 * 1000);
    connect(&m_periodic, &QTimer::timeout, this, [this] {
        if (SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(20 * 1000);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (SyncTree::autoSyncEnabled(*m_store))
            syncNow();
    });
    // The sync's own writes don't count; an edit made while a sync runs
    // queues another sync right after it.
    connect(store, &Store::changed, this, [this] {
        if (!configured() || m_applying || !SyncTree::autoSyncEnabled(*m_store))
            return;
        if (m_busy)
            m_again = true;
        else
            m_debounce.start();
    });
    if (configured())
        m_periodic.start();
}

bool GitSync::configured() const { return !remote().isEmpty(); }
QString GitSync::remote() const { return m_store->setting("git.remote").toString(); }
QDateTime GitSync::lastSync() const {
    return QDateTime::fromString(m_store->setting("git.lastSync").toString(), Qt::ISODate);
}

void GitSync::finish(bool ok, const QString &message) {
    m_busy = false;
    if (ok) {
        m_store->setSetting("git.lastSync", QDateTime::currentDateTime().toString(Qt::ISODate));
        m_status = "Synced";
    } else {
        m_status = message.isEmpty() ? QString("Sync failed") : message;
    }
    setProperty("lastOk", ok);
    emit stateChanged();
    if (ok)
        emit synced(property("changedLocally").toInt());
    if (m_again) {
        m_again = false;
        syncNow();
    }
}

void GitSync::syncNow() {
    if (!configured())
        return;
    if (m_busy) {
        m_again = true;
        return;
    }
    m_debounce.stop();
    m_busy = true;
    m_status = "Syncing…";
    emit stateChanged();
    runCycle([this](bool ok, const QString &msg) { finish(ok, msg); });
}

bool GitSync::syncBlocking(int timeoutMs) {
    if (!configured())
        return false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    connect(this, &GitSync::stateChanged, &loop, [&] {
        if (!m_busy && !m_again)
            loop.quit();
    });
    timeout.start(timeoutMs);
    setProperty("lastOk", false);
    syncNow();
    if (m_busy)
        loop.exec();
    return !timedOut && property("lastOk").toBool();
}

void GitSync::disconnectRemote() {
    m_periodic.stop();
    m_debounce.stop();
    m_store->setSetting("git.remote", QString());
    m_store->setSetting("git.lastCommit", QString());
    m_store->setSetting("git.lastSync", QString());
    m_store->clearSyncLinks("git");
    m_status = "Not set up";
    emit stateChanged();
}

void GitSync::setRemote(const QString &url) {
    if (m_busy)
        return;
    m_busy = true;
    m_status = "Connecting…";
    emit stateChanged();

    auto fail = [this](const QString &msg) { finish(false, msg); };
    auto adopt = [this, url] {
        m_store->setSetting("git.remote", url);
        m_store->setSetting("git.lastCommit", QString());
        m_periodic.start();
        // Run the first cycle through the normal path.
        m_busy = false;
        syncNow();
    };
    auto initRepo = [this, url, fail, adopt] {
        QDir().mkpath(m_directory);
        runGit(this, m_directory, {"init", "-b", "main"}, [=, this](int c, const QString &, const QString &e) {
            if (c)
                return fail(lastLine(e));
            runGit(this, m_directory, {"remote", "add", "origin", url}, [=, this](int c2, const QString &, const QString &e2) {
                if (c2)
                    return fail(lastLine(e2));
                adopt();
            });
        });
    };

    if (QFileInfo::exists(m_directory + "/.git")) {
        runGit(this, m_directory, {"remote", "set-url", "origin", url}, [=, this](int c, const QString &, const QString &) {
            if (!c)
                return adopt();
            runGit(this, m_directory, {"remote", "add", "origin", url}, [=, this](int c2, const QString &, const QString &e2) {
                if (c2)
                    return fail(lastLine(e2));
                adopt();
            });
        });
        return;
    }
    const bool occupied = QDir(m_directory).exists() && !QDir(m_directory).isEmpty();
    if (occupied) {
        initRepo();
        return;
    }
    QDir().mkpath(QFileInfo(m_directory).absolutePath());
    runGit(this, QFileInfo(m_directory).absolutePath(), {"clone", "-o", "origin", url, m_directory},
           [=, this](int c, const QString &, const QString &e) {
               if (!c)
                   return adopt();
               if (e.contains("empty", Qt::CaseInsensitive))
                   return initRepo();
               fail(lastLine(e));
           });
}

namespace {
struct Ctx {
    int attempt = 0;
    QString branch = "main";
    bool remoteHasBranch = false;
    QStringList changed, removed;
    int total = 0;
    std::function<void(bool, const QString &)> done;
    std::function<void()> fetch, findBranch, diff, apply, add, identity, commit, push, finishOk;
    QStringList identityArgs;
    bool ended = false;
};
}  // namespace

void GitSync::runCycle(std::function<void(bool ok, const QString &message)> done) {
    auto c = std::make_shared<Ctx>();
    c->done = std::move(done);
    const QString dir = m_directory;

    auto end = [this, c](bool ok, const QString &msg) {
        if (c->ended)
            return;
        c->ended = true;
        QTimer::singleShot(0, this, [this, c, ok, msg] {
            setProperty("changedLocally", c->total);
            auto done = c->done;
            c->fetch = c->findBranch = c->diff = c->apply = c->add = c->identity = c->commit = c->push =
                c->finishOk = nullptr;
            done(ok, msg);
        });
    };
    auto fail = [end](const QString &msg) { end(false, msg); };
    auto git = [this, dir](const QStringList &args, GitDone cb) { runGit(this, dir, args, cb); };

    c->fetch = [=, this] {
        m_status = "Fetching…";
        emit stateChanged();
        git({"fetch", "origin"}, [=, this](int code, const QString &, const QString &err) {
            if (code)
                return fail(lastLine(err));
            c->findBranch();
        });
    };

    c->findBranch = [=, this] {
        git({"for-each-ref", "--format=%(refname)", "refs/remotes/origin"},
            [=, this](int code, const QString &out, const QString &err) {
                if (code)
                    return fail(lastLine(err));
                const QStringList refs = out.split('\n', Qt::SkipEmptyParts);
                const QString prefix = "refs/remotes/origin/";
                QStringList names;
                for (const auto &r : refs)
                    if (r.startsWith(prefix) && r != prefix + "HEAD")
                        names << r.mid(prefix.size());
                if (names.isEmpty()) {
                    c->remoteHasBranch = false;
                    c->branch = "main";
                    return c->add();
                }
                c->remoteHasBranch = true;
                auto choose = [=, this](const QString &head) {
                    if (!head.isEmpty() && names.contains(head))
                        c->branch = head;
                    else if (names.contains("main"))
                        c->branch = "main";
                    else if (names.contains("master"))
                        c->branch = "master";
                    else
                        c->branch = names.first();
                    c->diff();
                };
                if (refs.contains(prefix + "HEAD")) {
                    git({"symbolic-ref", "--short", "refs/remotes/origin/HEAD"},
                        [=, this](int hc, const QString &hout, const QString &) {
                            QString head = hout.trimmed();
                            if (head.startsWith("origin/"))
                                head = head.mid(7);
                            choose(hc ? QString() : head);
                        });
                } else {
                    choose({});
                }
            });
    };

    c->diff = [=, this] {
        const QString last = m_store->setting("git.lastCommit").toString();
        const QString target = "origin/" + c->branch;
        auto all = [=, this] {
            git({"ls-tree", "-r", "--name-only", target}, [=, this](int code, const QString &out, const QString &err) {
                if (code)
                    return fail(lastLine(err));
                c->changed = out.split('\n', Qt::SkipEmptyParts);
                c->removed.clear();
                c->apply();
            });
        };
        if (last.isEmpty())
            return all();
        git({"diff", "--name-status", "--no-renames", last, target},
            [=, this](int code, const QString &out, const QString &) {
                if (code)
                    return all();  // lastCommit unknown to this clone
                c->changed.clear();
                c->removed.clear();
                for (const auto &line : out.split('\n', Qt::SkipEmptyParts)) {
                    const int tab = line.indexOf('\t');
                    if (tab < 1)
                        continue;
                    (line[0] == 'D' ? c->removed : c->changed) << line.mid(tab + 1);
                }
                c->apply();
            });
    };

    c->apply = [=, this] {
        m_status = "Merging…";
        emit stateChanged();
        git({"reset", "--hard", "origin/" + c->branch}, [=, this](int code, const QString &, const QString &err) {
            if (code)
                return fail(lastLine(err));
            QString error;
            m_applying = true;
            c->total += GitSyncFiles::readTree(*m_store, dir, c->changed, c->removed, &error);
            m_applying = false;
            c->add();
        });
    };

    c->add = [=, this] {
        QString error;
        if (!GitSyncFiles::writeTree(*m_store, dir, QDateTime::currentDateTime(), &error))
            return fail(error);
        git({"add", "-A"}, [=, this](int code, const QString &, const QString &err) {
            if (code)
                return fail(lastLine(err));
            git({"diff", "--cached", "--quiet"}, [=, this](int dc, const QString &, const QString &derr) {
                if (dc == 0)
                    return c->push();  // nothing staged
                if (dc != 1)
                    return fail(lastLine(derr));
                c->identity();
            });
        });
    };

    c->identity = [=, this] {
        git({"config", "user.name"}, [=, this](int nc, const QString &nout, const QString &) {
            git({"config", "user.email"}, [=, this](int ec, const QString &eout, const QString &) {
                c->identityArgs.clear();
                if (nc || nout.trimmed().isEmpty())
                    c->identityArgs << "-c" << "user.name=Fairfax";
                if (ec || eout.trimmed().isEmpty())
                    c->identityArgs << "-c" << "user.email=fairfax@localhost";
                c->commit();
            });
        });
    };

    c->commit = [=, this] {
        QStringList args = c->identityArgs;
        args << "commit" << "-q" << "-m" << "Sync from " + QSysInfo::machineHostName();
        git(args, [=, this](int code, const QString &out, const QString &err) {
            if (code)
                return fail(lastLine(err.isEmpty() ? out : err));
            c->push();
        });
    };

    c->push = [=, this] {
        m_status = "Pushing…";
        emit stateChanged();
        git({"rev-parse", "--verify", "-q", "HEAD"}, [=, this](int hc, const QString &, const QString &) {
            if (hc)
                return c->finishOk();  // nothing committed yet, nothing to push
            git({"push", "origin", "HEAD:" + c->branch}, [=, this](int code, const QString &, const QString &err) {
                if (code == 0)
                    return c->finishOk();
                const bool rejected = err.contains("rejected") || err.contains("non-fast-forward") ||
                                      err.contains("fetch first") || err.contains("failed to update ref");
                if (rejected && ++c->attempt < 3)
                    return c->fetch();
                fail(lastLine(err));
            });
        });
    };

    c->finishOk = [=, this] {
        git({"rev-parse", "HEAD"}, [=, this](int code, const QString &out, const QString &) {
            if (!code)
                m_store->setSetting("git.lastCommit", out.trimmed());
            end(true, {});
        });
    };

    c->fetch();
}
