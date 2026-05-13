/*
  Rifle Level Firmware
  Version: v0.2.4

  BLE packet format version: v:1

  BLE output is sent as 3 small packets, one packet at a time:

  v:1,c:-0.42,o:0.10
  p:1.80,x:0.999
  s:82,b:3.92

  Field meanings:
  v = format version
  c = cant angle for app
  o = cant calibration offset
  p = pitch angle
  x = cosine value
  s = stability score
  b = battery voltage

  Notes:
  - BLE/app cant value is inverted from the internal roll value.
  - Physical LED left / level / right behavior is unchanged.
  - BLE sends one small packet every 150 ms to reduce slowdown.

  Hardware:
  - Seeed Studio XIAO nRF52840 Sense
  - Onboard LSM6DS3 IMU
  - 3 external LEDs: Left / Level / Right
  - LiPo battery
  - Onboard RGB LED used for battery status

  Features:
  - Roll/cant level indication
  - Saved cant calibration using LittleFS
  - Button brightness control
  - Button calibration reset
  - BLE UART output for iOS app
  - Startup battery status
  - Low battery RGB warning
  - Pitch angle
  - Angle cosine
  - Stability score
*/

#include <Wire.h>
#include <LSM6DS3.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <bluefruit.h>

using namespace Adafruit_LittleFS_Namespace;

LSM6DS3 imu(I2C_MODE, 0x6A);

// =====================================================
// BLE
// =====================================================

BLEUart bleuart;

unsigned long lastBlePrint = 0;

// Send one small BLE line at a time.
// 150 ms per line = full 3-line update about every 450 ms.
const int blePacketInterval = 150;

int blePacketStep = 0;

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

const int BATTERY_CHECK_INTERVAL = 5000;
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

// =====================================================
// CALIBRATION
// =====================================================

float zeroOffset = 0;
bool hasValidCalibration = false;

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
const int printInterval = 1000;

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
  Serial.begin(115200);

  analogWriteResolution(8);
  analogReadResolution(12);

  delay(1000);

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
    Serial.println("IMU failed");
  } else {
    Serial.println("IMU ready");
  }

  if (!InternalFS.begin()) {
    Serial.println("InternalFS mount failed");
  } else {
    Serial.println("InternalFS mounted");
    hasValidCalibration = loadZeroOffset();
  }

  setupBLE();

  pinMode(buttonPin, INPUT_PULLUP);

  levelLedsOff();

  showBatteryStartup();

  levelLedsOff();

  if (hasValidCalibration && zeroOffset != 0) {
    showCalibratedStartup();
  } else {
    showNoCalibrationStartup();
  }

  levelLedsOff();

  Serial.println("System Ready");
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
  float adjustedPitch = filteredPitch;

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

  updateBLE(appCant, adjustedPitch, cosineValue);

  if (millis() - lastPrint >= printInterval) {
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

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 244);
  Bluefruit.Advertising.start(0);

  Serial.println("BLE UART ready");
}

void updateBLE(float appCant, float adjustedPitch, float cosineValue) {
  if (!Bluefruit.connected()) return;

  if (millis() - lastBlePrint < blePacketInterval) return;

  lastBlePrint = millis();

  char packet[40];

  if (blePacketStep == 0) {
    snprintf(packet,
             sizeof(packet),
             "v:1,c:%.2f,o:%.2f\n",
             appCant,
             zeroOffset);
  }
  else if (blePacketStep == 1) {
    snprintf(packet,
             sizeof(packet),
             "p:%.2f,x:%.3f\n",
             adjustedPitch,
             cosineValue);
  }
  else {
    snprintf(packet,
             sizeof(packet),
             "s:%.0f,b:%.2f\n",
             stabilityScore,
             latestBatteryVoltage);
  }

  bleuart.print(packet);

  blePacketStep++;

  if (blePacketStep >= 3) {
    blePacketStep = 0;
  }
}

void printSerialPackets(float appCant, float adjustedPitch, float cosineValue) {
  Serial.print("v:1,c:");
  Serial.print(appCant, 2);
  Serial.print(",o:");
  Serial.println(zeroOffset, 2);

  Serial.print("p:");
  Serial.print(adjustedPitch, 2);
  Serial.print(",x:");
  Serial.println(cosineValue, 3);

  Serial.print("s:");
  Serial.print(stabilityScore, 0);
  Serial.print(",b:");
  Serial.println(latestBatteryVoltage, 2);
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
    Serial.println("No saved calibration");
    return false;
  }

  String text = file.readString();
  file.close();

  float value = text.toFloat();

  if (isnan(value) || value < -180.0 || value > 180.0) {
    zeroOffset = 0;
    Serial.println("Saved calibration invalid");
    return false;
  }

  zeroOffset = value;

  Serial.print("Loaded zeroOffset: ");
  Serial.println(zeroOffset, 6);

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

    Serial.print("Saved zeroOffset: ");
    Serial.println(zeroOffset, 6);
  } else {
    Serial.println("Save failed");
  }
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

  Serial.println("===== BATTERY STARTUP =====");

  Serial.print("Battery Voltage: ");
  Serial.print(latestBatteryVoltage, 3);
  Serial.println(" V");

  if (latestBatteryVoltage >= BAT_GOOD) {
    Serial.println("Battery GOOD");
    rgbGreen();
  }
  else if (latestBatteryVoltage >= BAT_LOW) {
    Serial.println("Battery LOW");
    rgbYellow();
  }
  else {
    Serial.println("Battery VERY LOW");
    rgbRed();
  }

  delay(BATTERY_DISPLAY_TIME);

  rgbOff();

  Serial.println("===========================");
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

  Serial.print("Brightness changed to: ");
  Serial.println(LED_BRIGHTNESS);

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
        saveZeroOffset();
        hasValidCalibration = false;

        resetDone = true;

        Serial.println("Calibration reset");
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

        Serial.println("Calibration saved");
        blinkCalibration();
      }
      else {
        Serial.println("No action");
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