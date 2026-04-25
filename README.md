# 🐟 Smart Aquarium Controller — ESP32

<p align="center">
  <img src="https://img.shields.io/badge/ESP32-IoT-blue?style=for-the-badge&logo=espressif">
  <img src="https://img.shields.io/badge/Version-v1.0.1-green?style=for-the-badge">
  <img src="https://img.shields.io/badge/Status-Active-brightgreen?style=for-the-badge">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge">
  <img src="https://img.shields.io/badge/Dashboard-Web%20UI-cyan?style=for-the-badge">
</p>

> **Fully offline-first ESP32 aquarium automation system** — automatic fish feeding, pump timer, 4-relay control, async web dashboard, cloud sync, and intelligent fault recovery. Designed to work reliably even when the internet doesn't.

---

## Table of Contents

- [What This Does](#what-this-does)
- [Features](#features)
- [Hardware Required](#hardware-required)
- [Pin Configuration](#pin-configuration)
- [Wiring Guide](#wiring-guide)
- [Library Dependencies](#library-dependencies)
- [Project Structure](#project-structure)
- [First Time Setup](#first-time-setup)
  - [Step 1 — Arduino IDE Setup](#step-1--arduino-ide-setup)
  - [Step 2 — Install Libraries](#step-2--install-libraries)
  - [Step 3 — Configure Arduino IoT Cloud](#step-3--configure-arduino-iot-cloud)
  - [Step 4 — Flash the Firmware](#step-4--flash-the-firmware)
  - [Step 5 — Upload the Web Dashboard (SPIFFS)](#step-5--upload-the-web-dashboard-spiffs)
  - [Step 6 — First Boot & WiFi Setup](#step-6--first-boot--wifi-setup)
  - [Step 7 — Access the Dashboard](#step-7--access-the-dashboard)
- [Web Dashboard](#web-dashboard)
- [System Behavior](#system-behavior)
  - [Feeding Logic](#feeding-logic)
  - [Pump Timer](#pump-timer)
  - [Relay Control](#relay-control)
  - [WiFi & Reconnect Logic](#wifi--reconnect-logic)
  - [AP Mode](#ap-mode)
  - [Cloud Integration](#cloud-integration)
  - [LED Status Indicators](#led-status-indicators)
- [API Endpoints](#api-endpoints)
- [Preferences Storage](#preferences-storage)
- [Known Behavior & Limitations](#known-behavior--limitations)
- [Future Improvements](#future-improvements)
- [Release Notes](#release-notes)

---

## What This Does

This project turns an ESP32 into a fully automated aquarium controller. It handles:

- **Scheduled fish feeding** twice a day at configurable times (morning + night), synced to IST via NTP
- **Automatic pump cycling** — runs the filter pump ON and OFF on a configurable timer loop
- **4-relay control** for lights and other devices, controllable from the web dashboard or Arduino IoT Cloud
- **Missed feeding recovery** — if the device was powered off during a scheduled feed time, it detects this on next boot and feeds immediately
- **Complete local operation** — the dashboard, feeding, pump timer, and relay control all work without internet. Cloud is used only as an extra layer of remote control

---

## Features

### Core
- Non-blocking firmware — `millis()` based throughout, no `delay()` in main logic
- Scheduled feeding: morning + night, IST timezone via NTP
- Missed feeding recovery on reboot
- Manual feed trigger from web dashboard
- Feed cooldown (30 min) — prevents scheduled feed firing again right after a manual feed
- Pump auto-timer (configurable ON/OFF minutes)
- Pump countdown display on dashboard
- 4-relay control (aquarium light, filter pump, decorative light, room light)
- Relay states saved to Preferences — restored after any reboot or power cut
- System activity log (120 entries, timestamped)
- Preferences storage for schedule, pump timer, WiFi — survives reboots

### Web Dashboard
- Mobile-first responsive design, served directly from ESP32 SPIFFS
- Bottom navigation: Home / Devices / Logs / Settings
- Live clock, network info, signal strength bars
- Toggle switches per device with real-time state sync
- Pump countdown timer display
- Schedule configuration
- WiFi change without reflashing
- Device reboot and WiFi reset buttons
- Toast notifications instead of browser alerts

### Network & Connectivity
- WiFi STA mode for normal operation
- AP mode (access point) for setup and debugging
- Auto WiFi reconnect every 30 seconds on disconnect
- Automatic fallback to AP if reconnect fails
- Temporary AP mode (120s) triggered by button press or serial command `ap`
- While in temporary AP, STA stays active — check STA IP at `http://192.168.4.1/sta`

### Cloud
- Arduino IoT Cloud integration for relay control (read/write)
- Cloud watchdog — auto-reboots if cloud stays disconnected >5 minutes while WiFi is fine
- Timezone reapplied after cloud connect (prevents cloud library from corrupting IST time)
- Cloud operates independently — local system continues if cloud is down

### Status LEDs
- Dual LED system with distinct patterns for every system state

---

## Hardware Required

| Component | Specification | Notes |
|---|---|---|
| ESP32 Dev Board | 38-pin | Any standard ESP32 devkit |
| 4-Channel Relay Module | 5V, active LOW | Optocoupler isolated recommended |
| Servo Motor | MG996R or SG90 | Continuous rotation for feeder |
| Push Button | Momentary NO | For AP mode trigger |
| Status LED x2 | 3mm or 5mm | Any color |
| Resistor x2 | 220Ω | For LEDs |
| Power Supply | 12V DC | For relay loads |
| Buck Converter | 12V → 5V | To power ESP32 from same supply |

---

## Pin Configuration

| Component | GPIO | Notes |
|---|---|---|
| Relay 1 — Aquarium Light | 26 | Active LOW |
| Relay 2 — Filter Pump | 25 | Active LOW, auto-timer controlled |
| Relay 3 — Decorative Light | 27 | Active LOW |
| Relay 4 — Room Light | 14 | Active LOW |
| Servo (Feeder) | 13 | PWM |
| Status LED | 17 | With 220Ω resistor to GND |
| Activity LED | 16 | With 220Ω resistor to GND |
| AP Button | 33 | INPUT_PULLUP, connect to GND when pressed |

---

## Wiring Guide

```
ESP32 GPIO 26 ──► Relay 1 IN  (Aquarium Light)
ESP32 GPIO 25 ──► Relay 2 IN  (Filter Pump)
ESP32 GPIO 27 ──► Relay 3 IN  (Decorative Light)
ESP32 GPIO 14 ──► Relay 4 IN  (Room Light)
ESP32 GPIO 13 ──► Servo Signal
ESP32 GPIO 17 ──► 220Ω ──► LED1 ──► GND  (Status LED)
ESP32 GPIO 16 ──► 220Ω ──► LED2 ──► GND  (Activity LED)
ESP32 GPIO 33 ──► Button ──► GND           (AP Button, uses INPUT_PULLUP)

Relay Module:
  VCC ──► 5V
  GND ──► GND
  COM (each relay) ──► Device power line
  NO  (each relay) ──► Device load

Servo:
  Red   ──► 5V
  Brown ──► GND
  Orange──► GPIO 13
```

> **Note:** Relays are **active LOW** — the ESP32 sends LOW to turn a relay ON. All relay pins are initialized HIGH (OFF) on boot before any logic runs.

---

## Library Dependencies

Install all of these via Arduino IDE **Library Manager** (`Sketch → Include Library → Manage Libraries`):

| Library | Install Name | Version Tested |
|---|---|---|
| ESPAsyncWebServer | `ESPAsyncWebServer` | Latest |
| AsyncTCP | `AsyncTCP` | Latest |
| ESP32Servo | `ESP32Servo` | Latest |
| ArduinoIoTCloud | `ArduinoIoTCloud` | Latest |
| Arduino_ConnectionHandler | `Arduino_ConnectionHandler` | Latest |

> **ESPAsyncWebServer** and **AsyncTCP** are not in the standard Library Manager. Install them manually from GitHub:
> - https://github.com/me-no-dev/ESPAsyncWebServer
> - https://github.com/me-no-dev/AsyncTCP

---

## Project Structure

```
Smart-Aquarium-ESP32/
├── firmware/
│   ├── smart_aquarium/
│   │   ├── smart_aquarium.ino    ← Main firmware
│   │   └── data/
│   │       └── index.html        ← Web dashboard (uploaded to SPIFFS)
├── README.md
├── LICENSE
└── .gitignore
```

---

## First Time Setup

### Step 1 — Arduino IDE Setup

1. Open **Arduino IDE** (2.x recommended)
2. Go to `File → Preferences`
3. In **Additional Board Manager URLs**, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to `Tools → Board → Board Manager`
5. Search `esp32` and install **esp32 by Espressif Systems**
6. Select board: `Tools → Board → ESP32 Arduino → ESP32 Dev Module`

---

### Step 2 — Install Libraries

Install all libraries listed in [Library Dependencies](#library-dependencies).

For ESPAsyncWebServer and AsyncTCP, download as ZIP from GitHub and install via `Sketch → Include Library → Add .ZIP Library`.

---

### Step 3 — Configure Arduino IoT Cloud

1. Go to [https://app.arduino.cc](https://app.arduino.cc)
2. Create a new **Thing**
3. Add 4 variables of type `CloudSwitch`:
   - `light` — Aquarium Light (Relay 1)
   - `filter` — Filter Pump (Relay 2)
   - `light1` — Decorative Light (Relay 3)
   - `light2` — Room Light (Relay 4)
4. Link your ESP32 device to the Thing
5. Copy your **Device ID** and **Secret Key**
6. In `smart_aquarium.ino`, replace the placeholders:

```cpp
const char DEVICE_LOGIN_NAME[]  = "your-device-id-here";
const char DEVICE_KEY[]         = "your-secret-key-here";
```

> If you don't want cloud control, you can leave the placeholders — the system will still work fully locally. Cloud just won't connect.

---

### Step 4 — Flash the Firmware

1. Open `firmware/smart_aquarium/smart_aquarium.ino` in Arduino IDE
2. Connect ESP32 via USB
3. Select correct port: `Tools → Port → COMx` (Windows) or `/dev/ttyUSB0` (Linux/Mac)
4. Set these board settings:
   - **Board**: ESP32 Dev Module
   - **Upload Speed**: 115200
   - **Flash Size**: 4MB (32Mb)
   - **Partition Scheme**: `Default 4MB with spiffs`
5. Click **Upload** (→ arrow button)

Wait for `Done uploading` message.

---

### Step 5 — Upload the Web Dashboard (SPIFFS)

The web dashboard (`index.html`) is stored in the ESP32's flash memory (SPIFFS) and served directly to your browser. This is a **separate upload step** from flashing the firmware.

**Install the SPIFFS upload tool:**

For Arduino IDE 2.x:
1. Download the plugin: [arduino-esp32fs-plugin](https://github.com/lorol/arduino-esp32fs-plugin/releases)
2. Place the `.vsix` or JAR in your Arduino tools folder
3. Restart Arduino IDE

**Upload SPIFFS:**

1. Make sure your `data/` folder is inside the sketch folder (same level as `.ino`)
2. Go to `Tools → ESP32 Sketch Data Upload`
3. Wait for `SPIFFS Image Uploaded` message

> ⚠️ You must re-upload SPIFFS only when `index.html` changes. If you only change the `.ino` firmware, just re-flash the firmware — SPIFFS is unaffected.

---

### Step 6 — First Boot & WiFi Setup

On first boot (no saved WiFi credentials), the ESP32 automatically starts in **AP Setup Mode**.

1. On your phone or laptop, connect to WiFi network:
   ```
   ESP32_Setup
   ```
   (No password)

2. Open browser and go to:
   ```
   http://192.168.4.1
   ```

3. Enter your home WiFi SSID and password, then click **Save & Reboot**

4. ESP32 reboots and connects to your WiFi

---

### Step 7 — Access the Dashboard

After connecting to WiFi, find the ESP32's IP address using one of these methods:

**Method A — Serial Monitor**
Open `Tools → Serial Monitor` at 115200 baud. You'll see:
```
WiFi connected, IP: 192.168.x.x
```

**Method B — AP Button (easiest)**
1. Press the physical AP button on the device
2. Connect your phone to `ESP32_Setup`
3. Open: `http://192.168.4.1/sta`
4. This returns the current STA IP in JSON

**Method C — Router device list**
Log into your router admin page and look for `ESP32` in the connected devices list.

**Open the dashboard:**
```
http://<ESP32_IP_ADDRESS>
```

---

## Web Dashboard

The dashboard is a single-page mobile app served directly from the ESP32. No external server or internet needed.

### Home Tab
- Live clock (IST, NTP synced)
- Network name, IP address, signal strength
- Morning and night feeding schedule
- Last feeding timestamp
- Manual Feed Now button

### Devices Tab
- Toggle switches for all 4 relays
- Real-time ON/OFF state with glow effect when active
- Filter pump icon spins when pump is running

### Logs Tab
- Timestamped system activity log
- Last 120 entries, newest first

### Settings Tab
- Configure morning and night feed times (hour:minute)
- Change WiFi network without reflashing
- Pump timer: set ON duration, OFF duration, enable/disable auto mode
- Pump next-change countdown
- Device section: Reset WiFi credentials, Reboot device

---

## System Behavior

### Feeding Logic

The feeder uses a continuous-rotation servo. One "feed" = 4 rotations of the auger at a speed defined by `SPEED = 120` (servo write value).

- `oneTurnTime = 2900ms` — time for one full rotation
- `rotations = 4` — number of rotations per feed
- Total feed duration = `2900 × 4 = 11.6 seconds`

**Schedule:** Feeding triggers at configured morning and night times, checked every loop iteration against NTP-synced IST time. The `lastFedMinute` guard ensures it fires exactly once per scheduled minute, not hundreds of times.

**Feed cooldown:** If a manual/web feed was triggered within the last 30 minutes, the next scheduled feed is skipped and logged. This prevents double-feeding when you feed manually just before a scheduled time.

**Missed feeding recovery:** On every boot (after NTP sync succeeds), the system checks whether a scheduled feed time has passed since the last recorded feed. If yes, it feeds immediately and logs `Missed-AM` or `Missed-PM`. This covers power cuts and router reboot delays.

**Duplicate protection:** `feedingNow` flag prevents any second feed from starting while one is already running.

---

### Pump Timer

The filter pump (Relay 2) can run on an automatic ON/OFF cycle:

- Default: 60 minutes ON, 60 minutes OFF
- Configurable from Settings tab
- Timer uses `millis()` — non-blocking, doesn't affect anything else
- Manual toggle from the dashboard resets the timer cycle from that moment
- Timer can be disabled from Settings (Auto Timer toggle)
- Pump state is saved to Preferences and restored after reboot

---

### Relay Control

All 4 relays are **active LOW** (LOW = ON, HIGH = OFF). On boot, all relays are set HIGH (all OFF) for safety before any other logic runs.

Relay states are saved to Preferences (`prefs.begin("relays")`). After any reboot — whether from power cut, cloud watchdog, or manual reboot — all relays restore to their last saved state.

---

### WiFi & Reconnect Logic

```
Boot
 └─ Try connectWiFi()
     ├─ Success → syncNTP → startDashboard → initCloud
     └─ Fail    → startAP (120s)

loop() every 30s (if WiFi lost):
 └─ Try connectWiFi()
     ├─ Success → reapply NTP/TZ → startDashboard (if not running)
     └─ Fail    → startAP (120s fallback)
```

`WiFi.setAutoReconnect(true)` is set — the ESP32 WiFi stack also attempts reconnection independently at the hardware level.

---

### AP Mode

Two types of AP mode:

**Setup AP (indefinite):** Started on first boot or when WiFi credentials are cleared. Runs until credentials are saved and device reboots.

**Temporary AP (120s):** Started by button press or serial command `ap`. Runs for 120 seconds then stops automatically. During this time, STA (WiFi) stays connected if it was connected. Access STA IP at `http://192.168.4.1/sta`.

The AP server (`WebServer`) and async dashboard server (`ESPAsyncWebServer`) are completely separate — both can run simultaneously.

---

### Cloud Integration

Arduino IoT Cloud is used **for relay control only**. Time sync is handled by NTP, not the cloud library.

Cloud callbacks:
- `CONNECT` → marks `cloudConnected = true`, resets watchdog timer, reapplies IST timezone
- `DISCONNECT` → marks `cloudConnected = false`, starts 5-minute watchdog timer
- `SYNC` → logged

**Cloud watchdog:** If cloud stays disconnected for more than 5 minutes while WiFi is fine, the device automatically reboots. This recovers the common scenario where the cloud library gets stuck after a router reboot. Normal brief disconnects (< 5 min) are handled by `ArduinoCloud.update()` naturally without triggering a reboot.

---

### LED Status Indicators

**Status LED (GPIO 17):**

| Condition | Pattern |
|---|---|
| No WiFi | Slow blink (800ms) |
| WiFi OK, Cloud disconnected | Fast blink (200ms) |
| WiFi + Cloud both OK | Solid ON |

**Activity LED (GPIO 16):**

| Condition | Pattern |
|---|---|
| Feeding active | Fast blink (120ms) |
| AP mode active | Medium blink (300ms) |
| Pump ON | Slow blink (700ms) |
| Idle | OFF |

Priority order: Feeding > AP mode > Pump > Idle.

---

## API Endpoints

All endpoints are HTTP GET, served by the async dashboard server on port 80.

| Endpoint | Description | Parameters |
|---|---|---|
| `/` | Serves the web dashboard HTML | — |
| `/status` | Returns full system status JSON | — |
| `/feed` | Triggers manual feeding | — |
| `/relay` | Toggles a relay | `id=1\|2\|3\|4` |
| `/logsdata` | Returns system log entries JSON | — |
| `/setSchedule` | Updates feeding schedule | `mh, mm, nh, nm` |
| `/setWiFi` | Saves new WiFi credentials and reboots | `ssid, pass` |
| `/setPumpTimer` | Updates pump timer settings | `on, off, enabled` |
| `/resetWiFi` | Clears WiFi credentials and reboots | — |
| `/reboot` | Reboots the device | — |

**AP server endpoints** (available at `192.168.4.1` during AP mode):

| Endpoint | Description |
|---|---|
| `/` | WiFi setup page |
| `/save` | Saves credentials and reboots |
| `/sta` | Returns current STA IP as JSON |
| `/status` | Returns AP mode status |
| `/feed` | Triggers manual feed from AP |

**Example `/status` response:**
```json
{
  "time": "22:00:00",
  "epoch": 1745000000,
  "ip": "192.168.1.42",
  "ssid": "YourNetwork",
  "rssi": -62,
  "lastFeed": "25 Apr 22:00:01",
  "mh": 10, "mm": 0,
  "nh": 22, "nm": 0,
  "r1": 1, "r2": 0, "r3": 0, "r4": 0,
  "pumpOnMin": 60,
  "pumpOffMin": 60,
  "pumpEnabled": 1,
  "pumpCountdown": 1847
}
```

---

## Preferences Storage

All persistent settings are stored in ESP32 NVS (Non-Volatile Storage) via the `Preferences` library. Data survives power cuts, reboots, and firmware reflashes (unless you explicitly clear them).

| Namespace | Key | Type | Description |
|---|---|---|---|
| `wifi` | `ssid` | String | Saved WiFi SSID |
| `wifi` | `pass` | String | Saved WiFi password |
| `sched` | `mh`, `mm` | Int | Morning feed hour/minute |
| `sched` | `nh`, `nm` | Int | Night feed hour/minute |
| `pump` | `on` | ULong | Pump ON duration (ms) |
| `pump` | `off` | ULong | Pump OFF duration (ms) |
| `pump` | `en` | Bool | Pump auto mode enabled |
| `relays` | `r1`–`r4` | Bool | Last relay state per relay |
| `feed` | `lastEpoch` | ULong | Unix timestamp of last feed |

To factory reset all preferences: use the **Reset WiFi** button in Settings (clears `wifi` namespace). For a full preferences wipe, use `prefs.clear()` or the `nvs_flash_erase()` tool.

---

## Known Behavior & Limitations

- **WiFi password in URL:** The `/setWiFi` endpoint passes the password as a GET query parameter. This is fine on a local network but not suitable for untrusted networks. For home use this is acceptable.
- **Both feeds missed:** If both morning and night feeds are missed (very long power cut), morning feed triggers first. Since feeding is non-blocking, the night check also fires immediately — `feedingNow` guard prevents double-feed, but night feed will not auto-retry after morning completes. Rare scenario but worth knowing.
- **Cloud not initialized without NTP:** If NTP sync fails on boot, Arduino IoT Cloud is not initialized. Cloud will be initialized on next successful WiFi reconnect that also succeeds NTP sync.
- **AP server delay():** The AP portal `/save` endpoint uses `delay(1000)` before `ESP.restart()`. This is intentional and safe — the AP server is a synchronous `WebServer`, not the async server, so this does not cause issues.
- **SPIFFS and firmware are separate uploads:** Reflashing firmware does not erase SPIFFS. Re-uploading SPIFFS does not erase firmware. They are independent regions of flash.

---

## Future Improvements

- OTA (Over-the-Air) firmware updates from dashboard
- Water temperature sensor (DS18B20) display
- TDS/water quality sensor integration  
- Multiple feeding schedules (more than 2 per day)
- Per-device scheduling (turn lights ON/OFF at set times automatically)
- Home Assistant MQTT integration
- Mobile app (Flutter or React Native)
- Feed quantity control from dashboard (number of rotations)
- Email/Telegram alert on system events (missed feed, cloud disconnect, pump fault)

---

 
## Release Notes
 
### v1.0.1 — Current Release
 
**New Features:**
- Missed feeding recovery — detects and recovers missed scheduled feeds on reboot
- Feed cooldown (30 min) — prevents duplicate feed when manual + scheduled overlap
- Cloud watchdog — auto-reboots if cloud stays disconnected >5 min while WiFi is fine
- Relay state persistence — all relay states saved and restored across reboots/power cuts
- Completely redesigned web dashboard — mobile-first app layout with bottom nav, toggle switches, signal bars, toast notifications
**Bug Fixes:**
- Fixed duplicate async route registration — was causing dashboard to freeze after long uptime or WiFi reconnect
- Fixed `delay()` in async handlers — `/setWiFi`, `/resetWiFi`, `/reboot` now use `pendingReboot` flag so HTTP response sends cleanly before restart
- Fixed schedule firing hundreds of times per second — `lastFedMinute` guard now ensures exactly one trigger per scheduled minute
- Fixed schedule input validation — invalid hour/minute values now rejected with HTTP 400
- Fixed pin table in README — Relay 2 is GPIO 25, Relay 3 is GPIO 27, Relay 4 is GPIO 14 (was wrong in v1.0)
**Known Issues Resolved:**
- Web dashboard stopping after long uptime → fixed (duplicate handler registration)
- Arduino IoT Cloud failing to reconnect after router reboot → fixed (cloud watchdog)
- Manual restart required after cloud disconnect → fixed (watchdog auto-reboots)
  
---
 
### v1.0.0 — Initial Release
 
- Basic ESP32 aquarium controller
- Async web dashboard (SPIFFS hosted)
- 4-relay control
- Servo fish feeder with scheduling
- Pump auto-timer
- AP mode setup portal
- Arduino IoT Cloud relay control
- Temporary AP mode via button
- NTP time sync (IST)
- Dual LED status system
- System activity log

## License

MIT License — see [LICENSE](LICENSE) for details.

---

<p align="center">
  Built with ❤️ for the fish 🐟
</p>
