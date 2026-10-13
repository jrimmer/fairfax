# bash completion for precrime and pc.

_precrime_values() { precrime --complete "$1" 2>/dev/null; }

_pc() {
    local cur=${COMP_WORDS[COMP_CWORD]} cmd=${COMP_WORDS[1]}
    if (( COMP_CWORD == 1 )); then
        COMPREPLY=($(compgen -W "ls find done undone open add help today" -- "$cur"))
        return
    fi
    case $cmd in
        ls|list)
            local IFS=$'\n'
            COMPREPLY=($(compgen -W $'all\nstarred\nscheduled\ndone\ntoday\n'"$(_precrime_values lists)" -- "$cur"))
            ;;
        done)
            local IFS=$'\n'
            COMPREPLY=($(compgen -W "$(_precrime_values tasks | cut -f1)" -- "$cur"))
            ;;
        undone)
            local IFS=$'\n'
            COMPREPLY=($(compgen -W "$(_precrime_values done | cut -f1)" -- "$cur"))
            ;;
    esac
}

_precrime() {
    local cur=${COMP_WORDS[COMP_CWORD]} prev=${COMP_WORDS[COMP_CWORD-1]}
    local options="--help --version --db --demo --quick --background --open --new-window --settings --source --theme-dir --screenshot --size --edit --selftest"
    case $prev in
        --db|--theme-dir|--screenshot) COMPREPLY=($(compgen -f -- "$cur")); return ;;
        --source) COMPREPLY=($(compgen -W "all today starred scheduled done" -- "$cur")); return ;;
        import) COMPREPLY=($(compgen -f -X '!*.@(csv|ics|db|taskdb)' -- "$cur") $(compgen -d -- "$cur")); return ;;
        --maildir) COMPREPLY=($(compgen -d -- "$cur")); return ;;
        --list) local IFS=$'\n'; COMPREPLY=($(compgen -W "$(_precrime_values lists)" -- "$cur")); return ;;
        ctl) COMPREPLY=($(compgen -W "help url $(_precrime_values ctl)" -- "$cur")); return ;;
        setup) COMPREPLY=($(compgen -W "omarchy" -- "$cur")); return ;;
        omarchy) COMPREPLY=($(compgen -W "--remove --key --help" -- "$cur")); return ;;
    esac
    if (( COMP_CWORD == 1 )); then
        COMPREPLY=($(compgen -W "add ctl mcp import import-mail setup $options" -- "$cur"))
    elif [[ ${COMP_WORDS[1]} == import-mail ]]; then
        COMPREPLY=($(compgen -W "--list --natural-language --maildir" -- "$cur") $(compgen -f -X '!*.eml' -- "$cur"))
    else
        COMPREPLY=($(compgen -W "$options" -- "$cur"))
    fi
}

shopt -s extglob
complete -F _precrime precrime
complete -F _pc pc
