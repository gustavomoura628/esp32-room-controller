# Claude Code — Project Instructions

## Flashing the ESP32

**Do not run `pio run -t upload` or `pio device monitor` directly.** The serial
monitor can freeze Claude Code's shell.

Instead, use the signal file workflow:

1. The user runs `./watch_flash.sh` in a separate terminal (always-on)
2. To trigger a flash and read the result in one shot:
   ```
   touch reflash.signal && sleep 15 && echo "$(($(date +%s) - $(stat -c %Y serial.log)))s ago" && cat serial.log
   ```
   If the age is too high, the watcher may not be running or the flash
   is still in progress — wait and retry.

The watcher script picks up the signal within 1 second, kills any running
flash+monitor, and starts a fresh cycle. Output goes to both the user's
terminal and `serial.log`.

### When to flash

- After any edit to files that affect the firmware (source, headers, config)
- Touch `reflash.signal` and tell the user it's flashing
- Wait a few seconds, then read `serial.log` to check for errors
