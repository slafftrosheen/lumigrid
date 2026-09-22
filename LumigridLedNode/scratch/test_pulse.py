import serial
import time

s = serial.Serial('COM7', 115200, timeout=1)

# ESP32 standard auto-reset circuit:
# DTR controls IO0 and EN via 2 transistors:
# Q1: base = DTR, collector = EN, emitter = RTS
# Q2: base = RTS, collector = IO0, emitter = DTR
# When DTR=1, RTS=0: EN is pulled LOW (Reset)
# When DTR=0, RTS=1: IO0 is pulled LOW (Boot)

for dtr, rts in [
    (False, False),
    (True, False),
    (False, True),
    (True, True),
]:
    s.dtr = dtr
    s.rts = rts
    time.sleep(0.05)

# Reset pulse:
s.dtr = False
s.rts = True
time.sleep(0.1)
s.dtr = True
s.rts = False
time.sleep(0.05)
s.dtr = False
s.rts = False
time.sleep(0.1)

data = s.read(1000)
print("Read:", data)
s.close()
