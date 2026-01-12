# LumiGrid Python Examples

This directory contains Python examples and validation scripts for LumiGrid integration.

## Prerequisites

- Python 3.7 or higher
- pip package manager

## Installation

```bash
pip install -r requirements.txt
```

This will install the required dependencies:
- `jsonschema` - JSON Schema validator
- `requests` - HTTP client for API calls

## Validation Examples

These examples demonstrate how to validate JSON payloads against LumiGrid schemas.

### Sensor Data Validation

```bash
python validate_sensor_data.py
```

Validates sensor update payloads for temperature, humidity, pressure, light, and motion sensors.

**Schema:** `../../schemas/sensor-data.schema.json`

### LED Preset Validation

```bash
python validate_preset_led.py
```

Validates LED effect presets including RGBW color values and effects.

**Schema:** `../../schemas/preset-led.schema.json`

## API Integration Examples

These examples show how to interact with LumiGrid node APIs.

### Sensor Update POST

```bash
# Use default URL (http://192.168.4.1)
python sensor_update_post.py

# Target a specific node
NODE_URL=http://192.168.1.100 python sensor_update_post.py
```

**Demonstrates:**
- Validating sensor data before sending
- POSTing to `/api/sensor_update` endpoint
- Error handling for network requests
- Different sensor reading types

**API Endpoint:** `POST /api/sensor_update`

### Preset CRUD Operations

```bash
# Use default URL (http://192.168.4.1)
python preset_crud.py

# Target a specific node
NODE_URL=http://192.168.1.100 python preset_crud.py
```

**Demonstrates:**
- Create: Add new LED presets
- Read: Retrieve preset by ID
- Update: Modify existing presets
- Delete: Remove presets
- List: Get all presets
- Activate: Apply preset to LEDs

**API Endpoints:**
- `POST /api/presets` - Create preset
- `GET /api/presets` - List all presets
- `GET /api/presets/:id` - Read preset
- `PUT /api/presets/:id` - Update preset
- `DELETE /api/presets/:id` - Delete preset
- `POST /api/presets/:id/activate` - Activate preset

### UDP Sync Communication

```bash
# Use default settings (node-id: example-node-001, port: 5555)
python udp_sync.py

# Customize node ID and port
NODE_ID=led-kitchen-001 UDP_PORT=5555 python udp_sync.py

# Run multiple instances to test inter-node communication
# Terminal 1:
NODE_ID=led-001 python udp_sync.py

# Terminal 2:
NODE_ID=led-002 python udp_sync.py
```

**Demonstrates:**
- Broadcasting UDP packets
- Listening for incoming sync packets
- Validating packets with JSON Schema
- Different packet types:
  - `heartbeat` - Node health status
  - `color_change` - LED color updates
  - `effect_trigger` - Effect coordination
  - `state_update` - State synchronization
  - `config_change` - Configuration updates
- Targeted vs. broadcast messages

## Using as a Library

The examples can be imported and used in your own code:

```python
from sensor_update_post import post_sensor_update
from preset_crud import PresetClient
from udp_sync import UDPSyncClient

# Post sensor data
post_sensor_update('http://192.168.1.100', {
    'node_id': 'my-sensor',
    'readings': {'temperature_c': 23.5}
})

# Manage presets
client = PresetClient('http://192.168.1.100')
client.create({'preset_id': 1, 'name': 'Blue', 'color': {'r':0, 'g':0, 'b':255, 'w':0}})
client.activate(1)

# UDP sync
sync = UDPSyncClient('my-node', 5555)
sync.start_listening()
sync.send_color_change({'r': 255, 'g': 0, 'b': 0, 'w': 0})
```

## Code Generation from Schemas

You can generate Python dataclasses or Pydantic models from the JSON schemas.

### Using dataclasses-json

```bash
pip install dataclasses-json

# Create a generator script (schema_to_dataclass.py)
python schema_to_dataclass.py
```

### Using Pydantic

```bash
pip install pydantic datamodel-code-generator

# Generate Pydantic models
datamodel-codegen --input ../../schemas/sensor-data.schema.json --output models/sensor_data.py
datamodel-codegen --input ../../schemas/preset-led.schema.json --output models/preset_led.py
datamodel-codegen --input ../../schemas/preset-display.schema.json --output models/preset_display.py
datamodel-codegen --input ../../schemas/config.schema.json --output models/config.py
datamodel-codegen --input ../../schemas/sync-packet.schema.json --output models/sync_packet.py
```

### Example Generated Model Usage

```python
from models.sensor_data import SensorDataPayload

# Pydantic will validate on instantiation
sensor_data = SensorDataPayload(
    node_id="sensor-001",
    readings={
        "temperature_c": 23.5,
        "humidity_percent": 45.2
    }
)

# Convert to dict for JSON serialization
data_dict = sensor_data.dict()
```

## Best Practices

1. **Always Validate Before Sending**: Use schema validation to check data before making API calls
2. **Handle Errors Gracefully**: Network requests can fail; implement proper error handling
3. **Use Environment Variables**: Configure node URLs and IDs via environment variables for flexibility
4. **Type Hints**: Use Python type hints for better IDE support and code clarity
5. **Logging**: Use Python's logging module instead of print statements in production code

## Common Issues

### Module Not Found
```bash
# Make sure dependencies are installed
pip install -r requirements.txt
```

### Connection Refused
- Ensure the target node is powered on and connected to the network
- Check the IP address is correct
- Verify firewall settings allow the connection

### Validation Errors
- Check that your data matches the schema requirements
- Review the error messages from jsonschema for specific field issues
- Refer to the schema files in `../../schemas/` for field definitions

## Additional Resources

- [JSON Schema Documentation](https://json-schema.org/)
- [jsonschema Python Library](https://python-jsonschema.readthedocs.io/)
- [Requests Documentation](https://requests.readthedocs.io/)
- [Pydantic Documentation](https://docs.pydantic.dev/)
- LumiGrid Schemas: `../../schemas/README.md`
