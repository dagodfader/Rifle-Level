# Rifle Level

DIY electronic rifle cant level using a Seeed Studio XIAO nRF52840 Sense, onboard IMU, 3 external LEDs, LiPo battery, and BLE app support.

Current firmware version: **v0.2.9**

Firmware location:

```text
firmware/Rifle_Level/Rifle_Level.ino
```

## Hardware

- Seeed Studio XIAO nRF52840 Sense
- Onboard LSM6DS3 IMU
- 3 external LEDs for left / level / right indication
- LiPo battery
- Slide power switch
- Calibration button
- Compact 3D printed side-mounted enclosure

## LED Pins

| Function | Pin |
|---|---|
| Left LED | D0 |
| Center / Level LED | D1 |
| Right LED | D2 |
| Calibration / brightness button | D3 |

## Main Features

- 3-LED cant indication
- Saved cant calibration
- Saved pitch calibration
- iOS BLE app data output
- BLE app calibration commands
- Pitch angle output
- Angle cosine output
- Stability score
- Battery voltage output
- Startup battery status using onboard RGB LED
- Low battery warning using onboard RGB LED
- Adjustable LED brightness using the button
- BLE advertising timeout to reduce unnecessary broadcasting

## BLE Output

Firmware v0.2.9 uses short BLE packets to reduce truncation and reduce physical LED slowdown while BLE is connected.

Fast live packets alternate every **150 ms**:

```text
c:-0.42
p:1.80,x:0.999
```

Slow status packets alternate every **2000 ms**:

```text
o:0.10,s:82
b:3.92,cc:1,pc:1
```

## BLE Field Meanings

| Field | Meaning |
|---|---|
| `c` | Cant angle for the app |
| `p` | Pitch angle |
| `x` | Angle cosine value |
| `o` | Saved cant offset |
| `s` | Stability score |
| `b` | Battery voltage |
| `cc` | Cant calibration saved flag, `1` = saved, `0` = not saved |
| `pc` | Pitch calibration saved flag, `1` = saved, `0` = not saved |

Firmware version is **not currently sent over BLE**. This keeps live packets short and helps avoid BLE truncation.

## BLE App Commands

The iOS app can send BLE UART commands to control calibration.

| Command | Alias | Function | Firmware Reply |
|---|---|---|---|
| `ZERO_CANT` | `ZC` | Save current cant as zero | `ack:ZERO_CANT` |
| `RESET_CANT` | `RC` | Reset cant zero | `ack:RESET_CANT` |
| `ZERO_PITCH` | `ZP` | Save current pitch as zero | `ack:ZERO_PITCH` |
| `RESET_PITCH` | `RP` | Reset pitch zero | `ack:RESET_PITCH` |

Commands can end with `\n`, `\r`, or no line ending.

Unknown commands reply:

```text
err:UNKNOWN
```

Command buffer overflow replies:

```text
err:BUFFER
```

## Button Functions

| Button Action | Function |
|---|---|
| Tap under 1 second | Cycle LED brightness |
| Hold 2–5 seconds | Save cant zero calibration |
| Hold 5+ seconds | Reset cant calibration |

## Brightness Levels

The firmware has three LED brightness levels:

```text
4
15
40
```

Default brightness is:

```text
15
```

Button tap feedback:

| Brightness Mode | Feedback |
|---|---|
| Dim | Center LED blinks 1 time |
| Normal | Center LED blinks 2 times |
| Bright | Center LED blinks 3 times |

## Startup Behavior

On power-up:

1. The onboard RGB LED shows battery status for about 2 seconds.
2. The external LEDs show cant calibration status.
3. Normal level operation begins.

Battery startup colors:

| Color | Meaning |
|---|---|
| Green | Battery good |
| Yellow | Battery low |
| Red | Battery very low |

Cant calibration startup status:

| LED Pattern | Meaning |
|---|---|
| All 3 external LEDs solid for about 2 seconds | Cant calibration saved |
| All 3 external LEDs flash slowly 3 times | No cant calibration saved |

Pitch calibration is saved and reported to the app with `pc:1`, but it does not have a separate startup LED pattern.

## Low Battery Warning

During normal operation, the onboard RGB LED gives battery warnings:

| Battery Voltage | Warning |
|---|---|
| Above 3.50V | RGB LED off |
| 3.30V–3.50V | Yellow pulse every 10 seconds |
| Below 3.30V | Red pulse every 5 seconds |

Battery voltage is checked every 30 seconds to avoid slowing down the main LED loop.

## BLE Advertising Behavior

BLE advertising starts at power-up.

If no phone connects within 60 seconds, advertising stops.

If a phone disconnects, advertising restarts for another 60 seconds.

This helps reduce unnecessary BLE broadcasting and saves some battery.