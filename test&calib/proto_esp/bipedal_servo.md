# Bipedal Servo Library Documentation

This document outlines the usage, features, and algorithmic theory behind the custom `BipedalRobot` library designed for the ESP32-C3 Supermini.

## 1. Quick Start: How to Initialize and Use

The library is entirely self-contained within `bipedal_servo.h`. It utilizes the `ESP32Servo` library for precise PWM control and smooth transitions.

### Step 1: Include and Instantiate
Include the header file and create an instance of the `BipedalRobot` class globally in your Arduino sketch.

```cpp
#include "bipedal_servo.h"

BipedalRobot robot;
```

### Step 2: Initialize in `setup()`
In your Arduino `setup()` function, call `begin()` with your actual ESP32-C3 GPIO pins for the left hip, right hip, left foot, and right foot.

```cpp
void setup() {
  // robot.begin(LeftHipPin, RightHipPin, LeftFootPin, RightFootPin)
  robot.begin(0, 1, 2, 3); 
  
  // Optional: Apply mechanical trims (offsets)
  // Because physical servos are rarely perfectly aligned at exactly 90 degrees,
  // you can apply minor software offsets here so the robot stands perfectly straight.
  robot.setTrim(0, -5, 3, 0); 
}
```

### Step 3: Call Actions in `loop()`
You can now call any of the built-in walking mechanics or animations.

```cpp
void loop() {
  robot.walkForward(3);   // Take 3 full strides forward
  robot.happyBounce();    // Do a happy jump
  delay(1000);
}
```

---

## 2. Library Features & Animations

The library includes a robust set of movements broken down into walking mechanics and personality-driven "Emo" animations.

### Core Walking Mechanics
* `home()`: Returns all servos to exactly 90° (Standing Position).
* `off()`: Spreads the feet to 180°, relaxing the robot so it lays flat.
* `walkForward(steps, speed)`: Executes the forward walking stride.
* `walkBackward(steps, speed)`: Executes the reverse walking stride.
* `turnLeft(steps, speed)`: Anchors the left side while swinging the right side to pivot left.
* `turnRight(steps, speed)`: Anchors the right side while swinging the left side to pivot right.

### Emo / Personality Animations
* `happyBounce()`: The robot excitedly springs up on its toes and bobs down.
* `sadSlump()`: The hips drop slowly while the feet slouch outward.
* `angryStomp()`: The robot raises a leg high and slams it down violently.
* `curiousTilt()`: The robot tilts sideways and turns slightly, like a confused dog.

### General Animations (Otto-Style)
* `dance()`: A mix of moonwalk shuffles, tiptoe bounces, and rapid hip wiggles.
* `stretch()`: A slow, dramatic movement pushing the servos to vertical extremes and dropping into a deep squat.
* `idle()`: A gentle "breathing" effect where the robot subtly shifts its weight side to side (perfect for standby modes).

---

## 3. How to Expand and Create New Moves

Creating your own custom animations is very simple. The core engine runs on a single powerful function:
`moveServos(targetLeftHip, targetRightHip, targetLeftFoot, targetRightFoot, duration_ms)`

### The Logic of the Angles
* **Hips (90° is centered)**
  * `< 90°` Rotates the hip backward.
  * `> 90°` Rotates the hip forward.
* **Feet (90° is flat on the ground)**
  * `< 90°` Lifts the heel, digging the toe down (tilts the body over).
  * `> 90°` Lifts the toe edge, clearing ground friction.

### Creating a Custom Sequence
Inside `bipedal_servo.h`, you can define new moves by simply chaining `moveServos` commands together:

```cpp
void ninjaKick() {
    // 1. Tell servos where to go and how fast (e.g., 300ms)
    moveServos(90, 90, 120, 120, 300); 
    
    // 2. Add a delay if you want it to hold the pose
    delay(500);
    
    // 3. Next movement (fast snap, 100ms)
    moveServos(120, 60, 90, 90, 100);
    
    // 4. Always end with home() so it returns to a stable, flat-footed stance
    home();
}
```

---

## 4. Algorithmic Theory: Step-by-Step Motion Sequence

*This is the core logic that the `walkForward` and `walkBackward` functions are based on.*

When starting from a static, flat-footed stance, the robot cannot simply push a leg forward, or it will trip. It must intentionally break its balance to establish a walking cycle.

**State 0: Standing Position (Default Rest)**
* Hips: Both centered at 90° (Legs square with the chassis).
* Feet: Both centered at 90° (Feet flat on the ground).
* Status: Weight is evenly distributed 50/50.

**Step 1: The Initial Weight Shift (The Setup)**
Before taking a step forward with the right foot, the robot must clear the weight off it.
* Left Foot rotates down to 70°, forcing the heel up and tilting the upper body weight over to the right.
* Right Foot simultaneously rotates up to 110°, lifting its front toe edge completely off the ground to eliminate surface friction.
* Status: The robot is now dynamically balancing on the rear heel edge of the left foot.

**Step 2: Driving the Stride (The Swing)**
With the right foot floating free of friction, the hips can now execute the step.
* Left Hip rotates forward to 115°. Because the left foot is pinned to the ground, this action physically pushes the left side of the chassis forward.
* Right Hip rotates backward to 65°. Because the right leg is airborne, this action swings the entire right foot module forward into the air.
* Status: The right foot is extended forward ahead of the body, still airborne.

**Step 3: Ground Catch & Weight Transfer (The Plant)**
The robot must transition its weight onto the leading foot.
* Right Foot snaps back to flat (90°).
* Left Foot returns to neutral (90°), lowering the body back down.
* Status: Both feet are on the ground, but the right foot is planted forward. 

**Step 4: The Symmetrical Reset (Bringing the Rear Foot Up)**
To continue walking, the trailing left foot must now be pulled forward.
* Right Foot rotates down to 70° to tilt the body weight onto itself.
* Left Foot rotates up to 110° to lift its toe edge and clear ground friction.
* Status: The robot is now mirrored and ready to swing the trailing leg forward (repeating Step 2 mirrored).