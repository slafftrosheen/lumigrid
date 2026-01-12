"""
Sensor Data Validation Example

Demonstrates how to validate sensor data payloads using JSON Schema
before sending to /api/sensor_update endpoint
"""

import json
import sys
import time
from pathlib import Path
from jsonschema import validate, ValidationError

# Load schema
schema_path = Path(__file__).parent.parent.parent / 'schemas' / 'sensor-data.schema.json'
with open(schema_path) as f:
    schema = json.load(f)

# Example 1: Valid sensor data with temperature and humidity
valid_data_1 = {
    "node_id": "node-001",
    "readings": {
        "temperature_c": 23.5,
        "humidity_percent": 45.2
    }
}

# Example 2: Valid sensor data with all fields
valid_data_2 = {
    "node_id": "sensor-kitchen",
    "node_name": "Kitchen Sensor",
    "timestamp": int(time.time()),
    "readings": {
        "temperature_c": 22.1,
        "humidity_percent": 52.3,
        "pressure_hpa": 1013.25,
        "light_lux": 350.5,
        "motion_detected": True
    }
}

# Example 3: Invalid data - missing required field
invalid_data_1 = {
    "node_id": "node-003",
    # missing 'readings' field
}

# Example 4: Invalid data - humidity out of range
invalid_data_2 = {
    "node_id": "node-004",
    "readings": {
        "temperature_c": 25.0,
        "humidity_percent": 150  # Invalid: exceeds maximum of 100
    }
}

def validate_sensor_data(data):
    """Validate sensor data against schema"""
    try:
        validate(instance=data, schema=schema)
        return True, None
    except ValidationError as e:
        return False, str(e)

def main():
    print('=== LumiGrid Sensor Data Validation Examples ===\n')

    # Test valid data
    print('Example 1: Basic sensor data')
    is_valid, error = validate_sensor_data(valid_data_1)
    if is_valid:
        print('✓ Valid sensor data')
        print(json.dumps(valid_data_1, indent=2))
    else:
        print(f'✗ Invalid: {error}')

    print('\n' + '-' * 60 + '\n')

    print('Example 2: Complete sensor data')
    is_valid, error = validate_sensor_data(valid_data_2)
    if is_valid:
        print('✓ Valid sensor data')
        print(json.dumps(valid_data_2, indent=2))
    else:
        print(f'✗ Invalid: {error}')

    print('\n' + '-' * 60 + '\n')

    # Test invalid data
    print('Example 3: Invalid - missing readings')
    is_valid, error = validate_sensor_data(invalid_data_1)
    if is_valid:
        print('✓ Valid sensor data')
    else:
        print('✗ Invalid (as expected):')
        print(f'   {error}')

    print('\n' + '-' * 60 + '\n')

    print('Example 4: Invalid - humidity out of range')
    is_valid, error = validate_sensor_data(invalid_data_2)
    if is_valid:
        print('✓ Valid sensor data')
    else:
        print('✗ Invalid (as expected):')
        print(f'   {error}')

    print('\n' + '=' * 60)
    print('\nValidation complete!')
    print('\nUsage: Integrate this validation before POSTing to /api/sensor_update')
    print('Example: if validate(data): requests.post(url, json=data)')

if __name__ == '__main__':
    main()
