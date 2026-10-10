# LUCKY S3

Offline virtual-coin arcade game for ESP32-S3 N16R8 and ST7735S 1.8" TFT.

## TFT setup preserved
- Driver/library: Adafruit_ST7735 + Adafruit_GFX
- Resolution: 160x128 landscape
- SCLK: GPIO12
- MOSI/SDA: GPIO10
- DC/A0: GPIO8
- RESET: GPIO7
- CS: GPIO9
- Init: `initR(INITR_BLACKTAB)`
- Rotation: `1`

## Buttons
Buttons connect between GPIO and GND. Internal pull-ups are enabled with `INPUT_PULLUP`.
| Button | ESP32-S3 GPIO |
|---|---:|
| UP | 1 |
| DOWN | 2 |
| SELECT | 4 |
| BACK | 5 |

## Required libraries
Install through Arduino IDE Library Manager:
- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- Adafruit BusIO (normally installed as a dependency)

## Arduino IDE
- Board: ESP32S3 Dev Module
- USB CDC On Boot: Enabled (optional, useful for Serial Monitor)
- Flash Size: 16MB if available for your exact N16R8 board
- PSRAM: OPI PSRAM if this option is available for your board package
- Partition Scheme: any standard scheme with Preferences/NVS support
- Upload Speed: 460800 or lower if upload is unstable
- Port: the COM port of your ESP32-S3

The board menu labels can differ by ESP32 Arduino core version and board definition.

## Upload
1. Install the libraries above.
2. Open `LUCKY_S3.ino` in Arduino IDE.
3. Select the ESP32-S3 board and the correct COM port.
4. Upload.
5. Open Serial Monitor at 115200 baud. Expected startup lines include `LUCKY S3 STARTING`, `DISPLAY OK`, `BUTTONS OK`, `SAVE SYSTEM OK`, `GAME READY`.

## Notes
- All coins are fictional and not redeemable.
- Save data uses Preferences/NVS.
- Gameplay animations use `millis()` rather than blocking delays.
- The sketch uses a known earlier configuration for the same project. If your physical display wiring differs, do not upload until the wiring is checked.
- This is a V1 starter implementation. Brightness is stored as a setting but cannot change physical backlight brightness unless the backlight is connected to a controllable PWM pin. Sound is a setting only; no buzzer is required.
