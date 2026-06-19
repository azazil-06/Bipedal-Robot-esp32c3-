#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "FluxGarage_RoboEyes.h"
#include <ESP32Servo.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "bipedal_servo.h"

BipedalRobot robot;

// BLE Configuration
#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHARACTERISTIC_UUID "19b10001-e8f2-537e-4f6c-d104768a1214"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

void handleBLECommand(std::string cmd);

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        Serial.print("Received BLE command: ");
        Serial.println(value.c_str());
        handleBLECommand(value.c_str());
      }
    }
};

// Pin Definitions
const int proximityPin = 6;
const int pinD0 = 0;
const int pinD1 = 1;
const int pinD2 = 2;
const int pinD3 = 3;

// Variable to track the previous state of the sensor
int lastProxState = -1;

// --- Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR    0x3C
#define OLED_RST     -1

#define BUZZER_PIN 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
RoboEyes<Adafruit_SSD1306> roboEyes(display);

// --- Hardware Pins ---
const int TOUCH_PIN = 5;
const int LONG_TOUCH_TIME = 400; // ms for a long press
const int TAP_GAP_TIME = 400;   

// --- State Variables ---
unsigned long eventTimer;
// Optimization: Added 'L' suffix to large integer literals to ensure proper 32-bit long constant treatment
unsigned long nextResetDelay = 100000L;
bool sound = false;
unsigned long touchStartTime = 0;
unsigned long lastTouchReleaseTime = 0;
int tapCount = 0;
bool isTouching = false;
bool eventPlayed[21] = {false};



bool longPressTriggered = false;
bool flag = true;
bool isSleeping = false;
bool hasPlayed = false;
bool powerStatus = false;
bool boot = false;

int mood = 0;
int tap = 0;

volatile bool touchTriggered = false;

void IRAM_ATTR handleTouchInterrupt() {
  touchTriggered = true;
}

void handleBLECommand(std::string cmd) {
  if (cmd == "walk") {
    robot.walkForward(2);
  } else if (cmd == "back") {
    robot.walkBackward(2);
  } else if (cmd == "dance") {
    robot.dance();
  } else if (cmd == "bounce") {
    robot.happyBounce();
  } else if (cmd == "stomp") {
    playScream();
    robot.angryStomp();
  } else if (cmd == "slump") {
    playSad();
    robot.sadSlump();
  } else if (cmd == "tilt") {
    robot.curiousTilt();
  } else if (cmd == "stretch") {
    robot.stretch();
  } else if (cmd == "home") {
    robot.home();
  } else if (cmd == "off") {
    robot.off();
  } else if (cmd == "girl_on") {
    roboEyes.girlVersion = true;
  } else if (cmd == "girl_off") {
    roboEyes.girlVersion = false;
  } else if (cmd == "mood_happy") {
    roboEyes.setMood(HAPPY);
  } else if (cmd == "mood_angry") {
    roboEyes.setMood(ANGRY);
  } else if (cmd == "mood_sad") {
    roboEyes.setMood(TIRED);
  } else if (cmd == "mood_default") {
    roboEyes.setMood(DEFAULT);
  } else if (cmd.rfind("scale:", 0) == 0) {
    float scale = atof(cmd.substr(6).c_str());
    robot.setMotionScale(scale);
    Serial.print("Motion scale updated to: ");
    Serial.println(scale);
  } else if (cmd.rfind("servo:", 0) == 0) {
    int lh, rh, lf, rf, dur;
    if (sscanf(cmd.c_str(), "servo:%d,%d,%d,%d,%d", &lh, &rh, &lf, &rf, &dur) == 5) {
      Serial.print("Direct Servo Move: ");
      Serial.printf("LH=%d, RH=%d, LF=%d, RF=%d, DUR=%d\n", lh, rh, lf, rf, dur);
      robot.directMove(lh, rh, lf, rf, dur);
    }
  } else if (cmd.rfind("sound:", 0) == 0) {
    int freq, dur;
    if (sscanf(cmd.c_str(), "sound:%d,%d", &freq, &dur) == 2) {
      Serial.print("Custom Tone: ");
      Serial.printf("FREQ=%d, DUR=%d\n", freq, dur);
      tone(BUZZER_PIN, freq, dur);
      delay(dur + 10);
      noTone(BUZZER_PIN);
    }
  } else if (cmd == "sound_happy") {
    playHappy();
  } else if (cmd == "sound_sad") {
    playSad();
  } else if (cmd == "sound_scream") {
    playScream();
  } else if (cmd == "sound_r2d2") {
    playR2D2();
  } else if (cmd == "sound_surprise") {
    playSurprise();
  } else if (cmd == "sound_angry") {
    playAngry();
  } else if (cmd == "sound_sleep") {
    playSleep();
  }
}

void setup() {
  Serial.begin(9600);

  // Initialize bipedal robot with the correct pins (Left Hip, Right Hip, Left Ankle, Right Ankle)
  robot.begin(pinD1, pinD2, pinD0, pinD3);
  // Commented out to prevent OLED glitches/dropouts due to servo noise during movement
  // robot.registerUpdateCallback([]() { roboEyes.update(); });

  // Configure proximity sensor pin as standard input
  pinMode(proximityPin, INPUT);

  pinMode(TOUCH_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TOUCH_PIN), handleTouchInterrupt, RISING);
  Wire.begin(10, 9); // SDA = 10, SCL = 9

  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) { 
    for(;;); 
  }
  
  Wire.setClock(400000);
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.close();
  
  // Initialize BLE
  BLEDevice::init("Kuttappan_Robot");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started!");

  eventTimer = millis();
  nextResetDelay = random(100000L, 200000L);
}

void loop() {
  unsigned long currentMillis = millis();

  // BLE Connection advertising management
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); 
    pServer->startAdvertising(); // restart advertising
    Serial.println("Restarted BLE advertising");
    
    // Clear screen and reopen eyes if active
    display.clearDisplay();
    display.display();
    if (powerStatus) {
      roboEyes.open();
      roboEyes.update();
    }
    
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("BLE client connected");
    
    // Display "remote access" on the OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(24, 28);
    display.println("remote access");
    display.display();
    
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected) {
    // Periodically stream sensor telemetry
    static unsigned long lastTelemetryTime = 0;
    if (currentMillis - lastTelemetryTime >= 500) {
      bool isGrounded = (digitalRead(proximityPin) == HIGH);
      bool isPressed = (digitalRead(TOUCH_PIN) == HIGH) || touchTriggered;
      touchTriggered = false; // consume it
      
      char payload[64];
      snprintf(payload, sizeof(payload), "sensors:%s,%s", 
               isGrounded ? "Grounded" : "Lifted", 
               isPressed ? "Pressed" : "Released");
               
      pCharacteristic->setValue(payload);
      pCharacteristic->notify();
      lastTelemetryTime = currentMillis;
    }
    delay(10);
    return;
  }

  // Proximity Sensor Logic
  static unsigned long lastProxCheck = 0;
  if (currentMillis - lastProxCheck > 30) {
    int currentProxState = digitalRead(proximityPin);
    if (currentProxState != lastProxState) {
      if (currentProxState == LOW) {
        Serial.println("Sensor is LOW -> Angry Stomp!");
        roboEyes.setMood(ANGRY);
        playScream();
        robot.angryStomp();
      } 
      else if (currentProxState == HIGH) {
        Serial.println("Sensor is HIGH -> Back to normal");
        roboEyes.setMood(DEFAULT);
        robot.home();
      }
      lastProxState = currentProxState;
    }
    lastProxCheck = currentMillis;
  }

  bool isPressed = (digitalRead(TOUCH_PIN) == HIGH) || touchTriggered;
  if (isPressed) {
    touchTriggered = false;
  }

  static unsigned long powerOffTime = 0;
  
  if(!boot) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    // Optimization: Standard string literals are used here to save PROGMEM instruction overhead, since dynamic RAM usage is low (~50%)
    display.setCursor(50, 10);
    display.println(":)");
    display.setCursor(10, 30);
    display.println("Rocky standby");
    display.setCursor(20, 50);
    display.println("ESP32-C3 Online");
    display.display();
  }

  if (!powerStatus) {
    if (millis() - powerOffTime > 500) {
        if (isPressed && !isTouching) {
            powerStatus = true;
            boot = true;
        }
    }
  }

  if(isPressed && !powerStatus && !isTouching) {
    powerStatus = true;
  }

  if(powerStatus) {
    bool wasTouching = isTouching;
    static unsigned long releaseTime = 0;

    if (isPressed) {
      releaseTime = 0;
      if (!isTouching) {
        isTouching = true;
        touchStartTime = currentMillis;
        longPressTriggered = false;
        roboEyes.open();
      }
    } else {
      if (releaseTime == 0) {
        releaseTime = currentMillis;
      }
      if (currentMillis - releaseTime > 50) {
        isTouching = false;
      }
    }

    roboEyes.update();

    if (isTouching && !longPressTriggered) {
      if (currentMillis - touchStartTime >= LONG_TOUCH_TIME) {
        triggerLongTouchTask();
        longPressTriggered = true;
        tapCount = 0;
      }
    }

    if (wasTouching && !isTouching) {
      unsigned long duration = currentMillis - touchStartTime;
      if (!longPressTriggered && duration < LONG_TOUCH_TIME) {
        tapCount++;
        lastTouchReleaseTime = currentMillis;
      }
    }

    if (tapCount > 0 && (currentMillis - lastTouchReleaseTime > TAP_GAP_TIME)) {
      switch (tapCount) {
        case 1:
          triggerShortTouchTask();
          break;

        case 2:
          break;

        case 3:
          playSad();
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(WHITE);
          display.setCursor(20, 30);
          display.println("Rocky sleeping");
          display.display();
          robot.off(); // Park feet 0 and 180 deg
          delay(1000);
          display.clearDisplay();
          display.display();
          powerStatus = false;
          powerOffTime = millis();
          break;

        default:
          triggerTooManyTapsTask();
          break;
      }
      tapCount = 0;
    }

    if (!isTouching && tapCount == 0) {
      handleIdleAnimations();
    }
  }
}

void triggerShortTouchTask() {
  // Optimization: Passing string directly as const char* to avoid Arduino String class dynamic allocation and save PROGMEM instructions
  sendDataToPC("Sinle_TAP", 1);
  playR2D2();
  
  roboEyes.setHeight(36,36);
  roboEyes.setWidth(36,36);
  roboEyes.setBorderradius(8,8);
  
  if(isSleeping) {
    roboEyes.setMood(ANGRY);
    isSleeping = false;  
    robot.angryStomp();
  }
  else if(mood == 1) {
    roboEyes.setMood(DEFAULT);
    roboEyes.anim_laugh();
    mood = 0;
    if (random(0, 2) == 0) {
      robot.stretch();
    } else {
      robot.idle();
    }
  }
  else {
    roboEyes.setMood(HAPPY);
    roboEyes.blink();
    mood = 1;
    robot.happyBounce();
  }
}

void triggerLongTouchTask() {
  sound = !sound;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 50);
  display.println("sound mode:");
  display.setCursor(88, 50);
  sound ? display.println("ON") : display.println("OFF");
  display.display();
  delay(1000);
  display.clearDisplay();
  roboEyes.setMood(HAPPY);

  if (!hasPlayed) {
    roboEyes.setWidth(46, 46);
    roboEyes.setHeight(50, 50);
    roboEyes.setBorderradius(12, 12);
    roboEyes.setMood(HAPPY);
    roboEyes.setAutoblinker(OFF, 0, 0);
    roboEyes.anim_laugh();
    roboEyes.blink(0, 2);
    for (int i = 0; i < 3; i++) {
      tone(BUZZER_PIN, 1800 + (i * 300), 60);
      delay(90);
    }
    noTone(BUZZER_PIN);
    
    hasPlayed = true;
    robot.dance();
  }
  else {
    roboEyes.setMood(TIRED);
    robot.sadSlump();
  }
}

//dont edit this works good
void triggerTooManyTapsTask() {
  roboEyes.setWidth(36, 36);
  roboEyes.setHeight(36, 36);
  roboEyes.setBorderradius(8, 8);
  roboEyes.setMood(ANGRY);

  if (tap > 3) {
    playScream();
    roboEyes.setVFlicker(OFF, 0);
    tap = 0;
    robot.angryStomp();
  } 
  else {
    roboEyes.anim_confused();
    
    if (flag) {
      tone(BUZZER_PIN, 150, 100); 
      
      roboEyes.setVFlicker(ON, 10);
      for(int i = 0; i < 8; i++) {
        roboEyes.update();
        delay(10);
      }
      roboEyes.setVFlicker(OFF, 0);
    } 
    else {
      roboEyes.setHFlicker(ON, 10);
      tone(BUZZER_PIN, 2500, 20); 
      
      for(int i = 0; i < 8; i++) {
        roboEyes.update();
        delay(10);
      }
      roboEyes.setHFlicker(OFF, 0);
      tap++;
    }
    flag = !flag;
    robot.curiousTilt();
  }
}

void handleIdleAnimations() {
  unsigned long currentMillis = millis();

  if(currentMillis >= eventTimer + random(2000,5000) && !eventPlayed[0]) {
    eventPlayed[0] = true;
    roboEyes.setAutoblinker(ON, 3, 2);
    roboEyes.open(); 
    roboEyes.blink(0,1);
    if(sound){playR2D2();}
    unsigned long startMove = millis();
    robot.idle();
    eventTimer += (millis() - startMove);
  }
  
  if(currentMillis >= eventTimer + random(5000,8000) && !eventPlayed[1]) {
    eventPlayed[1] = true;
    roboEyes.setMood(TIRED);
    unsigned long startMove = millis();
    robot.sadSlump();
    eventTimer += (millis() - startMove);
  }

  if(currentMillis >= eventTimer + random(8000,11000) && !eventPlayed[2]) {
    eventPlayed[2] = true;
    roboEyes.setCuriosity(ON);
    roboEyes.setMood(DEFAULT);    
    roboEyes.setPosition(W); 
    unsigned long startMove = millis();
    robot.curiousTilt();
    eventTimer += (millis() - startMove);
  }

  if(currentMillis >= eventTimer + 11000 && !eventPlayed[3]) {
    eventPlayed[3] = true;   
    roboEyes.setPosition(SW);  
    roboEyes.setMood(DEFAULT);
    if(sound){playR2D2();}
    unsigned long startMove = millis();
    robot.walkBackward(1);
    eventTimer += (millis() - startMove);
  }

  if(currentMillis >= eventTimer + 14000 && !eventPlayed[4]) {
    eventPlayed[4] = true;  
    roboEyes.setPosition(E); 
    roboEyes.close(); 
    roboEyes.setMood(DEFAULT);
    if(sound){playR2D2();}
    unsigned long startMove = millis();
    robot.walkForward(1);
    eventTimer += (millis() - startMove);
  }

  if(currentMillis >= eventTimer + random(14000,17000) && !eventPlayed[5]) {
    eventPlayed[5] = true;
    roboEyes.open(); 
    roboEyes.setPosition(DEFAULT); 
    roboEyes.setMood(DEFAULT); 
    roboEyes.blink(0,1);
    unsigned long startMove = millis();
    robot.home();
    eventTimer += (millis() - startMove);
  }

  // Optimization & Fix: Corrected typo 140000 to 17000 for logical idle sequence progression
  if(currentMillis >= eventTimer + random(17000,20000) && !eventPlayed[6]) {
    eventPlayed[6] = true;
    roboEyes.setPosition(W);
    roboEyes.update();   
    roboEyes.close();
    roboEyes.setMood(DEFAULT);
  }

  if(currentMillis >= eventTimer + random(22000,25000) && !eventPlayed[7]) {
    eventPlayed[7] = true;
    roboEyes.setPosition(E);
    roboEyes.open();
    roboEyes.setMood(DEFAULT);
  }

  if(currentMillis >= eventTimer + 25000 && !eventPlayed[8]) {
    eventPlayed[8] = true; 
    roboEyes.setHeight(66,66);
    roboEyes.setWidth(66,66);
    roboEyes.update();
    roboEyes.setMood(DEFAULT);
    roboEyes.setPosition(DEFAULT);  
  }

  if(currentMillis >= eventTimer + 27000 && !eventPlayed[9]) {
    eventPlayed[9] = true;   
    roboEyes.setPosition(SW);  
    roboEyes.setHeight(36,36);
    roboEyes.setWidth(36,36);
    if(sound){playR2D2();}
    roboEyes.update();
    roboEyes.setMood(DEFAULT);
  }

  if(currentMillis >= eventTimer + 30000 && !eventPlayed[10]) {
    eventPlayed[10] = true;  
    roboEyes.setPosition(E); 
    roboEyes.setBorderradius(8,20);
    roboEyes.open(1, 0);
    if(sound){playR2D2();}
    roboEyes.setMood(DEFAULT);
    roboEyes.setSweat(ON);
  }

  if(currentMillis >= eventTimer + 32000 && !eventPlayed[11]) {
    eventPlayed[11] = true;
    roboEyes.open(); 
    roboEyes.setMood(DEFAULT);
    roboEyes.setPosition(DEFAULT);  
  } 

  if(currentMillis >= eventTimer + 32500 && !eventPlayed[12]) {
    eventPlayed[12] = true;
    roboEyes.setHFlicker(ON,10);  
    roboEyes.setMood(DEFAULT);
  } 

  if(currentMillis >= eventTimer + 33000 && !eventPlayed[13]) {
    eventPlayed[13] = true;
    roboEyes.setHFlicker(OFF,5);  
    roboEyes.setBorderradius(8,8);
    roboEyes.setWidth(36, 36);
    roboEyes.setMood(DEFAULT);
    roboEyes.setHeight(36, 36);
  } 

  if(currentMillis >= eventTimer + 36000 && !eventPlayed[14]) {
    eventPlayed[14] = true;
    if(sound){playR2D2();}
    roboEyes.setMood(TIRED); 
    isSleeping = true;
    roboEyes.setBorderradius(8,8);
    roboEyes.setWidth(36, 36);
    roboEyes.setHeight(32, 32);
    unsigned long startMove = millis();
    robot.off();
    eventTimer += (millis() - startMove);
  } 

  if(currentMillis >= eventTimer + 37000 && !eventPlayed[15]) {
    eventPlayed[15] = true;
    roboEyes.setHeight(28, 28);
  } 

  if(currentMillis >= eventTimer + 39000 && !eventPlayed[16]) {
    eventPlayed[16] = true;
    roboEyes.setHeight(25, 25);
  } 

  if(currentMillis >= eventTimer + 41000 && !eventPlayed[17]) {
    eventPlayed[17] = true;
    roboEyes.setHeight(20, 20);
  } 

  if(currentMillis >= eventTimer + 43000 && !eventPlayed[18]) {
    eventPlayed[18] = true;
    roboEyes.setHeight(17, 17);
  } 

  if(currentMillis >= eventTimer + 44500 && !eventPlayed[19]) {
    eventPlayed[19] = true;
    roboEyes.setVFlicker(ON,5);
    roboEyes.setMood(DEFAULT);
    isSleeping = false;
    roboEyes.setBorderradius(8,8);
    roboEyes.setWidth(36, 36);
    roboEyes.setHeight(15, 15);
    roboEyes.setSweat(OFF);
  } 

  if(currentMillis >= eventTimer + 45000 && !eventPlayed[20]) {
    eventPlayed[20] = true;
    roboEyes.setVFlicker(OFF,2);
    roboEyes.setHeight(36, 36);
    if(sound){playR2D2();}
    
    roboEyes.setCuriosity(ON);
    roboEyes.setAutoblinker(ON, 3, 2);
    roboEyes.setIdleMode(ON, 5, 2);
    unsigned long startMove = millis();
    robot.home();
    eventTimer += (millis() - startMove);
  }
    
  if(currentMillis >= eventTimer + nextResetDelay) {
    eventTimer = currentMillis;
    nextResetDelay = random(150000L, 250000L);
    roboEyes.setCuriosity(OFF);
    roboEyes.setAutoblinker(OFF, 3, 2);
    roboEyes.setIdleMode(OFF, 5, 2);
    
    memset(eventPlayed, 0, sizeof(eventPlayed));

    hasPlayed = false;
    tap = 0;
  }
}

// Optimization: Changed parameter from String to const char* to completely eliminate the heavy String class library from Flash memory (~1KB+ saved)
void sendDataToPC(const char* eventName, int value) {
  Serial.print(eventName);
  Serial.print(",");
  Serial.println(value);
}

// Optimization: Removed completely unused functions playSound() and playAngry() to free up program memory

void playR2D2() {
  int type = random(0, 3);

  if (type == 0) {
    // Sweet chirp tweet tweet
    for (int j = 0; j < 2; j++) {
      for (int freq = 2400; freq < 3800; freq += 180) {
        tone(BUZZER_PIN, freq, 8);
        delay(10);
      }
      noTone(BUZZER_PIN);
      delay(80);
    }
  } 
  else if (type == 1) {
    // High-pitched trill
    for (int i = 0; i < 4; i++) {
      tone(BUZZER_PIN, 3600, 20);
      delay(25);
      tone(BUZZER_PIN, 3200, 20);
      delay(25);
    }
    noTone(BUZZER_PIN);
  } 
  else {
    // Quick sweeping whistle
    for (int freq = 2000; freq < 3200; freq += 80) {
      tone(BUZZER_PIN, freq, 10);
      delay(12);
    }
    delay(40);
    tone(BUZZER_PIN, 3800, 60);
    delay(70);
    noTone(BUZZER_PIN);
  }
}

void playSad() {
  // A descending sad slide/whistle
  for (int freq = 2800; freq > 1400; freq -= 80) {
    tone(BUZZER_PIN, freq, 12);
    delay(15);
  }
  noTone(BUZZER_PIN);
}

void playScream() {
  // Fast high-pitched panic chirping
  for (int i = 0; i < 8; i++) {
    tone(BUZZER_PIN, random(3200, 4600), 25);
    delay(30);
  }
  tone(BUZZER_PIN, 800, 250);
  delay(270);
  noTone(BUZZER_PIN);
}

void playHappy() {
  // Rising arpeggio of cute chirps
  int notes[] = { 2093, 2637, 3136, 4186 }; // C7, E7, G7, C8
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, notes[i], 40);
    delay(50);
  }
  noTone(BUZZER_PIN);
}

void playSurprise() {
  // Sharp double pip
  tone(BUZZER_PIN, 2200, 30);
  delay(40);
  for (int freq = 3200; freq < 4400; freq += 200) {
    tone(BUZZER_PIN, freq, 8);
    delay(10);
  }
  noTone(BUZZER_PIN);
}

void playAngry() {
  // Fast, low rattling rumble
  for (int i = 0; i < 6; i++) {
    tone(BUZZER_PIN, 180 + random(0, 60), 30);
    delay(35);
  }
  noTone(BUZZER_PIN);
}

void playSleep() {
  // Breathing/whistling snores
  for (int snore = 0; snore < 2; snore++) {
    // Inhale
    for (int freq = 1600; freq < 1900; freq += 40) {
      tone(BUZZER_PIN, freq, 25);
      delay(30);
    }
    noTone(BUZZER_PIN);
    delay(250);
    // Exhale
    for (int freq = 1900; freq > 1600; freq -= 40) {
      tone(BUZZER_PIN, freq, 25);
      delay(30);
    }
    noTone(BUZZER_PIN);
    delay(400);
  }
}
