# JSON Schemas for LumiGrid LED Node

This directory contains JSON schemas that define the structure and validation rules for various API payloads used by the LumiGrid LED Node firmware.

## Schema Files

### preset-led.schema.json

Defines the structure for LED preset configurations used by the `/api/presets` endpoint.

**Purpose:** Validates LED preset payloads to ensure they contain valid effect parameters, color values, and metadata.

**Used by:**
- `POST /api/presets` - Create/update preset endpoint
- Preset file loading from LittleFS storage

**Key Requirements:**
- `preset_name`: Required, 1-64 characters, alphanumeric/dash/underscore only
- `effect_id`: Required, integer range 1000-9999
- `speed`: Optional, 0.0-5.0
- `intensity`: Optional, 0.0-1.0
- `brightness`: Optional, 0.0-1.0
- `palette_id`: Optional, 0-255
- `color1`, `color2`, `color3`: Optional RGB arrays [R,G,B], each value 0-255

**Example:**
```json
{
  "preset_name": "led_ocean",
  "description": "Smooth ocean wave effect",
  "effect_id": 1105,
  "speed": 0.8,
  "intensity": 1.0,
  "palette_id": 1,
  "color1": [0, 0, 255],
  "color2": [0, 100, 200],
  "color3": [0, 50, 150]
}
```

## Validation Implementation

The schemas are enforced in C code through custom validation functions in `firmware/components/rest_api/rest_api.c`:

- `rest_api_validate_preset_json()` - Main validation function for preset schemas
- `validate_color_array()` - Helper for RGB color array validation

These functions check:
- Field presence (required vs optional)
- Field types (string, number, array)
- Value ranges (numeric bounds)
- Array sizes and element types
- String patterns and lengths

## Testing

Unit tests for schema validation are located in:
- `firmware/main/test/test_presets_api.c`

Tests cover:
- Valid minimal and full payloads
- Missing required fields
- Out of range values
- Invalid data types
- Color format validation

Run tests with:
```bash
cd firmware
idf.py build test
```

## Schema Format

All schemas follow JSON Schema Draft 7 specification:
- `$schema`: Schema version identifier
- `type`: Root type (usually "object")
- `required`: Array of required field names
- `properties`: Field definitions with types and constraints
- `additionalProperties`: Whether extra fields are allowed (usually false)
- `examples`: Sample valid payloads

## Adding New Schemas

When adding a new schema:

1. Create a `.schema.json` file in this directory
2. Follow JSON Schema Draft 7 specification
3. Implement validation function in appropriate component (e.g., `rest_api.c`)
4. Add unit tests for validation
5. Document in API specification (e.g., `Docs/api_spec.yaml`)
6. Update this README with schema details

## References

- [JSON Schema Specification](https://json-schema.org/)
- [LumiGrid API Documentation](../../Docs/api_spec.yaml)
- [Preset API Guide](../../Docs/PRESET_API.md)
