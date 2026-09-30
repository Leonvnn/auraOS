AuraOS Standard (AOS) v1.4
==========================

AuraOS Standard is the full hardware version of AuraOS for the ESP32-C3 SuperMini Plus.
It combines an OLED display, three-button navigation, IR transmitting, LittleFS storage,
and a local WebUI.

HARDWARE
--------
- ESP32-C3 SuperMini Plus
- SSD1306 128x64 I2C OLED
- 3 push buttons
- IR transmitter module

PINOUT
------
OLED SDA : GPIO 7
OLED SCL : GPIO 10

UP       : GPIO 2
OK       : GPIO 4
BACK     : GPIO 5

IR TX    : GPIO 6
ONBOARD LED : GPIO 8

The onboard LED is controlled by AuraOS and runs at low brightness.

CONTROLS
--------
UP:
Move through menu items.

OK:
Open or select the highlighted item.

BACK:
Return to the previous screen.

IR SYSTEM
---------
AuraOS reads compatible .ir files from LittleFS.
Saved remotes can be opened from the OLED menu and their commands can be transmitted
with the IR transmitter connected to GPIO 6.

WEBUI
-----
AuraOS creates its own local Wi-Fi access point.

SSID     : AuraOS_Setup
Password : aura1234
WebUI    : http://192.168.4.1

The WebUI can be used to manage and upload IR files and view storage information.
No internet connection is required.

STORAGE
-------
IR files and settings are stored in LittleFS.
The WebUI shows used and available flash storage.

REQUIRED ARDUINO LIBRARIES
--------------------------
- Adafruit GFX Library
- Adafruit SSD1306
- IRremoteESP8266

LittleFS and Wi-Fi support are provided by the ESP32 Arduino core.

INSTALLATION
------------
1. Open the AOS .ino file in Arduino IDE.
2. Install the required libraries.
3. Select the correct ESP32-C3 board and COM port.
4. Check the wiring against the pinout above.
5. Compile and upload.
6. AuraOS will start automatically.

AURAOS VARIANTS
---------------
AOS - AuraOS Standard
      OLED + 3 buttons + IR + WebUI

AOB - AuraOS One Button
      OLED + 1 button + IR + WebUI

AOH - AuraOS Headless
      No OLED or buttons. Controlled through the WebUI.

VERSION
-------
AuraOS Standard (AOS) v1.4
