import serial
import time

s = serial.Serial('COM7', 115200, timeout=0.1)

print("Testing RTS/DTR combinations...")
for rts in [False, True, False]:
    for dtr in [False, True, False]:
        s.setDTR(dtr)
        s.setRTS(rts)
        time.sleep(0.1)
        data = s.read(s.in_waiting or 0)
        if data:
            print(f"RTS={rts}, DTR={dtr}:", data)

s.setDTR(False)
s.setRTS(False)
time.sleep(1)
data = s.read(s.in_waiting or 0)
print("Final reading (both False):", data)
s.close()
