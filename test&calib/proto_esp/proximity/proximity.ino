#include <ESP32Servo.h>

// Create servo objects
Servo servoD0;
Servo servoD1; // New fixed servo object
Servo servoD3;

// Pin Definitions
const int proximityPin = 6; // Proximity sensor input
const int pinD0 = 0;         // Servo D0 signal pin
const int pinD1 = 1;         // Servo D1 signal pin
const int pinD3 = 3;         // Servo D3 signal pin

// Variable to track the previous state of the sensor
int lastProxState = -1; 

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Allocate hardware timers for PWM control
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // Configure setup settings for Servo D0
  servoD0.setPeriodHertz(50);
  servoD0.attach(pinD0, 500, 2400);

  // Configure setup settings for Servo D1 (Fixed Servo)
  servoD1.setPeriodHertz(50);
  servoD1.attach(pinD1, 500, 2400);

  // Configure setup settings for Servo D3
  servoD3.setPeriodHertz(50);
  servoD3.attach(pinD3, 500, 2400);

  // Set Servo D1 to its fixed position immediately on startup
  servoD1.write(100);

  // Configure proximity sensor pin as standard input
  pinMode(proximityPin, INPUT);

  Serial.println("ESP32-C3 Servo Automation Active.");
  Serial.println("Servo D1 is locked at 100 degrees.");
}

void loop() {
  // Read the current state of the proximity sensor (HIGH or LOW)
  int currentProxState = digitalRead(proximityPin);

  // Only execute actions if the sensor state has flipped
  if (currentProxState != lastProxState) {
    
    if (currentProxState == LOW) {
      Serial.println("Sensor is LOW -> Moving D0 to 0° and D3 to 180°");
      servoD0.write(90);
      servoD3.write(90);
    } 
    else if (currentProxState == HIGH) {
      Serial.println("Sensor is HIGH -> Centering D0 and D3 to 90°");
      servoD0.write(0);
      servoD3.write(180);
    }

    // Save the current state to prevent repeating the command on the next loop
    lastProxState = currentProxState;
  }

  // Small delay to filter out electrical contact noise
  delay(30);
}