# LumiGrid Preset Examples

This directory contains example preset files for the LumiGrid Display Node.

## Using These Presets

### Via API

```bash
curl -X POST http://YOUR_PI_IP:5000/api/presets \
  -H "Content-Type: application/json" \
  -d @preset_examples/time_displays/clock_cyan_24h.json
```

### Via Web Interface

1. Navigate to the Presets tab
2. Click "Import Preset"
3. Upload one of these JSON files

## Preset Categories

### Time Displays (`time_displays/`)
- Various clock formats and color schemes
- 12-hour and 24-hour formats
- Different font sizes and positions

### Temperature Displays (`temperature_displays/`)
- Temperature with different color schemes
- Celsius and Fahrenheit formats
- Icon-based and text-only variants

### Message Displays (`message_displays/`)
- Static text messages
- Scrolling messages
- Welcome and notification templates

### Multi-Frame Sequences (`multi_frame/`)
- Complete frame sequences
- Coordinated multi-page displays
- Example slideshow configurations

## Schema Reference

All presets follow the schema defined in `/schemas/preset-display.schema.json`

## Creating Your Own Presets

1. Copy one of these examples as a template
2. Modify the colors, text, and configuration
3. Validate against the schema
4. Upload via API or web interface

## Support

For more information:
- [API Reference](../docs/API_REFERENCE.md)
- [Integration Guide](../INTEGRATION.md)
