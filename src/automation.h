#pragma once

// Fairfax's automation layer, mirroring the reference app's 26-command Automation Suite.
// One command table serves three front ends: the `fairfax ctl` command line,
// an MCP server on stdio (for Claude Co-Work and other agents) and the
// fairfax:// URL scheme. Commands use the reference app's snake_case names and argument
// names, are strict about unknown keys, and every mutating command is exactly
// one store change, so the app's undo reverses it.

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QTextStream>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

#include "model.h"
#include "store.h"

class ViewBuilder;

class Automation {
public:
    explicit Automation(Store &store);
    ~Automation();

    // UI hooks. A headless process (`fairfax mcp`, `fairfax ctl`) leaves
    // them unset and the commands that need a window answer
    // automation_unavailable instead of pretending.
    std::function<void(const QString &key)> selectSource;  // "today", "list:3", "project:5", "group:2".
    std::function<void(qint64 taskId)> openTask;
    std::function<QList<qint64>()> selectedTaskIds;
    std::function<QString()> clipboardText;  // For (clipboard) in URL parameters.

    // Overrides the clock, for tests that need overdue and "today" to be stable.
    void setNow(const QDateTime &now) { m_now = now; }

    // Runs one command. Always returns {"ok": true, ...} or
    // {"ok": false, "error": {"code", "message"}}.
    QJsonObject run(const QString &command, const QJsonObject &args = {});

    static QStringList commandNames();
    // {"tools": [{name, description, inputSchema}, ...]}, the MCP tools/list result.
    static QJsonObject mcpTools();

    // `fairfax ctl` with the arguments after "ctl". Prints the pretty JSON
    // response to `out`; returns 0 when the command succeeded and 1 otherwise.
    int runCli(const QStringList &args, QTextStream &out, QTextStream &err);

    // fairfax://x-callback-url/<action>?...; see url-schemes in the reference app's manual.
    // Actions that need the window use the callbacks when set, and always
    // describe what they want in the result ("source", "task_id", "search",
    // "quickEntry") so the caller can act on it.
    QJsonObject handleUrl(const QUrl &url);
    static QString shareableLink(const Task &task);

    // Re-reads the database, for a process that shares it with the app.
    void reloadStore() { m_store.reload(); }

    struct Impl;  // Public only so the command table in the .cpp can name it.

private:
    Store &m_store;
    QDateTime m_now;
    std::unique_ptr<Impl> d;
};

// MCP over stdio: JSON-RPC 2.0, one message per line.
class McpServer {
public:
    explicit McpServer(Automation &automation, const QString &version = QStringLiteral("1.0"));

    // Blocks reading `in` until EOF, answering on `out`. Returns 0.
    int run(QTextStream &in, QTextStream &out);

    // Handles one parsed message; returns an empty object for notifications.
    QJsonObject handle(const QJsonObject &message);

    // The server shares the database with the running app, so it reloads
    // before each tool call to see what the app changed in the meantime.
    void setReloadBeforeCall(bool on) { m_reload = on; }

private:
    Automation &m_automation;
    QString m_version;
    bool m_reload = true;
};
