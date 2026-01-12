# LumiGrid JSON Schemas

This directory contains JSON Schema files that define consistent API contracts across all LumiGrid nodes.

## Schema Files

### 1. `preset-led.schema.json`
Defines the structure for LED effect presets including RGBW color values and optional effects.

**Use cases:**
- Storing button color presets
- Defining LED effect configurations
- API endpoints for saving/loading LED states

### 2. `preset-display.schema.json`
Defines the structure for display frame presets with pixel data.

**Use cases:**
- Storing display frame configurations
- Matrix LED display patterns
- Animation frame definitions

### 3. `sensor-data.schema.json`
Defines the structure for sensor update payloads from nodes.

**Use cases:**
- Sensor reading transmissions
- Environmental monitoring data
- Motion and light detection payloads

### 4. `config.schema.json`
Defines the structure for node configuration settings.

**Use cases:**
- Initial node configuration
- Runtime configuration updates
- Persistent settings storage

### 5. `sync-packet.schema.json`
Defines the structure for UDP synchronization packets between nodes.

**Use cases:**
- Inter-node communication
- State synchronization
- Distributed effect coordination

## Usage

These schemas can be used for:
- **Validation**: Validate JSON payloads before processing
- **Documentation**: Auto-generate API documentation
- **Code Generation**: Generate typed interfaces/classes
- **Testing**: Create test fixtures with valid data structures

## Validation Example

Using a JSON Schema validator in your code:

```javascript
// Node.js example
const Ajv = require('ajv');
const ajv = new Ajv();
const schema = require('./schemas/sensor-data.schema.json');
const validate = ajv.compile(schema);

const data = {
  node_id: "node-001",
  readings: {
    temperature_c: 23.5,
    humidity_percent: 45.2
  }
};

if (validate(data)) {
  console.log('Valid sensor data');
} else {
  console.error('Invalid:', validate.errors);
}
```

```python
# Python example
import json
import jsonschema

with open('schemas/sensor-data.schema.json') as f:
    schema = json.load(f)

data = {
    "node_id": "node-001",
    "readings": {
        "temperature_c": 23.5,
        "humidity_percent": 45.2
    }
}

try:
    jsonschema.validate(data, schema)
    print("Valid sensor data")
except jsonschema.ValidationError as e:
    print(f"Invalid: {e}")
```

## Schema Version

All schemas use JSON Schema Draft 07 (`http://json-schema.org/draft-07/schema#`).

## Contributing

When modifying schemas:
1. Ensure backward compatibility when possible
2. Update version numbers for breaking changes
3. Validate JSON syntax before committing
4. Update this README if adding new schemas
