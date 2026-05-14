# Rifle Level

Rifle-mounted electronic level using a Seeed Studio XIAO nRF52840 Sense.

The device uses the onboard IMU to detect rifle cant/roll angle and drives three external LEDs for left / level / right indication. Bluetooth is used for app data, setup, diagnostics, pitch, cosine, stability, and battery voltage.

The physical LEDs are the primary shooting display. The phone app is mainly for setup, tuning, diagnostics, pitch/cosine, stability, and future shot log support.

---

## Current Firmware

Current version: **v0.2.5**

Arduino sketch:

```text
firmware/Rifle_Level/Rifle_Level.ino
```

---

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

---

## LED Pins

| Function | Pin |
|---|---|
| Left LED | D0 |
| Center / Level LED | D1 |
| Right LED | D2 |
| Button | D3 |

---

## Main Features

- Roll / cant LED level indication
- Saved cant calibration using LittleFS
- Button brightness control
- Button calibration reset
- Bluetooth UART output for iOS app
- BLE advertising timeout
- Startup battery status
- Low battery RGB warning
- Pitch angle output
- Angle cosine output
- Stability score output

---

## BLE Behavior

Firmware v0.2.5 sends app data as three small rotating BLE packets.

This was done to reduce BLE UART truncation and reduce slowdown compared to sending one long packet.

Example BLE output:

```text
v:1,c:-0.42,o:0.10
p:1.80,x:0.999
s:82,b:3.92
```

The firmware sends one small BLE packet every 150 ms.

A full app data refresh takes about:

```text
450 ms
```

---

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

---

## BLE Advertising Timeout

Bluetooth advertising starts when the rifle level powers on.

If no phone connects within 60 seconds, BLE advertising stops.

If a phone connects, BLE stays active.

If the phone disconnects, BLE advertising restarts for another 60 seconds.

This helps reduce unnecessary Bluetooth advertising and keeps the physical LED function prioritized.

---

## Button Functions

| Button Action | Hold Time | Function |
|---|---:|---|
| Short tap | Less than 1 second | Change LED brightness |
| Medium hold | 1 to 2 seconds | No action |
| Calibration hold | 2 to 5 seconds | Save cant zero calibration |
| Long hold | 5 seconds or longer | Reset saved cant calibration |

---

## Brightness Levels

A short button tap cycles through three LED brightness levels.

| Mode | Brightness Value | Feedback |
|---|---:|---|
| Dim | 4 | Center LED blinks 1 time |
| Normal | 15 | Center LED blinks 2 times |
| Bright | 40 | Center LED blinks 3 times |

Default brightness on startup:

```text
Normal / 15
```

Brightness is not currently saved after power-off.

---

## Startup LED Behavior

When the device powers on:

```text
1. Brief external LED boot flash may happen
2. Onboard RGB LED shows battery status
3. External LEDs show calibration status
4. Normal level operation begins
```

A brief boot flash can happen before the firmware fully takes control of the LED pins.

---

## Startup Battery Status

The onboard RGB LED shows battery status for about 2 seconds at startup.

| Battery Voltage | Onboard RGB LED |
|---|---|
| 3.75V or higher | Green |
| 3.50V to 3.74V | Yellow |
| Below 3.50V | Red |

After the startup battery check, the RGB LED turns off.

---

## Startup Calibration Status

After battery status, the external LEDs show calibration status.

| Calibration State | External LED Behavior |
|---|---|
| Saved calibration found | All 3 external LEDs solid for 2 seconds |
| No saved calibration | All 3 external LEDs flash 3 times slowly |

After this, the device enters normal level mode.

---

## Low Battery Warning During Use

During normal operation, the onboard RGB LED gives a low battery warning.

| Battery Voltage | Warning Behavior |
|---|---|
| Above 3.50V | RGB LED off |
| 3.30V to 3.50V | Yellow pulse every 10 seconds |
| Below 3.30V | Red pulse every 5 seconds |

Battery voltage is checked every 30 seconds in v0.2.5 to reduce blocking pauses.

---

## Physical LED Priority

The physical LEDs are the most important output.

Current physical LED update interval:

```text
20 ms
```

That is about:

```text
50 updates per second
```

Firmware v0.2.5 reduces background work by:

- Increasing battery update interval to 30 seconds
- Disabling serial debug output by default
- Keeping BLE packets short
- Stopping BLE advertising after timeout
- Leaving stability calculation unchanged for now

---

## Serial Debug

Serial output is disabled by default in v0.2.5.

In the firmware:

```cpp
const bool SERIAL_DEBUG = false;
```

Set it to `true` only when testing with Arduino Serial Monitor.

When enabled, serial output prints every 3 seconds.

---

## Documentation

- [LED and Button Behavior](docs/led-and-button-behavior.md)

---

## Future App Features

Planned or possible iOS app features:

- Cant zero from app
- Cant reset from app
- Pitch zero from app
- Pitch reset from app
- Level tolerance setting
- Pitch and cosine display
- User-entered target distance
- Angle-corrected distance
- Stability display
- Shot log with pitch, cosine, and stability
- Diagnostics screen

---

## Notes

The 3 external LEDs should stay dedicated to:

```text
LEFT / LEVEL / RIGHT
```

Pitch, cosine, stability, battery voltage, diagnostics, and future shot log features are intended for the phone app.