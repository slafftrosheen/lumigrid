"""
LED Preset Validation Example

Demonstrates how to validate LED preset data using JSON Schema
for CRUD operations on LED presets
"""

import json
from pathlib import Path
from jsonschema import validate, ValidationError

# Load schema
schema_path = Path(__file__).parent.parent.parent / 'schemas' / 'preset-led.schema.json'
with open(schema_path) as f:
    schema = json.load(f)

# Example 1: Valid LED preset - static blue
valid_preset_1 = {
    "preset_id": 1,
    "name": "Cool Blue",
    "color": {
        "r": 28,
        "g": 78,
        "b": 255,
        "w": 0
    },
    "effect": "static"
}

# Example 2: Valid LED preset - warm white with fade effect
valid_preset_2 = {
    "preset_id": 2,
    "name": "Warm White Fade",
    "color": {
        "r": 0,
        "g": 0,
        "b": 0,
        "w": 255
    },
    "effect": "fade",
    "duration_ms": 2000
}

# Example 3: Valid LED preset - rainbow effect
valid_preset_3 = {
    "preset_id": 3,
    "name": "Rainbow",
    "color": {
        "r": 255,
        "g": 0,
        "b": 0,
        "w": 0
    },
    "effect": "rainbow",
    "duration_ms": 5000
}

# Example 4: Invalid preset - color value out of range
invalid_preset_1 = {
    "preset_id": 4,
    "name": "Invalid Red",
    "color": {
        "r": 300,  # Invalid: exceeds 255
        "g": 0,
        "b": 0,
        "w": 0
    }
}

# Example 5: Invalid preset - missing required color field
invalid_preset_2 = {
    "preset_id": 5,
    "name": "Incomplete",
    "color": {
        "r": 100,
        "g": 50,
        "b": 200
        # missing 'w' field
    }
}

def validate_led_preset(data):
    """Validate LED preset against schema"""
    try:
        validate(instance=data, schema=schema)
        return True, None
    except ValidationError as e:
        return False, str(e)

def main():
    print('=== LumiGrid LED Preset Validation Examples ===\n')

    # Test valid presets
    print('Example 1: Static blue preset')
    is_valid, error = validate_led_preset(valid_preset_1)
    if is_valid:
        print('✓ Valid LED preset')
        print(json.dumps(valid_preset_1, indent=2))
    else:
        print(f'✗ Invalid: {error}')

    print('\n' + '-' * 60 + '\n')

    print('Example 2: Warm white with fade')
    is_valid, error = validate_led_preset(valid_preset_2)
    if is_valid:
        print('✓ Valid LED preset')
        print(json.dumps(valid_preset_2, indent=2))
    else:
        print(f'✗ Invalid: {error}')

    print('\n' + '-' * 60 + '\n')

    print('Example 3: Rainbow effect')
    is_valid, error = validate_led_preset(valid_preset_3)
    if is_valid:
        print('✓ Valid LED preset')
        print(json.dumps(valid_preset_3, indent=2))
    else:
        print(f'✗ Invalid: {error}')

    print('\n' + '-' * 60 + '\n')

    # Test invalid presets
    print('Example 4: Invalid - color out of range')
    is_valid, error = validate_led_preset(invalid_preset_1)
    if is_valid:
        print('✓ Valid LED preset')
    else:
        print('✗ Invalid (as expected):')
        print(f'   {error}')

    print('\n' + '-' * 60 + '\n')

    print('Example 5: Invalid - missing required field')
    is_valid, error = validate_led_preset(invalid_preset_2)
    if is_valid:
        print('✓ Valid LED preset')
    else:
        print('✗ Invalid (as expected):')
        print(f'   {error}')

    print('\n' + '=' * 60)
    print('\nValidation complete!')
    print('\nUsage: Validate before saving/loading presets')
    print('Available effects: static, fade, pulse, strobe, rainbow')

if __name__ == '__main__':
    main()
