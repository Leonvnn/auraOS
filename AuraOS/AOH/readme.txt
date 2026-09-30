AuraOS Headless (AOH) v1.0
============================

AOH is the screenless version of AuraOS. It is based on the proven AuraOS
v1.4 IR code, but removes the OLED and physical-button interface completely.
The browser is the main AuraOS interface.

HARDWARE / PINOUT
-----------------
ESP32-C3 SuperMini

IR:
- IR TX: GPIO6

Onboard LED:
- GPIO8
- Dimmed to about 5% brightness

No OLED is required.
No buttons are required.
GPIO7 and GPIO10 are therefore free in AOH.

WI-FI / WEBUI
-------------
SSID: AuraOS_Headless
Password: aura1234
WebUI: http://192.168.4.1

AOH creates its own Wi-Fi access point. Internet access is not needed.
Connect your phone/laptop directly to AuraOS_Headless and open 192.168.4.1.

HEADLESS DASHBOARD
------------------
The AOH WebUI is its main interface and includes:
- System status and IP address
- Uptime and free memory
- LittleFS used / total / free storage
- Uploading .ir files
- List of saved remotes
- Opening a remote and seeing all commands
- Sending individual IR commands
- Deleting saved files
- Rebooting AOH from the browser

SUPPORTED IR FILES
------------------
- Flipper Zero .ir files
- Multiple commands per file
- RAW IR
- NEC / NECext / Samsung32 / RC5 / RC5X / RC6
- SIRC / SIRC15 / SIRC20 / Kaseikyo
- Legacy AuraOS IR entries

LIBRARIES
---------
Install in Arduino IDE:
- IRremoteESP8266

WiFi, WebServer and LittleFS come with the ESP32 Arduino core.
AOH does NOT require Adafruit GFX or Adafruit SSD1306.

UPLOAD
------
1. Connect your IR transmitter to GPIO6.
2. Open AuraOS_AOH_v1_0.ino in Arduino IDE.
3. Select your ESP32-C3 board and port.
4. Compile and upload.
5. Connect to the Wi-Fi network AuraOS_Headless.
6. Use password aura1234.
7. Open http://192.168.4.1.

NOTE
----
AOH intentionally has no OLED menu code. This keeps the hardware simple and
makes the WebUI the full operating interface instead of only a companion page.
