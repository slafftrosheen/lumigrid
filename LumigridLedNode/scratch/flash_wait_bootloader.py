import subprocess
import time
import sys

esptool = r"C:\Users\alisi\.platformio\packages\tool-esptoolpy\esptool.py"
python_exe = r"C:\Python314\python.exe"

flash_cmd = [
    python_exe, "-u",
    esptool,
    "--port", "COM7",
    "--baud", "460800",
    "--before", "no_reset",
    "--after", "hard_reset",
    "--connect-attempts", "3",
    "write_flash", "-z",
    "--flash_mode", "dio",
    "--flash_freq", "40m",
    "--flash_size", "detect",
    "0x1000", r".pio\build\factory\bootloader.bin",
    "0x8000", r".pio\build\factory\partitions.bin",
    "0xd000", r".pio\build\factory\ota_data_initial.bin",
    "0x10000", r".pio\build\factory\firmware.bin"
]

print("=" * 65, flush=True)
print("  WAITING FOR ESP32 BOOTLOADER (SAFE NO-RESET MODE)", flush=True)
print("=" * 65, flush=True)
print("  Action needed on ESP32:", flush=True)
print("  1. Unplug the USB cable (or hold BOOT and tap EN).", flush=True)
print("  2. Hold the [BOOT] button and plug USB back in.", flush=True)
print("  3. Release [BOOT].", flush=True)
print("=" * 65, flush=True)

for attempt in range(1, 41):
    print(f"\n[Check {attempt}/40] Checking if ESP32 is in bootloader mode...", flush=True)
    res = subprocess.run(flash_cmd)
    if res.returncode == 0:
        print("\n" + "=" * 65, flush=True)
        print("  SUCCESS! FIRMWARE FLASHED TO ESP32!", flush=True)
        print("=" * 65, flush=True)
        sys.exit(0)
    time.sleep(0.5)

print("\nTimed out waiting for bootloader mode.", flush=True)
sys.exit(1)
