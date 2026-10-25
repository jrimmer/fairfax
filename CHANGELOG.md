# Changelog

## 1.0.2 (2026-10-25)

### Faster

- Opening a long list is about 20 times faster: an 80-task list in a tall
  window now opens in about a fifth of a second instead of several seconds.
  Today, All and search got the same treatment.
- A performance suite now runs with every test pass, with a time budget for
  switching lists, opening Today and All, searching, opening Quick Entry, the
  task editor and Settings, and completing a task.

### My Day

- Clearer wording: the sections are Overdue, Today and Could do today; the
  header reads "4h 25m of tasks · 4h 45m left in work day" (or "work day over");
  the end-of-day review is Wrap up day, with Not done yet and "Done today: 3
  tasks · 1h 10m of 4h 25m", and Back to plan leaves it.
- The review's summary line lines up with the title again.

### Fixes

- "Show completed tasks for: Never" now means it: a ticked task leaves the
  view about a second later. Ctrl+Z, or unticking it, brings it back.
- Settings opens each pane at its top instead of where the last pane was
  scrolled to.
- The sidebar's "Synced 2 min ago" no longer runs over the buttons beside it;
  it shortens to "2m" when space is tight.
- Sync files write empty lists as `[]`, matching common JSON writers, so files
  another program writes aren't reformatted.

### Installer

- The one-line installer now comes from the release itself, not the latest
  code, so it's as stable as the packages it installs.
- On Arch, it installs as root without needing sudo (containers, minimal
  servers), and says clearly when neither root nor sudo is available.

## 1.0.1 (2026-10-24)

- The sidebar's sync label keeps clear of the buttons beside it.

## 1.0.0 (2026-10-24)

The first public release, for Omarchy and other Linux desktops.

- Three panels, focus lists, start dates, projects and checklists, Smart Lists
  and a single key for every task command.
- My Day, a daily plan built from the dates your tasks already have.
- Sync through a git repository, a folder, WebDAV, CalDAV or Todoist, merging
  edits field by field so changes from two places both survive.
- Mail to tasks from a mailbox you own.
- Imports from TickTick, Things, OmniFocus, iCalendar and task databases,
  including zipped backups.
- `fairfax ctl` JSON commands, an MCP server and `fairfax://` links for
  scripts and agents; a documented sync format for programs that read or
  write tasks: https://jasonrimmer.com/fairfax/sync-format/
