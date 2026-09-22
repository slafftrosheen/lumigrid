import serial
import subprocess
import time
import sys

print("===================================================================", flush=True)
print("  MONITORING COM3 AND WI-FI FOR LUMIGRID SOFTAP                   ", flush=True)
print("===================================================================", flush=True)
print("  Please TAP the [EN] / [RST] button on your ESP32, or            ", flush=True)
print("  UNPLUG and RE-PLUG the USB cable now.                           ", flush=True)
print("===================================================================", flush=True)

start_time = time.time()
ser = None
while time.time() - start_time < 90:
    if ser is None:
        try:
            ser = serial.Serial('COM3', 115200, timeout=0.2)
            ser.dtr = False
            ser.rts = False
        except Exception:
            ser = None
            time.sleep(0.5)
            continue

    try:
        data = ser.read(ser.in_waiting or 1)
        if data:
            sys.stdout.write(data.decode('utf-8', errors='replace'))
            sys.stdout.flush()
    except Exception:
        if ser:
            try:
                ser.close()
            except Exception:
                pass
        ser = None
        time.sleep(0.5)

if ser:
    ser.close()
