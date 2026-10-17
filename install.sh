#!/usr/bin/env bash
# Fairfax one-line installer. Read this before piping it to bash:
#
#   curl -fsSL https://raw.githubusercontent.com/jrimmer/fairfax/main/install.sh | bash
#
# Flags (pass them through bash -s --):
#
#   --no-setup    don't wire Fairfax into Omarchy after a fresh install
#   --appimage    use the AppImage even on Arch
#   --uninstall   remove Fairfax
#   --help        show this help
#
# On Arch it installs the pacman package, asking for sudo only for pacman -U.
# Everywhere else it installs the AppImage under ~/.local, no root needed.
# Files come from $FAIRFAX_RELEASE_URL (the GitHub "latest/download" URL by
# default) and each one is checked against its .sha256 before use.

set -euo pipefail

RELEASE_URL="${FAIRFAX_RELEASE_URL:-https://github.com/jrimmer/fairfax/releases/latest/download}"

usage() {
    sed -n '2,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

no_setup=false
force_appimage=false
uninstall=false

for arg in "$@"; do
    case "$arg" in
        --no-setup) no_setup=true ;;
        --appimage) force_appimage=true ;;
        --uninstall) uninstall=true ;;
        --help|-h) usage; exit 0 ;;
        *) echo "install.sh: unknown option $arg (try --help)" >&2; exit 2 ;;
    esac
done

# Work in a private directory that goes away even if we fail.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

if command -v curl >/dev/null 2>&1; then
    fetch() { curl -fsSL "$1" -o "$2"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget -q -O "$2" "$1"; }
else
    echo "install.sh: need curl or wget to download Fairfax." >&2
    exit 1
fi

# Download an asset and its .sha256, then verify before anyone uses it. A
# mismatch aborts the whole run, so a half-installed tree never happens.
download() {
    local name="$1" dest="$tmp/$1"
    if ! fetch "$RELEASE_URL/$name" "$dest"; then
        echo "install.sh: could not download $name from $RELEASE_URL" >&2
        exit 1
    fi
    if ! fetch "$RELEASE_URL/$name.sha256" "$dest.sha256"; then
        echo "install.sh: could not download $name.sha256" >&2
        exit 1
    fi
    if ! ( cd "$tmp" && sha256sum -c --quiet "$1.sha256" ); then
        echo "install.sh: checksum mismatch for $name; nothing was installed." >&2
        exit 1
    fi
    printf '%s' "$dest"
}

machine=$(uname -m)

# Fetches fairfax-version.txt first, so we can say what we're about to do.
version=$(cat "$(download fairfax-version.txt)")

case "$machine" in
    x86_64|aarch64) ;;
    *) echo "install.sh: unsupported architecture '$machine' (Fairfax ships x86_64 and aarch64)." >&2; exit 1 ;;
esac

# Arch gets the pacman package unless --appimage asks for the portable one.
arch_pacman=false
if command -v pacman >/dev/null 2>&1 && ! $force_appimage; then
    arch_pacman=true
fi

omarchy="$HOME/.local/share/omarchy"
hypr_main="$HOME/.config/hypr/hyprland.lua"

is_omarchy=false
[ -d "$omarchy" ] && is_omarchy=true
omarchy_wired=false
[ -f "$hypr_main" ] && grep -qF 'require("hypr.fairfax")' "$hypr_main" && omarchy_wired=true

# The installed version, or empty for a fresh install.
installed_dir="$HOME/.local/share/fairfax"
installed_version_file="$installed_dir/installed-version"
have=""
if $arch_pacman; then
    if pacman_q=$(pacman -Q fairfax 2>/dev/null); then
        have=$(printf '%s\n' "$pacman_q" | awk '{print $2}')
        have=${have%-*}
    fi
elif [ -f "$installed_version_file" ]; then
    have=$(cat "$installed_version_file")
fi

if [ "$have" = "$version" ]; then
    echo "Fairfax $version is already installed."
    exit 0
fi

if [ -z "$have" ]; then
    echo "Installing Fairfax $version…"
else
    echo "Updating Fairfax $have → $version…"
fi

# --- uninstall ------------------------------------------------------------

uninstall_arch() {
    if $is_omarchy && [ -f "$hypr_main" ] && grep -qF 'require("hypr.fairfax")' "$hypr_main"; then
        if command -v fairfax >/dev/null 2>&1; then
            fairfax setup omarchy --remove || true
        fi
    fi
    echo "Running sudo pacman -R fairfax; you may be asked for your password."
    sudo pacman -R fairfax
    echo "Fairfax is uninstalled."
}

uninstall_appimage() {
    rm -f "$HOME/.local/bin/fairfax" \
          "$HOME/.local/bin/frfx" \
          "$HOME/.local/share/applications/fairfax.desktop" \
          "$HOME/.local/share/icons/hicolor/scalable/apps/fairfax.svg" \
          "$installed_version_file"
    echo "Fairfax is uninstalled."
}

if $uninstall; then
    if $arch_pacman; then uninstall_arch; else uninstall_appimage; fi
    exit 0
fi

# --- install --------------------------------------------------------------

install_arch() {
    local file="fairfax-$version-1-$machine.pkg.tar.zst"
    pkg=$(download "$file")
    echo "Running sudo pacman -U $file; you may be asked for your password."
    sudo pacman -U --noconfirm "$pkg"
}

install_appimage() {
    local desktop icon squash bindir appdir icondir
    appimage=$(download "fairfax-$version-$machine.AppImage")
    chmod +x "$appimage"

    # Take the .desktop entry and icon out of the AppImage. Extract it first,
    # so a broken archive aborts before anything lands in $HOME.
    ( cd "$tmp" && "$appimage" --appimage-extract >/dev/null 2>&1 )
    squash="$tmp/squashfs-root"
    desktop="$squash/usr/share/applications/fairfax.desktop"
    if [ ! -f "$desktop" ]; then
        echo "install.sh: the AppImage has no desktop entry; aborting." >&2
        exit 1
    fi

    bindir="$HOME/.local/bin"
    appdir="$HOME/.local/share/applications"
    icondir="$HOME/.local/share/icons/hicolor/scalable/apps"
    mkdir -p "$bindir" "$appdir" "$icondir" "$installed_dir"

    install -m755 "$appimage" "$bindir/fairfax"
    ln -sf fairfax "$bindir/frfx"

    # Point the launcher at the AppImage we just installed.
    sed "s|^Exec=.*|Exec=$bindir/fairfax %u|" "$desktop" >"$appdir/fairfax.desktop"
    icon=$(find "$squash/usr/share/icons" -name fairfax.svg -print -quit 2>/dev/null || true)
    if [ -n "$icon" ]; then
        install -m644 "$icon" "$icondir/fairfax.svg"
    fi

    case ":$PATH:" in
        *":$bindir:"*) ;;
        *) echo "Note: $bindir is not on your PATH; add it to run 'fairfax' directly." >&2 ;;
    esac

    printf '%s\n' "$version" >"$installed_version_file"
}

if $arch_pacman; then
    install_arch
else
    install_appimage
fi

# On Omarchy a fresh install gets wired in; an update keeps the current setup.
setup_ran=false
if $is_omarchy && [ -z "$have" ] && ! $no_setup; then
    if $omarchy_wired; then
        echo "Fairfax is already wired into Omarchy; leaving it as it is."
    elif command -v fairfax >/dev/null 2>&1; then
        if fairfax setup omarchy; then
            setup_ran=true
        else
            echo "The install is done. Finishing Omarchy setup failed; retry with:" >&2
            echo "    fairfax setup omarchy" >&2
        fi
    fi
fi

if $setup_ran; then
    echo "Start Fairfax with Super+Space → Fairfax, or 'fairfax'; press Super+Shift+T for Quick Entry."
elif $arch_pacman || $is_omarchy; then
    echo "Start Fairfax with Super+Space → Fairfax, or run 'fairfax'."
else
    echo "Start Fairfax from your applications menu, or run 'fairfax'."
fi
