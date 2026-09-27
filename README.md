# AuraOS 

A slick, lightweight mini operating system built for the **ESP32-C3 SuperMini Plus**. Features a custom pixel-art mascot (*Aura*), an IR transmitter/receiver toolset, a responsive OLED UI, and a built-in WebUI dashboard.

No bloated multi-file project setups—just a single, fully functional `.ino` sketch ready to compile and flash.

---

##  Features

- **Pixel Mascot (Aura):** Animated boot sequence and UI reactions on a 128x64 OLED.
- **IR Remote Suite:** Capture (Learn) raw IR signals, save them to internal flash storage, and transmit them on command.
- **LittleFS Storage:** Saves system settings and `.ir` code files persistently.
- **WebUI Dashboard:** Wirelessly manage, trigger, and clean up saved IR files straight from your browser.
- **Fallback Hotspot:** Automatically spawns its own Wi-Fi Access Point if no known network is found.
- **Non-blocking UI:** Smooth 30 FPS menu navigation using `millis()` timing—zero freezing or laggy `delay()` calls.

---

##  Hardware Pinout

| Component | Pin | Notes |
| :--- | :--- | :--- |
| **OLED SDA** | GPIO 8 | I2C Data |
| **OLED SCL** | GPIO 9 | I2C Clock |
| **UP Button** | GPIO 2 | Active LOW (`INPUT_PULLUP`) |
| **DOWN Button** | GPIO 3 | Active LOW (`INPUT_PULLUP`) |
| **OK Button** | GPIO 4 | Select / Confirm |
| **BACK Button** | GPIO 5 | Back / Cancel (Hold for Home) |
| **IR TX** | GPIO 6 | IR Transmitter LED |
| **IR RX** | GPIO 7 | IR Receiver Module |

---

##  Required Libraries

Install these via the **Arduino Library Manager**:

1. **Adafruit GFX Library**
2. **Adafruit SSD1306**
3. **IRremoteESP8266** *(Must use this specific library for ESP32/C3 compatibility)*

---

##  Quickstart

1. Open `AuraOS.ino` in Arduino IDE.
2. Select your board: `ESP32C3 Dev Module`.
3. Set **USB CDC On Boot** to `Enabled` under the Tools menu.
4. Hook up your components according to the pinout table above.
5. Compile and upload the sketch.

### First Boot Setup

1. On its first run (or if no saved Wi-Fi is reachable), AuraOS starts an Access Point:
   - **SSID:** `AuraOS_Setup`
   - **Password:** `aura1234`
2. Connect to the network using your phone or laptop.
3. Open your browser and navigate to `http://192.168.4.1` (or `http://auraos.local`).

---

## 📜 License

MIT License — Free to use, tweak, and build upon however you like.
