#!/bin/bash
#
# flash.sh — Trigger a flash and show results
#
# Touches reflash.signal (picked up by watch_flash.sh), then monitors
# serial.log for phase markers.  Only prints output on failure (build
# or upload).  For the monitor phase, waits 5 seconds after the serial
# monitor connects, then prints the captured ESP32 output.
#
# Usage:
#   ./flash.sh

SIGNAL="./reflash.signal"
LOG="./serial.log"
TIMEOUT=90

# Check if a pattern exists in the flash phase section of the log.
flash_has() {
    sed -n '/===PHASE:FLASH===/,$ p' "$LOG" 2>/dev/null | grep -q -- "$1"
}

# Truncate log so we only see fresh output
> "$LOG"

# Trigger the flash
touch "$SIGNAL"

start=$(date +%s)
deadline=$((start + TIMEOUT))

# ── Phase 1: Build ───────────────────────────────────────────────────
echo -n "Building"
while true; do
    grep -q "===PHASE:BUILD===" "$LOG" 2>/dev/null && break
    if [ "$(date +%s)" -ge "$((start + 20))" ]; then
        echo " - timed out waiting for watch_flash.sh"
        exit 1
    fi
    sleep 0.5
done

while true; do
    grep -q "===RESULT:BUILD_OK===" "$LOG" 2>/dev/null && { echo " - OK"; break; }
    grep -q "===RESULT:BUILD_FAILED===" "$LOG" 2>/dev/null && {
        echo " - FAILED"
        echo ""
        awk '/===PHASE:BUILD===/{f=1;next} /===RESULT:BUILD_FAILED===/{exit} f' "$LOG"
        exit 1
    }
    [ "$(date +%s)" -ge "$deadline" ] && { echo " - TIMEOUT"; exit 1; }
    sleep 0.5
done

# ── Phase 2: Upload ─────────────────────────────────────────────────
# With combined -t upload -t monitor, PlatformIO doesn't print [SUCCESS]
# between targets.  The monitor starting (--- Terminal) means upload worked.
echo -n "Uploading"
while true; do
    flash_has "--- Terminal" && { echo " - OK"; break; }
    flash_has "\\[FAILED\\]" && {
        echo " - FAILED"
        echo ""
        awk '/===PHASE:FLASH===/{f=1;next} f' "$LOG"
        exit 1
    }
    [ "$(date +%s)" -ge "$deadline" ] && { echo " - TIMEOUT"; exit 1; }
    sleep 0.5
done

# ── Phase 3: Serial output ──────────────────────────────────────────
# Wait 5 seconds for ESP32 output to accumulate, then print it.
sleep 5
echo "Serial output:"
awk '
    /===PHASE:FLASH===/ { flash=1 }
    flash && /--- Quit:/ { output=1; next }
    output
' "$LOG"
