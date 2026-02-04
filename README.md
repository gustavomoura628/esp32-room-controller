# ESP32 Room Controller

## TODO

- [x] Read battery voltage via ADC (voltage divider on GPIO4)
- [x] Wire up and integrate the MH-Z19C CO2 sensor
- [x] Wire up HTU21D temperature/humidity sensor (shares I2C bus with OLED)
- [ ] Home Assistant integration (ESPHome + MQTT)
- [x] Relay for room light control (GPIO7)
- [x] WS2813 LED strip (GPIO10, 1m 30 LEDs)

Indoor room controller built on an ESP32-C3 SuperMini with a built-in 0.42" OLED display. Monitors CO2, temperature, and humidity via MH-Z19C and HTU21D sensors. Controls a room light via relay and a WS2813 LED strip. Serves a dark-themed web UI with live sensor readings and controls.

![Web UI](screenshot.png)

## Hardware

| Component | Role |
|-----------|------|
| ESP32-C3 SuperMini + 0.42" OLED | MCU + display |
| MH-Z19C NDIR sensor | CO2 measurement |
| HTU21D | Temperature + humidity |
| 18650 cell + Battery Shield V3 | Portable power + voltage monitoring |
| Relay module + TIP122 | Room light control |
| WS2813 LED strip (1m, 30 LEDs) | Ambient lighting |

See [COMPONENTS.md](COMPONENTS.md) for full specs, pinouts, and known issues.

## Power modes

**Battery mode (portable sensors):** ESP32 + CO2 sensor + HTU21D powered by the
18650 battery shield. No relay or LED strip — they draw too much for battery.
Battery voltage monitored via ADC on GPIO4 with ntfy alerts below 3.4V.

**Wall mode (sensors + lights):** Everything powered from a 5V 2A+ phone charger.
The charger feeds a shared 5V rail on the breadboard — ESP32 (via V5 pin),
MH-Z19C, relay, and LED strip all tap from it. No battery needed. Worst-case
draw is ~2A (LED strip at full white + everything else).

## Features

- **Web UI** -- dark theme, live CO2/temp/humidity readings, battery voltage, LED strip controls, room light relay
- **LED strip** -- WS2813, 30 LEDs, solid color + rainbow mode, brightness slider, color picker
- **OLED display** -- shows IP address (scrolling if too long), CO2 ppm, battery voltage, temp/humidity
- **CO2 monitoring** -- MH-Z19C NDIR sensor, 5s polling, error status on web UI (warmup, timeout, desync, CRC)
- **Temperature & humidity** -- HTU21D sensor on shared I2C bus, 5s polling
- **Battery monitoring** -- ADC via voltage divider on GPIO4, ntfy alerts below 3.4V
- **Push notifications** -- ntfy alerts for boot and low battery
- **WiFi** -- STA mode with TX power limited to 8.5 dBm (antenna defect workaround)

## Building and flashing

Requires [PlatformIO](https://platformio.org/).

```
pio run -t upload -t monitor
```

When using Claude Code, use `./flash.sh` instead (requires `./watch_flash.sh`
running in a separate terminal). See [CLAUDE.md](CLAUDE.md).

## Web endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Serves the web UI |
| `/led` | GET | Toggles LED, returns `ON` or `OFF` |
| `/status` | GET | Returns current LED state as plain text |
| `/battery` | GET | Returns battery voltage as plain text (e.g. `3.82`) |
| `/co2` | GET | Returns CO2 ppm |
| `/co2status` | GET | Returns JSON: `result` (error code), `uptime` (seconds), `ppm` |
| `/co2temp` | GET | Returns CO2 sensor internal temp (unreliable, 0 before first read) |
| `/temp` | GET | Returns HTU21D temperature in °C |
| `/humidity` | GET | Returns HTU21D humidity in %RH |
| `/relay` | GET | Toggles relay, returns `ON` or `OFF` |
| `/relaystatus` | GET | Returns current relay state as plain text |
| `/strip` | GET | LED strip control: `on`, `brightness`, `mode`, `r`, `g`, `b` params |
| `/poll` | GET | Returns combined LED, relay, and strip state as JSON |

## WiFi antenna defect

The ESP32-C3 SuperMini has a known antenna design flaw -- the stock SMD antenna is tuned for 2.7 GHz instead of 2.4 GHz, and a required stripline is missing. TX power must be capped at 8.5 dBm in software (`WiFi.setTxPower(WIFI_POWER_8_5dBm)`). A 31mm wire antenna mod can fix this permanently. Details in [WIFI_TEST_LOG.md](WIFI_TEST_LOG.md) and [COMPONENTS.md](COMPONENTS.md).
