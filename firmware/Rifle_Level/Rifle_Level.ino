/*
  Rifle Level Firmware
  Version: v0.2.9

  BLE output is split into short packets to avoid truncation
  and reduce physical LED slowdown when BLE is connected.

  Fast live packets, alternating every 200 ms:

  c:-0.42
  p:1.80,x:0.999

  Slow status packets, alternating every 2000 ms:

  o:0.10,s:82
  b:3.92,cc:1,pc:1

  Field meanings:
  c  = cant angle for app
  p  = pitch angle
  x  = cosine value
  o  = cant calibration offset
  s  = stability score
  b  = battery voltage
  cc = cant calibration saved flag, 1 = saved, 0 = not saved
  pc = pitch calibration saved flag, 1 = saved, 0 = not saved

  iOS app commands:
  ZERO_CANT   or ZC = save current cant as zero
  RESET_CANT  or RC = reset cant zero
  ZERO_PITCH  or ZP = save current pitch as zero
  RESET_PITCH or RP = reset pitch zero

  Notes:
  - iOS commands can end with \n, \r, or no line ending.
  - BLE/app cant value is inverted from the internal roll value.
  - Physical LED left / level / right behavior is unchanged.
  - BLE advertising stops after 60 seconds if no phone connects.
  - If phone disconnects, advertising restarts for another 60 seconds.
  - Battery voltage updates every 30 seconds to reduce blocking pauses.
  - Serial debug is off by default to reduce background work.

  Hardware:
  - Seeed Studio XIAO nRF52840 Sense
  - Onboard LSM6DS3 IMU
  - 3 external LEDs: Left / Level / Right
  - LiPo battery
  - Onboard RGB LED used for battery status
*/

#include <Wire.h>
#include <LSM6DS3.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <bluefruit.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

using namespace Adafruit_LittleFS_Namespace;

LSM6DS3 imu(I2C_MODE, 0x6A);

// =====================================================
// DEBUG
// =====================================================

const bool SERIAL_DEBUG = false;

#define DEBUG_PRINT(...)    do { if (SERIAL_DEBUG) Serial.print(__VA_ARGS__); } while (0)
#define DEBUG_PRINTLN(...)  do { if (SERIAL_DEBUG) Serial.println(__VA_ARGS__); } while (0)

// =====================================================
// BLE
// =====================================================

BLEUart bleuart;

// Fast live BLE packets:
// Alternates between cant and pitch/cosine.
// Lower rate reduces physical LED slowdown.
unsigned long lastBleFastPrint = 0;
const int BLE_FAST_INTERVAL = 150;
int bleFastStep = 0;

// Slow status BLE packets:
// Alternates between offset/stability and battery/calibration flags.
unsigned long lastBleSlowPrint = 0;
const int BLE_SLOW_INTERVAL = 2000;
int bleSlowStep = 0;

// BLE command receive buffer
char bleCommandBuffer[40];
int bleCommandIndex = 0;

// BLE advertises for 60 seconds after startup.
// If no phone connects, advertising stops.
// If phone disconnects, advertising restarts for another 60 seconds.
const unsigned long BLE_ADVERTISE_TIMEOUT = 60000;

unsigned long bleAdvertisingStartTime = 0;
bool bleAdvertisingActive = false;
bool wasBleConnected = false;

// =====================================================
// IMU ORIENTATION
// =====================================================

const int ROLL_MODE = 1;
const int PITCH_MODE = 1;

// =====================================================
// LED PINS
// =====================================================

const int ledLeft   = D0;
const int ledCenter = D1;
const int ledRight  = D2;

// =====================================================
// BUTTON
// =====================================================

const int buttonPin = D3;

// =====================================================
// BRIGHTNESS
// =====================================================

const int brightnessModes[] = { 4, 15, 40 };
const int NUM_BRIGHTNESS_MODES = 3;

int brightnessIndex = 1;
int LED_BRIGHTNESS = brightnessModes[brightnessIndex];

// =====================================================
// BATTERY
// =====================================================

const float BAT_GOOD = 3.75;
const float BAT_LOW  = 3.50;
const float BAT_CRITICAL = 3.30;

#define BATTERY_ENABLE_PIN 14
#define BATTERY_READ_PIN   PIN_VBAT

// =====================================================
// RGB LED
// =====================================================

#define RGB_RED_PIN    LED_RED
#define RGB_GREEN_PIN  LED_GREEN
#define RGB_BLUE_PIN   LED_BLUE

// =====================================================
// LOW BATTERY WARNING
// =====================================================

unsigned long lastBatteryWarningCheck = 0;
unsigned long lastBatteryPulse = 0;
unsigned long batteryPulseStart = 0;

const int BATTERY_CHECK_INTERVAL = 30000;

const int LOW_BATTERY_PULSE_INTERVAL = 10000;
const int CRITICAL_BATTERY_PULSE_INTERVAL = 5000;
const int BATTERY_PULSE_DURATION = 120;

bool batteryPulseActive = false;
float latestBatteryVoltage = 0;

// =====================================================
// LEVEL TUNING
// =====================================================

const float levelThreshold = 0.6;
const float hysteresis = 0.15;
const float maxStep = 1.2;
const float alpha = 0.12;

// =====================================================
// STABILITY TUNING
// =====================================================

const int STABILITY_SAMPLES = 50;

float stabilityRollBuffer[STABILITY_SAMPLES];
float stabilityPitchBuffer[STABILITY_SAMPLES];

int stabilityIndex = 0;
int stabilityCount = 0;

float stabilityScore = 100.0;

// =====================================================
// STARTUP TIMING
// =====================================================

const int CALIBRATED_HOLD_DELAY = 2000;
const int NO_CAL_FLASH_DELAY = 350;
const int BATTERY_DISPLAY_TIME = 2000;

// =====================================================
// BLINK TIMING
// =====================================================

const int BLINK_FAST = 100;
const int BLINK_SLOW = 250;

// =====================================================
// STORAGE
// =====================================================

const char* zeroFilePath = "/zero.txt";
const char* pitchFilePath = "/pitch.txt";

// =====================================================
// CALIBRATION
// =====================================================

float zeroOffset = 0;
float pitchOffset = 0;

bool hasValidCalibration = false;
bool hasValidPitchCalibration = false;

// =====================================================
// LED STATE
// =====================================================

// state:
//  1  = left LED
//  0  = center LED
// -1  = right LED
int state = 0;

// =====================================================
// BUTTON STATE
// =====================================================

unsigned long pressStart = 0;

bool lastButtonState = HIGH;
bool buttonWasPressed = false;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 40;

bool resetDone = false;

// =====================================================
// BUTTON TIMING
// =====================================================

const unsigned long TAP_MAX_TIME    = 1000;
const unsigned long CAL_HOLD_TIME   = 2000;
const unsigned long RESET_HOLD_TIME = 5000;

// =====================================================
// FILTERING
// =====================================================

float filteredRoll = 0;
float filteredPitch = 0;

bool firstRead = true;

// =====================================================
// TIMING
// =====================================================

unsigned long lastPrint = 0;
const int printInterval = 3000;

unsigned long lastLoop = 0;
const int loopInterval = 20;

// =====================================================
// IMU ANGLE STRUCT
// =====================================================

struct AngleReading {
  float roll;
  float pitch;
};

// =====================================================
// SETUP
// =====================================================

void setup() {
  if (SERIAL_DEBUG) {
    Serial.begin(115200);
    delay(1000);
  } else {
    delay(100);
  }

  analogWriteResolution(8);
  analogReadResolution(12);

  pinMode(ledLeft, OUTPUT);
  pinMode(ledCenter, OUTPUT);
  pinMode(ledRight, OUTPUT);
  levelLedsOff();

  pinMode(BATTERY_ENABLE_PIN, OUTPUT);
  digitalWrite(BATTERY_ENABLE_PIN, LOW);

  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  rgbOff();

  Wire.begin();

  if (imu.begin() != 0) {
    DEBUG_PRINTLN("IMU failed");
  } else {
    DEBUG_PRINTLN("IMU ready");
  }

  if (!InternalFS.begin()) {
    DEBUG_PRINTLN("InternalFS mount failed");
  } else {
    DEBUG_PRINTLN("InternalFS mounted");

    hasValidCalibration = loadZeroOffset();
    hasValidPitchCalibration = loadPitchOffset();
  }

  setupBLE();

  pinMode(buttonPin, INPUT_PULLUP);

  levelLedsOff();

  showBatteryStartup();

  levelLedsOff();

  if (hasValidCalibration) {
    showCalibratedStartup();
  } else {
    showNoCalibrationStartup();
  }

  levelLedsOff();

  DEBUG_PRINTLN("System Ready");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  if (millis() - lastLoop < loopInterval) return;
  lastLoop = millis();

  AngleReading rawAngles = readRawAngles();

  if (firstRead) {
    filteredRoll = rawAngles.roll;
    filteredPitch = rawAngles.pitch;
    firstRead = false;
  } else {
    float rollDelta = rawAngles.roll - filteredRoll;
    float pitchDelta = rawAngles.pitch - filteredPitch;

    if (rollDelta > maxStep) rollDelta = maxStep;
    if (rollDelta < -maxStep) rollDelta = -maxStep;

    if (pitchDelta > maxStep) pitchDelta = maxStep;
    if (pitchDelta < -maxStep) pitchDelta = -maxStep;

    filteredRoll += alpha * rollDelta;
    filteredPitch += alpha * pitchDelta;
  }

  float adjustedRoll = filteredRoll - zeroOffset;
  float adjustedPitch = filteredPitch - pitchOffset;

  // App cant is inverted so the iOS display matches the desired direction.
  // Physical LED logic still uses adjustedRoll.
  float appCant = -adjustedRoll;

  float cosineValue = calculateCosine(adjustedPitch);

  updateStability(adjustedRoll, adjustedPitch);

  handleButton();

  updateLedState(adjustedRoll);

  analogWrite(ledLeft,   state == 1  ? LED_BRIGHTNESS : 0);
  analogWrite(ledCenter, state == 0  ? LED_BRIGHTNESS : 0);
  analogWrite(ledRight,  state == -1 ? LED_BRIGHTNESS : 0);

  updateBatteryWarning();

  updateBleAdvertisingTimeout();

  handleBleCommands();

  updateBLE(appCant, adjustedPitch, cosineValue);

  if (SERIAL_DEBUG && millis() - lastPrint >= printInterval) {
    lastPrint = millis();
    printSerialPackets(appCant, adjustedPitch, cosineValue);
  }
}

// =====================================================
// BLE
// =====================================================

void setupBLE() {
  Bluefruit.begin();

  Bluefruit.autoConnLed(false);

  Bluefruit.setTxPower(4);
  Bluefruit.setName("RifleLevel");

  bleuart.begin();

  Bluefruit.Advertising.addFlags(
    BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE
  );

  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();

  // We manage reconnect advertising ourselves so we can stop it after timeout.
  Bluefruit.Advertising.restartOnDisconnect(false);

  Bluefruit.Advertising.setInterval(160, 244);

  startBleAdvertisingWindow();

  DEBUG_PRINTLN("BLE UART ready");
}

void startBleAdvertisingWindow() {
  Bluefruit.Advertising.start(0);

  bleAdvertisingStartTime = millis();
  bleAdvertisingActive = true;

  DEBUG_PRINTLN("BLE advertising started");
}

void updateBleAdvertisingTimeout() {
  bool connected = Bluefruit.connected();

  if (connected) {
    wasBleConnected = true;
    bleAdvertisingActive = false;
    return;
  }

  if (wasBleConnected) {
    wasBleConnected = false;
    startBleAdvertisingWindow();
    return;
  }

  if (bleAdvertisingActive &&
      millis() - bleAdvertisingStartTime >= BLE_ADVERTISE_TIMEOUT) {

    Bluefruit.Advertising.stop();
    bleAdvertisingActive = false;

    DEBUG_PRINTLN("BLE advertising stopped after timeout");
  }
}

void updateBLE(float appCant, float adjustedPitch, float cosineValue) {
  if (!Bluefruit.connected()) return;

  unsigned long now = millis();

  // Fast live packets.
  // Short packets reduce BLE blocking and avoid truncation.
  if (now - lastBleFastPrint >= BLE_FAST_INTERVAL) {
    lastBleFastPrint = now;

    char packet[24];

    if (bleFastStep == 0) {
      snprintf(packet,
               sizeof(packet),
               "c:%.2f\n",
               appCant);
    } else {
      snprintf(packet,
               sizeof(packet),
               "p:%.2f,x:%.3f\n",
               adjustedPitch,
               cosineValue);
    }

    bleuart.print(packet);

    bleFastStep++;

    if (bleFastStep >= 2) {
      bleFastStep = 0;
    }

    return;
  }

  // Slow status packets.
  // Status does not need to update quickly.
  if (now - lastBleSlowPrint >= BLE_SLOW_INTERVAL) {
    lastBleSlowPrint = now;

    char packet[28];

    if (bleSlowStep == 0) {
      snprintf(packet,
               sizeof(packet),
               "o:%.2f,s:%.0f\n",
               zeroOffset,
               stabilityScore);
    } else {
      snprintf(packet,
               sizeof(packet),
               "b:%.2f,cc:%d,pc:%d\n",
               latestBatteryVoltage,
               hasValidCalibration ? 1 : 0,
               hasValidPitchCalibration ? 1 : 0);
    }

    bleuart.print(packet);

    bleSlowStep++;

    if (bleSlowStep >= 2) {
      bleSlowStep = 0;
    }
  }
}

// =====================================================
// BLE COMMANDS FROM IOS APP
// =====================================================

void handleBleCommands() {
  if (!Bluefruit.connected()) {
    bleCommandIndex = 0;
    return;
  }

  while (bleuart.available()) {
    int incoming = bleuart.read();

    if (incoming < 0) {
      return;
    }

    char c = (char)incoming;

    // Newline or carriage return means command is complete.
    // Supports both "\n" and "\r".
    if (c == '\n' || c == '\r') {
      if (bleCommandIndex > 0) {
        bleCommandBuffer[bleCommandIndex] = '\0';
        processBleCommand(bleCommandBuffer);
        bleCommandIndex = 0;
      }
      continue;
    }

    // Ignore spaces and tabs.
    if (c == ' ' || c == '\t') {
      continue;
    }

    // Store uppercase command characters.
    if (bleCommandIndex < (int)sizeof(bleCommandBuffer) - 1) {
      bleCommandBuffer[bleCommandIndex] =
        (char)toupper((unsigned char)c);

      bleCommandIndex++;
      bleCommandBuffer[bleCommandIndex] = '\0';

      // Process immediately if the command already matches.
      // This allows commands without "\n" to work.
      if (isKnownBleCommand(bleCommandBuffer)) {
        processBleCommand(bleCommandBuffer);
        bleCommandIndex = 0;
      }
    } else {
      bleCommandIndex = 0;
      sendBleError("BUFFER");
    }
  }
}

bool isKnownBleCommand(const char* command) {
  return strcmp(command, "ZERO_CANT") == 0 ||
         strcmp(command, "ZC") == 0 ||
         strcmp(command, "RESET_CANT") == 0 ||
         strcmp(command, "RC") == 0 ||
         strcmp(command, "ZERO_PITCH") == 0 ||
         strcmp(command, "ZP") == 0 ||
         strcmp(command, "RESET_PITCH") == 0 ||
         strcmp(command, "RP") == 0;
}

void processBleCommand(const char* command) {
  if (command[0] == '\0') {
    return;
  }

  if (strcmp(command, "ZERO_CANT") == 0 || strcmp(command, "ZC") == 0) {
    zeroCantFromApp();
  }
  else if (strcmp(command, "RESET_CANT") == 0 || strcmp(command, "RC") == 0) {
    resetCantFromApp();
  }
  else if (strcmp(command, "ZERO_PITCH") == 0 || strcmp(command, "ZP") == 0) {
    zeroPitchFromApp();
  }
  else if (strcmp(command, "RESET_PITCH") == 0 || strcmp(command, "RP") == 0) {
    resetPitchFromApp();
  }
  else {
    sendBleError("UNKNOWN");
  }
}

void zeroCantFromApp() {
  zeroOffset = getAverageRoll(20);
  saveZeroOffset();
  hasValidCalibration = true;

  sendBleAck("ZERO_CANT");

  DEBUG_PRINTLN("App command: ZERO_CANT");

  blinkCalibration();
}

void resetCantFromApp() {
  zeroOffset = 0;
  clearZeroOffset();
  hasValidCalibration = false;

  sendBleAck("RESET_CANT");

  DEBUG_PRINTLN("App command: RESET_CANT");

  blinkAllFast();
}

void zeroPitchFromApp() {
  pitchOffset = getAveragePitch(20);
  savePitchOffset();
  hasValidPitchCalibration = true;

  sendBleAck("ZERO_PITCH");

  DEBUG_PRINTLN("App command: ZERO_PITCH");

  blinkPitchCalibration();
}

void resetPitchFromApp() {
  pitchOffset = 0;
  clearPitchOffset();
  hasValidPitchCalibration = false;

  sendBleAck("RESET_PITCH");

  DEBUG_PRINTLN("App command: RESET_PITCH");

  blinkPitchReset();
}

void sendBleAck(const char* commandName) {
  if (!Bluefruit.connected()) return;

  bleuart.print("ack:");
  bleuart.print(commandName);
  bleuart.print("\n");
}

void sendBleError(const char* errorName) {
  if (!Bluefruit.connected()) return;

  bleuart.print("err:");
  bleuart.print(errorName);
  bleuart.print("\n");
}

void printSerialPackets(float appCant, float adjustedPitch, float cosineValue) {
  DEBUG_PRINT("c:");
  DEBUG_PRINTLN(appCant, 2);

  DEBUG_PRINT("p:");
  DEBUG_PRINT(adjustedPitch, 2);
  DEBUG_PRINT(",x:");
  DEBUG_PRINTLN(cosineValue, 3);

  DEBUG_PRINT("o:");
  DEBUG_PRINT(zeroOffset, 2);
  DEBUG_PRINT(",s:");
  DEBUG_PRINTLN(stabilityScore, 0);

  DEBUG_PRINT("b:");
  DEBUG_PRINT(latestBatteryVoltage, 2);
  DEBUG_PRINT(",cc:");
  DEBUG_PRINT(hasValidCalibration ? 1 : 0);
  DEBUG_PRINT(",pc:");
  DEBUG_PRINTLN(hasValidPitchCalibration ? 1 : 0);
}

// =====================================================
// IMU
// =====================================================

AngleReading readRawAngles() {
  AngleReading angles;

  float ax = imu.readFloatAccelX();
  float ay = imu.readFloatAccelY();
  float az = imu.readFloatAccelZ();

  angles.roll = calculateRoll(ax, ay, az);
  angles.pitch = calculatePitch(ax, ay, az);

  return angles;
}

float calculateRoll(float ax, float ay, float az) {
  if (ROLL_MODE == 1) return atan2(ay, az) * 180.0 / PI;
  if (ROLL_MODE == 2) return -atan2(ay, az) * 180.0 / PI;
  if (ROLL_MODE == 3) return atan2(ax, az) * 180.0 / PI;
  if (ROLL_MODE == 4) return -atan2(ax, az) * 180.0 / PI;

  return 0;
}

float calculatePitch(float ax, float ay, float az) {
  if (PITCH_MODE == 1) {
    return atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  }

  if (PITCH_MODE == 2) {
    return -atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  }

  if (PITCH_MODE == 3) {
    return atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;
  }

  if (PITCH_MODE == 4) {
    return -atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;
  }

  return 0;
}

float getAverageRoll(int samples) {
  float sum = 0;

  for (int i = 0; i < samples; i++) {
    AngleReading angles = readRawAngles();
    sum += angles.roll;
    delay(5);
  }

  return sum / samples;
}

float getAveragePitch(int samples) {
  float sum = 0;

  for (int i = 0; i < samples; i++) {
    AngleReading angles = readRawAngles();
    sum += angles.pitch;
    delay(5);
  }

  return sum / samples;
}

// =====================================================
// COSINE
// =====================================================

float calculateCosine(float pitchDegrees) {
  float pitchRadians = fabs(pitchDegrees) * PI / 180.0;
  return cos(pitchRadians);
}

// =====================================================
// STABILITY
// =====================================================

void updateStability(float adjustedRoll, float adjustedPitch) {
  stabilityRollBuffer[stabilityIndex] = adjustedRoll;
  stabilityPitchBuffer[stabilityIndex] = adjustedPitch;

  stabilityIndex++;

  if (stabilityIndex >= STABILITY_SAMPLES) {
    stabilityIndex = 0;
  }

  if (stabilityCount < STABILITY_SAMPLES) {
    stabilityCount++;
  }

  stabilityScore = calculateStabilityScore();
}

float calculateStabilityScore() {
  if (stabilityCount < 5) {
    return 100.0;
  }

  float minRoll = stabilityRollBuffer[0];
  float maxRoll = stabilityRollBuffer[0];

  float minPitch = stabilityPitchBuffer[0];
  float maxPitch = stabilityPitchBuffer[0];

  for (int i = 1; i < stabilityCount; i++) {
    if (stabilityRollBuffer[i] < minRoll) minRoll = stabilityRollBuffer[i];
    if (stabilityRollBuffer[i] > maxRoll) maxRoll = stabilityRollBuffer[i];

    if (stabilityPitchBuffer[i] < minPitch) minPitch = stabilityPitchBuffer[i];
    if (stabilityPitchBuffer[i] > maxPitch) maxPitch = stabilityPitchBuffer[i];
  }

  float rollRange = maxRoll - minRoll;
  float pitchRange = maxPitch - minPitch;

  float totalMovement = sqrt((rollRange * rollRange) + (pitchRange * pitchRange));

  float score = 100.0 - (totalMovement * 25.0);

  if (score > 100.0) score = 100.0;
  if (score < 0.0) score = 0.0;

  return score;
}

// =====================================================
// BATTERY
// =====================================================

float readBatteryVoltage() {
  const int samples = 20;
  long sum = 0;

  for (int i = 0; i < samples; i++) {
    sum += analogRead(BATTERY_READ_PIN);
    delay(2);
  }

  float rawAverage = sum / (float)samples;
  float voltage = (rawAverage / 4095.0) * 3.3;

  // Calibrated battery multiplier.
  voltage *= 3.22;

  return voltage;
}

void updateBatteryWarning() {
  unsigned long now = millis();

  if (now - lastBatteryWarningCheck >= BATTERY_CHECK_INTERVAL) {
    lastBatteryWarningCheck = now;
    latestBatteryVoltage = readBatteryVoltage();
  }

  if (batteryPulseActive) {
    if (now - batteryPulseStart >= BATTERY_PULSE_DURATION) {
      rgbOff();
      batteryPulseActive = false;
    }
    return;
  }

  if (latestBatteryVoltage <= 0) return;

  if (latestBatteryVoltage < BAT_CRITICAL) {
    if (now - lastBatteryPulse >= CRITICAL_BATTERY_PULSE_INTERVAL) {
      lastBatteryPulse = now;
      batteryPulseStart = now;
      batteryPulseActive = true;
      rgbRed();
    }
  }
  else if (latestBatteryVoltage < BAT_LOW) {
    if (now - lastBatteryPulse >= LOW_BATTERY_PULSE_INTERVAL) {
      lastBatteryPulse = now;
      batteryPulseStart = now;
      batteryPulseActive = true;
      rgbYellow();
    }
  }
  else {
    rgbOff();
  }
}

// =====================================================
// FILE SYSTEM
// =====================================================

bool loadZeroOffset() {
  File file = InternalFS.open(zeroFilePath, FILE_O_READ);

  if (!file) {
    zeroOffset = 0;
    DEBUG_PRINTLN("No saved cant calibration");
    return false;
  }

  String text = file.readString();
  file.close();

  float value = text.toFloat();

  if (isnan(value) || value < -180.0 || value > 180.0) {
    zeroOffset = 0;
    DEBUG_PRINTLN("Saved cant calibration invalid");
    return false;
  }

  zeroOffset = value;

  DEBUG_PRINT("Loaded cant zeroOffset: ");
  DEBUG_PRINTLN(zeroOffset, 6);

  return true;
}

void saveZeroOffset() {
  if (InternalFS.exists(zeroFilePath)) {
    InternalFS.remove(zeroFilePath);
  }

  File file = InternalFS.open(zeroFilePath, FILE_O_WRITE);

  if (file) {
    file.println(zeroOffset, 6);
    file.flush();
    file.close();

    DEBUG_PRINT("Saved cant zeroOffset: ");
    DEBUG_PRINTLN(zeroOffset, 6);
  } else {
    DEBUG_PRINTLN("Cant save failed");
  }
}

void clearZeroOffset() {
  if (InternalFS.exists(zeroFilePath)) {
    InternalFS.remove(zeroFilePath);
  }

  zeroOffset = 0;

  DEBUG_PRINTLN("Cleared cant calibration");
}

bool loadPitchOffset() {
  File file = InternalFS.open(pitchFilePath, FILE_O_READ);

  if (!file) {
    pitchOffset = 0;
    DEBUG_PRINTLN("No saved pitch calibration");
    return false;
  }

  String text = file.readString();
  file.close();

  float value = text.toFloat();

  if (isnan(value) || value < -180.0 || value > 180.0) {
    pitchOffset = 0;
    DEBUG_PRINTLN("Saved pitch calibration invalid");
    return false;
  }

  pitchOffset = value;

  DEBUG_PRINT("Loaded pitchOffset: ");
  DEBUG_PRINTLN(pitchOffset, 6);

  return true;
}

void savePitchOffset() {
  if (InternalFS.exists(pitchFilePath)) {
    InternalFS.remove(pitchFilePath);
  }

  File file = InternalFS.open(pitchFilePath, FILE_O_WRITE);

  if (file) {
    file.println(pitchOffset, 6);
    file.flush();
    file.close();

    DEBUG_PRINT("Saved pitchOffset: ");
    DEBUG_PRINTLN(pitchOffset, 6);
  } else {
    DEBUG_PRINTLN("Pitch save failed");
  }
}

void clearPitchOffset() {
  if (InternalFS.exists(pitchFilePath)) {
    InternalFS.remove(pitchFilePath);
  }

  pitchOffset = 0;

  DEBUG_PRINTLN("Cleared pitch calibration");
}

// =====================================================
// RGB
// =====================================================

void rgbOff() {
  digitalWrite(RGB_RED_PIN, HIGH);
  digitalWrite(RGB_GREEN_PIN, HIGH);
  digitalWrite(RGB_BLUE_PIN, HIGH);
}

void rgbRed() {
  digitalWrite(RGB_RED_PIN, LOW);
  digitalWrite(RGB_GREEN_PIN, HIGH);
  digitalWrite(RGB_BLUE_PIN, HIGH);
}

void rgbGreen() {
  digitalWrite(RGB_RED_PIN, HIGH);
  digitalWrite(RGB_GREEN_PIN, LOW);
  digitalWrite(RGB_BLUE_PIN, HIGH);
}

void rgbYellow() {
  digitalWrite(RGB_RED_PIN, LOW);
  digitalWrite(RGB_GREEN_PIN, LOW);
  digitalWrite(RGB_BLUE_PIN, HIGH);
}

void showBatteryStartup() {
  latestBatteryVoltage = readBatteryVoltage();

  DEBUG_PRINTLN("===== BATTERY STARTUP =====");

  DEBUG_PRINT("Battery Voltage: ");
  DEBUG_PRINT(latestBatteryVoltage, 3);
  DEBUG_PRINTLN(" V");

  if (latestBatteryVoltage >= BAT_GOOD) {
    DEBUG_PRINTLN("Battery GOOD");
    rgbGreen();
  }
  else if (latestBatteryVoltage >= BAT_LOW) {
    DEBUG_PRINTLN("Battery LOW");
    rgbYellow();
  }
  else {
    DEBUG_PRINTLN("Battery VERY LOW");
    rgbRed();
  }

  delay(BATTERY_DISPLAY_TIME);

  rgbOff();

  DEBUG_PRINTLN("===========================");
}

// =====================================================
// BUTTON
// =====================================================

void cycleBrightness() {
  brightnessIndex++;

  if (brightnessIndex >= NUM_BRIGHTNESS_MODES) {
    brightnessIndex = 0;
  }

  LED_BRIGHTNESS = brightnessModes[brightnessIndex];

  DEBUG_PRINT("Brightness changed to: ");
  DEBUG_PRINTLN(LED_BRIGHTNESS);

  blinkBrightnessMode();
}

void handleButton() {
  bool reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {

    if (reading == LOW && !buttonWasPressed) {
      buttonWasPressed = true;
      pressStart = millis();
      resetDone = false;
    }

    if (reading == LOW && buttonWasPressed) {
      unsigned long holdTime = millis() - pressStart;

      if (holdTime >= RESET_HOLD_TIME && !resetDone) {
        zeroOffset = 0;
        clearZeroOffset();
        hasValidCalibration = false;

        resetDone = true;

        DEBUG_PRINTLN("Calibration reset");
        blinkAllFast();
      }
    }

    if (reading == HIGH && buttonWasPressed) {
      unsigned long holdTime = millis() - pressStart;
      buttonWasPressed = false;

      if (!resetDone && holdTime < TAP_MAX_TIME) {
        cycleBrightness();
      }
      else if (!resetDone &&
               holdTime >= CAL_HOLD_TIME &&
               holdTime < RESET_HOLD_TIME) {

        zeroOffset = getAverageRoll(20);
        saveZeroOffset();
        hasValidCalibration = true;

        DEBUG_PRINTLN("Calibration saved");
        blinkCalibration();
      }
      else {
        DEBUG_PRINTLN("No action");
      }
    }
  }

  lastButtonState = reading;
}

// =====================================================
// LEVEL LOGIC
// =====================================================

void updateLedState(float adjustedRoll) {
  if (state == 0) {
    if (adjustedRoll > levelThreshold) {
      state = 1;
    }
    else if (adjustedRoll < -levelThreshold) {
      state = -1;
    }
  }
  else if (state == 1) {
    if (adjustedRoll < (levelThreshold - hysteresis)) {
      state = 0;
    }
  }
  else if (state == -1) {
    if (adjustedRoll > (-levelThreshold + hysteresis)) {
      state = 0;
    }
  }
}

// =====================================================
// STARTUP CALIBRATION STATUS
// =====================================================

void showCalibratedStartup() {
  analogWrite(ledLeft, LED_BRIGHTNESS);
  analogWrite(ledCenter, LED_BRIGHTNESS);
  analogWrite(ledRight, LED_BRIGHTNESS);

  delay(CALIBRATED_HOLD_DELAY);

  levelLedsOff();
}

void showNoCalibrationStartup() {
  for (int i = 0; i < 3; i++) {
    analogWrite(ledLeft, LED_BRIGHTNESS);
    analogWrite(ledCenter, LED_BRIGHTNESS);
    analogWrite(ledRight, LED_BRIGHTNESS);

    delay(NO_CAL_FLASH_DELAY);

    levelLedsOff();

    delay(NO_CAL_FLASH_DELAY);
  }
}

// =====================================================
// LED HELPERS
// =====================================================

void levelLedsOff() {
  analogWrite(ledLeft, 0);
  analogWrite(ledCenter, 0);
  analogWrite(ledRight, 0);
}

// =====================================================
// BLINK PATTERNS
// =====================================================

void blinkBrightnessMode() {
  int blinkCount = brightnessIndex + 1;

  for (int i = 0; i < blinkCount; i++) {
    analogWrite(ledCenter, LED_BRIGHTNESS);
    delay(BLINK_FAST);
    analogWrite(ledCenter, 0);
    delay(BLINK_FAST);
  }
}

void blinkCalibration() {
  for (int i = 0; i < 3; i++) {
    analogWrite(ledCenter, LED_BRIGHTNESS);
    delay(BLINK_SLOW);
    analogWrite(ledCenter, 0);
    delay(BLINK_SLOW);
  }
}

// Pitch zero feedback:
// left and right LEDs blink slowly together.
void blinkPitchCalibration() {
  for (int i = 0; i < 3; i++) {
    analogWrite(ledLeft, LED_BRIGHTNESS);
    analogWrite(ledRight, LED_BRIGHTNESS);

    delay(BLINK_SLOW);

    analogWrite(ledLeft, 0);
    analogWrite(ledRight, 0);

    delay(BLINK_SLOW);
  }
}

// Pitch reset feedback:
// left and right LEDs blink quickly together.
void blinkPitchReset() {
  for (int i = 0; i < 3; i++) {
    analogWrite(ledLeft, LED_BRIGHTNESS);
    analogWrite(ledRight, LED_BRIGHTNESS);

    delay(BLINK_FAST);

    analogWrite(ledLeft, 0);
    analogWrite(ledRight, 0);

    delay(BLINK_FAST);
  }
}

void blinkAllFast() {
  for (int i = 0; i < 3; i++) {
    analogWrite(ledLeft, LED_BRIGHTNESS);
    analogWrite(ledCenter, LED_BRIGHTNESS);
    analogWrite(ledRight, LED_BRIGHTNESS);

    delay(BLINK_FAST);

    levelLedsOff();

    delay(BLINK_FAST);
  }
}