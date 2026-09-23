#!/bin/bash
# Ring Out native aarch64 launcher for ROCKNIX / PortMaster.
#
# Install this file and the adjacent RingOut/ directory in /storage/roms/ports.
# It intentionally uses ROCKNIX's current glibc, Freedreno/Mesa Vulkan ICD,
# Wayland session and InputPlumber controller mapping; no foreign runtime rootfs
# or system files are installed.
set -eu

PORT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
GAME_DIR="$PORT_DIR/RingOut"
PM_DIR="/storage/roms/ports/PortMaster"
LOG_DIR="$GAME_DIR/userdata"
LOG="$LOG_DIR/rocknix-launch.log"

[ -x "$GAME_DIR/RingOut" ] || {
    echo "Ring Out: missing executable $GAME_DIR/RingOut" >&2
    exit 1
}
[ -x "$GAME_DIR/bin/moderngekko-run" ] || {
    echo "Ring Out: missing executable $GAME_DIR/bin/moderngekko-run" >&2
    exit 1
}

# Load the ROCKNIX PortMaster mapping. get_controls exports
# SDL_GAMECONTROLLERCONFIG_FILE for the pad configuration selected in ES.
. /etc/profile
if [ -r "$PM_DIR/control.txt" ]; then
    # shellcheck disable=SC1090
    . "$PM_DIR/control.txt"
    if command -v get_controls >/dev/null 2>&1; then
        get_controls
    fi
fi

# get_controls intentionally puts the legacy PortMaster compat directory first.
# Keep its SDL_GAMECONTROLLERCONFIG_FILE, but let RingOut use current ROCKNIX
# Mesa/Freedreno, Vulkan, Wayland, and glibc libraries instead.
unset LD_LIBRARY_PATH

# All mutable emulator state remains beside this port on /storage.
mkdir -p "$LOG_DIR/xdg-config" "$LOG_DIR/xdg-cache" "$LOG_DIR/xdg-data"
export HOME="$LOG_DIR/home"
export XDG_CONFIG_HOME="$LOG_DIR/xdg-config"
export XDG_CACHE_HOME="$LOG_DIR/xdg-cache"
export XDG_DATA_HOME="$LOG_DIR/xdg-data"
mkdir -p "$HOME"

# ROCKNIX uses this process name for task cleanup; native Wayland clients are
# full-screened by process lookup, matching ROCKNIX's own Dolphin launcher.
if command -v set_kill >/dev/null 2>&1; then
    set_kill set "moderngekko-run"
fi
if command -v sway_fullscreen >/dev/null 2>&1; then
    sway_fullscreen "moderngekko-run" "pidof" &
fi

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prev"
exec >"$LOG" 2>&1
echo "=== Ring Out ROCKNIX / PortMaster $(date -Is) ==="
echo "port=$PORT_DIR"
echo "wayland=${WAYLAND_DISPLAY:-unset}"
echo "controller-db=${SDL_GAMECONTROLLERCONFIG_FILE:-unset}"

cd "$GAME_DIR"
exec ./RingOut "$@"
