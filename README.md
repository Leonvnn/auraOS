AuraOS v1.4
============

AuraOS is a lightweight firmware for the ESP32-C3 SuperMini with a 128x64 OLED,
button controls, IR transmission, LittleFS storage and a local Wi-Fi WebUI.

HARDWARE / PINOUT
-----------------
ESP32-C3 SuperMini

OLED SSD1306 128x64 (I2C):
- SDA: GPIO7
- SCL: GPIO10
- Address: 0x3C

Buttons:
- UP: GPIO2
- OK: GPIO4
- BACK: GPIO5

IR:
- IR TX: GPIO6

Onboard LED:
- GPIO8
- Dimmed to about 5% brightness by AuraOS

IMPORTANT: v1.4 changes the OLED wiring from v1.3.
Move the OLED SDA wire from GPIO8 to GPIO7.
Move the OLED SCL wire from GPIO9 to GPIO10.
GPIO8 is now reserved for the onboard LED.

WI-FI / WEBUI
-------------
AuraOS creates its own local access point.

SSID: AuraOS_Setup
Password: aura1234
WebUI: http://192.168.4.1

The WebUI can manage and trigger saved IR files and displays LittleFS storage usage.
No external Wi-Fi network is required.

FEATURES
--------
- Clean 128x64 OLED interface
- Smooth scrolling menu
- Cached IR command lists for smoother browsing
- IR file playback
- LittleFS file storage
- Local Wi-Fi AP
- Browser-based WebUI
- Storage used / total / free display
- Onboard LED dimming

LIBRARIES
---------
- Adafruit GFX Library
- Adafruit SSD1306
- IRremoteESP8266
- ESP32 Arduino core libraries (WiFi, WebServer, LittleFS, Wire)

UPLOAD
------
1. Rewire the OLED to SDA GPIO7 and SCL GPIO10.
2. Open AuraOS_v1_4.ino in Arduino IDE.
3. Select the correct ESP32-C3 board and port.
4. Compile and upload.
5. After boot, connect to AuraOS_Setup and open 192.168.4.1.
