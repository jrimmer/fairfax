#pragma once

// Secrets (the Todoist token) kept outside the database, in each
// platform's own store: libsecret's secret-tool on Linux, the Keychain via
// `security` on macOS. Windows has no command-line credential reader, so
// there the secret goes in a file only the user can read, under the app's
// config folder.

#include <QString>

namespace Keyring {

bool store(const QString &service, const QString &account, const QString &secret, QString *error = nullptr);
QString lookup(const QString &service, const QString &account);
void clear(const QString &service, const QString &account);

// Keeps secrets in memory only, touching neither the keyring nor any file.
// The tests turn it on so they never read or write the real one; setting
// PRECRIME_KEYRING=memory does the same for screenshots and demo runs.
void useMemoryStore(bool on);

}  // namespace Keyring
