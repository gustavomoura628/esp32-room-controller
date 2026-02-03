#!/bin/bash
#
# watch_flash.sh — File-triggered flash+monitor loop
#
# Polls for a signal file and re-runs flash.sh whenever it appears.
# Designed to be driven by another process (e.g. Claude Code touching
# reflash.signal after editing code) while this script runs in a
# dedicated terminal.
#
# Why a signal file instead of running flash.sh directly?
#   pio's serial monitor sometimes freezes Claude Code's shell, making
#   it unrecoverable. By decoupling the two, Claude Code just touches a
#   file and this script handles the flash+monitor in its own terminal.
#
# Why `script -qfc`?
#   flash.sh runs in the background (&) so the watcher loop can keep
#   checking for new signals. Background processes lose their TTY,
#   which breaks miniterm (the serial monitor). The `script` command
#   allocates a pseudo-TTY for the child process, solving this.
#   Flags: -q (quiet), -f (flush output immediately), -c (run command).
#
# Usage:
#   Terminal 1:  ./watch_flash.sh
#   Terminal 2:  touch reflash.signal    (triggers build+flash+monitor)
#                touch reflash.signal    (kills current, starts fresh)
#   Ctrl+C in Terminal 1 to stop.

SIGNAL="./reflash.signal"
FLASH_PID=""

cleanup() {
    if [ -n "$FLASH_PID" ]; then
        kill "$FLASH_PID" 2>/dev/null
        pkill -P "$FLASH_PID" 2>/dev/null
    fi
    exit 0
}
trap cleanup INT TERM

while true; do
    if [ -f "$SIGNAL" ]; then
        rm -f "$SIGNAL"

        # Kill previous flash+monitor if still running
        if [ -n "$FLASH_PID" ] && kill -0 "$FLASH_PID" 2>/dev/null; then
            kill "$FLASH_PID" 2>/dev/null
            pkill -P "$FLASH_PID" 2>/dev/null
            wait "$FLASH_PID" 2>/dev/null
        fi

        # Start new flash+monitor in a pseudo-TTY
        script -qfc "./flash.sh" /dev/null &
        FLASH_PID=$!
    fi
    sleep 1
done
