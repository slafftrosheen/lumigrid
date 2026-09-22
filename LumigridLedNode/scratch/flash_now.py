import subprocess
import sys
import time

esptool = r"C:\Users\slavd\.platformio\packages\tool-esptoolpy\esptool.py"
python_exe = r"C:\Users\slavd\.platformio\penv\Scripts\python.exe"

cmd = [
    python_exe, "-u",
    esptool,
    "--port", "COM3",
    "--baud", "460800",
    "--before", "default_reset",
    "--after", "hard_reset",
    "--connect-attempts", "60",
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
print("  ESP32 FLASH TOOL READY ON COM3 (460800 baud)", flush=True)
print("=" * 65, flush=True)
print("  CRITICAL BUTTON TIMING:", flush=True)
print("  1. Press and HOLD the [BOOT] (or IO0) button.", flush=True)
print("  2. While STILL HOLDING [BOOT], press & release [EN] (or RST) once.", flush=True)
print("  3. Keep holding [BOOT] for 1 more second until it connects!", flush=True)
print("=" * 65, flush=True)
print("Connecting now...", flush=True)

proc = subprocess.Popen(cmd)
proc.wait()
sys.exit(proc.returncode)
