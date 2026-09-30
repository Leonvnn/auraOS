AuraOS

A slick, lightweight mini operating system built for the ESP32-C3 SuperMini Plus.

AuraOS features a custom pixel-art mascot called Aura, an IR transmitter, a responsive OLED interface, local IR file management, adjustable display brightness, and a built-in WebUI.

The entire operating system runs from a single .ino sketch, making it easy to compile, flash, modify, and experiment with.

---

## FEATURES

- Pixel Mascot (Aura)
  Custom pixel-art interface and boot screen on a 128x64 OLED display.

- IR Transmitter
  Send saved IR commands using the connected IR transmitter.

- IR Files
  Browse saved .ir files directly from the OLED interface.

- Send & Delete
  Select an IR file and choose to transmit or delete it.

- LittleFS Storage
  Stores .ir files and AuraOS settings after rebooting the ESP32-C3.

- Adjustable Brightness
  Change the OLED brightness directly from the Settings menu.

- Wi-Fi Access Point
  AuraOS creates its own local Wi-Fi network.

- WebUI
  Connect directly to AuraOS from a phone or computer and open the local WebUI.

- Simple OLED Interface
  Navigate the entire system using only three physical buttons:
  UP, OK, and BACK.

IMPORTANT:

AuraOS does NOT use Wi-Fi for jamming, deauthentication, Wi-Fi attacks, or similar functions.

Wi-Fi is only used to create the local AuraOS Access Point and provide access to the WebUI.

---

## CONTROLS

UP
Move to the next option.

OK
Select or confirm.

BACK
Go back.

Hold BACK
Return to Home.

The DOWN button is not used by AuraOS.

---

## HARDWARE PINOUT

OLED SDA
GPIO 8
I2C Data

OLED SCL
GPIO 9
I2C Clock

UP Button
GPIO 2
Active LOW / INPUT_PULLUP

OK Button
GPIO 4
Select / Confirm

BACK Button
GPIO 5
Back / Cancel
Hold for Home

IR TX
GPIO 6
IR Transmitter

---

## REQUIRED LIBRARIES

Install these libraries using the Arduino Library Manager:

1. Adafruit GFX Library
2. Adafruit SSD1306
3. IRremoteESP8266

IRremoteESP8266 is used for ESP32 / ESP32-C3 compatible IR transmission.

---

## QUICKSTART

1. Open AuraOS.ino in Arduino IDE.

2. Select:

   ESP32C3 Dev Module

3. Under Tools, set:

   USB CDC On Boot: Enabled

4. Connect the OLED, UP button, OK button, BACK button, and IR transmitter according to the pinout above.

5. Compile and upload AuraOS.

6. AuraOS should boot and display the main menu.

---

## WIFI ACCESS POINT

AuraOS creates its own Wi-Fi Access Point.

SSID:
AuraOS_Setup

Password:
aura1234

The ESP32-C3 does not need to connect to your home Wi-Fi network.

Instead, your phone or computer connects directly to AuraOS_Setup.

---

## WEBUI

1. Power on AuraOS.

2. Open the Wi-Fi settings on your phone or computer.

3. Connect to:

   AuraOS_Setup

4. Enter the password:

   aura1234

5. Open a browser.

6. Go to:

   http://192.168.4.1

The AuraOS WebUI should now appear.

The WebUI is hosted locally by the ESP32-C3.
An internet connection is not required.

---

## WEBUI FUNCTIONS

The WebUI can be used for:

- Viewing saved IR files
- Uploading compatible IR files
- Deleting saved IR files
- Selecting IR files
- Transmitting supported IR commands
- Managing AuraOS IR files

Wi-Fi is only used as the local connection between your device and the AuraOS WebUI.

---

## FILES

AuraOS includes a Files menu for managing saved IR files directly from the OLED.

Open:

Files

Use UP to browse through the available .ir files.

Press OK to select a file.

You can then choose:

Send
Transmit the selected IR command.

Delete
Remove the selected IR file from LittleFS.

Press BACK to return to the previous menu.

Hold BACK to return directly to Home.

---

## IR SYSTEM

AuraOS uses an IR transmitter connected to GPIO 6.

Compatible .ir files can be stored in LittleFS and selected from the Files menu.

A selected IR file can be transmitted directly from the device or managed through the WebUI.

AuraOS does not require an IR receiver.

---

## DISPLAY SETTINGS

OLED brightness can be changed from the Settings menu.

Open:

Settings > Brightness

Use UP to cycle through the available brightness levels.

Press OK to save the selected brightness.

The brightness setting is stored so it can be restored after rebooting AuraOS.

---

## DESIGN PHILOSOPHY

AuraOS is designed to stay:

- Simple
- Lightweight
- Modular
- Easy to modify
- Easy to use on small hardware

No unnecessary features.
No complicated controls.
Just Aura.

---

## LICENSE

MIT License

Free to use, modify, experiment with, and build upon.
