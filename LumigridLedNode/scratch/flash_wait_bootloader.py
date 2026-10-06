import subprocess
import time
import sys

esptool = r"C:\Users\slavd\.platformio\packages\tool-esptoolpy\esptool.py"
python_exe = r"C:\Users\slavd\.platformio\penv\Scripts\python.exe"

flash_cmd = [
    python_exe, "-u",
    esptool,
    "--port", "COM3",
    "--baud", "460800",
    "--before", "no_reset",
    "--after", "hard_reset",
    "--connect-attempts", "1",
    "write_flash", "-z",
    "--flash_mode", "dio",
    "--flash_freq", "40m",
    "--flash_size", "8MB",
    "0x1000", r".pio\build\factory\bootloader.bin",
    "0x8000", r".pio\build\factory\partitions.bin",
    "0xd000", r".pio\build\factory\ota_data_initial.bin",
    "0x10000", r".pio\build\factory\firmware.bin"
]

print("=" * 65, flush=True)
print("  WAITING FOR ESP32 BOOTLOADER ON COM3 (SAFE NO-RESET MODE)", flush=True)
print("=" * 65, flush=True)
print("  To flash:", flush=True)
print("  1. Press and hold the [BOOT] button on your ESP32.", flush=True)
print("  2. While holding [BOOT], press and release [EN] (or RST) once.", flush=True)
print("  3. Release [BOOT]. Writing will start immediately!", flush=True)
print("=" * 65, flush=True)

for attempt in range(1, 61):
    print(f"[Check {attempt}/60] Polling COM3 for download mode...", flush=True)
    res = subprocess.run(flash_cmd)
    if res.returncode == 0:
        print("\n" + "=" * 65, flush=True)
        print("  SUCCESS! FIRMWARE FLASHED TO ESP32!", flush=True)
        print("=" * 65, flush=True)
        sys.exit(0)
    time.sleep(0.5)

print("\nTimed out waiting for bootloader mode.", flush=True)
sys.exit(1)
