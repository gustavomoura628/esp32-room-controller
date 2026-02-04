#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <MHZ19.h>
#include <Wire.h>
#include <HTU21D.h>
#include <FastLED.h>
#include "secrets.h"

#define LED_PIN 8
#define BATTERY_PIN 4
#define BATTERY_READ_INTERVAL 10000
#define CO2_READ_INTERVAL 5000
#define HTU21D_READ_INTERVAL 5000
#define BATTERY_LOW_THRESHOLD 3.4
#define NTFY_INTERVAL 300000  // 5 minutes
#define RELAY_PIN 7
#define LED_STRIP_PIN 10
#define NUM_LEDS 30

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);
WebServer server(80);
MHZ19 mhz;
HTU21D htu;
CRGB leds[NUM_LEDS];

bool ledOn = false;
bool relayOn = false;
int ipScrollOffset = 0;
unsigned long lastScrollTime = 0;
float batteryVoltage = 0.0;
unsigned long lastBatteryRead = 0;
bool ntfyFirstAlert = true;
unsigned long lastNtfySent = 0;
int co2ppm = 0;
int co2temp = 0;
int co2error = 0;
unsigned long lastCO2Read = 0;
float htuTemp = 0.0;
float htuHumidity = 0.0;
uint8_t stripBrightness = 128;
CRGB stripColor = CRGB::White;
bool stripOn = false;
String stripMode = "solid";
uint8_t rainbowHue = 0;
unsigned long lastHTU21DRead = 0;

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
    snprintf(line2, sizeof(line2), "%dppm %.1fV", co2ppm, batteryVoltage);
    u8g2.drawStr(0, 20, line2);

    // Temperature + humidity
    char line3[16];
    snprintf(line3, sizeof(line3), "%.1fC %.0f%%", htuTemp, htuHumidity);
    u8g2.drawStr(0, 32, line3);

    u8g2.sendBuffer();
}

const char *PAGE = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Room Controller</title>
<style>
  body { font-family: sans-serif; max-width: 800px; margin: 40px auto; padding: 0 20px;
         background: #1a1a1a; color: #e0e0e0; }
  h1 { font-size: 1.4em; }
  .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
  .card { background: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 8px;
          padding: 16px; text-align: center; }
  .wide { grid-column: span 3; }
  .led-box { width: 40px; height: 40px; border-radius: 8px; margin: 0 auto 6px;
             transition: background 0.2s; }
  .led-on { background: #3b82f6; border: 2px solid #3b82f6; }
  .led-off { background: #1a1a1a; border: 2px solid #3a3a3a; }
  .relay-on { background: #e0e0e0; border: 2px solid #e0e0e0; }
  .relay-off { background: #1a1a1a; border: 2px solid #3a3a3a; }
  #ledlabel { font-size: 0.9em; color: #aaa; }
  button { padding: 8px 16px; font-size: 0.9em; margin-top: 8px; cursor: pointer;
           background: #3b82f6; color: #fff; border: none; border-radius: 6px; }
  button:active { background: #2563eb; }
  @media (max-width: 500px) { .grid { grid-template-columns: repeat(2, 1fr); }
    .wide { grid-column: span 2; } }
</style>
</head>
<body>
<h1>ESP32 Room Controller</h1>

<div class="grid">

<div class="card">
  <div id="co2val" style="font-size:2em;font-weight:bold;color:#888">--</div>
  <div id="co2label" style="font-size:0.85em;color:#aaa">CO2 (ppm)</div>
</div>

<div class="card">
  <div id="htutemp" style="font-size:2em;font-weight:bold;color:#888">--</div>
  <div style="font-size:0.85em;color:#aaa">Temperature</div>
</div>

<div class="card">
  <div id="htuhum" style="font-size:2em;font-weight:bold;color:#888">--</div>
  <div style="font-size:0.85em;color:#aaa">Humidity</div>
</div>

<div class="card">
  <div id="battvolt" style="font-size:1.6em;font-weight:bold;color:#888">--</div>
  <div id="battlabel" style="font-size:0.85em;color:#aaa">Battery</div>
</div>

<div class="card">
  <div style="font-size:0.85em;color:#aaa;margin-bottom:6px">Board LED</div>
  <div id="ledbox" class="led-box led-off"></div>
  <div id="ledlabel">OFF</div>
  <button onclick="toggleLed()">Toggle</button>
</div>

<div class="card">
  <div style="font-size:0.85em;color:#aaa;margin-bottom:6px">Room Light</div>
  <div id="relaybox" class="led-box relay-off"></div>
  <div id="relaylabel">OFF</div>
  <button onclick="toggleRelay()">Toggle</button>
</div>

<div class="card wide">
  <div style="font-size:0.9em;color:#aaa;margin-bottom:6px">LED Strip</div>
  <button id="stripbtn" onclick="toggleStrip()">Turn On</button>
  <div style="margin-top:10px;display:flex;gap:6px;justify-content:center;align-items:center">
    <button onclick="setMode('rainbow')" style="font-size:0.85em;padding:6px 12px;margin:0">Rainbow</button>
    <button onclick="setMode('solid')" style="font-size:0.85em;padding:6px 12px;margin:0">Solid</button>
    <input type="color" id="stripclr" value="#ffffff" onchange="setStrip()" style="height:32px;width:32px;border:none;padding:0;cursor:pointer">
  </div>
  <div style="margin-top:8px">
    <input type="range" id="stripbri" min="1" max="255" value="128" style="width:100%" oninput="setStrip()">
  </div>
</div>

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
    if (f < 0.5) {
      el.innerText = '--';
      el.style.color = '#555';
      document.getElementById('battlabel').innerText = 'Battery disconnected';
    } else {
      el.innerText = f.toFixed(2) + 'V';
      document.getElementById('battlabel').innerText = 'Battery';
      if (f >= 3.7) el.style.color = '#22c55e';
      else if (f >= 3.4) el.style.color = '#eab308';
      else el.style.color = '#ef4444';
    }
  });
}
updateBatt();
setInterval(updateBatt, 10000);
function updateCO2() {
  fetch('/co2status').then(function(r){return r.json()}).then(function(d) {
    var el = document.getElementById('co2val');
    var label = document.getElementById('co2label');
    if (d.result !== 1) {
      var errNames = {0:'no response',2:'timeout',3:'desync',4:'CRC error',5:'filter'};
      el.innerText = d.ppm || '--';
      el.style.color = '#ef4444';
      label.innerText = 'CO2 (' + (errNames[d.result] || 'error ' + d.result) + ')';
      return;
    }
    if (d.uptime < 180) {
      el.innerText = d.ppm;
      el.style.color = '#888';
      label.innerText = 'CO2 warming up (' + (180 - d.uptime) + 's)';
      return;
    }
    el.innerText = d.ppm;
    label.innerText = 'CO2 (ppm)';
    if (d.ppm <= 800) el.style.color = '#22c55e';
    else if (d.ppm <= 1000) el.style.color = '#eab308';
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
function setRelay(state) {
  var box = document.getElementById('relaybox');
  document.getElementById('relaylabel').innerText = state;
  if (state === 'ON') {
    box.className = 'led-box relay-on';
  } else {
    box.className = 'led-box relay-off';
  }
}
function toggleRelay() {
  var label = document.getElementById('relaylabel');
  setRelay(label.innerText === 'ON' ? 'OFF' : 'ON');
  fetch('/relay').then(function(r){return r.text()}).then(setRelay);
}
fetch('/relaystatus').then(function(r){return r.text()}).then(setRelay);
var stripIsOn = false;
function toggleStrip() {
  stripIsOn = !stripIsOn;
  document.getElementById('stripbtn').innerText = stripIsOn ? 'Turn Off' : 'Turn On';
  setStrip();
}
function setMode(m) {
  fetch('/strip?mode=' + m + '&on=1').then(function(r){return r.json()}).then(function(d) {
    stripIsOn = d.on === 1;
    document.getElementById('stripbtn').innerText = stripIsOn ? 'Turn Off' : 'Turn On';
  });
}
function setStrip() {
  var b = document.getElementById('stripbri').value;
  var c = document.getElementById('stripclr').value;
  var r = parseInt(c.substr(1,2),16);
  var g = parseInt(c.substr(3,2),16);
  var bl = parseInt(c.substr(5,2),16);
  fetch('/strip?on=' + (stripIsOn?1:0) + '&brightness=' + b + '&r=' + r + '&g=' + g + '&b=' + bl);
}
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

void handleRelay() {
    relayOn = !relayOn;
    digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
    server.send(200, "text/plain", relayOn ? "ON" : "OFF");
}

void handleRelayStatus() {
    server.send(200, "text/plain", relayOn ? "ON" : "OFF");
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
    http.addHeader("Title", "ESP32 Room Controller - Battery Low");
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
    snprintf(buf, sizeof(buf), "%d", co2ppm);
    server.send(200, "text/plain", buf);
}

void handleCO2Temp() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", co2temp);
    server.send(200, "text/plain", buf);
}

void handleCO2Status() {
    char buf[64];
    unsigned long uptime = millis() / 1000;
    snprintf(buf, sizeof(buf), "{\"result\":%d,\"uptime\":%lu,\"ppm\":%d}", co2error, uptime, co2ppm);
    server.send(200, "application/json", buf);
}

void readCO2() {
    co2ppm = mhz.getCO2();
    co2error = mhz.errorCode;
    co2temp = mhz.getTemperature();
    if (co2error == RESULT_OK) {
        Serial.printf("CO2: %d ppm  Temp: %d C\n", co2ppm, co2temp);
    } else {
        Serial.printf("CO2: read error (%d), ppm=%d\n", co2error, co2ppm);
    }
}

void readHTU21D() {
    htuTemp = htu.readTemperature();
    htuHumidity = htu.readHumidity();
    Serial.printf("HTU21D: %.1f C  %.1f %%RH\n", htuTemp, htuHumidity);
}

void updateStrip() {
    if (!stripOn) {
        FastLED.clear();
        FastLED.show();
        return;
    }
    FastLED.setBrightness(stripBrightness);
    if (stripMode == "solid") {
        fill_solid(leds, NUM_LEDS, stripColor);
    } else if (stripMode == "rainbow") {
        fill_rainbow(leds, NUM_LEDS, rainbowHue++, 255 / NUM_LEDS);
    }
    FastLED.show();
}

void handleStrip() {
    if (server.hasArg("on")) {
        stripOn = server.arg("on") == "1";
    }
    if (server.hasArg("brightness")) {
        stripBrightness = server.arg("brightness").toInt();
    }
    if (server.hasArg("mode")) {
        stripMode = server.arg("mode");
    }
    if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
        stripColor = CRGB(server.arg("r").toInt(), server.arg("g").toInt(), server.arg("b").toInt());
    }
    updateStrip();

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"on\":%d,\"brightness\":%d,\"mode\":\"%s\"}",
        stripOn ? 1 : 0, stripBrightness, stripMode.c_str());
    server.send(200, "application/json", buf);
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

    // Onboard LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // OFF (inverted)

    // Relay
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // OFF (active-high, inverted by transistor)

    // LED strip
    FastLED.addLeds<WS2813, LED_STRIP_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setCorrection(TypicalLEDStrip);
    FastLED.clear();
    FastLED.show();

    // CO2 sensor (UART on GPIO20 RX, GPIO21 TX)
    Serial1.begin(9600, SERIAL_8N1, 20, 21);
    delay(100);
    while (Serial1.available()) Serial1.read();
    mhz.begin(Serial1);
    mhz.autoCalibration(false);

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
    server.on("/co2status", handleCO2Status);
    server.on("/temp", handleTemp);
    server.on("/humidity", handleHumidity);
    server.on("/strip", handleStrip);
    server.on("/relay", handleRelay);
    server.on("/relaystatus", handleRelayStatus);
    server.begin();

    Serial.println("Web server started.");

    // Boot notification
    {
        HTTPClient http;
        http.begin(NTFY_BOOT);
        http.addHeader("Title", "ESP32 Room Controller booted");
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

    // Update LED strip (needed for animations like rainbow)
    if (stripOn && stripMode == "rainbow") {
        updateStrip();
    }

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
