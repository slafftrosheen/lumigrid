import time
import sys
import serial
import serial.tools.list_ports

port = "COM7"
baud = 115200

print(f"=== WAITING FOR REBOOT ON {port} ===")
print("Listening for incoming bytes or USB re-enumeration...")

start = time.time()
while time.time() - start < 60.0:
    try:
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if port not in ports:
            print(f"[{time.strftime('%H:%M:%S')}] Device disconnected! Waiting for re-plug...")
            while port not in [p.device for p in serial.tools.list_ports.comports()] and time.time() - start < 60.0:
                time.sleep(0.2)
            print(f"[{time.strftime('%H:%M:%S')}] Device reconnected! Opening serial...")
            time.sleep(0.5)

        ser = serial.Serial(port, baud, timeout=0.2)
        ser.dtr = False
        ser.rts = False

        connected_time = time.time()
        saw_data = False
        while time.time() - connected_time < 8.0:
            line = ser.readline()
            if line:
                saw_data = True
                print(line.decode('latin1', errors='replace').rstrip())
        ser.close()

        if saw_data:
            print("\n=== BOOT CAPTURED SUCCESSFULLY ===")
            sys.exit(0)
    except serial.SerialException:
        time.sleep(0.3)
    except Exception as e:
        time.sleep(0.3)

print("Timeout reached.")
