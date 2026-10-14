# fish completion for frfx, Fairfax's terminal shorthand.

set -l commands ls list find done undone open add help today
complete -c frfx -f
complete -c frfx -n "not __fish_seen_subcommand_from $commands" -a ls -d 'Show a list (Today when omitted)'
complete -c frfx -n "not __fish_seen_subcommand_from $commands" -a find -d 'Search everything'
complete -c frfx -n "not __fish_seen_subcommand_from $commands" -a done -d 'Complete tasks by number'
complete -c frfx -n "not __fish_seen_subcommand_from $commands" -a undone -d 'Reopen tasks by number'
complete -c frfx -n "not __fish_seen_subcommand_from $commands" -a open -d 'Open Fairfax'
complete -c frfx -n "not __fish_seen_subcommand_from $commands" -a add -d 'Add a task starting with a command word'
complete -c frfx -n "not __fish_seen_subcommand_from $commands" -a help -d 'Show usage'
complete -c frfx -n "__fish_seen_subcommand_from ls list" -a "all starred scheduled done today (fairfax --complete lists 2>/dev/null)"
complete -c frfx -n "__fish_seen_subcommand_from done" -a "(fairfax --complete tasks 2>/dev/null)"
complete -c frfx -n "__fish_seen_subcommand_from undone" -a "(fairfax --complete done 2>/dev/null)"
