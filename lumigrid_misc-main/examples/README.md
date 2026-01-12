# LumiGrid Integration Examples

This directory contains comprehensive examples, validation routines, and API integration code for LumiGrid nodes across multiple programming languages and platforms.

## Directory Structure

```
examples/
├── nodejs/          # Node.js/JavaScript examples
├── python/          # Python examples
├── arduino/         # Arduino/ESP32 examples
└── README.md        # This file
```

## Quick Start

### Node.js

```bash
cd nodejs
npm install
npm test                    # Run all validations
npm run example:sensor-post # Test sensor POST
npm run example:udp-sync    # Test UDP sync
```

### Python

```bash
cd python
pip install -r requirements.txt
python validate_sensor_data.py
python sensor_update_post.py
python udp_sync.py
```

### Arduino/ESP32

1. Open `arduino/sensor_bridge_esp32.ino` in Arduino IDE
2. Install required libraries (ArduinoJson, DHT sensor)
3. Configure WiFi credentials and node URL
4. Upload to ESP32 board

## What's Included

### ✅ Validation Examples

Test your data against JSON schemas before sending:

- **Sensor Data** - Temperature, humidity, pressure, light, motion
- **LED Presets** - RGBW colors with effects (static, fade, pulse, rainbow, strobe)
- **Display Presets** - Matrix display frames with pixel data
- **Node Config** - Network, hardware, sync settings
- **UDP Sync Packets** - Inter-node communication messages

### 🔌 API Integration Examples

Complete working examples for all LumiGrid APIs:

- **Sensor Update POST** - Send sensor readings to `/api/sensor_update`
- **Preset CRUD** - Create, Read, Update, Delete LED presets
- **UDP Sync** - Send and receive synchronization packets
- **Config Management** - Node configuration examples

### 🛠️ Code Generation Tools

Learn how to generate strongly-typed code from schemas:

- **TypeScript** - Generate interfaces for compile-time safety
- **Python** - Create Pydantic models or dataclasses
- **C++** - Generate structs for Arduino/ESP32 projects

### 📚 Documentation

Each directory includes comprehensive README files with:

- Installation instructions
- Usage examples
- API endpoint documentation
- Troubleshooting guides
- Best practices

## Language-Specific Examples

### Node.js Examples

**Location:** `nodejs/`

**What you'll find:**
- JSON Schema validation with Ajv
- HTTP client examples with Axios
- UDP broadcast/receive with dgram
- Reusable client classes
- Complete CRUD operations

**Run examples:**
```bash
cd nodejs && npm install
node validate-sensor-data.js
node sensor-update-post.js
node preset-crud.js
node udp-sync.js
```

### Python Examples

**Location:** `python/`

**What you'll find:**
- JSON Schema validation with jsonschema
- HTTP client examples with requests
- UDP socket programming
- Type hints and clean Python code
- Reusable client classes

**Run examples:**
```bash
cd python && pip install -r requirements.txt
python validate_sensor_data.py
python sensor_update_post.py
python preset_crud.py
python udp_sync.py
```

### Arduino/ESP32 Examples

**Location:** `arduino/`

**What you'll find:**
- Complete sensor bridge implementation
- WiFi connectivity management
- JSON payload construction with ArduinoJson
- HTTP POST client
- Multi-sensor support (DHT22, BH1750, PIR)
- Schema-compliant data structures

**Hardware supported:**
- ESP32 development boards
- DHT22 temperature/humidity sensor
- BH1750 light sensor (I2C)
- PIR motion sensors

## API Endpoints Reference

LumiGrid nodes expose the following REST API endpoints:

### Sensor Updates
```
POST /api/sensor_update
Content-Type: application/json

{
  "node_id": "sensor-001",
  "readings": {
    "temperature_c": 23.5,
    "humidity_percent": 48.3
  }
}
```

### Preset Management
```
POST   /api/presets              # Create preset
GET    /api/presets              # List all presets
GET    /api/presets/:id          # Get preset by ID
PUT    /api/presets/:id          # Update preset
DELETE /api/presets/:id          # Delete preset
POST   /api/presets/:id/activate # Activate preset
```

### UDP Sync Packets
```
UDP Broadcast on port 5555 (configurable)

{
  "packet_type": "color_change",
  "node_id": "led-001",
  "timestamp": 1704710400000,
  "sequence_number": 42,
  "payload": {
    "color": {"r": 255, "g": 0, "b": 0, "w": 0}
  },
  "target_nodes": []
}
```

## Schema Documentation

All schemas are located in `/schemas/` directory:

- **sensor-data.schema.json** - Sensor reading payloads
- **preset-led.schema.json** - LED effect presets (RGBW)
- **preset-display.schema.json** - Display frame presets
- **config.schema.json** - Node configuration
- **sync-packet.schema.json** - UDP sync messages

See `/schemas/README.md` for detailed schema documentation.

## Code Generation from Schemas

### TypeScript Type Generation

Generate TypeScript interfaces for compile-time type safety:

```bash
npm install -g json-schema-to-typescript

# Generate types
json-schema-to-typescript ../schemas/sensor-data.schema.json > types/sensor-data.d.ts
json-schema-to-typescript ../schemas/preset-led.schema.json > types/preset-led.d.ts
json-schema-to-typescript ../schemas/config.schema.json > types/config.d.ts
```

**Usage in TypeScript:**
```typescript
import { SensorDataPayload } from './types/sensor-data';

const data: SensorDataPayload = {
  node_id: "sensor-001",
  readings: {
    temperature_c: 23.5
  }
};
// TypeScript enforces schema structure at compile time
```

### Python Model Generation

Generate Pydantic models for runtime validation:

```bash
pip install datamodel-code-generator

# Generate Pydantic models
datamodel-codegen \
  --input ../schemas/sensor-data.schema.json \
  --output models/sensor_data.py

datamodel-codegen \
  --input ../schemas/preset-led.schema.json \
  --output models/preset_led.py
```

**Usage in Python:**
```python
from models.sensor_data import SensorDataPayload

# Pydantic validates on instantiation
data = SensorDataPayload(
    node_id="sensor-001",
    readings={
        "temperature_c": 23.5
    }
)

# Serialize to dict/JSON
json_data = data.dict()
```

### C++ Struct Generation

Generate C++ code for Arduino/ESP32 projects:

```bash
npm install -g quicktype

# Generate C++ headers
quicktype ../schemas/sensor-data.schema.json -o sensor_data.h --lang c++
quicktype ../schemas/preset-led.schema.json -o preset_led.h --lang c++
```

**Usage in C++:**
```cpp
#include "sensor_data.h"

SensorData data;
data.node_id = "sensor-001";
data.readings.temperature_c = 23.5;

String json = data.toJson();
```

## Integration Patterns

### Sensor Node Pattern

1. **Read sensors** (temperature, humidity, etc.)
2. **Construct JSON** matching sensor-data schema
3. **Validate** against schema (optional but recommended)
4. **POST** to `/api/sensor_update`
5. **Handle response** and errors

**Example platforms:**
- ESP32 + sensors → POST to LumiGrid node
- Raspberry Pi + sensors → POST to node
- Server monitoring → POST metrics

### Controller Node Pattern

1. **Create presets** for different scenes/moods
2. **Store** using `/api/presets` CRUD operations
3. **Activate** presets based on triggers
4. **Sync** with other nodes via UDP

**Example use cases:**
- Physical button → activate preset
- Time-based automation → switch presets
- Sensor trigger → change lighting

### Gateway Node Pattern

1. **Receive** data from multiple sensors
2. **Aggregate** and process
3. **Broadcast** state via UDP sync
4. **Coordinate** effects across nodes

**Example architectures:**
- Central controller + multiple LED nodes
- Mesh network with state synchronization
- Cloud gateway + local nodes

## Testing Your Integration

### 1. Validate Your Data

Always validate before sending:

```javascript
// Node.js
const validate = ajv.compile(schema);
if (!validate(data)) {
  console.error('Invalid:', validate.errors);
}
```

```python
# Python
from jsonschema import validate, ValidationError
try:
    validate(data, schema)
except ValidationError as e:
    print(f'Invalid: {e}')
```

### 2. Test API Endpoints

Use the provided examples to test each endpoint:

```bash
# Test sensor POST
NODE_URL=http://192.168.1.100 node sensor-update-post.js

# Test preset CRUD
NODE_URL=http://192.168.1.100 node preset-crud.js

# Test UDP sync (run in two terminals)
NODE_ID=node-1 node udp-sync.js
NODE_ID=node-2 node udp-sync.js
```

### 3. Monitor Network Traffic

Use tools to inspect the actual HTTP/UDP traffic:

```bash
# Monitor HTTP traffic
tcpdump -i any -A 'port 80'

# Monitor UDP sync packets
tcpdump -i any -A 'port 5555'
```

## Best Practices

### Security
- **Never hardcode credentials** - use environment variables
- **Validate all inputs** - check data against schemas
- **Use HTTPS** when possible for API calls
- **Sanitize sensor data** before processing

### Performance
- **Batch updates** when possible
- **Cache presets** locally
- **Rate limit** API calls
- **Use UDP for real-time sync** (faster than HTTP)

### Reliability
- **Handle network failures** gracefully
- **Retry with exponential backoff**
- **Log errors** for debugging
- **Monitor node health** with heartbeats

### Code Quality
- **Use type systems** (TypeScript, Python type hints)
- **Follow schema strictly** for compatibility
- **Write tests** for critical paths
- **Document your integration**

## Troubleshooting

### Common Issues

**"Module not found" errors:**
```bash
# Node.js
cd nodejs && npm install

# Python
cd python && pip install -r requirements.txt
```

**Network connection failures:**
- Verify node is powered on and connected
- Check IP address is correct
- Test with `ping <node-ip>`
- Check firewall settings

**Validation errors:**
- Compare your data to schema examples
- Check required fields are present
- Verify data types match schema
- Review error messages carefully

**UDP packets not received:**
- Ensure all nodes use same port
- Check broadcast address is correct
- Verify firewall allows UDP traffic
- Test on same subnet first

## Additional Resources

- **Schema Documentation:** `/schemas/README.md`
- **Original RF Remote:** `/lumigrid_RFremote.ino`
- **JSON Schema Spec:** https://json-schema.org/
- **Ajv (Node.js):** https://ajv.js.org/
- **jsonschema (Python):** https://python-jsonschema.readthedocs.io/
- **ArduinoJson:** https://arduinojson.org/

## Contributing

When adding new examples:

1. Follow the existing code structure
2. Include comprehensive comments
3. Validate against schemas
4. Add error handling
5. Update relevant README
6. Test on actual hardware when possible

## Support

For issues or questions:
- Check the troubleshooting sections in READMEs
- Review schema definitions in `/schemas/`
- Examine existing examples for patterns
- Test with the validation scripts first
