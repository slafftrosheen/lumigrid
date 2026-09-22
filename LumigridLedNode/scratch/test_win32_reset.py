import serial
import ctypes
from ctypes import wintypes
import time

port = "COM3"
baud = 115200

SETRTS = 3
CLRRTS = 4
SETDTR = 5
CLRDTR = 6

kernel32 = ctypes.windll.kernel32

print(f"Opening {port}...")
ser = serial.Serial(port, baud, timeout=0.1)
handle = ser._port_handle

print("Clearing DTR (ensure IO0 is HIGH)...")
kernel32.EscapeCommFunction(handle, CLRDTR)
time.sleep(0.1)

print("Asserting RTS (pull EN LOW)...")
kernel32.EscapeCommFunction(handle, SETRTS)
time.sleep(0.3)

print("Releasing RTS (release EN HIGH, chip boots!)...")
kernel32.EscapeCommFunction(handle, CLRRTS)
time.sleep(0.2)

print("Listening for boot output for 10 seconds...")
start = time.time()
lines = []
while time.time() - start < 10.0:
    chunk = ser.read(ser.in_waiting or 1)
    if chunk:
        text = chunk.decode('utf-8', errors='replace')
        print(text, end='', flush=True)
        lines.append(text)

ser.close()
print(f"\nDone. Total chunks: {len(lines)}")
