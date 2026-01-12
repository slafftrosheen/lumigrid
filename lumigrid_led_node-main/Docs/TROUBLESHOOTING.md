# LumiGrid LED Node - Troubleshooting Guide

**Version:** 1.0.0  
**Last Updated:** 2026-01-10

---

## Table of Contents

1. [Build and Flash Issues](#build-and-flash-issues)
2. [Hardware Issues](#hardware-issues)
3. [Network and Connectivity](#network-and-connectivity)
4. [Performance Issues](#performance-issues)
5. [Diagnostic Tools](#diagnostic-tools)
6. [Factory Reset](#factory-reset)
7. [Getting Help](#getting-help)

---

## Build and Flash Issues

### Device won't flash / Serial port not found

**Symptoms:**
- `idf.py flash` fails with "serial port not found"
- Device not recognized when connected via USB

**Solutions:**
1. **Check USB Cable:** Ensure you're using a data-capable USB cable, not a charge-only cable
2. **Install Drivers:**
   - Linux: Install USB serial drivers and add user to dialout group
     ```bash
     sudo apt-get install -y dialout
     sudo usermod -a -G dialout $USER
     # Log out and log back in
     ```
   - macOS: Install drivers from https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
   - Windows: Install CH340 or CP210x drivers

3. **Check Port:**
   ```bash
   # Linux/macOS
   ls /dev/ttyUSB* /dev/ttyACM*
   
   # Specify port explicitly
   idf.py -p /dev/ttyUSB0 flash
   ```

4. **Try Different USB Port:** Some USB ports may not provide enough power

### Build fails with "command not found: idf.py"

**Cause:** ESP-IDF environment not activated

**Solution:**
```bash
# Activate ESP-IDF
. ~/esp/esp-idf/export.sh

# Make permanent by adding to ~/.bashrc or ~/.zshrc
echo '. ~/esp/esp-idf/export.sh' >> ~/.bashrc
```

### Build fails with compilation errors

**Common Causes:**
1. **Wrong ESP-IDF version:** LumiGrid requires ESP-IDF v5.3+
   ```bash
   idf.py --version
   ```

2. **Incorrect target:** Ensure target is set to esp32
   ```bash
   idf.py set-target esp32
   ```

3. **Corrupted build:** Clean and rebuild
   ```bash
   idf.py fullclean
   idf.py build
   ```

---

## Hardware Issues

### LEDs not working or showing wrong colors

**Checklist:**

1. **Power Supply:**
   - Verify 5V DC ±5%
   - Ensure sufficient current (minimum 2A for controller + LED load)
   - Check power supply connections are secure

2. **Wiring:**
   - ESP32 GND must be connected to LED power supply GND
   - Data line should have 470Ω resistor between ESP32 and first LED
   - Verify GPIO pins match configuration

3. **LED Configuration:**
   - Check LED type in firmware config matches your strips
   - For WS2812B: Use default settings
   - For SK6812: May need timing adjustments

4. **Brightness:**
   - Try lower brightness values to reduce current draw
   - Check if LEDs work at brightness < 50

5. **LED Count:**
   - Verify LED count per channel doesn't exceed 300
   - Check `/api/status` for FPS - should be 30+ for smooth effects

### PCA9685 PWM not working

**Checklist:**

1. **I²C Connection:**
   - SDA on GPIO21
   - SCL on GPIO22
   - Pull-up resistors (4.7kΩ) on SDA and SCL
   - Common ground with ESP32

2. **I²C Address:**
   - Default: 0x40
   - Check for address conflicts: `i2cdetect` command in ESP-IDF

3. **Output Enable:**
   - GPIO25 (OE#) must be LOW for outputs to be active
   - Check if OE# pin is properly connected

4. **Power:**
   - PCA9685 VCC should be 3.3V or 5V
   - V+ determines output voltage (typically 5V)

---

## Network and Connectivity

### Can't connect to Wi-Fi Access Point

**Default Credentials:**
- SSID: `LumiGrid-XXXXXX` (XXXXXX = last 6 characters of MAC address)
- Password: `lumigrid123`
- IP: `192.168.4.1`

**Solutions:**

1. **Find actual SSID:**
   - Connect device via USB
   - Run `idf.py monitor` to see serial output
   - Look for AP configuration messages

2. **Network visibility:**
   - ESP32 only supports 2.4GHz Wi-Fi
   - Some phones hide 2.4GHz networks by default
   - Check "Advanced" or "Show all networks" in Wi-Fi settings

3. **Factory reset:** See [Factory Reset](#factory-reset) section

### Can't connect to existing Wi-Fi network

**Troubleshooting:**

1. **Check credentials:**
   - Verify SSID and password are correct
   - Check for special characters in password

2. **Network requirements:**
   - 2.4GHz network only (5GHz not supported)
   - WPA/WPA2 security (WPA3 may not work)
   - No enterprise authentication

3. **Signal strength:**
   - Move device closer to router
   - Check signal strength in serial monitor

4. **Check configuration:**
   ```bash
   curl http://192.168.4.1/api/config
   ```

### Web UI not loading

**Solutions:**

1. **Browser cache:**
   - Clear cache: Ctrl+Shift+Del
   - Hard refresh: Ctrl+Shift+R
   - Try incognito/private mode

2. **Check IP address:**
   ```bash
   # Via serial monitor
   idf.py monitor
   
   # Via mDNS (if supported)
   http://lumigrid.local/
   ```

3. **Network connectivity:**
   - Verify device and computer are on same network
   - Try ping: `ping <device-ip>`
   - Use http:// not https://

4. **Firewall:**
   - Temporarily disable firewall to test
   - Allow port 80/TCP

### Master/Slave synchronization not working

**Requirements:**
- All devices on same subnet
- Multicast enabled on network
- UDP port 4210 open
- Multicast address: 239.1.1.1

**Troubleshooting:**

1. **Check role configuration:**
   ```bash
   curl http://192.168.1.100/api/status | grep role
   ```

2. **Verify network:**
   - All devices should have IPs in same subnet (e.g., 192.168.1.x)
   - Router must support multicast (some disable it)

3. **Firewall:**
   ```bash
   # Linux: Allow multicast
   sudo iptables -A INPUT -p udp --dport 4210 -j ACCEPT
   ```

4. **Test multicast:**
   - Use `tcpdump` or Wireshark to verify multicast packets
   - Look for packets on 239.1.1.1:4210

---

## Performance Issues

### Low FPS / Effects running slowly

**Causes and Solutions:**

1. **Too many LEDs:**
   - Reduce LED count per channel (max 300 recommended)
   - Disable unused channels

2. **Memory:**
   - Check heap: `curl http://<ip>/api/status | grep heap`
   - Free heap should be >50KB
   - If low, reduce LED count or simplify effects

3. **CPU load:**
   - Simplify effect parameters
   - Reduce refresh rate
   - Check for other background tasks

4. **Network:**
   - MQTT or sync protocol may impact performance
   - Disable if not needed

### Device crashes or reboots

**Check serial monitor for:**
- Panic messages
- Stack traces
- Memory allocation failures

**Common causes:**

1. **Power issues:**
   - Insufficient power supply
   - Voltage drops under LED load
   - Add bulk capacitor (1000µF) across power supply

2. **Memory issues:**
   - Heap fragmentation
   - Stack overflow
   - Reduce LED count

3. **Software bugs:**
   - Update to latest firmware
   - Report issue with stack trace

---

## Diagnostic Tools

### Check System Status

```bash
curl http://192.168.1.100/api/status | jq
```

**Key metrics:**
- `uptime_ms`: Time since boot
- `heap_free_kb`: Available memory
- `fps`: Frame rate per channel
- `connected`: Wi-Fi status

### Serial Monitor

```bash
idf.py -p /dev/ttyUSB0 monitor
```

**Useful for:**
- Boot messages
- Error logs
- Wi-Fi connection status
- Heap memory warnings

### Memory diagnostics

In serial monitor, look for:
```
I (12345) main: Free heap: 112KB
```

Low values (<50KB) indicate memory pressure.

### Network diagnostics

```bash
# Ping device
ping 192.168.1.100

# Check open ports
nmap 192.168.1.100

# Monitor HTTP requests
curl -v http://192.168.1.100/api/status
```

---

## Factory Reset

**Method 1: Hardware (Boot Button)**

1. Power off the device
2. Hold the BOOT button (GPIO0)
3. Power on while holding BOOT
4. Keep holding for 3 seconds
5. Release button
6. Device creates default AP

**Method 2: API**

```bash
curl -X POST http://192.168.1.100/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "factory_reset"}'
```

**⚠️ Warning:** Factory reset erases:
- All presets
- Wi-Fi configuration
- MQTT settings
- Saved sequences
- All user data

---

## Getting Help

### Before Reporting Issues

Collect this information:

1. **Version info:**
   ```bash
   idf.py --version
   curl http://<ip>/api/status
   ```

2. **Serial output:**
   - Run `idf.py monitor`
   - Capture relevant logs

3. **Configuration:**
   ```bash
   curl http://<ip>/api/config
   ```

### Where to Get Help

- **GitHub Issues:** https://github.com/algorhythmicdev/lumigrid_led_node/issues
- **Documentation:** https://github.com/algorhythmicdev/lumigrid_led_node/tree/main/Docs

### Reporting Bugs

Include:
- ESP-IDF version
- Hardware details (board, LED type, count)
- Steps to reproduce
- Serial monitor output
- `/api/status` output
- Expected vs actual behavior

---

## Common Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| `E (xxx) wifi: failed to connect` | Wi-Fi credentials wrong | Check SSID/password |
| `E (xxx) heap: allocation failed` | Out of memory | Reduce LED count |
| `E (xxx) rmt: timeout` | LED data line issue | Check wiring, resistor |
| `E (xxx) i2c: i2c_master_cmd_begin failed` | I²C communication error | Check PCA9685 wiring |
| `Guru Meditation Error` | Fatal crash | Check serial output for details |

---

**Still having issues?** Check the [Setup Guide](SETUP_GUIDE.md) and [API Reference](API_REFERENCE.md), or open an issue on GitHub.
