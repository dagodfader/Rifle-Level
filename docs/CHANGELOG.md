# Changelog

## v0.2.5

- Added BLE advertising timeout
- Increased battery update interval to 30 seconds
- Added serial debug switch, off by default
- Kept BLE packet interval unchanged
- Kept stability unchanged

## v0.2.4

- Changed BLE output to send one small packet at a time
- Sends three rotating BLE packets instead of one long packet
- Reduced Bluetooth slowdown during normal level operation
- BLE packets now use short field names for iOS app parsing
- Full app data refresh is about every 450 ms
- Current BLE packet format:

```text
v:1,c:-0.42,o:0.10
p:1.80,x:0.999
s:82,b:3.92
```

## v0.2.3

- Changed BLE output into three smaller packets
- Added short field names:
  - `c` = cant
  - `o` = offset
  - `p` = pitch
  - `x` = cosine
  - `s` = stability
  - `b` = battery
- Removed shot field from BLE output for now

## v0.2.2

- Increased BLE text buffer
- Inverted cant sign for the iOS app
- Kept physical LED left/right behavior unchanged

## v0.2.1

- Updated BLE format for iOS app parsing
- Added format version field
- Removed shot detection from active output

## v0.2.0

- Added pitch angle calculation
- Added angle cosine calculation
- Added stability score
- Added BLE app output for:
  - Cant angle
  - Calibration offset
  - Pitch angle
  - Angle cosine
  - Stability score
  - Battery voltage

## v0.1.0

- Added roll/cant LED level indication
- Added saved cant calibration using LittleFS
- Added button brightness control
- Added calibration reset
- Added BLE UART output
- Added startup battery status
- Added low battery RGB warning