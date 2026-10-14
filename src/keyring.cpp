#include "keyring.h"

#include <QDir>
#include <QHash>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace {

struct Result {
    bool started = false;
    int code = -1;
    QString out;
};

Result run(const QString &program, const QStringList &args, const QByteArray &input = {}) {
    Result r;
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(3000))
        return r;
    r.started = true;
    if (!input.isEmpty())
        p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(15000)) {
        p.kill();
        return r;
    }
    r.code = p.exitCode();
    r.out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    return r;
}

#if defined(Q_OS_WIN)
QString secretFile(const QString &service, const QString &account) {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/secrets/" + service + "-" + account;
}
#endif

bool memoryMode = false;
QHash<QString, QString> &memory() {
    static QHash<QString, QString> secrets;
    return secrets;
}
bool useMemory() { return memoryMode || qgetenv("PRECRIME_KEYRING") == "memory"; }
QString memoryKey(const QString &service, const QString &account) { return service + '\n' + account; }

}  // namespace

namespace Keyring {

void useMemoryStore(bool on) { memoryMode = on; }

bool store(const QString &service, const QString &account, const QString &secret, QString *error) {
    if (useMemory()) {
        memory().insert(memoryKey(service, account), secret);
        return true;
    }
#if defined(Q_OS_MACOS)
    const Result r = run("security", {"add-generic-password", "-U", "-s", service, "-a", account, "-w", secret});
    if (!r.started || r.code != 0) {
        if (error) *error = "Couldn't store the secret in the Keychain.";
        return false;
    }
    return true;
#elif defined(Q_OS_WIN)
    const QString path = secretFile(service, account);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = "Couldn't write " + path;
        return false;
    }
    f.write(secret.toUtf8());
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
#else
    const Result r = run("secret-tool", {"store", "--label=Precrime " + account, "service", service, "account", account},
                         secret.toUtf8());
    if (!r.started) {
        if (error) *error = "secret-tool is not installed; install libsecret to store the token.";
        return false;
    }
    if (r.code != 0) {
        if (error) *error = "Couldn't store the token in the keyring.";
        return false;
    }
    return true;
#endif
}

QString lookup(const QString &service, const QString &account) {
    if (useMemory())
        return memory().value(memoryKey(service, account));
#if defined(Q_OS_MACOS)
    const Result r = run("security", {"find-generic-password", "-s", service, "-a", account, "-w"});
    return r.code == 0 ? r.out : QString();
#elif defined(Q_OS_WIN)
    QFile f(secretFile(service, account));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
#else
    const Result r = run("secret-tool", {"lookup", "service", service, "account", account});
    return r.code == 0 ? r.out : QString();
#endif
}

void clear(const QString &service, const QString &account) {
    if (useMemory()) {
        memory().remove(memoryKey(service, account));
        return;
    }
#if defined(Q_OS_MACOS)
    run("security", {"delete-generic-password", "-s", service, "-a", account});
#elif defined(Q_OS_WIN)
    QFile::remove(secretFile(service, account));
#else
    run("secret-tool", {"clear", "service", service, "account", account});
#endif
}

}  // namespace Keyring
