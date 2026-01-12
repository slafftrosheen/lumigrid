"""
Sensor Update POST Example

Demonstrates how to POST sensor data to a LumiGrid node's /api/sensor_update endpoint
with proper validation
"""

import json
import os
import time
import requests
from pathlib import Path
from jsonschema import validate, ValidationError

# Load schema
schema_path = Path(__file__).parent.parent.parent / 'schemas' / 'sensor-data.schema.json'
with open(schema_path) as f:
    schema = json.load(f)

def validate_sensor_data(data):
    """Validate sensor data against schema"""
    try:
        validate(instance=data, schema=schema)
        return True, None
    except ValidationError as e:
        return False, str(e)

def post_sensor_update(node_url, sensor_data, timeout=5):
    """
    Post sensor data to a LumiGrid node
    
    Args:
        node_url: Base URL of the node (e.g., 'http://192.168.1.100')
        sensor_data: Sensor data payload to send
        timeout: Request timeout in seconds
        
    Returns:
        Response data from the server
    """
    # Validate data before sending
    is_valid, error = validate_sensor_data(sensor_data)
    if not is_valid:
        raise ValueError(f'Validation failed: {error}')
    
    url = f'{node_url}/api/sensor_update'
    
    try:
        response = requests.post(
            url,
            json=sensor_data,
            headers={'Content-Type': 'application/json'},
            timeout=timeout
        )
        
        print(f'✓ Successfully posted to {url}')
        print(f'  Status: {response.status_code}')
        print(f'  Response: {response.text}')
        
        response.raise_for_status()
        return response.json() if response.text else None
        
    except requests.exceptions.RequestException as e:
        print(f'✗ Request failed: {e}')
        raise

def main():
    # Configuration - replace with your node's IP
    node_url = os.environ.get('NODE_URL', 'http://192.168.4.1')
    
    print('=== LumiGrid Sensor Update POST Example ===\n')
    print(f'Target node: {node_url}/api/sensor_update\n')

    # Example 1: Basic temperature and humidity reading
    sensor_data_1 = {
        "node_id": "sensor-room-001",
        "node_name": "Living Room Sensor",
        "timestamp": int(time.time()),
        "readings": {
            "temperature_c": 22.5,
            "humidity_percent": 48.3
        }
    }

    print('Example 1: Basic sensor reading')
    print(json.dumps(sensor_data_1, indent=2))
    
    try:
        post_sensor_update(node_url, sensor_data_1)
    except Exception as e:
        print('(Skipping - node may not be available)\n')

    print('\n' + '-' * 60 + '\n')

    # Example 2: Complete sensor reading with all fields
    sensor_data_2 = {
        "node_id": "sensor-outdoor-001",
        "node_name": "Outdoor Sensor",
        "timestamp": int(time.time()),
        "readings": {
            "temperature_c": 18.2,
            "humidity_percent": 65.7,
            "pressure_hpa": 1015.3,
            "light_lux": 850.5,
            "motion_detected": False
        }
    }

    print('Example 2: Complete sensor reading')
    print(json.dumps(sensor_data_2, indent=2))
    
    try:
        post_sensor_update(node_url, sensor_data_2)
    except Exception as e:
        print('(Skipping - node may not be available)\n')

    print('\n' + '-' * 60 + '\n')

    # Example 3: Motion detection event
    sensor_data_3 = {
        "node_id": "sensor-security-001",
        "timestamp": int(time.time()),
        "readings": {
            "motion_detected": True,
            "light_lux": 120.0
        }
    }

    print('Example 3: Motion detection')
    print(json.dumps(sensor_data_3, indent=2))
    
    try:
        post_sensor_update(node_url, sensor_data_3)
    except Exception as e:
        print('(Skipping - node may not be available)\n')

    print('\n' + '=' * 60)
    print('\nUsage notes:')
    print('- Set NODE_URL environment variable to target a specific node')
    print('- All data is validated against schema before sending')
    print('- Example: NODE_URL=http://192.168.1.100 python sensor_update_post.py')

if __name__ == '__main__':
    main()
