# Precrime

A keyboard-first task manager for [Omarchy](https://omarchy.org), modelled closely on
[the reference app](https://example.com) for the Mac: three panels (lists, tasks, tags), focus lists that
pull tasks from everywhere, start dates that hide work until it's time, projects and
checklists, and single-key commands for everything you do to a task.

It follows the Omarchy theme live, light or dark, and keeps everything in a local SQLite
database, synced between machines through a git repository you choose.

## Install

```sh
bin/install        # builds, then makepkg -fsi; Precrime appears in the launcher (Super+Space)
```

Or try it without installing, on a throwaway database full of sample tasks:

```sh
bin/build && build/precrime --demo
```

Your tasks live in `~/.local/share/precrime/precrime.db`. `--db PATH` opens another file.

## Quick Entry

Capture a task from anywhere by binding Quick Entry to a key in
`~/.config/hypr/bindings.conf`, and floating its window:

```ini
bindd = SUPER SHIFT, T, Precrime quick entry, exec, precrime --quick
windowrule = float, title:^(Precrime Quick Entry)$
windowrule = center, title:^(Precrime Quick Entry)$
```

Type the task the way you'd say it, and Precrime picks out the details:
`Call Sam tomorrow at 3pm #family !! every Monday [30m] /work/`. Dates, start dates
(`starting Friday`), ranges (`from Monday until Friday`), repeats, `alert 30m`,
`[durations]`, `!`/`!!`/`!!!` priority, `*` star, `#tags` and `/list/` all work, and
`"quotes"` keep words literal. The same parsing applies to a new task's title in the main
window. Return saves, Shift+Return saves and starts another (so does pressing the bind
again), Ctrl+Return saves and opens the task.

## Sync

Precrime syncs through a git repository of your own. Make a **private** repository, then
open Settings (Ctrl+,) → Sync and paste its URL (`git@github.com:you/tasks.git`, or any URL
or path git accepts). Every machine pointed at it stays in step.

The database never goes into git. Each task, list and tag is a small JSON file, so changes
on different machines merge record by record, with the most recent edit winning, and git
never reports a conflict. Precrime syncs at startup, about 20 seconds after you change
something, every five minutes, and once more on quit; Ctrl+S or the sidebar's sync button
syncs right away. git runs with your own SSH keys and config and never prompts, so the
remote must work non-interactively (`ssh -T git@github.com` should succeed without a
password prompt).

To get alarms after you sign in, start Precrime in the background from
`~/.config/hypr/autostart.conf`:

```ini
exec-once = precrime --background
```

## How it works

- **Focus lists.** *All*, *Today*, *Starred*, *Scheduled* and *Done* gather tasks from every list.
  *Today* holds what's due today, what's overdue, and what you've started; a task added
  while *Today* is open is due today, one added in *Starred* is starred.
- **Start and due dates.** A future start date hides a task (it waits in *Scheduled*) until
  that day. A duration with a due time makes the task show up ahead of its deadline.
- **Projects and checklists.** A project's tasks keep their own dates and appear on their own
  in the focus lists; a checklist's items ride along with the checklist.
- **Tags.** Group them, filter by them from the right panel, and *pause* one (right-click) to
  hide every task that carries it until you resume it.
- **Smart Lists.** Type a search in the reference app's syntax (`type: hiprio tags: !work`,
  `"exact phrase"`, `OR`, `-word`) and save it as a Smart List.
- **Focus Filter.** The sun in the sort bar narrows any list to tasks that are overdue,
  due today, this week, high priority, starred and so on, each list remembering its own.
- **Repeats.** Completing a repeating task keeps a done copy and moves it to the next
  occurrence, from its due date or from the day you finished it.
- **The editor.** Typeable dates with a calendar (click the field or press ↓; ⌥1–⌥4 for Today,
  Tomorrow, the day after, a week), a full repeat editor (every N days/weeks/months/years,
  chosen weekdays, "2nd Tuesday", "last Friday", ending after N times or on a date, from
  completion), sub-tasks you add with Return, alerts (presets, or a date and time; nagging
  optional), locations, a picture and audio notes (record with the mic button, or drop
  files), and notes with live Markdown, clickable links and `@{Task}` links to other tasks.
- **Calendar day bar.** Add an iCalendar address in Settings → Calendar and today's events
  appear above Today; drag a task onto the bar to block out time for it.
- **Arrange freely.** Drag lists and groups to reorder them, drag tasks onto lists, Today,
  Starred, Done, projects, tags, locations or calendar days, and between rows. Archived
  lists wait under *Archived* in the sidebar.
- **Undo.** Every change is undoable with Ctrl+Z.

## Keyboard

the reference app's ⌘ is Ctrl here, since Super belongs to Hyprland. The bare keys work while the task
list has focus. Press `?` in the app for the full list.

| Keys | Action |
|---|---|
| `↑` `↓` (Shift extends) | Move the selection |
| `Return` / `Esc` | Edit the task / finish |
| `Space` | Quick Look |
| `t` / `y` / `Ctrl+T` | Due today / due tomorrow / start today |
| `d` / `k` | Defer… / Schedule… (type `tom 3pm`, `fri`, `in 2w`, `18d`) |
| `e` / `x` | Due time… / Duration… |
| `]` `[` and `.` `,` | Due date / start date ±1 day (Shift: a week) |
| `Alt+[` / `Alt+,` | Remove the due / start date |
| `0`–`3`, `=` / `-` | Priority none…high, raise / lower |
| `s` / `Ctrl+S` | Star / unstar |
| `l` / `Alt+L` | Tag… / remove tags |
| `m` | Move to list… |
| `c` | Collapse a project or checklist |
| `p` / `u` | Perform the task's action / open its links |
| `j` / `f` / `/` | Jump to a list / find a task / find a tag |
| `Delete` | Delete |
| `Ctrl+N` / `Ctrl+Alt+N` / `Ctrl+Shift+Alt+N` / `Alt+N` | New task / project / checklist / sub-task |
| `Ctrl+Shift+N` | New list |
| `Ctrl+.` | Complete or reopen |
| `Ctrl+D` | Duplicate |
| `Ctrl+1`…`Ctrl+5`, `Ctrl+0` | All, Today, Starred, Scheduled, Done; Inbox |
| `Ctrl+↑` / `Ctrl+↓` | Previous / next list |
| `Ctrl+Shift+↑` / `Ctrl+Shift+↓` | Move the task up / down |
| `Ctrl+Alt+↑` / `Ctrl+Alt+↓` | Collapse / expand every project |
| `Ctrl+F` / `Ctrl+Shift+O` | Search / Quick Open |
| `Ctrl+K` / `Ctrl+U` / `Ctrl+O` | Show scheduled / show paused / hide completed |
| `Ctrl+Alt+J` | Project view |
| `Ctrl+Alt+←` / `Ctrl+Alt+→` | Show or hide the Lists / Tags panels |
| `Ctrl+=` / `Ctrl+-` | Text size |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo |
| `Ctrl+C` / `Ctrl+V` | Copy tasks as text / paste lines as new tasks |
| `Shift+F5` | Back to the previously selected task |
| `Ctrl+Alt+F` | Search this list / all lists |
| `Ctrl+Alt+W` | New window |
| `Ctrl+S` | Sync now |
| `Ctrl+,` | Settings |
| `?` | All shortcuts |
| `q` / `Ctrl+Q` | Quit |

## Automation

Everything the reference app's Automation Suite offers is available outside the window too:

```sh
precrime ctl help                                   # list commands
precrime ctl query_tasks focus=today
precrime ctl create_task '{"title": "Call Sam tomorrow 3pm #family", "natural_language": true}'
claude mcp add precrime -- precrime mcp             # let Claude work with your tasks
xdg-open 'precrime://x-callback-url/add?task=Buy%20milk&due=tomorrow'
```

Changes made this way appear in the open window within a couple of seconds.

For a Waybar module with today's counts (like the reference app's Dock badge), add to
`~/.config/waybar/config.jsonc`:

```jsonc
"custom/precrime": { "exec": "precrime --waybar", "return-type": "json", "interval": 60, "on-click": "precrime" }
```

## Development

```sh
bin/build    # qmake6 + make into build/precrime
bin/test     # builds and runs the Qt Test suite offscreen
build/precrime --demo --screenshot shot.png   # render offscreen to a PNG
```

`src/` holds the C++ (store, scheduling rules, view building, the `backend` QObject) and
`src/qml/` the interface. `docs/analysis.md` records how the reference app behaves and the roadmap.

Precrime is an independent project and is not affiliated with the reference app or its makers.
