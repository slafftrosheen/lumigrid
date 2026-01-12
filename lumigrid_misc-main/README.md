# lumigrid_misc

LumiGrid miscellaneous files - integration toolbox with API shims, schema validation, and usage examples.

## Contents

- **[schemas/](schemas/)** - JSON Schema definitions for all LumiGrid APIs
- **[examples/](examples/)** - Code examples in Node.js, Python, and Arduino/ESP32
- **[lumigrid_RFremote.ino](lumigrid_RFremote.ino)** - ESP32 RF remote controller implementation

## Quick Start

### 1. Install Dependencies

To install dependencies for the Node.js and Python examples, run the following command from the repository root:

```bash
make install
```

### 2. Validate Your Data

**Node.js:**
```bash
cd examples/nodejs
node validate-sensor-data.js
```

**Python:**
```bash
cd examples/python
python validate_sensor_data.py
```

### Use the APIs

**Send sensor data:**
```bash
cd examples/nodejs
NODE_URL=http://192.168.1.100 node sensor-update-post.js
```

**Manage LED presets:**
```bash
cd examples/nodejs
NODE_URL=http://192.168.1.100 node preset-crud.js
```

**Test UDP sync:**
```bash
cd examples/nodejs
NODE_ID=led-001 node udp-sync.js
```

### Arduino Sensor Bridge

See [examples/arduino/](examples/arduino/) for a complete ESP32 sensor bridge that POSTs data to LumiGrid nodes.

## Documentation

- **[Interop Contracts](docs/INTEROP.md)** - Node discovery, event, and data contracts
- **[Schemas Documentation](schemas/README.md)** - JSON Schema reference
- **[Examples Overview](examples/README.md)** - Integration examples and patterns
- **[Node.js Examples](examples/nodejs/README.md)** - JavaScript/TypeScript integration
- **[Python Examples](examples/python/README.md)** - Python integration
- **[Arduino Examples](examples/arduino/README.md)** - ESP32/Arduino integration

## Features

✅ **JSON Schema Validation** - Ensure data integrity before sending  
✅ **API Client Libraries** - Ready-to-use clients for Node.js and Python  
✅ **Code Generation** - Generate TypeScript, Python, and C++ types from schemas  
✅ **Complete Examples** - Working examples for all APIs and use cases  
✅ **Multi-Platform** - Node.js, Python, Arduino/ESP32 support  
✅ **UDP Sync** - Inter-node communication examples  
✅ **Sensor Bridge** - ESP32 sensor integration demo
