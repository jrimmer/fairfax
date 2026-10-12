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

## On Omarchy

After installing, wire Precrime into Omarchy with one command (undo with `--remove`):

```sh
precrime setup omarchy
```

It adds, each in a clearly marked place:

- **Super+Shift+T** opens Quick Entry from anywhere (`--key "SUPER + ALT + T"` to pick
  another; it refuses a key that's already taken). Quick Entry and Settings float, centred.
- **Super+Space → Precrime**: New Task, Today, Find a Task and Settings in the Omarchy menu.
- Precrime starts in the background when you sign in, so alarms arrive and Quick Entry is
  instant.

Hyprland's part lives in `~/.config/hypr/precrime.lua`, loaded by one line in
`hyprland.lua`; the menu entries are a marked block in
`~/.config/omarchy/extensions/omarchy-menu.jsonc`. Both files are backed up first.

## Quick Entry and the terminal

Quick Entry is one line to type plus a row of labelled pills (list, due, start, repeat,
alert, duration, tags, place, priority, star, attach) that always show what the task will
get. Click a pill, or Tab to it, and its choices open in one row underneath: pick with the
mouse, the arrow keys or a digit, and Escape closes the row. Type a task the way you'd say
it, and Precrime picks out the details and fills the pills:
`Call Sam tomorrow at 3pm #family !! every Monday [30m] /work/`. Dates, start dates
(`starting Friday`), ranges (`from Monday until Friday`), repeats, `alert 30m`,
`[durations]`, `!`/`!!`/`!!!` priority, `*` star, `#tags`, `/list/` and `@ Place` all work,
and `"quotes"` keep words literal. The same parsing applies to a new task's title in the
main window. In Quick Entry, Return saves, Shift+Return saves and starts another (so does
pressing the key again), and Ctrl+Return saves and opens the task.

From any terminal, `pc` (short for `precrime`) does the same:

```sh
pc Call Sam tomorrow 3pm #family !!   # add a task
pc                                    # Today, numbered
pc ls Personal                        # a list (or all, starred, scheduled, done)
pc find dentist                       # search everything; the reference app search syntax works
pc done 347                           # complete by number
pc help
```

Tab completion for `precrime` and `pc` comes with the package for bash, zsh and fish: `pc ls <Tab>`
offers your lists and `pc done <Tab>` your open tasks.

## How it works

- **Focus lists.** *All*, *Today*, *Starred*, *Scheduled* and *Done* gather tasks from every list.
  *Today* holds what's due today, what's overdue, and what you've started; a task added
  while *Today* is open is due today, one added in *Starred* is starred.
- **Start and due dates.** A future start date hides a task (it waits in *Scheduled*) until
  that day. A duration with a due time makes the task show up ahead of its deadline.
- **Projects and checklists.** A project's tasks keep their own dates and appear on their own
  in the focus lists; a checklist's items ride along with the checklist.
- **Sections.** Split a list into headings (right-click a list → New Section, or + → New
  Section): drag tasks onto a heading to move them, double-click a heading to rename it.
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
- **Mini calendar.** Busy days as a heat map, dots or nothing (the ⌄ beside the calendar
  button under the lists, which also shows or hides it and can shade weekends). Click a day
  to filter to it, drag across days for a range, ⌥-click to add a task due that day, scroll
  to page through months, and drop tasks on a day to defer them.
- **Arrange freely.** Drag lists and groups to reorder them, drag tasks onto lists, Today,
  Starred, Done, projects, tags, locations or calendar days, and between rows. Archived
  lists wait under *Archived* in the sidebar.
- **Tray.** Precrime sits in the bar's tray while it runs in the background; its menu is a
  quick view of today, with New Task, Find, Sync and Quit.
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

## Moving in

- **From TickTick:** in TickTick's web app, Settings → Account → Generate Backup. Then in
  Precrime, Settings → Import & Export → From TickTick…, or from a terminal:
  `precrime import ~/Downloads/TickTick-backup-*.csv`. Lists, folders, tags, dates (in each
  task's time zone), repeats, reminders, priorities, checklists, sub-tasks, sections and
  completed history come across, and importing the same backup again adds only what's new
  (and fills in sections for tasks an earlier import brought in).
- **From the reference app:** `precrime import tasks.db` (or a `.taskdb` backup).
- **From anything with iCalendar export:** `precrime import tasks.ics`.

Every import is one step that Ctrl+Z undoes, and a backup is taken first.

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

If you use Waybar rather than Omarchy's own bar, a module with today's counts (like the reference app's
Dock badge) goes in `~/.config/waybar/config.jsonc`:

```jsonc
"custom/precrime": { "exec": "precrime --waybar", "return-type": "json", "interval": 60, "on-click": "precrime" }
```

## Development

```sh
bin/build    # qmake6 + make into build/precrime
bin/test     # builds and runs the Qt Test suite offscreen
build/precrime --demo --screenshot shot.png   # render offscreen to a PNG
QT_QPA_PLATFORM=offscreen build/precrime --demo --selftest   # drive the UI with scripted keys; fails on any QML error
```

`src/` holds the C++ (store, scheduling rules, view building, the `backend` QObject) and
`src/qml/` the interface. `docs/analysis.md` records how the reference app behaves and the roadmap.

Precrime is an independent project and is not affiliated with the reference app or its makers.
