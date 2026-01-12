# LumiGrid Arduino/ESP32 Examples

This directory contains Arduino and ESP32 examples for LumiGrid integration.

## Overview

These examples demonstrate how to integrate sensors and devices with LumiGrid nodes using Arduino-compatible boards, particularly ESP32.

## Examples

### Sensor Bridge (ESP32)

**File:** `sensor_bridge_esp32.ino`

A complete example showing how to read sensor data and POST it to a LumiGrid node's `/api/sensor_update` endpoint.

**Features:**
- WiFi connectivity management
- DHT22 temperature/humidity sensor support
- Optional BH1750 light sensor (I2C)
- Optional PIR motion sensor
- JSON payload construction with ArduinoJson
- HTTP POST to LumiGrid API
- Schema-compliant data structure
- Status LED feedback
- Configurable update interval

**Hardware Requirements:**
- ESP32 development board
- DHT22 temperature/humidity sensor
- Optional: BH1750 light sensor (I2C)
- Optional: PIR motion sensor
- Breadboard and jumper wires

**Wiring:**
```
DHT22:
  VCC  -> 3.3V
  DATA -> GPIO 4
  GND  -> GND

BH1750 (I2C - optional):
  VCC -> 3.3V
  SDA -> GPIO 21
  SCL -> GPIO 22
  GND -> GND

PIR Sensor (optional):
  VCC    -> 5V
  SIGNAL -> GPIO 5
  GND    -> GND
```

**Required Libraries:**
- WiFi (built-in with ESP32)
- HTTPClient (built-in with ESP32)
- ArduinoJson (install via Library Manager)
- DHT sensor library (install via Library Manager)
- Optional: BH1750 library (if using light sensor)

**Installation:**

1. Install Arduino IDE or PlatformIO
2. Install ESP32 board support:
   - In Arduino IDE: File > Preferences > Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools > Board > Boards Manager > Search "ESP32" > Install

3. Install required libraries:
   - Sketch > Include Library > Manage Libraries
   - Search and install:
     - "ArduinoJson" by Benoit Blanchon
     - "DHT sensor library" by Adafruit
     - "BH1750" by Christopher Laws (if using light sensor)

4. Configure the sketch:
   - Update `WIFI_SSID` and `WIFI_PASSWORD`
   - Set `LUMIGRID_NODE_URL` to your node's IP address
   - Customize `NODE_ID` and `NODE_NAME`
   - Enable/disable sensors by commenting/uncommenting `#define` statements
   - Adjust `UPDATE_INTERVAL` as needed

5. Upload to ESP32:
   - Tools > Board > ESP32 Dev Module
   - Tools > Port > Select your ESP32 port
   - Upload

**Usage:**

After uploading, the ESP32 will:
1. Connect to WiFi
2. Initialize configured sensors
3. Read sensor data every `UPDATE_INTERVAL` milliseconds
4. Construct a JSON payload matching the sensor-data schema
5. POST the data to `http://<node-ip>/api/sensor_update`
6. Display results in Serial Monitor (115200 baud)

**Example Output:**
```
=== LumiGrid Sensor Bridge ===
Node ID: sensor-esp32-001
Node Name: ESP32 Sensor Bridge

Initializing sensors...
  ✓ DHT22 initialized
  ✓ PIR motion sensor initialized

Connecting to WiFi: MyWiFi... Connected!
IP Address: 192.168.1.50

Setup complete. Starting sensor readings...

--- Sensor Update ---
  Temperature: 23.5°C
  Humidity: 48.3%
  Motion: NO

JSON Payload:
{"node_id":"sensor-esp32-001","node_name":"ESP32 Sensor Bridge","timestamp":30,"readings":{"temperature_c":23.5,"humidity_percent":48.3,"motion_detected":false}}

POSTing to: http://192.168.1.100/api/sensor_update
✓ Response code: 200
  Response: OK
```

## Schema Compliance

All examples follow the LumiGrid JSON schemas defined in `/schemas/`:

### sensor-data.schema.json

**Required fields:**
- `node_id` (string, minLength: 1)
- `readings` (object)

**Optional fields:**
- `node_name` (string)
- `timestamp` (integer, minimum: 0)

**Readings fields** (all optional):
- `temperature_c` (number)
- `humidity_percent` (number, 0-100)
- `pressure_hpa` (number, minimum: 0)
- `light_lux` (number, minimum: 0)
- `motion_detected` (boolean)

## Code Generation for C++

You can generate C++ structures from JSON schemas using tools like:

### quicktype

```bash
npm install -g quicktype

# Generate C++ headers
quicktype ../../schemas/sensor-data.schema.json -o sensor_data.h --lang c++
quicktype ../../schemas/preset-led.schema.json -o preset_led.h --lang c++
quicktype ../../schemas/config.schema.json -o config.h --lang c++
```

This generates C++ structs with serialization/deserialization methods.

### Example Generated Code Usage

```cpp
#include "sensor_data.h"

// Create sensor data
SensorData data;
data.node_id = "sensor-001";
data.readings.temperature_c = 23.5;
data.readings.humidity_percent = 48.3;

// Serialize to JSON
String json = data.toJson();

// Deserialize from JSON
SensorData received = SensorData::fromJson(jsonString);
```

## Best Practices

1. **Error Handling**: Always check sensor read results (NaN, -1, etc.)
2. **WiFi Management**: Monitor connection status and reconnect if needed
3. **Memory Management**: Use StaticJsonDocument for predictable memory usage
4. **Update Interval**: Balance between data freshness and network load
5. **Sensor Accuracy**: Round floating-point values appropriately
6. **Power Management**: Consider deep sleep for battery-powered sensors
7. **Schema Validation**: Ensure your JSON matches the schema structure

## Expanding the Bridge

You can easily add more sensors:

1. **Add sensor library** to the sketch
2. **Define pin** in configuration section
3. **Initialize sensor** in `initializeSensors()`
4. **Read data** in `sendSensorUpdate()`
5. **Add to readings** object with appropriate key

Example for adding a pressure sensor:

```cpp
#ifdef USE_BMP280
  #include <Adafruit_BMP280.h>
  Adafruit_BMP280 bmp;
#endif

// In initializeSensors():
#ifdef USE_BMP280
  if (bmp.begin()) {
    Serial.println("  ✓ BMP280 initialized");
  }
#endif

// In sendSensorUpdate():
#ifdef USE_BMP280
  float pressure = bmp.readPressure() / 100.0;  // Convert to hPa
  readings["pressure_hpa"] = round(pressure * 10) / 10.0;
  Serial.printf("  Pressure: %.1f hPa\n", pressure);
#endif
```

## Troubleshooting

### WiFi Connection Issues
- Verify SSID and password are correct
- Check signal strength
- Ensure ESP32 is within range
- Try 2.4GHz network (ESP32 doesn't support 5GHz)

### Sensor Reading Errors
- Check wiring connections
- Verify power supply voltage (3.3V vs 5V)
- Test sensors individually
- Check I2C address (use I2C scanner sketch)

### HTTP POST Failures
- Verify node URL is correct and reachable
- Check if node's API endpoint is implemented
- Look for firewall issues
- Monitor Serial output for error messages

### Memory Issues
- Reduce JsonDocument size if needed
- Use StaticJsonDocument instead of DynamicJsonDocument
- Free unused libraries

## Additional Resources

- [ESP32 Documentation](https://docs.espressif.com/projects/arduino-esp32/)
- [ArduinoJson Documentation](https://arduinojson.org/)
- [DHT Sensor Guide](https://learn.adafruit.com/dht)
- LumiGrid Schemas: `../../schemas/README.md`
- Existing LumiGrid RF Remote: `../../lumigrid_RFremote.ino`
