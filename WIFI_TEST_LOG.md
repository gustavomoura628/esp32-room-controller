# WiFi Test Log

## Board Info
- **MCU:** ESP32-C3 SuperMini (revision v0.4)
- **MAC:** 10:00:3b:c5:3a:dc
- **Firmware:** WiFi demo (`src/main.cpp`)
- **Target SSID:** your-ssid

---

## Test 1 — 2026-02-02

**Firmware flashed:** OK (6.57s, 783 KB flash used)

**Serial output (partial capture):**
```
  status: 6 (attempt 28/30)
  status: 6 (attempt 29/30)
  status: 6 (attempt 30/30)
First attempt failed, scanning networks...
Retrying...
  status: 4 (retry 1/30)
  status: 4 (retry 2/30)
  status: 4 (retry 3/30)
  status: 4 (retry 4/30)
  status: 4 (retry 5/30)
  status: 4 (retry 6/30)
```

**Result:** FAILED

**Status codes observed:**
- `6` (WL_DISCONNECTED) — first 30 attempts all failed
- `4` (WL_CONNECT_FAILED) — retries also failing

**Diagnosis:**

| Possible Cause              | Status | Notes                                        |
|-----------------------------|--------|----------------------------------------------|
| Wrong SSID or password      | RULED OUT | Password confirmed correct                |
| Router 5 GHz only           | RULED OUT | your-ssid broadcasts on 2.4 GHz (2457 MHz, ch 10) at 87% signal |
| WPA3 / unsupported security | RULED OUT | WPA2 CCMP/PSK — fully supported by ESP32-C3 |
| Router MAC filtering        | RULED OUT | Tested from second PC, same result         |
| Signal too weak             | RULED OUT | 87% signal strength from host PC           |
| ESP32-C3 SuperMini antenna defect | **CONFIRMED** | See Test 5 for proof             |

**Router details (from host PC scan):**
```
SSID: your-ssid
BSSID: F8:D2:AC:4C:F7:88
Security: WPA2 (pair_ccmp group_ccmp psk)
Channel: 10 (2457 MHz)
Signal: 87%
```

---

## Test 2 — 2026-02-02

**Change:** Moved board right next to the router (no firmware change).

**Serial output:**
```
  status: 4 (retry 1/30)
  ...
  status: 4 (retry 30/30)
WiFi FAILED. Final status: 4
```

**Result:** FAILED

Proximity made no difference. Consistent with the antenna defect — the
crystal-antenna interference causes TX distortion, not just low RX
sensitivity.

---

## Test 3 — 2026-02-02

**Change:** Added `WiFi.setTxPower(WIFI_POWER_8_5dBm)` before `WiFi.begin()`.
This is the documented workaround for the SuperMini antenna defect
(COMPONENTS.md line 96). Lowering TX power reduces reflections from the
misplaced crystal back into the antenna.

**Serial output:**
```
RSSI: -40
Web server started.
```

**OLED display showed:** `192.168.0.24` — but this was **truncated** (see
OLED Bug below). The real IP was `192.168.0.242`.

**Result:** SUCCESS (STA connected, web server started) — but we didn't
know the correct IP at the time, so we couldn't verify reachability.

---

## Test 3b — 2026-02-02

**Change:** Same firmware as Test 3. Tried reaching the web server from the
network using the IP shown on the OLED.

**Network checks:**
- `ping 192.168.0.24` → Destination Host Unreachable
- ARP entry for 192.168.0.24 → `(incomplete)` / FAILED
- Same result from second PC on the network (192.168.0.200, wired)

**Result:** FALSE NEGATIVE

We were pinging `192.168.0.24` — a non-existent address. The real IP was
`192.168.0.242` but the OLED truncated the last digit. The ESP32 was
connected and working the whole time.

---

## Test 3c — 2026-02-02

**Change:** Tried `WIFI_POWER_11dBm` instead of 8.5 dBm (STA mode).

**Serial output:**
```
  status: 4 (retry 1/30)
  ...
  status: 4 (retry 30/30)
WiFi FAILED. Final status: 4
```

**Result:** FAILED — 11 dBm is too high, antenna defect prevents connection.

---

## Test 4 — 2026-02-02

**Change:** Switched to **AP mode** with `WIFI_POWER_8_5dBm`.

- SSID: `ESP32-CO2`, Password: `esp32test`
- IP: `192.168.4.1`

**Result:** SUCCESS

Connected PC to `ESP32-CO2` network, opened `http://192.168.4.1` — web
page loaded, LED toggle and OLED message features all working. Full two-way
communication confirmed.

---

## Test 5 — 2026-02-02

**Change:** Same AP mode but **removed** the `WiFi.setTxPower(WIFI_POWER_8_5dBm)`
call (full TX power, default ~20 dBm).

**Result:** FAILED — AP mode also broken at full TX power. This confirms
the antenna defect is real and affects both STA and AP modes.

---

## Test 6 — 2026-02-02

**Change:** Back to STA mode with `WIFI_POWER_8_5dBm`. Added extra debug
output (gateway, subnet, DNS, channel, BSSID). Simplified connection logic
to a single 30-attempt loop.

**Serial output:**
```
MAC: 10:00:3B:C5:3A:DC
Connecting to 'your-ssid'
  status: 3 (attempt 1/30)
Connected! IP: 192.168.0.242  RSSI: -29
Gateway: 192.168.0.1
Subnet: 255.255.255.0
DNS: 181.213.132.2
Channel: 10
BSSID: F8:D2:AC:4C:F7:88
Web server started.
```

**Ping from host PC:**
```
5 packets transmitted, 4 received, 20% packet loss
rtt min/avg/max/mdev = 22.399/58.054/102.763/36.000 ms
```

**Browser:** `http://192.168.0.242` — web page loads, LED toggle and OLED
message features all working.

**Result:** SUCCESS — full STA mode working.

---

## Final Summary

| Test | Mode | TX Power | Connects? | Reachable? | Web works? |
|------|------|----------|-----------|------------|------------|
| 1    | STA  | Default  | No        | —          | —          |
| 2    | STA  | Default (close) | No  | —          | —          |
| 3    | STA  | 8.5 dBm | Yes       | Yes*       | Yes*       |
| 3c   | STA  | 11 dBm  | No        | —          | —          |
| 4    | AP   | 8.5 dBm | Yes       | Yes        | Yes        |
| 5    | AP   | Default  | No        | —          | —          |
| 6    | STA  | 8.5 dBm | Yes       | Yes        | Yes        |

*Test 3 was working but we pinged the wrong IP due to OLED truncation.

**Key findings:**
1. This board has the known ESP32-C3 SuperMini antenna defect
2. `WiFi.setTxPower(WIFI_POWER_8_5dBm)` is **required** — both STA and AP
   modes fail without it
3. STA mode works fully with the TX power fix (connects, pingable, serves HTTP)
4. 11 dBm is too high — 8.5 dBm is the sweet spot for this board

**OLED truncation bug:** The 0.42" OLED at `u8g2_font_6x10_tr` (6px wide)
fits 12 characters max on the 72px display. `192.168.0.242` is 13 chars, so
the OLED displayed `192.168.0.24` — a valid-looking but wrong IP. This sent
us on a wild goose chase in Test 3b thinking STA mode was broken. **Fix:**
IP line now scrolls horizontally when the IP is too long to fit.

---

## Test 7 — 2026-02-02

**Change:** Bumped TX power from 8.5 dBm to 11 dBm (STA mode, from bedroom
~1 room away from router).

**Result:** SUCCESS — connects and serves HTTP from bedroom.

---

## Test 8 — 2026-02-02

**Change:** Bumped TX power to 13 dBm.

**Result:** FAILED — connection drops. 13 dBm triggers the antenna defect.
Reverted to 11 dBm.

**Conclusion:** 11 dBm is the maximum usable TX power for this board. Provides
more range than 8.5 dBm while staying below the interference threshold.

---

## Wire Antenna Mod (planned)

The stock CrossAir CA-C03 SMD antenna is tuned for 2.7 GHz, not 2.4 GHz. The
board is also missing a 3.8mm stripline required by the antenna datasheet. This
means the factory antenna was never working at the correct frequency.

**Fix:** Solder a 31mm quarter-wave wire antenna onto the chip antenna pads.
People report 6-10 dB gain (2-3x range), which should allow full TX power.

**Procedure (Peter Neufeld method):**
1. Cut 31mm of ~1mm solid wire (silver-plated or copper)
2. Wrap ~16mm of the wire around a 5mm drill bit to form a loop (~8mm diameter)
3. The remaining ~15mm points straight up vertically
4. Open the loop ends so they touch both pads of the existing SMD antenna
5. Solder one end to either pad (leave the chip antenna in place)

**References:**
- https://peterneufeld.wordpress.com/2025/03/04/esp32-c3-supermini-antenna-modification/
- https://hackaday.com/2025/04/07/simple-antenna-makes-for-better-esp32-c3-wifi/
- https://www.cnx-software.com/2025/04/09/antenna-hack-more-than-doubles-the-range-of-cheap-esp32-c3-usb-c-boards/

---

## Updated Summary

| Test | Mode | TX Power | Connects? | Reachable? | Web works? |
|------|------|----------|-----------|------------|------------|
| 1    | STA  | Default  | No        | —          | —          |
| 2    | STA  | Default (close) | No  | —          | —          |
| 3    | STA  | 8.5 dBm | Yes       | Yes*       | Yes*       |
| 3c   | STA  | 11 dBm  | No        | —          | —          |
| 4    | AP   | 8.5 dBm | Yes       | Yes        | Yes        |
| 5    | AP   | Default  | No        | —          | —          |
| 6    | STA  | 8.5 dBm | Yes       | Yes        | Yes        |
| 7    | STA  | 11 dBm  | Yes       | Yes        | Yes        |
| 8    | STA  | 13 dBm  | No        | —          | —          |

*Test 3 was working but we pinged the wrong IP due to OLED truncation.

Note: Test 3c (11 dBm, failed) and Test 7 (11 dBm, succeeded) suggest 11 dBm
is right at the edge — it may work or fail depending on conditions.

---

## Network Setup — D-Link Access Point

The main router (WLGG) is in the living room. At 11 dBm, the ESP32 connects
from nearby but struggles through walls to the bedroom. Solution: an old D-Link
router in the bedroom acting as a dumb access point.

**Setup:**
1. Factory reset the D-Link
2. Set SSID/password (stored in `include/secrets.h`)
3. **Disable DHCP** on the D-Link — the main router handles IP assignment
4. Connect the main router to a **LAN port** on the D-Link (not WAN)
5. Set the D-Link's own IP to something on the main subnet to avoid conflicts

This makes the D-Link a transparent bridge — all devices end up on the same
`192.168.0.0/24` subnet regardless of which AP they connect to.

**Result:** ESP32 connects to the D-Link at 11 dBm, gets IP `192.168.0.242`
from the main router's DHCP, and is reachable from all devices on the network.

---

## WiFi Status Code Reference

| Code | Constant            | Meaning                        |
|------|---------------------|--------------------------------|
| 0    | WL_IDLE_STATUS      | Idle                           |
| 1    | WL_NO_SSID_AVAIL    | SSID not found                 |
| 3    | WL_CONNECTED        | Connected                      |
| 4    | WL_CONNECT_FAILED   | Connection rejected / failed   |
| 6    | WL_DISCONNECTED     | Not connected                  |
