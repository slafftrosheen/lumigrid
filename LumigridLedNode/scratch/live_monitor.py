import serial
import serial.tools.list_ports
import time
import sys

port = 'COM3'
baud = 115200

print(f"Monitoring {port} (will auto-reconnect on USB unplug/replug)...", flush=True)

s = None
t_start = time.time()
while time.time() - t_start < 120:
    if s is None:
        try:
            s = serial.Serial(port, baud, timeout=0.5)
            print(f"Connected to {port} at {baud} baud.", flush=True)
        except Exception:
            time.sleep(0.5)
            continue
    
    try:
        line = s.readline()
        if line:
            text = line.decode('utf-8', errors='replace').rstrip()
            print(text, flush=True)
    except Exception as e:
        print(f"Port disconnected: {e}", flush=True)
        try:
            s.close()
        except Exception:
            pass
        s = None
        time.sleep(0.5)

if s:
    s.close()
print("Monitor ended.", flush=True)
