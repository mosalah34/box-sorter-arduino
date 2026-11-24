#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= Pins =================
const byte TRIG_TOP = 2;
const byte ECHO_TOP = 3;

const byte TRIG_SIDE = 4;
const byte ECHO_SIDE = 6;

const byte MOTOR_IN1 = 7;
const byte MOTOR_IN2 = 8;
const byte MOTOR_ENA = 5;

const byte SERVO_PIN = 9;

// ================= Servo =================
Servo sorterServo;

const int SERVO_CLOSED = 0;
const int SERVO_OPEN = 160;

// ================= Motor =================
const int MOTOR_SPEED = 80;

// ================= Detection =================
const float EMPTY_TOP_CM = 7.0;

const float LARGE_MAX_CM = 3.4;
const float SMALL_MIN_CM = 3.6;

const float TOP_CHANGE_CM = 0.8;
const float SIDE_CHANGE_CM = 0.8;

const float MAX_VALID_CM = 200.0;

const byte DETECT_CONFIRMATIONS = 2;

// ================= Measurement =================
const byte SAMPLE_COUNT = 5;
const unsigned long SAMPLE_INTERVAL_MS = 100;
const unsigned long ECHO_TIMEOUT_US = 25000;

float emptySideCm = 0.0;

// ================= State =================
bool waitingForClear = false;

// ------------------------------------------------
// Read ultrasonic distance
// ------------------------------------------------
float readDistance(byte trigPin, byte echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(4);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration =
      pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);

  if (duration == 0) {
    return 0.0;
  }

  float distance = duration * 0.0343 / 2.0;

  if (distance < 2.0 || distance > MAX_VALID_CM) {
    return 0.0;
  }

  return distance;
}

// ------------------------------------------------
// Motor control
// ------------------------------------------------
void motorStop() {
  analogWrite(MOTOR_ENA, 0);
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
}

void motorRun() {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  analogWrite(MOTOR_ENA, MOTOR_SPEED);
}

// ------------------------------------------------
// Read empty-belt baseline
// ------------------------------------------------
float getBaseline(byte trigPin, byte echoPin) {
  const byte N = 9;
  float values[N];
  byte count = 0;

  for (byte i = 0; i < N; i++) {
    float d = readDistance(trigPin, echoPin);

    if (d > 0.0) {
      values[count++] = d;
    }

    delay(70);
  }

  if (count < 5) {
    return 0.0;
  }

  for (byte i = 0; i < count - 1; i++) {
    for (byte j = i + 1; j < count; j++) {
      if (values[j] < values[i]) {
        float temp = values[i];
        values[i] = values[j];
        values[j] = temp;
      }
    }
  }

  return values[count / 2];
}

// ------------------------------------------------
// Confirm object using both sensors
// ------------------------------------------------
bool objectDetected() {
  static byte confirmations = 0;

  float top = readDistance(TRIG_TOP, ECHO_TOP);
  delay(35);
  float side = readDistance(TRIG_SIDE, ECHO_SIDE);

  bool topSeesObject =
      top > 0.0 &&
      top < (EMPTY_TOP_CM - TOP_CHANGE_CM);

  bool sideSeesObject =
      side > 0.0 &&
      emptySideCm > 0.0 &&
      side < (emptySideCm - SIDE_CHANGE_CM);

  if (topSeesObject && sideSeesObject) {
    confirmations++;

    if (confirmations >= DETECT_CONFIRMATIONS) {
      confirmations = 0;
      return true;
    }
  } else {
    confirmations = 0;
  }

  return false;
}

// ------------------------------------------------
// Take five readings and reject unstable readings
// ------------------------------------------------
bool getStableTopDistance(float &result) {
  float readings[SAMPLE_COUNT];
  byte count = 0;

  for (byte i = 0; i < SAMPLE_COUNT; i++) {
    float d = readDistance(TRIG_TOP, ECHO_TOP);

    if (d > 0.0 &&
        d < (EMPTY_TOP_CM - TOP_CHANGE_CM)) {
      readings[count++] = d;
    }

    if (i < SAMPLE_COUNT - 1) {
      delay(SAMPLE_INTERVAL_MS);
    }
  }

  if (count < 4) {
    return false;
  }

  // Sort readings
  for (byte i = 0; i < count - 1; i++) {
    for (byte j = i + 1; j < count; j++) {
      if (readings[j] < readings[i]) {
        float temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }

  // Median
  float median;

  if (count % 2 == 0) {
    median =
        (readings[count / 2 - 1] +
         readings[count / 2]) / 2.0;
  } else {
    median = readings[count / 2];
  }

  // Reject readings more than 0.35 cm from median
  float sum = 0.0;
  byte accepted = 0;

  for (byte i = 0; i < count; i++) {
    if (abs(readings[i] * 100 - median * 100) <= 35) {
      sum += readings[i];
      accepted++;
    }
  }

  if (accepted < 4) {
    return false;
  }

  result = sum / accepted;
  return true;
}

// ------------------------------------------------
// LCD messages
// ------------------------------------------------
void showWaiting() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Belt Running");
  lcd.setCursor(0, 1);
  lcd.print("Waiting Box...");
}

void showError() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Unstable Read");
  lcd.setCursor(0, 1);
  lcd.print("Check Box");
}

void showLarge(float distance) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LARGE BOX 4x4");

  lcd.setCursor(0, 1);
  lcd.print("Height:");
  lcd.print(distance, 1);
  lcd.print("cm");
}

void showSmall(float distance) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SMALL BOX 2x2");

  lcd.setCursor(0, 1);
  lcd.print("Height:");
  lcd.print(distance, 1);
  lcd.print("cm");
}

// ------------------------------------------------
// Check whether box has cleared both sensors
// ------------------------------------------------
bool boxHasCleared() {
  float top = readDistance(TRIG_TOP, ECHO_TOP);
  delay(35);
  float side = readDistance(TRIG_SIDE, ECHO_SIDE);

  bool topClear =
      top > 0.0 &&
      top >= EMPTY_TOP_CM - TOP_CHANGE_CM;

  bool sideClear =
      side > 0.0 &&
      side >= emptySideCm - SIDE_CHANGE_CM;

  return topClear && sideClear;
}

// ------------------------------------------------
// Setup
// ------------------------------------------------
void setup() {
  pinMode(TRIG_TOP, OUTPUT);
  pinMode(ECHO_TOP, INPUT);

  pinMode(TRIG_SIDE, OUTPUT);
  pinMode(ECHO_SIDE, INPUT);

  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_ENA, OUTPUT);

  motorStop();

  sorterServo.attach(SERVO_PIN);
  sorterServo.write(SERVO_CLOSED);

  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sorting System");
  lcd.setCursor(0, 1);
  lcd.print("Calibrating...");

  delay(1000);

  // Keep belt empty during calibration
  emptySideCm = getBaseline(TRIG_SIDE, ECHO_SIDE);

  if (emptySideCm <= 0.0) {
    lcd.clear();
    lcd.print("Side Sensor Err");

    while (true) {
      motorStop();
      delay(100);
    }
  }

  showWaiting();
  motorRun();
}

// ------------------------------------------------
// Main loop
// ------------------------------------------------
void loop() {

  // Wait for previous box to leave
  if (waitingForClear) {
    motorRun();

    if (boxHasCleared()) {
      delay(250);

      float top = readDistance(TRIG_TOP, ECHO_TOP);

      if (top > 0.0 &&
          top >= EMPTY_TOP_CM - TOP_CHANGE_CM) {
        waitingForClear = false;
        showWaiting();
      }
    }

    return;
  }

  // Detect real box
  if (!objectDetected()) {
    return;
  }

  // Stop belt for measurement
  motorStop();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Belt Stopped");
  lcd.setCursor(0, 1);
  lcd.print("Measuring...");

  // Let vibration settle
  delay(250);

  float measuredDistance = 0.0;

  bool stable = getStableTopDistance(measuredDistance);

  if (!stable) {
    showError();

    delay(700);

    // Keep previous servo position
    waitingForClear = true;
    motorRun();
    return;
  }

  // ================= CLASSIFICATION =================

  if (measuredDistance <= LARGE_MAX_CM) {

    // LARGE BOX
    showLarge(measuredDistance);

    // Open immediately and remain at 160 degrees
    sorterServo.write(SERVO_OPEN);

  } else if (measuredDistance >= SMALL_MIN_CM &&
             measuredDistance < EMPTY_TOP_CM) {

    // SMALL BOX
    showSmall(measuredDistance);

    // Close immediately and remain at 0 degrees
    sorterServo.write(SERVO_CLOSED);

  } else {

    // Uncertain reading: preserve previous servo position
    showError();
    delay(700);
  }

  // Restart conveyor immediately after issuing servo command
  waitingForClear = true;
  motorRun();
}
