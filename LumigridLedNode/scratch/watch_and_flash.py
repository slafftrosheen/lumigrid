import serial.tools.list_ports
import subprocess
import time
import sys

esptool = r"C:\Users\slavd\.platformio\packages\tool-esptoolpy\esptool.py"
python_exe = r"C:\Users\slavd\.platformio\penv\Scripts\python.exe"

print("===================================================================", flush=True)
print("  WATCHING FOR USB CONNECTION (PLUG IN WHILE HOLDING BOOT)        ", flush=True)
print("===================================================================", flush=True)
print("  Ready! Hold the [BOOT] button and plug the USB cable in now.    ", flush=True)
print("===================================================================", flush=True)

start_time = time.time()
while time.time() - start_time < 90:
    ports = serial.tools.list_ports.comports()
    target_port = None
    for p in ports:
        desc = (p.description or "").lower()
        # Look for CH340, CP210, USB Serial, or COM3
        if "ch340" in desc or "usb-serial" in desc or "cp210" in desc or p.device == "COM3":
            target_port = p.device
            break

    if target_port:
        print(f"\n[DETECTED] Found {target_port} ({p.description})! Attempting flash...", flush=True)
        # Give Windows a split second to finish enumerating the driver
        time.sleep(0.3)
        flash_cmd = [
            python_exe, "-u",
            esptool,
            "--port", target_port,
            "--baud", "460800",
            "--before", "no_reset",
            "--after", "hard_reset",
            "--connect-attempts", "5",
            "write_flash", "-z",
            "--flash_mode", "dio",
            "--flash_freq", "40m",
            "--flash_size", "8MB",
            "0x1000", r".pio\build\factory\bootloader.bin",
            "0x8000", r".pio\build\factory\partitions.bin",
            "0xd000", r".pio\build\factory\ota_data_initial.bin",
            "0x10000", r".pio\build\factory\firmware.bin"
        ]
        res = subprocess.run(flash_cmd)
        if res.returncode == 0:
            print("\n===================================================================", flush=True)
            print("  SUCCESS! FIRMWARE FLASHED AND REBOOTED SUCCESSFULLY!            ", flush=True)
            print("===================================================================", flush=True)
            sys.exit(0)
        else:
            print("\nFlash with no_reset failed. Trying with default_reset at 115200 baud...", flush=True)
            flash_cmd[6] = "115200"
            flash_cmd[8] = "default_reset"
            flash_cmd[12] = "10"
            res2 = subprocess.run(flash_cmd)
            if res2.returncode == 0:
                print("\n===================================================================", flush=True)
                print("  SUCCESS! FIRMWARE FLASHED AND REBOOTED SUCCESSFULLY!            ", flush=True)
                print("===================================================================", flush=True)
                sys.exit(0)

    time.sleep(0.2)

print("\nTimeout waiting for USB device to be plugged in.", flush=True)
sys.exit(1)
