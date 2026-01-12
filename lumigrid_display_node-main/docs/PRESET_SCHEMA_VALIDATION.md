# Preset Schema Validation

This document describes the JSON schema validation implemented for LumiGrid display presets.

## Overview

All preset-related POST requests to `/api/presets` are validated against a strict JSON schema contract defined in `schemas/preset-display.schema.json`. This ensures data integrity and provides clear error messages when invalid data is submitted.

## Schema Location

The schema file is located at:
```
schemas/preset-display.schema.json
```

This schema defines the structure for preset configurations including:
- Frame types (time, date, temperature, text, etc.)
- Color specifications (hex format)
- Font configuration
- Position settings
- Duration limits
- And more...

## Validation Process

When a preset is created via `POST /api/presets`, the following validation steps occur:

1. **JSON Schema Validation** - Validates against `preset-display.schema.json`
2. **Data Sanitization** - Removes potentially dangerous characters
3. **Frame Data Validation** - Additional business logic validation
4. **Controller Processing** - Adds preset to the system

## Error Messages

When validation fails, the API returns a 400 Bad Request with a detailed error message that includes:

1. The exact field path that failed validation
2. The specific validation rule that was violated
3. A complete example of a valid preset

### Example Error Response

```json
{
  "error": "Schema validation failed at 'frame_config -> type': 'invalid_type' is not one of ['time', 'date', 'datetime', 'temperature', 'humidity', 'text', 'message', 'data_visualization', 'interactive', 'template', 'conditional', 'multi_page', 'scrolling', 'animated']. Example valid preset: {
  \"preset_name\": \"display_time_small_red\",
  \"description\": \"Small time display, red color\",
  \"frame_config\": {
    \"type\": \"time\",
    \"name\": \"Small Red Time\",
    \"enabled\": true,
    \"duration\": 10,
    \"colors\": {
      \"background\": \"#000000\",
      \"text\": \"#FF0000\",
      \"accent\": \"#CC0000\"
    },
    \"font\": {
      \"path\": \"Minimal.ttf\",
      \"size\": 16
    },
    \"position\": {
      \"x\": 5,
      \"y\": 8
    },
    \"format\": \"24h\"
  }
}"
}
```

## Valid Preset Example

Here's a complete example of a valid preset request:

```json
{
  "preset_name": "display_time_small_red",
  "description": "Small time display, red color",
  "frame_config": {
    "type": "time",
    "name": "Small Red Time",
    "enabled": true,
    "duration": 10,
    "colors": {
      "background": "#000000",
      "text": "#FF0000",
      "accent": "#CC0000"
    },
    "font": {
      "path": "Minimal.ttf",
      "size": 16
    },
    "position": {
      "x": 5,
      "y": 8
    },
    "format": "24h"
  }
}
```

## Supported Frame Types

The schema supports the following frame types:

- `time` - Clock display with customizable format
- `date` - Date display
- `datetime` - Combined date and time
- `temperature` - Temperature reading display
- `humidity` - Humidity reading display
- `text` - Static text message
- `message` - Scrolling message
- `data_visualization` - Data charts and graphs
- `interactive` - Interactive displays
- `template` - Template-based displays
- `conditional` - Conditional displays based on sensor data
- `multi_page` - Multi-page displays
- `scrolling` - Scrolling content
- `animated` - Animated content

## Validation Rules

### Colors
- Must be in hex format: `#RGB` or `#RRGGBB`
- Examples: `#F00`, `#FF0000`, `#00FF00`

### Duration
- Minimum: 1 second
- Maximum: 3600 seconds (1 hour)

### Font Size
- Minimum: 6 points
- Maximum: 72 points

### Position
- Can be an integer from -1000 to 1000
- Or a string: `"center"`, `"left"`, `"right"`, `"top"`, `"bottom"`

### Temperature Unit
- Must be either `"celsius"` or `"fahrenheit"`

## Testing

Comprehensive tests have been added to verify schema validation:

```bash
# Run all preset API tests
pytest tests/test_web_api.py::TestPresetsAPI -v

# Run only malformed payload tests
pytest tests/test_web_api.py::TestPresetsAPI -k "malformed" -v
```

## Dependencies

The schema validation requires the `jsonschema` library:

```bash
pip install jsonschema>=4.0.0
```

If the library is not available, validation falls back to basic type checking without schema enforcement.

## References

- Main schema file: `schemas/preset-display.schema.json`
- Validation function: `lumigrid/validators.py::validate_preset_schema()`
- API endpoint: `lumigrid/web/app.py::create_preset()`
- Tests: `tests/test_web_api.py::TestPresetsAPI`
