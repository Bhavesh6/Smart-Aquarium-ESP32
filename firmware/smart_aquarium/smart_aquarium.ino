/*
  Smart Aquarium — Complete Code (AP button + temporary AP behavior)
  - STA (async dashboard) + AP (setup portal)
  - Press physical button or send "ap" on Serial to start a temporary AP (default 120s).
  - Temporary AP remains active for duration even if STA is connected (so you can check STA IP).
  - Auto WiFi reconnect and fallback to AP
  - 4 relays + continuous servo feeder
  - Non-blocking feeding + schedule (IST via NTP)
  - Logs buffer + serial debug
  - Web endpoints: /status, /feed, /relay, /setSchedule, /setWiFi, /logsdata, /resetWiFi, /reboot, /setPumpTimer
  - Arduino IoT Cloud: remote ON/OFF control for 4 relays ONLY (no time sync)

  FIXES APPLIED:
  [1] dashboardHandlersRegistered guard — prevents duplicate async route registration on reconnect
  [2] pendingReboot flag — removes delay() from async handlers (was corrupting responses)
  [3] lastFedMinute dedup — schedule fires exactly once per minute, not hundreds of times
  [4] Schedule input validation — rejects invalid hour/minute values
  [5] Cloud watchdog — auto-reboots if cloud disconnected >5min while WiFi is fine
  [6] Relay state persistence — relay states saved to Preferences, restored after reboot
  [7] Missed feeding recovery — on boot, checks if a scheduled feed was missed while powered off
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <ESP32Servo.h>
#include <time.h>

#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>

WebServer apServer(80);
AsyncWebServer asyncServer(80);
Preferences prefs;
Servo feederServo;

/* ===== PINS ===== */
#define R1 26   // Aquarium Light
#define R2 25   // Filter Pump (auto-timer)
#define R3 27   // Decorative Light
#define R4 14   // Room Light

int relayPins[4] = {R1, R2, R3, R4};
bool relayState[4] = {false, false, false, false}; // false == OFF (active LOW)

#define STATUS_LED 17
#define ACTIVITY_LED 16

bool cloudConnected = false;

/* Physical button to start temporary AP */
const int BUTTON_PIN = 33;
const unsigned long BUTTON_DEBOUNCE_MS = 50;

/* AP timeout default */
const unsigned long AP_TIMEOUT_MS = 120000UL; // 120 seconds

/* FEEDER */
const int STOP = 90;
const int SPEED = 120;
const int oneTurnTime = 2800;
int rotations = 5;
bool feedingNow = false;
unsigned long feedStartTime = 0;
unsigned long feedDuration = 0;
unsigned long lastManualFeedTime = 0;
const unsigned long FEED_COOLDOWN_MS = 30UL * 60UL * 1000UL; // 30 minutes

/* SCHEDULE */
int mHour = 10, mMin = 0;
int nHour = 22, nMin = 0;
String lastFeed = "Never";

/* PUMP TIMER (ms) */
unsigned long pumpOnDuration  = 60UL * 60UL * 1000UL;
unsigned long pumpOffDuration = 60UL * 60UL * 1000UL;
unsigned long pumpLastToggle = 0;
bool pumpTimerEnabled = true;

/* LOGS */
#define MAX_LOGS 120
String systemLogs[MAX_LOGS];
int logCount = 0;

/* TIME / NTP */
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // IST
const int daylightOffset_sec = 0;

/* FLAGS */
bool apModeActive = false;
bool dashboardRunning = false;
unsigned long lastReconnectAttempt = 0;
bool wifiConnectedOnce = false;
bool cloudInitialized = false;
bool dashboardHandlersRegistered = false; // [FIX 1]
bool pendingReboot = false;               // [FIX 2]

/* CLOUD WATCHDOG [FIX 5] */
unsigned long cloudDisconnectedSince = 0;
const unsigned long CLOUD_RECONNECT_TIMEOUT = 5UL * 60UL * 1000UL; // 5 minutes

/* AP expiry + mode */
unsigned long apExpiryMillis = 0;
bool apTemporary = false;
bool apHandlersRegistered = false;

/* Button state */
int lastButtonState = HIGH;
unsigned long lastButtonChange = 0;

/* Arduino IoT Cloud credentials — replace with your values */
const char DEVICE_LOGIN_NAME[]  = "------------------";
const char DEVICE_KEY[]         = "------------------";

/* ===== ARDUINO IOT CLOUD VARIABLES ===== */
CloudSwitch filter;   // Relay 2 (Pump)
CloudSwitch light;    // Relay 1
CloudSwitch light1;   // Relay 3
CloudSwitch light2;   // Relay 4

WiFiConnectionHandler ArduinoIoTPreferredConnection("", "");

/* Forward declarations */
void pushLog(String s);
String jsonEscape(String s);
void startFeeding(const char* source);
void updateFeeding();
void setRelay(int id, bool on);
void updatePumpTimer();
void printSavedWiFi();
void printScanNetworks();
bool isSSIDNearby(const String &target);
bool connectWiFi();
bool syncTimeWithRetry();
void startAP(unsigned long durationMs = AP_TIMEOUT_MS);
void stopAP();
void startDashboard();
void WiFiEvent(WiFiEvent_t event);
void initCloud();
void handleSerialInput();
void handleButton();
void checkMissedFeeding();

/* -------------------- Logging -------------------- */
void pushLog(String s){
  struct tm t;
  String ts = "";
  if (WiFi.status() == WL_CONNECTED && getLocalTime(&t)) {
    char buf[20]; strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    ts = "[" + String(buf) + "] ";
  }
  String full = ts + s;
  Serial.println(full);
  if (logCount < MAX_LOGS) logCount++;
  for (int i = logCount - 1; i > 0; --i) systemLogs[i] = systemLogs[i-1];
  systemLogs[0] = full;
}

String jsonEscape(String s){
  String out = "";
  for (size_t i = 0; i < s.length(); ++i){
    char c = s[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else out += c;
  }
  return out;
}

/* -------------------- Feeder -------------------- */
void startFeeding(const char* source){
  if (feedingNow) { pushLog("Feed requested but already feeding (" + String(source) + ")"); return; }

  // Cooldown check — skip scheduled feeds if fed recently, but allow manual/missed override
  String src = String(source);
  bool isScheduled = (src == "Schedule AM" || src == "Schedule PM");
  if (isScheduled && lastManualFeedTime > 0 &&
      millis() - lastManualFeedTime < FEED_COOLDOWN_MS) {
    pushLog("Scheduled feed skipped — already fed recently (" + src + ")");
    return;
  }

  // Track manual/web feeds
  if (!isScheduled) lastManualFeedTime = millis();

  feedingNow = true;
  feederServo.write(SPEED);
  feedStartTime = millis();
  feedDuration = (unsigned long)oneTurnTime * rotations;
  pushLog(String("Feeding started (") + source + ")");
 
  if (feedingNow) {
    pushLog("Feed requested but already feeding (" + String(source) + ")");
    return;
  }
  feedingNow = true;
  feederServo.write(SPEED);
  feedStartTime = millis();
  feedDuration = (unsigned long)oneTurnTime * rotations;
  pushLog(String("Feeding started (") + source + ")");
}

void updateFeeding(){
  if (feedingNow && millis() - feedStartTime >= feedDuration){
    feederServo.write(STOP);
    feedingNow = false;
    struct tm t;
    if (WiFi.status() == WL_CONNECTED && getLocalTime(&t)){
      char buf[40]; strftime(buf, sizeof(buf), "%d %b %H:%M:%S", &t);
      lastFeed = String(buf);

      // [FIX 7] Save last feed epoch to Preferences so it survives reboot
      time_t now = mktime(&t);
      prefs.begin("feed", false);
      prefs.putULong("lastEpoch", (unsigned long)now);
      prefs.end();

    } else lastFeed = "Unknown (no time)";
    pushLog("Feeding completed at " + lastFeed);
  }
}

/* -------------------- Relays -------------------- */
void setRelay(int id, bool on){
  if (id < 1 || id > 4) return;
  int idx = id - 1;
  relayState[idx] = on;
  digitalWrite(relayPins[idx], on ? LOW : HIGH); // active LOW

  // [FIX 6] Save relay state to Preferences so it survives reboot
  prefs.begin("relays", false);
  prefs.putBool(("r" + String(id)).c_str(), on);
  prefs.end();

  if (id == 2) pumpLastToggle = millis();

  if (cloudInitialized){
    if (id == 1) light  = on;
    if (id == 2) filter = on;
    if (id == 3) light1 = on;
    if (id == 4) light2 = on;
  }

  pushLog(String("Relay ") + String(id) + (on ? " ON" : " OFF"));
}

/* -------------------- Pump timer (non-blocking) -------------------- */
void updatePumpTimer() {
  if (!pumpTimerEnabled) return;

  unsigned long now = millis();
  unsigned long elapsed = now - pumpLastToggle;
  unsigned long target = relayState[1] ? pumpOnDuration : pumpOffDuration;

  if (elapsed >= target) {
    setRelay(2, !relayState[1]);
    pumpLastToggle = now;
    pushLog(String("Pump auto toggled by timer -> ") + (relayState[1] ? "ON" : "OFF"));
  }
}

/* -------------------- Missed feeding check [FIX 7] -------------------- */
void checkMissedFeeding() {
  struct tm t;
  if (!getLocalTime(&t)) {
    pushLog("Missed feed check skipped — no time available");
    return;
  }

  prefs.begin("feed", true);
  unsigned long lastEpoch = prefs.getULong("lastEpoch", 0);
  prefs.end();

  time_t nowEpoch = mktime(&t);

  // Build today's morning feed epoch
  struct tm morning = t;
  morning.tm_hour = mHour; morning.tm_min = mMin; morning.tm_sec = 0;
  time_t morningEpoch = mktime(&morning);

  // Build today's night feed epoch
  struct tm night = t;
  night.tm_hour = nHour; night.tm_min = nMin; night.tm_sec = 0;
  time_t nightEpoch = mktime(&night);

  bool fed = false;

  // Morning missed?
  if (nowEpoch >= morningEpoch && (long)lastEpoch < (long)morningEpoch) {
    pushLog("Missed morning feed detected — feeding now");
    startFeeding("Missed-AM");
    fed = true;
  }

  // Night missed?
  if (nowEpoch >= nightEpoch && (long)lastEpoch < (long)nightEpoch) {
    pushLog("Missed night feed detected — feeding now");
    startFeeding("Missed-PM");
    fed = true;
  }

  if (!fed) pushLog("No missed feedings detected");
}

/* -------------------- WiFi helper functions -------------------- */
void printSavedWiFi(){
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  Serial.println("---- Saved WiFi creds ----");
  Serial.print("SSID: '"); Serial.print(ssid); Serial.println("'");
  Serial.print("PASS length: "); Serial.println(pass.length());
  Serial.println("--------------------------");
}

void printScanNetworks(){
  Serial.println("Scanning nearby WiFi networks (this may take ~3s)...");
  int n = WiFi.scanNetworks();
  if (n == 0) { Serial.println("No networks found"); return; }
  for (int i = 0; i < n; ++i) {
    String ss = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    String enc = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Sec";
    Serial.printf("%d: %s  (RSSI %d) %s\n", i+1, ss.c_str(), rssi, enc.c_str());
  }
  WiFi.scanDelete();
}

bool isSSIDNearby(const String &target) {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) if (WiFi.SSID(i) == target) return true;
  return false;
}

bool connectWiFi(){
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  Serial.println();
  pushLog("Attempting WiFi connect...");
  printSavedWiFi();

  ssid.trim(); pass.trim();
  if (ssid == "") { pushLog("No saved SSID (empty)."); return false; }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);

  bool found = isSSIDNearby(ssid);
  if (!found) {
    pushLog("Saved SSID not found in scan. Will still attempt connect but check 2.4GHz or SSID spelling.");
    printScanNetworks();
  } else pushLog("Saved SSID found in scan.");

  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.setAutoReconnect(true);

  int tries = 0;
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED && tries < 30) { delay(500); yield(); Serial.print("."); tries++; }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED){ pushLog("WiFi connected, IP: " + WiFi.localIP().toString()); return true; }
  pushLog("WiFi connection FAILED after attempts");
  return false;
}

/* -------------------- AP functions -------------------- */
void startAP(unsigned long durationMs){
  apModeActive = true;
  apTemporary = (durationMs > 0);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32_Setup");
  WiFi.setSleep(false);

  if (durationMs > 0) apExpiryMillis = millis() + durationMs;
  else apExpiryMillis = 0;

  if (!apHandlersRegistered){
    apServer.on("/", HTTP_GET, [](){
      String page =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Setup</title></head><body style='font-family:Arial;max-width:420px;margin:auto;padding:12px'>"
        "<h2>ESP32 WiFi Setup</h2>"
        "<form action='/save' method='get'>SSID:<br><input name='s' placeholder='Your WiFi SSID' style='width:100%'><br><br>"
        "Password:<br><input name='p' type='password' placeholder='Password' style='width:100%'><br><br>"
        "<button type='submit'>Save & Reboot</button></form>"
        "<p style='color:#888;font-size:13px'>After saving the device will reboot and try to connect.</p>"
        "<p style='color:#888;font-size:12px'>If your router has both 2.4GHz and 5GHz, ensure the 2.4GHz SSID is visible.</p>"
        "<p style='color:#444;font-size:13px'>To view STA IP (if connected) open <code>/sta</code></p>"
        "</body></html>";
      apServer.send(200, "text/html", page);
    });

    apServer.on("/save", [](){
      String ssid = apServer.arg("s");
      String pass = apServer.arg("p");
      prefs.begin("wifi", false);
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      prefs.end();
      apServer.send(200, "text/html", "<p>Saved. Rebooting...</p>");
      pushLog("WiFi credentials saved via AP portal");
      delay(1000);
      ESP.restart();
    });

    apServer.on("/feed", [](){ startFeeding("AP"); apServer.send(200, "text/plain", "Feeding triggered"); });

    apServer.on("/sta", [](){
      String staInfo = "{\"sta_ip\":\"";
      if (WiFi.status() == WL_CONNECTED) staInfo += WiFi.localIP().toString();
      else staInfo += "Not connected";
      staInfo += "\"}";
      apServer.send(200, "application/json", staInfo);
    });

    apServer.on("/status", [](){
      String ip = WiFi.softAPIP().toString();
      String json = "{\"mode\":\"AP\",\"ip\":\"" + ip + "\"}";
      apServer.send(200, "application/json", json);
    });

    apHandlersRegistered = true;
  }

  apServer.begin();
  pushLog(String("AP Mode started: ESP32_Setup (") + WiFi.softAPIP().toString() + ") - for " + String(durationMs/1000) + "s");
}

void stopAP(){
  apServer.stop();
  WiFi.softAPdisconnect(false);
  WiFi.mode(WIFI_STA);
  apModeActive = false;
  apExpiryMillis = 0;
  apTemporary = false;
  pushLog("AP stopped");
}

/* -------------------- Dashboard (STA, async) -------------------- */
void startDashboard(){
  dashboardRunning = true;
  apModeActive = false;
  pushLog("Starting async dashboard server...");

  if (!dashboardHandlersRegistered) { // [FIX 1]

    asyncServer.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
      r->send(SPIFFS, "/index.html", "text/html");
    });

    asyncServer.on("/feed", HTTP_GET, [](AsyncWebServerRequest *r){
      startFeeding("Web");
      r->send(200, "text/plain", "OK");
    });

    asyncServer.on("/relay", HTTP_GET, [](AsyncWebServerRequest *r){
      if (r->hasParam("id")){
        int id = r->getParam("id")->value().toInt();
        if (id >= 1 && id <= 4){
          setRelay(id, !relayState[id-1]);
          r->send(200, "text/plain", "OK");
          return;
        }
      }
      r->send(400, "text/plain", "Bad Request");
    });

    asyncServer.on("/status", HTTP_GET, [](AsyncWebServerRequest *r){
      struct tm t; String timeStr = "--:--:--"; long epoch = 0;
      if (WiFi.status() == WL_CONNECTED && getLocalTime(&t)){
        char buf[20]; strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        timeStr = buf;
        epoch = mktime(&t);
      }
      String ip   = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "AP Mode";
      String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "Not connected";
      int rssi    = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

      String json = "{";
      json += "\"time\":\""     + timeStr             + "\",";
      json += "\"epoch\":"      + String(epoch)        + ",";
      json += "\"ip\":\""       + ip                   + "\",";
      json += "\"ssid\":\""     + jsonEscape(ssid)     + "\",";
      json += "\"rssi\":"       + String(rssi)         + ",";
      json += "\"lastFeed\":\"" + jsonEscape(lastFeed) + "\",";
      json += "\"mh\":"  + String(mHour) + ",\"mm\":" + String(mMin) + ",";
      json += "\"nh\":"  + String(nHour) + ",\"nm\":" + String(nMin) + ",";
      json += "\"r1\":"  + String(relayState[0]) + ",";
      json += "\"r2\":"  + String(relayState[1]) + ",";
      json += "\"r3\":"  + String(relayState[2]) + ",";
      json += "\"r4\":"  + String(relayState[3]) + ",";
      json += "\"pumpOnMin\":"   + String(pumpOnDuration  / 60000UL) + ",";
      json += "\"pumpOffMin\":"  + String(pumpOffDuration / 60000UL) + ",";
      json += "\"pumpEnabled\":" + String(pumpTimerEnabled ? 1 : 0);

      unsigned long remaining = 0;
      if (pumpTimerEnabled){
        unsigned long elapsed = millis() - pumpLastToggle;
        unsigned long target  = relayState[1] ? pumpOnDuration : pumpOffDuration;
        if (elapsed < target) remaining = (target - elapsed) / 1000UL;
      }
      json += ",\"pumpCountdown\":" + String(remaining);
      json += "}";
      r->send(200, "application/json", json);
    });

    asyncServer.on("/logsdata", HTTP_GET, [](AsyncWebServerRequest *r){
      String json = "{\"logs\":[";
      for (int i = 0; i < logCount; i++){
        json += "\"" + jsonEscape(systemLogs[i]) + "\"";
        if (i < logCount - 1) json += ",";
      }
      json += "]}";
      r->send(200, "application/json", json);
    });

    asyncServer.on("/setSchedule", HTTP_GET, [](AsyncWebServerRequest *r){
      if (r->hasParam("mh") && r->hasParam("mm") && r->hasParam("nh") && r->hasParam("nm")){
        int _mh = r->getParam("mh")->value().toInt();
        int _mm = r->getParam("mm")->value().toInt();
        int _nh = r->getParam("nh")->value().toInt();
        int _nm = r->getParam("nm")->value().toInt();
        // [FIX 4] Validate ranges
        if (_mh < 0 || _mh > 23 || _mm < 0 || _mm > 59 ||
            _nh < 0 || _nh > 23 || _nm < 0 || _nm > 59){
          r->send(400, "text/plain", "Invalid time values");
          return;
        }
        mHour = _mh; mMin = _mm; nHour = _nh; nMin = _nm;
        prefs.begin("sched", false);
        prefs.putInt("mh", mHour); prefs.putInt("mm", mMin);
        prefs.putInt("nh", nHour); prefs.putInt("nm", nMin);
        prefs.end();
        pushLog("Schedule updated via Web UI");
        r->send(200, "text/plain", "OK");
        return;
      }
      r->send(400, "text/plain", "Bad Request");
    });

    asyncServer.on("/setWiFi", HTTP_GET, [](AsyncWebServerRequest *r){
      if (r->hasParam("ssid")){
        String ssid = r->getParam("ssid")->value();
        String pass = r->hasParam("pass") ? r->getParam("pass")->value() : "";
        prefs.begin("wifi", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();
        pushLog("WiFi saved via Web UI, rebooting...");
        r->send(200, "text/plain", "Saved");
        pendingReboot = true; // [FIX 2]
        return;
      }
      r->send(400, "text/plain", "Bad Request");
    });

    asyncServer.on("/setPumpTimer", HTTP_GET, [](AsyncWebServerRequest *r){
      if (r->hasParam("on") && r->hasParam("off")){
        int onMin  = r->getParam("on")->value().toInt();
        int offMin = r->getParam("off")->value().toInt();
        pumpOnDuration  = (unsigned long)onMin  * 60000UL;
        pumpOffDuration = (unsigned long)offMin * 60000UL;
        if (r->hasParam("enabled"))
          pumpTimerEnabled = (r->getParam("enabled")->value().toInt() != 0);
        prefs.begin("pump", false);
        prefs.putULong("on",  pumpOnDuration);
        prefs.putULong("off", pumpOffDuration);
        prefs.putBool("en",   pumpTimerEnabled);
        prefs.end();
        pushLog("Pump timer updated via Web UI");
        r->send(200, "text/plain", "OK");
        return;
      }
      r->send(400, "text/plain", "Bad Request");
    });

    asyncServer.on("/resetWiFi", HTTP_GET, [](AsyncWebServerRequest *r){
      prefs.begin("wifi", false);
      prefs.clear();
      prefs.end();
      pushLog("WiFi credentials CLEARED from Web UI");
      r->send(200, "text/plain", "WiFi Cleared");
      pendingReboot = true; // [FIX 2]
    });

    asyncServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *r){
      pushLog("Device reboot triggered from Web UI");
      r->send(200, "text/plain", "Rebooting");
      pendingReboot = true; // [FIX 2]
    });

    dashboardHandlersRegistered = true;
  }

  asyncServer.begin();
  pushLog("Dashboard server started (async)");
}

/* -------------------- WiFi event handler -------------------- */
void WiFiEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("WiFi Connected, IP: " + WiFi.localIP().toString());
      wifiConnectedOnce = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi Lost Connection!");
      break;
    default: break;
  }
}

/* -------------------- Time sync (NTP) -------------------- */
bool syncTimeWithRetry() {
  configTime(gmtOffset_sec, daylightOffset_sec, "time.google.com", ntpServer);

  Serial.print("Syncing time");
  int retry = 0;
  const int maxRetry = 30;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo) && retry < maxRetry) {
    Serial.print(".");
    delay(500);
    retry++;
  }
  Serial.println();

  if (retry >= maxRetry) {
    pushLog("NTP time sync FAILED");
    return false;
  }

  setenv("TZ", "IST-5:30", 1);
  tzset();

  Serial.println("Time synced successfully (IST)");
  pushLog("NTP time synced");
  return true;
}

/* -------------------- Cloud init (control-only) -------------------- */
void onFilterChange()  { setRelay(2, filter); }
void onLightChange()   { setRelay(1, light); }
void onLight1Change()  { setRelay(3, light1); }
void onLight2Change()  { setRelay(4, light2); }

void initCloud(){
  ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);

  ArduinoCloud.addProperty(filter, READWRITE, ON_CHANGE, onFilterChange);
  ArduinoCloud.addProperty(light,  READWRITE, ON_CHANGE, onLightChange);
  ArduinoCloud.addProperty(light1, READWRITE, ON_CHANGE, onLight1Change);
  ArduinoCloud.addProperty(light2, READWRITE, ON_CHANGE, onLight2Change);
}

/* -------------------- Serial & Button helpers -------------------- */
void handleSerialInput(){
  while (Serial.available()){
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() == 0) continue;
    if (s.equalsIgnoreCase("ap")){
      pushLog("Serial trigger: start AP for test");
      startAP(AP_TIMEOUT_MS);
    } else {
      pushLog("Serial command: " + s);
    }
  }
}

void handleButton(){
  int state = digitalRead(BUTTON_PIN);
  if (state != lastButtonState){
    if (millis() - lastButtonChange > BUTTON_DEBOUNCE_MS){
      lastButtonChange = millis();
      lastButtonState = state;
      if (state == LOW){
        pushLog("Button pressed -> starting temporary AP for test");
        startAP(AP_TIMEOUT_MS);
      }
    }
  }
}

/* -------------------- LED helpers -------------------- */
void updateStatusLED() {
  static unsigned long ledTimer = 0;
  static bool ledState = false;
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (now - ledTimer > 800) { ledTimer = now; ledState = !ledState; digitalWrite(STATUS_LED, ledState); }
    return;
  }
  if (!cloudConnected) {
    if (now - ledTimer > 200) { ledTimer = now; ledState = !ledState; digitalWrite(STATUS_LED, ledState); }
    return;
  }
  digitalWrite(STATUS_LED, HIGH);
}

void updateActivityLED() {
  unsigned long now = millis();
  static unsigned long timer = 0;
  static bool state = false;

  if (feedingNow) {
    if (now - timer > 120) { timer = now; state = !state; digitalWrite(ACTIVITY_LED, state); }
    return;
  }
  if (apModeActive) {
    if (now - timer > 300) { timer = now; state = !state; digitalWrite(ACTIVITY_LED, state); }
    return;
  }
  if (relayState[1]) {
    if (now - timer > 700) { timer = now; state = !state; digitalWrite(ACTIVITY_LED, state); }
    return;
  }
  digitalWrite(ACTIVITY_LED, LOW);
}

/* -------------------- setup -------------------- */
void setup(){
  Serial.begin(115200); delay(10);

  pinMode(STATUS_LED, OUTPUT);
  pinMode(ACTIVITY_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(ACTIVITY_LED, LOW);

  for (int i = 0; i < 4; i++){
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH); // all OFF (active LOW)
  }
  feederServo.attach(13);
  feederServo.write(STOP);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastButtonState = digitalRead(BUTTON_PIN);

  if (!SPIFFS.begin(true)) Serial.println("SPIFFS mount failed!");
  WiFi.setSleep(false);

  // Load saved schedule
  prefs.begin("sched", true);
  mHour = prefs.getInt("mh", 10); mMin = prefs.getInt("mm", 0);
  nHour = prefs.getInt("nh", 22); nMin = prefs.getInt("nm", 0);
  prefs.end();

  // Load pump timer
  prefs.begin("pump", true);
  pumpOnDuration   = prefs.getULong("on",  3600000UL);
  pumpOffDuration  = prefs.getULong("off", 3600000UL);
  pumpTimerEnabled = prefs.getBool("en", true);
  prefs.end();

  // [FIX 6] Restore relay states from Preferences
  prefs.begin("relays", true);
  for (int i = 1; i <= 4; i++){
    bool state = prefs.getBool(("r" + String(i)).c_str(), false);
    relayState[i-1] = state;
    digitalWrite(relayPins[i-1], state ? LOW : HIGH);
  }
  prefs.end();
  pushLog("Relay states restored from memory");

  pushLog("Booting...");
  WiFi.onEvent(WiFiEvent);

  if (connectWiFi()){
    bool timeOk = syncTimeWithRetry();
    startDashboard();

    if (timeOk){
      // [FIX 7] Check for missed feedings after time is confirmed good
      checkMissedFeeding();

      initCloud();
      ArduinoCloud.begin(ArduinoIoTPreferredConnection, false);

      ArduinoCloud.addCallback(ArduinoIoTCloudEvent::CONNECT, [](){
        cloudConnected = true;
        cloudDisconnectedSince = 0; // [FIX 5] reset watchdog
        pushLog("Arduino IoT Cloud CONNECTED");
        configTime(gmtOffset_sec, daylightOffset_sec, "time.google.com", ntpServer);
        setenv("TZ", "IST-5:30", 1);
        tzset();
        pushLog("Reapplied timezone after cloud CONNECT");
      });

      ArduinoCloud.addCallback(ArduinoIoTCloudEvent::DISCONNECT, [](){
        cloudConnected = false;
        cloudDisconnectedSince = millis(); // [FIX 5] start watchdog timer
        pushLog("Arduino IoT Cloud DISCONNECTED");
      });

      ArduinoCloud.addCallback(ArduinoIoTCloudEvent::SYNC, [](){
        pushLog("Arduino IoT Cloud SYNCED");
      });

      cloudInitialized = true;
      pushLog("Cloud initialized (control-only)");

    } else pushLog("Cloud not initialized (time not synced)");

  } else {
    startAP(AP_TIMEOUT_MS);
  }

  // Pump starts OFF, timer starts now
  // Note: relay states already restored above, but pump timer resets from now
  pumpLastToggle = millis();

  pushLog("Boot sequence complete");
}

/* -------------------- loop -------------------- */
void loop(){
  // [FIX 2] Reboot flag — must be first
  if (pendingReboot) { delay(400); ESP.restart(); }

  handleSerialInput();
  handleButton();

  if (cloudInitialized) ArduinoCloud.update();

  // [FIX 5] Cloud watchdog — reboot if cloud disconnected >5min while WiFi is fine
  if (cloudInitialized && !cloudConnected &&
      cloudDisconnectedSince > 0 &&
      WiFi.status() == WL_CONNECTED &&
      millis() - cloudDisconnectedSince > CLOUD_RECONNECT_TIMEOUT) {
    pushLog("Cloud disconnected >5min with WiFi OK — rebooting to recover...");
    delay(500);
    ESP.restart();
  }

  updateFeeding();
  updatePumpTimer();

  // [FIX 3] Scheduled feeds — fires exactly once per scheduled minute
  static int lastFedMinute = -1;
  struct tm t;
  if (WiFi.status() == WL_CONNECTED && getLocalTime(&t)) {
    if (t.tm_sec == 0) {
      int nowMin = t.tm_hour * 60 + t.tm_min;
      if (nowMin != lastFedMinute) {
        if (t.tm_hour == mHour && t.tm_min == mMin) startFeeding("Schedule AM");
        if (t.tm_hour == nHour && t.tm_min == nMin) startFeeding("Schedule PM");
        lastFedMinute = nowMin;
      }
    }
  }

  // Auto reconnect / fallback
  if (WiFi.status() != WL_CONNECTED && !apModeActive && millis() - lastReconnectAttempt > 30000){
    lastReconnectAttempt = millis();
    pushLog("WiFi lost — attempting reconnect...");
    if (!connectWiFi()){
      pushLog("Reconnect failed — switching to AP mode");
      if (dashboardRunning){ asyncServer.end(); dashboardRunning = false; }
      startAP(AP_TIMEOUT_MS);
    } else {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      setenv("TZ", "IST-5:30", 1); tzset();
      if (!dashboardRunning) startDashboard();

      if (!cloudInitialized){
        bool timeOk = syncTimeWithRetry();
        if (timeOk){
          initCloud();
          ArduinoCloud.begin(ArduinoIoTPreferredConnection, false);

          ArduinoCloud.addCallback(ArduinoIoTCloudEvent::CONNECT, [](){
            cloudConnected = true;
            cloudDisconnectedSince = 0; // [FIX 5]
            pushLog("Arduino IoT Cloud CONNECTED");
            configTime(gmtOffset_sec, daylightOffset_sec, "time.google.com", ntpServer);
            setenv("TZ", "IST-5:30", 1);
            tzset();
            pushLog("Reapplied timezone after cloud CONNECT");
          });

          ArduinoCloud.addCallback(ArduinoIoTCloudEvent::DISCONNECT, [](){
            cloudConnected = false;
            cloudDisconnectedSince = millis(); // [FIX 5]
            pushLog("Arduino IoT Cloud DISCONNECTED");
          });

          ArduinoCloud.addCallback(ArduinoIoTCloudEvent::SYNC, [](){
            pushLog("Arduino IoT Cloud SYNCED");
          });

          cloudInitialized = true;
        }
      }
    }
  }

  // WiFi restored while in AP mode
  if (WiFi.status() == WL_CONNECTED && apModeActive && !apTemporary){
    pushLog("WiFi restored — stopping AP and starting dashboard");
    stopAP();
    if (!dashboardRunning){
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      setenv("TZ", "IST-5:30", 1); tzset();
      startDashboard();
    }
  }

  // AP expiry handling
  if (apModeActive && apTemporary && apExpiryMillis > 0 && millis() >= apExpiryMillis){
    pushLog("AP timeout reached -> stopping AP");
    stopAP();
    if (WiFi.status() == WL_CONNECTED && !dashboardRunning){
      startDashboard();
    }
  }

  if (apModeActive) apServer.handleClient();

  updateStatusLED();
  updateActivityLED();

  delay(2);
}
