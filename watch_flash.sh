#!/bin/bash
#
# watch_flash.sh — File-triggered flash+monitor loop
#
# Polls for a signal file and re-runs build+upload+monitor whenever it
# appears.  Designed to be driven by another process (e.g. Claude Code
# touching reflash.signal) while this script runs in a dedicated terminal.
#
# The build runs separately so flash.sh can report build errors cleanly.
# Upload+monitor run combined (pio run -t upload -t monitor) so the
# serial transition is seamless and no ESP32 output is lost.
#
# Usage:
#   Terminal 1:  ./watch_flash.sh
#   Terminal 2:  touch reflash.signal    (triggers build+flash+monitor)
#   Ctrl+C in Terminal 1 to stop.

SIGNAL="./reflash.signal"
LOG="./serial.log"
FLASH_PID=""

# --- Inner mode: called by the outer loop via `script` ----------------
if [ "$1" = "--run" ]; then
    > "$LOG"

    # Phase 1: Build only
    echo "===PHASE:BUILD===" >> "$LOG"
    pio run 2>&1 | stdbuf -oL tee -a "$LOG"
    if [ "${PIPESTATUS[0]}" -ne 0 ]; then
        echo "===RESULT:BUILD_FAILED===" >> "$LOG"
        exit 1
    fi
    echo "===RESULT:BUILD_OK===" >> "$LOG"

    # Phase 2: Upload + Monitor combined (seamless serial transition)
    # Runs until killed by outer loop on next signal or Ctrl+C.
    # flash.sh handles its own 5s read window.
    echo "===PHASE:FLASH===" >> "$LOG"
    pio run -t upload -t monitor 2>&1 | stdbuf -oL tee -a "$LOG"
fi

# --- Outer mode: poll for signal, launch inner via script -------------
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

        # Kill previous run if still going
        if [ -n "$FLASH_PID" ] && kill -0 "$FLASH_PID" 2>/dev/null; then
            kill "$FLASH_PID" 2>/dev/null
            pkill -P "$FLASH_PID" 2>/dev/null
            wait "$FLASH_PID" 2>/dev/null
        fi

        # Start new run in a pseudo-TTY (needed for pio device monitor)
        script -qfc "$0 --run" /dev/null &
        FLASH_PID=$!
    fi
    sleep 1
done
