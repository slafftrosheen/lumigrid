"""One wired migration: preserve NVS, reset OTA metadata, verify NVS before boot."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys
from verify_partition_layout import EXPECTED, read_layout, validate_layout, storage_budget

NODE = Path(__file__).resolve().parents[1]

def verify_artifacts(build):
    files = [build / name for name in ('bootloader.bin', 'partitions.bin', 'ota_data_initial.bin', 'firmware.bin')]
    for file in files:
        if not file.is_file(): raise ValueError('missing freshly built artifact: ' + str(file))
    for file in (files[0], files[3]):
        header = file.read_bytes()[:24]
        if len(header) != 24 or header[0] != 0xe9 or header[3] >> 4 != 3:
            raise ValueError('image header does not advertise 8 MB: ' + str(file))
    if files[2].read_bytes() != b'\xff' * 0x2000: raise ValueError('OTA metadata initializer must be fresh/empty')
    found = {}
    data = files[1].read_bytes()
    for offset in range(0, len(data) - 31, 32):
        magic, kind, subtype, address, size, name, flags = struct.unpack_from('<HBBII16sI', data, offset)
        if magic != 0x50aa: break
        label = name.split(b'\0', 1)[0].decode()
        found[label] = (kind, subtype, address, size)
    expected = {'nvs': (1,2,0x9000,0x4000), 'otadata': (1,0,0xd000,0x2000),
                'phy_init': (1,1,0xf000,0x1000), 'factory': (0,0,0x10000,0x250000),
                'ota_0': (0,0x10,0x260000,0x250000), 'ota_1': (0,0x11,0x4b0000,0x250000),
                'littlefs': (1,0x82,0x700000,0x100000)}
    if found != expected: raise ValueError('built partition binary does not match Layout v2')
    parts = read_layout(NODE / 'partitions.factory.csv')
    validate_layout(parts)
    storage_budget(parts, files[3].stat().st_size)
    return files

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--build-dir', type=Path, default=NODE / '.pio/build-lg0384/factory')
    parser.add_argument('--esptool', type=Path, default=Path.home() / '.platformio/packages/tool-esptoolpy/esptool.py')
    args = parser.parse_args()
    files = verify_artifacts(args.build_dir.resolve())
    log = NODE.parent / 'docs/LG-038.4-flash-layout-v2.log'
    private = NODE / '.pio/lg0384-private'
    private.mkdir(parents=True, exist_ok=True)
    def run(extra):
        result = subprocess.run([sys.executable, str(args.esptool), '--chip','esp32','--port',args.port,
                                 '--baud','115200', *extra], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        clean = re.sub(r'\x08', '', result.stdout)
        print(clean, end='')
        with log.open('a', encoding='utf-8') as target: target.write(clean)
        if result.returncode: raise RuntimeError('esptool failed; migration stopped')
        return clean
    identity = run(['flash_id'])
    if 'Detected flash size: 8MB' not in identity: raise ValueError('physical 8 MB flash not verified')
    before, after = private / 'nvs-migration-before.bin', private / 'nvs-migration-after.bin'
    if before.exists() or after.exists(): raise ValueError('existing NVS migration backups: archive privately before another migration')
    run(['read_flash','0x9000','0x4000',str(before)])
    run(['--after','no_reset','write_flash','--flash_mode','dio','--flash_freq','40m','--flash_size','8MB',
         '0x1000',str(files[0]),'0x8000',str(files[1]),'0xd000',str(files[2]),'0x10000',str(files[3])])
    run(['--before','no_reset','--after','no_reset','read_flash','0x9000','0x4000',str(after)])
    if before.read_bytes() != after.read_bytes(): raise ValueError('NVS preservation FAILED; do not boot/reset')
    proof = 'NVS byte-for-byte preserved before first boot; SHA256=' + hashlib.sha256(before.read_bytes()).hexdigest()
    print(proof)
    with log.open('a', encoding='utf-8') as target:
        target.write(proof + '\nFirmware SHA256=' + hashlib.sha256(files[3].read_bytes()).hexdigest() + '\n')
    # Reset into the new factory app, and capture the real boot table/heap evidence.
    import serial, time
    port = serial.Serial(); port.port = args.port; port.baudrate = 115200; port.timeout = .2
    port.dtr = False; port.rts = False; port.open()
    port.rts = True; time.sleep(.1); port.rts = False
    deadline = time.monotonic() + 35
    captured = bytearray()
    while time.monotonic() < deadline: captured.extend(port.read(4096))
    port.close()
    boot = captured.decode('utf-8', errors='replace')
    print(boot)
    with log.open('a', encoding='utf-8') as target: target.write(boot)

if __name__ == '__main__': main()
