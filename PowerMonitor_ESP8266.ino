/*
 * ============================================================
 *  SparkLab Power Monitor v2.0
 *  NodeMCU ESP8266 + ZMPT101B + ACS712 — Single A0 Pin
 *  Strategy: Alternate V and I sampling each cycle
 * ============================================================
 *
 *  WIRING:
 *  ───────
 *  ZMPT101B  OUT  ─┐
 *                   ├──► A0 (ESP8266)
 *  ACS712    OUT  ─┘
 *
 *  Both sensor outputs share A0.
 *  Power BOTH sensors at 3.3V so their idle midpoints stay ≤1V
 *  on A0 (safe for ESP8266). If powering at 5V, add a
 *  100kΩ + 100kΩ voltage divider before A0.
 *
 *  CALIBRATION FLOW:
 *  ─────────────────
 *  1. Flash → open Serial Monitor → note idle ADC values printed at boot
 *  2. Set V_ZERO_RAW and ACS_ZERO_RAW to those idle values
 *  3. Connect AC → compare Vrms to multimeter → adjust VCAL_FACTOR
 *  4. Connect known load (100W bulb) → adjust ICAL_FACTOR until amps match
 *
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <math.h>

// ── WiFi ──────────────────────────────────────────────────────
const char* SSID     = "YOUR_WIFI_SSID";
const char* PASSWORD = "YOUR_WIFI_PASSWORD";

// ── Calibration ───────────────────────────────────────────────
// Voltage (ZMPT101B)
// Raw ADC midpoint at no AC signal — check Serial at boot
const float V_ZERO_RAW  = 512.0;   // tune: idle ADC value with AC disconnected
const float VCAL_FACTOR = 230.0;   // tune: multiply raw Vrms → actual Vrms

// Current (ACS712)
// Raw ADC midpoint at 0A — check Serial at boot with load disconnected
const float ACS_ZERO_RAW = 512.0;  // tune: idle ADC value with 0A flowing
const float ICAL_FACTOR  = 0.0185; // tune: Amps per raw-unit deviation from zero
//   How to find ICAL_FACTOR:
//   Apply a known load (e.g. 100W bulb → 0.43A at 230V)
//   Add Serial.println(raw_rms) in readCurrentRMS() below
//   ICAL_FACTOR = known_amps / printed_raw_rms

// ── Sampling ──────────────────────────────────────────────────
const int SAMPLES   = 600;   // per RMS cycle (~90ms total at 150µs each)
const int SAMPLE_US = 150;   // µs between samples — well above 50Hz Nyquist

// ── Timing ────────────────────────────────────────────────────
const unsigned long MEASURE_INTERVAL = 500; // ms — alternate V then I every 500ms

// ── State ─────────────────────────────────────────────────────
ESP8266WebServer server(80);

float voltage_rms    = 0.0;
float current_rms    = 0.0;
float active_power   = 0.0;
float apparent_power = 0.0;
float power_factor   = 0.95; // assumed; no true PF without simultaneous V+I
float energy_wh      = 0.0;

bool  read_current_next = false; // alternates each cycle
unsigned long last_measure_ms = 0;
unsigned long last_energy_ms  = 0;
unsigned long session_start   = 0;

// ── Voltage RMS ───────────────────────────────────────────────
float readVoltageRMS() {
  double sum_sq = 0.0;
  for (int i = 0; i < SAMPLES; i++) {
    double offset = analogRead(A0) - V_ZERO_RAW;
    sum_sq += offset * offset;
    delayMicroseconds(SAMPLE_US);
  }
  // sqrt gives raw RMS deviation in ADC units
  // VCAL_FACTOR converts that to actual Volts (absorbs transformer ratio + divider)
  return sqrt(sum_sq / SAMPLES) * VCAL_FACTOR;
}

// ── Current RMS ───────────────────────────────────────────────
float readCurrentRMS() {
  double sum_sq = 0.0;
  for (int i = 0; i < SAMPLES; i++) {
    double offset = analogRead(A0) - ACS_ZERO_RAW;
    sum_sq += offset * offset;
    delayMicroseconds(SAMPLE_US);
  }
  float raw_rms = sqrt(sum_sq / SAMPLES);
  // Uncomment to find ICAL_FACTOR:
  // Serial.print("raw_rms = "); Serial.println(raw_rms);
  return raw_rms * ICAL_FACTOR;
}

// ── Alternating measurement ───────────────────────────────────
/*
 *  How alternating works:
 *
 *    t=0ms    → readVoltageRMS()  (~90ms, stores voltage_rms)
 *    t=500ms  → readCurrentRMS()  (~90ms, stores current_rms, computes power)
 *    t=1000ms → readVoltageRMS()  → repeat
 *
 *  The ~500ms gap between V and I readings is fine for a power monitor.
 *  Both are valid RMS averages covering ~2.5 full 50Hz cycles each.
 *  Power factor accuracy is limited (no simultaneous sampling) — shown as assumed 0.95.
 */
void doMeasurement() {
  if (!read_current_next) {
    // ── Voltage cycle ──
    voltage_rms = readVoltageRMS();
    Serial.printf("[V] %.2f V\n", voltage_rms);

  } else {
    // ── Current cycle ──
    current_rms    = readCurrentRMS();
    apparent_power = voltage_rms * current_rms;
    active_power   = apparent_power * power_factor;

    // Accumulate energy
    unsigned long now = millis();
    energy_wh += active_power * ((now - last_energy_ms) / 3600000.0);
    last_energy_ms = now;

    Serial.printf("[I] %.3f A  P=%.2f W  S=%.2f VA  E=%.3f Wh\n",
                  current_rms, active_power, apparent_power, energy_wh);
  }

  read_current_next = !read_current_next;
}

// ── HTTP handlers ─────────────────────────────────────────────
void handleData() {
  StaticJsonDocument<256> doc;
  doc["voltage"]  = round(voltage_rms    * 10)   / 10.0;
  doc["current"]  = round(current_rms    * 1000) / 1000.0;
  doc["power"]    = round(active_power   * 10)   / 10.0;
  doc["apparent"] = round(apparent_power * 10)   / 10.0;
  doc["pf"]       = power_factor;
  doc["energy"]   = round(energy_wh      * 1000) / 1000.0;
  doc["uptime"]   = (millis() - session_start) / 1000;

  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleReset() {
  energy_wh = 0.0;
  last_energy_ms = millis();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

void handleRoot() {
  String p = "<html><body style='font-family:monospace;background:#0b0d11;color:#e2e8f0;padding:24px'>";
  p += "<h2>SparkLab Power Monitor</h2>";
  p += "<p>V: <b>" + String(voltage_rms,1) + " V</b> &nbsp; I: <b>" + String(current_rms,3) + " A</b> &nbsp; P: <b>" + String(active_power,1) + " W</b></p>";
  p += "<p><a href='/data' style='color:#f59e0b'>/data — JSON</a></p>";
  p += "<p style='color:#64748b;font-size:12px'>Open dashboard.html, set IP to " + WiFi.localIP().toString() + "</p></body></html>";
  server.send(200, "text/html", p);
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nSparkLab Power Monitor v2.0 — alternating A0 mode");

  // Print idle ADC values for calibration
  Serial.print("Idle ADC samples (use for V_ZERO_RAW / ACS_ZERO_RAW): ");
  for (int i = 0; i < 12; i++) { Serial.print(analogRead(A0)); Serial.print(" "); delay(30); }
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("WiFi");
  for (int t = 0; t < 40 && WiFi.status() != WL_CONNECTED; t++) { delay(500); Serial.print("."); }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected");
    Serial.println("IP:        http://" + WiFi.localIP().toString() + "/");
    Serial.println("JSON API:  http://" + WiFi.localIP().toString() + "/data");
  } else {
    Serial.println(" FAILED — offline mode (Serial only)");
  }

  server.on("/",      handleRoot);
  server.on("/data",  handleData);
  server.on("/reset", handleReset);
  server.begin();

  session_start  = millis();
  last_energy_ms = millis();
  last_measure_ms = millis();
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  server.handleClient();
  if (millis() - last_measure_ms >= MEASURE_INTERVAL) {
    doMeasurement();
    last_measure_ms = millis();
  }
}

/*
 * Libraries needed (Library Manager):
 *   ArduinoJson  by Benoit Blanchon — v6.x
 *
 * Board settings:
 *   NodeMCU 1.0 (ESP-12E Module), 80 MHz, 115200 baud
 */