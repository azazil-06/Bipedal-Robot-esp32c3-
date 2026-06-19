#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

enum BuzzSound : uint8_t {
  BUZZ_NONE = 0,
  BUZZ_R2D2,
  BUZZ_SAD,
  BUZZ_SCREAM,
  BUZZ_HAPPY,
  BUZZ_SURPRISE,
  BUZZ_ANGRY,
  BUZZ_SLEEP,
  BUZZ_CHIRP,
  BUZZ_FANFARE,
  BUZZ_CONFUSED,
  BUZZ_TAUNT,
  BUZZ_VICTORY,
  BUZZ_HELLO,
  BUZZ_ALERT,
  BUZZ_MELODY,
  BUZZ_PROX_GRUMBLE
};

struct BuzzStep {
  uint16_t freq;
  uint16_t ms;
  uint16_t gap;
};

#define BUZZ_QUEUE_MAX 40

class BuzzerPlayer {
public:
  void begin(uint8_t pin) {
    buzzPin = pin;
    pinMode(buzzPin, OUTPUT);
    digitalWrite(buzzPin, LOW);
    clear();
  }

  bool isPlaying() const { return active; }

  void stop() {
    noTone(buzzPin);
    clear();
  }

  void play(BuzzSound id) {
    clear();
    buildScript(id);
    if (stepCount == 0) {
      return;
    }
    active = true;
    stepIndex = 0;
    inGap = false;
    phaseStart = millis();
    startCurrentPhase();
  }

  void playTone(uint16_t freq, uint16_t durationMs) {
    clear();
    if (durationMs == 0) {
      return;
    }
    queue[0] = {freq, durationMs, 10};
    stepCount = 1;
    active = true;
    stepIndex = 0;
    inGap = false;
    phaseStart = millis();
    startCurrentPhase();
  }

  void update() {
    if (!active) {
      return;
    }
    unsigned long now = millis();
    uint16_t wait = inGap ? queue[stepIndex].gap : queue[stepIndex].ms;
    if (now - phaseStart < wait) {
      return;
    }
    if (!inGap) {
      noTone(buzzPin);
      if (queue[stepIndex].gap > 0) {
        inGap = true;
        phaseStart = now;
        return;
      }
    }
    inGap = false;
    stepIndex++;
    if (stepIndex >= stepCount) {
      stop();
      return;
    }
    phaseStart = now;
    startCurrentPhase();
  }

  void playBlocking(BuzzSound id, uint16_t timeoutMs = 8000) {
    play(id);
    unsigned long start = millis();
    while (isPlaying() && (millis() - start < timeoutMs)) {
      update();
      delay(1);
    }
  }

private:
  uint8_t buzzPin = 4;
  BuzzStep queue[BUZZ_QUEUE_MAX];
  uint8_t stepCount = 0;
  uint8_t stepIndex = 0;
  bool active = false;
  bool inGap = false;
  unsigned long phaseStart = 0;

  void clear() {
    noTone(buzzPin);
    stepCount = 0;
    stepIndex = 0;
    active = false;
    inGap = false;
  }

  void enqueue(uint16_t freq, uint16_t ms, uint16_t gap = 15) {
    if (stepCount >= BUZZ_QUEUE_MAX) {
      return;
    }
    queue[stepCount++] = {freq, ms, gap};
  }

  void enqueueRest(uint16_t ms) {
    enqueue(0, 0, ms);
  }

  void startCurrentPhase() {
    if (!inGap && queue[stepIndex].freq > 0 && queue[stepIndex].ms > 0) {
      tone(buzzPin, queue[stepIndex].freq, queue[stepIndex].ms);
    }
  }

  void buildScript(BuzzSound id) {
    switch (id) {
      case BUZZ_CHIRP:
        enqueue(2800, 40);
        enqueue(3200, 40);
        enqueue(3600, 50);
        break;

      case BUZZ_FANFARE:
        enqueue(2093, 80, 20);
        enqueue(2637, 80, 20);
        enqueue(3136, 80, 20);
        enqueue(4186, 120, 30);
        break;

      case BUZZ_CONFUSED:
        enqueue(1800, 60);
        enqueue(1400, 60);
        enqueue(1800, 60);
        enqueue(1400, 80);
        break;

      case BUZZ_TAUNT:
        enqueue(400, 80);
        enqueue(600, 80);
        enqueue(400, 80);
        enqueue(900, 100);
        break;

      case BUZZ_VICTORY:
        enqueue(523, 100, 30);
        enqueue(659, 100, 30);
        enqueue(784, 100, 30);
        enqueue(1047, 180, 40);
        break;

      case BUZZ_HELLO:
        enqueue(2400, 50);
        enqueue(3000, 50);
        enqueue(2400, 50);
        enqueue(3600, 90);
        break;

      case BUZZ_ALERT:
        enqueue(2000, 40, 40);
        enqueue(2000, 40, 40);
        enqueue(2500, 60);
        break;

      case BUZZ_PROX_GRUMBLE:
        enqueue(120, 50);
        enqueue(90, 50);
        enqueue(140, 60);
        enqueue(100, 80);
        break;

      case BUZZ_HAPPY:
        enqueue(2093, 45, 20);
        enqueue(2637, 45, 20);
        enqueue(3136, 45, 20);
        enqueue(4186, 70, 30);
        break;

      case BUZZ_SURPRISE:
        enqueue(2200, 35, 30);
        enqueue(3200, 25, 15);
        enqueue(3600, 25, 15);
        enqueue(4000, 25, 15);
        enqueue(4400, 40);
        break;

      case BUZZ_ANGRY:
        for (int i = 0; i < 6; i++) {
          enqueue(180 + (i * 7), 35, 25);
        }
        break;

      case BUZZ_SAD:
        for (int f = 2800; f >= 1400; f -= 120) {
          enqueue((uint16_t)f, 35, 12);
        }
        break;

      case BUZZ_SCREAM:
        for (int i = 0; i < 7; i++) {
          enqueue(3400 + (i * 120), 28, 18);
        }
        enqueue(800, 200, 30);
        break;

      case BUZZ_SLEEP:
        for (int s = 0; s < 2; s++) {
          for (int f = 1600; f <= 1900; f += 50) {
            enqueue((uint16_t)f, 28, 8);
          }
          enqueueRest(200);
          for (int f = 1900; f >= 1600; f -= 50) {
            enqueue((uint16_t)f, 28, 8);
          }
          enqueueRest(350);
        }
        break;

      case BUZZ_MELODY: {
        const uint16_t notes[] = {262, 294, 330, 349, 392, 440, 494, 523};
        for (int i = 0; i < 8; i++) {
          enqueue(notes[i], 90, 25);
        }
        break;
      }

      case BUZZ_R2D2:
        buildR2D2Variant(random(0, 3));
        break;

      default:
        break;
    }
  }

  void buildR2D2Variant(int type) {
    if (type == 0) {
      for (int j = 0; j < 2; j++) {
        for (int f = 2400; f < 3800; f += 200) {
          enqueue((uint16_t)f, 25, 8);
        }
        enqueueRest(70);
      }
    } else if (type == 1) {
      for (int i = 0; i < 4; i++) {
        enqueue(3600, 22, 18);
        enqueue(3200, 22, 18);
      }
    } else {
      for (int f = 2000; f < 3200; f += 100) {
        enqueue((uint16_t)f, 22, 8);
      }
      enqueueRest(35);
      enqueue(3800, 65, 30);
    }
  }
};

#endif
