# Precrime

A keyboard-first task manager for [Omarchy](https://omarchy.org), modelled closely on
[the reference app](https://example.com) for the Mac: three panels (lists, tasks, tags), focus lists that
pull tasks from everywhere, start dates that hide work until it's time, projects and
checklists, and single-key commands for everything you do to a task.

It follows the Omarchy theme live, light or dark, and keeps everything in a local SQLite
database. Sync (Todoist first) comes later.

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
| `Ctrl+Alt+↑` / `Ctrl+Alt+↓` | Move the task up / down |
| `Ctrl+F` / `Ctrl+Shift+O` | Search / Quick Open |
| `Ctrl+K` / `Ctrl+U` / `Ctrl+O` | Show scheduled / show paused / hide completed |
| `Ctrl+Alt+J` | Project view |
| `Ctrl+Alt+←` / `Ctrl+Alt+→` | Show or hide the Lists / Tags panels |
| `Ctrl+=` / `Ctrl+-` | Text size |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo |
| `?` | All shortcuts |
| `q` / `Ctrl+Q` | Quit |

## Development

```sh
bin/build    # qmake6 + make into build/precrime
bin/test     # builds and runs the Qt Test suite offscreen
build/precrime --demo --screenshot shot.png   # render offscreen to a PNG
```

`src/` holds the C++ (store, scheduling rules, view building, the `backend` QObject) and
`src/qml/` the interface. `docs/analysis.md` records how the reference app behaves and the roadmap.

Precrime is an independent project and is not affiliated with the reference app or its makers.
