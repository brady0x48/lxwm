#!/usr/bin/env sh
set -eu

PROJECT_NAME="lxwm"
PREFIX="${PREFIX:-/usr/local}"
XSESSIONS_DIR="${XSESSIONS_DIR:-/usr/share/xsessions}"
CONFIG_PATH_REL=".config/lxwm/config"

say() {
    printf '%s\n' "$*"
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        die "missing command: $1"
    fi
}

need_pkg() {
    if ! pkg-config --exists "$1"; then
        MISSING_PKGS="${MISSING_PKGS}${MISSING_PKGS:+ }$1"
    fi
}

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$repo_root"

if [ ! -f "Makefile" ] || [ ! -d "src" ]; then
    die "run this script from the project root"
fi

say "Checking required tools..."
need_cmd make
need_cmd cc
need_cmd pkg-config
need_cmd install

MISSING_PKGS=""
need_pkg x11
need_pkg xinerama
need_pkg xft

if [ -n "$MISSING_PKGS" ]; then
    say "Missing development libraries: $MISSING_PKGS"
    say ""
    say "Install them with your package manager and rerun:"
    say "  Debian/Ubuntu: sudo apt install build-essential pkg-config libx11-dev libxinerama-dev libxft-dev"
    say "  Fedora:        sudo dnf install gcc make pkgconf-pkg-config libX11-devel libXinerama-devel libXft-devel"
    say "  Arch:          sudo pacman -S base-devel pkgconf libx11 libxinerama libxft"
    exit 1
fi

say "Building $PROJECT_NAME..."
make

if [ "$(id -u)" -eq 0 ]; then
    ROOTCMD=""
else
    if command -v sudo >/dev/null 2>&1; then
        ROOTCMD="sudo"
    else
        die "need root privileges for install-bin/install-xsession (install sudo or run as root)"
    fi
fi

say "Installing binaries and xsession entry..."
if [ -n "$ROOTCMD" ]; then
    $ROOTCMD make install-bin install-xsession PREFIX="$PREFIX" XSESSIONS_DIR="$XSESSIONS_DIR"
else
    make install-bin install-xsession PREFIX="$PREFIX" XSESSIONS_DIR="$XSESSIONS_DIR"
fi

if [ -n "${SUDO_USER:-}" ]; then
    TARGET_USER="$SUDO_USER"
    TARGET_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
else
    TARGET_USER="${USER:-$(id -un)}"
    TARGET_HOME="${HOME:-$(getent passwd "$TARGET_USER" | cut -d: -f6)}"
fi

[ -n "$TARGET_HOME" ] || die "could not determine target home directory"

CONFIG_PATH="$TARGET_HOME/$CONFIG_PATH_REL"
say "Checking user config at $CONFIG_PATH"

if [ -n "$ROOTCMD" ]; then
    $ROOTCMD make install-user-config
else
    make install-user-config
fi

if [ "$(id -u)" -eq 0 ]; then
    chown "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.config/lxwm" "$CONFIG_PATH" 2>/dev/null || true
fi

say ""
say "Done."
say "Binary: ${PREFIX}/bin/lxwm"
say "X session file: ${XSESSIONS_DIR}/lxwm.desktop"
say "User config: ${CONFIG_PATH}"
