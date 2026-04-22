Project Overview

The Smart Aquarium Controller V1 is an ESP32-based IoT-enabled system designed to automate home aquariums with both local and remote control, ensuring fish feeding, pump management, and environmental monitoring.

This system was designed with reliability in mind, using dual networking (AP + STA), non-blocking timers, and cloud control for remote operations.

It is V1 because it demonstrates full functionality but has some limitations in long-term stability and cloud resilience.


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

 Future Improvements 

* Auto-reconnect cloud + OTA updates
* Mobile app for Android/iOS (Home Assistant integration)
* Multi-user system + access control
* Temperature / water quality sensor integration
* Enhanced dashboard responsiveness
* Watchdog for auto recovery

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

Logs Example

* [10:45:00] Pump auto toggled by timer -> ON
* [10:45:00] Relay 2 ON
* [10:00:11] Feeding completed at 11 Feb 10:00:11
* [10:00:00] Feed requested but already feeding (Schedule AM)

Notes 

* Button-triggered temporary AP allows STA IP inspection
* Dual server design ensures AP + web dashboard run independently
* Cloud control is optional; system works fully locally
* LED behavior allows rapid status assessment
* `millis()`-based timers prevent blocking main loop


