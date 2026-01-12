#!/usr/bin/env python3
"""
Preset Format Migration Tool

Converts preset JSON files from the old format (color objects) to the new
standardized format (color arrays).

Usage:
    python migrate_presets.py <preset_file.json>
    python migrate_presets.py --directory /path/to/presets/

The script will:
1. Read the old format preset
2. Convert color objects {r,g,b} to arrays [r,g,b]
3. Ensure preset_name field is present
4. Validate against new schema
5. Save to new file or overwrite (with --overwrite flag)
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
        try:
            import jsonschema
        except ImportError:
            return None
        script_dir = Path(__file__).parent
        schema_path = script_dir.parent / "firmware" / "schemas" / "preset-led.schema.json"
        if not schema_path.exists():
            schema_path = script_dir / "firmware" / "schemas" / "preset-led.schema.json"
        if not schema_path.exists():
            return None
        with open(schema_path, 'r') as f:
            return json.load(f)

try:
    import jsonschema
    from jsonschema import validate, ValidationError
except ImportError:
    jsonschema = None


def convert_color_object_to_array(color_obj):
    """Convert color object {r,g,b} to array [r,g,b]."""
    if isinstance(color_obj, list):
        return color_obj  # Already an array
    
    if isinstance(color_obj, dict) and 'r' in color_obj and 'g' in color_obj and 'b' in color_obj:
        return [color_obj['r'], color_obj['g'], color_obj['b']]
    
    return color_obj  # Unknown format, leave as-is


def migrate_preset(data):
    """Migrate preset from old to new format."""
    migrated = data.copy()
    changes = []
    
    # Convert "name" to "preset_name" if needed
    if 'name' in migrated and 'preset_name' not in migrated:
        migrated['preset_name'] = migrated.pop('name')
        changes.append("Renamed 'name' to 'preset_name'")
    
    # Convert color objects to arrays
    for color_field in ['color1', 'color2', 'color3']:
        if color_field in migrated:
            old_val = migrated[color_field]
            new_val = convert_color_object_to_array(old_val)
            if old_val != new_val:
                migrated[color_field] = new_val
                changes.append(f"Converted {color_field} from object to array")
    
    return migrated, changes


def validate_preset(data, schema):
    """Validate preset against schema if available."""
    if not schema or not jsonschema:
        return True, "Validation skipped (jsonschema not available)"
    
    try:
        validate(instance=data, schema=schema)
        return True, "Valid"
    except ValidationError as e:
        return False, str(e)


def main():
    parser = argparse.ArgumentParser(
        description='Migrate preset JSON from old to new format'
    )
    parser.add_argument(
        'input',
        help='Path to preset JSON file or directory'
    )
    parser.add_argument(
        '--overwrite',
        action='store_true',
        help='Overwrite original file instead of creating .new.json'
    )
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Show what would be changed without writing files'
    )
    
    args = parser.parse_args()
    
    # Load schema
    schema = load_schema()
    if schema:
        print("✓ Loaded validation schema")
    else:
        print("⚠ Schema not found, skipping validation")
    
    input_path = Path(args.input)
    
    # Handle directory
    if input_path.is_dir():
        json_files = list(input_path.glob("*.json"))
        if not json_files:
            print(f"No JSON files found in {input_path}")
            return 1
        
        print(f"Found {len(json_files)} JSON file(s)\n")
        
        for json_file in json_files:
            process_file(json_file, schema, args.overwrite, args.dry_run)
            print()
        
        return 0
    
    # Handle single file
    if not input_path.exists():
        print(f"Error: File not found: {input_path}")
        return 1
    
    return process_file(input_path, schema, args.overwrite, args.dry_run)


def process_file(file_path, schema, overwrite, dry_run):
    """Process a single preset file."""
    print(f"Processing: {file_path.name}")
    
    # Read original
    try:
        with open(file_path, 'r') as f:
            original = json.load(f)
    except json.JSONDecodeError as e:
        print(f"  ✗ Error parsing JSON: {e}")
        return 1
    
    # Migrate
    migrated, changes = migrate_preset(original)
    
    if not changes:
        print("  ℹ No changes needed (already in new format)")
        return 0
    
    # Show changes
    print("  Changes:")
    for change in changes:
        print(f"    - {change}")
    
    # Validate
    valid, msg = validate_preset(migrated, schema)
    if valid:
        print(f"  ✓ Validation: {msg}")
    else:
        print(f"  ✗ Validation failed: {msg}")
        if not dry_run:
            print("  Skipping write due to validation error")
            return 1
    
    # Write
    if dry_run:
        print("  (dry-run, not writing)")
        print(f"\n  New content:\n{json.dumps(migrated, indent=2)}")
        return 0
    
    if overwrite:
        output_path = file_path
    else:
        output_path = file_path.with_suffix('.new.json')
    
    try:
        with open(output_path, 'w') as f:
            json.dump(migrated, f, indent=2)
            f.write('\n')  # Add trailing newline
        print(f"  ✓ Written to: {output_path}")
        return 0
    except IOError as e:
        print(f"  ✗ Error writing file: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())
