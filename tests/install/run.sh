#!/usr/bin/env bash
# Hermetic tests for install.sh. No network, no root.
#
# Builds a throwaway release directory (a version file, dummy Arch packages
# and a tiny fake AppImage, each with a real .sha256), points install.sh at it
# with FAIRFAX_RELEASE_URL, and runs install.sh against stubs for pacman,
# sudo, uname and fairfax that log their arguments. Every case gets a fresh
# HOME, and the non-Arch cases get a PATH without pacman.
#
# Run directly, or through bin/test-install.

set -euo pipefail

ROOT="$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)"
INSTALL="$ROOT/install.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fails=0
checks=0

fail() { echo "FAIL: $*" >&2; fails=$((fails + 1)); }
ok() { echo "ok: $*"; }

assert_file() {
    checks=$((checks + 1))
    [ -e "$1" ] || fail "$2 (missing $1)"
}
assert_no_file() {
    checks=$((checks + 1))
    [ ! -e "$1" ] || fail "$2 (unexpected $1)"
}
assert_eq() {
    checks=$((checks + 1))
    [ "$1" = "$2" ] || fail "$3 (expected '$2', got '$1')"
}
assert_contains() {
    checks=$((checks + 1))
    case "$1" in *"$2"*) ;; *) fail "$3 (no '$2' in output)";; esac
}
assert_not_contains() {
    checks=$((checks + 1))
    case "$1" in *"$2"*) fail "$3 (found '$2')";; *) ;; esac
}

# --- the fake release ------------------------------------------------------

VERSION=0.5.2
RELEASE="$WORK/release"
mkdir -p "$RELEASE"

printf '%s\n' "$VERSION" >"$RELEASE/fairfax-version.txt"

# Dummy Arch packages: content differs by arch so the checksum check is real.
for m in x86_64 aarch64; do
    printf 'pkg-%s' "$m" >"$RELEASE/fairfax-$VERSION-1-$m.pkg.tar.zst"
done

# A tiny fake AppImage: a script that, for --appimage-extract, writes the
# squashfs-root tree the real installer expects.
make_appimage() {
    local path="$1"
    cat >"$path" <<'APPIMAGE'
#!/usr/bin/env bash
set -euo pipefail
if [ "${1:-}" = "--appimage-extract" ]; then
    mkdir -p squashfs-root/usr/share/applications \
             squashfs-root/usr/share/icons/hicolor/scalable/apps
    cat >squashfs-root/usr/share/applications/fairfax.desktop <<DESKTOP
[Desktop Entry]
Type=Application
Name=Fairfax
Exec=fairfax %u
Icon=fairfax
Terminal=false
DESKTOP
    printf '<svg/>' >squashfs-root/usr/share/icons/hicolor/scalable/apps/fairfax.svg
    exit 0
fi
echo "fairfax appimage stub"
APPIMAGE
    chmod +x "$path"
}
for m in x86_64 aarch64; do
    make_appimage "$RELEASE/fairfax-$VERSION-$m.AppImage"
done

# Real .sha256 files for every asset.
sign_release() {
    local dir="$1"
    ( cd "$dir" && sha256sum fairfax-* >SHA256SUMS )
    while read -r sum file; do
        printf '%s  %s\n' "$sum" "$file" >"$dir/$file.sha256"
    done <"$dir/SHA256SUMS"
    rm "$dir/SHA256SUMS"
}
sign_release "$RELEASE"

# A second release, for the update cases.
VERSION2=0.5.3
RELEASE2="$WORK/release2"
mkdir -p "$RELEASE2"
printf '%s\n' "$VERSION2" >"$RELEASE2/fairfax-version.txt"
for m in x86_64 aarch64; do
    printf 'pkg2-%s' "$m" >"$RELEASE2/fairfax-$VERSION2-1-$m.pkg.tar.zst"
    make_appimage "$RELEASE2/fairfax-$VERSION2-$m.AppImage"
done
sign_release "$RELEASE2"

# --- stubs -----------------------------------------------------------------

STUBS="$WORK/stubs"
mkdir -p "$STUBS"

# Every stub appends its arguments (one line per call) to $STUB_LOG.
cat >"$STUBS/pacman" <<'STUB'
#!/usr/bin/env bash
printf 'pacman %s\n' "$*" >>"$STUB_LOG"
case "${1:-}" in
    -Q)
        if [ "${PACMAN_INSTALLED:-}" = "yes" ]; then
            printf '%s %s\n' fairfax "${PACMAN_VERSION:-0.5.1}"
        else
            exit 1
        fi
        ;;
esac
exit 0
STUB

cat >"$STUBS/sudo" <<'STUB'
#!/usr/bin/env bash
printf 'sudo %s\n' "$*" >>"$STUB_LOG"
exec "$@"
STUB

cat >"$STUBS/uname" <<'STUB'
#!/usr/bin/env bash
printf 'uname %s\n' "$*" >>"$STUB_LOG"
if [ "${1:-}" = "-m" ]; then
    printf '%s\n' "${STUB_MACHINE:-x86_64}"
    exit 0
fi
exec /usr/bin/uname "$@"
STUB

# The fairfax stub logs the call; it can fail setup on demand.
cat >"$STUBS/fairfax" <<'STUB'
#!/usr/bin/env bash
printf 'fairfax %s\n' "$*" >>"$STUB_LOG"
if [ "${FAIRFAX_SETUP_FAIL:-}" = "yes" ]; then
    echo "SUPER + SHIFT + T is already bound" >&2
    echo "Pick another with: fairfax setup omarchy --key \"SUPER + ALT + T\"" >&2
    exit 1
fi
exit 0
STUB
chmod +x "$STUBS"/*

# A copy of the stubs without pacman, for the non-Arch cases: install.sh must
# not find a pacman at all there.
STUBS_NOPACMAN="$WORK/stubs-nopacman"
mkdir -p "$STUBS_NOPACMAN"
for s in sudo uname fairfax; do ln -s "$STUBS/$s" "$STUBS_NOPACMAN/$s"; done

# A PATH with the stubs, the host tools, and the host's pacman (Arch cases).
TEST_PATH_ARCH="$STUBS:$PATH"

# A PATH with the stubs but no pacman at all (non-Arch cases). The real tools
# install.sh needs are symlinked into realbin, everything else is excluded.
REALBIN="$WORK/realbin"
mkdir -p "$REALBIN"
for tool in bash env curl sha256sum mktemp sed grep find awk install \
            mkdir ln chmod cat rm dirname pwd sleep cmp touch; do
    real=$(command -v "$tool" 2>/dev/null) || continue
    ln -s "$real" "$REALBIN/$tool"
done
TEST_PATH_NO_PACMAN="$STUBS_NOPACMAN:$REALBIN"

# --- harness ---------------------------------------------------------------

new_home() {
    HOME_DIR=$(mktemp -d "$WORK/home.XXXXXX")
    export HOME="$HOME_DIR"
    export STUB_LOG="$HOME_DIR/stub.log"
    : >"$STUB_LOG"
    export PACMAN_INSTALLED="" PACMAN_VERSION="" STUB_MACHINE="x86_64" FAIRFAX_SETUP_FAIL=""
    export FAIRFAX_RELEASE_URL="file://$RELEASE"
    export PATH="$TEST_PATH_ARCH"
}
use_no_pacman() { export PATH="$TEST_PATH_NO_PACMAN"; }
run() {
    set +e
    OUT=$(bash "$INSTALL" "$@" 2>&1)
    STATUS=$?
    set -e
}

make_omarchy() { mkdir -p "$HOME/.local/share/omarchy"; }
wire_hyprland() {
    mkdir -p "$HOME/.config/hypr"
    printf 'require("hypr.fairfax") -- added\n' >"$HOME/.config/hypr/hyprland.lua"
}

log_has()   { grep -q -- "$1" "$STUB_LOG"; }
log_lacks() { ! grep -q -- "$1" "$STUB_LOG"; }

# --- fresh Arch install ----------------------------------------------------

echo "== fresh Arch install =="
new_home
make_omarchy
run
assert_eq "$STATUS" 0 "fresh Arch install exits 0"
assert_contains "$OUT" "Installing Fairfax $VERSION" "says it is installing"
assert_contains "$OUT" "sudo pacman -U" "warns about sudo before pacman"
assert_contains "$OUT" "fairfax-$VERSION-1-x86_64.pkg.tar.zst" "arch-named package used"
log_has "sudo pacman -U --noconfirm" || fail "sudo used for pacman -U"
log_has "fairfax setup omarchy" || fail "setup run on Omarchy fresh install"
assert_contains "$OUT" "Super+Shift+T" "start line mentions Quick Entry when setup ran"
ok "fresh Arch install"

# fresh Arch but not Omarchy: no setup
new_home
run
assert_eq "$STATUS" 0 "fresh Arch without Omarchy exits 0"
log_lacks "fairfax setup omarchy" || fail "setup must not run without Omarchy"
assert_not_contains "$OUT" "Super+Shift+T" "no Quick Entry line without setup"
ok "fresh Arch without Omarchy"

# fresh Arch on Omarchy but --no-setup
new_home
make_omarchy
run --no-setup
assert_eq "$STATUS" 0 "fresh Arch --no-setup exits 0"
log_lacks "fairfax setup omarchy" || fail "--no-setup must skip setup"
ok "fresh Arch --no-setup"

# fresh Arch on Omarchy, already wired: skipped with a note
new_home
make_omarchy
wire_hyprland
run
assert_eq "$STATUS" 0 "already-wired install exits 0"
log_lacks "fairfax setup omarchy" || fail "setup must be skipped when hook exists"
assert_contains "$OUT" "already wired" "notes that Omarchy is already wired"
ok "fresh Arch already wired"

# --- already up to date ----------------------------------------------------

echo "== already up to date =="
new_home
export PACMAN_INSTALLED=yes PACMAN_VERSION="$VERSION"
run
assert_eq "$STATUS" 0 "up-to-date exits 0"
assert_contains "$OUT" "Fairfax $VERSION is already installed." "reports up to date"
log_lacks "pacman -U" || fail "no pacman -U when up to date"
ok "already up to date (Arch)"

# --- update ----------------------------------------------------------------

echo "== update =="
new_home
make_omarchy
export PACMAN_INSTALLED=yes PACMAN_VERSION=0.5.1
export FAIRFAX_RELEASE_URL="file://$RELEASE2"
run
assert_eq "$STATUS" 0 "update exits 0"
assert_contains "$OUT" "Updating Fairfax 0.5.1 → $VERSION2" "reports the update"
log_has "pacman -U --noconfirm" || fail "pacman -U called on update"
log_lacks "fairfax setup omarchy" || fail "setup must not run on update"
ok "Arch update"

# --- checksum mismatch -----------------------------------------------------

echo "== checksum mismatch =="
new_home
make_omarchy
cp "$RELEASE/fairfax-$VERSION-1-x86_64.pkg.tar.zst" "$WORK/pkg.bak"
# Corrupt the package without touching its .sha256.
printf 'tampered' >>"$RELEASE/fairfax-$VERSION-1-x86_64.pkg.tar.zst"
run
assert_eq "$STATUS" 1 "checksum mismatch exits non-zero"
assert_contains "$OUT" "checksum mismatch" "names the mismatch"
log_lacks "pacman -U" || fail "no install after a checksum mismatch"
mv "$WORK/pkg.bak" "$RELEASE/fairfax-$VERSION-1-x86_64.pkg.tar.zst"
ok "checksum mismatch aborts"

# --- unsupported arch ------------------------------------------------------

echo "== unsupported architecture =="
new_home
export STUB_MACHINE=riscv64
run
assert_eq "$STATUS" 1 "unsupported arch exits non-zero"
assert_contains "$OUT" "unsupported architecture" "names the unsupported arch"
log_lacks "pacman -U" || fail "no install on unsupported arch"
ok "unsupported architecture"

# --- AppImage install (non-Arch) ------------------------------------------

echo "== AppImage install =="
new_home
use_no_pacman
BINDIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/scalable/apps"
run
assert_eq "$STATUS" 0 "AppImage install exits 0"
assert_file "$BINDIR/fairfax" "appimage installed to ~/.local/bin"
assert_file "$BINDIR/frfx" "frfx symlink made"
assert_file "$APP_DIR/fairfax.desktop" "desktop entry installed"
assert_file "$ICON_DIR/fairfax.svg" "icon installed"
assert_file "$HOME/.local/share/fairfax/installed-version" "installed-version written"
assert_eq "$(cat "$HOME/.local/share/fairfax/installed-version")" "$VERSION" "installed-version content"
assert_contains "$(cat "$APP_DIR/fairfax.desktop")" "Exec=$BINDIR/fairfax %u" "desktop Exec points at the AppImage"
log_lacks "sudo" || fail "no root used for the AppImage install"
assert_contains "$OUT" "not on your PATH" "warns when ~/.local/bin is off PATH"
ok "AppImage install"

# --appimage on Arch: the AppImage path is forced.
echo "== --appimage on Arch =="
new_home
make_omarchy
run --appimage
assert_eq "$STATUS" 0 "--appimage on Arch exits 0"
assert_file "$HOME/.local/bin/fairfax" "--appimage installs the AppImage"
log_lacks "pacman -U" || fail "--appimage must not call pacman"
log_lacks "sudo" || fail "--appimage must not use root"
ok "--appimage on Arch"

# --- AppImage update and up-to-date ---------------------------------------

echo "== AppImage update =="
new_home
use_no_pacman
mkdir -p "$HOME/.local/share/fairfax"
printf '0.5.1\n' >"$HOME/.local/share/fairfax/installed-version"
export FAIRFAX_RELEASE_URL="file://$RELEASE2"
run
assert_eq "$STATUS" 0 "AppImage update exits 0"
assert_contains "$OUT" "Updating Fairfax 0.5.1 → $VERSION2" "reports the AppImage update"
assert_eq "$(cat "$HOME/.local/share/fairfax/installed-version")" "$VERSION2" "installed-version updated"
ok "AppImage update"

echo "== AppImage up to date =="
new_home
use_no_pacman
mkdir -p "$HOME/.local/share/fairfax"
printf '%s\n' "$VERSION" >"$HOME/.local/share/fairfax/installed-version"
run
assert_eq "$STATUS" 0 "AppImage up-to-date exits 0"
assert_contains "$OUT" "already installed" "reports AppImage up to date"
assert_no_file "$HOME/.local/bin/fairfax" "nothing installed when up to date"
ok "AppImage up to date"

# --- uninstall -------------------------------------------------------------

echo "== uninstall (Arch) =="
new_home
make_omarchy
wire_hyprland
run --uninstall
assert_eq "$STATUS" 0 "Arch uninstall exits 0"
log_has "fairfax setup omarchy --remove" || fail "remove hook on Arch uninstall"
log_has "sudo pacman -R fairfax" || fail "pacman -R called"
ok "Arch uninstall"

echo "== uninstall (AppImage) =="
new_home
use_no_pacman
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" \
         "$HOME/.local/share/icons/hicolor/scalable/apps" \
         "$HOME/.local/share/fairfax"
touch "$HOME/.local/bin/fairfax" "$HOME/.local/bin/frfx" \
      "$HOME/.local/share/applications/fairfax.desktop" \
      "$HOME/.local/share/icons/hicolor/scalable/apps/fairfax.svg" \
      "$HOME/.local/share/fairfax/installed-version"
run --uninstall
assert_eq "$STATUS" 0 "AppImage uninstall exits 0"
assert_no_file "$HOME/.local/bin/fairfax" "appimage removed"
assert_no_file "$HOME/.local/bin/frfx" "frfx removed"
assert_no_file "$HOME/.local/share/applications/fairfax.desktop" "desktop entry removed"
assert_no_file "$HOME/.local/share/icons/hicolor/scalable/apps/fairfax.svg" "icon removed"
assert_no_file "$HOME/.local/share/fairfax/installed-version" "installed-version removed"
log_lacks "sudo" || fail "no root used to uninstall the AppImage"
ok "AppImage uninstall"

# --- setup failure ---------------------------------------------------------

echo "== setup failure =="
new_home
make_omarchy
export FAIRFAX_SETUP_FAIL=yes
run
assert_eq "$STATUS" 0 "install still succeeds when setup fails"
log_has "pacman -U --noconfirm" || fail "package still installed when setup fails"
assert_contains "$OUT" "already bound" "prints the setup failure message"
assert_contains "$OUT" "fairfax setup omarchy" "prints the retry command"
assert_contains "$OUT" "install is done" "says the install counts as done"
ok "setup failure leaves the install done"

# --- help ------------------------------------------------------------------

echo "== --help =="
new_home
run --help
assert_eq "$STATUS" 0 "--help exits 0"
assert_contains "$OUT" "Fairfax one-line installer" "help prints the usage block"
assert_contains "$OUT" "--no-setup" "help lists --no-setup"
ok "--help"

# The README pipes the script into bash; --help must work in that form too,
# where BASH_SOURCE is empty.
echo "== --help piped through bash =="
new_home
set +e
OUT=$(cat "$INSTALL" | bash -s -- --help 2>&1)
STATUS=$?
set -e
assert_eq "$STATUS" 0 "piped --help exits 0"
assert_contains "$OUT" "Fairfax one-line installer" "piped help prints the usage block"
ok "--help piped through bash"

# --- summary ---------------------------------------------------------------

echo
if [ "$fails" -ne 0 ]; then
    echo "$fails check(s) failed."
    exit 1
fi
echo "install tests passed ($checks checks)."
