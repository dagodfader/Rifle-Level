# Rifle Level

Rifle-mounted electronic level using a Seeed Studio XIAO nRF52840 Sense.

The device uses the onboard IMU to detect rifle cant/roll angle and drives three external LEDs for left / level / right indication. Bluetooth is used for app data, setup, diagnostics, pitch, cosine, stability, and battery voltage.

## Current Firmware

Current version: **v0.2.4**

Arduino sketch:

```text
firmware/Rifle_Level/Rifle_Level.ino
```

## Hardware

- Seeed Studio XIAO nRF52840 Sense
- Onboard LSM6DS3 IMU
- 3 external LEDs
  - Left LED
  - Center / level LED
  - Right LED
- LiPo battery
- Calibration / brightness button
- Onboard RGB LED for battery status
- Compact 3D printed enclosure

## Features

- Roll / cant LED level indication
- Saved cant calibration using LittleFS
- Button brightness control
- Button calibration reset
- Bluetooth UART output for iOS app
- Startup battery status
- Low battery RGB warning
- Pitch angle output
- Angle cosine output
- Stability score output

## LED Pins

| Function | Pin |
|---|---|
| Left LED | D0 |
| Center LED | D1 |
| Right LED | D2 |
| Button | D3 |

## BLE Output

Firmware v0.2.4 sends app data as three small rotating BLE packets to reduce Bluetooth slowdown.

Example:

```text
v:1,c:-0.42,o:0.10
p:1.80,x:0.999
s:82,b:3.92
```

## BLE Field Meanings

| Field | Example | Meaning |
|---|---:|---|
| v | 1 | BLE format version |
| c | -0.42 | App cant angle in degrees |
| o | 0.10 | Cant calibration offset |
| p | 1.80 | Rifle pitch angle |
| x | 0.999 | Angle cosine based on pitch |
| s | 82 | Stability score, 0–100 |
| b | 3.92 | Battery voltage |

## Documentation

- [LED and Button Behavior](docs/led-and-button-behavior.md)

## Notes

The physical LEDs remain the primary shooting display.

The Bluetooth app is mainly for:

- Calibration
- Level tolerance setup
- Pitch and cosine display
- Stability display
- Diagnostics
- Battery voltage
- Future shot log support