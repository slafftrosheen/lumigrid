import subprocess
import time
import sys

cmd = [
    r"C:\Python314\python.exe",
    r"C:\Users\alisi\.platformio\packages\tool-esptoolpy\esptool.py",
    "--chip", "esp32",
    "--port", "COM7",
    "--baud", "115200",
    "--before", "default_reset",
    "--after", "hard_reset",
    "write_flash", "-z",
    "--flash_mode", "dio",
    "--flash_freq", "40m",
    "--flash_size", "4MB",
    "0x1000", r".pio\build\factory\bootloader.bin",
    "0x8000", r".pio\build\factory\partitions.bin",
    "0x10000", r".pio\build\factory\firmware.bin"
]

print("=== Flashing Loop: Hold the BOOT button on the ESP32 until connection succeeds ===")
for attempt in range(1, 6):
    print(f"\n>>> Attempt {attempt}/5: Connecting to COM7...")
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print("\n========================================")
        print("   SUCCESSFULLY FLASHED FIRMWARE!       ")
        print("========================================")
        sys.exit(0)
    time.sleep(1)

print("Failed after 5 attempts.")
sys.exit(1)
