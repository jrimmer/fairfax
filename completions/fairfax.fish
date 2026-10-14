# fish completion for fairfax.

set -l commands add ctl mcp import import-mail setup
complete -c fairfax -f
complete -c fairfax -n "not __fish_seen_subcommand_from $commands" -a add -d 'Add a task (natural language)'
complete -c fairfax -n "not __fish_seen_subcommand_from $commands" -a ctl -d 'Run an automation command'
complete -c fairfax -n "not __fish_seen_subcommand_from $commands" -a mcp -d 'Run the MCP server on stdio'
complete -c fairfax -n "not __fish_seen_subcommand_from $commands" -a import -d 'Import TickTick, the reference app or iCalendar'
complete -c fairfax -n "not __fish_seen_subcommand_from $commands" -a import-mail -d 'Turn emails into tasks (Mail to Fairfax)'
complete -c fairfax -n "not __fish_seen_subcommand_from $commands" -a setup -d 'Wire Fairfax into Omarchy'
complete -c fairfax -n "__fish_seen_subcommand_from ctl" -a "help url (fairfax --complete ctl 2>/dev/null)"
complete -c fairfax -n "__fish_seen_subcommand_from import" -F -a "(__fish_complete_suffix .csv .ics .db .taskdb)"
complete -c fairfax -n "__fish_seen_subcommand_from import-mail" -l list -x -a "(fairfax --complete lists 2>/dev/null)" -d 'Destination list'
complete -c fairfax -n "__fish_seen_subcommand_from import-mail" -l natural-language -d 'Read dates and tags from the subject'
complete -c fairfax -n "__fish_seen_subcommand_from import-mail" -l maildir -r -a "(__fish_complete_directories)" -d 'Import a Maildir'
complete -c fairfax -n "__fish_seen_subcommand_from import-mail" -F
complete -c fairfax -n "__fish_seen_subcommand_from setup; and not __fish_seen_subcommand_from omarchy" -a omarchy
complete -c fairfax -n "__fish_seen_subcommand_from omarchy" -l remove -d 'Undo the Omarchy setup'
complete -c fairfax -n "__fish_seen_subcommand_from omarchy" -l key -x -d 'Quick Entry key'
complete -c fairfax -l db -r -F -d 'Use this database file'
complete -c fairfax -l demo -d 'Throwaway database with sample tasks'
complete -c fairfax -l quick -d 'Open Quick Entry'
complete -c fairfax -l background -d 'Start without a window, for alarms'
complete -c fairfax -l open -d 'Open Quick Open'
complete -c fairfax -l settings -d 'Open Settings'
complete -c fairfax -l new-window -d 'Open another window'
complete -c fairfax -l source -x -a 'all today starred scheduled done' -d 'Open this list first'
complete -c fairfax -l theme-dir -r -a '(__fish_complete_directories)' -d 'Theme directory'
complete -c fairfax -l screenshot -r -F -d 'Render offscreen to a PNG'
complete -c fairfax -l selftest -d 'Drive the UI with scripted keys'
complete -c fairfax -l help -d 'Show help'
complete -c fairfax -l version -d 'Show version'
