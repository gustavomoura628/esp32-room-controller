# ESP32 CO2 Sensor

## TODO

- [ ] Read battery voltage via ADC (voltage divider on a free GPIO)
- [ ] Wire up and integrate the MH-Z19B/C CO2 sensor

Indoor CO2 monitor built on an ESP32-C3 SuperMini with a built-in 0.42" OLED display. Serves a dark-themed web UI for controlling the onboard LED and sending messages to the OLED. CO2 sensing (MH-Z19B/C) is planned but not yet wired up.

## Hardware

| Component | Role |
|-----------|------|
| ESP32-C3 SuperMini + 0.42" OLED | MCU + display |
| MH-Z19B/C NDIR sensor | CO2 measurement (planned) |
| 18650 cell + Battery Shield V3 | Portable power (planned) |

See [COMPONENTS.md](COMPONENTS.md) for full specs, pinouts, and known issues.

## Features

- **Web UI** -- dark theme, LED toggle with visual indicator, OLED message input
- **LED control** -- blue rounded-square indicator, optimistic UI updates
- **OLED display** -- shows IP address (scrolling if too long), LED state, and custom message
- **WiFi** -- STA mode with TX power limited to 11 dBm (antenna defect workaround)

## Building and flashing

Requires [PlatformIO](https://platformio.org/).

```
pio run -t upload
```

Monitor serial output:

```
pio device monitor
```

## Web endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Serves the web UI |
| `/led` | GET | Toggles LED, returns `ON` or `OFF` |
| `/status` | GET | Returns current LED state as plain text |
| `/msg?t=` | GET | Sets OLED message (max 24 chars), redirects to `/` |

## WiFi antenna defect

The ESP32-C3 SuperMini has a known antenna design flaw -- the stock SMD antenna is tuned for 2.7 GHz instead of 2.4 GHz, and a required stripline is missing. TX power must be capped at 11 dBm in software (`WiFi.setTxPower(WIFI_POWER_11dBm)`). A 31mm wire antenna mod can fix this permanently. Details in [WIFI_TEST_LOG.md](WIFI_TEST_LOG.md) and [COMPONENTS.md](COMPONENTS.md).
