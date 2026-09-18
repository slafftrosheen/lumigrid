import subprocess
import time
import sys

esptool = r"C:\Users\alisi\.platformio\packages\tool-esptoolpy\esptool.py"
python_exe = r"C:\Python314\python.exe"

cmd = [
    python_exe,
    esptool,
    "--port", "COM7",
    "--baud", "460800",
    "--before", "default_reset",
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

print("=" * 60, flush=True)
print(" READY TO FLASH ESP32 ON COM7 (460800 baud)", flush=True)
print(" If it does not connect automatically, hold BOOT and tap EN/RST.", flush=True)
print("=" * 60, flush=True)

for i in range(1, 31):
    print(f"\n[Attempt {i}/30] Connecting to ESP32 on COM7...", flush=True)
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print("\n" + "=" * 60, flush=True)
        print(" SUCCESS! FIRMWARE FLASHED TO ESP32!", flush=True)
        print("=" * 60, flush=True)
        sys.exit(0)
    time.sleep(0.3)

print("\nFlashing timed out after 30 attempts.", flush=True)
sys.exit(1)
