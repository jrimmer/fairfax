# Fairfax

A keyboard-first task manager for [Omarchy](https://omarchy.org), modelled closely on
[the reference app](https://example.com) for the Mac: three panels (lists, tasks, tags), focus lists that
pull tasks from everywhere, start dates that hide work until it's time, projects and
checklists, and single-key commands for everything you do to a task.

It follows the Omarchy theme live, light or dark, and keeps everything in a local SQLite
database, synced between machines through a git repository you choose.

## Install

```sh
bin/install        # builds, then makepkg -fsi; Fairfax appears in the launcher (Super+Space)
```

Or try it without installing, on a throwaway database full of sample tasks:

```sh
bin/build && build/fairfax --demo
```

Your tasks live in `~/.local/share/fairfax/fairfax.db`. `--db PATH` opens another file.

## On Omarchy

After installing, wire Fairfax into Omarchy with one command (undo with `--remove`):

```sh
fairfax setup omarchy
```

It adds, each in a clearly marked place:

- **Super+Shift+T** opens Quick Entry from anywhere (`--key "SUPER + ALT + T"` to pick
  another; it refuses a key that's already taken). Quick Entry and Settings float, centred.
- **Super+Space → Fairfax**: New Task, Today, Find a Task and Settings in the Omarchy menu.
- Fairfax starts in the background when you sign in, so alarms arrive and Quick Entry is
  instant.

Hyprland's part lives in `~/.config/hypr/fairfax.lua`, loaded by one line in
`hyprland.lua`; the menu entries are a marked block in
`~/.config/omarchy/extensions/omarchy-menu.jsonc`. Both files are backed up first.

## Quick Entry and the terminal

Quick Entry is one line to type plus a row of labelled pills (list, due, start, repeat,
alert, duration, tags, place, priority, star, attach) that always show what the task will
get. Click a pill, or Tab to it, and its choices open in one row underneath: pick with the
mouse, the arrow keys or a digit, and Escape closes the row. Type a task the way you'd say
it, and Fairfax picks out the details and fills the pills:
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

From any terminal, `frfx` (short for `fairfax`) does the same:

```sh
frfx Call Sam tomorrow 3pm #family !!    # add a task
frfx                                     # Today, numbered
frfx ls Personal                         # a list (or all, starred, scheduled, done)
frfx find dentist                        # search everything; the reference app search syntax works
frfx done 347                            # complete by number
frfx help
```

Tab completion for `fairfax` and `frfx` comes with the package for bash, zsh and fish: `frfx ls <Tab>`
offers your lists and `frfx done <Tab>` your open tasks.

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
  (Ctrl+Shift+Alt+F, or right-click a Smart List → Edit Search…) has a Search Presets rule
  builder, a row per clause (Priority is High, Tag is not set…) with Match All or Any, [−] and
  [+], and Alt+click on [+] (or right-click) for a sub-filter; it also sets a due-date range
  (between two dates, in the next N days to years, or no due date) and whether the list
  includes completed or scheduled tasks. `lists:`, `groups:` and `completed:` search every
  list, and `completed:` or `type: donetoday` find finished tasks; `type: scheduled` and
  `type: paused` find the tasks that are otherwise hidden. Parentheses can group
  whole filters, `tags: home OR (lists: Work type: starred)`, and Search Options has a
  group row, a This list / All lists scope, an Include Scheduled switch and a Sounds like
  switch that is remembered. The Find, Jump, Tag Search and Quick Open bars have an
  Exact words switch (Alt+E), remembered per bar, and ↑ / ↓ wrap at the ends; the search
  field's ✕ clears the search like Esc does; Defer also takes an offset such as
  `+3d`, `+1w` or `-2d` for the whole selection.
- **List options.** Right-click a list for Exclude/Include Completed and Delete all Tasks;
  Settings → Today & Lists chooses the counts on lists and whether a project view inherits
  the search it was opened from. Lists also sort by Note and Location.
- **More settings.** Settings → General sets the first day of the week (used by this-week
  searches, the mini calendar, date pickers and weekly repeats). Appearance has month
  dividers, always-show tag names, overdue in red and durations in headings; Tasks has the
  default due time, "only / not in Quick Entry" for the default due date, applying the
  completed rule to sub-tasks and turning Quick Look off; Today & Lists has started-task
  visibility, matched sub-tasks listed separately and sorting by list in list order or A to Z.
  Settings → Diagnostics shows versions, database path, schema, task counts and recent
  warnings, with Copy Diagnostics for bug reports.
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
  appear above Today; drag a task onto the bar to block out time for it. The arrows beside the
  caption browse other days (Today jumps back), clicking an event shows its details and link,
  and the expanded or collapsed state is remembered. Each calendar can be switched off or given
  a colour, and invitations you declined (matched by your email addresses) are struck through.
  The bar is hidden while searching.
- **Mini calendar.** Busy days as a heat map, dots or nothing (the ⌄ beside the calendar
  button under the lists, which also shows or hides it and can shade weekends). Click a day
  to filter to it, drag across days for a range, ⌥-click to add a task due that day, scroll
  to page through months, and drop tasks on a day to defer them (hold over the top or
  bottom week to page while dragging).
- **Arrange freely.** Drag lists, groups and Smart Lists to reorder them, drag tasks onto
  lists, Today, Starred, Done, projects, tags, locations or calendar days, and between rows.
  Archived lists wait under *Archived* in the sidebar; their tasks still show in Smart Lists
  and Done.
- **List colours and groups.** Right-click a list, Smart List or group for a palette or a
  custom colour; *Color Grouped Lists* recolours a whole group. *Edit List…* sets name,
  colour, group, Focus List exclusions, completed tasks and muted alerts. Hover a list for a
  `+` that adds a task to it. *Hide lists* (Settings > Today & Lists) takes lists out of the
  sidebar, All, Today and searches until you pick *Show Hidden Lists* in the sidebar's `+` menu.
  Selecting Done with 500+ completed tasks and auto-delete off asks once about auto-deleting.
- **Stars, mail and files.** Click a row's star to star or unstar it (right-click or hold for
  the priority menu). Drop a mail message (.eml) on a task and it becomes the task's action
  (or lands in the notes when it has one); drop one on empty space for a new task titled with
  the subject. Drag tasks out of the window to hand a file manager or mail composer an
  `.ics` (one dated task) or `.txt`, or use *Email These Tasks…* in the context menu.
- **Tray.** Fairfax sits in the bar's tray while it runs in the background; its menu is a
  quick view of today, with New Task, Find, Sync and Quit. The quick view, the commands and
  the badge colour can be configured in Settings > General.
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
| `s` / `Ctrl+Alt+S` | Star / toggle the star |
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
| `Ctrl+Shift+Alt+↑` / `Ctrl+Shift+Alt+↓` | Collapse / expand every list group |
| `Ctrl+F` / `Ctrl+Shift+O` | Search / Quick Open |
| `Ctrl+K` / `Ctrl+U` / `Ctrl+O` | Show scheduled / show paused / hide completed |
| `Ctrl+Alt+J` | Project view |
| `Ctrl+Alt+←` / `Ctrl+Alt+→` | Show or hide the Lists / Tags panels (remembered between launches) |
| `Ctrl+=` / `Ctrl+-` | Text size |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut tasks; paste restores them, or turns pasted lines into tasks (indented lines or `-` bullets become sub-tasks; pasting onto a project nests under it) |
| `Ctrl+E` `Ctrl+B` `Ctrl+G` `Ctrl+'` `Ctrl+/` `Ctrl+;` | Open the editor at the notes / action / location / alerts / dates / repeat |
| `Ctrl+J` | Toggle the Focus Filter |
| `Ctrl+Alt+P` / `Ctrl+Alt+C` | New project / checklist from the selection |
| `Ctrl+Alt+T` / `Ctrl+Shift+Alt+T` / `Ctrl+Alt+R` | New tag / new tag group / remove all tags |
| `Shift+F5` | Back to the previously selected task |
| `Ctrl+Alt+F` | Search this list / all lists |
| `Ctrl+Alt+W` | New window (open windows come back on the next launch) |
| `F11` | Full screen |
| `Ctrl+P` | Save the list as a PDF (sections, dates, notes) and open it for printing |
| `Ctrl+S` | Sync now |
| `Ctrl+,` | Settings |
| `?` | All shortcuts |
| `q` / `Ctrl+Q` | Quit |

## Moving in

- **From TickTick:** in TickTick's web app, Settings → Account → Generate Backup. Then in
  Fairfax, Settings → Import & Export → From TickTick…, or from a terminal:
  `fairfax import ~/Downloads/TickTick-backup-*.csv`. Lists, folders, tags, dates (in each
  task's time zone), repeats, reminders, priorities, checklists, sub-tasks, sections and
  completed history come across, and importing the same backup again adds only what's new
  (and fills in sections for tasks an earlier import brought in).
- **From the reference app:** `fairfax import tasks.db` (or a `.taskdb` backup).
- **From Things 3:** a JSON export (`fairfax import things.json`, or Settings → Import &
  Export → From Things…). Areas become lists, projects stay projects, checklist items stay
  checklists; "when" becomes the start date and the deadline the due date.
- **From OmniFocus:** File → Export → TaskPaper, then `fairfax import export.taskpaper` (or
  From OmniFocus…). Folders become list groups and projects lists; `@due`, `@defer`, `@done`,
  `@estimate`, `@flagged` and tags come across, and indented lines become notes.
- **From anything with iCalendar export:** `fairfax import tasks.ics`.

Every import is one step that Ctrl+Z undoes, and a backup is taken first.

## Mail to Fairfax

the reference app's "Mail to the reference app" turns email into tasks through a private hosted address. Fairfax has no
server, so mail reaches it from a mailbox you own, and the conversion is the same as the reference app's:

- The subject is the title (leading `Re:`, `Fwd:`, `AW:` and friends are trimmed), the plain-text
  body is the notes (up to 10,000 characters), and attachments and HTML formatting are dropped.
  An HTML-only message is turned into text.
- A body whose lines are numbered `1.` `2.` `3.` becomes a checklist. A signature ("-- ",
  "Sent from my iPhone", "Get Outlook for …") ends it.
- The subject can carry filing instructions, which are taken out of the title: `start(tomorrow)`,
  `due(friday 3pm)`, `priority(!!!)`, `list(Work)`, `tag(home, diy)` and
  `action(call, 0793273373)` (`call`, `message`, `url`, `visit`, `google`, `email`). A list that
  doesn't exist leaves the task in the destination list; an instruction Fairfax doesn't know
  stays in the title.
- A message becomes one task, once: its id is derived from the Message-ID, so the same message
  fetched again, or fetched on two machines that sync, never makes a second task, even after you
  delete the first. The search `type: capturedmail` finds tasks that arrived this way.

### Watching a mailbox (Settings → Mail)

Fairfax can watch an IMAP mailbox you own and import only the messages meant for it. The mailbox
can be your everyday one: the rest is never fetched, flagged or moved. Mail arrives at once when
the server supports IDLE, otherwise every few minutes; Ctrl+S and the sidebar's sync button also
fetch it. Nothing is registered with anyone: Fairfax ships no OAuth client of its own, so the
defaults below need none, and signing in with OAuth is only for a client you create yourself.

**Gmail** (the default), in three lines:

1. Turn on 2-Step Verification, make an app password at
   [myaccount.google.com/apppasswords](https://myaccount.google.com/apppasswords), and check that
   IMAP is on in Gmail's settings.
2. In Settings → Mail pick Gmail, enter your address and paste the app password (kept in your
   keyring, never in the database), then turn *Enable Mail to Fairfax* on.
3. Send or forward mail to `you+fairfax@gmail.com`. Gmail delivers it to your normal inbox;
   Fairfax takes only mail sent to that address, files it under the `Fairfax` label and out of
   your inbox, and leaves everything else alone.

*Gmail with your own OAuth client* (instead of the app password): in Google Cloud create an OAuth
client of type "Desktop app", and publish its consent screen to Production, or Google expires the
sign-in after seven days. Download its `client_secret.json` (the same file the `gog` CLI uses),
then in Settings → Mail → *Advanced: use your own OAuth client* choose *Import client JSON…* and
*Sign in with Google*. The refresh token goes to your keyring.

**Outlook.com and Microsoft 365** have no registration-free route, so Settings → Mail leads with
the options that need none:

- *Forward it*: an Outlook rule that redirects the mail you choose to your Gmail plus-address, or to
  any IMAP mailbox you connect in Fairfax.
- *DavMail*: [DavMail](https://davmail.sourceforge.net/) (in the AUR) is a local gateway that
  makes Outlook look like an IMAP server. Set it up, then press *Use DavMail* in Fairfax, which
  fills in `127.0.0.1`, port 1143, no TLS.
- *Your own client ID*: register an app in Microsoft Entra (App registrations) as a public client
  with the redirect `http://localhost` and the delegated `IMAP.AccessAsUser.All` permission, paste
  its Application (client) ID under *Use your own client ID*, and press *Sign in with Microsoft*.

**Any other IMAP server, or a bridge** (ProtonMail Bridge, DavMail, Dovecot, Fastmail…): choose
*Other*, fill in the server, port and security (TLS, STARTTLS, or None for a bridge on 127.0.0.1),
your user name and password.

What it picks, and what it does afterwards, is set under *Which mail* and *What happens*:

- *Only mail sent to* matches the To, Cc, Delivered-To and X-Original-To addresses exactly (a
  server's own search is by substring, so Fairfax re-checks each message). For Gmail it starts as
  your plus-address and follows the address until you edit it. *Only mail labelled* matches a Gmail
  label, or an IMAP keyword elsewhere. With both empty, every message in the folder becomes a task,
  and the pane warns you.
- *After importing*: *Move to* a folder or label (created when missing; the Gmail default),
  *Mark as read*, or *Leave as is*, which relies on the task's identity and a per-folder
  high-water mark so old mail is not fetched again. Messages that were already tasks get the same
  treatment. Messages over 25 MB are skipped and reported.
- *New tasks go to* a list (the Inbox by default), and *Read dates and tags in the subject* adds
  Quick Entry's parsing to the reference app's filing instructions, which always work.

From a script, `procmail`, Sieve, `fetchmail` or a ProtonMail Bridge setup:

    fairfax import-mail message.eml             # or several files
    some-fetcher | fairfax import-mail --list Work
    fairfax import-mail --maildir ~/Mail/fairfax     # everything in new/, moved to cur/ as seen
    fairfax import-mail --natural-language ...       # also read "tomorrow 3pm #home !!" in the subject

It prints a line per message (`Added "Title" to List` or `Skipped: duplicate`) and exits 0 when
everything was added or was a duplicate, 1 for a message it couldn't read, and 2 for bad usage.
Like the other terminal commands it works while the app is open, which shows the new tasks within
a couple of seconds.

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
fairfax ctl help                                    # list commands
fairfax ctl query_tasks focus=today
fairfax ctl create_task '{"title": "Call Sam tomorrow 3pm #family", "natural_language": true}'
claude mcp add fairfax -- fairfax mcp               # let Claude work with your tasks
xdg-open 'fairfax://x-callback-url/add?task=Buy%20milk&due=tomorrow'
```

Changes made this way appear in the open window within a couple of seconds.

`query_tasks` takes `list_id`, `list_group_id`, `smart_list_id` or `focus`, the date windows
`due_on_or_after`, `due_on_or_before`, `start_on_or_after` and `start_on_or_before`, and a
`sort` (`{"field": "due_date", "direction": "descending"}`; fields title, due_date,
start_date, priority, duration, created, modified, completed). `create_task` and
`update_task` also take `recurrence` (`{"frequency": "weekly", "weekdays": ["mon"],
"end_rule": {...}}`, or `{"frequency": "none"}` to clear it) and `location_update`
(`{"mode": "append", "location_ids": [...]}`). Alarms have their own commands:
`list_alarms`, `add_alarm` (relative with `offset_seconds`, absolute with `at`, or
`days_before` with `time_of_day`) and `remove_alarm`.

The URL scheme follows the reference app's `twodo://` one under `fairfax://x-callback-url/<action>`:
`add` (with `task`, `note`, `forlist`, `forparentname`, `due`, `duetime`, `start`, `repeat`,
`tags`, `locations`, `subtasks`, `priority`, `starred`, `action`, `picture` and `audio` as
base64, `ignoredefaults`, `edit`, `usequickentry`, `saveinclipboard`), `paste`, `addnewtask`,
`search`, `showall`/`showtoday`/`showstarred`/`showscheduled`, `showlist`, `showtask`,
`completetasks`, `gettaskid` and `launch`. `(clipboard)` works as the value of `task`,
`note`, `tags`, `locations` and `subtasks`; `x-success` is opened with the new task's uid,
and `x-error` with `errorCode` and `errorMessage`.

Alarms go off through the resident Fairfax process. The Waybar module below turns red
(class `no-daemon`, text `!`) when alarms are waiting and no Fairfax is running to deliver
them. Notifications show the list, the due time and a teaser of the notes, carry a button
for the task's action (Call, Email, Open Link…), and stay silent while mako or dunst is in
Do Not Disturb.

The desktop file has a **New Window** action as well (`fairfax --new-window`).

If you use Waybar rather than Omarchy's own bar, a module with today's counts (like the reference app's
Dock badge) goes in `~/.config/waybar/config.jsonc`:

```jsonc
"custom/fairfax": { "exec": "fairfax --waybar", "return-type": "json", "interval": 60, "on-click": "fairfax" }
```

Settings → Alerts → Badge shows picks what it, and the number on the tray icon, counts:
Overdue + Today, Overdue, Today or None.

## Development

```sh
bin/build    # qmake6 + make into build/fairfax
bin/test     # builds and runs the Qt Test suite offscreen
bin/e2e      # builds and runs the end-to-end UI tests offscreen (see below)
build/fairfax --demo --screenshot shot.png    # render offscreen to a PNG
QT_QPA_PLATFORM=offscreen build/fairfax --demo --selftest    # drive the UI with scripted keys; fails on any QML error
```

`bin/e2e` boots the real window on a temporary database and drives it the way a user does
(mouse clicks, double-clicks, typing, shortcuts), then checks the store or the screen. Each
scenario in `tests/e2e/` quotes the the reference app manual page it comes from, and a layout check fails when
any visible item sticks out of its dialog, popup or window. Pass Qt Test arguments through
(`bin/e2e doubleClickOpensEditorWithTitle`); `FAIRFAX_E2E=1 bin/test` runs it after the unit tests.

`src/` holds the C++ (store, scheduling rules, view building, the `backend` QObject) and
`src/qml/` the interface. `docs/analysis.md` records how the reference app behaves and the roadmap.

Fairfax is an independent project and is not affiliated with the reference app or its makers.
