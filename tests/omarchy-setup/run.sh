#!/bin/bash
# Tests for scripts/fairfax-omarchy-setup, against a throwaway HOME: setup and
# --remove, and that --remove only undoes what setup still owns (an edited
# module or menu block, or a line you wrote, stays). Hermetic: no Hyprland,
# no omarchy command, no network.
#
#   tests/omarchy-setup/run.sh
set -euo pipefail

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
SETUP="$HERE/../../scripts/fairfax-omarchy-setup"
FIX="$HERE/fixtures-1.0"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"
# `omarchy menu keybindings --print` prints $work/listing (empty: nothing taken).
printf '#!/bin/sh\ncat "%s/listing" 2>/dev/null\nexit 0\n' "$work" >"$work/bin/omarchy"
chmod +x "$work/bin/omarchy"

failures=0
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }
pass() { echo "ok   $*"; }

home='' hypr='' module='' main='' menu=''
fresh() {
  home="$work/home-$1"
  hypr="$home/.config/hypr"
  module="$hypr/fairfax.lua"
  main="$hypr/hyprland.lua"
  menu="$home/.config/omarchy/extensions/omarchy-menu.jsonc"
  mkdir -p "$hypr" "$(dirname "$menu")"
  printf -- '-- my config\nrequire("hypr.monitors")\n' >"$main"
  printf '{\n  "mine": {"label":"Mine","action":"true"}\n}\n' >"$menu"
  cp "$main" "$work/main-$1.orig"
  cp "$menu" "$work/menu-$1.orig"
}
run() {
  env -u HYPRLAND_INSTANCE_SIGNATURE HOME="$home" PATH="$work/bin:$PATH" bash "$SETUP" "$@" >"$work/out" 2>&1
}
has() { grep -qF -- "$1" "$work/out"; }
same() { cmp -s "$1" "$2"; }

# --- an untouched setup comes out completely
fresh plain
run || fail "setup failed: $(cat "$work/out")"
grep -q '^-- fairfax-setup-sha256: [0-9a-f]\{64\}$' "$module" || fail "module has no checksum line"
grep -q '// fairfax: begin .* sha256:[0-9a-f]\{64\}$' "$menu" || fail "menu block has no checksum"
# JSONC: line comments and trailing commas are allowed.
python3 -c 'import json,re,sys; t=re.sub(r"(?m)^[ \t]*//[^\n]*","",open(sys.argv[1]).read()); json.loads(re.sub(r",(\s*\})",r"\1",t))' "$menu" \
  || fail "menu is not valid JSONC after setup"
run --remove || fail "remove failed: $(cat "$work/out")"
[[ ! -e $module ]] || fail "untouched module was not removed"
same "$main" "$work/main-plain.orig" || fail "hyprland.lua not restored exactly"
same "$menu" "$work/menu-plain.orig" || fail "menu not restored exactly"
has "no longer wired into Omarchy" || fail "no all-clear message"
pass "untouched setup is removed completely"

# --- setup run twice (another key) is still setup's
fresh twice
run && run --key "SUPER + ALT + N" || fail "setup twice failed"
grep -q 'SUPER + ALT + N' "$module" || fail "second key not written"
run --remove
[[ ! -e $module ]] && same "$main" "$work/main-twice.orig" && same "$menu" "$work/menu-twice.orig" \
  || fail "re-run setup not removed completely"
pass "setup run twice is removed completely"

# --- an edited module stays on disk but is no longer loaded; the menu goes
fresh module
run
printf 'o.bind("SUPER + F", "my thing", "foot")\n' >>"$module"
cp "$module" "$work/module-edited"
run --remove
same "$module" "$work/module-edited" || fail "edited module was changed or removed"
same "$main" "$work/main-module.orig" || fail "the marked line stayed beside an edited module"
same "$menu" "$work/menu-module.orig" || fail "menu block not removed beside an edited module"
has "Kept $module" || fail "no message about the kept module"
has "no longer loaded" || fail "no word that the kept module is no longer loaded"
has "except for what you changed" || fail "no summary of what was kept"
pass "edited module stays on disk, unloaded"

# --- an edited module with its checksum line deleted is still edited
fresh nosum
run
sed -i '/^-- fairfax-setup-sha256: /d' "$module"
sed -i 's/fairfax --background/fairfax --background --quiet/' "$module"
run --remove
[[ -e $module ]] || fail "edited module without checksum was removed"
pass "edited module without its checksum line stays"

# --- an edited menu block stays; the module and its line go
fresh menu
run
sed -i 's/"label":"Today"/"label":"Today!"/' "$menu"
cp "$menu" "$work/menu-edited"
run --remove
same "$menu" "$work/menu-edited" || fail "edited menu block was changed"
[[ ! -e $module ]] || fail "untouched module not removed beside an edited menu"
same "$main" "$work/main-menu.orig" || fail "hyprland.lua not restored beside an edited menu"
has "Kept the Fairfax section" || fail "no message about the kept menu block"
pass "edited menu block stays"

# --- a menu block missing its end marker is left alone
fresh broken
run
sed -i '/\/\/ fairfax: end/d' "$menu"
cp "$menu" "$work/menu-broken"
run --remove
same "$menu" "$work/menu-broken" || fail "menu without an end marker was changed"
has "Kept the Fairfax section" || fail "no message about the half-marked menu"
pass "menu block without its end marker stays"

# --- your own line that loads the module stays, and so does the module
fresh ownline
run
sed -i 's/^require("hypr.fairfax") -- fairfax: added by .*/require("hypr.fairfax")  -- mine now/' "$main"
cp "$main" "$work/main-ownline"
run --remove
same "$main" "$work/main-ownline" || fail "a line you wrote was changed or removed"
[[ -e $module ]] || fail "module removed while your own line still loads it"
has "Kept your own line" || fail "no message about the kept line"
pass "your own require line and the module it loads stay"

# --- the marked line goes even when the module is already gone
fresh nomodule
run
rm "$module"
run --remove
same "$main" "$work/main-nomodule.orig" || fail "marked line stayed after the module was deleted"
pass "marked line goes when the module is gone"

# --- a 1.0 install (no checksums) comes out completely
fresh legacy
cp "$FIX/fairfax.lua" "$module"
cp "$FIX/omarchy-menu.jsonc" "$menu"
printf -- '-- my config\nrequire("hypr.monitors")\nrequire("hypr.fairfax") -- fairfax: added by `fairfax setup omarchy`\n' >"$main"
run --remove
[[ ! -e $module ]] || fail "1.0 module not removed"
same "$main" "$work/main-legacy.orig" || fail "1.0 hyprland.lua line not removed"
grep -q fairfax "$menu" && fail "1.0 menu block not removed"
pass "1.0 install is removed completely"

# --- an edited 1.0 module stays
fresh legacyedit
sed 's/center = true })$/center = false })/' "$FIX/fairfax.lua" >"$module"
cp "$module" "$work/module-legacyedit"
printf 'require("hypr.fairfax") -- fairfax: added by `fairfax setup omarchy`\n' >>"$main"
run --remove
same "$module" "$work/module-legacyedit" || fail "edited 1.0 module was removed"
same "$main" "$work/main-legacyedit.orig" || fail "1.0 marked line stayed beside an edited module"
pass "edited 1.0 module stays"

# --- a taken key is refused however it is written; Fairfax's own binding isn't
fresh keys
# The listing's format: modifiers space-separated, "+" only before the key.
cat >"$work/listing" <<'LIST'
SUPER + K                           → Keybindings
SUPER SHIFT + RETURN                → Browser
SUPER SHIFT + T                     → Activity
SUPER CTRL + T                      → Theme menu
SUPER ALT + J                       → Fairfax quick entry
LIST
for key in "SUPER + SHIFT + RETURN" "super + shift + return" "SHIFT + SUPER + RETURN" \
           "SUPER + SHIFT + T" "SUPER + CONTROL + T" "SUPER + K"; do
  if run --key "$key"; then
    fail "taken key \"$key\" was accepted"
  else
    has "is already bound" || fail "no 'already bound' for \"$key\": $(cat "$work/out")"
  fi
  [[ ! -e $module ]] || { fail "a refused key \"$key\" still wrote $module"; rm -f "$module"; }
done
same "$main" "$work/main-keys.orig" || fail "a refused key changed hyprland.lua"
run --key "SUPER + ALT + J" || fail "Fairfax's own binding counted as taken: $(cat "$work/out")"
rm -f "$module"
run || fail "the default key is taken in Omarchy's listing: $(cat "$work/out")"
grep -qF 'o.bind("SUPER + ALT + T",' "$module" || fail "default key is not SUPER + ALT + T"
rm -f "$work/listing"
pass "taken keys are refused in any order and case; Fairfax's own and the default pass"

# --- a key that isn't a plain key name is refused before anything is written
fresh badkey
for key in 'SUPER + "T' 'SUPER + T") os.execute("x' '' '+ +'; do
  if run --key "$key"; then fail "bad key \"$key\" was accepted"; fi
done
run --key 2>/dev/null && fail "--key with no value was accepted"
has "--key needs a key" || fail "no message for --key with no value"
[[ ! -e $module ]] || fail "a bad key wrote $module"
same "$main" "$work/main-badkey.orig" || fail "a bad key changed hyprland.lua"
pass "keys that aren't plain key names are refused"

if ((failures)); then
  echo "$failures failure(s)" >&2
  exit 1
fi
echo "all omarchy-setup tests passed"
