#!/usr/bin/env python3
"""
Quick test script to verify Lumigrid Display Node setup.

This script tests individual components without running the full application.
"""

import sys
import time


def test_imports():
    """Test if all required libraries can be imported."""
    print("Testing Python imports...")
    
    try:
        import yaml
        print("  ✓ PyYAML")
    except ImportError:
        print("  ✗ PyYAML - Install with: pip install PyYAML")
        return False
    
    try:
        import flask
        print("  ✓ Flask")
    except ImportError:
        print("  ✗ Flask - Install with: pip install Flask")
        return False
    
    try:
        from PIL import Image
        print("  ✓ Pillow")
    except ImportError:
        print("  ✗ Pillow - Install with: pip install Pillow")
        return False
    
    try:
        import pytz
        print("  ✓ pytz")
    except ImportError:
        print("  ✗ pytz - Install with: pip install pytz")
        return False
    
    try:
        import board
        print("  ✓ adafruit-blinka (board module)")
    except ImportError:
        print("  ! adafruit-blinka not available (needed for RTC and DHT sensors)")
    
    return True


def test_rgbmatrix():
    """Test RGB Matrix library."""
    print("\nTesting RGB Matrix library...")
    try:
        from rgbmatrix import RGBMatrix, RGBMatrixOptions
        print("  ✓ rgbmatrix library found")
        return True
    except ImportError:
        print("  ✗ rgbmatrix library not found")
        print("    Install from: https://github.com/hzeller/rpi-rgb-led-matrix")
        print("    Or run install.sh to install automatically")
        return False


def test_i2c():
    """Test I2C interface."""
    print("\nTesting I2C interface...")
    try:
        import subprocess
        result = subprocess.run(['i2cdetect', '-y', '1'], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            print("  ✓ I2C interface available")
            print("\nI2C Devices:")
            print(result.stdout)
            return True
        else:
            print("  ✗ I2C not available")
            return False
    except FileNotFoundError:
        print("  ✗ i2cdetect not found - Install with: sudo apt-get install i2c-tools")
        return False


def test_onewire():
    """Test 1-Wire interface."""
    print("\nTesting 1-Wire interface...")
    import os
    import glob
    
    device_folder = '/sys/bus/w1/devices/'
    if not os.path.exists(device_folder):
        print("  ✗ 1-Wire interface not available")
        print("    Enable in /boot/config.txt (or /boot/firmware/config.txt): dtoverlay=w1-gpio")
        return False
    
    device_files = glob.glob(device_folder + '28-*')
    if device_files:
        print(f"  ✓ Found {len(device_files)} DS18B20 sensor(s)")
        for device in device_files:
            print(f"    - {os.path.basename(device)}")
        return True
    else:
        print("  ! 1-Wire enabled but no DS18B20 sensors found")
        return True


def test_temperature_sensor():
    """Test temperature sensor."""
    print("\nTesting temperature sensor...")
    
    # Try DS18B20
    try:
        from w1thermsensor import W1ThermSensor, Sensor
        sensor = W1ThermSensor(sensor_type=Sensor.DS18B20)
        temp = sensor.get_temperature()
        print(f"  ✓ DS18B20 sensor: {temp:.1f}°C")
        return True
    except ImportError:
        print("  ! w1thermsensor library not available")
    except Exception as e:
        print(f"  ✗ DS18B20 error: {e}")
    
    # Try DHT (CircuitPython version)
    try:
        import board
        import adafruit_dht
        print("  ✓ adafruit-circuitpython-dht library available")
        print("    (Actual sensor test requires GPIO connection)")
        return True
    except ImportError:
        print("  ! adafruit-circuitpython-dht library not available")
    
    return False


def test_rtc():
    """Test RTC module."""
    print("\nTesting RTC module...")
    
    try:
        import board
        import busio
        from adafruit_ds3231 import DS3231
        
        i2c = busio.I2C(board.SCL, board.SDA)
        rtc = DS3231(i2c)
        rtc_time = rtc.datetime
        
        print(f"  ✓ DS3231 RTC found")
        print(f"    Time: {rtc_time.tm_year}-{rtc_time.tm_mon:02d}-{rtc_time.tm_mday:02d} "
              f"{rtc_time.tm_hour:02d}:{rtc_time.tm_min:02d}:{rtc_time.tm_sec:02d}")
        return True
    except ImportError:
        print("  ! RTC libraries not available")
        print("    Install with: pip install adafruit-blinka adafruit-circuitpython-ds3231")
    except Exception as e:
        print(f"  ✗ RTC error: {e}")
    
    return False


def test_config():
    """Test configuration file."""
    print("\nTesting configuration...")
    import os
    
    if os.path.exists('config.yaml'):
        print("  ✓ config.yaml found")
        try:
            import yaml
            with open('config.yaml', 'r') as f:
                config = yaml.safe_load(f)
                print(f"    Display mode: {config.get('display', {}).get('default_mode', 'N/A')}")
                print(f"    Web port: {config.get('web', {}).get('port', 'N/A')}")
            return True
        except Exception as e:
            print(f"  ✗ Error reading config: {e}")
            return False
    else:
        print("  ! config.yaml not found")
        print("    Copy from: cp config.yaml.example config.yaml")
        return False


def test_venv():
    """Test if running in virtual environment."""
    print("\nTesting virtual environment...")
    import sys
    
    # Check if we're in a virtual environment
    in_venv = (hasattr(sys, 'real_prefix') or 
               (hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix))
    
    if in_venv:
        print(f"  ✓ Running in virtual environment: {sys.prefix}")
        return True
    else:
        print("  ! Not running in a virtual environment")
        print("    Activate with: source venv/bin/activate")
        return False


def main():
    """Run all tests."""
    print("=" * 50)
    print("Lumigrid Display Node - Test Script")
    print("=" * 50)
    
    results = {
        "Virtual Environment": test_venv(),
        "Python Imports": test_imports(),
        "RGB Matrix": test_rgbmatrix(),
        "I2C Interface": test_i2c(),
        "1-Wire Interface": test_onewire(),
        "Temperature Sensor": test_temperature_sensor(),
        "RTC Module": test_rtc(),
        "Configuration": test_config(),
    }
    
    print("\n" + "=" * 50)
    print("Test Summary")
    print("=" * 50)
    
    for test_name, result in results.items():
        status = "✓ PASS" if result else "✗ FAIL"
        print(f"{test_name:.<30} {status}")
    
    passed = sum(results.values())
    total = len(results)
    print(f"\nResults: {passed}/{total} tests passed")
    
    if passed == total:
        print("\n✓ All tests passed! You're ready to run Lumigrid Display Node.")
        return 0
    else:
        print("\n! Some tests failed. Please address the issues above.")
        return 1


if __name__ == '__main__':
    sys.exit(main())
