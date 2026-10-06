"""Bounded serial capture, resilient to USB reconnects; never changes Wi-Fi."""
import argparse
from pathlib import Path
import time
import serial

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', default='COM3')
    p.add_argument('--seconds', type=float, default=60)
    p.add_argument('--command', default='')
    p.add_argument('--log', type=Path, default=Path(__file__).resolve().parents[2] / 'docs/LG-038.4-flash-layout-v2.log')
    args = p.parse_args()
    deadline = time.monotonic() + args.seconds
    connection = None
    with args.log.open('a', encoding='utf-8') as log:
        while time.monotonic() < deadline:
            try:
                if connection is None:
                    connection = serial.Serial()
                    connection.port = args.port; connection.baudrate = 115200; connection.timeout = .2
                    connection.dtr = False; connection.rts = False; connection.open()
                    print('Serial connected: ' + args.port, flush=True)
                    if args.command: connection.write((args.command + '\n').encode('ascii'))
                data = connection.read(4096)
                if data:
                    text = data.decode('utf-8', errors='replace')
                    print(text, end='', flush=True); log.write(text); log.flush()
            except serial.SerialException:
                if connection:
                    try: connection.close()
                    except serial.SerialException: pass
                connection = None
                time.sleep(.1)
    if connection: connection.close()

if __name__ == '__main__': main()
