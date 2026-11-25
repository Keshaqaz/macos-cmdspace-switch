#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LABEL="local.bibs.cmdspace-switch"
BIN_PATH="$SCRIPT_DIR/cmdspace-switch"
PLIST_SRC="$SCRIPT_DIR/$LABEL.plist"
PLIST_DST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs"
UID_NUM="$(id -u)"
DOMAIN="gui/$UID_NUM"
SERVICE="$DOMAIN/$LABEL"

cmd_install() {
    if [[ ! -x "$BIN_PATH" ]]; then
        echo "Error: binary not found or not executable: $BIN_PATH" >&2
        exit 1
    fi
    if [[ ! -f "$PLIST_SRC" ]]; then
        echo "Error: plist template not found: $PLIST_SRC" >&2
        exit 1
    fi

    mkdir -p "$HOME/Library/LaunchAgents" "$LOG_DIR"
    sed "s|__BIN_DIR__|$SCRIPT_DIR|g; s|__LOG_DIR__|$LOG_DIR|g" \
        "$PLIST_SRC" > "$PLIST_DST"
    plutil -lint "$PLIST_DST"

    launchctl bootout "$SERVICE" 2>/dev/null || true
    for _ in {1..20}; do
        if ! launchctl print "$SERVICE" >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
    launchctl enable "$SERVICE" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$PLIST_DST"
    echo "Installed and started $LABEL"
    cmd_status
}

cmd_uninstall() {
    launchctl bootout "$SERVICE" 2>/dev/null || true
    if [[ -f "$PLIST_DST" ]]; then
        rm -f "$PLIST_DST"
        echo "Removed $PLIST_DST"
    else
        echo "Plist not installed at $PLIST_DST"
    fi

    python3 "$SCRIPT_DIR/hotkeys.py" enable 60 1
    echo "Uninstalled $LABEL and re-enabled symbolic hotkey 60"
}

cmd_status() {
    if [[ -f "$PLIST_DST" ]]; then
        echo "LaunchAgent plist: installed ($PLIST_DST)"
    else
        echo "LaunchAgent plist: not installed ($PLIST_DST)"
    fi

    if launchctl print "$SERVICE" >/dev/null 2>&1; then
        echo "launchd service: loaded ($SERVICE)"
        launchctl print "$SERVICE" | grep -E "state =|pid =" || true
    else
        echo "launchd service: not loaded ($SERVICE)"
    fi

    echo "Processes:"
    pgrep -fl "cmdspace-switch" || echo "  (none running)"

    echo "Symbolic hotkeys:"
    python3 "$SCRIPT_DIR/hotkeys.py"
}

case "${1:-}" in
    install)
        cmd_install
        ;;
    uninstall)
        cmd_uninstall
        ;;
    status)
        cmd_status
        ;;
    *)
        echo "Usage: $0 {install|uninstall|status}" >&2
        exit 2
        ;;
esac
