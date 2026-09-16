import serial
import time

s = serial.Serial('COM7', 115200, timeout=1)

# Reset ESP32: pull EN low then release
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
time.sleep(0.05)

start = time.time()
while time.time() - start < 3:
    data = s.read(s.in_waiting or 1)
    if data:
        print(data.decode('utf-8', errors='replace'), end='', flush=True)

s.close()
