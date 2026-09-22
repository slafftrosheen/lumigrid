import sys
import time
import serial
import struct

sys.path.insert(0, r'C:\Users\alisi\.platformio\packages\tool-esptoolpy')
import esptool

port = "COM7"
baud = 115200

print(f"Connecting to already-running stub on {port}...")
# Use no_reset so we don't mess up DTR/RTS
esp = esptool.cmds.detect_chip(port=port, baud=baud, connect_mode="no_reset")
print(f"Connected to chip: {esp.CHIP_NAME}")

print("Sending flash_finish(reboot=False) to jump directly into application in flash...")
try:
    # reboot=False: 0x01 in packet -> stub jumps to flash entry point without resetting
    esp.flash_begin(0, 0)
    esp.flash_finish(reboot=False)
    print("Command sent successfully!")
except Exception as e:
    print(f"flash_finish exception: {e}")

ser = esp._port
print("\n--- STREAMING SERIAL OUTPUT FOR 15 SECONDS ---")
start = time.time()
while time.time() - start < 15.0:
    if ser.in_waiting:
        chunk = ser.read(ser.in_waiting)
        text = chunk.decode('latin1', errors='replace')
        sys.stdout.write(text)
        sys.stdout.flush()
    time.sleep(0.05)

ser.close()
print("\n--- FINISHED ---")
