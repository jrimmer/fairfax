# Scripts and agents

Everything you can do in Fairfax's window can also be done from outside it: in a
terminal, from a script, from a link, or from a program that speaks the Model Context
Protocol. This chapter is for when you want to automate your tasks. You can skip it and
lose nothing.

Changes made this way show up in the open window within a couple of seconds, and they
can be undone in the app like any other change.

## `frfx`: tasks from the terminal

`frfx` is short for `fairfax`. It is the quickest way to capture or check a task
without leaving the terminal.

```sh
frfx Call Sam tomorrow 3pm #family !!    # add a task
frfx                                     # Today, numbered
frfx ls Personal                         # a list (or all, starred, scheduled, done)
frfx find dentist                        # search everything; the search syntax works
frfx done 347                            # complete by number
frfx open                                # open the window
frfx help
```

What you type after `frfx` is read the way Quick Entry reads it, so dates, tags,
priority and the rest work. If your task begins with a word that is also a command
(`done`, `find`, `ls`, `open`), start with `frfx add` instead: `frfx add find a plumber`.

The numbers beside each task in `frfx` and `frfx ls` are the ones you give to
`frfx done`.

**Tab completion** comes with the package for bash, zsh and fish. `frfx ls <Tab>` offers
your lists, and `frfx done <Tab>` offers your open tasks.

`fairfax add …` does the same as `frfx …`, for when you would rather type the full
name.

## `fairfax ctl`: commands that answer in JSON

For scripts, `fairfax ctl` runs a command and prints a JSON answer.

```sh
fairfax ctl help                                  # list every command
fairfax ctl help create_task                      # one command, with its arguments
fairfax ctl query_tasks focus=today
fairfax ctl create_task '{"title": "Call Sam tomorrow 3pm #family", "natural_language": true}'
fairfax ctl create_task title="Call Sam" due_date=2026-10-12
fairfax ctl complete_task task_id=347
fairfax ctl myday                                 # open My Day in the running window
```

Give arguments either as `key=value` pairs or as one JSON object. The answer always
has `"ok": true` on success, or an `error` with a code and a message, and the exit
status says which.

The commands cover lists, tasks, alerts, tags and the window:

| Group | Commands |
|---|---|
| Looking things up | `list_lists`, `list_list_groups`, `list_smart_lists`, `list_tags`, `list_tag_groups`, `list_locations`, `get_default_task_destination`, `get_task`, `list_subtasks`, `query_tasks`, `selected_tasks` |
| Changing tasks | `create_task`, `update_task`, `update_task_action`, `delete_task`, `duplicate_task`, `move_task`, `complete_task`, `uncomplete_task` |
| Alerts | `list_alarms`, `add_alarm`, `remove_alarm` |
| Tags | `pause_tag`, `unpause_tag` |
| The window | `select_list`, `select_list_group`, `select_smart_list`, `select_focus`, `myday`, `open_task` |

A few things worth knowing:

- `query_tasks` takes one of `list_id`, `list_group_id`, `smart_list_id` or `focus`,
  and a `search` using [the search syntax](04-search-and-smart-lists.md). It also takes date
  windows (`due_on_or_after`, `due_on_or_before`, `start_on_or_after`,
  `start_on_or_before`), a `sort` such as
  `{"field": "due_date", "direction": "descending"}`, and `limit` and `offset` for pages.
  Sort fields are title, due_date, start_date, priority, duration, created, modified,
  completed.
- `create_task` and `update_task` take `recurrence`
  (`{"frequency": "weekly", "weekdays": ["mon"]}`, or `{"frequency": "none"}` to clear it)
  and `location_update`.
- `add_alarm` takes `trigger_type`: relative (with `offset_seconds`), absolute (with `at`)
  or days-before (with `days_before` and `time_of_day`).
- Completing a repeating task keeps a done copy and advances the task, so do not
  re-run it blindly.

`fairfax ctl url '<link>'` runs a `fairfax://` link (below) from the terminal.

## `fairfax mcp`: let an assistant work with your tasks

`fairfax mcp` is a Model Context Protocol server over standard input and output, offering
the same commands as `fairfax ctl`. Any MCP-capable assistant can use it. For example,
in Claude Code:

```sh
claude mcp add fairfax -- fairfax mcp
```

The assistant then works with your tasks through the same commands, and everything it
changes can be undone with **Ctrl+Z**. Nothing leaves your machine unless the assistant
you chose sends it somewhere.

## `fairfax://` links

Links in the `fairfax://x-callback-url/<action>` form can add tasks, search or open
views. They work from a browser, a note, a launcher or `xdg-open`:

```sh
xdg-open 'fairfax://x-callback-url/add?task=Buy%20milk&due=tomorrow'
```

| Action | Does |
|---|---|
| `add` | Adds a task. It takes `task`, `note`, `forlist`, `forparentname`, `due`, `duetime`, `start`, `repeat`, `tags`, `locations`, `subtasks`, `priority`, `starred`, `action`, `picture` and `audio` (as base64), `ignoredefaults`, `edit`, `usequickentry` and `saveinclipboard` |
| `paste`, `addnewtask` | Adds tasks from the clipboard, or a blank new one |
| `search` | Searches |
| `showall`, `showtoday`, `showstarred`, `showscheduled` | Opens a focus list |
| `showlist`, `showtask` | Opens a list or a task |
| `completetasks` | Completes tasks |
| `gettaskid` | Gives back a task's id |
| `launch` | Opens Fairfax |

Typing `(clipboard)` as the value of `task`, `note`, `tags`, `locations` or `subtasks`
uses the clipboard. `x-success` is opened with the new task's id when it works, and
`x-error` with `errorCode` and `errorMessage` when it does not.

From a task's editor menu you can copy a **shareable link** to a single task (a
`showTask` link).

## A badge in your bar

On Linux, `fairfax --waybar` prints a small JSON object for a status bar. If you use
Waybar, add this to `~/.config/waybar/config.jsonc`:

```jsonc
"custom/fairfax": { "exec": "fairfax --waybar", "return-type": "json", "interval": 60, "on-click": "fairfax" }
```

It shows the count you chose under **Badge shows** in
[Settings → Alerts](12-settings.md#alerts). It turns red and shows a **!** when alerts
are waiting but Fairfax is not running to deliver them.

## Suggested changes from a model (off by default)

Fairfax can ask a language model to suggest changes, which you review and accept one at
a time. It is **off**. Nothing is sent anywhere until you turn it on.

### Turn it on

In **Settings → Sync**, under **Suggestions**, turn on **Offer suggested changes**, then
set:

- **Model server.** The default is `http://127.0.0.1:11434/v1`, which is a model running
  on your own computer (an Ollama server's OpenAI-compatible address). You can point it
  at another server that speaks the same interface.
- **Model.** The model's name, for example `llama3.2`.

A request goes to `POST /v1/chat/completions` on that server and nowhere else.

### What it offers

With suggestions on:

- **Suggest Steps…** (in the editor's toolbar and a task's right-click menu) breaks a
  task, especially a project, into ordered steps with optional durations. It is told the
  task's title, notes and existing checklist items or sub-tasks (so it does not repeat
  them) and the durations you tend to use.
- **Suggest Priorities** (in the right-click menu when you select several tasks).
- **Triage Inbox** (a button while the Inbox is open).
- **Ask in plain English…** (**Ctrl+Shift+E**, or the search menu) turns a request into a
  search.

### How you review them

Proposals land in a sheet (behind the checklist button in the task pane's toolbar, which
only appears while the feature is on). Each row shows what it would change:

| Key | Does |
|---|---|
| **y** | Apply the highlighted one |
| **n** or **Delete** | Dismiss it |
| **a** | Apply all of them |
| **Esc** | Close the sheet |

Applying goes through the same code as everything else, so **Ctrl+Z** undoes it.
Proposals are kept, so a review survives a restart. **Settings → Diagnostics** shows the
last request, the last response and any error, which helps if something does not work.
