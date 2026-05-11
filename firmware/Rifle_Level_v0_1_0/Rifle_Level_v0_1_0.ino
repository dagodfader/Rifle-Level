/*
  Rifle Level Firmware
  Version: v0.1.0

  Hardware:
  - Seeed Studio XIAO nRF52840 Sense
  - Onboard LSM6DS3 IMU
  - 3 external LEDs: Left / Level / Right
  - LiPo battery
  - Onboard RGB LED used for battery status

  Features in this version:
  - Roll/cant level indication
  - Saved cant calibration using LittleFS
  - Button brightness control
  - Button calibration reset
  - BLE UART output
  - Startup battery status
  - Low battery RGB warning
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
const int blePrintInterval = 1000;

// =====================================================
// ROLL MODE
// =====================================================

const int ROLL_MODE = 1;

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

// How far the rifle can be tilted before the LEFT or RIGHT LED turns on.
// Smaller number = more sensitive.
// Larger number = less sensitive.
//
// Current setting:
// 0.6 degrees means the center LED stays on from about:
// -0.6° to +0.6°
//
// Good range for this project:
// 0.4 = very sensitive
// 0.6 = good practical setting
// 0.8 = less jumpy / easier to use
const float levelThreshold = 0.6;


// Hysteresis prevents the LEDs from flickering back and forth near the limit.
//
// Example with levelThreshold = 0.6 and hysteresis = 0.15:
//
// Center LED turns off and side LED turns on at:
// +0.60° or -0.60°
//
// Side LED does NOT return to center until the rifle comes back inside:
// +0.45° or -0.45°
//
// Why:
// Without hysteresis, the LED can rapidly flicker when the rifle is sitting
// right around 0.6°.
//
// Good range:
// 0.10 = tighter but may flicker more
// 0.15 = recommended
// 0.20 = more stable but slightly less responsive
const float hysteresis = 0.15;


// maxStep limits how much the filtered angle is allowed to change each loop.
//
// This helps ignore sudden vibration, bumps, recoil shake, or sensor spikes.
//
// Smaller number = smoother, but slower response.
// Larger number = faster response, but more jumpy.
//
// Current setting:
// The filtered roll can only move up to 1.2 degrees per loop update.
//
// Since loopInterval is 20 ms, this is still fast enough for normal use.
const float maxStep = 1.2;


// alpha controls smoothing.
//
// It decides how much of the new sensor reading is blended into the displayed
// angle each loop.
//
// Smaller alpha = smoother but slower.
// Larger alpha = faster but more twitchy.
//
// Current setting:
// 0.12 means each update moves about 12% toward the newest sensor reading.
//
// Good range:
// 0.08 = very smooth, slower
// 0.12 = recommended balanced setting
// 0.18 = more responsive, slightly more jitter
// 0.25 = fast but may look nervous
const float alpha = 0.12;

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
bool firstRead = true;

// =====================================================
// TIMING
// =====================================================

unsigned long lastPrint = 0;
const int printInterval = 1000;

unsigned long lastLoop = 0;
const int loopInterval = 20;

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  analogWriteResolution(8);
  analogReadResolution(12);

  delay(1000);

  // External LED pins set as outputs as early as possible.
  // This helps reduce startup flicker, but the tiny boot flash may still happen
  // before the sketch starts running.
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

  float rawRoll = readRawRoll();

  if (firstRead) {
    filteredRoll = rawRoll;
    firstRead = false;
  } else {
    float delta = rawRoll - filteredRoll;

    if (delta > maxStep) delta = maxStep;
    if (delta < -maxStep) delta = -maxStep;

    filteredRoll += alpha * delta;
  }

  float adjustedRoll = filteredRoll - zeroOffset;

  handleButton();

  updateLedState(adjustedRoll);

  analogWrite(ledLeft,   state == 1  ? LED_BRIGHTNESS : 0);
  analogWrite(ledCenter, state == 0  ? LED_BRIGHTNESS : 0);
  analogWrite(ledRight,  state == -1 ? LED_BRIGHTNESS : 0);

  updateBatteryWarning();

  updateBLE(adjustedRoll);

  if (millis() - lastPrint >= printInterval) {
    lastPrint = millis();

    Serial.print("Roll: ");
    Serial.print(adjustedRoll, 2);

    Serial.print(" | Brightness: ");
    Serial.print(LED_BRIGHTNESS);

    Serial.print(" | Battery: ");
    Serial.print(latestBatteryVoltage, 2);
    Serial.print("V");

    Serial.print(" | zeroOffset: ");
    Serial.println(zeroOffset, 6);
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

void updateBLE(float adjustedRoll) {
  if (!Bluefruit.connected()) return;

  if (millis() - lastBlePrint >= blePrintInterval) {
    lastBlePrint = millis();

    char buffer[40];

    char cur =
      (state == 1)  ? 'L' :
      (state == -1) ? 'R' : 'C';

    snprintf(buffer,
             sizeof(buffer),
             "%c%.1f Z%.2f B%.2f\n",
             cur,
             fabs(adjustedRoll),
             zeroOffset,
             latestBatteryVoltage);

    bleuart.print(buffer);
  }
}

// =====================================================
// IMU
// =====================================================

float readRawRoll() {
  float ax = imu.readFloatAccelX();
  float ay = imu.readFloatAccelY();
  float az = imu.readFloatAccelZ();

  if (ROLL_MODE == 1) return atan2(ay, az) * 180.0 / PI;
  if (ROLL_MODE == 2) return -atan2(ay, az) * 180.0 / PI;
  if (ROLL_MODE == 3) return atan2(ax, az) * 180.0 / PI;
  if (ROLL_MODE == 4) return -atan2(ax, az) * 180.0 / PI;

  return 0;
}

float getAverageRoll(int samples) {
  float sum = 0;

  for (int i = 0; i < samples; i++) {
    sum += readRawRoll();
    delay(5);
  }

  return sum / samples;
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

  // Calibrated multiplier for this specific board/battery reading.
  // If Serial voltage does not match your multimeter, adjust this value.
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