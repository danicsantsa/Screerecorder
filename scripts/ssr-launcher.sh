#!/usr/bin/env bash
set -euo pipefail

# Simple wrapper to run the installed simplescreenrecorder with a sanitized environment
# Prevent Snap or custom LD_LIBRARY_PATH/LD_PRELOAD from injecting incompatible libs.

exec env -i \
    PATH="$PATH" \
    DISPLAY="${DISPLAY:-:0}" \
    XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
    DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" \
    HOME="$HOME" \
    /usr/local/bin/simplescreenrecorder "$@"
