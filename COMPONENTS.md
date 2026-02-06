# ESP32 Room Controller -- Component Reference

## Bill of Materials

| # | Component | Role |
|---|-----------|------|
| 1 | ESP32-C3 SuperMini + 0.42" OLED | Microcontroller + display |
| 2 | 400-point breadboard | Prototyping |
| 3 | Electrolytic capacitor kit (1uF-470uF) | Power filtering |
| 4 | MH-Z19B/C NDIR CO2 sensor | CO2 measurement |
| 5 | 18650 Li-ion cell (3.7V, 2600mAh) | Battery power |
| 6 | 18650 Battery Shield V3 | Charging + 5V/3.3V boost |
| 7 | HTU21D / SI7021 / GY-21 | Temperature + humidity |
| 8 | Relay module | Room light control |
| 9 | WS2813 LED strip (1m, 30 LEDs, DC5V) | Ambient lighting |
| 10 | TIP122 Darlington transistor | Relay level shifter (3.3V → 5V) |
| 11 | 700Ω resistor | TIP122 base resistor |
| 12 | 4.7kΩ resistor | Relay IN pull-up to 5V |

---

## 1. ESP32-C3 SuperMini with 0.42" OLED

### Chip Specifications

| Parameter | Value |
|-----------|-------|
| Architecture | 32-bit RISC-V, single-core |
| Clock speed | Up to 160 MHz |
| SRAM | 400 KB (16 KB cache) |
| Flash | 4 MB SPI |
| Wi-Fi | 802.11 b/g/n, 2.4 GHz |
| Bluetooth | BLE 5.0 |
| Antenna | Ceramic (on-board) |
| USB | Native USB-Serial-JTAG (no external chip) |
| Voltage regulator | ME6211 LDO, 500 mA max output |

### Pinout

```
        [USB-C]
   ┌───────────────┐
   │ GPIO3   GPIO0 │
   │ GPIO4   GPIO1 │
   │ GPIO5   GPIO2 │  GPIO5 = OLED SDA (internal)
   │ GPIO6   TX    │  GPIO6 = OLED SCL (internal)
   │ GPIO7   RX    │  TX = GPIO21, RX = GPIO20
   │ GPIO8   V3    │  GPIO8 = blue LED (inverted: LOW=ON)
   │ GPIO9   GD    │  GPIO9 = BOOT button
   │ GPIO10  V5    │
   └───────────────┘
```

**Freely usable GPIOs:** GPIO0, GPIO1, GPIO3 (all ADC-capable)

**Usable with caution (strapping pins):**
- GPIO2 -- must be HIGH at boot
- GPIO8 -- drives onboard blue LED, strapping pin
- GPIO9 -- BOOT button (has pull-up), strapping pin

**Internally committed:**
- GPIO5 (SDA) and GPIO6 (SCL) -- wired to the OLED. Can share the I2C bus with other devices at different addresses.
- GPIO20 (RX) and GPIO21 (TX) -- UART0. Free for general use when USB CDC is enabled (USB serial uses separate internal GPIO18/19). Note: some reports of GPIO20 (RX) not receiving data on battery power — investigate if UART RX issues appear without USB connected.

**Planned GPIO allocation:**

| GPIO | Function |
|------|----------|
| 0 | *Spare (ADC-capable)* |
| 1 | *Spare (ADC-capable)* |
| 3 | *Spare (ADC-capable)* |
| 4 | Battery voltage ADC |
| 5 | I2C SDA (OLED + HTU21D) |
| 6 | I2C SCL (OLED + HTU21D) |
| 7 | Relay (room light) |
| 8 | Onboard blue LED |
| 9 | BOOT button |
| 10 | WS2813 LED strip data |
| 20 | UART RX (MH-Z19C) |
| 21 | UART TX (MH-Z19C) |

### Built-in 0.42" OLED

| Parameter | Value |
|-----------|-------|
| Resolution | 72 x 40 pixels |
| Controller | SSD1306-compatible |
| Interface | I2C |
| I2C address | 0x3C |
| SDA / SCL | GPIO5 / GPIO6 |

**Library support:** Use **U8g2** -- it has a native 72x40 constructor. The Adafruit SSD1306 library does NOT support 72x40 and will produce garbled output.

```cpp
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);
```

### Current Consumption

| Mode | Current |
|------|---------|
| Active + WiFi TX | ~335 mA peak |
| Active, no WiFi | ~20-24 mA |
| Deep sleep (board-level) | ~400-600 uA |
| Deep sleep (red LED removed) | ~100 uA |

The high deep-sleep current is caused by the ME6211 LDO quiescent draw and the red power LED. Desoldering the red LED helps significantly.

### Arduino IDE Setup

- Board: **ESP32C3 Dev Module**
- **USB CDC On Boot: Enabled** (required for Serial Monitor over USB)
- Flash Mode: QIO, Flash Size: 4MB
- Define `LED_BUILTIN` manually: `#define LED_BUILTIN 8`
- First upload may require holding BOOT while pressing RESET

### Known Issues

- **WiFi antenna defect (most batches):** The stock CrossAir CA-C03 SMD antenna is tuned for 2.7 GHz, not 2.4 GHz, and the board is missing a 3.8mm stripline required by the antenna datasheet. The crystal is also placed too close to the antenna, causing TX distortion at higher power levels. Software workaround: `WiFi.setTxPower(WIFI_POWER_8_5dBm)` — the only reliable power level (11 dBm connects but drops requests, 13 dBm and default fail entirely). Hardware fix: solder a 31mm quarter-wave wire antenna onto the chip antenna pads (6-10 dB gain, allows full TX power). See: https://peterneufeld.wordpress.com/2025/03/04/esp32-c3-supermini-antenna-modification/
- **Single core:** No dual-core task pinning. All FreeRTOS tasks share one RISC-V core.
- **No DAC, no touch pins** (unlike classic ESP32).
- **GPIO8/9 for I2C causes boot failures** -- external pull-ups interfere with strapping. Avoid.

---

## 2. MH-Z19B/C NDIR CO2 Sensor

### How NDIR Sensing Works

The sensor uses Non-Dispersive Infrared spectroscopy. CO2 absorbs IR light at 4.26 um. An IR lamp pulses through a gold-plated chamber, and a thermopile detector behind a narrowband filter measures how much light was absorbed. A parallel reference channel (filled with non-absorbing gas) compensates for lamp aging and contamination. The ratio of active-to-reference signals gives CO2 concentration via the Beer-Lambert law.

### Electrical Specifications

| Parameter | MH-Z19B | MH-Z19C |
|-----------|---------|---------|
| Supply voltage | 4.5 - 5.5V | 5.0 +/- 0.1V (strict!) |
| Average current | < 60 mA | < 40 mA |
| Peak current | 150 mA | 125 mA |
| UART logic level | 3.3V TTL | 3.3V TTL |
| Sampling period | 5 seconds | 1 second |

### Accuracy

| Parameter | MH-Z19B | MH-Z19C |
|-----------|---------|---------|
| Range | 0-2000/5000/10000 ppm | 400-5000 ppm |
| Accuracy | +/- (50 ppm + 3% of reading) | +/- (50 ppm + 5% of reading) |
| Resolution | 1 ppm | 1 ppm |
| Response time (T90) | < 120 s | < 120 s |
| Preheat time | 3 min | 2.5 min |
| Operating temp | 0 to 50 C | -10 to 50 C |

Example accuracy at 1000 ppm (MH-Z19B): +/- (50 + 30) = +/- 80 ppm, so reading could be 920-1080.

### Pin Connections (7-pin header with cable)

**MH-Z19B:**

| Pin | Wire Color | Function |
|-----|------------|----------|
| 1 | Yellow | NC |
| 2 | Green | UART TX -> connect to ESP RX |
| 3 | Blue | UART RX -> connect to ESP TX |
| 4 | Red | Vin (5V) |
| 5 | Black | GND |
| 6 | White | NC |
| 7 | Brown | Analog output |

**MH-Z19C (our unit):**

Unit info: MH-Z19C, 400-5000PPM, date 20251025, batch HX40024.

Physical layout — two pin headers, left (5 pins) and right (4 pins):

```
Left (5 pins):   HD | blank | Tx | Rx | blank
Right (4 pins):  PWM | blank | GND | Vin
```

Wiring to ESP32-C3 SuperMini:

| Sensor Pin | Connect to |
|------------|------------|
| Tx (left, pin 3) | GPIO20 (ESP32 RX) |
| Rx (left, pin 4) | GPIO21 (ESP32 TX) |
| Vin (right, pin 4) | 5V rail |
| GND (right, pin 3) | GND |

TX/RX are cross-connected (sensor TX -> ESP RX, sensor RX -> ESP TX). Logic
levels are 3.3V, no level shifter needed. Place a 100-470uF electrolytic cap
between Vin and GND right at the sensor to absorb 125mA current peaks.

Do not touch the HD pin (calibration trigger).

### UART Protocol

**Settings:** 9600 baud, 8N1. Logic levels are 3.3V -- directly compatible with ESP32-C3, no level shifter needed.

#### Read CO2 (command 0x86)

```
TX: FF 01 86 00 00 00 00 00 79
RX: FF 86 [CO2_H] [CO2_L] [TEMP] [STATUS] [xx] [xx] [CKSUM]
```

- CO2 (ppm) = `CO2_H * 256 + CO2_L`
- Temperature = `TEMP - 40` (internal only, not accurate for display)
- Minimum polling interval: 5 seconds (MH-Z19B) / 1 second (MH-Z19C)

#### Checksum Calculation

```c
uint8_t checksum(uint8_t *packet) {
    uint8_t sum = 0;
    for (int i = 1; i <= 7; i++) sum += packet[i];
    return 0xFF - sum + 1;
}
```

Always validate response checksums. Discard frames that fail.

#### Other Commands

| Command | Bytes | Description |
|---------|-------|-------------|
| Zero calibration | `FF 01 87 00 00 00 00 00 78` | Calibrates to 400 ppm (NOT 0!) |
| ABC on | `FF 01 79 A0 00 00 00 00 E6` | Enable auto-calibration |
| ABC off | `FF 01 79 00 00 00 00 00 86` | Disable auto-calibration |
| Set range 5000 | `FF 01 99 00 00 00 13 88 CB` | Set detection range |
| Get firmware | `FF 01 A0 00 00 00 00 00 5F` | Returns 4-char version string |

#### Undocumented Commands (community-discovered)

| Command | Description |
|---------|-------------|
| 0x7D | Get ABC status |
| 0x85 | Raw CO2 (unclipped) + float temperature |
| 0x9B | Get current detection range |
| 0xA0 | Get firmware version |

Use undocumented commands with caution.

### PWM Output

Cycle duration: ~1004 ms. Formula:

```
CO2 (ppm) = Range * (T_H - 2ms) / (T_H + T_L - 4ms)
```

UART is recommended over PWM for richer data and better precision.

### Warm-Up Behavior

The sensor has **no status register** for warm-up. You must use a timer.

| Phase | Time | Behavior |
|-------|------|----------|
| Phase 1 | 0-20 s | May return 0 ppm or CRC errors |
| Phase 2 | 20-60 s | Returns placeholder (~400-410 ppm) |
| Phase 3 | 60-180 s | Readings start responding but still drifting |
| Stabilized | > 3 min | Reliable readings |

**Discard all readings for the first 3 minutes.** For calibration, wait 20+ minutes.

### ABC (Auto Baseline Correction)

ABC records the lowest CO2 reading each 24-hour period and assumes it equals 400 ppm (outdoor ambient). It adjusts the baseline accordingly. Factory default is **ON**.

#### When to Keep ABC On

- Rooms ventilated at least once daily (offices, classrooms, living rooms)
- Permanent installations where you want zero maintenance

#### When to Disable ABC

- Greenhouses, breweries, sealed chambers
- Bedrooms occupied 24/7 that never reach 400 ppm
- Battery-powered devices that power-cycle the sensor
- Any space not ventilated daily

#### ABC Pitfalls

1. **Non-ventilated rooms:** If the daily minimum is 800 ppm, ABC gradually shifts so 800 reads as 400. ALL readings become 400 ppm too low. This accumulates daily.
2. **Power cycling:** ABC needs uninterrupted 24-hour cycles. Frequent power cycling corrupts calibration.
3. **First 24 hours:** ABC can overwrite factory calibration with bad data before experiencing a full cycle.
4. **Ships enabled by default.** You must explicitly disable it if needed.

### Manual Calibration

**"Zero point" calibration = 400 ppm, NOT 0 ppm.** Do not use nitrogen gas.

Procedure:
1. Place sensor outdoors in clean air (away from roads, kitchens, exhaust)
2. Power on and wait **20+ minutes**
3. Send `FF 01 87 00 00 00 00 00 78` (or pull HD pin LOW for 7+ seconds)
4. Calibration stored in EEPROM

With ABC disabled, recalibrate every ~6 months.

### MH-Z19B vs MH-Z19C: Which Do You Have?

| Feature | MH-Z19B | MH-Z19C |
|---------|---------|---------|
| Voltage tolerance | 4.5-5.5V (forgiving) | 5.0 +/- 0.1V (strict) |
| Power consumption | Higher (60mA avg) | Lower (40mA avg) |
| Accuracy | Better (+/- 3%) | Worse (+/- 5%) |
| Update rate | 5 seconds | 1 second |
| Gas windows | 2 (dual optical path) | 1 (single) |

**The MH-Z19C's strict 5V requirement is a known pain point.** If you have the C variant and power it from a battery shield, verify the 5V rail stays within 4.9-5.1V under load.

### Counterfeit Sensors

Fakes are widespread on AliExpress. Signs of a counterfeit:

| Indicator | Genuine | Fake |
|-----------|---------|------|
| PCB color | Green | Black |
| Housing texture | Embossed, textured | Smooth, plain |
| Firmware version | 0430, 0443 | Often 0436 |
| Reading stability | Smooth | Up to 50 ppm jitter |
| Price | $15-25 | Suspiciously cheap ($5-8) |

Query firmware with command `0xA0` after power-up to check.

### Placement and Enclosure Tips

- Diffusion window must have unobstructed airflow (min 1mm gap to enclosure)
- Shield from direct sunlight and IR sources (interferes with measurement)
- Mount at breathing height (~1-1.5m) for room air quality monitoring
- Keep away from heat sources, MCU exhaust, and where people directly exhale
- Exhaled breath is ~40,000 ppm CO2 -- even breathing near the sensor during testing causes huge spikes

### Soldering Precautions

- Max iron temperature: 350 C
- Max contact time per pad: 3 seconds
- Do not apply pressure to the gold-plated housing

### Long-Term Drift

Without calibration, baseline can drift to 800 ppm (from 400) within 3 months. With ABC in a ventilated room or manual calibration every 6 months, the sensor stays in spec for its rated 5+ year lifespan.

---

## 3. 18650 Battery Shield V3

### Overview

A charge/boost board that accepts an 18650 cell and provides regulated 5V and 3.3V outputs. Multiple hardware revisions exist with different ICs.

### Charging

| Parameter | Value |
|-----------|-------|
| Charger IC | TP4056/ME4057 (older) or IP5306 (newer) |
| Input | Micro USB, 5V |
| Charge current | 0.5A (USB-limited) |
| Charge voltage | 4.2V (CC/CV) |
| Termination | C/10 method |
| Charge time (2600mAh) | ~5-6 hours from empty |

### Output

| Rail | Voltage | Source |
|------|---------|--------|
| 5V pads | 5.0V | Boost converter from battery |
| 3.3V pads | ~3.3V | LDO fed from 5V boost |

Architecture: Battery (3.0-4.2V) -> Boost -> 5V -> LDO -> 3.3V

**The physical switch only controls the USB-A output port. The 5V/3.3V solder pads are always live when a battery is inserted.**

### LED Indicators

| LED | Meaning |
|-----|---------|
| Red solid | Charging |
| Green solid | Fully charged |
| Both on | Abnormal (common defect on some boards) |

### Protection

The board provides basic overcharge protection via the charger IC. Overdischarge protection depends on variant -- some have DW01A + 8205A protection, others rely solely on the charger IC. **No output overcurrent or short-circuit protection** on the 5V/3.3V rails.

### Micro USB Connector Repair

The micro USB charging port was ripped off the board from repeated use. Repaired
by soldering wires directly to the input pads where the connector used to sit.
Only VBUS (+5V) and GND are needed since the port is just for charging.

To identify the pads: use a multimeter in continuity mode — GND has continuity
with the battery negative terminal and the large shield tabs. VBUS is the
remaining signal pad that traces back to the charger IC (U1).

### Battery Voltage Monitoring

Read cell voltage via ADC through a voltage divider on GPIO4:

```
Battery + ── 100kΩ ──┬── 100kΩ ── GND
                     │
                   GPIO4
```

Two equal 100kΩ resistors halve the voltage, mapping the 3.0-4.2V cell range
to 1.5-2.1V (within the ESP32-C3 ADC range). The high resistance means
negligible drain (~21uA).

Firmware will send a push notification via ntfy when the cell drops below 3.4V.

### Critical Pitfalls

1. **Reverse battery polarity destroys the board.** No protection. Double-check +/- markings.
2. **Auto-shutdown with low loads (IP5306 variant).** If load stays below ~50mA for 32 seconds, the board shuts off. This kills deep-sleep applications. Workaround: periodic "keep-alive" pulse.
3. **High quiescent current (~40-100mA).** The boost converter draws significant idle current even with no load. This dominates battery life in low-power scenarios.
4. **Pass-through charging is unreliable.** Brief power dropout when switching between USB and battery power. Not suitable as a UPS without hardware modification.
5. **Voltage sag under load.** Some boards can't sustain 5V under higher current -- drops to ~3.4V.
6. **Protected 18650 cells may not fit.** They're 68-70mm vs 65mm for unprotected. Check the holder.

---

## 4. 18650 Battery (3.7V, 2600mAh)

### Voltage Characteristics

| State | Voltage |
|-------|---------|
| Fully charged | 4.20V |
| Nominal | 3.70V |
| Low (stop using) | 3.00V |
| Storage | 3.70-3.85V |

### Safety

- Never charge above 4.2V or discharge below 2.5V
- Never short-circuit, puncture, crush, or expose to fire
- Store at 40-50% charge for long-term storage
- Buy from reputable manufacturers (Samsung, LG, Panasonic, Sony/Murata)
- Counterfeits are extremely common

### Protected vs Unprotected

Protected cells have a small PCB that adds overcharge, overdischarge, and short-circuit protection but makes them 3-5mm longer. **Use a protected cell** unless the shield has its own robust protection circuit.

### Runtime Estimates

| Scenario | Avg Draw | Runtime |
|----------|----------|---------|
| Always-on + WiFi (through shield) | ~200mA at 5V | **~7-8 hours** |
| Always-on + WiFi (direct, no shield overhead) | ~200mA at 3.3V | **~12 hours** |

The shield's quiescent current (40-100mA) is a major penalty. At 85% boost efficiency, a 200mA load at 5V translates to ~318mA draw from the battery.

---

## 5. Breadboard (400 Tie Points)

Standard solderless breadboard. 400 tie points arranged in two halves of 30 rows x 5 columns, separated by a center channel, with 2 power rails on each side.

Tips:
- Power the MH-Z19 from the 5V rail with capacitors placed close to it
- Keep the sensor's UART wires short to avoid noise
- Use separate power rails for 5V (sensor) and 3.3V (ESP32) if needed

---

## 6. Capacitor Kit

The kit includes values from 1uF to 470uF at various voltage ratings (16V, 25V, 50V).

**For this project, use:**
- **100-470uF electrolytic** on the MH-Z19's 5V power input (absorbs 150mA peak current pulses)
- **0.1uF ceramic** (if available in the kit) right at the sensor's power pins for high-frequency decoupling
- **10-47uF** on the 3.3V rail near the ESP32 if powered from the battery shield

Place capacitors as close to the sensor's VIN/GND pins as physically possible.

---

## 7. HTU21D / SI7021 / GY-21 Temperature & Humidity Sensor

These are all the same silicon (SI7021) on different breakout boards. Any of
them works identically.

| Parameter | Value |
|-----------|-------|
| Interface | I2C |
| I2C address | 0x40 |
| Supply voltage | 3.3V |
| Temperature accuracy | +/- 0.3 C |
| Humidity accuracy | +/- 3% RH |
| Current draw | ~150 uA active, <1 uA standby |

Shares the I2C bus with the OLED (GPIO5 SDA, GPIO6 SCL on the ESP32-C3
SuperMini). No additional wiring needed beyond VIN, GND, and the shared bus.

**The paper/membrane on top is a dust filter — leave it on.** It protects the
sensing element from contamination while allowing moisture through.

Note: The MH-Z19 has a built-in temperature reading, but it's inaccurate and
only meant for internal compensation. Use this dedicated sensor for actual
temperature/humidity display.

---

## 8. Relay Module

3-pin relay module (driver circuit built in) on GPIO7 for room light control
(220V room light bulb).

The relay module requires 5V on its IN pin to activate, but the ESP32-C3 GPIO
outputs 3.3V. A TIP122 Darlington NPN transistor acts as a level shifter. A
4.7kΩ pull-up holds IN at 5V when the transistor is off; when GPIO7 goes HIGH,
the TIP122 pulls IN to GND, switching the relay. This inverts the logic — the
firmware accounts for it.

### Module wiring

| Wire colour | Pin | Connect to |
|-------------|-----|------------|
| Orange | VCC | 5V rail |
| Brown | GND | Shared GND |
| Yellow | IN | TIP122 collector + 4.7kΩ pull-up to 5V |

### TIP122 wiring

Hold the TIP122 with the label facing you (metal tab away):

```
Left = Base    → 700Ω resistor → GPIO7
Middle = Collector → Relay IN (yellow wire) + 4.7kΩ to 5V
Right = Emitter    → GND
```

### Known Issue: CO2 sensor corruption on shared 5V rail

The relay coil draws ~70-200mA continuously while energized. When powered from the same USB 5V rail as the MH-Z19C, this causes a permanent voltage sag that corrupts the NDIR CO2 measurement. Symptoms: CO2 readings climb from ~700 to ~5000 ppm (sensor max) over ~40 seconds while the relay is ON, then recover over ~30 seconds after relay OFF. Temperature and humidity sensors are unaffected.

This is not a transient spike -- a 470µF decoupling capacitor does not help. The sag persists as long as the coil is energized.

**Fix:** Power the relay circuit from a separate 5V supply (e.g., a dedicated USB charger or power strip PSU). Both the relay module VCC and the TIP122 collector-side 5V (including the 4.7kΩ pull-up) should be on the separate supply. The TIP122 base is still driven by ESP32 GPIO7.

Confirmed by testing: identical behavior on both Arduino and ESPHome firmware, persists with mains breaker off (ruling out contact-side effects), disappears completely with relay on separate 5V PSU.

---

## 9. WS2813 LED Strip

| Parameter | Value |
|-----------|-------|
| Type | WS2813 (dual data line) |
| Voltage | DC 5V |
| LEDs | 30 per meter, 1m length |
| Rating | IP30 (no waterproofing) |
| Data pin | GPIO10 |

WS2813 is similar to WS2812B but has a backup data line — if one LED dies, the
signal bypasses it and the rest of the strip keeps working.

The ESP32-C3 outputs 3.3V logic but WS2813 expects 5V. At 30 LEDs on short
wires this usually works fine. If you get flickering, add a logic level shifter
or wire the data line through the DIN of a sacrificial first pixel powered at
5V.

Peak current at full white: 30 LEDs x ~50mA = 1.5A. Power the strip directly
from a 5V supply, not through the ESP32.

### Connector wiring

The strip comes with a 4-wire connector. Pads between LEDs read
`GND  BO-BI  DO-DI  +5V`.

| Wire colour | Pad | Connect to |
|-------------|-----|------------|
| White | GND | Shared GND |
| Blue | DO-DI | GPIO10 (data) |
| Green | BO-BI | Leave unconnected (backup data) |
| Red | +5V | 5V charger rail |
