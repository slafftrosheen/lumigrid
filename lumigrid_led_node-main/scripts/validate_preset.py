#!/usr/bin/env python3
"""
Preset JSON Schema Validator

This script validates preset JSON payloads against the preset-led.schema.json schema.
It can be used for testing and validating preset files before uploading to the LED node.

Usage:
    python validate_preset.py <preset.json>
    python validate_preset.py --inline '{"preset_name":"test","effect_id":1101}'

Requirements:
    pip install jsonschema
"""

import sys
import json
import argparse
from pathlib import Path

try:
    from preset_utils import load_schema
except ImportError:
    # Fallback if running standalone without utils
    def load_schema():
        script_dir = Path(__file__).parent
        schema_path = script_dir.parent / "firmware" / "schemas" / "preset-led.schema.json"
        if not schema_path.exists():
            schema_path = script_dir / "firmware" / "schemas" / "preset-led.schema.json"
        if not schema_path.exists():
            raise FileNotFoundError(f"Schema file not found")
        with open(schema_path, 'r') as f:
            return json.load(f)

try:
    import jsonschema
    from jsonschema import validate, ValidationError
except ImportError:
    print("Error: jsonschema library not found.")
    print("Install with: pip install jsonschema")
    sys.exit(1)


def validate_preset(data, schema):
    """Validate preset data against schema."""
    try:
        validate(instance=data, schema=schema)
        return True, None
    except ValidationError as e:
        return False, str(e)


def main():
    parser = argparse.ArgumentParser(
        description='Validate LED preset JSON against schema'
    )
    parser.add_argument(
        'input',
        nargs='?',
        help='Path to preset JSON file or use --inline'
    )
    parser.add_argument(
        '--inline',
        type=str,
        help='JSON string to validate inline'
    )
    parser.add_argument(
        '--examples',
        action='store_true',
        help='Show example presets'
    )
    
    args = parser.parse_args()
    
    # Load schema
    schema = load_schema()
    
    # Show examples if requested
    if args.examples:
        if 'examples' in schema:
            print("Example valid presets:")
            print(json.dumps(schema['examples'], indent=2))
        else:
            print("No examples in schema")
        return 0
    
    # Get data to validate
    if args.inline:
        try:
            data = json.loads(args.inline)
        except json.JSONDecodeError as e:
            print(f"Error parsing inline JSON: {e}")
            return 1
    elif args.input:
        try:
            with open(args.input, 'r') as f:
                data = json.load(f)
        except FileNotFoundError:
            print(f"Error: File not found: {args.input}")
            return 1
        except json.JSONDecodeError as e:
            print(f"Error parsing JSON file: {e}")
            return 1
    else:
        parser.print_help()
        return 1
    
    # Validate
    valid, error = validate_preset(data, schema)
    
    if valid:
        print("✓ Preset is valid!")
        print(json.dumps(data, indent=2))
        return 0
    else:
        print("✗ Validation failed:")
        print(error)
        return 1


if __name__ == '__main__':
    sys.exit(main())
