#include "keyring.h"

#include <QDir>
#include <QHash>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#include <string>
#include <windows.h>
#include <wincred.h>
#endif

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
// Where secrets lived before they moved to the Credential Manager; read once
// to migrate, then removed.
QString secretFile(const QString &service, const QString &account) {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/secrets/" + service + "-" + account;
}

// A generic credential holds at most CRED_MAX_CREDENTIAL_BLOB_SIZE (2560)
// bytes, so a longer secret (an OAuth token set) is split across "name",
// "name#2", "name#3" and so on.
constexpr int ChunkBytes = 2048;

std::wstring targetName(const QString &service, const QString &account, int part) {
    QString name = service + QLatin1Char(':') + account;
    if (part > 1)
        name += QLatin1Char('#') + QString::number(part);
    return name.toStdWString();
}

bool writePart(const std::wstring &target, const QString &account, const QByteArray &bytes) {
    const std::wstring user = account.toStdWString();
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = const_cast<LPWSTR>(target.c_str());
    c.UserName = const_cast<LPWSTR>(user.c_str());
    c.CredentialBlobSize = DWORD(bytes.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(bytes.constData()));
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;  // This user, this machine; never roams.
    return CredWriteW(&c, 0) == TRUE;
}

bool readPart(const std::wstring &target, QByteArray *out) {
    PCREDENTIALW c = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &c))
        return false;
    *out = QByteArray(reinterpret_cast<const char *>(c->CredentialBlob), int(c->CredentialBlobSize));
    CredFree(c);
    return true;
}

// Removes part `from` and every later one.
void deleteParts(const QString &service, const QString &account, int from) {
    for (int part = from; CredDeleteW(targetName(service, account, part).c_str(), CRED_TYPE_GENERIC, 0); ++part) {
    }
}

bool storeCredential(const QString &service, const QString &account, const QString &secret) {
    const QByteArray bytes = secret.toUtf8();
    int part = 1;
    for (qsizetype at = 0; at == 0 || at < bytes.size(); at += ChunkBytes, ++part)
        if (!writePart(targetName(service, account, part), account, bytes.mid(at, ChunkBytes)))
            return false;
    deleteParts(service, account, part);  // Leftovers from a longer secret stored before.
    return true;
}

bool lookupCredential(const QString &service, const QString &account, QString *secret) {
    QByteArray all, chunk;
    if (!readPart(targetName(service, account, 1), &all))
        return false;
    for (int part = 2; readPart(targetName(service, account, part), &chunk); ++part)
        all += chunk;
    *secret = QString::fromUtf8(all);
    return true;
}
#endif

bool memoryMode = false;
QHash<QString, QString> &memory() {
    static QHash<QString, QString> secrets;
    return secrets;
}
bool useMemory() { return memoryMode || qgetenv("FAIRFAX_KEYRING") == "memory"; }
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
    if (!storeCredential(service, account, secret)) {
        if (error) *error = "Couldn't store the secret in the Windows Credential Manager.";
        return false;
    }
    QFile::remove(secretFile(service, account));  // Never leave an old copy behind.
    return true;
#else
    const Result r = run("secret-tool", {"store", "--label=Fairfax " + account, "service", service, "account", account},
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
    QString secret;
    if (lookupCredential(service, account, &secret))
        return secret;
    // A secret saved by an older version as a file: move it into the
    // Credential Manager, and remove the file once that worked.
    QFile f(secretFile(service, account));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    secret = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    if (storeCredential(service, account, secret))
        QFile::remove(secretFile(service, account));
    return secret;
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
    deleteParts(service, account, 1);
    QFile::remove(secretFile(service, account));
#else
    run("secret-tool", {"clear", "service", service, "account", account});
#endif
}

}  // namespace Keyring
