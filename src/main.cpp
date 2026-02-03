#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <MHZ19.h>
#include <Wire.h>
#include <HTU21D.h>
#include "secrets.h"

#define LED_PIN 8
#define BATTERY_PIN 4
#define BATTERY_READ_INTERVAL 10000
#define CO2_READ_INTERVAL 5000
#define HTU21D_READ_INTERVAL 5000
#define CO2_WARMUP_MS 180000  // 3 minutes
#define BATTERY_LOW_THRESHOLD 3.4
#define NTFY_INTERVAL 300000  // 5 minutes
#define SKIP_WARMUP true      // debug: skip CO2 warmup

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);
WebServer server(80);
MHZ19 mhz;
HTU21D htu;

RTC_DATA_ATTR bool sensorWasRunning = false;

bool ledOn = false;
int ipScrollOffset = 0;
unsigned long lastScrollTime = 0;
float batteryVoltage = 0.0;
unsigned long lastBatteryRead = 0;
bool ntfyFirstAlert = true;
unsigned long lastNtfySent = 0;
int co2ppm = 0;
int co2temp = 0;
unsigned long lastCO2Read = 0;
float htuTemp = 0.0;
float htuHumidity = 0.0;
unsigned long lastHTU21DRead = 0;
unsigned long bootTime = 0;

void updateOled() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    // Status line at top (scrolls if IP too wide)
    String ip = WiFi.localIP().toString();
    int ipWidth = ip.length() * 6;
    if (ipWidth <= 72) {
        u8g2.drawStr(0, 9, ip.c_str());
    } else {
        int gap = 30; // pixel gap between copies
        int totalWidth = ipWidth + gap;
        int x = -ipScrollOffset;
        u8g2.drawStr(x, 9, ip.c_str());
        u8g2.drawStr(x + totalWidth, 9, ip.c_str());
    }

    // CO2 + battery voltage
    char line2[16];
    if (millis() - bootTime < CO2_WARMUP_MS) {
        unsigned long remaining = (CO2_WARMUP_MS - (millis() - bootTime)) / 1000;
        snprintf(line2, sizeof(line2), "%lus %.2fV", remaining, batteryVoltage);
    } else {
        snprintf(line2, sizeof(line2), "%dppm %.1fV", co2ppm, batteryVoltage);
    }
    u8g2.drawStr(0, 20, line2);

    // LED state
    u8g2.drawStr(0, 32, ledOn ? "LED: ON" : "LED: OFF");

    u8g2.sendBuffer();
}

const char *PAGE = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-C3 Test</title>
<style>
  body { font-family: sans-serif; max-width: 400px; margin: 40px auto; padding: 0 20px;
         background: #1a1a1a; color: #e0e0e0; }
  h1 { font-size: 1.4em; }
  h2 { font-size: 1.1em; margin-top: 28px; }
  .card { background: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 8px;
          padding: 20px; margin: 16px 0; text-align: center; }
  .led-box { width: 60px; height: 60px; border-radius: 12px; margin: 0 auto 8px;
             transition: background 0.2s; }
  .led-on { background: #3b82f6; border: 2px solid #3b82f6; }
  .led-off { background: #1a1a1a; border: 2px solid #3a3a3a; }
  #ledlabel { font-size: 0.9em; color: #aaa; }
  button { padding: 10px 20px; font-size: 1em; margin-top: 12px; cursor: pointer;
           background: #3b82f6; color: #fff; border: none; border-radius: 6px; }
  button:active { background: #2563eb; }
</style>
</head>
<body>
<h1>ESP32-C3 Test</h1>

<div class="card">
  <div id="co2val" style="font-size:2.2em;font-weight:bold;color:#888">--</div>
  <div style="font-size:0.9em;color:#aaa">CO2 (ppm)</div>
</div>

<div class="card">
  <div id="tempval" style="font-size:1.8em;font-weight:bold;color:#888">--</div>
  <div style="font-size:0.9em;color:#aaa">CO2 sensor temp (unreliable)</div>
</div>

<div class="card">
  <div id="htutemp" style="font-size:1.8em;font-weight:bold;color:#888">--</div>
  <div style="font-size:0.9em;color:#aaa">Temperature</div>
</div>

<div class="card">
  <div id="htuhum" style="font-size:1.8em;font-weight:bold;color:#888">--</div>
  <div style="font-size:0.9em;color:#aaa">Humidity</div>
</div>

<div class="card">
  <div id="battvolt" style="font-size:1.8em;font-weight:bold;color:#888">--</div>
  <div id="battlabel" style="font-size:0.9em;color:#aaa">Battery</div>
</div>

<div class="card">
  <div id="ledbox" class="led-box led-off"></div>
  <div id="ledlabel">OFF</div>
  <button onclick="toggleLed()">Toggle LED</button>
</div>

<script>
function setLed(state) {
  var box = document.getElementById('ledbox');
  var label = document.getElementById('ledlabel');
  if (state === 'ON') {
    box.className = 'led-box led-on';
    label.innerText = 'ON';
  } else {
    box.className = 'led-box led-off';
    label.innerText = 'OFF';
  }
}
function toggleLed() {
  var label = document.getElementById('ledlabel');
  setLed(label.innerText === 'ON' ? 'OFF' : 'ON');
  fetch('/led').then(function(r){return r.text()}).then(setLed);
}
fetch('/status').then(function(r){return r.text()}).then(setLed);
function updateBatt() {
  fetch('/battery').then(function(r){return r.text()}).then(function(v) {
    var el = document.getElementById('battvolt');
    var f = parseFloat(v);
    el.innerText = f.toFixed(2) + 'V';
    if (f >= 3.7) el.style.color = '#22c55e';
    else if (f >= 3.4) el.style.color = '#eab308';
    else el.style.color = '#ef4444';
  });
}
updateBatt();
setInterval(updateBatt, 10000);
function updateCO2() {
  fetch('/co2').then(function(r){return r.text()}).then(function(v) {
    var el = document.getElementById('co2val');
    var ppm = parseInt(v);
    if (ppm < 0) {
      el.innerText = 'WARM ' + Math.abs(ppm) + 's';
      el.style.color = '#888';
      return;
    }
    el.innerText = ppm;
    if (ppm <= 800) el.style.color = '#22c55e';
    else if (ppm <= 1000) el.style.color = '#eab308';
    else el.style.color = '#ef4444';
  });
}
updateCO2();
setInterval(updateCO2, 5000);
function updateHTU() {
  fetch('/temp').then(function(r){return r.text()}).then(function(v) {
    var el = document.getElementById('htutemp');
    var t = parseFloat(v);
    el.innerText = t.toFixed(1) + '\u00B0C';
    el.style.color = '#22c55e';
  });
  fetch('/humidity').then(function(r){return r.text()}).then(function(v) {
    var el = document.getElementById('htuhum');
    var h = parseFloat(v);
    el.innerText = h.toFixed(1) + '%';
    if (h <= 60) el.style.color = '#22c55e';
    else if (h <= 70) el.style.color = '#eab308';
    else el.style.color = '#ef4444';
  });
}
updateHTU();
setInterval(updateHTU, 5000);
function updateCO2Temp() {
  fetch('/co2temp').then(function(r){return r.text()}).then(function(v) {
    var el = document.getElementById('tempval');
    var t = parseInt(v);
    if (t === -1) { el.innerText = '--'; el.style.color = '#888'; return; }
    el.innerText = t + '\u00B0C';
    el.style.color = '#aaa';
  });
}
updateCO2Temp();
setInterval(updateCO2Temp, 5000);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", PAGE);
}

void handleStatus() {
    server.send(200, "text/plain", ledOn ? "ON" : "OFF");
}

void handleLed() {
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? LOW : HIGH); // inverted logic
    updateOled();
    server.send(200, "text/plain", ledOn ? "ON" : "OFF");
}

void handleBattery() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%.2f", batteryVoltage);
    server.send(200, "text/plain", buf);
}

void readBattery() {
    int mv = analogReadMilliVolts(BATTERY_PIN);
    batteryVoltage = mv * 2.0 / 1000.0;
    Serial.printf("Battery: %.2fV\n", batteryVoltage);
}

void sendNtfyAlert() {
    HTTPClient http;
    http.begin(NTFY_BATTERY);
    http.addHeader("Title", "ESP32 CO2 Sensor - Battery Low");
    http.addHeader("Tags", "warning,battery");
    http.addHeader("Priority", "high");
    char msg[32];
    snprintf(msg, sizeof(msg), "Battery: %.2fV", batteryVoltage);
    http.POST(msg);
    http.end();
    lastNtfySent = millis();
}

void handleCO2() {
    char buf[8];
    if (millis() - bootTime < CO2_WARMUP_MS) {
        int remaining = (CO2_WARMUP_MS - (millis() - bootTime)) / 1000;
        snprintf(buf, sizeof(buf), "-%d", remaining);
    } else {
        snprintf(buf, sizeof(buf), "%d", co2ppm);
    }
    server.send(200, "text/plain", buf);
}

void handleCO2Temp() {
    char buf[8];
    if (millis() - bootTime < CO2_WARMUP_MS) {
        server.send(200, "text/plain", "-1");
    } else {
        snprintf(buf, sizeof(buf), "%d", co2temp);
        server.send(200, "text/plain", buf);
    }
}

void readCO2() {
    co2ppm = mhz.getCO2();
    co2temp = mhz.getTemperature();
    Serial.printf("CO2: %d ppm  Temp: %d C\n", co2ppm, co2temp);
    sensorWasRunning = true;
}

void readHTU21D() {
    htuTemp = htu.readTemperature();
    htuHumidity = htu.readHumidity();
    Serial.printf("HTU21D: %.1f C  %.1f %%RH\n", htuTemp, htuHumidity);
}

void handleTemp() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", htuTemp);
    server.send(200, "text/plain", buf);
}

void handleHumidity() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", htuHumidity);
    server.send(200, "text/plain", buf);
}

void setup() {
    Serial.begin(115200);

    // LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // OFF (inverted)

    // CO2 sensor (UART on GPIO20 RX, GPIO21 TX)
    Serial1.begin(9600, SERIAL_8N1, 20, 21);
    mhz.begin(Serial1);
    mhz.autoCalibration(false);
    bootTime = millis();
    if (SKIP_WARMUP || sensorWasRunning) {
        bootTime -= CO2_WARMUP_MS; // skip warmup
        Serial.println("Skipping CO2 warmup.");
    }

    char fwVer[5];
    mhz.getVersion(fwVer);
    fwVer[4] = '\0';
    if (mhz.errorCode == RESULT_OK) {
        Serial.printf("MH-Z19: firmware %s, range %d ppm\n", fwVer, mhz.getRange());
    } else {
        Serial.printf("MH-Z19: sensor not found! (error %d)\n", mhz.errorCode);
    }

    // OLED
    u8g2.begin();

    // HTU21D (shares I2C bus with OLED on GPIO5/6, already initialized by u8g2)
    if (htu.begin() != true) {
        Serial.println("HTU21D: sensor not found!");
    } else {
        Serial.printf("HTU21D: %.1f C  %.1f %%RH\n", htu.readTemperature(), htu.readHumidity());
    }
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Connecting");
    u8g2.drawStr(0, 22, "WiFi...");
    u8g2.sendBuffer();

    // WiFi - STA mode
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("Connecting to '%s'\n", WIFI_SSID);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(1000);
        Serial.printf("  status: %d (attempt %d/30)\n", WiFi.status(), attempts + 1);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("WiFi FAILED. Final status: %d\n", WiFi.status());
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "WiFi FAIL");
        u8g2.drawStr(0, 22, "Check serial");
        u8g2.sendBuffer();
        while (true) delay(1000);
    }

    Serial.printf("Connected! IP: %s  RSSI: %d\n",
        WiFi.localIP().toString().c_str(), WiFi.RSSI());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Subnet: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("DNS: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("Channel: %d\n", WiFi.channel());
    Serial.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());

    // Initial battery reading
    readBattery();

    // Web server routes
    server.on("/", handleRoot);
    server.on("/led", handleLed);
    server.on("/status", handleStatus);
    server.on("/battery", handleBattery);
    server.on("/co2", handleCO2);
    server.on("/co2temp", handleCO2Temp);
    server.on("/temp", handleTemp);
    server.on("/humidity", handleHumidity);
    server.begin();

    Serial.println("Web server started.");

    // Boot notification
    {
        HTTPClient http;
        http.begin(NTFY_BOOT);
        http.addHeader("Title", "ESP32 CO2 Sensor booted");
        http.addHeader("Tags", "electric_plug");
        char msg[128];
        snprintf(msg, sizeof(msg), "IP: %s\nRSSI: %d dBm\nMAC: %s\nSSID: %s",
            WiFi.localIP().toString().c_str(), WiFi.RSSI(),
            WiFi.macAddress().c_str(), WIFI_SSID);
        http.POST(msg);
        http.end();
    }

    updateOled();
}

void loop() {
    server.handleClient();

    // Read CO2 periodically
    if (millis() - lastCO2Read >= CO2_READ_INTERVAL) {
        lastCO2Read = millis();
        readCO2();
        updateOled();
    }

    // Read HTU21D periodically
    if (millis() - lastHTU21DRead >= HTU21D_READ_INTERVAL) {
        lastHTU21DRead = millis();
        readHTU21D();
    }

    // Read battery voltage periodically
    if (millis() - lastBatteryRead >= BATTERY_READ_INTERVAL) {
        lastBatteryRead = millis();
        readBattery();
        updateOled();

        // Send ntfy alert if battery is low
        if (batteryVoltage > 0.5 && batteryVoltage < BATTERY_LOW_THRESHOLD
            && (ntfyFirstAlert || millis() - lastNtfySent >= NTFY_INTERVAL)) {
            ntfyFirstAlert = false;
            sendNtfyAlert();
        }
    }

    // Scroll IP if it doesn't fit
    String ip = WiFi.localIP().toString();
    int ipWidth = ip.length() * 6;
    if (ipWidth > 72 && millis() - lastScrollTime > 300) {
        lastScrollTime = millis();
        int gap = 30;
        int totalWidth = ipWidth + gap;
        ipScrollOffset += 2;
        if (ipScrollOffset >= totalWidth) {
            ipScrollOffset = 0;
        }
        updateOled();
    }
}
