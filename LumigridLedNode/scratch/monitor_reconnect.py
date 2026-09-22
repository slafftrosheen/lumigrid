import time
import sys
import serial
import serial.tools.list_ports

port = "COM7"
baud = 115200
print(f"Monitoring {port} for hardware reset / reconnect...")
print("Please press the EN (or RST) button, or unplug and re-plug the USB cable now!")

start = time.time()
while time.time() - start < 40.0:
    try:
        # Check if port exists in system
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if port not in ports:
            time.sleep(0.3)
            continue

        ser = serial.Serial(port, baud, timeout=0.5)
        # Clear DTR and RTS so they don't hold the chip
        try:
            ser.setDTR(False)
            ser.setRTS(False)
        except:
            pass

        print(f"[{time.strftime('%H:%M:%S')}] Connected to {port}! Waiting for boot/heartbeat...")
        while time.time() - start < 40.0:
            line = ser.readline()
            if line:
                text = line.decode('latin1', errors='replace')
                sys.stdout.write(text)
                sys.stdout.flush()
        ser.close()
        break
    except serial.SerialException:
        time.sleep(0.3)
    except Exception as e:
        time.sleep(0.3)

print("\n--- Monitor finished ---")
