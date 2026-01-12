# Preset Management API

## Overview
The preset management API allows users to save, load, list, and delete LED configuration presets. Presets are stored as JSON files in the `/spiffs/presets/` directory on LittleFS and persist across reboots.

## Schema Validation
All preset payloads are validated against the JSON schema defined in `firmware/schemas/preset-led.schema.json`. Invalid requests will be rejected with detailed error messages.

## Endpoints

### POST /api/presets
Save a new preset or update an existing one.

**Request Body (compliant with preset-led.schema.json):**
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

**Field Specifications:**
- `preset_name` (required): String, 1-64 chars, alphanumeric/dash/underscore only
- `description` (optional): String, max 256 chars
- `effect_id` (required): Integer, range 1000-9999
- `speed` (optional): Number, range 0.0-5.0, default 1.0
- `intensity` (optional): Number, range 0.0-1.0, default 0.5
- `brightness` (optional): Number, range 0.0-1.0, default 1.0
- `palette_id` (optional): Integer, range 0-255
- `color1`, `color2`, `color3` (optional): RGB arrays [R,G,B], each value 0-255

**Response (Success):**
```json
{
  "ok": true
}
```

**Response (Validation Error - 400):**
```
effect_id=500 out of range [1000,9999]
```
or
```
color1 must have exactly 3 elements [R,G,B]
```
or
```
preset_name is required (string, non-empty)
```

**Notes:**
- The `preset_name` field is required and used as the filename
- The preset is saved to `/spiffs/presets/{preset_name}.json`
- Atomic write is used (write to temp file, then rename) to prevent corruption
- If a preset with the same name exists, it will be overwritten
- All fields are validated according to the schema before saving

### GET /api/presets
List all available presets.

**Response:**
```json
[
  {"name": "FX_1101"},
  {"name": "FX_1102"},
  {"name": "led_ocean"}
]
```

**Notes:**
- Returns an empty array `[]` if no presets exist
- Only lists `.json` files from `/spiffs/presets/`

### GET /api/presets/{name}
Load a specific preset by name.

**Example:** `GET /api/presets/led_ocean`

**Response:**
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

**Error Response (404):**
```
preset not found
```

**Notes:**
- The `.json` extension is optional in the URL
- Returns the complete JSON content of the preset file

### DELETE /api/presets/{name}
Delete a preset.

**Example:** `DELETE /api/presets/led_ocean`

**Response:**
```
204 No Content
```

**Error Response (404):**
```
preset not found
```

## Storage Details

- **Location:** `/spiffs/presets/` (on LittleFS partition)
- **File Format:** JSON files with `.json` extension
- **Persistence:** Presets survive reboots as they are stored in flash memory
- **Directory Creation:** The `/spiffs/presets/` directory is automatically created during system initialization

## Factory Reset

To restore factory defaults and clear all presets:
```
POST /api/config/restore-factory
```

This endpoint:
1. Loads `default_config.json` if available
2. Writes it to `config.json`
3. Reloads the configuration
4. Presets are NOT deleted by factory reset (they remain in `/spiffs/presets/`)

## Implementation Notes

1. **Thread Safety:** All file operations are performed atomically using temp files
2. **Error Handling:** Comprehensive error checking for filesystem operations
3. **Schema Validation:** All preset payloads are validated against `firmware/schemas/preset-led.schema.json`
   - Required fields: `preset_name`, `effect_id`
   - Field type checking (string, number, array)
   - Range validation (effect_id: 1000-9999, speed: 0.0-5.0, etc.)
   - Color arrays must be exactly 3 elements [R,G,B] with values 0-255
   - Detailed error messages returned on validation failure
4. **Preset Name Validation:** Names are sanitized to prevent path traversal attacks
   - Only alphanumeric characters, dash (-), and underscore (_) are allowed
   - Path traversal sequences (.., /, \) are rejected
   - Maximum length: 64 characters
5. **Size Limits:** Preset payload is limited to 2048 bytes
6. **File System:** Requires LittleFS to be mounted and initialized

## Schema Reference

The complete JSON schema is available at `firmware/schemas/preset-led.schema.json`. Key validation rules:

| Field | Type | Required | Range/Pattern | Default |
|-------|------|----------|---------------|---------|
| `preset_name` | string | Yes | 1-64 chars, `^[a-zA-Z0-9_-]+$` | - |
| `description` | string | No | Max 256 chars | - |
| `effect_id` | integer | Yes | 1000-9999 | - |
| `speed` | number | No | 0.0-5.0 | 1.0 |
| `intensity` | number | No | 0.0-1.0 | 0.5 |
| `brightness` | number | No | 0.0-1.0 | 1.0 |
| `palette_id` | integer | No | 0-255 | - |
| `color1`, `color2`, `color3` | array | No | 3 integers [R,G,B], each 0-255 | - |

## Example Usage

### Save a preset
```bash
curl -X POST http://lumigrid.local/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "preset_name": "led_ocean",
    "description": "Smooth ocean wave effect",
    "effect_id": 1105,
    "speed": 0.8,
    "intensity": 1.0,
    "palette_id": 1,
    "color1": [0, 0, 255],
    "color2": [0, 100, 200]
  }'
```

### Save a simple preset with just colors
```bash
curl -X POST http://lumigrid.local/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "preset_name": "fire_warm",
    "effect_id": 1104,
    "speed": 0.8,
    "intensity": 1.0,
    "color1": [255, 40, 40]
  }'
```

### Load a preset
```bash
curl http://lumigrid.local/api/presets/led_ocean
```

### Delete a preset
```bash
curl -X DELETE http://lumigrid.local/api/presets/led_ocean
```

## Testing

Unit tests are available in `firmware/main/test/test_presets_api.c`:
- `preset path write/read basic` - Tests atomic write pattern
- `preset extract name from URI` - Tests URI parsing
- `preset delete operation` - Tests delete functionality

Run tests with:
```bash
cd firmware
idf.py build test
```
