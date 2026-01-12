# Preset JSON Validation Examples

This document provides examples of valid and invalid preset JSON payloads for the LumiGrid LED Node API.

## Valid Examples

### Minimal Valid Preset
```json
{
  "preset_name": "simple_white",
  "effect_id": 1101
}
```

### Full Preset with All Fields
```json
{
  "preset_name": "led_ocean",
  "description": "Smooth ocean wave effect",
  "effect_id": 1105,
  "speed": 0.8,
  "intensity": 1.0,
  "brightness": 0.9,
  "palette_id": 1,
  "color1": [0, 0, 255],
  "color2": [0, 100, 200],
  "color3": [0, 50, 150]
}
```

### Preset with Single Color
```json
{
  "preset_name": "fire_warm",
  "effect_id": 1104,
  "speed": 0.8,
  "intensity": 1.0,
  "color1": [255, 40, 40]
}
```

### Preset with Palette (No Colors)
```json
{
  "preset_name": "rainbow_flow",
  "effect_id": 1105,
  "speed": 0.25,
  "intensity": 1.0,
  "palette_id": 0
}
```

## Invalid Examples (With Error Messages)

### Missing Required Field: preset_name
```json
{
  "effect_id": 1101
}
```
**Error:** `preset_name is required (string, non-empty)`

### Missing Required Field: effect_id
```json
{
  "preset_name": "test"
}
```
**Error:** `effect_id is required (integer)`

### Invalid effect_id Range (Too Low)
```json
{
  "preset_name": "test",
  "effect_id": 500
}
```
**Error:** `effect_id=500 out of range [1000,9999]`

### Invalid effect_id Range (Too High)
```json
{
  "preset_name": "test",
  "effect_id": 10000
}
```
**Error:** `effect_id=10000 out of range [1000,9999]`

### Invalid speed Range
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "speed": 10.0
}
```
**Error:** `speed=10.00 out of range [0.0,5.0]`

### Invalid intensity Range
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "intensity": 1.5
}
```
**Error:** `intensity=1.50 out of range [0.0,1.0]`

### Invalid brightness Range
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "brightness": -0.5
}
```
**Error:** `brightness=-0.50 out of range [0.0,1.0]`

### Invalid palette_id Range
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "palette_id": 300
}
```
**Error:** `palette_id=300 out of range [0,255]`

### Invalid Color Format (Object Instead of Array)
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "color1": {"r": 255, "g": 0, "b": 0}
}
```
**Error:** `color must be an array`

**Note:** This was the old format. The new standardized format uses arrays.

### Invalid Color Array Size (Too Few Elements)
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "color1": [255, 0]
}
```
**Error:** `color must have exactly 3 elements [R,G,B]`

### Invalid Color Array Size (Too Many Elements)
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "color1": [255, 0, 0, 255]
}
```
**Error:** `color must have exactly 3 elements [R,G,B]`

### Invalid Color Value (Out of Range)
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "color1": [255, 0, 300]
}
```
**Error:** `color[2]=300 out of range [0,255]`

### Invalid Color Value (Negative)
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "color1": [255, -10, 0]
}
```
**Error:** `color[1]=-10 out of range [0,255]`

### Invalid preset_name (Too Long)
```json
{
  "preset_name": "this_is_a_very_long_preset_name_that_exceeds_the_maximum_allowed_length_of_64_characters",
  "effect_id": 1101
}
```
**Error:** `preset_name too long (max 64 chars)`

### Invalid preset_name (Path Traversal)
```json
{
  "preset_name": "../etc/passwd",
  "effect_id": 1101
}
```
**Error:** `preset_name contains invalid characters (use alphanumeric, -, _)`

### Invalid preset_name (Special Characters)
```json
{
  "preset_name": "test@preset!",
  "effect_id": 1101
}
```
**Error:** `preset_name contains invalid characters (use alphanumeric, -, _)`

### Invalid Field Type (String Instead of Number)
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "speed": "fast"
}
```
**Error:** `speed must be a number`

### Invalid description (Too Long)
```json
{
  "preset_name": "test",
  "effect_id": 1101,
  "description": "This is a very long description that exceeds the maximum allowed length of 256 characters. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat..."
}
```
**Error:** `description too long (max 256 chars)`

## Testing Your Presets

### Using curl
```bash
curl -X POST http://lumigrid.local/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "preset_name": "test_preset",
    "effect_id": 1101,
    "speed": 1.0,
    "color1": [255, 255, 255]
  }'
```

### Using Python Validator
```bash
# Validate a preset file
python3 scripts/validate_preset.py my_preset.json

# Validate inline JSON
python3 scripts/validate_preset.py --inline '{"preset_name":"test","effect_id":1101}'

# Show example presets
python3 scripts/validate_preset.py --examples
```

### Using HTTP Response
When a validation error occurs, the API returns HTTP 400 Bad Request with the error message in the response body:

```
HTTP/1.1 400 Bad Request
Content-Type: text/plain

effect_id=500 out of range [1000,9999]
```

## Migration from Old Format

If you have presets using the old color object format, convert them to arrays:

**Old Format:**
```json
{
  "preset_name": "FX_1101",
  "effect_id": 1101,
  "color1": {"r": 255, "g": 255, "b": 255}
}
```

**New Format:**
```json
{
  "preset_name": "FX_1101",
  "effect_id": 1101,
  "color1": [255, 255, 255]
}
```

## Schema Reference

Full schema documentation: `firmware/schemas/preset-led.schema.json`

API documentation: `Docs/PRESET_API.md`

OpenAPI specification: `Docs/api_spec.yaml`
