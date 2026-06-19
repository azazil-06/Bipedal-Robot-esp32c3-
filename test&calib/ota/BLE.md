# ROCKY — BLE API Reference

Bipedal robot firmware for **ESP32-C3**. Control motions, eyes, and sounds over Bluetooth Low Energy.

---

## Device identity

| Field | Value |
|--------|--------|
| **Advertised name** | `ROCKY` |
| **MCU** | ESP32-C3 |
| **Protocol** | BLE 4.x (GATT server) |

There is no custom “device ID” string in firmware. Use the identifiers below to find and bind to the correct robot:

| Identifier | How to obtain |
|------------|----------------|
| **Name** | BLE scan → look for `ROCKY` |
| **MAC address** | Scanner app / OS Bluetooth settings (e.g. `AA:BB:CC:DD:EE:FF`) — unique per board |
| **Service UUID** | `19b10000-e8f2-537e-4f6c-d104768a1214` — use to filter scans |

> **Tip:** In crowded environments, always confirm **name + service UUID** (or MAC) before writing commands.

---

## GATT layout

### Service

```
UUID: 19b10000-e8f2-537e-4f6c-d104768a1214
```

Advertised in the scan response.

### Characteristic (command + telemetry)

```
UUID: 19b10001-e8f2-537e-4f6c-d104768a1214
```

| Property | Direction | Purpose |
|----------|-----------|---------|
| **Write** | Central → ROCKY | Send command string (UTF-8) |
| **Notify** | ROCKY → Central | Sensor telemetry (~every 500 ms while connected) |
| Read / Indicate | Present | Not required for normal apps; prefer **Write + Notify** |

Descriptor: Client Characteristic Configuration (CCCD) `0x2902` — **enable notifications** to receive telemetry.

---

## Connection flow

1. **Scan** for peripheral named `ROCKY` (optional filter: service UUID above).
2. **Connect** as GATT client.
3. **Discover** service `19b10000-...` and characteristic `19b10001-...`.
4. **Subscribe** to notifications on `19b10001-...` (write `0x0001` to CCCD).
5. **Write** command bytes to the characteristic (plain text, no null terminator required if your stack sends length).
6. While connected, ROCKY shows **“remote access”** on the OLED and pauses local idle/tap logic (proximity still applies).

On disconnect, ROCKY resumes advertising as `ROCKY`.

### Power / standby

- On cold boot, ROCKY may be in **standby** until the capacitive touch pad is tapped once.
- BLE is advertised in standby; **leg motion commands are ignored** until the robot is awake (`legsHomed`).
- Wake the robot with a touch, then send `home` if needed.

---

## Sending commands

### Format

- **Encoding:** UTF-8 ASCII string  
- **Payload:** Single command per write (e.g. `walk`, not JSON)  
- **Case-sensitive**  
- **No line ending required** (`\n` / `\r` are not parsed; avoid trailing junk)

### Example (conceptual)

```
Write to 19b10001-e8f2-537e-4f6c-d104768a1214:
  "dance"
```

### nRF Connect (mobile)

1. Connect to **ROCKY**.
2. Open service `19b10000-...` → characteristic `19b10001-...`.
3. Enable **Notify**.
4. Type command in **Write** (UTF-8 string).
5. Watch **Notifications** for `sensors:...` lines.

### Python ([Bleak](https://bleak.readthedocs.io/))

```python
import asyncio
from bleak import BleakClient

NAME = "ROCKY"
SERVICE = "19b10000-e8f2-537e-4f6c-d104768a1214"
CHAR    = "19b10001-e8f2-537e-4f6c-d104768a1214"

def on_notify(_, data: bytearray):
    print(data.decode("utf-8", errors="replace"))

async def main():
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name and "ROCKY" in d.name
    )
    async with BleakClient(device) as client:
        await client.start_notify(CHAR, on_notify)
        await client.write_gatt_char(CHAR, b"walk", response=True)
        await asyncio.sleep(5)

asyncio.run(main())
```

### Web Bluetooth

Use the same service/characteristic UUIDs. Write command strings with `characteristic.writeValue()`. Subscribe to `characteristicvaluechanged` for telemetry.

---

## Telemetry (notifications)

While connected, ROCKY notifies approximately **every 500 ms**:

```
sensors:<proximity>,<touch>
```

| Field | Values | Meaning |
|--------|--------|---------|
| Proximity | `Near` \| `Clear` | `Near` = object detected (GPIO 6 LOW, debounced) |
| Touch | `Pressed` \| `Released` | Capacitive pad (GPIO 5 HIGH = pressed) |

**Example:**

```
sensors:Clear,Released
sensors:Near,Pressed
```

---

## Proximity lockout

When proximity is **Near**:

- Leg motions (`walk`, `dance`, `servo:...`, etc.) are **silently ignored**.
- These still work: **mood**, **sound**, **girl_on/off**, **scale:** (blocked — see below).

When **Clear**, leg commands work again (if the robot is awake and homed).

| Allowed while Near | Blocked while Near |
|--------------------|--------------------|
| `mood_*`, `sound_*`, `sound:freq,dur` | `walk`, `back`, `dance`, `servo:...`, `home`, `off`, … |
| `girl_on`, `girl_off` | `scale:` (motion-related; blocked with leg commands) |

---

## Command reference

### Locomotion & poses

| Command | Action |
|---------|--------|
| `walk` | Walk forward 2 steps |
| `back` | Walk backward 2 steps |
| `dance` | Dance routine |
| `bounce` | Happy bounce |
| `stomp` | Scream + angry stomp |
| `slump` | Sad slump |
| `tilt` | Curious head/hip tilt |
| `stretch` | Stretch sequence |
| `slide_left` | Slide left ×2 |
| `slide_right` | Slide right ×2 |
| `shuffle` | In-place shuffle |
| `moonwalk` | Moonwalk-style step |
| `home` | Stand pose (LH=100, RH=90, LF=90, RF=90) |
| `off` | Park pose (feet flat: LF=0, RF=180) |

### Eyes (OLED)

| Command | Action |
|---------|--------|
| `mood_happy` | Happy eyes |
| `mood_angry` | Angry eyes |
| `mood_sad` | Tired/sad eyes |
| `mood_default` | Default eyes |
| `girl_on` | “Girl” eyelash style |
| `girl_off` | Default eye style |

### Motion tuning

| Command | Format | Example | Notes |
|---------|--------|---------|--------|
| `scale` | `scale:<float>` | `scale:0.7` | Motion amplitude multiplier (default **0.7**). `1.0` = full programmed angles. |

### Direct servo (advanced)

| Command | Format |
|---------|--------|
| `servo` | `servo:<LH>,<RH>,<LF>,<RF>,<duration_ms>` |

**Example:** `servo:100,90,90,90,500` → home pose over 500 ms.

| Index | Joint | Typical home |
|-------|--------|----------------|
| LH | Left hip | 100 |
| RH | Right hip | 90 |
| LF | Left foot/ankle | 90 |
| RF | Right foot/ankle | 90 |

Angles are **logical** values before per-servo trim offsets (set in firmware, not over BLE).

### Sounds

All sounds are **non-blocking** (queued; may overlap if sent rapidly).

| Command | Description |
|---------|-------------|
| `sound_happy` | Rising arpeggio |
| `sound_sad` | Descending whistle |
| `sound_scream` | Panic chirp |
| `sound_r2d2` | Random droid chirp |
| `sound_surprise` | Double pip + sweep |
| `sound_angry` | Low rumble |
| `sound_sleep` | Snore pattern |
| `sound_chirp` | Short chirps |
| `sound_fanfare` | Fanfare |
| `sound_confused` | Wobble tones |
| `sound_taunt` | Taunt bleeps |
| `sound_victory` | Victory phrase |
| `sound_hello` | Hello chirp |
| `sound_alert` | Alert beeps |
| `sound_melody` | Short scale |
| `sound:<freq>,<ms>` | Custom tone, e.g. `sound:2000,200` |

---

## Local touch gestures (no BLE)

For reference when designing a companion app:

| Gesture | Behavior |
|---------|----------|
| 1 tap | Happy / alternate mood + motion |
| 2 taps | Random dance move + fanfare |
| 3 taps | Deep sleep (touch to wake) |
| 4+ taps | Confused / angry reactions |
| Long press | Toggle sound mode + dance/slump sequence |

---

## Hardware pins (debugging)

| GPIO | Function |
|------|----------|
| 0 | Left foot servo |
| 1 | Left hip servo |
| 2 | Right hip servo |
| 3 | Right foot servo |
| 4 | Buzzer |
| 5 | Touch input (HIGH = pressed) |
| 6 | Proximity (LOW = near) |
| 9 | I2C SCL (OLED) |
| 10 | I2C SDA (OLED) |

---

## Troubleshooting

| Issue | Check |
|-------|--------|
| Device not found | Powered on? Not in deep sleep? Scan for `ROCKY` or service UUID. |
| Writes do nothing | Robot awake (touch once)? Proximity **Clear**? Leg command vs sound/mood. |
| No telemetry | Notifications enabled on CCCD? Still connected? |
| Motion weak/strong | Send `scale:0.5` … `scale:1.0` |
| Disconnects during move | Normal on some phones; reconnect and avoid rapid-fire writes. |

---

## UUID quick copy

```
Service:        19b10000-e8f2-537e-4f6c-d104768a1214
Characteristic: 19b10001-e8f2-537e-4f6c-d104768a1214
Name:           ROCKY
```

---

## Version

Document matches firmware in this repo (`main.ino` + `buzzer.h` + `bipedal_servo.h`). If you change UUIDs or commands in code, update this file.
