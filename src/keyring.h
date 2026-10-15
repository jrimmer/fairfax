#pragma once

// Secrets (tokens and passwords) kept outside the database, in each
// platform's own store: libsecret's secret-tool on Linux, the Keychain via
// `security` on macOS, and the Windows Credential Manager (generic
// credentials named "service:account", local to this machine) on Windows.

#include <QString>

namespace Keyring {

bool store(const QString &service, const QString &account, const QString &secret, QString *error = nullptr);
QString lookup(const QString &service, const QString &account);
void clear(const QString &service, const QString &account);

// Keeps secrets in memory only, touching neither the keyring nor any file.
// The tests turn it on so they never read or write the real one; setting
// FAIRFAX_KEYRING=memory does the same for screenshots and demo runs.
void useMemoryStore(bool on);

}  // namespace Keyring
