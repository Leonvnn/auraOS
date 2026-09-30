/*
  ============================================================
  AuraOS Headless (AOH) v1.0
  ESP32-C3 SuperMini Plus
  ============================================================

  Headless build based on AuraOS v1.4.

  Hardware:
  - ESP32-C3 SuperMini Plus
  - IR transmitter on GPIO 6
  - No OLED required
  - No buttons required

  Onboard LED:
  - GPIO 8
  - Dimmed to about 5% brightness

  Wi-Fi:
  - Local AP only
  - SSID: AuraOS_Headless
  - Password: aura1234
  - WebUI: http://192.168.4.1

  Library:
  - IRremoteESP8266
  ============================================================
*/

#include <Arduino.h>
#include <new>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

#define ONBOARD_LED    8
#define LED_DIM_LEVEL  242
#define IR_TX_PIN      6

#define AURA_AP_SSID   "AuraOS_Headless"
#define AURA_AP_PASS   "aura1234"
#define AOH_VERSION    "1.0"

WebServer server(80);

struct IRSignal {
  String name;
  String type;
  String protocol;
  String address;
  String command;
  String rawData;
  uint32_t frequency;
  float dutyCycle;

  IRSignal() { clear(); }

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

File uploadFile;
bool uploadAccepted = false;
String uploadPath = "";

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
// AOH WEB UI
// ============================================================

const char INDEX_HTML[] PROGMEM =
"<!doctype html>\n"
"<html><head><meta charset='utf-8'>\n"
"<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
"<meta name='theme-color' content='#07090c'>\n"
"<title>AuraOS Headless</title>\n"
"<style>\n"
"*{box-sizing:border-box}body{margin:0;background:#07090c;color:#f3fbff;font-family:Inter,system-ui,-apple-system,Segoe UI,sans-serif}"
"main{max-width:850px;margin:auto;padding:22px}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:24px}"
".brand{display:flex;align-items:center;gap:13px}.mark{width:34px;height:34px}.title{font-size:22px;font-weight:750;letter-spacing:3px}"
".sub{font-size:11px;color:#78909c;letter-spacing:1px}.pill{border:1px solid #28404a;padding:7px 10px;font:600 11px ui-monospace,monospace;color:#8ee9ff}"
".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.card{border:1px solid #172930;background:#0b1014;padding:16px}"
".wide{grid-column:1/-1}.label{font:700 11px ui-monospace,monospace;letter-spacing:1.5px;color:#748c98;margin-bottom:10px}"
".big{font-size:23px;font-weight:700}.muted{color:#78909c;font-size:12px}.statrow{display:flex;justify-content:space-between;gap:14px;margin:7px 0}"
".bar{height:8px;border:1px solid #203740;margin-top:12px}.bar>div{height:100%;background:#42dfff;width:0}"
"button{font:700 11px ui-monospace,monospace;letter-spacing:.7px;border:1px solid #2edcff;background:transparent;color:#91edff;padding:9px 12px;cursor:pointer}"
"button:hover{background:#0d252d}.danger{border-color:#4d5c63;color:#aab7bd}.row{display:flex;align-items:center;justify-content:space-between;gap:10px;border-top:1px solid #142229;padding:12px 0}"
".row:first-child{border-top:0}.name{overflow-wrap:anywhere;font:600 13px ui-monospace,monospace}.actions{white-space:nowrap}"
"input[type=file]{width:100%;color:#c8d9e0;margin:7px 0 12px}.status{min-height:20px;padding-top:12px;color:#7eeaff;font:12px ui-monospace,monospace}"
".online{display:inline-block;width:7px;height:7px;border-radius:50%;background:#63f59a;margin-right:7px;box-shadow:0 0 8px #63f59a}"
"@media(max-width:620px){main{padding:14px}.grid{grid-template-columns:1fr}.wide{grid-column:auto}.top{align-items:flex-start}.pill{margin-top:2px}.row{align-items:flex-start;flex-direction:column}.actions{width:100%;display:flex;gap:8px}.actions button{flex:1}}"
"</style></head><body><main>\n"
"<div class='top'><div class='brand'>"
"<svg class='mark' viewBox='0 0 7 7' fill='none' stroke='white' stroke-width='.7' stroke-linecap='square'><path d='M3.5 .4 L.5 6.6 M3.5 .4 L6.5 6.6 M1.5 4.4 H5.5'/></svg>"
"<div><div class='title'>AURA OS</div><div class='sub'>LOCAL IR CONTROL</div></div></div><div class='pill'>AOH v1.0</div></div>\n"
"<div class='grid'>\n"
"<section class='card'><div class='label'>SYSTEM</div><div class='big'><span class='online'></span>ONLINE</div><div class='statrow'><span class='muted'>IP</span><span id='ip'>192.168.4.1</span></div><div class='statrow'><span class='muted'>UPTIME</span><span id='uptime'>--</span></div><div class='statrow'><span class='muted'>FREE HEAP</span><span id='heap'>--</span></div></section>\n"
"<section class='card'><div class='label'>STORAGE</div><div class='big' id='storageText'>--</div><div class='muted' id='storageFree'>loading...</div><div class='bar'><div id='storageFill'></div></div></section>\n"
"<section class='card wide'><div class='label'>UPLOAD .IR</div><form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='file' accept='.ir' required><button type='submit'>UPLOAD FILE</button></form></section>\n"
"<section class='card wide'><div class='label'>IR REMOTES</div><div id='files'>loading...</div></section>\n"
"<section class='card wide' id='commandsCard' hidden><div class='label' id='commandsTitle'>COMMANDS</div><div id='commands'></div></section>\n"
"<section class='card wide'><div class='label'>SYSTEM ACTIONS</div><button class='danger' onclick='rebootDevice()'>REBOOT AOH</button><div class='status' id='status'></div></section>\n"
"</div>\n"
"<script>\n"
"const byId=id=>document.getElementById(id);\n"
"function status(t){byId('status').textContent=t;}\n"
"function fmtBytes(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(2)+' MB';}\n"
"function fmtTime(s){const d=Math.floor(s/86400);s%=86400;const h=Math.floor(s/3600);s%=3600;const m=Math.floor(s/60);return (d?d+'d ':'')+(h?h+'h ':'')+m+'m';}\n"
"function btn(label,fn,cls=''){const b=document.createElement('button');b.textContent=label;b.className=cls;b.onclick=fn;return b;}\n"
"function row(name){const r=document.createElement('div');r.className='row';const n=document.createElement('span');n.className='name';n.textContent=name;r.appendChild(n);return r;}\n"
"async function loadSystem(){try{const r=await fetch('/api/system');const d=await r.json();byId('ip').textContent=d.ip;byId('uptime').textContent=fmtTime(d.uptime);byId('heap').textContent=fmtBytes(d.heap);}catch(e){status('System info unavailable');}}\n"
"async function loadStorage(){try{const r=await fetch('/api/storage');const d=await r.json();byId('storageText').textContent=fmtBytes(d.used)+' / '+fmtBytes(d.total);byId('storageFree').textContent=fmtBytes(d.free)+' free';const p=d.total?Math.min(100,d.used/d.total*100):0;byId('storageFill').style.width=p.toFixed(1)+'%';}catch(e){byId('storageText').textContent='Unavailable';}}\n"
"async function loadFiles(){const box=byId('files');box.textContent='loading...';try{const r=await fetch('/api/files');const d=await r.json();box.textContent='';if(!d.files.length){box.textContent='No .ir files uploaded yet';return;}d.files.forEach(f=>{const rr=row(f);const a=document.createElement('span');a.className='actions';a.appendChild(btn('OPEN',()=>openFile(f)));a.appendChild(btn('DELETE',()=>deleteFile(f),'danger'));rr.appendChild(a);box.appendChild(rr);});}catch(e){box.textContent='Could not load files';}}\n"
"async function openFile(f){status('Loading commands...');try{const r=await fetch('/api/signals?name='+encodeURIComponent(f));const d=await r.json();const box=byId('commands');box.textContent='';byId('commandsTitle').textContent=f.toUpperCase();byId('commandsCard').hidden=false;if(!d.signals.length){box.textContent='No valid commands';status('');return;}d.signals.forEach((s,i)=>{const rr=row(s);const a=document.createElement('span');a.className='actions';a.appendChild(btn('SEND',()=>sendCommand(f,i)));rr.appendChild(a);box.appendChild(rr);});status('');}catch(e){status('Could not open this file');}}\n"
"async function sendCommand(f,i){status('Sending IR...');try{const r=await fetch('/send?name='+encodeURIComponent(f)+'&index='+i,{method:'POST'});status(await r.text());}catch(e){status('Send failed');}}\n"
"async function deleteFile(f){if(!confirm('Delete '+f+'?'))return;try{const r=await fetch('/delete?name='+encodeURIComponent(f),{method:'POST'});status(await r.text());byId('commandsCard').hidden=true;loadFiles();loadStorage();}catch(e){status('Delete failed');}}\n"
"async function rebootDevice(){if(!confirm('Reboot AuraOS Headless?'))return;status('Rebooting...');try{await fetch('/reboot',{method:'POST'});}catch(e){}setTimeout(()=>location.reload(),4500);}\n"
"loadSystem();loadStorage();loadFiles();setInterval(()=>{loadSystem();loadStorage();},5000);\n"
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

void handleSystem() {
  String json = "{\"version\":\"" AOH_VERSION "\",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"uptime\":" + String(millis() / 1000UL) + ",";
  json += "\"heap\":" + String((unsigned long)ESP.getFreeHeap()) + ",";
  json += "\"files\":" + String(countIRFiles()) + "}";
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

void handleReboot() {
  server.send(200, "text/plain", "Rebooting");
  delay(150);
  ESP.restart();
}

void setupWebUI() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/files", HTTP_GET, handleFileList);
  server.on("/api/storage", HTTP_GET, handleStorage);
  server.on("/api/system", HTTP_GET, handleSystem);
  server.on("/api/signals", HTTP_GET, handleSignalList);
  server.on("/send", HTTP_POST, handleWebSend);
  server.on("/delete", HTTP_POST, handleWebDelete);
  server.on("/upload", HTTP_POST, handleUploadComplete, handleFileUpload);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.begin();
}

void startAuraAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AURA_AP_SSID, AURA_AP_PASS);
  delay(100);
  Serial.print("AOH AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  delay(150);

  // Keep the onboard LED dim instead of full brightness.
  ledcAttach(ONBOARD_LED, 5000, 8);
  ledcWrite(ONBOARD_LED, LED_DIM_LEVEL);

  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);

  if (!LittleFS.begin(false)) {
    if (!LittleFS.begin(true)) {
      Serial.println("LittleFS ERROR");
      while (true) delay(1000);
    }
  }

  startAuraAP();
  setupWebUI();

  Serial.println("AuraOS Headless ready");
  Serial.println("Connect to AuraOS_Headless and open http://192.168.4.1");
}

void loop() {
  server.handleClient();
  delay(2);
}
