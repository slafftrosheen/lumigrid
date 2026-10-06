"""Production layout v2 and firmware headroom contract (no device mutation)."""
import argparse
import csv
from pathlib import Path

FLASH_BYTES = 0x800000
LAYOUT_VERSION = 2
EXPECTED = {
    'nvs': ('data', 'nvs', 0x9000, 0x4000),
    'otadata': ('data', 'ota', 0xd000, 0x2000),
    'phy_init': ('data', 'phy', 0xf000, 0x1000),
    'factory': ('app', 'factory', 0x10000, 0x250000),
    'ota_0': ('app', 'ota_0', 0x260000, 0x250000),
    'ota_1': ('app', 'ota_1', 0x4b0000, 0x250000),
    'littlefs': ('data', 'spiffs', 0x700000, 0x100000),
}

def read_layout(path):
    parts = {}
    for row in csv.reader(path.read_text(encoding='utf-8').splitlines()):
        if not row or row[0].strip().startswith('#'): continue
        name, kind, subtype, offset, size = [v.strip() for v in row[:5]]
        if name in parts: raise ValueError('duplicate partition: ' + name)
        parts[name] = (kind, subtype, int(offset, 0), int(size, 0))
    return parts

def validate_layout(parts, flash_bytes=FLASH_BYTES, version=LAYOUT_VERSION):
    if version != 2: raise ValueError('partition layout version must be 2')
    end = 0
    for name, (kind, subtype, address, size) in sorted(parts.items(), key=lambda p: p[1][2]):
        if size <= 0 or address < end: raise ValueError('overlap/invalid size: ' + name)
        if kind == 'app' and address % 0x10000: raise ValueError('unaligned app: ' + name)
        end = address + size
    if end > flash_bytes: raise ValueError('partitions exceed configured flash')
    if parts != EXPECTED: raise ValueError('partition layout does not match production v2 contract')
    if end != FLASH_BYTES: raise ValueError('production layout must end at 0x800000')
    return end

def size_health(image_bytes, slot_bytes):
    if image_bytes <= 0 or slot_bytes <= 0: raise ValueError('invalid firmware/slot size')
    if image_bytes * 100 > slot_bytes * 85: return 'FAIL'
    if image_bytes * 100 >= slot_bytes * 75: return 'WARNING'
    return 'HEALTHY'

def storage_budget(parts, image_bytes):
    slot = parts['factory'][3]
    status = size_health(image_bytes, slot)
    print(f'LUMIGRID STORAGE\nPhysical target: 8 MB\nPartition layout: v2\n'
          f'Factory slot: {slot} bytes\nFirmware: {image_bytes} bytes\n'
          f'Usage: {image_bytes / slot:.1%}\nRemaining: {slot-image_bytes} bytes\n'
          f'OTA0: {parts["ota_0"][3]} bytes\nOTA1: {parts["ota_1"][3]} bytes\n'
          f'LittleFS reserve: {parts["littlefs"][3]} bytes\nSTATUS: {status}')
    if status == 'FAIL': raise ValueError('production firmware exceeds 85% slot budget')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--partitions', type=Path, default=Path(__file__).resolve().parents[1] / 'partitions.factory.csv')
    parser.add_argument('--firmware', type=Path)
    args = parser.parse_args()
    parts = read_layout(args.partitions)
    validate_layout(parts)
    if args.firmware: storage_budget(parts, args.firmware.stat().st_size)
    else: print('Partition Layout v2: 8 MB, preserved NVS, three aligned 0x250000 app slots')

if __name__ == '__main__': main()
