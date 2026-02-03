# ESP32 CO2 Sensor

## TODO

- [ ] Read battery voltage via ADC (voltage divider on GPIO4)
  - Wire: Battery+ ── 100kΩ ── GPIO4 ── 100kΩ ── GND
  - Halves voltage: 4.2V→2.1V, 3.0V→1.5V (within ADC range)
  - Display on OLED and web UI
  - Send ntfy push notification when below 3.4V
  - Use for discharging the old dead cell (monitor voltage down to 3.0V)
- [ ] Wire up and integrate the MH-Z19B/C CO2 sensor
- [ ] Wire up HTU21D temperature/humidity sensor (shares I2C bus with OLED)
- [ ] Home Assistant integration (ESPHome + MQTT)
- [ ] Relay for room light control (GPIO7)
- [ ] WS2813 LED strip (GPIO10, 1m 30 LEDs)

Indoor CO2 monitor built on an ESP32-C3 SuperMini with a built-in 0.42" OLED display. Serves a dark-themed web UI for controlling the onboard LED and sending messages to the OLED. CO2 sensing (MH-Z19B/C) is planned but not yet wired up.

## Hardware

| Component | Role |
|-----------|------|
| ESP32-C3 SuperMini + 0.42" OLED | MCU + display |
| MH-Z19B/C NDIR sensor | CO2 measurement (planned) |
| HTU21D | Temperature + humidity (planned) |
| 18650 cell + Battery Shield V3 | Portable power (planned) |
| Relay module | Room light control (planned) |
| WS2813 LED strip (1m, 30 LEDs) | Ambient lighting (planned) |

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

- **Web UI** -- dark theme, LED toggle with visual indicator, OLED message input
- **LED control** -- blue rounded-square indicator, optimistic UI updates
- **OLED display** -- shows IP address (scrolling if too long), LED state, and custom message
- **WiFi** -- STA mode with TX power limited to 8.5 dBm (antenna defect workaround)

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

The ESP32-C3 SuperMini has a known antenna design flaw -- the stock SMD antenna is tuned for 2.7 GHz instead of 2.4 GHz, and a required stripline is missing. TX power must be capped at 8.5 dBm in software (`WiFi.setTxPower(WIFI_POWER_8_5dBm)`). A 31mm wire antenna mod can fix this permanently. Details in [WIFI_TEST_LOG.md](WIFI_TEST_LOG.md) and [COMPONENTS.md](COMPONENTS.md).
