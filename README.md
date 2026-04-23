# 🐟 Smart Aquarium Controller (ESP32) 

![ESP32](https://img.shields.io/badge/ESP32-IoT-blue)
![Version](https://img.shields.io/badge/version-v1.0-green)
![Status](https://img.shields.io/badge/status-stable-orange)
![License](https://img.shields.io/badge/license-MIT-yellow)

An **ESP32-based Smart Aquarium Automation System** designed for reliability, offline operation, and remote control.

---

##  What is this?

This system automates essential aquarium tasks:

*  Automatic fish feeding
*  Smart pump ON/OFF cycle
*  Multi-relay control (lights, devices)
*  Local web dashboard
*  Remote control via Arduino IoT Cloud

 Designed to **work even without internet**

---

##  Key Features

*  Fully **non-blocking system**
*  **Scheduled feeding** (NTP synced IST time)
*  **Pump automation timer (ON/OFF loop)**
*  **Temporary AP mode for setup & debugging**
*  **Async web dashboard (fast + responsive)**
*  **Cloud control (relay only, safe design)**
*  **Status LED feedback system**
*  Preferences storage (no reconfiguration needed)

---

##  Unique Feature — Temporary AP Mode

Unlike typical IoT devices:

* Press button → AP starts for 120 seconds
* Connect to `ESP32_Setup`
* Check device IP or reconfigure WiFi

 No need to access router or guess IP
 Makes debugging extremely easy

---

##  System Architecture

```
        [User / Mobile]
               │
     ┌─────────▼─────────┐
     │  Web Dashboard    │
     └─────────┬─────────┘
               │
        ┌──────▼──────┐
        │   ESP32     │
        │  Controller │
        └──────┬──────┘
               │
     ┌─────────┼─────────┐
     │         │         │
 [Relays]   [Servo]   [Timers]
               │
        [Fish Feeder]

               │
        (Optional)
               ▼
       Arduino IoT Cloud
```
---
##  Hardware Used

- ESP32 Dev Board (38-pin)
- 4-Channel Relay Module
- Servo Motor (MG996R)
- Push Button (AP Mode Trigger)
- Status LEDs (2x)
- Power Supply (12V + Buck Converter)
---

##  Pin Configuration

| Component       | GPIO |
| --------------- | ---- |
| Relay 1 (Light) | 26   |
| Relay 2 (Pump)  | 27   |
| Relay 3         | 14   |
| Relay 4         | 12   |
| Servo (Feeder)  | 13   |
| Status LED 1    | 17   |
| Status LED 2    | 16   |
| AP Button       | 33   |

---

##  Status LED Behavior

| Condition          | LED Behavior |
| ------------------ | ------------ |
| Normal (all OK)    | Solid ON     |
| WiFi connecting    | Slow blink   |
| AP mode active     | Fast blink   |
| Feeding            | Quick blink  |
| Cloud disconnected | Rapid blink  |

---
##  Wiring Overview

- Relays are active LOW
- Servo connected to GPIO 13
- Button uses INPUT_PULLUP (connect to GND when pressed)
- LED uses resistor (220Ω recommended)

---

##  Fault Handling

System is designed to handle failures:

- WiFi loss → auto reconnect attempt
- Router delay → retry logic
- Cloud failure → local system continues
- Feeding protected from duplicate triggers

---

##  Initial Setup Guide

###  Step 1: Connect to ESP32

* Connect to WiFi:

```
ESP32_Setup
```

---

###  Step 2: Open Setup Page

```
http://192.168.4.1
```

---

###  Step 3: Enter WiFi Credentials

* Enter SSID & Password
* Click **Save & Reboot**

---

###  Step 4: Device Connects

* ESP32 connects to your WiFi
* AP mode turns OFF

---

###  Step 5: Access Dashboard

#### Find IP:

* Serial Monitor
  **OR**
* Router device list

---

###  Alternate Method (Recommended)

* Press **AP Button**
* Connect to `ESP32_Setup`
* Open:

```
http://192.168.4.1/sta
```

 Shows current ESP32 IP

---

###  Open Dashboard

```
http://<ESP32_IP>
```

---

##  Design Philosophy

* Cloud is used **only for control**
* Core logic runs locally
* System works even if internet fails

---

##  Known Issues 

* Web dashboard may stop responding after long uptime
* Arduino IoT Cloud may fail to reconnect after router reboot
* ESP32 boots faster than router → cloud fails
* Manual restart may be required

---

##  Known Behavior

* Multiple schedule triggers may occur within the same second
* Feeding logic prevents duplicate execution safely

---

##  Smart WiFi Recovery & Fallback System

The system is designed to handle real-world network failures automatically.

###  Auto Reconnect Logic

* If WiFi disconnects:

  * ESP32 continuously attempts reconnection
  * No manual restart required

---

### 📡 Automatic Fallback AP Mode

If reconnection fails:

* ESP32 starts **temporary AP mode**
* User can:

  * Access dashboard
  * Reconfigure WiFi
  * Check system status

---

###  Use Case: WiFi Password Changed

If router credentials are changed:

1. ESP32 fails to reconnect
2. Automatically enables AP mode
3. User connects to:

   ```
   ESP32_Setup
   ```
4. Opens:

   ```
   http://192.168.4.1
   ```
5. Enters new WiFi credentials

 System recovers without reflashing firmware

---

###  Hybrid Mode Behavior

* While connected to WiFi → Normal STA mode
* On failure → AP fallback activates
* User can manually trigger AP anytime (button / serial)

---

###  Why This Matters

Most IoT systems:

* Fail silently on WiFi issues
* Require manual reset or reprogramming

This system:

* Detects failure
* Recovers automatically
* Provides user access via AP

 Makes the system **robust and user-friendly**

---

##  Future Improvements 

*  Mobile App (Home Assistant / Flutter)
*  OTA updates
*  Auto cloud reconnect system
*  Multi-user access
*  Sensor integration

---

##  Philosophy

> “Designed to work even when the internet doesn’t.”

---

##  License

MIT License
