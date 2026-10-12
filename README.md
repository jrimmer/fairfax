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
main window, which shows what it found as capsules, with a "Stop detecting" chip to turn
it off for that task. Pasting several lines into Quick Entry keeps the first as the title
and moves the rest into the notes; a pasted link becomes the title and a URL action.
The new-task list comes from Settings → Tasks → Default list (a list, the first list, or
the one you used last), and "Always use the default list" sends tasks added from the main
window there instead of to the list being viewed. In Quick Entry, Return saves, Shift+Return saves and starts another (so does
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
  `"exact phrase"`, `OR`, `-word`) and save it as a Smart List. Search Options
  (Ctrl+Shift+Alt+F, or right-click a Smart List → Edit Search…) also sets a due-date range
  (between two dates, in the next N days to years, or no due date) and whether the list
  includes completed or scheduled tasks. `lists:`, `groups:` and `completed:` search every
  list, and `completed:` or `type: donetoday` find finished tasks. Parentheses can group
  whole filters, `tags: home OR (lists: Work type: starred)`, and Search Options has a
  group row, a This list / All lists scope, an Include Scheduled switch and a Sounds like
  switch that is remembered. The Find, Jump, Tag Search and Quick Open bars have an
  Exact words switch (Alt+E), remembered per bar; Defer also takes an offset such as
  `+3d`, `+1w` or `-2d` for the whole selection.
- **List options.** Right-click a list for Exclude/Include Completed and Delete all Tasks;
  Settings → Today & Lists chooses the counts on lists and whether a project view inherits
  the search it was opened from. Lists also sort by Note and Location.
- **Focus Filter.** The sun in the sort bar narrows any list to tasks that are overdue,
  due today, this week, high priority, starred and so on, each list remembering its own.
- **Repeats.** Completing a repeating task keeps a done copy and moves it to the next
  occurrence, from its due date or from the day you finished it.
- **The editor.** Typeable dates with a calendar (click the field or press ↓; ⌥1–⌥4 for Today,
  Tomorrow, the day after, a week), a full repeat editor (every N days/weeks/months/years,
  chosen weekdays, "2nd Tuesday", "last Friday", ending after N times or on a date, from
  completion), sub-tasks you add with Return, alerts (presets, N days before at a time, or a
  date and time; each Message or Message with Sound; click an alert chip to change it),
  locations, a picture and audio notes (record with the mic button, or drop
  files), and notes with live Markdown, clickable links and `@{Task}` links to other tasks (type `@{`
  for a picker of tasks, Return inserts the link).
- **Alerts.** Settings → Alerts has Default Reminders (an automatic alert for tasks with a due
  time, and for date-only tasks, added when a due date is set and the task has none), a repeat
  interval for sound alerts (Never, 2, 5 or 10 minutes), and right-click a list → Mute Alerts.
  Alerts count from the start date when a task has no due date. Notifications offer Snooze for
  5 minutes, 15 minutes, 1 hour, this evening or tomorrow morning (the Default Due Time when
  set), and close themselves when the task is completed, deleted or re-dated.
- **Export.** Ctrl+Shift+E opens the export panel (choose lists, All / Not Done / Done, CSV or
  .ics); the task right-click menu has Add to Calendar (opens the task as an event in your
  calendar app), Save as Event (.ics)… and Export Selection….
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
| `Ctrl+Shift+[` | Remove the due time |
| `0`–`3`, `=` / `-` | Priority none…high, raise / lower |
| `s` / `Ctrl+S` | Star / unstar |
| `l` / `Alt+L` | Tag… / Untag… (choose which tags to remove) |
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
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut tasks; paste restores them, or turns pasted lines into tasks (indented lines or `-` bullets become sub-tasks; pasting onto a project nests under it) |
| `Ctrl+E` `Ctrl+B` `Ctrl+G` `Ctrl+'` `Ctrl+/` `Ctrl+;` | Open the editor at the notes / action / location / alerts / dates / repeat |
| `Ctrl+J` | Toggle the Focus Filter |
| `Ctrl+Alt+P` / `Ctrl+Alt+C` | New project / checklist from the selection |
| `Ctrl+Alt+T` / `Ctrl+Shift+Alt+T` / `Ctrl+Alt+R` | New tag / new tag group / remove all tags |
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

## Sync and backups

Settings → Sync offers four ways to keep machines in step, all of which can run at once:
a git repository, a plain folder (for Syncthing, Dropbox or Nextcloud's desktop client),
a WebDAV folder (Nextcloud, ownCloud and the like; the password lives in your keyring),
and Todoist. The folder and WebDAV providers use the same per-task files as git sync; the
newest edit of each task wins. The first time a machine meets a folder or server that
already holds tasks, it asks whether to Merge, Replace Local (a backup is made first) or
Replace Server. "Sync automatically" (on by default) can be turned off to sync only with
`Ctrl+S`. The sidebar footer shows when sync last succeeded for whichever providers are set
up, and `Ctrl+S` or its button syncs them all.

Right-click a list and choose **Don't Sync This List** to keep it, and its tasks, on this
machine only; every sync provider skips it.

In Settings → Backups, each backup can be restored whole, **Lists…** to restore only chosen
lists (with their tasks, tags, alerts and attachments) as new lists in a "Restored" group
without touching what you have, saved elsewhere with **Save a Copy…**, or deleted.

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
