# Todoist API: Implementation Reference for a Desktop Sync Client

Status: Todoist API v1 is current. The official reference is at https://developer.todoist.com/api/v1/ (Sync overview, Authorization, Projects, Sections, Sync commands). The v9 Sync reference at https://developer.todoist.com/sync/v9/ is legacy, and the v1 page has a "Migrating from v9" section. REST v2 is merged into v1. Items marked UNVERIFIED were not confirmed in an official source during research.

## 1. Authentication

- Base URL: `https://api.todoist.com/api/v1/` (source: v1 overview page).
- Header: `Authorization: Bearer <token>` on every request (source: v1 overview, Authorization section).
- Personal token: available from the Todoist integrations settings page (source: v1 overview, Authorization section). The exact menu path is UNVERIFIED.
- OAuth 2.0 for other users. Token endpoint: `https://api.todoist.com/oauth/access_token` (source: v1 overview). Scope names such as `data:read_write` are UNVERIFIED in the fetched text. Confirm them in the Authorization section before implementing.
- A personal token can be migrated to an OAuth access token via `/api/v1/access_tokens/migrate_personal_token` (source: v1 overview example paths).

## 2. Sync endpoint

- `POST https://api.todoist.com/api/v1/sync`
- Content type: `application/x-www-form-urlencoded`. Responses are JSON (source: v1 Sync overview).
- Request parameters (source: v1 Sync section):
  - `sync_token` (required): `*` for a full sync, or the token from the previous response for an incremental sync.
  - `resource_types` (required): a JSON array of strings. Use `["all"]` or specific types. A type prefixed with `-` is excluded. `reminders` and `reminders_location` must both be listed to fetch both reminder types.
  - `commands` (optional, for writes): a JSON array of command objects, executed in order.

### Incremental sync flow

1. Send `sync_token=*` with `resource_types=["all"]`. The response contains the full data set and a new `sync_token`. Persist the token.
2. For later reads and writes, send the stored token. The server returns only changed resources.
3. Check `full_sync` in each response. If the account is large, `full_sync_date_utc` may lag. If the gap is large, do an incremental sync from the token returned by the initial call.
4. `workspace_users` is not returned in a full sync. Load it separately (the REST endpoint is documented in the v1 reference) or request it in an incremental sync (source: v1 Sync section).

### Response shape

- Top level: `sync_token`, `full_sync`, `full_sync_date_utc`, plus resource arrays: `projects`, `items`, `sections`, `labels`, `filters`, `reminders`, `notes`, `project_notes`, `collaborators`, `workspaces`, `user_settings`, `completed_info`, `user_item_orders`, and others (source: v1 Sync section).
- Note: the tasks array key is `items`, not `tasks`.
- Writes: `sync_status` maps each command `uuid` to `"ok"` or an error object that includes `http_code`. `temp_id_mapping` maps each `temp_id` to the server ID (source: v1 Sync section).
- Errors: for a 400 command error, the docs say not to retry that command (source: v1 Sync section).

### Batching and rate limits

- Commands can be batched in one request. A third-party guide gives a limit of 100 commands per request (rollout.com, UNVERIFIED against official docs).
- Request limits are in a separate "Request limits" page (source: v1 Sync overview, which links to it). The fetched text was truncated, so the official quotas are UNVERIFIED. Third-party sources report figures such as 1,000 requests per 15 minutes, but these conflict and are unverified.
- Handle HTTP 429 with exponential backoff. The error body can include `retry_after` in seconds (source: v1 Sync status table and error_extra fields).

## 3. Resources

Field types are from the official Python SDK models (https://doist.github.io/todoist-api-python/models/), which mirror the v1 API. Official per-field tables were not fully readable, so treat these as strings unless noted.

### Projects

- `id` (string), `name`, `description`, `order` (int), `color` (string, name from the color list), `is_collapsed`, `is_shared`, `is_favorite`, `is_archived`, `can_assign_tasks`, `view_style` (`list` | `board` | `calendar`), `parent_id` (string or null), `is_inbox_project` (bool, present only on Inbox), `workspace_id`, `folder_id`, `created_at`, `updated_at`.
- Source: v1 Projects section (field list) and SDK models.
- The sync payload uses `child_order` for position (v1 Projects section). The SDK model names it `order`. Use the field name from the payload you receive.

### Sections

- `id`, `name`, `project_id`, `is_collapsed`, `order` (int), plus `section_order`, `order_key`, `is_archived`, `archived_at`, `is_deleted`, `added_at`, `updated_at`, `description` in the v1 Sync section (sources: v1 Sections section and SDK models).

### Tasks (`items`)

- `id` (string), `content`, `description`, `project_id`, `section_id` (string or null), `parent_id` (string or null), `labels` (array of label names, string), `priority` (int), `due` (object or null), `deadline` (object or null), `duration` (object or null), `is_collapsed`, `order` (int), `assignee_id`, `assigner_id`, `creator_id`, `completed_at` (date, optional), `created_at`, `updated_at`, `added_at`, `child_order`, `checked`, `is_deleted`. Some field names above differ between payload and SDK model; verify against a live response.
- Priority: the API stores 1–4 as an int. Which int maps to p1 (urgent) is UNVERIFIED in the fetched text. Todoist's UI convention is that the API's `4` is p1, but confirm before implementing.
- `due` object: `date` (date string for all-day; datetime string for timed), `string` (natural-language text), `lang` (default `en`), `is_recurring` (bool), `timezone` (string or null, used with fixed-timezone datetimes). Source: SDK models and v1 Due dates sections (headings only were readable; field names UNVERIFIED against the official table).
- `deadline` object: `date`, `lang`. Source: SDK models.
- `duration` object: `amount` (int), `unit` (`minute` or `day`). Source: SDK models.

### Labels

- `id` (string), `name`, `color` (string), `order` (int), `is_favorite` (bool). Source: SDK models. The v1 Labels field list was not readable; `item_order` is the sync-payload name and is UNVERIFIED.

## 4. Commands

Command object shape (source: v1 Sync section):

- `type`: command name (for example `item_add`).
- `uuid`: client-generated UUID. It maps the result in `sync_status` and makes retries safe, since a UUID that was already processed is not run again.
- `temp_id`: optional, used only on creation commands. Later commands in the same request can reference it.
- `args`: object with the command parameters.

Command types named in the v1 reference (argument tables not fully readable):

- Tasks: `item_add`, `item_update`, `item_move`, `item_close` (complete), `item_uncomplete`, `item_delete`. The sidebar names are "Complete task", "Close task", "Uncomplete item", and "Move a task". Arguments: UNVERIFIED beyond `content` and `project_id` in `item_add`.
- Projects: `project_add`, `project_update`, `project_delete`. Example args: `name`, `color` (source: v1 Sync section example).
- Labels: `label_add`, `label_update`, `label_delete`. Args: UNVERIFIED.
- Sections: `section_add`, `section_update`, `section_delete`. Args: UNVERIFIED.

Due dates: set them either with a natural-language `due.string` (the server parses it), or with `due_date` / `due_datetime` plus `due_timezone` for an exact value. Confirm the exact arg names in the Due dates section (UNVERIFIED).

Recurring tasks: complete them with `item_close`. For recurring tasks, the server advances the due date to the next occurrence rather than marking the task done. This matches the Todoist app behavior. The API-specific wording is UNVERIFIED.

Deprecated commands (source: v1 Sync section, quoted):
- `day_orders` in the sync response: "**Deprecated** and will be removed in a future version — use `user_item_orders`, which carries one entry per task with a stable `order_key`, instead."
- `workspace_set_default_project_ordering`: "This command is deprecated and will be removed in a future version." Use `default_order_key` on `project_update` or `folder_update`.

## 5. Completed tasks

- REST endpoints exist: "getTasks Completed By Completion Date" and "getTasks Completed By Due Date" (source: v1 navigation). Paths, query params (`since`, `until`, `limit`, `cursor`), and pagination shape are UNVERIFIED.
- Sync also returns `completed_info` (counts of completed items per project, section, or parent). Source: v1 Sync section.

## 6. Colors

- Projects and labels both have a `color` string. The allowed names are on a "Colors" page (source: v1 navigation), but its contents were not readable. The one example value seen is `berry_red` (project_add example) and `lime_green`. The full list is UNVERIFIED.

## 7. Gotchas

- IDs are strings. Do not assume numeric IDs. Client `temp_id` values are not valid server IDs; use `temp_id_mapping` after a write.
- Use `uuid` on every command so retries are idempotent.
- Sync tokens must be persisted. Losing the token forces a full sync.
- Full syncs can lag on large accounts. Compare `full_sync_date_utc` with current time (source: v1 Sync section).
- The v1 API uses `items` for tasks in sync payloads. Legacy code may still expect `tasks`.
- Pagination for REST list endpoints is documented in a "Pagination" section (cursor-based details UNVERIFIED).
- Rate-limit quotas are UNVERIFIED. Implement backoff on 429 and honor `retry_after`.

## Sources

- https://developer.todoist.com/api/v1/ (overview, Authorization, Sync, Projects, Sections, Due dates)
- https://developer.todoist.com/sync/v9/ (legacy reference, used for comparison only)
- https://doist.github.io/todoist-api-python/models/ (SDK field types)
