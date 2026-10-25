# Fairfax sync format

This is the reference for the files Fairfax shares between machines. A program that reads or writes
them through the git repository behaves like another machine running Fairfax.

This is the format Fairfax 1.0 and later read and write. Every machine sharing a repository should
run 1.0 or later: earlier development builds merge whole records and drop `ext` and `modified_by`.

Every ```` ```json ```` block that follows a `Sample:` line below is imported by the test suite
(`tests/test_syncformat.cpp`) and compared with what Fairfax exports. If this document and the code
disagree, that test fails.

## Overview

The sync repository holds one small JSON file per record. The database itself is never synced.

```
Store.json
list-groups/<uid>.json
lists/<uid>.json
sections/<uid>.json
tag-groups/<uid>.json
tags/<uid>.json
locations/<uid>.json
tasks/<uid>.json
alarms/<uid>.json
attachments/<uid>.json
attachment-files/<file>
```

- `Store.json` names the format and its version. Fairfax writes it and does not read it back.
- Every record kind lives in its own folder. The folder name is fixed; use exactly the names above.
- A record file is named `<uid>.json`. The file name, without `.json`, is the record's uid. Fairfax
  takes the uid from the file name, not from the file's contents.
- A uid is a string. Fairfax makes uppercase UUIDs without braces, for example
  `3F2504E0-4F89-41D3-9A0C-0305E82C3301`. Use the same. Keep to the characters `A-Z a-z 0-9 _ -`:
  the folder and WebDAV providers ignore files with other names.
- A record file is one JSON object. Keys are sorted alphabetically, indentation is four spaces,
  and the file ends with a newline. Fairfax rewrites every file it syncs in that form. Other
  forms are read fine, but a different layout makes Fairfax commit a reformatting of your file.
- Files that are not JSON objects are skipped when reading, and Fairfax later deletes or overwrites them in the
  record folders. Files outside the record folders, and files that do not end in `.json`, are left alone.

Sample: `Store.json`

```json
{
    "format": "fairfax-sync",
    "version": 1
}
```

The file is written as `{"format": "fairfax-sync", "version": 1}`. Do not create or edit it.

## How a sync merges

Git sync works in this order: fetch; reset the working copy to the remote branch; merge the
files that changed on the remote since the last sync into the local database; write every record
back out; commit if anything changed; push. A rejected push repeats the cycle (three attempts).
The remote branch is the one `origin/HEAD` points at, else `main`, else `master`, else the first
branch. Use `main`.

Fairfax only reads the files that differ between the last commit it synced and the remote head.
A change that is not in a commit is invisible to it.

**Merging is per record, and per field.** Fairfax compares each changed file with its local copy
of the record, and with the base: the file as it was when Fairfax last synced it, the last version
both sides agreed on. The record is merged three ways, field by field:

- A field changed only in the file (its value differs from the base) is taken from the file. A
  field changed only locally keeps the local value. A field both sides changed, to different values,
  goes to the side with the larger `modified` (if those are equal, the side whose serialized
  file text sorts higher, bytewise; both machines pick the same side).
- `tag_uids` and `location_uids` merge as sets: what either side added is kept, what either side
  removed is removed. `ext` merges the same way per writer key (`ext.acme-sync` is one unit).
- A field the file leaves out is not a change; Fairfax keeps its value. (To reset a field, write
  its default.)
- `modified_by` follows the side that won; `modified` becomes the larger of the two. If the merge
  produced a record neither side had, Fairfax writes it back in the same sync.
- A tombstone against edits: the tombstone wins only if its `modified` is larger. Otherwise the
  record stays live with the other side's edits.
- With no base (the first time Fairfax sees a file, or a base it cannot read) the old rule applies:
  the larger `modified` replaces the whole record, a tie going to the higher serialized text.
- A record that Fairfax does not have yet is inserted.
- A file that is deleted in git deletes the local record outright. Prefer a tombstone (below).

The base is the file in the last commit Fairfax synced (git), or a copy of each file as last pulled
or pushed that Fairfax keeps beside its local mirror (folder and WebDAV).

Example. In Fairfax you complete a task. Before Fairfax syncs, another program pushes an edit of
that task's `notes` with a newer `modified`. Whole-record "newest wins" would bring the task back
not done. Fairfax sees `completed` and `completed_at` changed only locally and `notes` only in the
file, so the task ends completed with the new notes, and `modified` is the newer of the two.

**What a writer must do.**

1. Set `uid` to the file name (without `.json`).
2. Set `modified` to the current time, as an integer count of seconds since 1970-01-01 UTC.
3. When changing a record, make `modified` strictly greater than the value it had. Use the larger
   of now and the old value plus one.
4. Write all the fields the record needs (see the field tables). In a new record a missing field
   gets its default. In a record Fairfax already has, a missing field is left as it was.
5. Keep the fields you read and do not understand. Fairfax keeps `ext` (below) and drops every other key it
   does not know, so put your own data under `ext.<your-name>`.

A record with a missing or non-numeric `modified` counts as time 0: it loses to every existing
local record, and a new one is stamped with the time it arrived. A `modified` far in the future
wins every later comparison until the clocks catch up, so do not guess.

**Tombstones.** To delete a record, keep its file and set `"deleted": true` with a new `modified`.
Fairfax hides deleted records but keeps them so the deletion reaches every machine. When Fairfax
writes the tree it removes the file of a tombstone whose `modified` is more than 60 days old.
Delete a task's subtasks the same way. Fairfax does not tombstone a deleted task's alarms and
attachments; they are hidden with their task. A writer can tombstone them too.

**References.** Records point at each other by uid:

| Field | Record | Points at |
|---|---|---|
| `group_uid` | list | list group |
| `group_uid` | tag | tag group |
| `list_uid` | task, section | list |
| `section_uid` | task | section |
| `parent_uid` | task | task (its parent) |
| `tag_uids` | task | tags |
| `location_uids` | task | locations |
| `task_uid` | alarm, attachment | task |

An empty string means "none". A reference to a uid Fairfax does not have (yet) is kept as it is:
Fairfax remembers the raw uid, writes it back unchanged, and resolves it whenever the named record
arrives in a later sync. So a writer may commit a task before its list, tag, section, parent or
place, and an alarm or attachment before its task; committing referenced records first is still
tidier, because until the record arrives:

- a task whose `list_uid` is unknown shows in the default list for new tasks (Settings, Tasks,
  Default list) on that machine, while its file keeps naming the real list. Moving the task to
  another list in Fairfax ends the wait: the file then names the new list;
- a task whose `section_uid` or `parent_uid` is unknown shows without a section, or at the top level;
- unknown `tag_uids` and `location_uids` entries are kept but not shown, next to the ones that
  resolve;
- an alarm or attachment whose task is unknown is stored but does nothing (an alarm never fires).

A reference that never resolves stays in the file for good; Fairfax never rewrites it to "none".

**Data from other writers.** Every record kind may carry an `ext` object: `"ext": {"<writer-name>": {...}}`.
Fairfax stores it as it is, never reads into it, and writes it back unchanged (keys sorted like the
rest of the file). It is omitted when empty. Put your own data under one key that names your program
(`ext.acme-sync`) and leave the other writers' keys alone. Copying a task in Fairfax (duplicate,
the copy a repeating task leaves behind) does not copy `ext`. Numbers inside `ext` are read as
doubles, so integers above 2^53 lose precision; store such values as strings.

**Who changed it.** Every record may carry `"modified_by": "<who>"`, a free-form string naming the
machine or program that last changed it. Fairfax sets it to `fairfax@<hostname>` whenever it
changes a record on that machine (not when it only rewrites a file in canonical form), and keeps
whatever another writer put there otherwise. Set it to your program's name when you change a
record. Fairfax does not show it.

**Excluded lists.** A user can mark a list "Don't Sync This List" on one machine. That machine
neither writes nor imports the list's files or anything that belongs to it. Another machine's
files for it stay in the repository untouched.

## List groups

Folder `list-groups/`. A group holds lists in the sidebar.

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `name` | string | Group name | yes | `""` |
| `sort_order` | number | Position among groups, ascending | no | `0` |
| `collapsed` | bool | Shown collapsed in the sidebar | no | `false` |
| `color` | integer | Palette index lists in the group take on; `-1` for none | no | `-1` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `list-groups/10000000-0000-4000-8000-000000000001.json`

```json
{
    "collapsed": false,
    "color": 3,
    "deleted": false,
    "modified": 1792000000,
    "name": "Work",
    "sort_order": 1,
    "uid": "10000000-0000-4000-8000-000000000001"
}
```

## Lists

Folder `lists/`. A list holds tasks. A list is normal, the Inbox, or a Smart List (a saved search).

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `name` | string | List name | yes | `""` |
| `kind` | integer | `0` normal, `1` Inbox, `2` Smart List | no | `0` |
| `group_uid` | string | List group, or `""` for none | no | `""` |
| `color` | integer | Palette index; with bit `0x1000000` set, the low 24 bits are a custom `0xRRGGBB` | no | `0` |
| `sort_order` | number | Position among lists, ascending | no | `0` |
| `sort_field` | integer | How the list orders its tasks: `0` due date, `1` start date, `2` priority, `3` list, `4` title, `5` creation, `6` modified, `7` duration, `8` manual, `9` tag, `10` completion, `11` status, `12` section, `13` note, `14` location | no | `0` |
| `sort_ascending` | bool | Direction of the sort | no | `true` |
| `separated` | bool | Show section headers in the list | no | `true` |
| `show_in_all` | bool | Include its tasks in the All view | no | `true` |
| `show_in_today` | bool | Include its tasks in Today | no | `true` |
| `show_in_done` | bool | Include its completed tasks in the Done view | no | `true` |
| `archived` | bool | List is archived | no | `false` |
| `smart_query` | string | Search text of a Smart List; empty for other lists | no | `""` |
| `smart_range` | string | Date filter of a Smart List: `""` none, `next:N:unit` (unit `d`, `w`, `m` or `y`), `fixed:yyyy-MM-dd:yyyy-MM-dd`, or `nodue` | no | `""` |
| `completed_mode` | integer | Completed tasks in this list: `0` list default, `1` always show, `2` never show | no | `0` |
| `include_scheduled` | bool | Smart Lists: also match scheduled tasks | no | `false` |
| `mute_alerts` | bool | Alerts of this list's tasks stay silent | no | `false` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `lists/20000000-0000-4000-8000-000000000001.json`

```json
{
    "archived": false,
    "color": 4,
    "completed_mode": 0,
    "deleted": false,
    "ext": {
        "acme-sync": {
            "id": "P-7"
        }
    },
    "group_uid": "10000000-0000-4000-8000-000000000001",
    "include_scheduled": false,
    "kind": 0,
    "modified": 1792000000,
    "modified_by": "fairfax@desktop",
    "mute_alerts": false,
    "name": "Website",
    "separated": true,
    "show_in_all": true,
    "show_in_done": true,
    "show_in_today": true,
    "smart_query": "",
    "smart_range": "",
    "sort_ascending": true,
    "sort_field": 8,
    "sort_order": 1,
    "uid": "20000000-0000-4000-8000-000000000001"
}
```

An Inbox list (`kind` 1) with no group:

Sample: `lists/20000000-0000-4000-8000-000000000002.json`

```json
{
    "archived": false,
    "color": 0,
    "completed_mode": 0,
    "deleted": false,
    "group_uid": "",
    "include_scheduled": false,
    "kind": 1,
    "modified": 1792000000,
    "mute_alerts": false,
    "name": "Inbox",
    "separated": true,
    "show_in_all": true,
    "show_in_done": true,
    "show_in_today": true,
    "smart_query": "",
    "smart_range": "",
    "sort_ascending": true,
    "sort_field": 0,
    "sort_order": 2,
    "uid": "20000000-0000-4000-8000-000000000002"
}
```

## Sections

Folder `sections/`. A section is a heading inside a list that groups tasks.

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `list_uid` | string | The list it belongs to | yes | `""` |
| `name` | string | Heading text | yes | `""` |
| `sort_order` | number | Position among the list's sections, ascending | no | `0` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `sections/30000000-0000-4000-8000-000000000001.json`

```json
{
    "deleted": false,
    "list_uid": "20000000-0000-4000-8000-000000000001",
    "modified": 1792000000,
    "name": "Launch",
    "sort_order": 1,
    "uid": "30000000-0000-4000-8000-000000000001"
}
```

## Tag groups

Folder `tag-groups/`.

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `name` | string | Group name | yes | `""` |
| `sort_order` | number | Position among tag groups, ascending | no | `0` |
| `collapsed` | bool | Shown collapsed in the sidebar | no | `false` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `tag-groups/40000000-0000-4000-8000-000000000001.json`

```json
{
    "collapsed": false,
    "deleted": false,
    "modified": 1792000000,
    "name": "Context",
    "sort_order": 1,
    "uid": "40000000-0000-4000-8000-000000000001"
}
```

## Tags

Folder `tags/`.

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `name` | string | Tag name | yes | `""` |
| `group_uid` | string | Tag group, or `""` for none | no | `""` |
| `paused` | bool | A paused tag hides the tasks that carry it | no | `false` |
| `sort_order` | number | Position among tags, ascending | no | `0` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `tags/50000000-0000-4000-8000-000000000001.json`

```json
{
    "deleted": false,
    "group_uid": "40000000-0000-4000-8000-000000000001",
    "modified": 1792000000,
    "name": "errand",
    "paused": false,
    "sort_order": 1,
    "uid": "50000000-0000-4000-8000-000000000001"
}
```

A tag without a group:

Sample: `tags/50000000-0000-4000-8000-000000000002.json`

```json
{
    "deleted": false,
    "group_uid": "",
    "modified": 1792000000,
    "name": "waiting",
    "paused": false,
    "sort_order": 2,
    "uid": "50000000-0000-4000-8000-000000000002"
}
```

## Locations

Folder `locations/`. A saved place a task can carry.

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `name` | string | Place name | yes | `""` |
| `address` | string | Street address, free text | no | `""` |
| `latitude` | number | Degrees | no | `0` |
| `longitude` | number | Degrees | no | `0` |
| `paused` | bool | A paused location hides the tasks that carry it | no | `false` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `locations/60000000-0000-4000-8000-000000000001.json`

```json
{
    "address": "12 High Street, Springfield",
    "deleted": false,
    "latitude": 51.5,
    "longitude": -0.12,
    "modified": 1792000000,
    "name": "Hardware store",
    "paused": false,
    "uid": "60000000-0000-4000-8000-000000000001"
}
```

## Tasks

Folder `tasks/`. See "Tasks in depth" below for how the fields work together.

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `title` | string | Task title | yes | `""` |
| `list_uid` | string | The list it is in | yes | `""` |
| `parent_uid` | string | Parent task (a project, checklist or task), or `""` | no | `""` |
| `section_uid` | string | Section of the list, or `""` | no | `""` |
| `type` | integer | `0` task, `1` project, `2` checklist | no | `0` |
| `notes` | string | Free text, may contain newlines | no | `""` |
| `priority` | integer | `0` none, `1` low, `2` medium, `3` high | no | `0` |
| `starred` | bool | Flagged | no | `false` |
| `start_date` | string or null | Local date `yyyy-MM-dd`; `null` for none | no | `null` |
| `start_time` | integer | Minutes after local midnight (0 to 1439); `-1` for a date-only start | no | `-1` |
| `due_date` | string or null | Local date `yyyy-MM-dd`; `null` for none | no | `null` |
| `due_time` | integer | Minutes after local midnight; `-1` for an all-day deadline | no | `-1` |
| `duration` | integer | Planned length in minutes | no | `0` |
| `completed` | bool | The task is done | no | `false` |
| `completed_at` | integer or null | Seconds since epoch, UTC, when it was completed; `null` if open | no | `null` |
| `action_type` | integer | `0` none, `1` call, `2` message, `3` mail, `4` visit, `5` URL, `6` Google | no | `0` |
| `action_value` | string | What the action acts on (a phone number, an address, a URL); empty for none | no | `""` |
| `rec_frequency` | integer | `0` none (not repeating), `1` daily, `2` weekly, `3` monthly, `4` yearly, `5` weekdays | no | `0` |
| `rec_interval` | integer | Repeat every N units; values below 1 become 1 | no | `1` |
| `rec_from` | integer | `0` next date counts from the due date, `1` from the completion date | no | `0` |
| `rec_weekdays` | integer | Weekly rules: bit mask, bit 0 Monday to bit 6 Sunday; `0` means "same weekday" | no | `0` |
| `rec_month_ordinal` | integer | Monthly rules: `1` to `5` or `-1` for last; `0` means "same day of month" | no | `0` |
| `rec_month_weekday` | integer | With an ordinal: `1` Monday to `7` Sunday | no | `0` |
| `rec_end_type` | integer | `0` never, `1` after `rec_end_count` occurrences, `2` on `rec_end_date` | no | `0` |
| `rec_end_count` | integer | Occurrence limit for end type `1` | no | `0` |
| `rec_end_date` | string or null | Last date for end type `2`, `yyyy-MM-dd` | no | `null` |
| `rec_done` | integer | Occurrences already completed | no | `0` |
| `series_uid` | string | Shared by a repeating task and the completed copies it left behind; `""` if none | no | `""` |
| `sort_order` | number | Position among siblings (same list and parent), ascending | no | `0` |
| `collapsed` | bool | A project or checklist is shown collapsed | no | `false` |
| `tag_uids` | array of string | Tags on the task; a uid Fairfax does not have yet is kept | no | `[]` |
| `location_uids` | array of string | Locations on the task; a uid Fairfax does not have yet is kept | no | `[]` |
| `created` | integer or null | Seconds since epoch, UTC; if absent Fairfax uses `modified` | no | `modified` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

A project with a subtask, tags, a location and a deadline at 17:00:

Sample: `tasks/70000000-0000-4000-8000-000000000001.json`

```json
{
    "action_type": 5,
    "action_value": "https://example.com/launch",
    "collapsed": false,
    "completed": false,
    "completed_at": null,
    "created": 1791900000,
    "deleted": false,
    "due_date": "2026-10-30",
    "due_time": 1020,
    "duration": 90,
    "ext": {
        "acme-sync": {
            "etag": "9f3c",
            "id": "T-1042"
        }
    },
    "list_uid": "20000000-0000-4000-8000-000000000001",
    "location_uids": [
        "60000000-0000-4000-8000-000000000001"
    ],
    "modified": 1792000000,
    "modified_by": "acme-sync@laptop",
    "notes": "Check the checklist first.\nThen announce.",
    "parent_uid": "",
    "priority": 3,
    "rec_done": 0,
    "rec_end_count": 0,
    "rec_end_date": null,
    "rec_end_type": 0,
    "rec_frequency": 0,
    "rec_from": 0,
    "rec_interval": 1,
    "rec_month_ordinal": 0,
    "rec_month_weekday": 0,
    "rec_weekdays": 0,
    "section_uid": "30000000-0000-4000-8000-000000000001",
    "series_uid": "",
    "sort_order": 1,
    "starred": true,
    "start_date": "2026-10-24",
    "start_time": -1,
    "tag_uids": [
        "50000000-0000-4000-8000-000000000001",
        "50000000-0000-4000-8000-000000000002"
    ],
    "title": "Launch the new site",
    "type": 1,
    "uid": "70000000-0000-4000-8000-000000000001"
}
```

A completed subtask of that project:

Sample: `tasks/70000000-0000-4000-8000-000000000002.json`

```json
{
    "action_type": 0,
    "action_value": "",
    "collapsed": false,
    "completed": true,
    "completed_at": 1791996400,
    "created": 1791900000,
    "deleted": false,
    "due_date": "2026-10-27",
    "due_time": -1,
    "duration": 0,
    "list_uid": "20000000-0000-4000-8000-000000000001",
    "location_uids": [],
    "modified": 1792000000,
    "notes": "",
    "parent_uid": "70000000-0000-4000-8000-000000000001",
    "priority": 0,
    "rec_done": 0,
    "rec_end_count": 0,
    "rec_end_date": null,
    "rec_end_type": 0,
    "rec_frequency": 0,
    "rec_from": 0,
    "rec_interval": 1,
    "rec_month_ordinal": 0,
    "rec_month_weekday": 0,
    "rec_weekdays": 0,
    "section_uid": "30000000-0000-4000-8000-000000000001",
    "series_uid": "",
    "sort_order": 1,
    "starred": false,
    "start_date": null,
    "start_time": -1,
    "tag_uids": [],
    "title": "Order the banner",
    "type": 0,
    "uid": "70000000-0000-4000-8000-000000000002"
}
```

A task that repeats every Monday and Wednesday at 09:00:

Sample: `tasks/70000000-0000-4000-8000-000000000003.json`

```json
{
    "action_type": 0,
    "action_value": "",
    "collapsed": false,
    "completed": false,
    "completed_at": null,
    "created": 1791900000,
    "deleted": false,
    "due_date": "2026-10-26",
    "due_time": 540,
    "duration": 0,
    "list_uid": "20000000-0000-4000-8000-000000000002",
    "location_uids": [],
    "modified": 1792000000,
    "notes": "",
    "parent_uid": "",
    "priority": 1,
    "rec_done": 2,
    "rec_end_count": 0,
    "rec_end_date": null,
    "rec_end_type": 0,
    "rec_frequency": 2,
    "rec_from": 0,
    "rec_interval": 1,
    "rec_month_ordinal": 0,
    "rec_month_weekday": 0,
    "rec_weekdays": 5,
    "section_uid": "",
    "series_uid": "70000000-0000-4000-8000-000000000003",
    "sort_order": 2,
    "starred": false,
    "start_date": null,
    "start_time": -1,
    "tag_uids": [],
    "title": "Water the plants",
    "type": 0,
    "uid": "70000000-0000-4000-8000-000000000003"
}
```

The smallest useful task. Everything left out takes its default:

Sample: `tasks/70000000-0000-4000-8000-000000000004.json`

```json
{
    "created": 1792000000,
    "list_uid": "20000000-0000-4000-8000-000000000002",
    "modified": 1792000000,
    "title": "Call the plumber",
    "uid": "70000000-0000-4000-8000-000000000004"
}
```

A deleted task (a tombstone):

Sample: `tasks/70000000-0000-4000-8000-000000000005.json`

```json
{
    "action_type": 0,
    "action_value": "",
    "collapsed": false,
    "completed": false,
    "completed_at": null,
    "created": 1791900000,
    "deleted": true,
    "due_date": null,
    "due_time": -1,
    "duration": 0,
    "list_uid": "20000000-0000-4000-8000-000000000002",
    "location_uids": [],
    "modified": 1792000000,
    "notes": "",
    "parent_uid": "",
    "priority": 0,
    "rec_done": 0,
    "rec_end_count": 0,
    "rec_end_date": null,
    "rec_end_type": 0,
    "rec_frequency": 0,
    "rec_from": 0,
    "rec_interval": 1,
    "rec_month_ordinal": 0,
    "rec_month_weekday": 0,
    "rec_weekdays": 0,
    "section_uid": "",
    "series_uid": "",
    "sort_order": 3,
    "starred": false,
    "start_date": null,
    "start_time": -1,
    "tag_uids": [],
    "title": "Old idea",
    "type": 0,
    "uid": "70000000-0000-4000-8000-000000000005"
}
```

### Tasks in depth

**Completion.** Set `completed` to `true` and `completed_at` to the completion time. To reopen a
task, set `completed` to `false` and `completed_at` to `null`. Completing a project or checklist
in Fairfax also completes its open children, one record each. A writer that completes a project
should do the same. Fairfax may later tombstone completed tasks automatically after a number of
days the user chooses.

**Repeating tasks.** Fairfax does not recompute anything from the rule when it reads a file. When
a repeating task is completed in Fairfax, it leaves a copy behind and moves the task itself on:

- the copy is a new task record with no repeat rule, `completed` true, `completed_at` set, and the
  `series_uid` of the series (the original task's uid if it had none);
- the live task keeps its uid, gets the next due date (and a start date moved by the same
  amount), `rec_done` plus one, and `completed` false;
- when the rule has run out (count or end date reached), the live task is simply completed.

A writer that completes a repeating task should do the same, or else complete it as if it did
not repeat (clear `rec_frequency` first only if the series should end). The next date is found
from `due_date` (or `start_date` if there is no due date), or from the completion date when
`rec_from` is `1`. Weekly rules with `rec_weekdays` pick the next set weekday; with an interval
above 1 they jump whole weeks from the Monday of the current week. A monthly rule with an ordinal
takes the Nth weekday of the month and skips months that have no such day.

**Dates and times.** `start_date`, `due_date` and `rec_end_date` are calendar dates in the
user's local time zone, written `yyyy-MM-dd`. They carry no time zone. `start_time` and `due_time`
are minutes after local midnight, so 09:00 is `540` and 17:00 is `1020`. Both are `-1` when the
task has only a date. A task with a `due_date` and `due_time` of `-1` is all-day; it counts as
due at the end of that day. A time without a date has no meaning: set the date as well. All
timestamps (`created`, `modified`, `completed_at`, an alarm's `at`) are whole seconds since the
epoch, in UTC.

**Due and start.** `due_date` (with `due_time`) is the deadline. `start_date` is when the task
becomes available to work on. Neither requires the other. With a start time and a due time,
`duration` is the planned length in minutes; Fairfax shows a timed task as starting `duration`
minutes before its due time.

**Notes.** `notes` is plain text with `\n` newlines.

**Subtasks, projects and checklists.** A task's children are the tasks whose `parent_uid` is its
uid. Children belong in the parent's list; give them the parent's `list_uid`. A project
(`type` 1) and a checklist (`type` 2) are tasks that are meant to have children; a plain task
(`type` 0) can have them too. Nesting is by `parent_uid` only. Do not make cycles.

**Tags and locations.** `tag_uids` and `location_uids` are arrays of uids of tags and locations.
Use uids of undeleted records. The order is not significant.

**Ordering.** Within one list and one parent, tasks are ordered by `sort_order` ascending when the
list sorts manually (`sort_field` 8). Fairfax creates a new task with one more than the largest
`sort_order` among its siblings. Other `sort_field` values order by the named property instead.
Sections are ordered the same way by their own `sort_order`.

## Alarms

Folder `alarms/`. An alert on a task. A task can have at most 10 (Fairfax enforces that when the
user adds them; the importer does not).

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `task_uid` | string | The task | yes | `""` |
| `kind` | integer | `0` relative: `offset_minutes` minutes before the due moment; `1` absolute: at `at`; `2` days before: `offset_minutes` whole days before the due date, at the time of day of `at` | no | `0` |
| `offset_minutes` | integer | Minutes (kind 0) or days (kind 2) before the due moment | no | `0` |
| `at` | integer or null | Seconds since epoch, UTC; the moment for kind 1, the time of day for kind 2 | no | `null` |
| `sound` | bool | Alert with a sound; `false` is a silent message | no | `true` |
| `snoozed_until` | integer or null | A snoozed alarm fires again at this moment | no | `null` |
| `fired_for` | integer or null | The fire moment it last went off for | no | `null` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `alarms/80000000-0000-4000-8000-000000000001.json`

```json
{
    "at": null,
    "deleted": false,
    "fired_for": null,
    "kind": 0,
    "modified": 1792000000,
    "offset_minutes": 60,
    "snoozed_until": null,
    "sound": true,
    "task_uid": "70000000-0000-4000-8000-000000000001",
    "uid": "80000000-0000-4000-8000-000000000001"
}
```

An alarm at a fixed moment:

Sample: `alarms/80000000-0000-4000-8000-000000000002.json`

```json
{
    "at": 1793005200,
    "deleted": false,
    "fired_for": null,
    "kind": 1,
    "modified": 1792000000,
    "offset_minutes": 0,
    "snoozed_until": null,
    "sound": false,
    "task_uid": "70000000-0000-4000-8000-000000000003",
    "uid": "80000000-0000-4000-8000-000000000002"
}
```

A writer leaves `snoozed_until` and `fired_for` as `null`. They record what one machine did.

## Attachments

Folder `attachments/`. A picture, audio note or file kept with a task. The record describes the
file; the bytes are a separate file in `attachment-files/`.

| Field | Type | Meaning | Required | Default |
|---|---|---|---|---|
| `uid` | string | Same as the file name | yes | |
| `task_uid` | string | The task | yes | `""` |
| `kind` | integer | `0` image, `1` audio, `2` other file | yes | `0` |
| `name` | string | The original file name, shown to the user | no | `""` |
| `file_name` | string | Name of the bytes file in `attachment-files/`; Fairfax uses `<uid>.<extension>` | yes | `""` |
| `bytes` | integer | Size of the file in bytes | no | `0` |
| `created` | integer or null | Seconds since epoch, UTC; if absent Fairfax uses `modified` | no | `modified` |
| `modified` | integer | Seconds since epoch, UTC | yes | |
| `deleted` | bool | Tombstone | no | `false` |
| `ext` | object | Data of other writers, kept as is (see "Data from other writers") | no | omitted |
| `modified_by` | string | Who last changed it: a machine or program name | no | omitted |

Sample: `attachments/90000000-0000-4000-8000-000000000001.json`

```json
{
    "bytes": 48213,
    "created": 1791950000,
    "deleted": false,
    "file_name": "90000000-0000-4000-8000-000000000001.png",
    "kind": 0,
    "modified": 1792000000,
    "name": "mockup.png",
    "task_uid": "70000000-0000-4000-8000-000000000001",
    "uid": "90000000-0000-4000-8000-000000000001"
}
```

The file's bytes live in `attachment-files/<file_name>`, committed in the same commit as the
record. They are binary; they are not JSON. Fairfax copies a file to its local attachments folder
when the record arrives, and only if it is not there yet. It removes files from `attachment-files/`
that no live attachment record names, so write a record for every file you add.

## Writing from another program

Work in your own clone of the repository, never in Fairfax's own working copy: Fairfax resets that
one to the remote on every sync. Do not create or edit `Store.json`. Set `GIT_TERMINAL_PROMPT=0`
if there is no terminal.

Keep your own data under `ext.<your-name>` and set `modified_by` to your program's name whenever
you change a record. You may commit a record before the records it names (a task before its list);
Fairfax keeps the references until those arrive, though committing lists first is still tidier.

Every example below assumes `cd` into the clone, `git pull --ff-only` just done, and these shell
variables:

```sh
NOW=$(date +%s)
UID_NEW=$(uuidgen | tr a-z A-Z)
LIST=20000000-0000-4000-8000-000000000002   # an existing list's uid
TASK=70000000-0000-4000-8000-000000000004   # an existing task's uid
```

**Create a task.** Write a new file and commit it. Commit its list first, or in the same commit.

```sh
cat > "tasks/$UID_NEW.json" <<EOF
{
    "created": $NOW,
    "list_uid": "$LIST",
    "modified": $NOW,
    "title": "Call the plumber",
    "uid": "$UID_NEW"
}
EOF
git add "tasks/$UID_NEW.json"
git commit -m "Add task: Call the plumber"
git push origin main
```

**Edit a task.** Change only the fields you mean to, and set a larger `modified`. With `jq`:

```sh
f=tasks/$TASK.json   # TASK is the uid of an existing task
old=$(jq .modified "$f"); new=$(( NOW > old ? NOW : old + 1 ))
jq -S --indent 4 --argjson m "$new" '.title = "Call the plumber about the tap" | .modified = $m' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
git commit -am "Edit task: Call the plumber"
git push origin main
```

**Complete a task.** Set `completed` and `completed_at`. This is for a task that does not repeat.

```sh
f=tasks/$TASK.json
old=$(jq .modified "$f"); new=$(( NOW > old ? NOW : old + 1 ))
jq -S --indent 4 --argjson m "$new" '.completed = true | .completed_at = $m | .modified = $m' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
git commit -am "Complete task: Call the plumber"
git push origin main
```

**Delete a task.** Set `deleted` rather than removing the file, and do the same for its subtasks.

```sh
f=tasks/$TASK.json
old=$(jq .modified "$f"); new=$(( NOW > old ? NOW : old + 1 ))
jq -S --indent 4 --argjson m "$new" '.deleted = true | .modified = $m' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
git commit -am "Delete task: Call the plumber"
git push origin main
```

**When the push is rejected.** Someone pushed first. Do not merge or rebase. Throw the attempt away
and apply the change again to the newest files, because the other machine may have changed the
same record and your `modified` has to beat its `modified`.

```sh
git fetch origin
git reset --hard origin/main
# read the file again, re-apply the edit with a fresh NOW and a larger modified, then:
git commit -am "Edit task: Call the plumber"
git push origin main
```

Retry a few times, then give up and report the failure. Fairfax picks the change up on its next
sync (every five minutes, or after the user edits something) and writes the record back in its own
form. That can add a commit with the same content in canonical layout; this is expected.

## Review before freezing

These are things that look tied to how Fairfax stores data today, or are awkward for other
programs. Nothing here has been changed in the code.

1. **Enumerations are bare integers.** `type`, `priority`, `action_type`, `rec_frequency`,
   `rec_from`, `rec_end_type`, and the list and alarm `kind`, `sort_field` and `completed_mode`.
   Cleaner: strings such as `"project"`, `"high"`, `"weekly"`, `"relative"`.
2. **Recurrence is ten flat `rec_*` keys.** Cleaner: one `recurrence` object, or `null`, with
   `frequency`, `interval`, `from`, `weekdays`, `month`, `end` and `done` inside it.
3. **`rec_weekdays` is a bit mask** (bit 0 Monday). Cleaner: an array such as `["mon", "wed"]`.
4. **Times of day are minutes with `-1` meaning "none"**, while dates use `null` for none. Cleaner:
   `"due": "2026-10-30"` or `"due": "2026-10-30T17:00"` (local, no zone) or `null`, one field for date and time.
   `start_time`, `due_time`, `duration` and `rec_*` counts could then be dropped or made clearly optional.
5. **Timestamps are epoch seconds**, dates are ISO strings. Cleaner: ISO 8601 UTC strings
   (`"2026-10-14T17:46:40Z"`) for `created`, `modified`, `completed_at`, `at`, which are readable in a diff.
6. **`completed` and `completed_at` say the same thing.** Cleaner: `completed_at` alone, `null` when open.
7. **`deleted` plus `modified`.** Cleaner: `deleted_at` (a timestamp), so the 60-day prune does not
   depend on the last edit time.
8. **`uid` is both the file name and a key inside the file.** The file name wins on import.
   Cleaner: name only, or require the two to match.
9. **`modified` must be an integer for the merge to work, with no error otherwise.** A missing or
   non-numeric value silently counts as 0. Cleaner: reject such files and say so.
10. **List `color` mixes a palette index and a custom colour behind a flag bit (`0x1000000`).**
    Cleaner: `"color": "#3584e4"` or a named palette entry.
11. **`smart_range` is a small string language** (`next:3:d`, `fixed:from:to`, `nodue`), and
    `smart_query` is another. Cleaner: an object for the range; document the query syntax separately.
12. **`offset_minutes` of an alarm counts days when `kind` is 2**, and `at` means a moment for kind 1
    but only a time of day for kind 2. Cleaner: `offset` with a `unit`, and a separate `time` field.
13. **Per-machine state is synced**: `collapsed` (groups, tag groups, tasks), `snoozed_until`,
    `fired_for`. They make other machines' views jump. Cleaner: keep them out of the shared files.
14. **Task `sort_order` is a floating-point number.** Fairfax inserts at "+0.5". Cleaner: a string
    rank (fractional index) or an explicit order list on the parent.
15. **Attachment `file_name` is `<uid>.<ext>` by convention only**, and `name` versus `file_name` is easy to mix up.
    Cleaner: store the extension or media type, and derive the path.
16. **`Store.json` has a version that is never read.** A future format change would be ignored by older
    machines. Cleaner: refuse to sync when the version is newer than the code knows.
17. **Tombstones are pruned after 60 days from `modified`.** A machine that was offline longer than
    that, with an older copy, may bring the record back. I could not confirm this from the code.
18. **The code comment in `src/gitsync.h` says the cycle uses `git reset --soft`.** The code runs
    `git reset --hard origin/<branch>`. This document follows the code.
19. **A task with an empty `list_uid` is imported with no list.** I could not confirm
    from the code how every view shows such a task. Writers should always give a list.
20. **Fairfax deletes unreadable `.json` files from the record folders** when it writes. A writer
    that produces a malformed file loses it without warning.
21. **`action_value` has no checked format.** Its content depends on `action_type`; Fairfax does not
    validate it on import.
