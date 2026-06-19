#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "FluxGarage_RoboEyes.h"
#include <ESP32Servo.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_sleep.h"
#include <WiFi.h>
#include <ArduinoOTA.h>

#include "bipedal_servo.h"
#include "buzzer.h"

// Arduino OTA over WiFi (runs alongside BLE). Set WIFI_SSID or add wifi_credentials.h.
// Partition: Tools -> "Minimal SPIFFS" (arduino-cli: PartitionScheme=min_spiffs).
// Upload: arduino-cli upload -p ROCKY.local --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs
#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "your_ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "__pass__"
#endif
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "ROCKY"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "rocky"
#endif

BipedalRobot robot;
BuzzerPlayer buzzer;
volatile bool g_servoMotionActive = false;

#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHARACTERISTIC_UUID "19b10001-e8f2-537e-4f6c-d104768a1214"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

const int proximityPin = 6;
const int pinD0 = 0;
const int pinD1 = 1;
const int pinD2 = 2;
const int pinD3 = 3;

bool proximityBlocked = false;
const int PROX_ACTIVE_LEVEL = LOW;
const int PROX_DEBOUNCE_MS = 80;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR    0x3C
#define OLED_RST     -1

#define BUZZER_PIN 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
RoboEyes<Adafruit_SSD1306> roboEyes(display);

const int TOUCH_PIN = 5;
const int TOUCH_ACTIVE_LEVEL = HIGH;
const int TOUCH_DEBOUNCE_MS = 45;
const int LONG_TOUCH_TIME = 400;
const int TAP_GAP_TIME = 450;

unsigned long eventTimer;
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
bool legsHomed = false;
bool standbyScreenDrawn = false;

int mood = 0;
int tap = 0;

volatile bool touchTriggered = false;

unsigned long nextLegMoveTime = 0;
unsigned long nextEyeBeatTime = 0;
uint8_t idleEyePhase = 0;

static bool otaWifiEnabled = false;
static bool otaServiceStarted = false;
static volatile bool otaUpdateActive = false;
static unsigned long otaWifiRetryMs = 0;

static bool otaConfigured() {
  return WIFI_SSID[0] != '\0';
}

void IRAM_ATTR handleTouchInterrupt() {
  touchTriggered = true;
}

bool readTouchDebounced();
bool readProximityDebounced();
bool isProximityActive();
bool canMoveLegs();
void refreshEyesAfterMove();
void updateProximityState();
void enterProximityMode();
void exitProximityMode();
void enterDeepSleep();
void drawStandbyScreen();
void ensureLegsHomed();
void runRandomIdleLegMove();
void runRandomDanceMove();
void triggerShortTouchTask();
void triggerDoubleTapDance();
void triggerLongTouchTask();
void triggerTooManyTapsTask();
void handleIdleAnimations();
void handleBLECommand(std::string cmd);
void beginOta();
void serviceOta();

void drawOtaScreen(const char* line1, const char* line2 = nullptr) {
  if (deviceConnected) {
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2 != nullptr) {
    display.setCursor(0, 16);
    display.println(line2);
  }
  display.display();
}

void setupArduinoOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (OTA_PASSWORD[0] != '\0') {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    otaUpdateActive = true;
    drawOtaScreen("OTA update", "starting...");
  });
  ArduinoOTA.onEnd([]() {
    drawOtaScreen("OTA complete", "rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) {
      return;
    }
    uint8_t pct = (uint8_t)((progress * 100UL) / total);
    static uint8_t lastPct = 255;
    if (pct != lastPct && (pct % 10 == 0 || pct >= 99)) {
      lastPct = pct;
      char buf[20];
      snprintf(buf, sizeof(buf), "OTA %u%%", pct);
      drawOtaScreen(buf);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    char buf[24];
    snprintf(buf, sizeof(buf), "OTA err %u", (unsigned)error);
    drawOtaScreen("OTA failed", buf);
    otaUpdateActive = false;
  });

  ArduinoOTA.begin();
  otaServiceStarted = true;

  if (!deviceConnected) {
    char ipLine[24];
    snprintf(ipLine, sizeof(ipLine), "%s", WiFi.localIP().toString().c_str());
    drawOtaScreen("WiFi ready", ipLine);
  }
}

void beginOta() {
  if (!otaConfigured()) {
    return;
  }
  otaWifiEnabled = true;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  otaWifiRetryMs = millis() + 15000;
}

void serviceOta() {
  if (!otaWifiEnabled) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!otaServiceStarted) {
      setupArduinoOta();
    }
    ArduinoOTA.handle();
    return;
  }

  if (millis() >= otaWifiRetryMs) {
    WiFi.disconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    otaWifiRetryMs = millis() + 30000;
  }
}

bool isBleSoundOnly(const std::string& cmd) {
  return cmd == "girl_on" || cmd == "girl_off" ||
         cmd.rfind("mood_", 0) == 0 || cmd.rfind("sound:", 0) == 0 ||
         cmd == "sound_happy" || cmd == "sound_sad" || cmd == "sound_scream" ||
         cmd == "sound_r2d2" || cmd == "sound_surprise" || cmd == "sound_angry" ||
         cmd == "sound_sleep" || cmd == "sound_chirp" || cmd == "sound_fanfare" ||
         cmd == "sound_confused" || cmd == "sound_taunt" || cmd == "sound_victory" ||
         cmd == "sound_hello" || cmd == "sound_alert" || cmd == "sound_melody";
}

void handleBleSound(const std::string& cmd) {
  if (cmd == "sound_happy") buzzer.play(BUZZ_HAPPY);
  else if (cmd == "sound_sad") buzzer.play(BUZZ_SAD);
  else if (cmd == "sound_scream") buzzer.play(BUZZ_SCREAM);
  else if (cmd == "sound_r2d2") buzzer.play(BUZZ_R2D2);
  else if (cmd == "sound_surprise") buzzer.play(BUZZ_SURPRISE);
  else if (cmd == "sound_angry") buzzer.play(BUZZ_ANGRY);
  else if (cmd == "sound_sleep") buzzer.play(BUZZ_SLEEP);
  else if (cmd == "sound_chirp") buzzer.play(BUZZ_CHIRP);
  else if (cmd == "sound_fanfare") buzzer.play(BUZZ_FANFARE);
  else if (cmd == "sound_confused") buzzer.play(BUZZ_CONFUSED);
  else if (cmd == "sound_taunt") buzzer.play(BUZZ_TAUNT);
  else if (cmd == "sound_victory") buzzer.play(BUZZ_VICTORY);
  else if (cmd == "sound_hello") buzzer.play(BUZZ_HELLO);
  else if (cmd == "sound_alert") buzzer.play(BUZZ_ALERT);
  else if (cmd == "sound_melody") buzzer.play(BUZZ_MELODY);
  else if (cmd.rfind("sound:", 0) == 0) {
    int freq, dur;
    if (sscanf(cmd.c_str(), "sound:%d,%d", &freq, &dur) == 2) {
      buzzer.playTone((uint16_t)freq, (uint16_t)dur);
    }
  }
}

void handleBLECommand(std::string cmd) {
  if (!canMoveLegs() && !isBleSoundOnly(cmd)) {
    return;
  }

  if (cmd == "walk") {
    robot.walkForward(2);
  } else if (cmd == "back") {
    robot.walkBackward(2);
  } else if (cmd == "dance") {
    robot.dance();
  } else if (cmd == "bounce") {
    robot.happyBounce();
  } else if (cmd == "stomp") {
    buzzer.play(BUZZ_SCREAM);
    robot.angryStomp();
  } else if (cmd == "slump") {
    buzzer.play(BUZZ_SAD);
    robot.sadSlump();
  } else if (cmd == "tilt") {
    robot.curiousTilt();
  } else if (cmd == "stretch") {
    robot.stretch();
  } else if (cmd == "slide_left") {
    robot.slideLeft(2);
  } else if (cmd == "slide_right") {
    robot.slideRight(2);
  } else if (cmd == "shuffle") {
    robot.shuffle();
  } else if (cmd == "moonwalk") {
    robot.moonwalkStep();
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
  } else if (cmd.rfind("servo:", 0) == 0) {
    int lh, rh, lf, rf, dur;
    if (sscanf(cmd.c_str(), "servo:%d,%d,%d,%d,%d", &lh, &rh, &lf, &rf, &dur) == 5) {
      robot.directMove(lh, rh, lf, rf, dur);
    }
  } else {
    handleBleSound(cmd);
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        handleBLECommand(std::string(value.c_str()));
      }
    }
};

void setup() {
  robot.begin(pinD1, pinD2, pinD0, pinD3);
  robot.registerUpdateCallback(refreshEyesAfterMove);
  buzzer.begin(BUZZER_PIN);

  pinMode(proximityPin, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TOUCH_PIN), handleTouchInterrupt, CHANGE);

  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_GPIO) {
    powerStatus = true;
  }

  Wire.begin(10, 9);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    for (;;) {}
  }

  Wire.setClock(400000);
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.close();

  BLEDevice::init("ROCKY");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY |
      BLECharacteristic::PROPERTY_INDICATE);
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  eventTimer = millis();
  nextResetDelay = random(50000L, 80000L);
  nextLegMoveTime = millis() + random(6000, 12000);
  nextEyeBeatTime = millis() + random(2500, 5000);

  if (powerStatus) {
    roboEyes.open();
    ensureLegsHomed();
    updateProximityState();
  } else {
    drawStandbyScreen();
  }

  beginOta();
}

void loop() {
  unsigned long currentMillis = millis();
  buzzer.update();
  serviceOta();

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    display.clearDisplay();
    display.display();
    if (powerStatus) {
      roboEyes.open();
      if (!g_servoMotionActive) {
        roboEyes.update();
      }
    }
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected && !oldDeviceConnected) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(24, 28);
    display.println("remote access");
    display.display();
    oldDeviceConnected = deviceConnected;
  }

  updateProximityState();

  if (deviceConnected) {
    static unsigned long lastTelemetryTime = 0;
    if (currentMillis - lastTelemetryTime >= 500) {
      bool near = readProximityDebounced();
      bool isPressed = readTouchDebounced();
      char payload[64];
      snprintf(payload, sizeof(payload), "sensors:%s,%s",
               near ? "Near" : "Clear",
               isPressed ? "Pressed" : "Released");
      pCharacteristic->setValue(payload);
      pCharacteristic->notify();
      lastTelemetryTime = currentMillis;
    }
    delay(10);
    return;
  }

  bool isPressed = readTouchDebounced();

  if (!powerStatus) {
    if (!standbyScreenDrawn) {
      drawStandbyScreen();
    }
    if (isPressed && !isTouching) {
      powerStatus = true;
      standbyScreenDrawn = true;
      display.clearDisplay();
      roboEyes.open();
      ensureLegsHomed();
      updateProximityState();
    }
    return;
  }

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

  if (!g_servoMotionActive) {
    roboEyes.update();
  }

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
        triggerDoubleTapDance();
        break;
      case 3:
        enterDeepSleep();
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

bool readTouchDebounced() {
  static bool stableState = false;
  static bool lastRaw = false;
  static unsigned long lastEdgeMs = 0;

  bool raw = (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL);
  if (touchTriggered) {
    raw = true;
    touchTriggered = false;
  }

  unsigned long now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    lastEdgeMs = now;
  }
  if (now - lastEdgeMs >= (unsigned long)TOUCH_DEBOUNCE_MS) {
    stableState = raw;
  }
  return stableState;
}

bool readProximityDebounced() {
  static bool stableNear = false;
  static bool lastRaw = false;
  static unsigned long lastEdgeMs = 0;

  bool raw = (digitalRead(proximityPin) == PROX_ACTIVE_LEVEL);
  unsigned long now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    lastEdgeMs = now;
  }
  if (now - lastEdgeMs >= (unsigned long)PROX_DEBOUNCE_MS) {
    stableNear = raw;
  }
  return stableNear;
}

bool isProximityActive() {
  return readProximityDebounced();
}

bool canMoveLegs() {
  return !proximityBlocked && legsHomed;
}

void refreshEyesAfterMove() {
  if (!g_servoMotionActive) {
    roboEyes.update();
  }
}

void drawStandbyScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(50, 10);
  display.println(":)");
  display.setCursor(10, 30);
  display.println("Rocky standby");
  display.setCursor(20, 50);
  display.println("touch to wake");
  display.display();
  standbyScreenDrawn = true;
}

void ensureLegsHomed() {
  if (legsHomed || !powerStatus) {
    return;
  }
  Wire.setClock(100000);
  robot.home();
  Wire.setClock(400000);
  legsHomed = true;
  refreshEyesAfterMove();
}

void updateProximityState() {
  if (!powerStatus || !legsHomed) {
    if (proximityBlocked) {
      proximityBlocked = false;
      roboEyes.setMood(DEFAULT);
      roboEyes.open();
      if (!g_servoMotionActive) {
        roboEyes.update();
      }
    }
    return;
  }

  if (isProximityActive()) {
    if (!proximityBlocked) {
      enterProximityMode();
    } else if (!g_servoMotionActive) {
      roboEyes.update();
    }
  } else if (proximityBlocked) {
    exitProximityMode();
  }
}

void enterProximityMode() {
  proximityBlocked = true;
  roboEyes.setMood(ANGRY);
  roboEyes.close();
  buzzer.play(BUZZ_SCREAM);
  Wire.setClock(100000);
  robot.closeLegs();
  Wire.setClock(400000);
  refreshEyesAfterMove();
}

void exitProximityMode() {
  proximityBlocked = false;
  roboEyes.setMood(DEFAULT);
  roboEyes.open();
  Wire.setClock(100000);
  robot.home();
  Wire.setClock(400000);
  refreshEyesAfterMove();
}

void enterDeepSleep() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 30);
  display.println("Rocky sleeping");
  display.display();

  buzzer.playBlocking(BUZZ_SLEEP);

  Wire.setClock(100000);
  robot.off();
  Wire.setClock(400000);
  delay(200);

  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  detachInterrupt(digitalPinToInterrupt(TOUCH_PIN));
  esp_deep_sleep_enable_gpio_wakeup(1ULL << TOUCH_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}

void runRandomDanceMove() {
  if (!canMoveLegs()) {
    return;
  }
  Wire.setClock(100000);
  switch (random(0, 8)) {
    case 0: robot.dance(); break;
    case 1: robot.happyBounce(); break;
    case 2: robot.shuffle(); break;
    case 3: robot.moonwalkStep(); break;
    case 4: robot.slideLeft(2); break;
    case 5: robot.slideRight(2); break;
    case 6: robot.stretch(); break;
    default: robot.curiousTilt(); break;
  }
  Wire.setClock(400000);
  refreshEyesAfterMove();
}

void runRandomIdleLegMove() {
  if (!canMoveLegs()) {
    return;
  }
  Wire.setClock(100000);
  switch (random(0, 12)) {
    case 0:  robot.idle(); break;
    case 1:  robot.happyBounce(); break;
    case 2:  robot.curiousTilt(); break;
    case 3:  robot.walkForward(1); break;
    case 4:  robot.walkBackward(1); break;
    case 5:  robot.slideLeft(1); break;
    case 6:  robot.slideRight(1); break;
    case 7:  robot.shuffle(); break;
    case 8:  robot.moonwalkStep(); break;
    case 9:  robot.stretch(); break;
    case 10: robot.dance(); break;
    default: robot.sadSlump(); break;
  }
  Wire.setClock(400000);
  refreshEyesAfterMove();
}

void triggerShortTouchTask() {
  if (!canMoveLegs()) {
    roboEyes.setMood(ANGRY);
    roboEyes.blink();
    refreshEyesAfterMove();
    return;
  }

  buzzer.play(BUZZ_R2D2);
  roboEyes.setHeight(36, 36);
  roboEyes.setWidth(36, 36);
  roboEyes.setBorderradius(8, 8);

  if (isSleeping) {
    roboEyes.setMood(ANGRY);
    isSleeping = false;
    robot.angryStomp();
  } else if (mood == 1) {
    roboEyes.setMood(DEFAULT);
    roboEyes.anim_laugh();
    mood = 0;
    switch (random(0, 4)) {
      case 0: robot.stretch(); break;
      case 1: robot.slideLeft(1); break;
      case 2: robot.shuffle(); break;
      default: robot.idle(); break;
    }
  } else {
    roboEyes.setMood(HAPPY);
    roboEyes.blink();
    mood = 1;
    robot.happyBounce();
  }
  refreshEyesAfterMove();
}

void triggerDoubleTapDance() {
  if (!canMoveLegs()) {
    roboEyes.setMood(ANGRY);
    roboEyes.blink();
    refreshEyesAfterMove();
    return;
  }
  buzzer.play(BUZZ_FANFARE);
  roboEyes.setMood(HAPPY);
  roboEyes.setWidth(40, 40);
  roboEyes.setHeight(40, 40);
  roboEyes.anim_laugh();
  runRandomDanceMove();
}

void triggerLongTouchTask() {
  if (!canMoveLegs()) {
    return;
  }

  sound = !sound;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 50);
  display.println("sound mode:");
  display.setCursor(88, 50);
  display.print(sound ? "ON" : "OFF");
  display.display();

  unsigned long soundBannerUntil = millis() + 1000;

  roboEyes.setMood(HAPPY);

  if (!hasPlayed) {
    roboEyes.setWidth(46, 46);
    roboEyes.setHeight(50, 50);
    roboEyes.setBorderradius(12, 12);
    roboEyes.setAutoblinker(OFF, 0, 0);
    roboEyes.anim_laugh();
    roboEyes.blink(0, 2);
    buzzer.play(BUZZ_MELODY);
    hasPlayed = true;
    robot.dance();
  } else {
    roboEyes.setMood(TIRED);
    buzzer.play(BUZZ_SAD);
    robot.sadSlump();
  }

  while (millis() < soundBannerUntil) {
    buzzer.update();
    serviceOta();
    if (!g_servoMotionActive) {
      roboEyes.update();
    }
    delay(10);
  }
  display.clearDisplay();
  refreshEyesAfterMove();
}

void triggerTooManyTapsTask() {
  if (!canMoveLegs()) {
    roboEyes.setMood(ANGRY);
    refreshEyesAfterMove();
    return;
  }

  roboEyes.setWidth(36, 36);
  roboEyes.setHeight(36, 36);
  roboEyes.setBorderradius(8, 8);
  roboEyes.setMood(ANGRY);

  if (tap > 3) {
    buzzer.play(BUZZ_SCREAM);
    roboEyes.setVFlicker(OFF, 0);
    tap = 0;
    robot.angryStomp();
  } else {
    roboEyes.anim_confused();
    if (flag) {
      buzzer.play(BUZZ_CONFUSED);
      roboEyes.setVFlicker(ON, 10);
      for (int i = 0; i < 8; i++) {
        buzzer.update();
        if (!g_servoMotionActive) {
          roboEyes.update();
        }
        delay(10);
      }
      roboEyes.setVFlicker(OFF, 0);
    } else {
      buzzer.play(BUZZ_TAUNT);
      roboEyes.setHFlicker(ON, 10);
      for (int i = 0; i < 8; i++) {
        buzzer.update();
        if (!g_servoMotionActive) {
          roboEyes.update();
        }
        delay(10);
      }
      roboEyes.setHFlicker(OFF, 0);
      tap++;
    }
    flag = !flag;
    robot.curiousTilt();
  }
  refreshEyesAfterMove();
}

void handleIdleAnimations() {
  if (!canMoveLegs()) {
    if (!g_servoMotionActive) {
      roboEyes.update();
    }
    return;
  }

  unsigned long currentMillis = millis();

  if (currentMillis >= nextLegMoveTime) {
    if (sound) {
      buzzer.play(BUZZ_R2D2);
    }
    runRandomIdleLegMove();
    nextLegMoveTime = millis() + random(7000, 14000);
  }

  if (currentMillis >= nextEyeBeatTime) {
    nextEyeBeatTime = currentMillis + random(3000, 6000);
    idleEyePhase = (idleEyePhase + 1) % 6;
    switch (idleEyePhase) {
      case 0:
        roboEyes.setAutoblinker(ON, 3, 2);
        roboEyes.blink(0, 1);
        break;
      case 1:
        roboEyes.setMood(TIRED);
        break;
      case 2:
        roboEyes.setCuriosity(ON);
        roboEyes.setPosition(W);
        break;
      case 3:
        roboEyes.setPosition(E);
        roboEyes.setMood(DEFAULT);
        break;
      case 4:
        roboEyes.setPosition(DEFAULT);
        roboEyes.setMood(HAPPY);
        break;
      default:
        roboEyes.setCuriosity(OFF);
        roboEyes.setMood(DEFAULT);
        break;
    }
    if (!g_servoMotionActive) {
      roboEyes.update();
    }
  }

  if (currentMillis >= eventTimer + 45000 && !eventPlayed[14]) {
    eventPlayed[14] = true;
    roboEyes.setMood(TIRED);
    isSleeping = true;
    roboEyes.setHeight(28, 28);
    roboEyes.setWidth(36, 36);
    if (!g_servoMotionActive) {
      roboEyes.update();
    }
  }
  if (currentMillis >= eventTimer + 52000 && !eventPlayed[19]) {
    eventPlayed[19] = true;
    isSleeping = false;
    roboEyes.setHeight(36, 36);
    roboEyes.setMood(DEFAULT);
    roboEyes.open();
    if (!g_servoMotionActive) {
      roboEyes.update();
    }
  }

  if (currentMillis >= eventTimer + nextResetDelay) {
    eventTimer = currentMillis;
    nextResetDelay = random(50000L, 80000L);
    roboEyes.setCuriosity(OFF);
    roboEyes.setAutoblinker(OFF, 3, 2);
    roboEyes.setIdleMode(OFF, 5, 2);
    memset(eventPlayed, 0, sizeof(eventPlayed));
    hasPlayed = false;
    tap = 0;
  }
}
