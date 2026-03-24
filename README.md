# ⚡ SparkLab Power Monitor

Real-time AC voltage, current, and power monitor using NodeMCU ESP8266 with a live browser dashboard.

---

## What You Need

| Component | Purpose |
|-----------|---------|
| NodeMCU ESP8266 | Microcontroller + WiFi |
| ZMPT101B module | AC voltage sensing |
| ACS712 module (5A) | AC current sensing |
| USB cable + 5V adapter | Power the ESP |
| Your phone/laptop browser | View the dashboard |

---

## Wiring

Both sensor outputs share the single **A0** pin on the ESP8266.

```
ZMPT101B  OUT ──┐
                ├──► A0 (ESP8266)
ACS712    OUT ──┘

ZMPT101B  VCC ──► 3.3V
ZMPT101B  GND ──► GND

ACS712    VCC ──► 3.3V
ACS712    GND ──► GND
```

> ⚠️ **Power both sensors at 3.3V, not 5V.**
> The ESP8266 A0 pin only accepts 0–1V max.
> At 3.3V supply, the sensor idle midpoints are ~1.65V — safe for the ADC.
> At 5V supply you MUST add a voltage divider (two 100kΩ resistors) before A0 or you will damage the ESP.

---

## Files

```
PowerMonitor_ESP8266.ino   — Arduino firmware (flash this to the ESP)
dashboard.html             — Browser dashboard (open this on your phone/laptop)
README.md                  — This file
```

---

## Step 1 — Flash the Firmware

1. Open `PowerMonitor_ESP8266.ino` in Arduino IDE
2. Install board support if you haven't:
   - Go to **File → Preferences**
   - Add this URL to Additional Boards Manager URLs:
     `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Go to **Tools → Board → Boards Manager**, search `esp8266`, install it
3. Install the one required library:
   - **Sketch → Include Library → Manage Libraries**
   - Search `ArduinoJson` by Benoit Blanchon → install **version 6.x**
4. Set your board:
   - **Tools → Board → NodeMCU 1.0 (ESP-12E Module)**
   - **Tools → CPU Frequency → 80 MHz**
   - **Tools → Upload Speed → 115200**
5. Edit these two lines in the firmware with your WiFi details:
   ```cpp
   const char* SSID     = "YOUR_WIFI_SSID";
   const char* PASSWORD = "YOUR_WIFI_PASSWORD";
   ```
6. Click **Upload**

---

## Step 2 — Find the ESP's IP Address

1. After upload, open **Tools → Serial Monitor**
2. Set baud rate to **115200**
3. Press the **RST** button on the NodeMCU
4. You'll see something like:
   ```
   SparkLab Power Monitor v2.0
   Idle ADC samples: 341 339 342 340 ...
   WiFi........... connected
   IP:       http://192.168.1.105/
   JSON API: http://192.168.1.105/data
   ```
5. Note down that IP address — you'll need it for the dashboard

---

## Step 3 — Calibrate the Sensors

Calibration is done by editing 4 numbers at the top of the `.ino` file. Do this **before** connecting any AC load.

### A — Find the ADC zero points

At boot, the Serial Monitor prints idle ADC samples (the line starting with `Idle ADC samples:`).
These are the sensor midpoints with no signal. Note that number.

```cpp
const float V_ZERO_RAW   = 512.0;   // ← replace with your idle ADC value
const float ACS_ZERO_RAW = 512.0;   // ← replace with your idle ADC value
```

Both will likely be the same value since both sensors share A0. Typical values:
- At 3.3V supply: **~341**
- At 5V supply with ÷2 divider: **~512**

### B — Calibrate voltage (VCAL_FACTOR)

1. Connect AC mains to the ZMPT101B input
2. Watch Serial Monitor — it prints `[V] x.xx V` every 500ms
3. Measure the same outlet with a multimeter
4. Adjust `VCAL_FACTOR` until the two match:

```
Formula: new VCAL_FACTOR = current VCAL_FACTOR × (multimeter reading / firmware reading)

Example: multimeter says 231V, firmware prints 1.1V
         new factor = 230.0 × (231 / 1.1) = 48,272  ← keep adjusting
```

Start with `VCAL_FACTOR = 1.0`, read what comes out, then scale up in one step.

### C — Calibrate current (ICAL_FACTOR)

1. Connect a simple resistive load — a **filament bulb** is ideal (not an LED, not a phone charger)
   - 100W bulb at 230V draws 100/230 = **0.43A**
   - 60W bulb at 230V draws 60/230 = **0.26A**
2. Temporarily add this line inside `readCurrentRMS()` just before the `return`:
   ```cpp
   Serial.print("raw_rms = "); Serial.println(raw_rms);
   ```
3. Note the printed `raw_rms` value
4. Calculate and set:
   ```cpp
   // ICAL_FACTOR = known_amps / raw_rms
   // Example: 0.43A / 23.5 raw = 0.0183
   const float ICAL_FACTOR = 0.0183;
   ```
5. Remove the debug `Serial.print` line, re-upload

---

## Step 4 — Open the Dashboard

1. Open `dashboard.html` in any browser (Chrome, Firefox, Safari)
   - Works on your phone too — just open the file from your phone's browser
2. In the **ESP IP** field at the top right, enter the IP you noted in Step 2
3. Click **Connect**
4. The cards and charts will update live every second

> 💡 The dashboard works on demo mode automatically if no ESP is connected, so you can preview it before wiring anything.

---

## Dashboard Features

| Feature | Description |
|---------|-------------|
| Voltage card | Live Vrms from ZMPT101B |
| Current card | Live Irms from ACS712 |
| Active Power | V × I × Power Factor |
| Apparent Power | V × I (VA) |
| Power Factor | Assumed 0.95 (see note below) |
| Energy card | Watt-hours accumulated since boot or last reset |
| Cost estimate | Rough ₹ cost at ₹8/kWh (edit in dashboard JS) |
| Live charts | Last 60 seconds of V, I, Power, Energy |
| Pause / Resume | Freeze the charts without stopping the ESP |
| Reset Energy | Zero the Wh counter (also resets on the ESP) |
| Export CSV | Download all readings as a .csv file |

---

## How the Single A0 Pin Works

The ESP8266 has only one analog input. Both sensors share it using **alternating sampling**:

```
t = 0ms    → Read voltage RMS  (takes ~90ms)  → stores voltage_rms
t = 500ms  → Read current RMS  (takes ~90ms)  → computes power
t = 1000ms → Read voltage RMS  → repeat
```

Each reading is a true RMS calculation over 600 samples, covering ~2.5 full AC cycles at 50Hz. The 500ms gap between voltage and current readings is negligible for a power monitor — both values are stable averages.

**Limitation:** True power factor measurement requires simultaneous V and I sampling. This project uses an assumed PF of 0.95. For a real PF measurement, add an ADS1115 I2C ADC module (two separate channels, ~₹80).

---

## JSON API

The ESP serves data at `http://<ip>/data`. You can use this from any app or script on the same network.

**Example response:**
```json
{
  "voltage":  231.4,
  "current":  0.432,
  "power":    95.2,
  "apparent": 99.8,
  "pf":       0.95,
  "energy":   0.026,
  "uptime":   142
}
```

**Endpoints:**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/data` | GET | JSON with all readings |
| `/reset` | GET | Reset energy counter to 0 |
| `/` | GET | Simple status page |

---

## Troubleshooting

**Serial Monitor shows garbage text**
→ Check baud rate is set to 115200

**WiFi never connects**
→ Double-check SSID and password. ESP8266 only supports 2.4GHz networks, not 5GHz.

**Dashboard shows "No data · check IP"**
→ Make sure your phone/laptop and the ESP are on the same WiFi network. Try opening `http://<ip>/data` directly in the browser — if it shows JSON, the ESP is fine and the issue is the IP field in the dashboard.

**Voltage reads zero or garbage**
→ Check V_ZERO_RAW — if the idle ADC value is wrong, the offset subtraction goes negative. Re-run the idle sample check from Step 3A.

**Current always reads a small non-zero value**
→ ACS_ZERO_RAW is slightly off. Disconnect the load, watch Serial for the idle current ADC value, and update the constant.

**Vrms seems way off even after calibration**
→ The ZMPT101B has a small trimmer potentiometer on the module. Turn it slowly until the output waveform (viewed on Serial plotter with raw ADC printed) is a clean sine wave centered at your zero point.

---

## Serial Monitor — What to Expect

```
SparkLab Power Monitor v2.0 — alternating A0 mode
Idle ADC samples: 341 341 342 340 341 342 340 341 342 341 341 340
WiFi............ connected
IP:       http://192.168.1.105/
JSON API: http://192.168.1.105/data
HTTP server started

[V] 231.40 V
[I] 0.432 A  P=95.18 W  S=99.96 VA  E=0.026 Wh
[V] 231.38 V
[I] 0.431 A  P=95.00 W  S=99.77 VA  E=0.053 Wh
```

`[V]` and `[I]` alternate every 500ms. Once both are showing realistic values that match your multimeter, calibration is complete.

---

## Customising the Dashboard

All configuration is at the top of `dashboard.html`:

```javascript
let ESP_IP = '192.168.1.100';  // default IP shown in the input box
let FETCH_INTERVAL = 1000;     // how often to poll the ESP (ms)
const MAX_POINTS = 60;         // how many seconds of history to show
```

To change the electricity rate shown in the energy card:
```javascript
// Find this line in the updateUI() function:
setText('e-sub', `≈ ₹${(d.energy/1000 * 8).toFixed(4)} (₹8/kWh)`);
//                                           ↑
//                                    change this number
```
