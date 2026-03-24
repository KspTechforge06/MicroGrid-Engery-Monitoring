/*
 * ============================================================
 *  SparkLab Power Monitor v1.0
 *  NodeMCU ESP8266 + ZMPT101B + ACS712
 * ============================================================
 *
 *  WIRING:
 *  -------
 *  ZMPT101B  → A0  (via voltage divider if needed, see note)
 *  ACS712    → A0  (switch with ZMPT via selector or use
 *                   external ADC like ADS1115 for both)
 *
 *  RECOMMENDED: Use ADS1115 (I2C) for true dual-channel:
 *    ADS1115 A0  → ZMPT101B OUT
 *    ADS1115 A1  → ACS712 OUT
 *    ADS1115 SDA → D2 (GPIO4)
 *    ADS1115 SCL → D1 (GPIO5)
 *
 *  Single-ADC workaround included below — reads voltage only
 *  on A0, swap manually or use a relay.
 *
 *  SENSOR NOTES:
 *  -------------
 *  ZMPT101B  — AC voltage transformer module, outputs ~1.1–2.5V AC
 *              Calibrate VREF and VCAL_FACTOR for your transformer
 *  ACS712-5A — 185 mV/A sensitivity, mid-rail ≈ VCC/2 = 2.5V on 5V
 *              For 3.3V ESP8266 use the 3.3V-compatible version or
 *              add a level-shift / voltage divider on the output.
 *
 *  DASHBOARD:
 *  ----------
 *  Connect ESP8266 to your WiFi → open Serial Monitor → get IP
 *  Navigate to http://<ip>/ in browser on same network
 *
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <math.h>

// ── WiFi credentials ─────────────────────────────────────────
const char* SSID     = "YOUR_WIFI_SSID";
const char* PASSWORD = "YOUR_WIFI_PASSWORD";

// ── Calibration ───────────────────────────────────────────────
// ZMPT101B — adjust until Vrms matches your multimeter
const float VCAL_FACTOR  = 234.0;    // multiply raw Vrms → actual Vrms (tune this)
const float V_MIDPOINT   = 512.0;    // ADC midpoint (0-1023); 512 for ideal 50% rail

// ACS712 — for 5A module: 185 mV/A; for 20A: 100 mV/A; for 30A: 66 mV/A
const float ACS_SENSITIVITY = 0.185; // V/A (change for your module variant)
const float ACS_VREF        = 2.5;   // V, voltage at zero current (VCC/2)
const float ADC_VREF        = 3.3;   // ESP8266 ADC reference voltage

// Sampling
const int   SAMPLES      = 500;      // samples per RMS calculation cycle
const int   SAMPLE_DELAY = 100;      // µs between samples

// ── Globals ───────────────────────────────────────────────────
ESP8266WebServer server(80);

float voltage_rms = 0.0;
float current_rms = 0.0;
float active_power = 0.0;
float apparent_power = 0.0;
float power_factor = 0.0;
float energy_wh = 0.0;

unsigned long last_sample_ms = 0;
unsigned long last_energy_ms = 0;

// ── Sensor read ───────────────────────────────────────────────
/*
 *  NOTE: ESP8266 has ONE analog pin (A0). If you're using a single
 *  ADC, you'll need to choose: read voltage OR current each loop.
 *  For a proper dual reading, use ADS1115. The code below shows
 *  the ADS1115 path commented out — uncomment and install the
 *  Adafruit ADS1X15 library for it.
 *
 *  For single-ADC demo mode, we simulate current from voltage.
 *  Replace readCurrentRMS() body with real ACS712 reads when wired.
 */

float readVoltageRMS() {
  long sum_sq = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int raw = analogRead(A0);           // 0–1023
    long offset = raw - (int)V_MIDPOINT;
    sum_sq += offset * offset;
    delayMicroseconds(SAMPLE_DELAY);
  }
  float raw_rms = sqrt((float)sum_sq / SAMPLES);
  // Convert ADC units → voltage (ADC range 0–1023 maps to 0–ADC_VREF)
  float v_rms_scaled = raw_rms * (ADC_VREF / 1023.0);
  return v_rms_scaled * VCAL_FACTOR;
}

float readCurrentRMS() {
  /*
   *  If ACS712 is on a separate pin or ADS1115 channel, read it here.
   *  For dual A0 use, comment the voltage read above and swap each loop.
   *
   *  DEMO / PLACEHOLDER — replace with real ACS712 read:
   *  long sum_sq = 0;
   *  for (int i = 0; i < SAMPLES; i++) {
   *    int raw = analogRead(ACS_PIN);
   *    float v = raw * (ADC_VREF / 1023.0);
   *    float amperes = (v - ACS_VREF) / ACS_SENSITIVITY;
   *    sum_sq += (long)(amperes * amperes * 1000);
   *    delayMicroseconds(SAMPLE_DELAY);
   *  }
   *  return sqrt((float)sum_sq / SAMPLES / 1000.0);
   */

  // ── Demo simulation: current scales with voltage ─────────
  //    Remove this block when ACS712 is wired properly
  float simulated_load_ohms = 220.0;  // simulate a ~220Ω load
  return voltage_rms / simulated_load_ohms;
}

void takeMeasurement() {
  voltage_rms    = readVoltageRMS();
  current_rms    = readCurrentRMS();
  apparent_power = voltage_rms * current_rms;

  // For resistive loads PF ≈ 1.0. For real PF measurement
  // you need simultaneous V+I sampling and phase calculation.
  power_factor   = 0.95;  // assumed; replace with real calculation
  active_power   = apparent_power * power_factor;

  // Energy accumulation
  unsigned long now = millis();
  float dt_h = (now - last_energy_ms) / 3600000.0;
  energy_wh += active_power * dt_h;
  last_energy_ms = now;
}

// ── HTTP routes ───────────────────────────────────────────────
void handleRoot() {
  // Serve the dashboard HTML (same as the web file, embedded)
  // For large HTML, use LittleFS. Here we redirect to /dashboard.
  server.sendHeader("Location", "/dashboard");
  server.send(302);
}

void handleData() {
  StaticJsonDocument<256> doc;
  doc["voltage"]  = round(voltage_rms * 10) / 10.0;
  doc["current"]  = round(current_rms * 1000) / 1000.0;
  doc["power"]    = round(active_power * 10) / 10.0;
  doc["apparent"] = round(apparent_power * 10) / 10.0;
  doc["pf"]       = round(power_factor * 100) / 100.0;
  doc["energy"]   = round(energy_wh * 100) / 100.0;
  doc["ts"]       = millis();

  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleReset() {
  energy_wh = 0.0;
  server.send(200, "text/plain", "Energy reset");
}

// The dashboard HTML is served from the separate .html file
// For embedded serving, see the companion dashboard.html file
// and use LittleFS or serve it from PROGMEM.
void handleDashboard() {
  // Tell user to open the dashboard HTML directly
  // In production: store dashboard.html in LittleFS and serve it here
  String msg = "<h2>SparkLab Power Monitor</h2>";
  msg += "<p>API endpoint: <a href='/data'>/data</a></p>";
  msg += "<p>Open the <b>dashboard.html</b> file in your browser and set the IP to ";
  msg += WiFi.localIP().toString();
  msg += "</p><p><a href='/data'>View raw JSON data</a></p>";
  server.send(200, "text/html", msg);
}

// ── Setup & Loop ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n\nSparkLab Power Monitor v1.0");
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi failed — check credentials. Continuing in offline mode.");
  }

  server.on("/",         handleDashboard);
  server.on("/dashboard", handleDashboard);
  server.on("/data",     handleData);
  server.on("/reset",    handleReset);
  server.begin();

  Serial.println("HTTP server started.");
  Serial.println("Dashboard: http://" + WiFi.localIP().toString() + "/");
  Serial.println("JSON data: http://" + WiFi.localIP().toString() + "/data");

  last_sample_ms = millis();
  last_energy_ms = millis();
}

void loop() {
  server.handleClient();

  // Take a new measurement every 500ms
  if (millis() - last_sample_ms >= 500) {
    takeMeasurement();
    last_sample_ms = millis();

    // Debug output to Serial
    Serial.printf("V: %.1f V  |  I: %.3f A  |  P: %.1f W  |  PF: %.2f  |  E: %.2f Wh\n",
                  voltage_rms, current_rms, active_power, power_factor, energy_wh);
  }
}

/*
 * ============================================================
 *  LIBRARIES NEEDED (install via Library Manager):
 *  - ESP8266WiFi        (bundled with ESP8266 board package)
 *  - ESP8266WebServer   (bundled)
 *  - ArduinoJson        by Benoit Blanchon (v6.x)
 *
 *  OPTIONAL (for ADS1115 dual-channel):
 *  - Adafruit ADS1X15   by Adafruit
 *
 *  BOARD SETTINGS:
 *  - Board: NodeMCU 1.0 (ESP-12E Module)
 *  - CPU Frequency: 80 MHz
 *  - Upload Speed: 115200
 * ============================================================
 */
