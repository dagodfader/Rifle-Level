# LED and Button Behavior

Firmware version: v0.1.0

This document lists the LED behavior and button functions for the Rifle Level firmware.

## LED Types

The device has two different LED systems:

| LED System | Controlled By | Purpose |
|---|---|---|
| 3 external LEDs | Firmware | Main left / level / right cant indication |
| Onboard RGB LED | Firmware | Startup battery status and low battery warning |
| Charge LED | XIAO charging circuit | USB / battery charging status |

The charging LED is hardware-controlled by the XIAO nRF52840 Sense charging circuit. It is not controlled by the firmware.

---

# External Level LEDs

The three external LEDs are the main shooting display.

| LED | Color | Pin | Meaning |
|---|---|---|---|
| Left LED | Red | D0 | Rifle is tilted left |
| Center LED | Green | D1 | Rifle is level |
| Right LED | Blue | D2 | Rifle is tilted right |

## Normal Operation

During normal use:

| Rifle Condition | LED Behavior |
|---|---|
| Rifle is level | Center LED on |
| Rifle tilted left past threshold | Left LED on |
| Rifle tilted right past threshold | Right LED on |

Only one of the three external LEDs should be on during normal operation.

## Level Threshold

Current setting:

```cpp
levelThreshold = 0.6;
```

This means the center LED stays on when the rifle is within about:

```text
-0.6° to +0.6°
```

If the rifle tilts past that range, the left or right LED turns on.

## Hysteresis

Current setting:

```cpp
hysteresis = 0.15;
```

Hysteresis prevents the LEDs from flickering rapidly near the edge of the threshold.

With the current settings:

| Action | Angle |
|---|---:|
| Side LED turns on | Past ±0.60° |
| Center LED returns | Back inside about ±0.45° |

---

# Startup LED Behavior

When the device powers on, the startup sequence is:

```text
1. Brief external LED boot flash may happen
2. Onboard RGB LED shows battery status
3. External LEDs show calibration status
4. Normal level operation begins
```

## Brief Boot Flash

A short flash of the external LEDs may happen immediately at power-up.

This happens before the firmware fully takes control of the pins.

This is accepted behavior in the current build.

## Startup Battery Status

The onboard RGB LED shows battery status for about 2 seconds at startup.

| Battery Voltage | Onboard RGB LED |
|---|---|
| 3.75V or higher | Green |
| 3.50V to 3.74V | Yellow |
| Below 3.50V | Red |

After the battery status display, the onboard RGB LED turns off.

## Startup Calibration Status

After the battery status display, the external LEDs show calibration status.

| Calibration State | External LED Behavior |
|---|---|
| Saved calibration found | All 3 external LEDs solid for 2 seconds |
| No saved calibration | All 3 external LEDs flash 3 times slowly |

After this, the device enters normal level mode.

Note: In the current firmware, startup calibration status checks whether a saved `zeroOffset` exists and is not `0`.

---

# Low Battery Warning During Use

During normal operation, the onboard RGB LED gives a low battery warning.

| Battery Voltage | Warning Behavior |
|---|---|
| Above 3.50V | RGB LED off |
| 3.30V to 3.50V | Yellow pulse every 10 seconds |
| Below 3.30V | Red pulse every 5 seconds |

The low battery warning uses the onboard RGB LED so it does not interfere with the three main level LEDs.

---

# Charging LED Behavior

The XIAO nRF52840 Sense charging LED is controlled by the board charging circuit, not by the firmware.

Observed behavior on this build:

| Condition | Charge LED Behavior |
|---|---|
| USB plugged in | Green charge/status LED may turn on |
| Battery actively charging | Charge LED behavior depends on charger state |
| Battery full or standby | May show green / standby behavior |
| USB unplugged | Charge LED off |

The firmware does not turn the charge LED on or off.

The battery charges automatically when a LiPo is connected to the battery pads and USB power is plugged in.

---

# Button Hardware

The button is connected to:

```cpp
buttonPin = D3;
```

The firmware uses internal pullup mode:

```cpp
pinMode(buttonPin, INPUT_PULLUP);
```

That means:

| Button State | Pin Reading |
|---|---|
| Not pressed | HIGH |
| Pressed | LOW |

---

# Button Functions

| Button Action | Hold Time | Function |
|---|---:|---|
| Short tap | Less than 1 second | Change LED brightness |
| Medium hold | 1 to 2 seconds | No action |
| Calibration hold | 2 to 5 seconds | Save cant zero calibration |
| Long hold | 5 seconds or longer | Reset saved cant calibration |

---

# Brightness Control

A short button tap cycles through three LED brightness levels.

| Mode | Brightness Value | Feedback |
|---|---:|---|
| Dim | 4 | Center LED blinks 1 time |
| Normal | 15 | Center LED blinks 2 times |
| Bright | 40 | Center LED blinks 3 times |

Current default brightness on startup:

```cpp
LED_BRIGHTNESS = 15;
```

Brightness is not currently saved after power-off.

When the device is restarted, brightness returns to the default value.

---

# Calibration Function

Holding the button for 2 to 5 seconds saves the current rifle position as level.

This saves the current roll angle as:

```cpp
zeroOffset
```

After calibration is saved:

```text
Center LED blinks 3 times slowly
```

Use calibration when the rifle is mechanically level and the device is mounted correctly.

Recommended calibration steps:

```text
1. Mount the rifle level securely
2. Level the rifle using a trusted level
3. Hold the button for 2 to 5 seconds
4. Release the button
5. Wait for 3 slow center LED blinks
```

---

# Reset Function

Holding the button for 5 seconds or longer resets the saved cant calibration.

Reset sets:

```cpp
zeroOffset = 0;
hasValidCalibration = false;
```

Reset happens while the button is still being held.

After reset:

```text
All 3 external LEDs blink quickly
```

After releasing the button, no extra calibration action should happen.

---

# Button Feedback Summary

| Action | LED Feedback |
|---|---|
| Brightness set to dim | Center LED blinks 1 time |
| Brightness set to normal | Center LED blinks 2 times |
| Brightness set to bright | Center LED blinks 3 times |
| Calibration saved | Center LED blinks 3 times slowly |
| Calibration reset | All 3 external LEDs blink quickly |

---

# Recommended Normal Use

```text
1. Power on the rifle level
2. Watch onboard RGB battery status
3. Watch external LED calibration status
4. If needed, level the rifle and calibrate
5. Use the external LEDs for left / level / right cant indication
```

The physical LEDs are the primary shooting display.

The phone app / Bluetooth features are mainly for setup, tuning, diagnostics, pitch, cosine, stability, and shot logging.