/*
  ============================================================
  AuraOS
  ESP32-C3 Mini Operating System
  ============================================================

  Hardware:
  ESP32-C3 SuperMini Plus
  SSD1306 128x64 I2C OLED
  4 buttons
  IR transmitter
  IR receiver

  GPIO:
  OLED SDA  = GPIO 8
  OLED SCL  = GPIO 9
  UP        = GPIO 2
  DOWN      = GPIO 3
  OK        = GPIO 4
  BACK      = GPIO 5
  IR TX     = GPIO 6
  IR RX     = GPIO 7

  Libraries:
  - Adafruit GFX Library
  - Adafruit SSD1306
  - IRremoteESP8266

  ============================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include <LittleFS.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

// ============================================================
// HARDWARE
// ============================================================

#define OLED_SDA       8
#define OLED_SCL       9

#define BTN_UP         2
#define BTN_DOWN       3
#define BTN_OK         4
#define BTN_BACK       5

#define IR_TX_PIN      6
#define IR_RX_PIN      7

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64

#define OLED_ADDRESS   0x3C

#define DEBOUNCE_MS    45
#define LONG_PRESS_MS  700

#define HOSTNAME       "auraos"

// ============================================================
// OBJECTS
// ============================================================

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

IRsend irsend(IR_TX_PIN);

IRrecv irrecv(
  IR_RX_PIN,
  1024,
  50,
  true
);

decode_results results;

WebServer server(80);

// ============================================================
// APP STATES
// ============================================================

enum AppState {
  STATE_HOME,
  STATE_MENU,
  STATE_LEARN,
  STATE_TRANSMIT,
  STATE_FILES,
  STATE_WIFI,
  STATE_SETTINGS,
  STATE_ABOUT
};

AppState currentState = STATE_HOME;

// ============================================================
// MENU
// ============================================================

const char* menuItems[] = {
  "IR Receiver",
  "Transmit Last",
  "Files",
  "WiFi",
  "Settings",
  "About"
};

const uint8_t MENU_COUNT = 6;

uint8_t menuIndex = 0;

// ============================================================
// SETTINGS
// ============================================================

struct Settings {
  uint8_t brightness;
  bool animations;
};

Settings settings = {
  255,
  true
};

// ============================================================
// IR STORAGE
// ============================================================

String lastCapturedCode = "NONE";

uint64_t lastRawCode = 0;

uint16_t lastCodeBits = 0;

decode_type_t lastProtocol = UNKNOWN;

bool hasLastIR = false;

// ============================================================
// WIFI
// ============================================================

String wifiSSID = "";
String wifiPassword = "";

bool wifiAPMode = false;

unsigned long wifiReconnectTimer = 0;

bool mdnsStarted = false;

// ============================================================
// BUTTON SYSTEM
// ============================================================

struct Button {
  uint8_t pin;

  bool stableState;
  bool lastReading;

  bool pressed;
  bool longPressHandled;

  unsigned long lastChange;
  unsigned long pressStart;
};

Button btnUp = {
  BTN_UP,
  HIGH,
  HIGH,
  false,
  false,
  0,
  0
};

Button btnDown = {
  BTN_DOWN,
  HIGH,
  HIGH,
  false,
  false,
  0,
  0
};

Button btnOK = {
  BTN_OK,
  HIGH,
  HIGH,
  false,
  false,
  0,
  0
};

Button btnBack = {
  BTN_BACK,
  HIGH,
  HIGH,
  false,
  false,
  0,
  0
};

// ============================================================
// MASCOT
// ============================================================

const unsigned char PROGMEM auraFrame1[] = {
  0x00,0x00,
  0x07,0xE0,
  0x1F,0xF8,
  0x38,0x1C,
  0x71,0x8E,
  0x67,0xE6,
  0x6F,0xF6,
  0x60,0x06,
  0x63,0xC6,
  0x61,0x86,
  0x31,0x8C,
  0x38,0x1C,
  0x1F,0xF8,
  0x07,0xE0,
  0x00,0x00,
  0x00,0x00
};

const unsigned char PROGMEM auraFrame2[] = {
  0x00,0x00,
  0x07,0xE0,
  0x1F,0xF8,
  0x38,0x1C,
  0x71,0x8E,
  0x67,0xE6,
  0x6F,0xF6,
  0x60,0x06,
  0x61,0x86,
  0x63,0xC6,
  0x31,0x8C,
  0x38,0x1C,
  0x1F,0xF8,
  0x07,0xE0,
  0x00,0x00,
  0x00,0x00
};

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void renderCurrentScreen();

void saveSettings();

void loadSettings();

void saveWiFi();

void loadWiFi();

void startAPMode();

void connectWiFi();

void setupWebUI();

void handleFileUpload();

void handleRoot();

void handleStatus();

void handleFileList();

void handleFileDownload();

void handleFileDelete();

void handleIRSend();

void handleWiFiSave();

void handleWiFiScan();

void handleSettings();

void setupMDNS();

void processButtons();

bool updateButton(Button &button);

void drawHeader(const char* title);

void drawMascot(int x, int y, bool frame);

void drawBoot();

void drawHome();

void drawMenu();

void drawLearn();

void drawTransmit();

void drawFiles();

void drawWiFi();

void drawSettings();

void drawAbout();

void startLearnMode();

void transmitLastIR();

bool saveLastIR();

bool loadLastIR();

bool deleteLastIR();

String protocolName(decode_type_t type);

String htmlEscape(String input);

// ============================================================
// BUTTON ENGINE
// ============================================================

bool updateButton(Button &button) {

  bool reading = digitalRead(button.pin);

  unsigned long now = millis();

  if (reading != button.lastReading) {
    button.lastChange = now;
    button.lastReading = reading;
  }

  if ((now - button.lastChange) > DEBOUNCE_MS) {

    if (reading != button.stableState) {

      button.stableState = reading;

      if (button.stableState == LOW) {

        button.pressed = true;
        button.longPressHandled = false;
        button.pressStart = now;

        return true;
      }

      else {

        button.pressed = false;
      }
    }
  }

  return false;
}

// ============================================================
// LONG PRESS
// ============================================================

bool backLongPressed() {

  if (btnBack.stableState == LOW &&
      !btnBack.longPressHandled &&
      millis() - btnBack.pressStart >= LONG_PRESS_MS) {

    btnBack.longPressHandled = true;

    return true;
  }

  return false;
}

// ============================================================
// SETTINGS
// ============================================================

void loadSettings() {

  if (!LittleFS.exists("/settings.cfg")) {
    return;
  }

  File file = LittleFS.open("/settings.cfg", "r");

  if (!file) {
    return;
  }

  while (file.available()) {

    String line = file.readStringUntil('\n');

    line.trim();

    if (line.startsWith("brightness=")) {

      settings.brightness =
        line.substring(11).toInt();

    }

    else if (line.startsWith("animations=")) {

      settings.animations =
        line.substring(11).toInt() == 1;
    }
  }

  file.close();
}

// ============================================================

void saveSettings() {

  File file = LittleFS.open(
    "/settings.cfg",
    "w"
  );

  if (!file) {
    return;
  }

  file.println(
    "brightness=" +
    String(settings.brightness)
  );

  file.println(
    "animations=" +
    String(settings.animations ? 1 : 0)
  );

  file.close();
}

// ============================================================
// WIFI STORAGE
// ============================================================

void loadWiFi() {

  if (!LittleFS.exists("/wifi.cfg")) {
    return;
  }

  File file = LittleFS.open(
    "/wifi.cfg",
    "r"
  );

  if (!file) {
    return;
  }

  wifiSSID = file.readStringUntil('\n');
  wifiPassword = file.readStringUntil('\n');

  wifiSSID.trim();
  wifiPassword.trim();

  file.close();
}

// ============================================================

void saveWiFi() {

  File file = LittleFS.open(
    "/wifi.cfg",
    "w"
  );

  if (!file) {
    return;
  }

  file.println(wifiSSID);
  file.println(wifiPassword);

  file.close();
}

// ============================================================
// WIFI
// ============================================================

void setupMDNS() {

  if (mdnsStarted) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (MDNS.begin(HOSTNAME)) {

    MDNS.addService(
      "http",
      "tcp",
      80
    );

    mdnsStarted = true;
  }
}

// ============================================================

void startAPMode() {

  wifiAPMode = true;

  WiFi.mode(WIFI_AP);

  WiFi.softAP(
    "AuraOS_Setup",
    "aura1234"
  );

  mdnsStarted = false;
}

// ============================================================

void connectWiFi() {

  wifiAPMode = false;

  if (wifiSSID.length() == 0) {

    startAPMode();

    return;
  }

  WiFi.mode(WIFI_STA);

  WiFi.setHostname(HOSTNAME);

  WiFi.begin(
    wifiSSID.c_str(),
    wifiPassword.c_str()
  );

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 8000
  ) {

    delay(100);

    yield();
  }

  if (WiFi.status() != WL_CONNECTED) {

    startAPMode();

    return;
  }

  setupMDNS();
}

// ============================================================
// IR
// ============================================================

String protocolName(
  decode_type_t type
) {

  switch (type) {

    case NEC:
      return "NEC";

    case SONY:
      return "SONY";

    case RC5:
      return "RC5";

    case RC6:
      return "RC6";

    case PANASONIC:
      return "PANASONIC";

    case JVC:
      return "JVC";

    case SAMSUNG:
      return "SAMSUNG";

    case LG:
      return "LG";

    case WHYNTER:
      return "WHYNTER";

    case DENON:
      return "DENON";

    case SHARP:
      return "SHARP";

    case DISH:
      return "DISH";

    case COOLIX:
      return "COOLIX";

    case DAIKIN:
      return "DAIKIN";

    case MITSUBISHI:
      return "MITSUBISHI";

    case FUJITSU_AC:
      return "FUJITSU_AC";

    default:
      return "UNKNOWN";
  }
}

// ============================================================
// SAVE LAST IR
// ============================================================

bool saveLastIR() {

  if (!hasLastIR) {
    return false;
  }

  File file = LittleFS.open(
    "/last.ir",
    "w"
  );

  if (!file) {
    return false;
  }

  file.println(
    "PROTOCOL=" +
    String((int)lastProtocol)
  );

  file.println(
    "VALUE=" +
    String((unsigned long long)lastRawCode)
  );

  file.println(
    "BITS=" +
    String(lastCodeBits)
  );

  file.println(
    "HEX=" +
    lastCapturedCode
  );

  file.close();

  return true;
}

// ============================================================
// LOAD LAST IR
// ============================================================

bool loadLastIR() {

  if (!LittleFS.exists("/last.ir")) {
    return false;
  }

  File file = LittleFS.open(
    "/last.ir",
    "r"
  );

  if (!file) {
    return false;
  }

  while (file.available()) {

    String line =
      file.readStringUntil('\n');

    line.trim();

    if (line.startsWith("PROTOCOL=")) {

      lastProtocol =
        (decode_type_t)
        line.substring(9).toInt();
    }

    else if (line.startsWith("VALUE=")) {

      lastRawCode =
        strtoull(
          line.substring(6).c_str(),
          nullptr,
          10
        );
    }

    else if (line.startsWith("BITS=")) {

      lastCodeBits =
        line.substring(5).toInt();
    }

    else if (line.startsWith("HEX=")) {

      lastCapturedCode =
        line.substring(4);
    }
  }

  file.close();

  hasLastIR =
    lastRawCode != 0 &&
    lastCodeBits != 0 &&
    lastProtocol != UNKNOWN;

  return hasLastIR;
}

// ============================================================

bool deleteLastIR() {

  if (LittleFS.exists("/last.ir")) {
    return LittleFS.remove("/last.ir");
  }

  return false;
}

// ============================================================
// TRANSMIT
// ============================================================

void transmitLastIR() {

  if (!hasLastIR) {
    return;
  }

  if (lastProtocol == UNKNOWN) {
    return;
  }

  irsend.send(
    lastProtocol,
    lastRawCode,
    lastCodeBits
  );
}

// ============================================================
// MASCOT
// ============================================================

void drawMascot(
  int x,
  int y,
  bool frame
) {

  if (frame) {

    display.drawBitmap(
      x,
      y,
      auraFrame1,
      16,
      16,
      SSD1306_WHITE
    );

  } else {

    display.drawBitmap(
      x,
      y,
      auraFrame2,
      16,
      16,
      SSD1306_WHITE
    );
  }
}

// ============================================================
// HEADER
// ============================================================

void drawHeader(
  const char* title
) {

  display.clearDisplay();

  display.setTextSize(1);

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setCursor(0, 0);

  display.print(title);

  bool frame =
    (millis() / 400) % 2;

  drawMascot(
    110,
    0,
    frame
  );

  display.drawLine(
    0,
    17,
    127,
    17,
    SSD1306_WHITE
  );
}

// ============================================================
// HOME
// ============================================================

void drawHome() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  bool frame =
    (millis() / 350) % 2;

  drawMascot(
    56,
    4,
    frame
  );

  display.setTextSize(2);

  display.setCursor(
    29,
    24
  );

  display.print("AURA");

  display.setTextSize(1);

  display.setCursor(
    45,
    44
  );

  display.print("OS");

  display.setCursor(
    2,
    56
  );

  if (WiFi.status() == WL_CONNECTED) {

    display.print(
      WiFi.localIP()
    );

  } else if (wifiAPMode) {

    display.print(
      "Setup: 192.168.4.1"
    );

  } else {

    display.print(
      "WiFi offline"
    );
  }

  display.display();
}

// ============================================================
// MENU
// ============================================================

void drawMenu() {

  drawHeader("AuraOS");

  const int visible = 4;

  int first = 0;

  if (menuIndex >= visible) {
    first =
      menuIndex - visible + 1;
  }

  for (
    int i = 0;
    i < visible;
    i++
  ) {

    int index =
      first + i;

    if (index >= MENU_COUNT) {
      break;
    }

    int y =
      21 + i * 10;

    if (index == menuIndex) {

      display.fillRect(
        0,
        y - 1,
        128,
        10,
        SSD1306_WHITE
      );

      display.setTextColor(
        SSD1306_BLACK
      );

    } else {

      display.setTextColor(
        SSD1306_WHITE
      );
    }

    display.setCursor(
      4,
      y
    );

    display.print(
      menuItems[index]
    );
  }

  display.setTextColor(
    SSD1306_WHITE
  );

  display.display();
}

// ============================================================
// LEARN
// ============================================================

void drawLearn() {

  drawHeader("IR Receiver");

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    22
  );

  display.print(
    "Point remote..."
  );

  display.setCursor(
    0,
    34
  );

  display.print(
    "Protocol: "
  );

  if (hasLastIR) {

    display.println(
      protocolName(lastProtocol)
    );

  } else {

    display.println(
      "WAIT"
    );
  }

  display.setCursor(
    0,
    45
  );

  display.print(
    "Code: "
  );

  display.print(
    lastCapturedCode
  );

  display.setCursor(
    0,
    56
  );

  display.print(
    "[BACK] Return"
  );

  display.display();
}

// ============================================================
// TRANSMIT
// ============================================================

void drawTransmit() {

  drawHeader("IR Transmit");

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    23
  );

  if (!hasLastIR) {

    display.println(
      "No IR signal saved."
    );

    display.setCursor(
      0,
      45
    );

    display.print(
      "[BACK] Return"
    );

  } else {

    display.print(
      protocolName(lastProtocol)
    );

    display.setCursor(
      0,
      35
    );

    display.print(
      lastCapturedCode
    );

    display.setCursor(
      0,
      53
    );

    display.print(
      "[OK] Send [BACK] Exit"
    );
  }

  display.display();
}

// ============================================================
// FILES
// ============================================================

void drawFiles() {

  drawHeader("LittleFS");

  File root =
    LittleFS.open("/");

  if (!root) {

    display.setCursor(
      0,
      25
    );

    display.println(
      "Filesystem error"
    );

    display.display();

    return;
  }

  File file =
    root.openNextFile();

  int row = 0;

  while (
    file &&
    row < 3
  ) {

    display.setCursor(
      0,
      23 + row * 11
    );

    String name =
      String(file.name());

    if (name.length() > 15) {
      name =
        name.substring(
          name.length() - 15
        );
    }

    display.print(name);

    display.print(" ");

    display.print(
      file.size()
    );

    display.print("B");

    row++;

    file =
      root.openNextFile();
  }

  if (row == 0) {

    display.setCursor(
      0,
      25
    );

    display.print(
      "No files."
    );
  }

  display.setCursor(
    0,
    56
  );

  display.print(
    "[BACK] Return"
  );

  display.display();
}

// ============================================================
// WIFI SCREEN
// ============================================================

void drawWiFi() {

  drawHeader("WiFi");

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    22
  );

  if (wifiAPMode) {

    display.println(
      "Mode: SETUP AP"
    );

    display.println(
      "SSID: AuraOS_Setup"
    );

    display.println(
      "IP: 192.168.4.1"
    );

  } else if (
    WiFi.status() == WL_CONNECTED
  ) {

    display.println(
      "Connected"
    );

    display.print(
      "IP: "
    );

    display.println(
      WiFi.localIP()
    );

    display.print(
      "RSSI: "
    );

    display.print(
      WiFi.RSSI()
    );

    display.println(
      " dBm"
    );

  } else {

    display.println(
      "Disconnected"
    );
  }

  display.setCursor(
    0,
    56
  );

  display.print(
    "[BACK] Return"
  );

  display.display();
}

// ============================================================
// SETTINGS
// ============================================================

void drawSettings() {

  drawHeader("Settings");

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    24
  );

  display.print(
    "Brightness: "
  );

  display.println(
    settings.brightness
  );

  display.setCursor(
    0,
    36
  );

  display.print(
    "Animations: "
  );

  display.println(
    settings.animations
      ? "ON"
      : "OFF"
  );

  display.setCursor(
    0,
    49
  );

  display.print(
    "UP/DOWN change"
  );

  display.setCursor(
    0,
    59
  );

  display.print(
    "OK save"
  );

  display.display();
}

// ============================================================
// ABOUT
// ============================================================

void drawAbout() {

  drawHeader("About");

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    24
  );

  display.println(
    "AuraOS v1.0"
  );

  display.setCursor(
    0,
    36
  );

  display.println(
    "ESP32-C3 Edition"
  );

  display.setCursor(
    0,
    48
  );

  display.println(
    "Mini embedded OS"
  );

  display.setCursor(
    0,
    59
  );

  display.print(
    "[BACK] Return"
  );

  display.display();
}

// ============================================================
// SCREEN ROUTER
// ============================================================

void renderCurrentScreen() {

  switch (currentState) {

    case STATE_HOME:
      drawHome();
      break;

    case STATE_MENU:
      drawMenu();
      break;

    case STATE_LEARN:
      drawLearn();
      break;

    case STATE_TRANSMIT:
      drawTransmit();
      break;

    case STATE_FILES:
      drawFiles();
      break;

    case STATE_WIFI:
      drawWiFi();
      break;

    case STATE_SETTINGS:
      drawSettings();
      break;

    case STATE_ABOUT:
      drawAbout();
      break;
  }
}

// ============================================================
// BOOT ANIMATION
// ============================================================

void drawBoot() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  for (
    int x = -16;
    x <= 56;
    x += 8
  ) {

    display.clearDisplay();

    drawMascot(
      x,
      20,
      (x / 8) % 2
    );

    display.display();

    delay(45);
  }

  display.clearDisplay();

  drawMascot(
    56,
    14,
    false
  );

  display.setTextSize(2);

  display.setCursor(
    27,
    34
  );

  display.print(
    "AURA"
  );

  display.display();

  delay(500);

  display.setTextSize(1);

  display.setCursor(
    45,
    52
  );

  display.print(
    "OS"
  );

  display.display();

  delay(500);
}

// ============================================================
// WEB HTML
// ============================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>AuraOS</title>

<style>

body {
  font-family: Arial, sans-serif;
  background:#101018;
  color:#fff;
  max-width:650px;
  margin:auto;
  padding:20px;
}

h1 {
  text-align:center;
}

.card {
  background:#1b1b27;
  padding:18px;
  margin:14px 0;
  border-radius:12px;
}

button,
input {
  width:100%;
  box-sizing:border-box;
  padding:12px;
  margin-top:8px;
  border-radius:8px;
  border:0;
}

button {
  background:#ffffff;
  color:#111;
  font-weight:bold;
}

input {
  background:#292936;
  color:white;
}

.file {
  padding:10px;
  border-bottom:1px solid #333;
}

</style>
</head>

<body>

<h1>AuraOS</h1>

<div class="card">

<h2>Device</h2>

<p id="status">
Loading...
</p>

</div>

<div class="card">

<h2>IR</h2>

<button onclick="sendIR()">
Transmit Last IR
</button>

<p id="ir">
Loading...
</p>

</div>

<div class="card">

<h2>Files</h2>

<form
method="POST"
action="/upload"
enctype="multipart/form-data">

<input
type="file"
name="file"
accept=".ir">

<button type="submit">
Upload IR file
</button>

</form>

<div id="files">
Loading...
</div>

</div>

<div class="card">

<h2>WiFi</h2>

<form method="POST" action="/wifi">

<input
name="ssid"
placeholder="WiFi SSID">

<input
name="password"
type="password"
placeholder="WiFi password">

<button type="submit">
Save WiFi
</button>

</form>

</div>

<script>

async function loadStatus() {

  let r =
    await fetch('/api/status');

  let d =
    await r.json();

  document.getElementById('status').innerHTML =
    'IP: ' + d.ip +
    '<br>Mode: ' + d.mode +
    '<br>RSSI: ' + d.rssi;

  document.getElementById('ir').innerHTML =
    'Protocol: ' + d.protocol +
    '<br>Code: ' + d.code;
}

async function loadFiles() {

  let r =
    await fetch('/api/files');

  let d =
    await r.json();

  let html = '';

  d.files.forEach(
    function(f) {

      html +=
        '<div class="file">' +
        f +
        '<br><a href="/download?name=' +
        encodeURIComponent(f) +
        '">Download</a> ' +
        '<a href="/delete?name=' +
        encodeURIComponent(f) +
        '">Delete</a>' +
        '</div>';
    }
  );

  document.getElementById('files').innerHTML =
    html || 'No files';
}

async function sendIR() {

  let r =
    await fetch('/send', {
      method:'POST'
    });

  alert(
    await r.text()
  );
}

loadStatus();
loadFiles();

</script>

</body>
</html>
)rawliteral";

// ============================================================
// HTML ESCAPE
// ============================================================

String htmlEscape(
  String input
) {

  input.replace(
    "&",
    "&amp;"
  );

  input.replace(
    "<",
    "&lt;"
  );

  input.replace(
    ">",
    "&gt;"
  );

  input.replace(
    "\"",
    "&quot;"
  );

  return input;
}

// ============================================================
// WEB ROOT
// ============================================================

void handleRoot() {

  server.send_P(
    200,
    "text/html",
    INDEX_HTML
  );
}

// ============================================================
// WEB STATUS
// ============================================================

void handleStatus() {

  String ip;

  String mode;

  int rssi = 0;

  if (wifiAPMode) {

    ip = "192.168.4.1";

    mode = "AP";

  } else {

    ip =
      WiFi.localIP().toString();

    mode = "STA";

    rssi =
      WiFi.RSSI();
  }

  String json = "{";

  json +=
    "\"ip\":\"" +
    ip +
    "\",";

  json +=
    "\"mode\":\"" +
    mode +
    "\",";

  json +=
    "\"rssi\":" +
    String(rssi) +
    ",";

  json +=
    "\"protocol\":\"" +
    protocolName(lastProtocol) +
    "\",";

  json +=
    "\"code\":\"" +
    lastCapturedCode +
    "\"";

  json += "}";

  server.send(
    200,
    "application/json",
    json
  );
}

// ============================================================
// WEB FILE LIST
// ============================================================

void handleFileList() {

  String json =
    "{\"files\":[";

  File root =
    LittleFS.open("/");

  File file =
    root.openNextFile();

  bool first = true;

  while (file) {

    if (!first) {
      json += ",";
    }

    first = false;

    String name =
      String(file.name());

    json +=
      "\"" +
      name +
      "\"";

    file =
      root.openNextFile();
  }

  json += "]}";

  server.send(
    200,
    "application/json",
    json
  );
}

// ============================================================
// WEB DOWNLOAD
// ============================================================

void handleFileDownload() {

  if (!server.hasArg("name")) {

    server.send(
      400,
      "text/plain",
      "Missing filename"
    );

    return;
  }

  String path =
    server.arg("name");

  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  if (!LittleFS.exists(path)) {

    server.send(
      404,
      "text/plain",
      "File not found"
    );

    return;
  }

  File file =
    LittleFS.open(
      path,
      "r"
    );

  server.streamFile(
    file,
    "application/octet-stream"
  );

  file.close();
}

// ============================================================
// WEB DELETE
// ============================================================

void handleFileDelete() {

  if (!server.hasArg("name")) {

    server.send(
      400,
      "text/plain",
      "Missing filename"
    );

    return;
  }

  String path =
    server.arg("name");

  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  if (
    path == "/wifi.cfg" ||
    path == "/settings.cfg"
  ) {

    server.send(
      403,
      "text/plain",
      "Protected file"
    );

    return;
  }

  if (LittleFS.remove(path)) {

    server.send(
      200,
      "text/plain",
      "Deleted"
    );

  } else {

    server.send(
      404,
      "text/plain",
      "Delete failed"
    );
  }
}

// ============================================================
// WEB IR SEND
// ============================================================

void handleIRSend() {

  if (!hasLastIR) {

    server.send(
      400,
      "text/plain",
      "No IR signal saved."
    );

    return;
  }

  transmitLastIR();

  server.send(
    200,
    "text/plain",
    "IR signal transmitted."
  );
}

// ============================================================
// WEB WIFI SAVE
// ============================================================

void handleWiFiSave() {

  if (
    !server.hasArg("ssid") ||
    !server.hasArg("password")
  ) {

    server.send(
      400,
      "text/plain",
      "Missing WiFi information."
    );

    return;
  }

  wifiSSID =
    server.arg("ssid");

  wifiPassword =
    server.arg("password");

  saveWiFi();

  server.send(
    200,
    "text/plain",
    "WiFi saved. Reboot AuraOS."
  );
}

// ============================================================
// WIFI SCAN
// ============================================================

void handleWiFiScan() {

  int count =
    WiFi.scanNetworks();

  String json =
    "{\"networks\":[";

  for (
    int i = 0;
    i < count;
    i++
  ) {

    if (i > 0) {
      json += ",";
    }

    json +=
      "\"" +
      htmlEscape(
        WiFi.SSID(i)
      ) +
      "\"";
  }

  json += "]}";

  WiFi.scanDelete();

  server.send(
    200,
    "application/json",
    json
  );
}

// ============================================================
// FILE UPLOAD
// ============================================================

void handleFileUpload() {

  HTTPUpload& upload =
    server.upload();

  if (
    upload.status ==
    UPLOAD_FILE_START
  ) {

    String filename =
      upload.filename;

    if (!filename.startsWith("/")) {
      filename =
        "/" + filename;
    }

    if (!filename.endsWith(".ir")) {

      return;
    }

    File file =
      LittleFS.open(
        filename,
        "w"
      );

    if (file) {
      file.close();
    }
  }

  else if (
    upload.status ==
    UPLOAD_FILE_WRITE
  ) {

    String filename =
      upload.filename;

    if (!filename.startsWith("/")) {
      filename =
        "/" + filename;
    }

    File file =
      LittleFS.open(
        filename,
        "a"
      );

    if (file) {

      file.write(
        upload.buf,
        upload.currentSize
      );

      file.close();
    }
  }
}

// ============================================================
// WEB SETTINGS
// ============================================================

void handleSettings() {

  if (
    server.hasArg("brightness")
  ) {

    int value =
      server.arg(
        "brightness"
      ).toInt();

    settings.brightness =
      constrain(
        value,
        0,
        255
      );
  }

  if (
    server.hasArg("animations")
  ) {

    settings.animations =
      server.arg(
        "animations"
      ) == "1";
  }

  saveSettings();

  server.send(
    200,
    "text/plain",
    "Settings saved."
  );
}

// ============================================================
// WEB SERVER
// ============================================================

void setupWebUI() {

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/api/status",
    HTTP_GET,
    handleStatus
  );

  server.on(
    "/api/files",
    HTTP_GET,
    handleFileList
  );

  server.on(
    "/download",
    HTTP_GET,
    handleFileDownload
  );

  server.on(
    "/delete",
    HTTP_GET,
    handleFileDelete
  );

  server.on(
    "/send",
    HTTP_POST,
    handleIRSend
  );

  server.on(
    "/wifi",
    HTTP_POST,
    handleWiFiSave
  );

  server.on(
    "/scan",
    HTTP_GET,
    handleWiFiScan
  );

  server.on(
    "/settings",
    HTTP_POST,
    handleSettings
  );

  server.on(
    "/upload",
    HTTP_POST,
    []() {

      server.send(
        200,
        "text/plain",
        "Upload complete."
      );

    },
    handleFileUpload
  );

  server.begin();
}

// ============================================================
// LEARN MODE
// ============================================================

void startLearnMode() {

  currentState =
    STATE_LEARN;

  irrecv.enableIRIn();

  renderCurrentScreen();
}

// ============================================================
// BUTTON PROCESSING
// ============================================================

void processButtons() {

  bool up =
    updateButton(btnUp);

  bool down =
    updateButton(btnDown);

  bool ok =
    updateButton(btnOK);

  bool back =
    updateButton(btnBack);

  bool backLong =
    backLongPressed();

  // ----------------------------------------------------------
  // LONG BACK = HOME
  // ----------------------------------------------------------

  if (backLong) {

    currentState =
      STATE_HOME;

    renderCurrentScreen();

    return;
  }

  // ----------------------------------------------------------
  // BACK
  // ----------------------------------------------------------

  if (
    back &&
    currentState != STATE_HOME
  ) {

    if (
      currentState == STATE_MENU
    ) {

      currentState =
        STATE_HOME;

    } else {

      currentState =
        STATE_MENU;
    }

    renderCurrentScreen();

    return;
  }

  // ----------------------------------------------------------
  // HOME
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_HOME
  ) {

    if (ok) {

      currentState =
        STATE_MENU;

      renderCurrentScreen();
    }

    return;
  }

  // ----------------------------------------------------------
  // MENU
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_MENU
  ) {

    if (up) {

      if (menuIndex == 0) {
        menuIndex =
          MENU_COUNT - 1;
      } else {
        menuIndex--;
      }

      renderCurrentScreen();

      return;
    }

    if (down) {

      menuIndex =
        (menuIndex + 1)
        % MENU_COUNT;

      renderCurrentScreen();

      return;
    }

    if (ok) {

      switch (menuIndex) {

        case 0:
          startLearnMode();
          break;

        case 1:
          currentState =
            STATE_TRANSMIT;
          break;

        case 2:
          currentState =
            STATE_FILES;
          break;

        case 3:
          currentState =
            STATE_WIFI;
          break;

        case 4:
          currentState =
            STATE_SETTINGS;
          break;

        case 5:
          currentState =
            STATE_ABOUT;
          break;
      }

      renderCurrentScreen();

      return;
    }
  }

  // ----------------------------------------------------------
  // LEARN
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_LEARN
  ) {

    if (
      irrecv.decode(
        &results
      )
    ) {

      lastRawCode =
        results.value;

      lastCodeBits =
        results.bits;

      lastProtocol =
        results.decode_type;

      lastCapturedCode =
        resultToHexidecimal(
          &results
        );

      hasLastIR =
        lastRawCode != 0 &&
        lastCodeBits != 0 &&
        lastProtocol != UNKNOWN;

      saveLastIR();

      irrecv.resume();

      renderCurrentScreen();
    }

    return;
  }

  // ----------------------------------------------------------
  // TRANSMIT
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_TRANSMIT
  ) {

    if (ok) {

      transmitLastIR();

      display.clearDisplay();

      display.setTextColor(
        SSD1306_WHITE
      );

      display.setTextSize(2);

      display.setCursor(
        16,
        22
      );

      display.print(
        "SENT!"
      );

      display.display();

      delay(400);

      renderCurrentScreen();
    }

    return;
  }

  // ----------------------------------------------------------
  // FILES
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_FILES
  ) {

    renderCurrentScreen();

    return;
  }

  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_WIFI
  ) {

    renderCurrentScreen();

    return;
  }

  // ----------------------------------------------------------
  // SETTINGS
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_SETTINGS
  ) {

    bool changed =
      false;

    if (up) {

      if (
        settings.brightness < 245
      ) {

        settings.brightness += 10;

      } else {

        settings.brightness = 255;
      }

      changed = true;
    }

    if (down) {

      if (
        settings.brightness > 10
      ) {

        settings.brightness -= 10;

      } else {

        settings.brightness = 0;
      }

      changed = true;
    }

    if (ok) {

      settings.animations =
        !settings.animations;

      saveSettings();

      changed = true;
    }

    if (changed) {

      renderCurrentScreen();
    }

    return;
  }

  // ----------------------------------------------------------
  // ABOUT
  // ----------------------------------------------------------

  if (
    currentState ==
    STATE_ABOUT
  ) {

    renderCurrentScreen();

    return;
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(200);

  // ----------------------------------------------------------
  // BUTTONS
  // ----------------------------------------------------------

  pinMode(
    BTN_UP,
    INPUT_PULLUP
  );

  pinMode(
    BTN_DOWN,
    INPUT_PULLUP
  );

  pinMode(
    BTN_OK,
    INPUT_PULLUP
  );

  pinMode(
    BTN_BACK,
    INPUT_PULLUP
  );

  // ----------------------------------------------------------
  // I2C
  // ----------------------------------------------------------

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  // ----------------------------------------------------------
  // OLED
  // ----------------------------------------------------------

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  ) {

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.display();

  // ----------------------------------------------------------
  // LITTLEFS
  // ----------------------------------------------------------

  if (!LittleFS.begin(false)) {

    if (!LittleFS.begin(true)) {

      display.clearDisplay();

      display.setCursor(
        0,
        20
      );

      display.println(
        "LittleFS ERROR"
      );

      display.display();

      while (true) {
        delay(1000);
      }
    }
  }

  // ----------------------------------------------------------
  // SETTINGS
  // ----------------------------------------------------------

  loadSettings();

  // ----------------------------------------------------------
  // LAST IR
  // ----------------------------------------------------------

  loadLastIR();

  // ----------------------------------------------------------
  // BOOT
  // ----------------------------------------------------------

  drawBoot();

  // ----------------------------------------------------------
  // IR
  // ----------------------------------------------------------

  irsend.begin();

  irrecv.enableIRIn();

  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  loadWiFi();

  connectWiFi();

  // ----------------------------------------------------------
  // WEB UI
  // ----------------------------------------------------------

  setupWebUI();

  // ----------------------------------------------------------
  // HOME
  // ----------------------------------------------------------

  currentState =
    STATE_HOME;

  renderCurrentScreen();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

  server.handleClient();

  processButtons();

  // ----------------------------------------------------------
  // WIFI RECONNECT
  // ----------------------------------------------------------

  if (
    !wifiAPMode &&
    wifiSSID.length() > 0 &&
    WiFi.status() != WL_CONNECTED
  ) {

    if (
      millis() -
      wifiReconnectTimer >
      15000
    ) {

      wifiReconnectTimer =
        millis();

      WiFi.disconnect();

      WiFi.begin(
        wifiSSID.c_str(),
        wifiPassword.c_str()
      );
    }
  }

  // ----------------------------------------------------------
  // MDNS
  // ----------------------------------------------------------

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    setupMDNS();
  }

  // ----------------------------------------------------------
  // ANIMATED HOME
  // ----------------------------------------------------------

  static unsigned long lastAnimation =
    0;

  if (
    currentState ==
    STATE_HOME &&
    settings.animations &&
    millis() - lastAnimation > 400
  ) {

    lastAnimation =
      millis();

    renderCurrentScreen();
  }

  delay(5);
}