#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>

class DataStore;
class Task;
class Project;

// ─────────────────────────────────────────────────────────────────────────────
// clicore — UI-free helpers shared by the `opentodo` CLI (offline path) and the
// in-app CliBridge (live path). Everything here operates on a DataStore that is
// already loaded; nothing pulls in Quick/Widgets, so it links cleanly into a
// headless console tool.
//
// The JSON shapes produced here are the CLI's public contract — Claude Code and
// scripts parse them, so keep them stable (snake_case keys, like state.json).
// ─────────────────────────────────────────────────────────────────────────────
namespace clicore {

// One task → the CLI JSON shape (resolves the human project name too).
QJsonObject taskToJson(const Task *t, const DataStore *store);

// Decide which project a query is scoped to:
//   * projectName non-empty → case-insensitive name match (skips tombstones)
//   * else repoPath non-empty → the project whose linked_repo_path canonically
//     equals repoPath (this is how `opentodo list` auto-scopes to the repo the
//     command runs in)
//   * else → nullptr (caller treats as "every project")
// When non-null is returned via the repo branch, *matchedByRepo is set true.
Project *resolveProject(const DataStore *store,
                        const QString &repoPath,
                        const QString &projectName,
                        bool *matchedByRepo = nullptr);

// Active (or, with includeCompleted, all) non-deleted tasks for the scope,
// sorted priority ascending (p1 first) then by order.
QList<Task *> scopedTasks(const DataStore *store, const Project *project,
                          bool includeCompleted);

// ── offline mutators (app closed): mutate + stamp CRDT _meta so the change
//    wins LWW on the next sync. The CALLER is responsible for persisting
//    (FileManager::saveImmediate) afterwards. ────────────────────────────────

// Mark a task completed. Returns false (with *outError set) when the task is
// missing, already done, or RECURRING — recurrence advancing lives in
// TaskViewModel and must run in the live app, so we refuse it here rather than
// silently mis-advancing the schedule.
bool completeTaskOffline(DataStore *store, const QString &id, QString *outError);

// Create a task. `dueString` is parsed with DateParser when present.
// Returns the new task id, or empty string on failure.
QString addTaskOffline(DataStore *store, const QString &content,
                       const QString &projectId, int priority,
                       const QString &dueString, const QString &lang);

// Append a comment to a task. Returns the new comment id (empty on failure).
QString addCommentOffline(DataStore *store, const QString &taskId,
                          const QString &text);

// Execute a full CLI request object {cmd, ...} against `store` with OFFLINE
// semantics and return a reply object {ok, data|error}. The CLI client uses
// this directly when the app isn't running; CliBridge reuses it for the
// read-only commands (list/next/show) so the live path returns identical JSON.
//
// Recognised cmd values: list, next, show, done, add, comment.
// Writes (done/add/comment) mutate `store` — the caller must persist on ok.
QJsonObject handleRequest(DataStore *store, const QJsonObject &req);

} // namespace clicore
