/*
  ============================================================
  AuraOS v1.4
  ESP32-C3 SuperMini Plus
  ============================================================

  Hardware:
  - ESP32-C3 SuperMini Plus
  - SSD1306 128x64 I2C OLED
  - 3 buttons: UP / OK / BACK
  - IR transmitter on GPIO 6

  Pinout:
  OLED SDA = GPIO 8
  OLED SCL = GPIO 9
  UP       = GPIO 2
  OK       = GPIO 4
  BACK     = GPIO 5
  IR TX    = GPIO 6

  IR support:
  - Flipper Zero .ir files
  - Multiple named commands per .ir file
  - RAW signals
  - Parsed NEC, NECext, Samsung32, RC5, RC5X, RC6,
    SIRC, SIRC15, SIRC20 and Kaseikyo

  Wi-Fi:
  - Local AP only
  - SSID: AuraOS_Setup
  - Password: aura1234
  - WebUI: http://192.168.4.1

  Libraries:
  - Adafruit GFX Library
  - Adafruit SSD1306
  - IRremoteESP8266
  ============================================================
*/

#include <Arduino.h>
#include <new>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ============================================================
// HARDWARE
// ============================================================

#define OLED_SDA       7
#define OLED_SCL       10
#define ONBOARD_LED    8
#define LED_DIM_LEVEL  242  // Active-low LED: ~5% visible brightness
#define BTN_UP         2
#define BTN_OK         4
#define BTN_BACK       5
#define IR_TX_PIN      6

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_ADDRESS   0x3C

#define DEBOUNCE_MS    45
#define LONG_PRESS_MS  700

#define AURA_AP_SSID   "AuraOS_Setup"
#define AURA_AP_PASS   "aura1234"

// ============================================================
// OBJECTS
// ============================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

// ============================================================
// STATE
// ============================================================

enum AppState {
  STATE_HOME,
  STATE_MENU,
  STATE_FILES,
  STATE_FILE_ACTIONS,
  STATE_COMMANDS,
  STATE_DELETE_CONFIRM,
  STATE_WIFI,
  STATE_SETTINGS,
  STATE_ABOUT
};

AppState currentState = STATE_HOME;

const char* menuItems[] = {
  "Files",
  "WiFi",
  "Settings",
  "About"
};

const char* fileActionItems[] = {
  "Send",
  "Delete"
};

const char* deleteConfirmItems[] = {
  "Cancel",
  "Delete file"
};

const uint8_t MENU_COUNT = 4;
uint8_t menuIndex = 0;

int fileIndex = 0;
uint8_t fileActionIndex = 0;
uint8_t deleteConfirmIndex = 0;
int commandIndex = 0;
String selectedIRFile = "";

// Cached command labels for the currently opened .ir file.
// This keeps LittleFS reads out of the 60 FPS drawing loop.
String *commandNameCache = nullptr;
int commandCacheCount = 0;
String commandCacheFile = "";

// UI animation state. The selector is deliberately shared by every list screen,
// but reset whenever the screen changes.
float selectorY = 18.0f;
float selectorTargetY = 18.0f;
float listOffsetY = 0.0f;
int lastListTop = 0;
AppState animationState = STATE_HOME;
bool animationReady = false;
unsigned long lastAnimationFrame = 0;

// ============================================================
// SETTINGS
// ============================================================

struct Settings {
  uint8_t brightness;
};

Settings settings = { 192 };

const uint8_t brightnessLevels[] = {16, 32, 64, 96, 128, 160, 192, 224, 255};
const uint8_t BRIGHTNESS_LEVEL_COUNT = sizeof(brightnessLevels) / sizeof(brightnessLevels[0]);

// ============================================================
// BUTTONS
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

Button btnUp   = { BTN_UP,   HIGH, HIGH, false, false, 0, 0 };
Button btnOK   = { BTN_OK,   HIGH, HIGH, false, false, 0, 0 };
Button btnBack = { BTN_BACK, HIGH, HIGH, false, false, 0, 0 };

// ============================================================
// IR SIGNAL MODEL
// ============================================================

struct IRSignal {
  String name;
  String type;
  String protocol;
  String address;
  String command;
  String rawData;
  uint32_t frequency;
  float dutyCycle;

  IRSignal() {
    clear();
  }

  void clear() {
    name = "";
    type = "";
    protocol = "";
    address = "";
    command = "";
    rawData = "";
    frequency = 38000;
    dutyCycle = 0.33f;
  }
};

// ============================================================
// WEB UPLOAD STATE
// ============================================================

File uploadFile;
bool uploadAccepted = false;
String uploadPath = "";

// ============================================================
// MASCOT
// ============================================================

const unsigned char PROGMEM auraFrame1[] = {
  0x00,0x00, 0x07,0xE0, 0x1F,0xF8, 0x38,0x1C,
  0x71,0x8E, 0x67,0xE6, 0x6F,0xF6, 0x60,0x06,
  0x63,0xC6, 0x61,0x86, 0x31,0x8C, 0x38,0x1C,
  0x1F,0xF8, 0x07,0xE0, 0x00,0x00, 0x00,0x00
};

const unsigned char PROGMEM auraFrame2[] = {
  0x00,0x00, 0x07,0xE0, 0x1F,0xF8, 0x38,0x1C,
  0x71,0x8E, 0x67,0xE6, 0x6F,0xF6, 0x60,0x06,
  0x61,0x86, 0x63,0xC6, 0x31,0x8C, 0x38,0x1C,
  0x1F,0xF8, 0x07,0xE0, 0x00,0x00, 0x00,0x00
};

// ============================================================
// HELPERS
// ============================================================

String normalizePath(String path) {
  path.trim();
  if (!path.startsWith("/")) path = "/" + path;
  return path;
}

bool isSafeIRPath(String path) {
  path = normalizePath(path);
  if (!path.endsWith(".ir")) return false;
  if (path.indexOf("..") >= 0) return false;
  if (path.indexOf('\\') >= 0) return false;
  return true;
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\r", "");
  s.replace("\n", "\\n");
  return s;
}

String cleanUploadFilename(String name) {
  name.replace("\\", "/");
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  name.trim();
  while (name.startsWith(".")) name.remove(0, 1);
  name.replace("/", "_");
  return name;
}

String shortName(String s, uint8_t maxLen) {
  if (s.startsWith("/")) s.remove(0, 1);
  if (s.length() > maxLen) s = s.substring(0, maxLen - 2) + "..";
  return s;
}

uint64_t reverseBitsN(uint64_t value, uint8_t bits) {
  uint64_t out = 0;
  for (uint8_t i = 0; i < bits; i++) {
    out <<= 1;
    out |= (value >> i) & 1ULL;
  }
  return out;
}

uint32_t swap32Bytes(uint32_t value) {
  return ((value & 0x000000FFUL) << 24) |
         ((value & 0x0000FF00UL) << 8)  |
         ((value & 0x00FF0000UL) >> 8)  |
         ((value & 0xFF000000UL) >> 24);
}

String compactHex(String s) {
  s.replace(" ", "");
  s.replace("\t", "");
  s.trim();
  return s;
}

uint8_t firstHexByte(String s) {
  s.trim();
  int space = s.indexOf(' ');
  if (space >= 0) s = s.substring(0, space);
  if (s.length() > 2) s = s.substring(0, 2);
  return (uint8_t)strtoul(s.c_str(), nullptr, 16);
}

void applyBrightness() {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(settings.brightness);
}

void toast(const String &message, uint16_t ms = 550) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  int16_t x = 0;
  if (message.length() < 21) {
    int candidate = (128 - (int)message.length() * 6) / 2;
    x = candidate > 0 ? candidate : 0;
  }
  display.setCursor(x, 28);
  display.print(message);
  display.display();
  delay(ms);
}

// ============================================================
// FILE BROWSER
// ============================================================

int countIRFiles() {
  File root = LittleFS.open("/");
  if (!root) return 0;

  int count = 0;
  File f = root.openNextFile();
  while (f) {
    String name = String(f.name());
    if (!f.isDirectory() && name.endsWith(".ir")) count++;
    f.close();
    f = root.openNextFile();
  }
  root.close();
  return count;
}

String getIRFileByIndex(int target) {
  File root = LittleFS.open("/");
  if (!root) return "";

  int index = 0;
  String result = "";
  File f = root.openNextFile();
  while (f) {
    String name = String(f.name());
    if (!f.isDirectory() && name.endsWith(".ir")) {
      if (index == target) {
        result = normalizePath(name);
        f.close();
        break;
      }
      index++;
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
  return result;
}

int countSignalsInFile(const String &inputPath) {
  String path = normalizePath(inputPath);
  if (!isSafeIRPath(path) || !LittleFS.exists(path)) return 0;

  File f = LittleFS.open(path, "r");
  if (!f) return 0;

  int count = 0;
  bool legacyContent = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("name:")) count++;
    if (line.startsWith("PROTOCOL=") || line.startsWith("VALUE=") || line.startsWith("data:")) {
      legacyContent = true;
    }
  }

  f.close();
  if (count == 0 && legacyContent) count = 1;
  return count;
}

void clearCommandCache() {
  if (commandNameCache) {
    delete[] commandNameCache;
    commandNameCache = nullptr;
  }
  commandCacheCount = 0;
  commandCacheFile = "";
}

bool loadCommandCache(const String &inputPath) {
  clearCommandCache();

  String path = normalizePath(inputPath);
  int count = countSignalsInFile(path);
  if (count <= 0) return false;

  String *cache = new (std::nothrow) String[count];
  if (!cache) return false;

  File f = LittleFS.open(path, "r");
  if (!f) {
    delete[] cache;
    return false;
  }

  int index = 0;
  while (f.available() && index < count) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.startsWith("name:")) continue;

    String name = line.substring(5);
    name.trim();
    if (name.length() == 0) name = "Command " + String(index + 1);
    cache[index++] = shortName(name, 17);
  }
  f.close();

  // Legacy AuraOS files may contain one signal without a name: field.
  if (index == 0 && count == 1) {
    cache[0] = "Signal";
    index = 1;
  }

  // Fill any unusual missing names with a stable fallback label.
  while (index < count) {
    cache[index] = "Command " + String(index + 1);
    index++;
  }

  commandNameCache = cache;
  commandCacheCount = count;
  commandCacheFile = path;
  return true;
}

bool getSignalByIndex(const String &inputPath, int target, IRSignal &signal) {
  signal.clear();

  String path = normalizePath(inputPath);
  if (!isSafeIRPath(path) || !LittleFS.exists(path) || target < 0) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  int current = -1;
  bool targetStarted = false;
  bool sawNamedSignal = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("#") || line.startsWith("Filetype:") || line.startsWith("Version:")) {
      continue;
    }

    if (line.startsWith("name:")) {
      sawNamedSignal = true;
      current++;

      if (targetStarted && current > target) {
        f.close();
        return signal.name.length() > 0;
      }

      if (current == target) {
        signal.clear();
        signal.name = line.substring(5);
        signal.name.trim();
        targetStarted = true;
      }
      continue;
    }

    if (sawNamedSignal && current != target) continue;

    // Legacy one-signal AuraOS file support.
    if (!sawNamedSignal && target == 0) {
      targetStarted = true;
      if (signal.name.length() == 0) signal.name = "Signal";
    }

    if (!targetStarted) continue;

    if (line.startsWith("type:")) {
      signal.type = line.substring(5);
      signal.type.trim();
    } else if (line.startsWith("protocol:")) {
      signal.protocol = line.substring(9);
      signal.protocol.trim();
    } else if (line.startsWith("address:")) {
      signal.address = line.substring(8);
      signal.address.trim();
    } else if (line.startsWith("command:")) {
      signal.command = line.substring(8);
      signal.command.trim();
    } else if (line.startsWith("frequency:")) {
      signal.frequency = (uint32_t)line.substring(10).toInt();
    } else if (line.startsWith("duty_cycle:")) {
      signal.dutyCycle = line.substring(11).toFloat();
    } else if (line.startsWith("data:")) {
      signal.rawData = line.substring(5);
      signal.rawData.trim();
      if (signal.type.length() == 0) signal.type = "raw";
    } else if (line.startsWith("PROTOCOL=")) {
      signal.type = "legacy";
      signal.protocol = line.substring(9);
      signal.protocol.trim();
    } else if (line.startsWith("VALUE=")) {
      signal.command = line.substring(6);
      signal.command.trim();
    } else if (line.startsWith("BITS=")) {
      signal.address = line.substring(5); // Re-used as bits for legacy files.
      signal.address.trim();
    }
  }

  f.close();
  return targetStarted && (signal.rawData.length() > 0 || signal.type.length() > 0 || signal.protocol.length() > 0);
}

// ============================================================
// IR TRANSMISSION
// ============================================================

void finishIRSend() {
  // Keep the output low between transmissions. This prevents accidental light
  // output on modules that expose an indicator LED.
  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);
}

bool sendRawSignal(const IRSignal &signal) {
  if (signal.rawData.length() == 0) return false;

  uint32_t frequency = signal.frequency;
  if (frequency < 1000) frequency *= 1000;  // Accept 38 as well as 38000.
  if (frequency < 10000 || frequency > 60000) return false;

  // Count samples first. Flipper/Bruce RAW entries are space-separated
  // microsecond timings. IRremoteESP8266 sendRaw() expects uint16_t timings.
  uint16_t sampleCount = 0;
  bool inToken = false;
  for (size_t i = 0; i < signal.rawData.length(); i++) {
    char c = signal.rawData[i];
    bool sep = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    if (!sep && !inToken) {
      inToken = true;
      sampleCount++;
    } else if (sep) {
      inToken = false;
    }
  }

  if (sampleCount < 2 || sampleCount > 1200) return false;

  uint16_t *data = new (std::nothrow) uint16_t[sampleCount];
  if (!data) return false;

  uint16_t out = 0;
  int tokenStart = -1;
  const int len = signal.rawData.length();
  for (int i = 0; i <= len && out < sampleCount; i++) {
    bool sep = (i == len) || signal.rawData[i] == ' ' || signal.rawData[i] == '\t' ||
               signal.rawData[i] == '\r' || signal.rawData[i] == '\n';

    if (!sep && tokenStart < 0) tokenStart = i;
    if (sep && tokenStart >= 0) {
      String token = signal.rawData.substring(tokenStart, i);
      long value = token.toInt();
      if (value < 0) value = -value;
      if (value < 1) value = 1;
      if (value > 65535) value = 65535;
      data[out++] = (uint16_t)value;
      tokenStart = -1;
    }
  }

  IRsend tx(IR_TX_PIN);
  tx.begin();
  tx.sendRaw(data, out, (uint16_t)frequency);
  delete[] data;
  finishIRSend();

  Serial.printf("IR RAW sent: %u samples @ %lu Hz\n", out, (unsigned long)frequency);
  return out >= 2;
}

bool sendParsedSignal(const IRSignal &signal) {
  String protocol = signal.protocol;
  protocol.trim();
  if (protocol.length() == 0) return false;

  // NEC
  if (protocol.equalsIgnoreCase("NEC")) {
    IRsend tx(IR_TX_PIN);
    tx.begin();
    uint8_t address = firstHexByte(signal.address);
    uint8_t command = firstHexByte(signal.command);
    uint64_t data = tx.encodeNEC(address, command);
    tx.sendNEC(data, 32);
    finishIRSend();
    return true;
  }

  // NEC extended. Kept compatible with the conversion used by Bruce.
  if (protocol.equalsIgnoreCase("NECext")) {
    String a = compactHex(signal.address);
    String c = compactHex(signal.command);

    int zero = a.indexOf("00", 2);
    if (zero >= 0) a = a.substring(0, zero);
    zero = c.indexOf("00", 2);
    if (zero >= 0) c = c.substring(0, zero);

    uint16_t address = (uint16_t)strtoul(a.c_str(), nullptr, 16);
    uint16_t command = (uint16_t)strtoul(c.c_str(), nullptr, 16);
    uint16_t swappedAddress = (address >> 8) | (address << 8);
    uint16_t swappedCommand = (command >> 8) | (command << 8);
    uint16_t lsbAddress = (uint16_t)reverseBitsN(swappedAddress, 16);
    uint16_t lsbCommand = (uint16_t)reverseBitsN(swappedCommand, 16);
    uint32_t data = ((uint32_t)lsbAddress << 16) | lsbCommand;

    IRsend tx(IR_TX_PIN);
    tx.begin();
    tx.sendNEC(data, 32);
    finishIRSend();
    return true;
  }

  // Samsung32
  if (protocol.equalsIgnoreCase("Samsung32")) {
    IRsend tx(IR_TX_PIN);
    tx.begin();
    uint8_t address = firstHexByte(signal.address);
    uint8_t command = firstHexByte(signal.command);
    uint64_t data = tx.encodeSAMSUNG(address, command);
    tx.sendSAMSUNG(data, 32);
    finishIRSend();
    return true;
  }

  // Bruce uses inverted output for RC5 and RC6.
  if (protocol.equalsIgnoreCase("RC5") || protocol.equalsIgnoreCase("RC5X")) {
    IRsend tx(IR_TX_PIN, true);
    tx.begin();
    uint8_t address = firstHexByte(signal.address);
    uint8_t command = firstHexByte(signal.command);
    uint16_t data = tx.encodeRC5(address, command);
    tx.sendRC5(data, 13);
    finishIRSend();
    return true;
  }

  if (protocol.equalsIgnoreCase("RC6")) {
    IRsend tx(IR_TX_PIN, true);
    tx.begin();
    uint8_t address = firstHexByte(signal.address);
    uint8_t command = firstHexByte(signal.command);
    uint64_t data = tx.encodeRC6(address, command);
    tx.sendRC6(data, 20);
    finishIRSend();
    return true;
  }

  // Sony SIRC variants.
  if (protocol.equalsIgnoreCase("SIRC") ||
      protocol.equalsIgnoreCase("SIRC15") ||
      protocol.equalsIgnoreCase("SIRC20")) {

    uint8_t bits = 12;
    if (protocol.equalsIgnoreCase("SIRC15")) bits = 15;
    if (protocol.equalsIgnoreCase("SIRC20")) bits = 20;

    String a = compactHex(signal.address);
    String c = compactHex(signal.command);
    uint32_t addressValue = strtoul(a.c_str(), nullptr, 16);
    uint32_t commandValue = strtoul(c.c_str(), nullptr, 16);
    uint16_t swappedAddr = (uint16_t)swap32Bytes(addressValue);
    uint8_t swappedCmd = (uint8_t)swap32Bytes(commandValue);

    uint32_t data = 0;
    if (bits == 12) data = ((swappedAddr & 0x1F) << 7) | (swappedCmd & 0x7F);
    if (bits == 15) data = ((swappedAddr & 0xFF) << 7) | (swappedCmd & 0x7F);
    if (bits == 20) data = ((swappedAddr & 0x1FFF) << 7) | (swappedCmd & 0x7F);
    data = (uint32_t)reverseBitsN(data, bits);

    IRsend tx(IR_TX_PIN);
    tx.begin();
    tx.sendSony(data, bits, 2);
    finishIRSend();
    return true;
  }

  // Kaseikyo / Panasonic 48-bit, matching Bruce's Flipper conversion.
  if (protocol.equalsIgnoreCase("Kaseikyo")) {
    String a = compactHex(signal.address);
    String c = compactHex(signal.command);
    uint32_t addressValue = strtoul(a.c_str(), nullptr, 16);
    uint32_t commandValue = strtoul(c.c_str(), nullptr, 16);

    uint32_t newAddress = swap32Bytes(addressValue);
    uint16_t newCommand = (uint16_t)swap32Bytes(commandValue);
    uint8_t id = (newAddress >> 24) & 0xFF;
    uint16_t vendor = (newAddress >> 8) & 0xFFFF;
    uint8_t genre1 = (newAddress >> 4) & 0x0F;
    uint8_t genre2 = newAddress & 0x0F;
    uint16_t data = newCommand & 0x03FF;

    uint8_t bytes[6];
    bytes[0] = vendor & 0xFF;
    bytes[1] = (vendor >> 8) & 0xFF;
    uint8_t vendorParity = bytes[0] ^ bytes[1];
    vendorParity = (vendorParity & 0x0F) ^ (vendorParity >> 4);
    bytes[2] = (genre1 << 4) | (vendorParity & 0x0F);
    bytes[3] = ((data & 0x0F) << 4) | genre2;
    bytes[4] = ((id & 0x03) << 6) | ((data >> 4) & 0x3F);
    bytes[5] = bytes[2] ^ bytes[3] ^ bytes[4];

    uint64_t lsbData = 0;
    for (uint8_t i = 0; i < 6; i++) lsbData |= (uint64_t)bytes[i] << (8 * i);
    uint64_t msbData = reverseBitsN(lsbData, 48);

    IRsend tx(IR_TX_PIN);
    tx.begin();
    tx.sendPanasonic64(msbData, 48);
    finishIRSend();
    return true;
  }

  Serial.println("Unsupported Flipper parsed protocol: " + protocol);
  finishIRSend();
  return false;
}

bool sendLegacySignal(const IRSignal &signal) {
  if (signal.protocol.length() == 0 || signal.command.length() == 0 || signal.address.length() == 0) return false;

  decode_type_t type = (decode_type_t)signal.protocol.toInt();
  uint64_t value = strtoull(signal.command.c_str(), nullptr, 0);
  uint16_t bits = (uint16_t)signal.address.toInt();
  if (type == UNKNOWN || value == 0 || bits == 0) return false;

  IRsend tx(IR_TX_PIN);
  tx.begin();
  bool ok = tx.send(type, value, bits);
  finishIRSend();
  return ok;
}

bool sendIRSignal(const IRSignal &signal) {
  Serial.println("--- AuraOS IR send ---");
  Serial.println("Name: " + signal.name);
  Serial.println("Type: " + signal.type);
  Serial.println("Protocol: " + signal.protocol);

  if (signal.type.equalsIgnoreCase("raw")) return sendRawSignal(signal);
  if (signal.type.equalsIgnoreCase("parsed")) return sendParsedSignal(signal);
  if (signal.type.equalsIgnoreCase("legacy")) return sendLegacySignal(signal);

  if (signal.rawData.length() > 0) return sendRawSignal(signal);
  if (signal.protocol.length() > 0) return sendParsedSignal(signal);
  return false;
}

bool sendCommandFromFile(const String &path, int index) {
  IRSignal signal;
  if (!getSignalByIndex(path, index, signal)) return false;
  return sendIRSignal(signal);
}

// ============================================================
// SETTINGS STORAGE
// ============================================================

void loadSettings() {
  if (!LittleFS.exists("/settings.cfg")) return;
  File f = LittleFS.open("/settings.cfg", "r");
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("brightness=")) {
      int value = line.substring(11).toInt();
      settings.brightness = (uint8_t)constrain(value, 0, 255);
    }
  }
  f.close();
}

void saveSettings() {
  File f = LittleFS.open("/settings.cfg", "w");
  if (!f) return;
  f.println("brightness=" + String(settings.brightness));
  f.close();
}

void nextBrightness() {
  uint8_t bestIndex = 0;
  uint16_t bestDistance = 999;
  for (uint8_t i = 0; i < BRIGHTNESS_LEVEL_COUNT; i++) {
    uint16_t distance = abs((int)brightnessLevels[i] - (int)settings.brightness);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i;
    }
  }
  bestIndex = (bestIndex + 1) % BRIGHTNESS_LEVEL_COUNT;
  settings.brightness = brightnessLevels[bestIndex];
  applyBrightness();
}

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

  if ((now - button.lastChange) > DEBOUNCE_MS && reading != button.stableState) {
    button.stableState = reading;

    if (button.stableState == LOW) {
      button.pressed = true;
      button.longPressHandled = false;
      button.pressStart = now;
      return true;
    }

    button.pressed = false;
  }

  return false;
}

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
// DISPLAY / UI
// ============================================================

static const int UI_LIST_Y = 17;
static const int UI_ROW_H = 11;
static const int UI_VISIBLE_ROWS = 4;
static const int UI_CONTENT_RIGHT = 120;

void drawMascot(int x, int y, bool frame) {
  display.drawBitmap(x, y, frame ? auraFrame1 : auraFrame2, 16, 16, SSD1306_WHITE);
}

void resetUIAnimation() {
  animationReady = false;
  listOffsetY = 0.0f;
  animationState = currentState;
}

int listTopFor(int selected, int count) {
  if (count <= UI_VISIBLE_ROWS) return 0;
  int top = selected - (UI_VISIBLE_ROWS - 1);
  if (top < 0) top = 0;
  int maxTop = count - UI_VISIBLE_ROWS;
  if (top > maxTop) top = maxTop;
  return top;
}

void prepareListAnimation(int selected, int count) {
  int top = listTopFor(selected, count);
  int row = selected - top;
  float target = (float)(UI_LIST_Y + row * UI_ROW_H);

  if (!animationReady || animationState != currentState) {
    selectorY = target;
    selectorTargetY = target;
    listOffsetY = 0.0f;
    lastListTop = top;
    animationState = currentState;
    animationReady = true;
    return;
  }

  if (top != lastListTop) {
    int delta = top - lastListTop;
    listOffsetY += (float)(delta * UI_ROW_H);
    lastListTop = top;
  }
  selectorTargetY = target;
}

bool tickUIAnimation() {
  if (!animationReady) return false;
  bool changed = false;

  float dy = selectorTargetY - selectorY;
  if (dy > 0.35f || dy < -0.35f) {
    selectorY += dy * 0.42f;
    changed = true;
  } else if (selectorY != selectorTargetY) {
    selectorY = selectorTargetY;
    changed = true;
  }

  if (listOffsetY > 0.35f || listOffsetY < -0.35f) {
    listOffsetY *= 0.56f;
    changed = true;
  } else if (listOffsetY != 0.0f) {
    listOffsetY = 0.0f;
    changed = true;
  }

  return changed;
}

void drawPixelLogo(int x, int y) {
  // Small angular A mark for the clean AuraOS chrome.
  display.drawLine(x + 3, y, x, y + 6, SSD1306_WHITE);
  display.drawLine(x + 3, y, x + 6, y + 6, SSD1306_WHITE);
  display.drawLine(x + 1, y + 4, x + 5, y + 4, SSD1306_WHITE);
}

void drawHeader(const String &title) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  drawPixelLogo(2, 2);

  String cleanTitle = title;
  cleanTitle.toUpperCase();
  int16_t x = (128 - (int)cleanTitle.length() * 6) / 2;
  if (x < 12) x = 12;
  display.setCursor(x, 2);
  display.print(cleanTitle);
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
}

void drawDownArrow(int x, int y) {
  display.drawLine(x, y, x + 4, y, SSD1306_WHITE);
  display.drawLine(x + 1, y + 1, x + 3, y + 1, SSD1306_WHITE);
  display.drawPixel(x + 2, y + 2, SSD1306_WHITE);
}

void drawUpArrow(int x, int y) {
  display.drawPixel(x + 2, y, SSD1306_WHITE);
  display.drawLine(x + 1, y + 1, x + 3, y + 1, SSD1306_WHITE);
  display.drawLine(x, y + 2, x + 4, y + 2, SSD1306_WHITE);
}

void drawScrollBar(int top, int count, int selected) {
  if (count <= 1) return;

  const int trackX = 125;
  const int trackY = 18;
  const int trackH = 38;
  display.drawLine(trackX, trackY, trackX, trackY + trackH, SSD1306_WHITE);

  int visible = min(count, UI_VISIBLE_ROWS);
  int thumbH = max(4, (trackH * visible) / count);
  int maxTravel = trackH - thumbH;
  int thumbY = trackY;
  if (count > visible) {
    int maxTop = count - visible;
    thumbY += (maxTravel * top) / maxTop;
  } else {
    thumbY += (maxTravel * selected) / max(1, count - 1);
  }
  display.fillRect(trackX - 1, thumbY, 3, thumbH, SSD1306_WHITE);

  if (top > 0 || selected > 0) drawUpArrow(122, 14);
  if ((top + visible) < count || selected < count - 1) drawDownArrow(122, 59);
}

void drawSelectorBox(const String &label) {
  int textW = min((int)label.length() * 6, 102);
  int boxW = textW + 12;
  if (boxW < 42) boxW = 42;
  if (boxW > 112) boxW = 112;
  int x = (UI_CONTENT_RIGHT - boxW) / 2;
  int y = (int)(selectorY + 0.5f) - 1;

  display.drawRect(x, y, boxW, 10, SSD1306_WHITE);
  // Cut the four corner pixels for a subtle 8-bit chamfer.
  display.drawPixel(x, y, SSD1306_BLACK);
  display.drawPixel(x + boxW - 1, y, SSD1306_BLACK);
  display.drawPixel(x, y + 9, SSD1306_BLACK);
  display.drawPixel(x + boxW - 1, y + 9, SSD1306_BLACK);
}

String mainMenuLabel(int index) {
  if (index < 0 || index >= MENU_COUNT) return "";
  return String(menuItems[index]);
}

String fileLabel(int index) {
  return shortName(getIRFileByIndex(index), 17);
}

String commandLabel(int index) {
  if (commandCacheFile == normalizePath(selectedIRFile) &&
      commandNameCache &&
      index >= 0 && index < commandCacheCount) {
    return commandNameCache[index];
  }
  return "Command " + String(index + 1);
}

String fileActionLabel(int index) {
  if (index < 0 || index > 1) return "";
  return String(fileActionItems[index]);
}

String deleteConfirmLabel(int index) {
  if (index < 0 || index > 1) return "";
  return String(deleteConfirmItems[index]);
}

// Menu label sources use plain uint8_t values on purpose.
// Arduino auto-generates function prototypes for .ino sketches. A custom
// function-pointer typedef in a function parameter can be moved behind those
// generated prototypes and cause: 'MenuLabelProvider has not been declared'.
// Keeping this API primitive makes the sketch resistant to that preprocessor.
#define MENU_SRC_MAIN           0
#define MENU_SRC_FILES          1
#define MENU_SRC_COMMANDS       2
#define MENU_SRC_FILE_ACTIONS   3
#define MENU_SRC_DELETE_CONFIRM 4

String menuLabel(uint8_t source, int index) {
  switch (source) {
    case MENU_SRC_MAIN:           return mainMenuLabel(index);
    case MENU_SRC_FILES:          return fileLabel(index);
    case MENU_SRC_COMMANDS:       return commandLabel(index);
    case MENU_SRC_FILE_ACTIONS:   return fileActionLabel(index);
    case MENU_SRC_DELETE_CONFIRM: return deleteConfirmLabel(index);
    default:                      return "";
  }
}

void drawScrollableMenu(const String &title, int count, int selected, uint8_t source) {
  drawHeader(title);

  if (count <= 0) {
    display.setCursor(20, 29);
    display.print("NOTHING HERE");
    display.display();
    return;
  }

  selected = constrain(selected, 0, count - 1);

  int top = listTopFor(selected, count);
  prepareListAnimation(selected, count);

  int visible = min(count - top, UI_VISIBLE_ROWS);
  for (int row = 0; row < visible; row++) {
    int index = top + row;
    String label = menuLabel(source, index);
    int y = UI_LIST_Y + row * UI_ROW_H + (int)(listOffsetY + 0.5f);
    int textW = min((int)label.length() * 6, 102);
    int x = (UI_CONTENT_RIGHT - textW) / 2;
    if (x < 2) x = 2;
    display.setCursor(x, y);
    display.print(label);
  }

  drawSelectorBox(menuLabel(source, selected));
  drawScrollBar(top, count, selected);
  display.display();
}

void drawHome() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  drawMascot(56, 3, (millis() / 550) % 2);
  display.drawLine(31, 21, 97, 21, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(23, 27);
  display.print("AURA OS");

  display.setTextSize(1);
  display.setCursor(39, 49);
  display.print("OK TO OPEN");
  display.drawLine(42, 59, 85, 59, SSD1306_WHITE);
  display.display();
}

void drawMenu() {
  drawScrollableMenu("Aura OS", MENU_COUNT, menuIndex, MENU_SRC_MAIN);
}

void drawFiles() {
  int count = countIRFiles();
  if (count <= 0) {
    drawHeader("Files");
    display.setCursor(22, 27);
    display.print("NO .IR FILES");
    display.setCursor(13, 43);
    display.print("UPLOAD VIA WEBUI");
    display.display();
    return;
  }

  if (fileIndex >= count) fileIndex = count - 1;
  if (fileIndex < 0) fileIndex = 0;
  drawScrollableMenu("Files", count, fileIndex, MENU_SRC_FILES);
}

void drawFileActions() {
  drawScrollableMenu(shortName(selectedIRFile, 16), 2, fileActionIndex, MENU_SRC_FILE_ACTIONS);
}

void drawCommands() {
  int count = (commandCacheFile == normalizePath(selectedIRFile)) ? commandCacheCount : 0;
  if (count <= 0) {
    drawHeader("Commands");
    display.setCursor(18, 29);
    display.print("INVALID .IR FILE");
    display.display();
    return;
  }

  if (commandIndex >= count) commandIndex = count - 1;
  if (commandIndex < 0) commandIndex = 0;
  drawScrollableMenu("Commands", count, commandIndex, MENU_SRC_COMMANDS);
}

void drawDeleteConfirm() {
  drawScrollableMenu("Delete?", 2, deleteConfirmIndex, MENU_SRC_DELETE_CONFIRM);
}

void drawWiFi() {
  drawHeader("WiFi AP");
  String apIP = WiFi.softAPIP().toString();

  display.setCursor(4, 18);
  display.print("SSID: ");
  display.print(AURA_AP_SSID);

  display.setCursor(4, 30);
  display.print("IP: ");
  display.print(apIP);

  display.setCursor(4, 42);
  display.print("PASS: ");
  display.print(AURA_AP_PASS);

  display.setCursor(4, 54);
  display.print("WEBUI READY");
  display.display();
}

void drawSettings() {
  drawHeader("Settings");

  int percent = map(settings.brightness, 0, 255, 0, 100);
  display.setCursor(28, 20);
  display.print("BRIGHTNESS ");
  display.print(percent);
  display.print("%");

  display.drawRect(13, 34, 102, 10, SSD1306_WHITE);
  int fill = map(settings.brightness, 0, 255, 0, 98);
  if (fill > 0) display.fillRect(15, 36, fill, 6, SSD1306_WHITE);

  display.setCursor(8, 53);
  display.print("UP CHANGE   OK SAVE");
  display.display();
}

void drawAbout() {
  drawHeader("About");
  display.setCursor(6, 20);
  display.print("AURA OS 1.3");
  display.setCursor(6, 31);
  display.print("ESP32-C3 / 128x64");
  display.setCursor(6, 42);
  display.print("IR TX + LITTLEFS");
  display.setCursor(6, 53);
  display.print("UP  OK  BACK");
  display.display();
}

void renderCurrentScreen() {
  switch (currentState) {
    case STATE_HOME:           drawHome(); break;
    case STATE_MENU:           drawMenu(); break;
    case STATE_FILES:          drawFiles(); break;
    case STATE_FILE_ACTIONS:   drawFileActions(); break;
    case STATE_COMMANDS:       drawCommands(); break;
    case STATE_DELETE_CONFIRM: drawDeleteConfirm(); break;
    case STATE_WIFI:           drawWiFi(); break;
    case STATE_SETTINGS:       drawSettings(); break;
    case STATE_ABOUT:          drawAbout(); break;
  }
}

void setState(AppState next) {
  currentState = next;
  resetUIAnimation();
  renderCurrentScreen();
}

void drawBoot() {
  // Original AuraOS v1 boot animation. Kept intentionally simple and clean.
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  for (int x = -16; x <= 56; x += 8) {
    display.clearDisplay();
    drawMascot(x, 20, (x / 8) % 2);
    display.display();
    delay(45);
  }

  display.clearDisplay();
  drawMascot(56, 14, false);

  display.setTextSize(2);
  display.setCursor(27, 34);
  display.print("AURA");
  display.display();
  delay(500);

  display.setTextSize(1);
  display.setCursor(45, 52);
  display.print("OS");
  display.display();
  delay(500);
}

// ============================================================
// BUTTON PROCESSING
// ============================================================

void advanceIndex(int &index, int count) {
  if (count <= 0) return;
  int next = (index + 1) % count;
  if (next == 0) resetUIAnimation();
  index = next;
}

void advanceIndex8(uint8_t &index, uint8_t count) {
  if (count == 0) return;
  uint8_t next = (uint8_t)((index + 1) % count);
  if (next == 0) resetUIAnimation();
  index = next;
}

void processButtons() {
  bool up = updateButton(btnUp);
  bool ok = updateButton(btnOK);
  bool back = updateButton(btnBack);
  bool backLong = backLongPressed();

  if (backLong) {
    setState(STATE_HOME);
    return;
  }

  if (back && currentState != STATE_HOME) {
    if (currentState == STATE_MENU) setState(STATE_HOME);
    else if (currentState == STATE_FILE_ACTIONS) setState(STATE_FILES);
    else if (currentState == STATE_COMMANDS) setState(STATE_FILE_ACTIONS);
    else if (currentState == STATE_DELETE_CONFIRM) setState(STATE_FILE_ACTIONS);
    else setState(STATE_MENU);
    return;
  }

  if (currentState == STATE_HOME) {
    if (ok) setState(STATE_MENU);
    return;
  }

  if (currentState == STATE_MENU) {
    if (up) {
      advanceIndex8(menuIndex, MENU_COUNT);
      renderCurrentScreen();
      return;
    }
    if (ok) {
      if (menuIndex == 0) setState(STATE_FILES);
      else if (menuIndex == 1) setState(STATE_WIFI);
      else if (menuIndex == 2) setState(STATE_SETTINGS);
      else setState(STATE_ABOUT);
    }
    return;
  }

  if (currentState == STATE_FILES) {
    int count = countIRFiles();
    if (up && count > 0) {
      advanceIndex(fileIndex, count);
      renderCurrentScreen();
      return;
    }
    if (ok && count > 0) {
      clearCommandCache();
      selectedIRFile = getIRFileByIndex(fileIndex);
      fileActionIndex = 0;
      setState(STATE_FILE_ACTIONS);
    }
    return;
  }

  if (currentState == STATE_FILE_ACTIONS) {
    if (up) {
      advanceIndex8(fileActionIndex, 2);
      renderCurrentScreen();
      return;
    }

    if (ok) {
      if (fileActionIndex == 0) {
        int count = countSignalsInFile(selectedIRFile);
        if (count <= 0) {
          toast("INVALID IR FILE");
          renderCurrentScreen();
        } else if (count == 1) {
          bool sent = sendCommandFromFile(selectedIRFile, 0);
          toast(sent ? "IR SENT" : "IR ERROR");
          renderCurrentScreen();
        } else {
          commandIndex = 0;
          if (loadCommandCache(selectedIRFile)) {
            setState(STATE_COMMANDS);
          } else {
            toast("CACHE ERROR");
            renderCurrentScreen();
          }
        }
      } else {
        deleteConfirmIndex = 0;
        setState(STATE_DELETE_CONFIRM);
      }
    }
    return;
  }

  if (currentState == STATE_COMMANDS) {
    int count = (commandCacheFile == normalizePath(selectedIRFile)) ? commandCacheCount : 0;
    if (up && count > 0) {
      advanceIndex(commandIndex, count);
      renderCurrentScreen();
      return;
    }
    if (ok && count > 0) {
      bool sent = sendCommandFromFile(selectedIRFile, commandIndex);
      toast(sent ? "IR SENT" : "IR ERROR");
      renderCurrentScreen();
    }
    return;
  }

  if (currentState == STATE_DELETE_CONFIRM) {
    if (up) {
      advanceIndex8(deleteConfirmIndex, 2);
      renderCurrentScreen();
      return;
    }
    if (ok) {
      if (deleteConfirmIndex == 0) {
        setState(STATE_FILE_ACTIONS);
      } else {
        bool removed = isSafeIRPath(selectedIRFile) && LittleFS.remove(selectedIRFile);
        if (removed) clearCommandCache();
        int count = countIRFiles();
        if (count <= 0) fileIndex = 0;
        else if (fileIndex >= count) fileIndex = count - 1;
        toast(removed ? "FILE DELETED" : "DELETE ERROR");
        setState(STATE_FILES);
      }
    }
    return;
  }

  if (currentState == STATE_SETTINGS) {
    if (up) {
      nextBrightness();
      renderCurrentScreen();
      return;
    }
    if (ok) {
      saveSettings();
      toast("SAVED", 350);
      renderCurrentScreen();
    }
    return;
  }
}

// ============================================================
// WEB UI
// ============================================================

// Deliberately written as normal C string literals instead of a C++ raw string.
// This avoids Arduino's sketch preprocessor mistaking JavaScript `function`
// declarations for C++ code (the compile error from the previous version).
const char INDEX_HTML[] PROGMEM =
"<!doctype html>\n"
"<html><head><meta charset='utf-8'>\n"
"<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
"<title>AuraOS</title>\n"
"<style>\n"
"*{box-sizing:border-box}body{margin:0;background:#070a0d;color:#eefaff;font-family:ui-monospace,Consolas,monospace}"
"main{max-width:720px;margin:auto;padding:24px}h1{font-size:28px;letter-spacing:4px;margin:0 0 4px}"
".sub{color:#6f8795;font-size:12px;margin-bottom:22px}.card{border:1px solid #18313b;background:#0b1116;padding:15px;margin:12px 0}"
".row{display:flex;align-items:center;justify-content:space-between;gap:10px;border-top:1px solid #14242c;padding:11px 0}"
"button{font:inherit;border:1px solid #28d7ff;background:transparent;color:#7eeaff;padding:7px 11px;cursor:pointer}"
"button:hover{background:#0c2630}.danger{border-color:#53606a;color:#b9c2c8}.name{overflow-wrap:anywhere}"
"input[type=file]{width:100%;color:#cce7f0;margin:8px 0 12px}.status{min-height:18px;color:#7eeaff;font-size:12px}.storageLine{display:flex;justify-content:space-between;gap:12px;margin-top:10px;color:#cce7f0}.bar{height:8px;border:1px solid #18313b;margin-top:10px}.bar>div{height:100%;background:#28d7ff;width:0}"
"</style></head><body><main>\n"
"<h1>AURA OS</h1><div class='sub'>local control / 192.168.4.1</div>\n"
"<div class='card'><b>STORAGE</b><div class='storageLine'><span id='storageText'>loading...</span><span id='storageFree'></span></div><div class='bar'><div id='storageFill'></div></div></div>\n"
"<div class='card'><b>UPLOAD .IR</b><form method='POST' action='/upload' enctype='multipart/form-data'>"
"<input type='file' name='file' accept='.ir' required><br><button type='submit'>UPLOAD</button></form></div>\n"
"<div class='card'><b>FILES</b><div id='files'></div></div>\n"
"<div class='card' id='commandsCard' hidden><b id='commandsTitle'>COMMANDS</b><div id='commands'></div></div>\n"
"<div class='status' id='status'></div>\n"
"<script>\n"
"const byId=id=>document.getElementById(id);\n"
"function setStatus(t){byId('status').textContent=t;}\n"
"function mkButton(label,fn,cls){const b=document.createElement('button');b.textContent=label;if(cls)b.className=cls;b.onclick=fn;return b;}\n"
"function mkRow(name){const r=document.createElement('div');r.className='row';const n=document.createElement('span');n.className='name';n.textContent=name;r.appendChild(n);return r;}\n"
"function fmtBytes(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(2)+' MB';}\n"
"async function loadStorage(){try{const r=await fetch('/api/storage');const d=await r.json();byId('storageText').textContent=fmtBytes(d.used)+' / '+fmtBytes(d.total);byId('storageFree').textContent=fmtBytes(d.free)+' free';const pct=d.total?Math.min(100,(d.used/d.total)*100):0;byId('storageFill').style.width=pct.toFixed(1)+'%';}catch(e){byId('storageText').textContent='Storage unavailable';}}\n"
"async function loadFiles(){const box=byId('files');box.textContent='loading...';try{const r=await fetch('/api/files');const d=await r.json();box.textContent='';if(!d.files.length){box.textContent='No .ir files';return;}d.files.forEach(f=>{const row=mkRow(f);const a=document.createElement('span');a.appendChild(mkButton('OPEN',()=>openFile(f)));a.appendChild(document.createTextNode(' '));a.appendChild(mkButton('DELETE',()=>delFile(f),'danger'));row.appendChild(a);box.appendChild(row);});}catch(e){box.textContent='Could not load files';}}\n"
"async function openFile(f){setStatus('loading commands...');const r=await fetch('/api/signals?name='+encodeURIComponent(f));const d=await r.json();const box=byId('commands');box.textContent='';byId('commandsTitle').textContent=f.toUpperCase();byId('commandsCard').hidden=false;if(!d.signals.length){box.textContent='No valid commands';setStatus('');return;}d.signals.forEach((s,i)=>{const row=mkRow(s);row.appendChild(mkButton('SEND',()=>sendCmd(f,i)));box.appendChild(row);});setStatus('');}\n"
"async function sendCmd(f,i){setStatus('sending...');const r=await fetch('/send?name='+encodeURIComponent(f)+'&index='+i,{method:'POST'});setStatus(await r.text());}\n"
"async function delFile(f){if(!confirm('Delete '+f+'?'))return;const r=await fetch('/delete?name='+encodeURIComponent(f),{method:'POST'});setStatus(await r.text());byId('commandsCard').hidden=true;loadFiles();loadStorage();}\n"
"loadFiles();loadStorage();\n"
"</script></main></body></html>\n";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleFileList() {
  String json = "{\"files\":[";
  bool first = true;

  File root = LittleFS.open("/");
  if (root) {
    File f = root.openNextFile();
    while (f) {
      String name = String(f.name());
      if (!f.isDirectory() && name.endsWith(".ir")) {
        if (!first) json += ",";
        first = false;
        if (name.startsWith("/")) name.remove(0, 1);
        json += "\"" + jsonEscape(name) + "\"";
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
  }

  json += "]}";
  server.send(200, "application/json", json);
}

void handleStorage() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  size_t freeBytes = (used <= total) ? (total - used) : 0;

  String json = "{\"used\":" + String((unsigned long)used) +
                ",\"total\":" + String((unsigned long)total) +
                ",\"free\":" + String((unsigned long)freeBytes) + "}";
  server.send(200, "application/json", json);
}

void handleSignalList() {
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"signals\":[]}");
    return;
  }

  String path = normalizePath(server.arg("name"));
  if (!isSafeIRPath(path) || !LittleFS.exists(path)) {
    server.send(404, "application/json", "{\"signals\":[]}");
    return;
  }

  int count = countSignalsInFile(path);
  String json = "{\"signals\":[";
  bool first = true;

  for (int i = 0; i < count; i++) {
    IRSignal signal;
    if (!getSignalByIndex(path, i, signal)) continue;
    if (!first) json += ",";
    first = false;
    String name = signal.name.length() ? signal.name : ("Command " + String(i + 1));
    json += "\"" + jsonEscape(name) + "\"";
  }

  json += "]}";
  server.send(200, "application/json", json);
}

void handleWebSend() {
  if (!server.hasArg("name") || !server.hasArg("index")) {
    server.send(400, "text/plain", "Missing file or command index");
    return;
  }

  String path = normalizePath(server.arg("name"));
  int index = server.arg("index").toInt();

  if (!isSafeIRPath(path) || !LittleFS.exists(path)) {
    server.send(404, "text/plain", "IR file not found");
    return;
  }

  bool sent = sendCommandFromFile(path, index);
  server.send(sent ? 200 : 400, "text/plain", sent ? "IR command sent" : "Could not send this IR command");
}

void handleWebDelete() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing filename");
    return;
  }

  String path = normalizePath(server.arg("name"));
  if (!isSafeIRPath(path)) {
    server.send(400, "text/plain", "Invalid filename");
    return;
  }

  bool removed = LittleFS.remove(path);
  if (removed && path == commandCacheFile) clearCommandCache();
  server.send(removed ? 200 : 404, "text/plain", removed ? "Deleted" : "Delete failed");
}

void handleFileUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (uploadFile) uploadFile.close();
    uploadAccepted = false;

    String filename = cleanUploadFilename(upload.filename);
    uploadPath = normalizePath(filename);
    if (!isSafeIRPath(uploadPath)) return;

    uploadFile = LittleFS.open(uploadPath, "w");
    uploadAccepted = (bool)uploadFile;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadAccepted && uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (uploadPath.length() && LittleFS.exists(uploadPath)) LittleFS.remove(uploadPath);
    uploadAccepted = false;
  }
}

void handleUploadComplete() {
  if (uploadAccepted) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "Uploaded");
  } else {
    server.send(400, "text/plain", "Upload failed. Only .ir files are accepted.");
  }
  uploadAccepted = false;
}

void setupWebUI() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/files", HTTP_GET, handleFileList);
  server.on("/api/storage", HTTP_GET, handleStorage);
  server.on("/api/signals", HTTP_GET, handleSignalList);
  server.on("/send", HTTP_POST, handleWebSend);
  server.on("/delete", HTTP_POST, handleWebDelete);
  server.on("/upload", HTTP_POST, handleUploadComplete, handleFileUpload);
  server.begin();
}

// ============================================================
// WIFI AP
// ============================================================

void startAuraAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AURA_AP_SSID, AURA_AP_PASS);
  delay(100);
  Serial.print("AuraOS AP IP: ");
  Serial.println(WiFi.softAPIP());
}

// ============================================================
// SETUP / LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(150);

  // GPIO8 is the onboard LED. It used to share SDA in v1.3.
  // v1.4 moves the OLED to GPIO7/10 so the LED can be controlled separately.
  ledcAttach(ONBOARD_LED, 5000, 8);
  ledcWrite(ONBOARD_LED, LED_DIM_LEVEL);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  // Keep the IR output explicitly idle until a send starts.
  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  if (!LittleFS.begin(false)) {
    if (!LittleFS.begin(true)) {
      display.setCursor(0, 20);
      display.println("LittleFS ERROR");
      display.display();
      while (true) delay(1000);
    }
  }

  loadSettings();
  applyBrightness();

  startAuraAP();
  setupWebUI();

  drawBoot();
  currentState = STATE_HOME;
  resetUIAnimation();
  renderCurrentScreen();
}

void loop() {
  server.handleClient();
  processButtons();

  unsigned long now = millis();
  if (now - lastAnimationFrame >= 16) {
    lastAnimationFrame = now;
    if (tickUIAnimation()) renderCurrentScreen();
  }

  static unsigned long lastHomeRefresh = 0;
  if (currentState == STATE_HOME && now - lastHomeRefresh > 550) {
    lastHomeRefresh = now;
    renderCurrentScreen();
  }

  delay(2);
}
