# fish completion for precrime.

set -l commands add ctl mcp import setup
complete -c precrime -f
complete -c precrime -n "not __fish_seen_subcommand_from $commands" -a add -d 'Add a task (natural language)'
complete -c precrime -n "not __fish_seen_subcommand_from $commands" -a ctl -d 'Run an automation command'
complete -c precrime -n "not __fish_seen_subcommand_from $commands" -a mcp -d 'Run the MCP server on stdio'
complete -c precrime -n "not __fish_seen_subcommand_from $commands" -a import -d 'Import TickTick, the reference app or iCalendar'
complete -c precrime -n "not __fish_seen_subcommand_from $commands" -a setup -d 'Wire Precrime into Omarchy'
complete -c precrime -n "__fish_seen_subcommand_from ctl" -a "help url (precrime --complete ctl 2>/dev/null)"
complete -c precrime -n "__fish_seen_subcommand_from import" -F -a "(__fish_complete_suffix .csv .ics .db .taskdb)"
complete -c precrime -n "__fish_seen_subcommand_from setup; and not __fish_seen_subcommand_from omarchy" -a omarchy
complete -c precrime -n "__fish_seen_subcommand_from omarchy" -l remove -d 'Undo the Omarchy setup'
complete -c precrime -n "__fish_seen_subcommand_from omarchy" -l key -x -d 'Quick Entry key'
complete -c precrime -l db -r -F -d 'Use this database file'
complete -c precrime -l demo -d 'Throwaway database with sample tasks'
complete -c precrime -l quick -d 'Open Quick Entry'
complete -c precrime -l background -d 'Start without a window, for alarms'
complete -c precrime -l open -d 'Open Quick Open'
complete -c precrime -l settings -d 'Open Settings'
complete -c precrime -l new-window -d 'Open another window'
complete -c precrime -l source -x -a 'all today starred scheduled done' -d 'Open this list first'
complete -c precrime -l theme-dir -r -a '(__fish_complete_directories)' -d 'Theme directory'
complete -c precrime -l screenshot -r -F -d 'Render offscreen to a PNG'
complete -c precrime -l selftest -d 'Drive the UI with scripted keys'
complete -c precrime -l help -d 'Show help'
complete -c precrime -l version -d 'Show version'
