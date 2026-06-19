import asyncio
import sys
import os
import time
from bleak import BleakScanner, BleakClient

# Only import msvcrt on Windows for non-blocking keypresses
if sys.platform == "win32":
    import msvcrt
else:
    msvcrt = None

# ROCKY BLE (see BLE.md)
SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1214"
CHARACTERISTIC_UUID = "19b10001-e8f2-537e-4f6c-d104768a1214"
DEVICE_NAME = "ROCKY"

# Global states
current_motion_scale = 0.7
sensor_proximity = "Unknown"
sensor_touch = "Unknown"
last_sensor_state = None

# Macro recorder variables
macro_recording = []
is_recording = False
recording_start_time = 0.0

# Map command keys to command strings and descriptions
COMMAND_MAP = {
    "1": ("walk", "Walk Forward"),
    "2": ("back", "Walk Backward"),
    "3": ("dance", "Dance"),
    "4": ("bounce", "Happy Bounce"),
    "5": ("stomp", "Angry Stomp"),
    "6": ("slump", "Sad Slump"),
    "7": ("tilt", "Curious Tilt"),
    "8": ("stretch", "Stretch"),
    "9": ("home", "Go Home (Stance Reset)"),
    "10": ("off", "Go Off (Sleep / Flat Park)"),
    "11": ("slide_left", "Slide Left x2"),
    "12": ("slide_right", "Slide Right x2"),
    "13": ("shuffle", "In-Place Shuffle"),
    "14": ("moonwalk", "Moonwalk Step"),
    "h": ("mood_happy", "Set Mood: Happy"),
    "a": ("mood_angry", "Set Mood: Angry"),
    "s": ("mood_sad", "Set Mood: Sad / Tired"),
    "d": ("mood_default", "Set Mood: Default"),
    "g_on": ("girl_on", "Enable Eyelashes (Girl Version)"),
    "g_off": ("girl_off", "Disable Eyelashes (Normal Version)"),
}

PRESET_SOUND_MAP = {
    "1": ("sound_happy", "Happy"),
    "2": ("sound_sad", "Sad"),
    "3": ("sound_scream", "Scream"),
    "4": ("sound_r2d2", "R2D2"),
    "5": ("sound_surprise", "Surprise"),
    "6": ("sound_angry", "Angry"),
    "7": ("sound_sleep", "Sleep"),
    "8": ("sound_chirp", "Chirp"),
    "9": ("sound_fanfare", "Fanfare"),
    "10": ("sound_confused", "Confused"),
    "11": ("sound_taunt", "Taunt"),
    "12": ("sound_victory", "Victory"),
    "13": ("sound_hello", "Hello"),
    "14": ("sound_alert", "Alert"),
    "15": ("sound_melody", "Melody"),
}


def normalize_command(cmd_str):
    """Strip whitespace and line endings (BLE.md: no \\n/\\r parsing on device)."""
    return cmd_str.strip().strip("\r\n")


def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')


def print_status_bar():
    prox_hint = ""
    if sensor_proximity == "Near":
        prox_hint = "  [leg motion blocked while Near]"
    print("=" * 60)
    print(f" STATUS   : Connected to {DEVICE_NAME}")
    print(f" SCALE    : {current_motion_scale}x")
    print(f" TELEMETRY: Proximity={sensor_proximity} | Touch={sensor_touch}{prox_hint}")
    if is_recording:
        print(f" RECORDER : [RECORDING ACTIVE] {len(macro_recording)} actions saved")
    else:
        print(f" RECORDER : Idle ({len(macro_recording)} actions in memory)")
    print("=" * 60)


def notification_handler(sender, data):
    global last_sensor_state, sensor_proximity, sensor_touch
    try:
        text = data.decode('utf-8', errors='replace')
        if text.startswith("sensors:"):
            state = text.split("sensors:", 1)[1].strip()
            if state != last_sensor_state:
                last_sensor_state = state
                parts = state.split(",")
                if len(parts) == 2:
                    sensor_proximity, sensor_touch = parts[0].strip(), parts[1].strip()
                    sys.stdout.write(
                        f"\r[Telemetry] Proximity: {sensor_proximity} | Touch: {sensor_touch}\n> "
                    )
                    sys.stdout.flush()
    except Exception:
        pass


async def send_command(client, cmd_str, silent=False):
    global is_recording, macro_recording, recording_start_time
    cmd_str = normalize_command(cmd_str)
    if not cmd_str:
        return
    if not silent:
        print(f"[*] Sending BLE command: {cmd_str}")
    await client.write_gatt_char(
        CHARACTERISTIC_UUID, cmd_str.encode('utf-8'), response=True
    )

    if is_recording:
        if not macro_recording:
            recording_start_time = time.time()
        macro_recording.append((cmd_str, time.time() - recording_start_time))
        if not silent:
            print(f"[+] Recorded: {cmd_str}")


def _device_matches(device, advertisement_data):
    """Match ROCKY by name; confirm service UUID when present in scan data."""
    name = device.name
    if advertisement_data:
        name = name or advertisement_data.local_name
    if not name or DEVICE_NAME not in name:
        return False
    if advertisement_data and advertisement_data.service_uuids:
        uuids = {u.lower() for u in advertisement_data.service_uuids}
        if uuids and SERVICE_UUID.lower() not in uuids:
            return False
    return True


async def scan_and_connect():
    print(f"Scanning for BLE device '{DEVICE_NAME}' (service {SERVICE_UUID[:8]}...)...")

    target_device = await BleakScanner.find_device_by_filter(
        _device_matches,
        timeout=15.0,
    )

    if not target_device:
        print(f"[-] Could not find '{DEVICE_NAME}'. Check power and that BLE is advertising.")
        return None

    print(f"[+] Found {target_device.name} ({target_device.address}). Connecting...")
    client = BleakClient(target_device)
    await client.connect()
    if client.is_connected:
        print("[+] Connected successfully!")
        return client
    print("[-] Connection failed.")
    return None


def print_menu():
    print_status_bar()
    print(" Movements:")
    print("   1: Walk Forward         2: Walk Backward")
    print("   3: Dance                4: Happy Bounce")
    print("   5: Angry Stomp          6: Sad Slump")
    print("   7: Curious Tilt         8: Stretch")
    print("   9: Go Home (Neutral)   10: Park Feet (Flat)")
    print("  11: Slide Left           12: Slide Right")
    print("  13: Shuffle              14: Moonwalk")
    print("-" * 60)
    print(" Interactive & Custom Control Modes:")
    print("   i: Interactive WASD Drive Mode")
    print("   t: Buzzer keyboard Composer Mode")
    print("   ds: Direct Servo Move   p: Play Preset Sound")
    print("   c: Play Custom Tone     scale:<val> (e.g. scale:0.7)")
    print("-" * 60)
    print(" Macro Recorder:")
    print("   r: Toggle recording (Start/Stop)")
    print("   play: Playback recorded macro")
    print("   clear: Clear macro memory")
    print("-" * 60)
    print(" Moods & Display Customization:")
    print("   h: Happy                a: Angry")
    print("   s: Sad / Tired          d: Default")
    print("   g_on: Eyelashes ON      g_off: Eyelashes OFF")
    print("-" * 60)
    print("   q: Quit")
    print("=" * 60)


async def get_key_async():
    if msvcrt is None:
        loop = asyncio.get_event_loop()
        val = await loop.run_in_executor(None, sys.stdin.readline)
        return val.strip().lower()

    while True:
        if msvcrt.kbhit():
            ch = msvcrt.getch()
            try:
                return ch.decode('utf-8').lower()
            except UnicodeDecodeError:
                if ch == b'\x1b':
                    return 'q'
                return str(ch)
        await asyncio.sleep(0.02)


async def interactive_drive(client):
    if msvcrt is None:
        print("[-] Interactive WASD mode requires Windows OS.")
        await asyncio.sleep(2)
        return

    clear_screen()
    print("=" * 60)
    print("          INTERACTIVE WASD DRIVE MODE ACTIVE")
    print("=" * 60)
    print("  Controls (single keystroke, no Enter key needed):")
    print("    [w] Walk Forward        [s] Walk Backward")
    print("    [d] Dance               [b] Happy Bounce")
    print("    [t] Curious Tilt        [x] Stretch")
    print("    [l] Sad Slump           [g] Angry Stomp")
    print("    [<] Slide Left          [>] Slide Right")
    print("    [f] Shuffle             [m] Moonwalk")
    print("    [h] Go Home             [o] Sleep/Off")
    print("    [q] or [ESC] Exit interactive drive mode")
    print("=" * 60)
    print("Listening for input...")

    drive_map = {
        'w': 'walk',
        's': 'back',
        'd': 'dance',
        'b': 'bounce',
        't': 'tilt',
        'x': 'stretch',
        'l': 'slump',
        'g': 'stomp',
        '<': 'slide_left',
        ',': 'slide_left',
        '>': 'slide_right',
        '.': 'slide_right',
        'f': 'shuffle',
        'm': 'moonwalk',
        'h': 'home',
        'o': 'off',
    }

    while True:
        key = await get_key_async()
        if key in ['q', 'esc']:
            print("\n[*] Exiting Interactive Drive Mode...")
            await asyncio.sleep(1)
            break
        if key in drive_map:
            cmd = drive_map[key]
            print(f"\n[Drive] Key '{key}' -> '{cmd}'")
            await send_command(client, cmd)
        else:
            print(f"\n[Drive] Key '{key}' not mapped.")


async def composer_mode(client):
    if msvcrt is None:
        print("[-] Composer mode requires Windows OS.")
        await asyncio.sleep(2)
        return

    clear_screen()
    print("=" * 60)
    print("          BUZZER KEYBOARD COMPOSER MODE ACTIVE")
    print("=" * 60)
    print("  Play notes on ROCKY's buzzer (sound:freq,ms):")
    print("    [1] C5 (523 Hz)        [2] D5 (587 Hz)")
    print("    [3] E5 (659 Hz)        [4] F5 (698 Hz)")
    print("    [5] G5 (784 Hz)        [6] A5 (880 Hz)")
    print("    [7] B5 (988 Hz)        [8] C6 (1047 Hz)")
    print("    [q] Exit composer mode")
    print("=" * 60)
    print("Play notes now...")

    notes_map = {
        '1': 523,
        '2': 587,
        '3': 659,
        '4': 698,
        '5': 784,
        '6': 880,
        '7': 988,
        '8': 1047,
    }

    while True:
        key = await get_key_async()
        if key == 'q':
            print("\n[*] Exiting Composer Mode...")
            await asyncio.sleep(1)
            break
        if key in notes_map:
            freq = notes_map[key]
            cmd = f"sound:{freq},150"
            print(f"\n[Composer] Note '{key}' -> {freq} Hz")
            await send_command(client, cmd, silent=True)
        else:
            print(f"\n[Composer] Key '{key}' has no note mapped.")


async def play_preset_sound(client):
    print("\nPreset Sounds (BLE.md):")
    for key, (_, label) in PRESET_SOUND_MAP.items():
        print(f"  {key}: {label}")
    choice = input("Select sound preset (1-15): ").strip()
    if choice in PRESET_SOUND_MAP:
        cmd, label = PRESET_SOUND_MAP[choice]
        print(f"[*] Playing: {label}")
        await send_command(client, cmd)
    else:
        print("[-] Invalid selection.")


async def direct_servo_move(client):
    try:
        print("\nEnter target angles (0 - 180 degrees):")
        lh = int(input("  Left Hip  (Home=100): "))
        rh = int(input("  Right Hip (Home=90) : "))
        lf = int(input("  Left Foot (Home=90) : "))
        rf = int(input("  Right Foot (Home=90): "))
        dur = int(input("  Duration  (ms)       : "))
        cmd = f"servo:{lh},{rh},{lf},{rf},{dur}"
        await send_command(client, cmd)
    except ValueError:
        print("[-] Invalid input. Angles and duration must be integers.")


async def play_custom_tone(client):
    try:
        freq = int(input("\nEnter frequency (Hz): "))
        dur = int(input("Enter duration (ms): "))
        cmd = f"sound:{freq},{dur}"
        await send_command(client, cmd)
    except ValueError:
        print("[-] Invalid input. Frequency and duration must be integers.")


async def run_macro_playback(client):
    global macro_recording
    if not macro_recording:
        print("[-] No macro sequence found in memory to playback.")
        return

    print(f"\n[*] Playing back {len(macro_recording)} recorded actions...")
    for idx, (cmd_str, rel_time) in enumerate(macro_recording):
        if idx > 0:
            delay_sec = rel_time - macro_recording[idx - 1][1]
            if delay_sec > 0.01:
                print(f"[*] Delaying {delay_sec:.2f}s...")
                await asyncio.sleep(delay_sec)
        print(f"[*] [{idx + 1}/{len(macro_recording)}]: {cmd_str}")
        await send_command(client, cmd_str, silent=True)

    print("[+] Macro playback completed.")


async def main():
    global is_recording, macro_recording, current_motion_scale, recording_start_time
    client = await scan_and_connect()
    if not client:
        return

    try:
        await client.start_notify(CHARACTERISTIC_UUID, notification_handler)
        print("[+] Subscribed to sensor telemetry (sensors:proximity,touch).")
        await asyncio.sleep(1)

        while True:
            clear_screen()
            print_menu()
            user_input = input("> ").strip().lower()

            if user_input == 'q':
                print("[*] Disconnecting and exiting...")
                break

            if user_input == 'i':
                await interactive_drive(client)
                continue

            if user_input == 't':
                await composer_mode(client)
                continue

            if user_input == 'ds':
                await direct_servo_move(client)
                print("Press Enter to continue...")
                input()
                continue

            if user_input == 'p':
                await play_preset_sound(client)
                print("Press Enter to continue...")
                input()
                continue

            if user_input == 'c':
                await play_custom_tone(client)
                print("Press Enter to continue...")
                input()
                continue

            if user_input == 'r':
                is_recording = not is_recording
                if is_recording:
                    macro_recording.clear()
                    recording_start_time = time.time()
                    print("[*] Macro recording started.")
                else:
                    print(f"[*] Macro recording stopped. Saved {len(macro_recording)} actions.")
                print("Press Enter to continue...")
                input()
                continue

            if user_input == 'play':
                await run_macro_playback(client)
                print("Press Enter to continue...")
                input()
                continue

            if user_input == 'clear':
                macro_recording.clear()
                print("[+] Macro memory cleared.")
                print("Press Enter to continue...")
                input()
                continue

            if user_input.startswith("scale:"):
                try:
                    scale_val = float(user_input.split(":", 1)[1])
                    cmd_str = f"scale:{scale_val}"
                    await send_command(client, cmd_str)
                    current_motion_scale = scale_val
                except ValueError:
                    print("[-] Invalid scale. Use format: scale:0.7")
                print("Press Enter to continue...")
                input()
                continue

            if user_input in COMMAND_MAP:
                cmd_str, desc = COMMAND_MAP[user_input]
                print(f"[*] {desc}...")
                await send_command(client, cmd_str)
                await asyncio.sleep(0.5)
            else:
                print("[-] Unknown option. See menu above.")
                await asyncio.sleep(1)

    except Exception as e:
        print(f"[-] Connection error: {e}")
    finally:
        if client and client.is_connected:
            try:
                await client.stop_notify(CHARACTERISTIC_UUID)
            except Exception:
                pass
            await client.disconnect()
            print("[+] Disconnected successfully.")


if __name__ == "__main__":
    if sys.platform == "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    asyncio.run(main())
