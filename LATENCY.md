# Web UI Response Latency Investigation

Investigation into what causes delay between pressing a button on the web UI and the
physical action (relay click, LED toggle) on the ESP32-C3.

## Architecture

The ESP32-C3 SuperMini is **single-core** (`CONFIG_FREERTOS_UNICORE=y`). Everything
runs on one RISC-V core at 160MHz. FreeRTOS is preemptive with a 1000Hz tick rate.

### Task priorities

| Task | Priority | Role |
|------|----------|------|
| WiFi / LwIP | 23 | TCP/IP stack, packet processing |
| Arduino loop | 1 | `loop()`, `server.handleClient()`, sensor reads |
| Idle | 0 | Watchdog, cleanup |

WiFi at priority 23 **preempts** the loop task at priority 1 via hardware interrupts.
The loop task does NOT need to yield for WiFi to run. WiFi runs whenever it has work.

### The unicore yield

On unicore builds, the Arduino framework inserts `yieldIfNecessary()` between every
`loop()` call:

```cpp
// cores/esp32/main.cpp
void yieldIfNecessary(void){
    static uint64_t lastYield = 0;
    uint64_t now = millis();
    if((now - lastYield) > 2000) {
        lastYield = now;
        vTaskDelay(5); // 5ms yield every 2 seconds
    }
}
```

This is a safety yield to prevent the watchdog from firing. It does NOT help with
web responsiveness -- it only fires once every 2 seconds.

## The WebServer

Source: `framework-arduinoespressif32/libraries/WebServer/src/WebServer.cpp`

### Single client only

The ESP32 Arduino WebServer **can only handle one client at a time**. From the source:
"Supports only one simultaneous client." All other connections queue at the TCP level.

### handleClient() state machine

```
HC_NONE ──[_server.available()]──> HC_WAIT_READ ──[data ready]──> parse + handle + respond ──> HC_NONE
                                        │
                                        └──[no data, <5s]──> keep waiting (yield)
                                        └──[no data, >5s]──> drop client ──> HC_NONE
```

Key behaviors:

1. **When no client is waiting:** calls `delay(1)` (`_nullDelay` defaults to `true`)
   which yields to FreeRTOS for 1ms. This means the loop runs at ~1000 iterations/sec
   when idle.

2. **HC_WAIT_READ with no data available:** holds the client for up to
   `HTTP_MAX_DATA_WAIT` = **5000ms**, calling `yield()` each iteration. During this
   time, **no new connections are accepted**.

3. **HC_WAIT_CLOSE is disabled:** The transition to HC_WAIT_CLOSE is commented out
   (Chrome browser fix). After sending a response, the server immediately returns
   to HC_NONE and can accept the next client. The `HTTP_MAX_CLOSE_WAIT = 2000ms`
   constant exists but is never reached.

4. **Connection: close** is always sent -- no HTTP keep-alive. Each request needs
   a fresh TCP connection (SYN → SYN-ACK → ACK → GET → response → FIN).

### The HC_WAIT_READ trap

This is the most dangerous path. If a TCP connection is established but the HTTP
request data hasn't been buffered by LwIP yet, handleClient() enters HC_WAIT_READ
and holds the client for up to **5 seconds**. During those 5 seconds:
- No new clients are accepted
- Each handleClient() call just calls yield() and returns
- The user's button press queues behind this stuck connection

On weak WiFi (8.5 dBm TX power, antenna defect), TCP can be slow. A poll request
that arrives with slow data delivery could trigger this trap.

## Blocking operations in the loop

### MH-Z19 CO2 sensor (UART)

Source: `.pio/libdeps/esp32-c3-devkitm-1/MH-Z19/src/MHZ19.cpp`

Each `provisioning()` call sends 9 bytes on UART and **busy-waits** for 9 bytes
back with a 500ms timeout:

```cpp
// MHZ19.cpp read() - tight busy-wait loop, NO yield
while (mySerial->available() < MHZ19_DATA_LEN) {
    if (millis() - timeStamp >= TIMEOUT_PERIOD)  // TIMEOUT_PERIOD = 500ms
        return RESULT_TIMEOUT;
}
```

This is a **CPU-hogging spin loop** with no `yield()` or `delay()` inside. The loop
task cannot run handleClient() during this wait.

Current readCO2() makes **two** UART round trips:

| Call | Blocks | Purpose |
|------|--------|---------|
| `getCO2()` | ~100-200ms (up to 500ms) | CO2 measurement |
| `getTemperature()` | ~100-200ms (up to 500ms) | Sensor internal temp (unreliable) |
| `delay(1500)` on error | 1500ms fixed | Desync recovery |

**Normal case:** ~200-400ms of busy-wait where handleClient() cannot run.
**Error case:** additional 1500ms hard delay.

Note: `getTemperature(true)` (the default) sends the **same UART command** as
getCO2() a second time. The response already contains the temperature data.
Calling `getTemperature(false)` reads from the cached buffer with zero blocking.

### HTU21D temperature/humidity (I2C)

`readHTU21D()` does two I2C reads:
- `htu.readTemperature()`: up to ~50ms (sensor conversion time)
- `htu.readHumidity()`: up to ~16ms (sensor conversion time)

Total: **~66ms** of I2C blocking. The I2C driver uses `i2c_master_cmd_begin()`
which blocks the loop task but yields to FreeRTOS (higher-priority tasks can run).
However, handleClient() is in the same task and still cannot run.

### OLED display (I2C)

`updateOled()` does: clearBuffer + 3x drawStr + sendBuffer. The sendBuffer transfers
~360 bytes over I2C. At 100kHz I2C, this takes **~30ms**. Like HTU21D, the I2C
driver yields to FreeRTOS but handleClient() still can't run.

Currently called from:
- After readCO2() (every 5s)
- After readBattery() (every 10s)
- Scroll timer (every 300ms when IP doesn't fit on screen)
- handleLed() (on LED toggle)

### Combined worst case

If a web request arrives at the worst possible moment:

```
readCO2() starts
├── getCO2(): busy-wait ~200ms          ← handleClient() blocked
├── getTemperature(): busy-wait ~200ms  ← handleClient() blocked
├── [on error: delay(1500)]             ← handleClient() blocked
└── updateOled(): I2C ~30ms             ← handleClient() blocked (yields to WiFi, not to handleClient)
readHTU21D():
├── readTemperature(): I2C ~50ms        ← handleClient() blocked
└── readHumidity(): I2C ~16ms           ← handleClient() blocked
readBattery(): ADC ~1ms
updateOled(): I2C ~30ms
```

**Worst case without errors:** ~530ms where handleClient() cannot run.
**Worst case with CO2 error:** ~2030ms (adds 1500ms delay).

This happens every 5 seconds when CO2 and HTU21D timers align (both are 5s interval).

## The OLED mystery

### What changed

When the OLED was consolidated from scattered calls to a single 2-second timer:

**Before (working):**
- updateOled() after handleLed() → I2C yield after user action
- updateOled() after readCO2() → I2C yield after UART block
- updateOled() after readBattery() → I2C yield after ADC
- updateOled() every 300ms from scroll timer → regular I2C yields

**After (slow):**
- updateOled() removed from all handlers
- updateOled() every 2000ms in a single timer

### Why it mattered

The I2C driver's `i2c_master_cmd_begin()` blocks the **calling task** and yields to
the FreeRTOS scheduler. This lets higher-priority tasks (WiFi at p23) run. But
handleClient() is in the **same** task -- it cannot run during I2C yields.

So I2C yields don't directly help handleClient(). But they help **indirectly**:

1. **WiFi/LwIP processing time:** The I2C block gives the WiFi stack sustained
   CPU time (30ms per updateOled call) to complete TCP processing. While WiFi can
   preempt the loop task via interrupts, it may need uninterrupted time to complete
   multi-step operations (TCP handshake, packet reassembly, socket buffer writes).

2. **Socket buffer readiness:** When handleClient() accepts a new client and checks
   `_currentClient.available()`, the data must already be in the socket buffer. If
   LwIP hasn't had enough sustained CPU time to move received data into the socket
   buffer, available() returns false → server enters HC_WAIT_READ → holds the
   client for up to 5 seconds, blocking all other connections.

3. **handleClient() inside handleLed():** In the old code, the sequence was:
   `handleClient() → handleLed() → updateOled() → loop() → handleClient()`. The
   updateOled() inside the handler happened AFTER the response was sent, giving the
   WiFi stack time to process the outgoing TCP packets before the next handleClient()
   call potentially accepted another connection.

### The 2-second coincidence

The reported delay of ~2 seconds matches `yieldIfNecessary()`'s 2-second interval.
Without frequent I2C yields, the loop task runs nearly continuously (broken only by
handleClient's 1ms delay). Every 2 seconds, `yieldIfNecessary()` forces a 5ms yield.
This may be the only window long enough for LwIP to complete socket buffer operations
that make `_currentClient.available()` return true.

### Remaining uncertainty

With preemptive scheduling, WiFi at p23 should preempt the loop at p1 whenever
interrupts fire. The exact mechanism by which reduced I2C yields cause web latency
isn't fully proven. It may involve:
- LwIP's tcpip_thread needing sustained (not just preemptive) CPU time
- The ESP32-C3's single-core interrupt handling interacting with FreeRTOS scheduling
- Socket buffer operations being deferred to lower-priority contexts

## Improving response time

### Fix 1: Eliminate the second UART round trip (easy, safe)

```cpp
co2temp = mhz.getTemperature(false);  // read from getCO2() buffer, no UART
```

Cuts UART blocking from ~400ms to ~200ms. The default `getTemperature(true)` sends
the **exact same command** as getCO2() again -- pure waste.

### Fix 2: Add handleClient() between blocking operations (easy, safe)

```cpp
readCO2();
server.handleClient();  // process pending requests between sensor reads
readHTU21D();
server.handleClient();
```

Ensures web requests are processed between UART and I2C blocking windows. Reduces
worst-case response time from ~530ms to max(200ms, 66ms) = ~200ms.

### Fix 3: Non-blocking CO2 reads (medium effort, big win)

Instead of the library's blocking read, implement a state machine:

```
State 1: Send CO2 command (9 bytes UART write)
State 2: Check Serial1.available() each loop iteration
         If 9 bytes ready → parse response → State 1 (next interval)
         If timeout → error recovery
```

This eliminates ALL busy-waiting. handleClient() runs between every byte received.
Worst-case response time drops to ~1ms (one loop iteration).

### Fix 4: Use AsyncWebServer (major refactor, best result)

The ESPAsyncWebServer library handles connections in the WiFi/LwIP task context
(priority 23), not in the loop task. Request handlers are called as callbacks when
data is ready. No polling, no single-client limitation, no HC_WAIT_READ trap.

This would make response time truly independent of loop() blocking.

### Fix 5: Combine poll endpoints (easy, reduces contention)

Instead of 3 separate poll requests every 5 seconds (/status, /relaystatus, /strip),
add a single `/poll` endpoint that returns all state in one JSON response. Reduces
TCP connections from 3 to 1 per poll cycle.

### Summary of expected improvements

| Fix | Worst-case delay | Effort |
|-----|-------------------|--------|
| Current code | ~530ms (normal), ~2030ms (CO2 error) | -- |
| Fix 1 (getTemperature false) | ~330ms | 1 line |
| Fix 1+2 (+ handleClient between) | ~200ms | 3 lines |
| Fix 1+2+5 (+ combined poll) | ~200ms, less contention | ~20 lines |
| Fix 3 (non-blocking CO2) | ~66ms (HTU21D only) | ~50 lines |
| Fix 3+5 | ~66ms | ~70 lines |
| Fix 4 (AsyncWebServer) | <1ms | major refactor |
