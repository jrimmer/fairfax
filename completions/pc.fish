# fish completion for pc, Precrime's terminal shorthand.

set -l commands ls list find done undone open add help today
complete -c pc -f
complete -c pc -n "not __fish_seen_subcommand_from $commands" -a ls -d 'Show a list (Today when omitted)'
complete -c pc -n "not __fish_seen_subcommand_from $commands" -a find -d 'Search everything'
complete -c pc -n "not __fish_seen_subcommand_from $commands" -a done -d 'Complete tasks by number'
complete -c pc -n "not __fish_seen_subcommand_from $commands" -a undone -d 'Reopen tasks by number'
complete -c pc -n "not __fish_seen_subcommand_from $commands" -a open -d 'Open Precrime'
complete -c pc -n "not __fish_seen_subcommand_from $commands" -a add -d 'Add a task starting with a command word'
complete -c pc -n "not __fish_seen_subcommand_from $commands" -a help -d 'Show usage'
complete -c pc -n "__fish_seen_subcommand_from ls list" -a "all starred scheduled done today (precrime --complete lists 2>/dev/null)"
complete -c pc -n "__fish_seen_subcommand_from done" -a "(precrime --complete tasks 2>/dev/null)"
complete -c pc -n "__fish_seen_subcommand_from undone" -a "(precrime --complete done 2>/dev/null)"
