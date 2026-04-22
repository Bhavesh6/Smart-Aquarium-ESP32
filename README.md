Smart Aquarium Controller (ESP32) 

An embedded IoT-based aquarium automation system designed for reliability, offline operation, and remote control.

Key Highlights
- Works without internet (local automation)
- Temporary AP mode for debugging & setup
- IoT Cloud control (relay only)
- Fully non-blocking system
- Smart pump automation cycle
- Automatic fish feeding system

--

Problem Statement

Aquarium owners often:
- Forget to feed fish regularly
- Run filters continuously (wasting power)
- Lack remote control when away
- Cannot monitor system status easily

This project solves these issues using an ESP32-based automation system.

--

🧠 Design Decisions

- Cloud is used only for control, not logic → improves reliability
- Local timers ensure system works without internet
- Temporary AP mode avoids permanent hotspot issues
- Non-blocking code ensures smooth operation

--

Core Features

| Feature                       | Purpose                                           | Technical Notes                                                                |
| ----------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------------ |
| Web dashboard (AsyncServer)   | Control relays, pumps, and feeding schedule       | Uses `ESPAsyncWebServer`; responsive on most browsers                          |
| Temporary AP mode             | Configure Wi-Fi and check STA IP                  | Activated via **physical button** or **Serial command**, duration configurable |
| Pump automation               | Cycle ON/OFF for water pump                       | Non-blocking, uses `millis()`, adjustable timer through web                    |
| Fish feeder automation        | Scheduled AM/PM feed                              | Servo controlled, avoids overlapping feeding requests                          |
| Arduino IoT Cloud integration | Remote ON/OFF control for 4 relays                | Cloud handles only relay state, not scheduling                                 |
| LED indicators                | System status, cloud connection, AP mode, feeding | Dual LED: blue for cloud, yellow/green for status                              |
| Logging                       | Maintain last 120 logs                            | Includes timestamps (IST) and action type                                      |
| NTP time sync                 | Ensures scheduling uses IST                       | Force timezone manually; cloud does not override                               |
| AP/Web API                    | Manage Wi-Fi, feed, relay, pump, schedule         | JSON responses, fully readable by browsers or apps                             |

--

Hardware Pinout

| Component         | Pin | Description                            |
| ----------------- | --- | -------------------------------------- |
| Relay 1           | 26  | Aquarium light                         |
| Relay 2           | 27  | Filter pump (auto ON/OFF)              |
| Relay 3           | 14  | Decorative light                       |
| Relay 4           | 12  | Room light                             |
| Servo             | 13  | Fish feeder servo                      |
| Status LED        | 17  | Shows cloud, feeding, AP, Wi-Fi status |
| Activity LED      | 16  | Optional feedback                      |
| AP trigger button | 33  | Physical push button for temporary AP  |

--

System Architecture

 Networking

  * STA mode: Primary Wi-Fi, serves async web dashboard
  * Temporary AP mode: Only activated on button/serial command or Wi-Fi failure, serves synchronous setup page
  * AP can return STA IP for user reference

 Cloud

  * Uses Arduino IoT Cloud for remote relay control
  * Handles 4 relays only (filter, lights)
  * Cloud disconnect/reconnect handled with callbacks

 Timers

  * Non-blocking pump cycle (`millis()`)
  * Scheduled AM/PM feeding using NTP + local IST time
  * Prevents overlapping feeding via `feedingNow` flag

 Logging

  * Rolling buffer (max 120 entries)
  * Logs every significant event:

    * Relay state change
    * Feeding start/end
    * Pump auto toggle
    * Wi-Fi/connectivity events

LED Feedback

  * Status LED flashes differently depending on:

    * Cloud disconnected → rapid blink
    * AP active → fast blink
    * Feeding → quick blink
    * Wi-Fi connecting → slow blink
    * All good → solid ON

--

Initial Setup Guide (First Time Use)

When the device is powered ON for the first time (or after WiFi reset), it automatically starts in **Access Point (AP) mode**.

Step 1: Connect to ESP32 Setup Network

* Open WiFi settings on your phone/laptop
* Connect to:

  SSID: ESP32_Setup
  Password: (leave empty if not set)

Step 2: Open Setup Page

* Open browser and go to:

  http://192.168.4.1
  
* You will see the WiFi configuration page


Step 3: Enter Your WiFi Credentials

* Enter your:

  * WiFi SSID
  * WiFi Password
    
* Click Save & Reboot


Step 4: Device Reboots & Connects

* ESP32 will restart
* It will try to connect to your WiFi network
* If successful:

  * AP mode turns OFF
  * Dashboard becomes available

Step 5: Access Dashboard (STA Mode)

After successful WiFi connection, the ESP32 dashboard can be accessed using its IP address.

  How to Find ESP32 IP

* Check Serial Monitor logs

  WiFi connected, IP: 192.168.x.x

OR

* Check your router’s **connected devices list**

Alternate Method (Recommended)

If you cannot find the IP:

* Press the AP Button
* ESP32 will start temporary AP mode (120 seconds)

Then:

1. Connect to:

   ESP32_Setup
2. Open:

   http://192.168.4.1/

👉 This will show the current STA IP address

Open Dashboard

Once you have the IP:
http://<ESP32_IP>

Example:
http://192.168.29.46

This method avoids needing router access and makes debugging easier.

--

Known Issues (V1)

1. Web Dashboard Access

   * After long runtime, page may not load in browser
   * Works immediately after restart
   * Cause: Possible AsyncServer memory leak / ESP32 heap fragmentation

2. Arduino Cloud

   * Cloud occasionally disconnects
   * Does not auto-reconnect in some scenarios
   * After router boot delay, cloud may not connect until ESP32 restart

3. Power / Router Boot Sequence

   * Power cut → ESP32 boots faster than router
   * Cloud fails to sync because NTP + TLS connection fails
   * Temporary AP needed for Wi-Fi/STA troubleshooting

4. Pump Timer

   * If pump is manually toggled during timer, timer cycle resets
   * Countdown displays are accurate but LED indicators can blink rapidly

5. Memory / Stability

   * Long uptimes can accumulate heap fragmentation
   * Potential crash if memory runs low (no watchdog yet)

--

Future Improvements 

* Auto-reconnect cloud + OTA updates
* Mobile app for Android/iOS (Home Assistant integration)
* Multi-user system + access control
* Temperature / water quality sensor integration
* Enhanced dashboard responsiveness
* Watchdog for auto recovery

--

Getting Started

1. Flash Firmware

   * Use Arduino IDE or PlatformIO
   * ESP32 DevKit (38-pin)

2. Upload Web Dashboard

   * SPIFFS / LittleFS filesystem
   * Place `index.html` under `/data`

3. Initial Setup

   * Connect button to GPIO33
   * Press button to start temporary AP for Wi-Fi setup
   * Access AP at `192.168.4.1`

4. Cloud Setup

   * Use Arduino Cloud Device ID + Key in firmware
   * Only relay ON/OFF supported

--

Logs Example

* [10:45:00] Pump auto toggled by timer -> ON
* [10:45:00] Relay 2 ON
* [10:00:11] Feeding completed at 11 Feb 10:00:11
* [10:00:00] Feed requested but already feeding (Schedule AM)

--

Notes 

* Button-triggered temporary AP allows STA IP inspection
* Dual server design ensures AP + web dashboard run independently
* Cloud control is optional; system works fully locally
* LED behavior allows rapid status assessment
* `millis()`-based timers prevent blocking main loop


