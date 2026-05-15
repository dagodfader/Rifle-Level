
Use this for your **CHANGELOG.md** update. Put the newest version at the top.

```markdown
# Changelog

## v0.2.9

- Split BLE output into shorter packets to reduce truncation
- Removed version field from live BLE packets
- Changed live BLE data to alternate between cant and pitch/cosine packets
- Fast BLE live packets now use:
  - `c:-0.42`
  - `p:1.80,x:0.999`
- Set fast BLE live packet interval to 150 ms
- Added slow status packets for offset, stability, battery, and calibration flags
- Slow BLE status packets now use:
  - `o:0.10,s:82`
  - `b:3.92,cc:1,pc:1`
- Added `cc` field for saved cant calibration status
- Added `pc` field for saved pitch calibration status
- Reduced BLE load to help prevent physical LED slowdown when BLE is connected
- Reset commands now delete saved calibration files instead of saving `0`
- Startup cant calibration status now checks the saved calibration flag instead of checking whether the offset is non-zero
- Physical LED left / level / right behavior remains unchanged

## v0.2.8

- Changed BLE output to fast live packets and slow status packets
- Fast packet included cant, pitch, and cosine
- Slow packet included offset, stability, battery, and calibration flags
- Added `cc` and `pc` calibration flags
- Improved app responsiveness compared to the previous 3-packet rotation format
- Later replaced by v0.2.9 because packets could still be too long and BLE activity could slow the physical LEDs

## v0.2.7

- Improved BLE command parser for iOS app commands
- Commands now work with newline, carriage return, or no terminator when the exact command matches
- Added known-command detection to process complete commands immediately
- Improved compatibility with different iOS BLE write behavior

## v0.2.6

- Added iOS app commands for cant and pitch calibration
- Added `ZERO_CANT` / `RESET_CANT` commands
- Added `ZERO_PITCH` / `RESET_PITCH` commands
- Added aliases `ZC`, `RC`, `ZP`, `RP`
- Added saved pitch offset using LittleFS
- Added BLE ack/error replies for app commands
- Kept existing 3-packet BLE app data format

## v0.2.5

- Added 60-second BLE advertising timeout
- BLE advertising stops if no phone connects within the timeout window
- BLE advertising restarts for another 60 seconds after disconnect
- Increased battery update interval from 5 seconds to 30 seconds
- Added `SERIAL_DEBUG` switch, disabled by default
- Increased debug serial print interval to 3 seconds when enabled
- Kept BLE packet interval at 150 ms
- Left stability calculation unchanged
- Physical LED update interval remains 20 ms

## v0.2.4

- Changed BLE output to send one small packet at a time
- Sends three rotating BLE packets instead of one long packet
- Reduced Bluetooth slowdown during normal level operation
- BLE packets now use short field names for iOS app parsing
- Full app data refresh is about every 450 ms

## v0.2.3

- Changed BLE output into three smaller packets
- Added short field names `c`, `o`, `p`, `x`, `s`, `b`
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
- Added BLE app output for cant, offset, pitch, cosine, stability, and battery

## v0.1.0

- Added roll/cant LED level indication
- Added saved calibration using LittleFS
- Added button brightness control
- Added calibration reset
- Added BLE UART output
- Added startup battery status
- Added low battery RGB warning