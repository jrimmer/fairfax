-- Fairfax, added by `fairfax setup omarchy`. Remove with
-- `fairfax setup omarchy --remove`, or edit freely.

-- Quick Entry from anywhere: press it again to save and start the next task.
o.bind("SUPER + ALT + J", "Fairfax quick entry", "fairfax --quick")

-- Quick Entry and Settings float, centred, like the panels. Quick Entry
-- sizes itself to its contents.
o.window({ class = "^fairfax$", title = "^Fairfax Quick Entry$" }, { float = true, center = true })
o.window({ class = "^fairfax$", title = "^Fairfax Settings$" }, { float = true, center = true, size = { 760, 580 } })


-- Keep Fairfax running in the background after sign-in, so alarms arrive
-- and Quick Entry opens instantly.
o.launch_on_start("fairfax --background")
