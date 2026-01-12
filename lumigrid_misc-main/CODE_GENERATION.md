# Code Generation from JSON Schemas

This guide shows how to generate strongly-typed code from LumiGrid JSON schemas for various programming languages.

## Why Generate Code?

JSON Schema validation catches errors at runtime, but type generation provides:

- **Compile-time safety** - Catch errors before running code
- **IDE autocomplete** - Better development experience
- **Documentation** - Types serve as inline documentation
- **Refactoring support** - Easier to update code when schemas change

## TypeScript

### Using json-schema-to-typescript

**Install:**
```bash
npm install -g json-schema-to-typescript
```

**Generate types:**
```bash
# Navigate to schema directory
cd schemas

# Generate TypeScript interfaces
json-schema-to-typescript sensor-data.schema.json > ../types/sensor-data.d.ts
json-schema-to-typescript preset-led.schema.json > ../types/preset-led.d.ts
json-schema-to-typescript preset-display.schema.json > ../types/preset-display.d.ts
json-schema-to-typescript config.schema.json > ../types/config.d.ts
json-schema-to-typescript sync-packet.schema.json > ../types/sync-packet.d.ts
```

**Example generated type:**
```typescript
// sensor-data.d.ts
export interface SensorDataPayload {
  node_id: string;
  node_name?: string;
  timestamp?: number;
  readings: {
    temperature_c?: number;
    humidity_percent?: number;
    pressure_hpa?: number;
    light_lux?: number;
    motion_detected?: boolean;
  };
}
```

**Usage in TypeScript:**
```typescript
import { SensorDataPayload } from './types/sensor-data';

// Compile-time type checking
const data: SensorDataPayload = {
  node_id: "sensor-001",
  readings: {
    temperature_c: 23.5,
    humidity_percent: 48.3
  }
};

// TypeScript will catch this error at compile time:
// const bad: SensorDataPayload = {
//   node_id: 123,  // Error: Type 'number' is not assignable to type 'string'
//   readings: {}
// };
```

### Batch Generation Script

Create `generate-types.sh`:
```bash
#!/bin/bash
mkdir -p types

for schema in schemas/*.schema.json; do
  filename=$(basename "$schema" .schema.json)
  json-schema-to-typescript "$schema" > "types/${filename}.d.ts"
  echo "Generated types/${filename}.d.ts"
done
```

## Python

### Using datamodel-code-generator (Pydantic)

**Install:**
```bash
pip install datamodel-code-generator[http]
```

**Generate Pydantic models:**
```bash
# Create output directory
mkdir -p models

# Generate models
datamodel-codegen \
  --input schemas/sensor-data.schema.json \
  --output models/sensor_data.py \
  --class-name SensorDataPayload

datamodel-codegen \
  --input schemas/preset-led.schema.json \
  --output models/preset_led.py \
  --class-name LedPreset

datamodel-codegen \
  --input schemas/preset-display.schema.json \
  --output models/preset_display.py \
  --class-name DisplayPreset

datamodel-codegen \
  --input schemas/config.schema.json \
  --output models/config.py \
  --class-name NodeConfig

datamodel-codegen \
  --input schemas/sync-packet.schema.json \
  --output models/sync_packet.py \
  --class-name SyncPacket
```

**Example generated model:**
```python
# models/sensor_data.py
from __future__ import annotations
from typing import Optional
from pydantic import BaseModel, Field

class Readings(BaseModel):
    temperature_c: Optional[float] = Field(None, description='Temperature in degrees Celsius')
    humidity_percent: Optional[float] = Field(None, description='Relative humidity percentage', ge=0.0, le=100.0)
    pressure_hpa: Optional[float] = Field(None, description='Atmospheric pressure in hectopascals', ge=0.0)
    light_lux: Optional[float] = Field(None, description='Light intensity in lux', ge=0.0)
    motion_detected: Optional[bool] = Field(None, description='Whether motion was detected')

class SensorDataPayload(BaseModel):
    node_id: str = Field(..., description='Unique identifier for the node', min_length=1)
    node_name: Optional[str] = Field(None, description='Human-readable name for the node')
    timestamp: Optional[int] = Field(None, description='Unix timestamp in seconds', ge=0)
    readings: Readings = Field(..., description='Sensor reading values')
```

**Usage with Pydantic:**
```python
from models.sensor_data import SensorDataPayload

# Pydantic validates on instantiation
data = SensorDataPayload(
    node_id="sensor-001",
    readings={
        "temperature_c": 23.5,
        "humidity_percent": 48.3
    }
)

# This will raise ValidationError:
# bad = SensorDataPayload(
#     node_id="sensor-001",
#     readings={
#         "humidity_percent": 150  # Error: exceeds maximum of 100
#     }
# )

# Serialize to dict/JSON
json_dict = data.dict()
json_str = data.json()

# Deserialize from dict/JSON
loaded = SensorDataPayload.parse_obj(json_dict)
loaded_from_json = SensorDataPayload.parse_raw(json_str)
```

### Batch Generation Script

Create `generate_models.sh`:
```bash
#!/bin/bash
mkdir -p models

schemas=(
  "sensor-data:SensorDataPayload"
  "preset-led:LedPreset"
  "preset-display:DisplayPreset"
  "config:NodeConfig"
  "sync-packet:SyncPacket"
)

for entry in "${schemas[@]}"; do
  IFS=':' read -r schema class <<< "$entry"
  datamodel-codegen \
    --input "schemas/${schema}.schema.json" \
    --output "models/${schema//-/_}.py" \
    --class-name "$class"
  echo "Generated models/${schema//-/_}.py"
done
```

## C++ / Arduino

### Using quicktype

**Install:**
```bash
npm install -g quicktype
```

**Generate C++ headers:**
```bash
# Create output directory
mkdir -p include

# Generate C++ code
quicktype schemas/sensor-data.schema.json -o include/sensor_data.h --lang c++
quicktype schemas/preset-led.schema.json -o include/preset_led.h --lang c++
quicktype schemas/config.schema.json -o include/config.h --lang c++
```

**Example generated code:**
```cpp
// include/sensor_data.h
#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <string>
#include <optional>
#include <nlohmann/json.hpp>

struct Readings {
    std::optional<double> temperature_c;
    std::optional<double> humidity_percent;
    std::optional<double> pressure_hpa;
    std::optional<double> light_lux;
    std::optional<bool> motion_detected;
};

struct SensorDataPayload {
    std::string node_id;
    std::optional<std::string> node_name;
    std::optional<int64_t> timestamp;
    Readings readings;
};

// JSON serialization/deserialization
void to_json(nlohmann::json& j, const Readings& r);
void from_json(const nlohmann::json& j, Readings& r);
void to_json(nlohmann::json& j, const SensorDataPayload& s);
void from_json(const nlohmann::json& j, SensorDataPayload& s);

#endif // SENSOR_DATA_H
```

**Usage in C++:**
```cpp
#include "sensor_data.h"
#include <nlohmann/json.hpp>

// Create data
SensorDataPayload data;
data.node_id = "sensor-001";
data.readings.temperature_c = 23.5;
data.readings.humidity_percent = 48.3;

// Serialize to JSON
nlohmann::json j = data;
std::string json_str = j.dump();

// Deserialize from JSON
nlohmann::json parsed = nlohmann::json::parse(json_str);
SensorDataPayload loaded = parsed.get<SensorDataPayload>();
```

**For Arduino (simpler approach):**

Since Arduino has limited resources, use ArduinoJson directly:

```cpp
#include <ArduinoJson.h>

// Create JSON document
StaticJsonDocument<256> doc;
doc["node_id"] = "sensor-001";
JsonObject readings = doc.createNestedObject("readings");
readings["temperature_c"] = 23.5;
readings["humidity_percent"] = 48.3;

// Serialize to string
String output;
serializeJson(doc, output);

// Parse JSON
StaticJsonDocument<256> parsed;
deserializeJson(parsed, input);
String nodeId = parsed["node_id"];
float temp = parsed["readings"]["temperature_c"];
```

### Batch Generation Script

Create `generate_cpp.sh`:
```bash
#!/bin/bash
mkdir -p include

schemas=(
  "sensor-data"
  "preset-led"
  "preset-display"
  "config"
  "sync-packet"
)

for schema in "${schemas[@]}"; do
  quicktype "schemas/${schema}.schema.json" \
    -o "include/${schema//-/_}.h" \
    --lang c++
  echo "Generated include/${schema//-/_}.h"
done
```

## Go

### Using quicktype

**Generate Go code:**
```bash
mkdir -p pkg/lumigrid

quicktype schemas/sensor-data.schema.json -o pkg/lumigrid/sensor_data.go --lang go --package lumigrid
quicktype schemas/preset-led.schema.json -o pkg/lumigrid/preset_led.go --lang go --package lumigrid
```

**Example generated code:**
```go
package lumigrid

type SensorDataPayload struct {
    NodeID    string   `json:"node_id"`
    NodeName  *string  `json:"node_name,omitempty"`
    Timestamp *int64   `json:"timestamp,omitempty"`
    Readings  Readings `json:"readings"`
}

type Readings struct {
    TemperatureC    *float64 `json:"temperature_c,omitempty"`
    HumidityPercent *float64 `json:"humidity_percent,omitempty"`
    PressureHpa     *float64 `json:"pressure_hpa,omitempty"`
    LightLux        *float64 `json:"light_lux,omitempty"`
    MotionDetected  *bool    `json:"motion_detected,omitempty"`
}
```

**Usage in Go:**
```go
import (
    "encoding/json"
    "github.com/yourorg/lumigrid/pkg/lumigrid"
)

// Create data
data := lumigrid.SensorDataPayload{
    NodeID: "sensor-001",
    Readings: lumigrid.Readings{
        TemperatureC: ptr(23.5),
        HumidityPercent: ptr(48.3),
    },
}

// Serialize to JSON
jsonBytes, err := json.Marshal(data)

// Deserialize from JSON
var loaded lumigrid.SensorDataPayload
err = json.Unmarshal(jsonBytes, &loaded)
```

## Rust

### Using quicktype

**Generate Rust code:**
```bash
mkdir -p src/models

quicktype schemas/sensor-data.schema.json -o src/models/sensor_data.rs --lang rust
quicktype schemas/preset-led.schema.json -o src/models/preset_led.rs --lang rust
```

**Example generated code:**
```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SensorDataPayload {
    pub node_id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub node_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub timestamp: Option<i64>,
    pub readings: Readings,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Readings {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub temperature_c: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub humidity_percent: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pressure_hpa: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub light_lux: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub motion_detected: Option<bool>,
}
```

**Usage in Rust:**
```rust
use serde_json;

// Create data
let data = SensorDataPayload {
    node_id: "sensor-001".to_string(),
    node_name: None,
    timestamp: None,
    readings: Readings {
        temperature_c: Some(23.5),
        humidity_percent: Some(48.3),
        pressure_hpa: None,
        light_lux: None,
        motion_detected: None,
    },
};

// Serialize to JSON
let json_str = serde_json::to_string(&data)?;

// Deserialize from JSON
let loaded: SensorDataPayload = serde_json::from_str(&json_str)?;
```

## Best Practices

### 1. Version Your Schemas

When schemas change, regenerate types:

```bash
# Add version to schema file name
sensor-data.v1.schema.json
sensor-data.v2.schema.json

# Generate versioned types
json-schema-to-typescript sensor-data.v2.schema.json > types/sensor-data.v2.d.ts
```

### 2. Automate Generation

Add to `package.json`:
```json
{
  "scripts": {
    "generate:types": "bash generate-types.sh",
    "prebuild": "npm run generate:types"
  }
}
```

Or use a Makefile:
```makefile
.PHONY: generate
generate: generate-ts generate-py generate-cpp

generate-ts:
	@bash generate-types.sh

generate-py:
	@bash generate-models.sh

generate-cpp:
	@bash generate-cpp.sh
```

### 3. Commit Generated Code

Decide whether to commit generated code or generate at build time:

**Commit generated code** (recommended):
- Easier for contributors
- Faster builds
- No tool dependencies

**Generate at build time**:
- Always in sync with schemas
- Smaller repository size
- Requires build tools

### 4. Validate Schemas

Before generating, validate your schemas:

```bash
npm install -g ajv-cli

# Validate schema syntax
ajv compile -s schemas/sensor-data.schema.json

# Test against examples
ajv validate -s schemas/sensor-data.schema.json -d schemas/examples/sensor-data-example.json
```

## Tool Comparison

| Tool | Languages | Validation | Docs | License |
|------|-----------|------------|------|---------|
| json-schema-to-typescript | TypeScript | ✓ | Great | MIT |
| datamodel-code-generator | Python | ✓ | Good | MIT |
| quicktype | 20+ languages | ✗ | Excellent | Apache 2.0 |
| openapi-generator | 50+ languages | ✓ | Good | Apache 2.0 |

## Resources

- [JSON Schema](https://json-schema.org/)
- [json-schema-to-typescript](https://github.com/bcherny/json-schema-to-typescript)
- [datamodel-code-generator](https://github.com/koxudaxi/datamodel-code-generator)
- [quicktype](https://quicktype.io/)
- [Pydantic](https://docs.pydantic.dev/)
- [ArduinoJson](https://arduinojson.org/)
