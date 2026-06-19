#include <ESP32Servo.h>

// ==========================================
// SERVO SETUP
// ==========================================

Servo servos[4];

const int servoPins[4] = {0, 1, 2, 3};

// Servo Mapping
// D0 = Left Ankle
// D1 = Left Hip
// D2 = Right Hip
// D3 = Right Ankle

// ==========================================
// SENSOR
// ==========================================

const int touchPin = 5;

// ==========================================
// POSITION TRACKING
// ==========================================

int currentPos[4] = {90, 90, 90, 90};

// ==========================================
// HOME POSITIONS
// ==========================================

const int HOME_D0 = 90;
const int HOME_D1 = 90;
const int HOME_D2 = 90;
const int HOME_D3 = 90;

// ==========================================
// SETUP
// ==========================================

void setup() {

  Serial.begin(115200);
  delay(2000);

  pinMode(touchPin, INPUT);

  // Allocate PWM timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // Attach servos
  for (int i = 0; i < 4; i++) {

    servos[i].setPeriodHertz(50);

    servos[i].attach(servoPins[i], 500, 2400);

    servos[i].write(currentPos[i]);
  }

  Serial.println("==================================");
  Serial.println("      EMO STYLE WALK READY        ");
  Serial.println("==================================");

  delay(3000);
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

  // Optional touch sensor
  int touchState = digitalRead(touchPin);

  if (touchState == HIGH) {
    Serial.println("[TOUCH] Interaction detected!");
  }

  // ==========================================
  // RIGHT LEG STEP
  // ==========================================

  Serial.println("RIGHT STEP");

  // STEP 1
  // Lean left + lift right foot

  moveServosSmoothly(
    160,   // Left ankle tilt
    90,    // Left hip
    90,    // Right hip
    135,   // Right ankle lift
    40
  );

  delay(150);

  // STEP 2
  // Swing right leg forward

  moveServosSmoothly(
    90,
    90,
    45,    // Right hip forward
    80,
    35
  );

  delay(150);

  // STEP 3
  // Return neutral

  moveServosSmoothly(
    HOME_D0,
    HOME_D1,
    HOME_D2,
    HOME_D3,
    40
  );

  delay(300);

  // ==========================================
  // LEFT LEG STEP
  // ==========================================

  Serial.println("LEFT STEP");

  // STEP 4
  // Lean right + lift left foot

  moveServosSmoothly(
    45,    // Left ankle lift
    90,
    90,
    20,    // Right ankle tilt
    40
  );

  delay(150);

  // STEP 5
  // Swing left leg forward

  moveServosSmoothly(
    90,
    135,   // Left hip forward
    90,
    100,
    35
  );

  delay(150);

  // STEP 6
  // Return neutral

  moveServosSmoothly(
    HOME_D0,
    HOME_D1,
    HOME_D2,
    HOME_D3,
    40
  );

  delay(300);
}

// ==========================================
// SMOOTH MOTION ENGINE
// ==========================================

void moveServosSmoothly(
  int t0,
  int t1,
  int t2,
  int t3,
  int steps
) {

  int targets[4] = {t0, t1, t2, t3};

  float increments[4];

  // Calculate increments
  for (int i = 0; i < 4; i++) {

    increments[i] =
      (float)(targets[i] - currentPos[i]) / steps;
  }

  // Execute smooth movement
  for (int s = 0; s < steps; s++) {

    for (int i = 0; i < 4; i++) {

      float nextPos =
        currentPos[i] + (increments[i] * (s + 1));

      servos[i].write((int)nextPos);
    }

    delay(12);
  }

  // Save positions
  for (int i = 0; i < 4; i++) {

    currentPos[i] = targets[i];
  }
}
