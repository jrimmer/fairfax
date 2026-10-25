# Accessibility

Fairfax is designed to be usable with a keyboard alone and with a screen reader. This
chapter says what to expect and how to tune the window for your eyes and hands.

## Screen readers

Fairfax works with **Orca** and with any other screen reader that Qt applications report
to (the desktop's accessibility service must be running, as it is on most desktops that
offer a screen reader).

What it gives you:

- Every button, row, field, menu entry and dialog has a **name and a role**, so your
  screen reader says what each thing is.
- The task list, the sidebar and the calendar answer the keyboard, and Orca reads them as
  you move: the tasks, the sidebar counts, the editor's fields, the Settings panes, the
  rules in the search builder, and each popup's title.
- Settings has a list of panes that reads as a tab list, and each setting's note is read as
  its description.

### What it says as you work

Changes that you might otherwise miss are announced as they happen:

- Completing or reopening a task: "Completed: Call Sam", or "Completed 3 tasks" when you
  have several selected.
- Moving a task to another list, deleting, undoing and redoing.
- Copying and pasting tasks.

A **sync or mail failure** interrupts with an assertive message ("Sync failed: …",
"Mail: …"). The warning mark in the sidebar is not visible to a screen reader, so the
message makes sure you still know.

## Using only the keyboard

Everything in Fairfax can be done from the keyboard.

- The task list answers the arrow keys. **Return** edits, **Esc** finishes, **Space** opens
  Quick Look.
- Bare letters change a selected task in place (**t** for due today, **s** for a star,
  **l** to tag it), and **Ctrl** and **Alt** shortcuts work anywhere. The full list is in
  [The keyboard](07-keyboard.md), or press **?** in the app.
- Quick Entry's pills can be reached with **Tab**, and their choices picked with the arrow
  keys or a digit.
- **Esc** closes popups and dialogs, including Settings.
- The sidebar, the calendar and the Settings window all answer the keyboard. Move between
  the Settings panes and their fields with **Tab** and the arrow keys.
- If a shortcut gets in your way, or you need one that is easier on your hands, change it
  in **Settings → Shortcuts**. Bare keys do nothing while you are typing in a field, so
  typing is never mistaken for a command.

### Focus rings

A **focus ring** shows which control has the keyboard. It appears only for keyboard
focus, so it is there when you need it and does not clutter the window when you use the
mouse.

## Making things easier to see

- **Text size.** Press **Ctrl+=** to make text larger and **Ctrl+-** to make it smaller,
  from anywhere in the window. The same control is the **Text size** slider in
  **Settings → Appearance**.
- **Light or dark.** In **Settings → Appearance**, *System* follows your Omarchy theme
  live. Choose *Dark* or *Light* to force one, whichever is easier to read.
- **Row spacing.** **Task rows** can be *Compressed*, *Normal* or *Extended*. Extended gives
  each row more room.
- **Content width.** **Content width** keeps rows from stretching across a very wide
  screen, so your eyes do not have to travel so far.
- **Names, not icons.** Turn on **Always show tag names** and **Always show list names** so
  you are not relying on small icons or holding Alt.
- **Overdue.** Overdue tasks show in red, but they also say how late they are in words
  ("Overdue by 2 days"), so colour is never the only signal. You can turn red off with
  **Show overdue tasks in red**.
- **Friendly dates.** Words like *Today* and *Tomorrow* are easier to take in than dates.
  It is the default; the setting is **Use friendly dates**.

## Quieter alerts, or louder ones

- Alerts can be a plain **Message** or **Message with Sound**. Choose per alert in the
  task editor.
- To make a sound alert hard to miss, set **Sound alerts repeat until dismissed** in
  **Settings → Alerts**.
- Notifications go through your desktop's notification service, so your usual settings for
  it (including Do Not Disturb) apply.

## Tell us what is not working

If something is hard to use with your screen reader or keyboard, we want to know. Open
**Settings → Diagnostics**, choose **Copy Diagnostics**, and include it with a description
at [github.com/jrimmer/fairfax](https://github.com/jrimmer/fairfax).

Fairfax is currently in English only.
