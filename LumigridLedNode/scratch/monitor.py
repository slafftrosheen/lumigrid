import serial
import time
import sys

port = 'COM7'
baud = 115200

print(f"Waiting for reboot logs on {port} at {baud} baud...", flush=True)

try:
    s = serial.Serial(port, baud, timeout=1)
except Exception as e:
    print(f"Failed to open {port}: {e}", flush=True)
    sys.exit(1)

t0 = time.time()
while time.time() - t0 < 60:
    line = s.readline()
    if line:
        text = line.decode('utf-8', errors='replace').rstrip()
        print(text, flush=True)
        # Reset timeout on activity
        t0 = time.time()

s.close()
print("Monitor finished.", flush=True)
