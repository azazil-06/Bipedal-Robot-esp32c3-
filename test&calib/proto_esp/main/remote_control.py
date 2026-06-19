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

# Kuttappan's BLE Service and Characteristic UUIDs
SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1214"
CHARACTERISTIC_UUID = "19b10001-e8f2-537e-4f6c-d104768a1214"
DEVICE_NAME = "Kuttappan_Robot"

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
    "h": ("mood_happy", "Set Mood: Happy"),
    "a": ("mood_angry", "Set Mood: Angry"),
    "s": ("mood_sad", "Set Mood: Sad / Tired"),
    "d": ("mood_default", "Set Mood: Default"),
    "g_on": ("girl_on", "Enable Eyelashes (Girl Version)"),
    "g_off": ("girl_off", "Disable Eyelashes (Normal Version)"),
}

def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def print_status_bar():
    print("=" * 60)
    print(f" STATUS   : Connected to {DEVICE_NAME}")
    print(f" SCALE    : {current_motion_scale}x")
    print(f" TELEMETRY: Proximity={sensor_proximity} | Touch={sensor_touch}")
    if is_recording:
        print(f" RECORDER : [RECORDING ACTIVE] {len(macro_recording)} actions saved")
    else:
        print(f" RECORDER : Idle ({len(macro_recording)} actions in memory)")
    print("=" * 60)

def notification_handler(sender, data):
    global last_sensor_state, sensor_proximity, sensor_touch
    try:
        text = data.decode('utf-8')
        if text.startswith("sensors:"):
            state = text.split("sensors:")[1]
            if state != last_sensor_state:
                last_sensor_state = state
                parts = state.split(",")
                if len(parts) == 2:
                    sensor_proximity, sensor_touch = parts[0], parts[1]
                    # Inline notification if not in silent mode
                    sys.stdout.write(f"\r[Telemetry Update] Proximity: {sensor_proximity} | Touch: {sensor_touch}\n> ")
                    sys.stdout.flush()
    except Exception:
        pass

async def send_command(client, cmd_str, silent=False):
    global is_recording, macro_recording
    if not silent:
        print(f"[*] Sending BLE command: {cmd_str}")
    await client.write_gatt_char(CHARACTERISTIC_UUID, cmd_str.encode('utf-8'))
    
    # Record command for macro sequence
    if is_recording:
        macro_recording.append((cmd_str, time.time()))
        if not silent:
            print(f"[+] Recorded: {cmd_str}")

async def scan_and_connect():
    print(f"Scanning for BLE device: '{DEVICE_NAME}'...")
    devices = await BleakScanner.discover()
    target_device = None
    for d in devices:
        if d.name == DEVICE_NAME:
            target_device = d
            break
            
    if not target_device:
        print(f"[-] Could not find '{DEVICE_NAME}'. Please check robot power.")
        return None
        
    print(f"[+] Found {DEVICE_NAME} ({target_device.address}). Connecting...")
    client = BleakClient(target_device.address)
    await client.connect()
    if client.is_connected:
        print(f"[+] Connected successfully!")
        return client
    else:
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
        # Fallback for non-Windows systems (requires Enter)
        loop = asyncio.get_event_loop()
        val = await loop.run_in_executor(None, sys.stdin.readline)
        return val.strip().lower()
    
    while True:
        if msvcrt.kbhit():
            ch = msvcrt.getch()
            try:
                return ch.decode('utf-8').lower()
            except UnicodeDecodeError:
                # Handle special keys
                if ch == b'\x1b': # ESC
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
        'h': 'home',
        'o': 'off',
    }

    while True:
        key = await get_key_async()
        if key in ['q', 'esc']:
            print("\n[*] Exiting Interactive Drive Mode...")
            await asyncio.sleep(1)
            break
        elif key in drive_map:
            cmd = drive_map[key]
            print(f"\n[Drive Command] Key '{key}' -> Triggering '{cmd}'...")
            await send_command(client, cmd)
        else:
            print(f"\n[Drive Alert] Key '{key}' not mapped.")

async def composer_mode(client):
    if msvcrt is None:
        print("[-] Composer mode requires Windows OS.")
        await asyncio.sleep(2)
        return

    clear_screen()
    print("=" * 60)
    print("          BUZZER KEYBOARD COMPOSER MODE ACTIVE")
    print("=" * 60)
    print("  Play notes interactively on Kuttappan's buzzer:")
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
        elif key in notes_map:
            freq = notes_map[key]
            # Play a short 150ms tone
            cmd = f"sound:{freq},150"
            print(f"\n[Composer] Note '{key}' -> Freq {freq}Hz")
            await send_command(client, cmd, silent=True)
        else:
            print(f"\n[Composer] Key '{key}' has no note mapped.")

async def play_preset_sound(client):
    print("\nPreset Sounds:")
    print("  1: Happy       2: Sad         3: Scream")
    print("  4: R2D2        5: Surprise    6: Angry")
    print("  7: Sleep")
    choice = input("Select sound preset (1-7): ").strip()
    preset_map = {
        "1": "sound_happy",
        "2": "sound_sad",
        "3": "sound_scream",
        "4": "sound_r2d2",
        "5": "sound_surprise",
        "6": "sound_angry",
        "7": "sound_sleep"
    }
    if choice in preset_map:
        cmd = preset_map[choice]
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
        
    print(f"\n[*] Commencing playback of {len(macro_recording)} recorded actions...")
    for idx, (cmd_str, timestamp) in enumerate(macro_recording):
        if idx > 0:
            delay_sec = timestamp - macro_recording[idx - 1][1]
            if delay_sec > 0.01:
                print(f"[*] Delaying {delay_sec:.2f} seconds...")
                await asyncio.sleep(delay_sec)
        print(f"[*] Executing recorded command [{idx + 1}/{len(macro_recording)}]: {cmd_str}")
        await client.write_gatt_char(CHARACTERISTIC_UUID, cmd_str.encode('utf-8'))
        
    print("[+] Macro sequence playback completed successfully!")

async def main():
    global is_recording, macro_recording, current_motion_scale
    client = await scan_and_connect()
    if not client:
        return

    try:
        # Start notification subscription for real-time sensor updates
        await client.start_notify(CHARACTERISTIC_UUID, notification_handler)
        print("[+] Subscribed to sensor telemetry notifications.")
        await asyncio.sleep(1)

        while True:
            clear_screen()
            print_menu()
            user_input = input("> ").strip().lower()

            if user_input == 'q':
                print("[*] Disconnecting and exiting...")
                break

            elif user_input == 'i':
                await interactive_drive(client)
                continue

            elif user_input == 't':
                await composer_mode(client)
                continue

            elif user_input == 'ds':
                await direct_servo_move(client)
                print("Press Enter to continue...")
                input()
                continue

            elif user_input == 'p':
                await play_preset_sound(client)
                print("Press Enter to continue...")
                input()
                continue

            elif user_input == 'c':
                await play_custom_tone(client)
                print("Press Enter to continue...")
                input()
                continue

            elif user_input == 'r':
                is_recording = not is_recording
                if is_recording:
                    macro_recording.clear()
                    print("[*] Macro Recording: Started. All actions will be logged.")
                else:
                    print(f"[*] Macro Recording: Stopped. Saved {len(macro_recording)} actions.")
                print("Press Enter to continue...")
                input()
                continue

            elif user_input == 'play':
                await run_macro_playback(client)
                print("Press Enter to continue...")
                input()
                continue

            elif user_input == 'clear':
                macro_recording.clear()
                print("[+] Macro recording memory cleared.")
                print("Press Enter to continue...")
                input()
                continue

            elif user_input.startswith("scale:"):
                try:
                    scale_val = float(user_input.split(":")[1])
                    cmd_str = f"scale:{scale_val}"
                    await send_command(client, cmd_str)
                    current_motion_scale = scale_val
                except ValueError:
                    print("[-] Invalid scale value. Use format: scale:0.7")
                print("Press Enter to continue...")
                input()
                continue

            elif user_input in COMMAND_MAP:
                cmd_str, desc = COMMAND_MAP[user_input]
                print(f"[*] Triggering action: {desc}...")
                await send_command(client, cmd_str)
                # Wait briefly for execution
                await asyncio.sleep(0.5)
            else:
                print("[-] Unknown option. Select a command from the menu.")
                await asyncio.sleep(1)
                
    except Exception as e:
        print(f"[-] Connection Error: {e}")
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
        # Required for python on Windows to handle asyncio properly
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    asyncio.run(main())
