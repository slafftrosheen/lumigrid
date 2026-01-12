# LumiGrid Node.js Examples

This directory contains Node.js examples and validation scripts for LumiGrid integration.

## Prerequisites

- Node.js 14.x or higher
- npm or yarn package manager

## Installation

```bash
npm install
```

This will install the required dependencies:
- `ajv` - JSON Schema validator
- `ajv-formats` - Format validators (e.g., IPv4)
- `axios` - HTTP client for API calls
- `dgram` - UDP socket support (built-in)

## Validation Examples

These examples demonstrate how to validate JSON payloads against LumiGrid schemas before sending them to nodes.

### Run All Validations

```bash
npm test
# or
node validate-all.js
```

### Individual Validation Scripts

#### Sensor Data Validation
```bash
npm run validate:sensor
# or
node validate-sensor-data.js
```

Validates sensor update payloads for temperature, humidity, pressure, light, and motion sensors.

**Schema:** `../../schemas/sensor-data.schema.json`

#### LED Preset Validation
```bash
npm run validate:preset-led
# or
node validate-preset-led.js
```

Validates LED effect presets including RGBW color values and effects.

**Schema:** `../../schemas/preset-led.schema.json`

#### Display Preset Validation
```bash
npm run validate:preset-display
# or
node validate-preset-display.js
```

Validates display frame presets with pixel data for matrix LED displays.

**Schema:** `../../schemas/preset-display.schema.json`

#### Node Configuration Validation
```bash
npm run validate:config
# or
node validate-config.js
```

Validates node configuration including network settings, hardware pins, and sync options.

**Schema:** `../../schemas/config.schema.json`

#### UDP Sync Packet Validation
```bash
npm run validate:sync
# or
node validate-sync-packet.js
```

Validates UDP synchronization packets for inter-node communication.

**Schema:** `../../schemas/sync-packet.schema.json`

## API Integration Examples

These examples show how to interact with LumiGrid node APIs.

### Sensor Update POST

```bash
# Use default URL (http://192.168.4.1)
node sensor-update-post.js

# Target a specific node
NODE_URL=http://192.168.1.100 node sensor-update-post.js
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
node preset-crud.js

# Target a specific node
NODE_URL=http://192.168.1.100 node preset-crud.js
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
node udp-sync.js

# Customize node ID and port
NODE_ID=led-kitchen-001 UDP_PORT=5555 node udp-sync.js

# Run multiple instances to test inter-node communication
# Terminal 1:
NODE_ID=led-001 node udp-sync.js

# Terminal 2:
NODE_ID=led-002 node udp-sync.js
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

```javascript
const { postSensorUpdate } = require('./sensor-update-post.js');
const { PresetClient } = require('./preset-crud.js');
const { UDPSyncClient } = require('./udp-sync.js');

// Post sensor data
await postSensorUpdate('http://192.168.1.100', {
  node_id: 'my-sensor',
  readings: { temperature_c: 23.5 }
});

// Manage presets
const client = new PresetClient('http://192.168.1.100');
await client.create({ preset_id: 1, name: 'Blue', color: { r:0, g:0, b:255, w:0 } });
await client.activate(1);

// UDP sync
const sync = new UDPSyncClient('my-node', 5555);
sync.startListening();
sync.sendColorChange({ r: 255, g: 0, b: 0, w: 0 });
```

## Code Generation from Schemas

You can generate TypeScript types from the JSON schemas using tools like:

### json-schema-to-typescript

```bash
npm install -g json-schema-to-typescript

# Generate TypeScript types
json-schema-to-typescript ../../schemas/sensor-data.schema.json > types/sensor-data.d.ts
json-schema-to-typescript ../../schemas/preset-led.schema.json > types/preset-led.d.ts
json-schema-to-typescript ../../schemas/preset-display.schema.json > types/preset-display.d.ts
json-schema-to-typescript ../../schemas/config.schema.json > types/config.d.ts
json-schema-to-typescript ../../schemas/sync-packet.schema.json > types/sync-packet.d.ts
```

This generates TypeScript interfaces that provide compile-time type safety for your LumiGrid integration.

### Example Generated Type Usage

```typescript
import { SensorDataPayload } from './types/sensor-data';

const sensorData: SensorDataPayload = {
  node_id: "sensor-001",
  readings: {
    temperature_c: 23.5,
    humidity_percent: 45.2
  }
};
// TypeScript will enforce the schema structure at compile time
```

## Best Practices

1. **Always Validate Before Sending**: Use the compiled validators to check data before making API calls
2. **Handle Errors Gracefully**: Network requests can fail; implement proper error handling
3. **Use Environment Variables**: Configure node URLs and IDs via environment variables for flexibility
4. **Schema Validation is Fast**: Compiled Ajv validators are highly optimized for production use
5. **Type Safety**: Use TypeScript and generated types for compile-time validation

## Common Issues

### Module Not Found
```bash
# Make sure dependencies are installed
npm install
```

### Connection Refused
- Ensure the target node is powered on and connected to the network
- Check the IP address is correct
- Verify firewall settings allow the connection

### Validation Errors
- Check that your data matches the schema requirements
- Review the error messages from Ajv for specific field issues
- Refer to the schema files in `../../schemas/` for field definitions

## Additional Resources

- [JSON Schema Documentation](https://json-schema.org/)
- [Ajv Documentation](https://ajv.js.org/)
- [Axios Documentation](https://axios-http.com/)
- LumiGrid Schemas: `../../schemas/README.md`
