import subprocess
import time
import sys

esptool = r"C:\Users\alisi\.platformio\packages\tool-esptoolpy\esptool.py"
python_exe = r"C:\Python314\python.exe"

cmd = [
    python_exe,
    esptool,
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

print("=" * 60)
print(" READY TO FLASH ESP32 ON COM7")
print(" Please hold the BOOT (or IO0) button on the ESP32 board now!")
print(" (If it has an EN/RST button, tap EN while holding BOOT, then release)")
print("=" * 60)

for i in range(1, 10):
    print(f"\n[Attempt {i}/9] Connecting...")
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print("\n" + "=" * 60)
        print(" SUCCESS! FIRMWARE FLASHED TO ESP32!")
        print("=" * 60)
        sys.exit(0)
    time.sleep(0.5)

print("\nFlashing timed out after 9 attempts.")
sys.exit(1)
