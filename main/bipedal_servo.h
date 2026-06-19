#ifndef BIPEDAL_SERVO_H
#define BIPEDAL_SERVO_H

#include <ESP32Servo.h>
#include <Arduino.h>

class BipedalRobot {
private:
    Servo servoLeftHip;
    Servo servoRightHip;
    Servo servoLeftFoot;
    Servo servoRightFoot;

    int pinLeftHip, pinRightHip, pinLeftFoot, pinRightFoot;
    
    // Store current logical angles (before applying trim) for smooth transitions
    float currentLeftHip = 100.0;
    float currentRightHip = 90.0;
    float currentLeftFoot = 90.0;
    float currentRightFoot = 90.0;

    // Hardware calibration trims (offsets). 
    int trimLeftHip = 0;
    int trimRightHip = 0;
    int trimLeftFoot = 0;
    int trimRightFoot = 0;

    // Telemetry flag
    bool telemetryEnabled = false;

    // Callback function to run during servo movement steps
    void (*updateCallback)() = nullptr;

    // Motion scale factor (can be adjusted via BLE/Python)
    float motionScale = 0.7; // Default 30% toned down

    /**
     * The Engine: Helper to smoothly transition servos.
     * This slices a big movement into small 20ms steps, preventing the robot from jerking and falling.
     */
    void moveServos(int targetLeftHip, int targetRightHip, int targetLeftFoot, int targetRightFoot, int duration_ms) {
        int steps = duration_ms / 20; // 20ms per step (50Hz updates)
        if (steps <= 0) steps = 1;

        float incLeftHip = (targetLeftHip - currentLeftHip) / steps;
        float incRightHip = (targetRightHip - currentRightHip) / steps;
        float incLeftFoot = (targetLeftFoot - currentLeftFoot) / steps;
        float incRightFoot = (targetRightFoot - currentRightFoot) / steps;

        if (telemetryEnabled) {
            Serial.print("TELEMETRY_START:");
            Serial.println(duration_ms);
        }

        for (int i = 0; i < steps; i++) {
            currentLeftHip += incLeftHip;
            currentRightHip += incRightHip;
            currentLeftFoot += incLeftFoot;
            currentRightFoot += incRightFoot;

            // Apply trim offsets when writing to physical hardware
            servoLeftHip.write(round(currentLeftHip) + trimLeftHip);
            servoRightHip.write(round(currentRightHip) + trimRightHip);
            servoLeftFoot.write(round(currentLeftFoot) + trimLeftFoot);
            servoRightFoot.write(round(currentRightFoot) + trimRightFoot);
            
            if (telemetryEnabled && (i % 5 == 0)) { // Broadcast every 100ms
                Serial.print("TELEMETRY:");
                Serial.print(round(currentLeftHip)); Serial.print(",");
                Serial.print(round(currentRightHip)); Serial.print(",");
                Serial.print(round(currentLeftFoot)); Serial.print(",");
                Serial.println(round(currentRightFoot));
            }

            // Run user-registered update callback if available (e.g. to update OLED eyes)
            if (updateCallback != nullptr) {
                updateCallback();
            }

            delay(20);
        }

        // Ensure final exact position without float rounding errors
        currentLeftHip = targetLeftHip;
        currentRightHip = targetRightHip;
        currentLeftFoot = targetLeftFoot;
        currentRightFoot = targetRightFoot;
        
        servoLeftHip.write(currentLeftHip + trimLeftHip);
        servoRightHip.write(currentRightHip + trimRightHip);
        servoLeftFoot.write(currentLeftFoot + trimLeftFoot);
        servoRightFoot.write(currentRightFoot + trimRightFoot);
    }

    /**
     * Scale targets relative to neutral positions and move.
     */
    void moveServosScaled(int targetLeftHip, int targetRightHip, int targetLeftFoot, int targetRightFoot, int duration_ms) {
        int scaledLeftHip = 100 + round((targetLeftHip - 100) * motionScale);
        int scaledRightHip = 90 + round((targetRightHip - 90) * motionScale);
        int scaledLeftFoot = 90 + round((targetLeftFoot - 90) * motionScale);
        int scaledRightFoot = 90 + round((targetRightFoot - 90) * motionScale);
        
        moveServos(scaledLeftHip, scaledRightHip, scaledLeftFoot, scaledRightFoot, duration_ms);
    }

public:
    BipedalRobot() {}

    /**
     * Initializes the robot. Call this once inside Arduino setup().
     */
    void begin(int pinLH, int pinRH, int pinLF, int pinRF) {
        pinLeftHip = pinLH;
        pinRightHip = pinRH;
        pinLeftFoot = pinLF;
        pinRightFoot = pinRF;

        // ESP32Servo requires timers allocated specifically
        ESP32PWM::allocateTimer(0);
        ESP32PWM::allocateTimer(1);
        ESP32PWM::allocateTimer(2);
        ESP32PWM::allocateTimer(3);

        servoLeftHip.setPeriodHertz(50);
        servoRightHip.setPeriodHertz(50);
        servoLeftFoot.setPeriodHertz(50);
        servoRightFoot.setPeriodHertz(50);

        servoLeftHip.attach(pinLeftHip, 500, 2400);
        servoRightHip.attach(pinRightHip, 500, 2400);
        servoLeftFoot.attach(pinLeftFoot, 500, 2400);
        servoRightFoot.attach(pinRightFoot, 500, 2400);
        
        home(); // Stand up straight on boot
    }

    /**
     * Registers a callback function to be called during servo movement increments.
     */
    void registerUpdateCallback(void (*callback)()) {
        updateCallback = callback;
    }

    /**
     * Set dynamic motion scaling factor (1.0 = raw angles, < 1.0 = toned down, > 1.0 = toned up).
     */
    void setMotionScale(float scale) {
        if (scale < 0.0) scale = 0.0;
        motionScale = scale;
    }

    /**
     * Enable or disable live telemetry broadcasting over Serial.
     */
    void setTelemetry(bool enabled) {
        telemetryEnabled = enabled;
    }

    /**
     * Public wrapper to test specific servo angles interactively.
     */
    void directMove(int targetLH, int targetRH, int targetLF, int targetRF, int duration_ms) {
        moveServosScaled(targetLH, targetRH, targetLF, targetRF, duration_ms);
    }

    /**
     * Set Hardware Calibration Offsets (Trims).
     */
    void setTrim(int offsetLeftHip, int offsetRightHip, int offsetLeftFoot, int offsetRightFoot) {
        trimLeftHip = offsetLeftHip;
        trimRightHip = offsetRightHip;
        trimLeftFoot = offsetLeftFoot;
        trimRightFoot = offsetRightFoot;
        home(); // Re-apply home stance with new trims
    }

    /**
     * Stand up straight (State 0). All logical angles set to 90 degrees.
     */
    void home() {
        moveServos(100, 90, 90, 90, 500); // Raw move to absolute home
    }

    /**
     * Off State. Spreads feet so it lays flat/relaxes.
     * Parks Right Foot (GPIO3) at 180 and Left Foot (GPIO0) at 0 degrees.
     */
    void off() {
        moveServos(100, 90, 0, 180, 1000); // Raw move to absolute park
    }

    // ==========================================
    // WALKING MECHANICS (Based on Algorithm in my_walk.ino)
    // ==========================================

    /**
     * Walk forward.
     */
    void walkForward(int steps, int speed = 300) {
        for (int i = 0; i < steps; i++) {
            // Right Leg Step
            // 1. Lean left, lift right foot
            moveServosScaled(100, 90, 160, 135, speed);
            // 2. Swing right hip forward, right foot drops slightly
            moveServosScaled(100, 45, 90, 80, speed);
            // 3. Stabilization
            moveServosScaled(100, 90, 90, 90, speed);

            // Left Leg Step
            // 4. Lean right, lift left foot
            moveServosScaled(100, 90, 45, 20, speed);
            // 5. Left hip swings forward, left foot drops slightly
            moveServosScaled(145, 90, 90, 100, speed);
            // 6. Stabilization
            moveServosScaled(100, 90, 90, 90, speed);
        }
    }

    void walkBackward(int steps, int speed = 300) {
        for (int i = 0; i < steps; i++) {
            // Right Leg Step
            // 1. Lean left, lift right foot
            moveServosScaled(100, 90, 160, 135, speed);
            // 2. Swing right hip backward, right foot drops slightly
            moveServosScaled(100, 135, 90, 80, speed);
            // 3. Stabilization
            moveServosScaled(100, 90, 90, 90, speed);

            // Left Leg Step
            // 4. Lean right, lift left foot
            moveServosScaled(100, 90, 45, 20, speed);
            // 5. Left hip swings backward, left foot drops slightly
            moveServosScaled(55, 90, 90, 100, speed);
            // 6. Stabilization
            moveServosScaled(100, 90, 90, 90, speed);
        }
    }



    // ==========================================
    // EMO / EXPRESSION ANIMATIONS
    // ==========================================

    /**
     * Happy bounce! Robot jumps/bobs up and down excitedly.
     */
    void happyBounce() {
        for(int i = 0; i < 3; i++) {
            // Spring up on toes (inward tilt)
            moveServosScaled(100, 90, 45, 135, 150);
            // Drop down into a squat (outward tilt)
            moveServosScaled(100, 90, 160, 20, 150);
        }
        home();
    }

    /**
     * Sad slump. Hips drop, shoulders/feet slouch down slowly.
     */
    void sadSlump() {
        // Squat and lean forward slightly
        moveServosScaled(120, 70, 45, 135, 1500); 
        delay(1000); // Sit in sadness
        home();
    }

    /**
     * Angry stomp. Raises a foot and slams it down fast.
     */
    void angryStomp() {
        // Lean left, raise right leg high
        moveServosScaled(100, 90, 160, 135, 400); 
        delay(200);
        // Slam right leg down flat violently (fast speed)
        moveServosScaled(100, 90, 90, 90, 80); 
        delay(300);
        
        // Lean right, raise left leg high
        moveServosScaled(100, 90, 45, 20, 400);
        delay(200);
        // Slam left leg down
        moveServosScaled(100, 90, 90, 90, 80);
        
        home();
    }

    /**
     * Curious tilt. Tilts to the side and slightly turns, like a confused dog.
     */
    void curiousTilt() {
        // Lean right and turn hips slightly
        moveServosScaled(120, 70, 45, 20, 600);
        delay(800);
        // Snap back
        home();
    }

    // ==========================================
    // GENERAL ANIMATIONS (OTTO-STYLE)
    // ==========================================

    void stretch() {
        moveServosScaled(100, 90, 160, 20, 1500); // Tall stretch (outward tilt)
        delay(500);
        moveServosScaled(100, 90, 45, 135, 1500); // Deep squat (inward tilt)
        delay(500);
        moveServosScaled(55, 135, 45, 135, 1000); // Twist left while squatting
        delay(500);
        moveServosScaled(145, 45, 45, 135, 1000); // Twist right while squatting
        delay(500);
        home();
    }

    void idle() {
        // Gentle "breathing" / shifting weight
        moveServosScaled(100, 90, 110, 70, 1000); // Outward
        moveServosScaled(100, 90, 70, 110, 1000); // Inward
        moveServosScaled(100, 90, 90, 90, 800);
        moveServosScaled(100, 90, 120, 110, 800); // Lean left slightly
        moveServosScaled(100, 90, 70, 70, 800); // Lean right slightly
        home();
    }

    void dance() {
        // Moonwalk-like shuffle
        moveServosScaled(100, 90, 160, 135, 250); // Lean left
        moveServosScaled(145, 135, 160, 135, 250); // Twist hips
        moveServosScaled(145, 135, 90, 90, 250); // Plant
        moveServosScaled(145, 135, 45, 20, 250); // Lean right
        moveServosScaled(55, 45, 45, 20, 250); // Twist hips
        moveServosScaled(55, 45, 90, 90, 250); // Plant
        home();

        // Hip wiggle / Jitter
        for(int i = 0; i < 4; i++) {
            moveServosScaled(55, 135, 90, 90, 200);
            moveServosScaled(145, 45, 90, 90, 200);
        }
        home();
    }

    // ==========================================
    // HOW TO EXPAND & CREATE NEW MOVES
    // ==========================================
    /*
     * Want to add your own moves? It's simple!
     * 1. Copy the `customMoveTemplate()` function below.
     * 2. Rename it to whatever you want (e.g., `void ninjaKick()`).
     * 3. Call `moveServos(LeftHip, RightHip, LeftFoot, RightFoot, Duration)` in sequence.
     * 4. Call `home()` at the end so the robot resets its stance.
     * 
     * REMEMBER:
     * - Hip Angles: 90 is centered. < 90 rotates backward, > 90 rotates forward.
     * - Foot Angles: 90 is flat. < 90 lifts the heel/tilts body, > 90 lifts the toe/clears ground.
     * - Duration: 300 is standard speed. 100 is fast snap. 1500 is super slow.
     */

    void customMoveTemplate() {
        // Step 1: Tell servos where to go and how fast
        // moveServos(90, 90, 90, 90, 500); 

        // Step 2: Add a delay if you want it to hold the pose
        // delay(500);

        // Step 3: Next movement...
        // moveServos(120, 60, 90, 90, 500);

        // Always end with home so it doesn't stay awkwardly frozen
        home();
    }
};

#endif
