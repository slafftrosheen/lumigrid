import serial
import time

s = serial.Serial('COM7', 115200, timeout=1)

# Sequence to force download mode:
# EN = 0, IO0 = 0
s.dtr = True
s.rts = True
time.sleep(0.1)

# EN = 1, IO0 = 0 (Release reset while holding IO0 low)
s.dtr = False
s.rts = True
time.sleep(0.1)

# Release both
s.dtr = False
s.rts = False
time.sleep(0.2)

data = s.read(500)
print("Response after reset:", data)
s.close()
