import serial
import time
import sys

port = "COM7"
baud = 115200

print(f"Opening {port} at {baud} baud...", flush=True)

while True:
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
        print(f"Connected to {port}! Waiting for ESP32 boot log...", flush=True)
        print(">>> PLEASE PRESS THE 'EN' (OR 'RST') BUTTON ON THE ESP32 NOW <<<", flush=True)
        
        while True:
            n = ser.in_waiting
            if n > 0:
                data = ser.read(n)
                sys.stdout.write(data.decode('utf-8', errors='replace'))
                sys.stdout.flush()
            time.sleep(0.02)
    except serial.SerialException as e:
        print(f"Port notice: {e}. Retrying in 1s...", flush=True)
        time.sleep(1.0)
    except KeyboardInterrupt:
        break
