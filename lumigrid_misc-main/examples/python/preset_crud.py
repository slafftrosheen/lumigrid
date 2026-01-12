"""
Preset CRUD Operations Example

Demonstrates Create, Read, Update, Delete operations for LED presets
with validation using JSON Schema
"""

import json
import os
import requests
from pathlib import Path
from jsonschema import validate, ValidationError

# Load schema
schema_path = Path(__file__).parent.parent.parent / 'schemas' / 'preset-led.schema.json'
with open(schema_path) as f:
    schema = json.load(f)

def validate_preset(data):
    """Validate LED preset against schema"""
    try:
        validate(instance=data, schema=schema)
        return True, None
    except ValidationError as e:
        return False, str(e)

class PresetClient:
    """LumiGrid Preset API Client"""
    
    def __init__(self, node_url, timeout=5):
        self.node_url = node_url
        self.base_url = f'{node_url}/api/presets'
        self.timeout = timeout
    
    def create(self, preset):
        """Create a new preset"""
        is_valid, error = validate_preset(preset)
        if not is_valid:
            raise ValueError(f'Validation failed: {error}')
        
        try:
            response = requests.post(
                self.base_url,
                json=preset,
                headers={'Content-Type': 'application/json'},
                timeout=self.timeout
            )
            response.raise_for_status()
            print(f'✓ Created preset {preset["preset_id"]}: "{preset["name"]}"')
            return response.json() if response.text else None
        except requests.exceptions.RequestException as e:
            self._handle_error('create', e)
    
    def read(self, preset_id):
        """Read a preset by ID"""
        try:
            response = requests.get(
                f'{self.base_url}/{preset_id}',
                timeout=self.timeout
            )
            response.raise_for_status()
            print(f'✓ Read preset {preset_id}')
            return response.json()
        except requests.exceptions.RequestException as e:
            self._handle_error('read', e)
    
    def list(self):
        """List all presets"""
        try:
            response = requests.get(
                self.base_url,
                timeout=self.timeout
            )
            response.raise_for_status()
            data = response.json()
            count = len(data) if isinstance(data, list) else 0
            print(f'✓ Retrieved {count} presets')
            return data
        except requests.exceptions.RequestException as e:
            self._handle_error('list', e)
    
    def update(self, preset_id, preset):
        """Update an existing preset"""
        is_valid, error = validate_preset(preset)
        if not is_valid:
            raise ValueError(f'Validation failed: {error}')
        
        try:
            response = requests.put(
                f'{self.base_url}/{preset_id}',
                json=preset,
                headers={'Content-Type': 'application/json'},
                timeout=self.timeout
            )
            response.raise_for_status()
            print(f'✓ Updated preset {preset_id}')
            return response.json() if response.text else None
        except requests.exceptions.RequestException as e:
            self._handle_error('update', e)
    
    def delete(self, preset_id):
        """Delete a preset"""
        try:
            response = requests.delete(
                f'{self.base_url}/{preset_id}',
                timeout=self.timeout
            )
            response.raise_for_status()
            print(f'✓ Deleted preset {preset_id}')
            return response.json() if response.text else None
        except requests.exceptions.RequestException as e:
            self._handle_error('delete', e)
    
    def activate(self, preset_id):
        """Activate a preset (apply it to LEDs)"""
        try:
            response = requests.post(
                f'{self.base_url}/{preset_id}/activate',
                timeout=self.timeout
            )
            response.raise_for_status()
            print(f'✓ Activated preset {preset_id}')
            return response.json() if response.text else None
        except requests.exceptions.RequestException as e:
            self._handle_error('activate', e)
    
    def _handle_error(self, operation, error):
        """Handle request errors"""
        if hasattr(error, 'response') and error.response is not None:
            print(f'✗ {operation} failed: {error.response.status_code}')
        else:
            print(f'✗ {operation} failed: {error}')
        raise

def main():
    node_url = os.environ.get('NODE_URL', 'http://192.168.4.1')
    
    print('=== LumiGrid Preset CRUD Example ===\n')
    print(f'Target node: {node_url}/api/presets\n')

    client = PresetClient(node_url)

    # Define some example presets
    cool_blue = {
        "preset_id": 1,
        "name": "Cool Blue",
        "color": {"r": 28, "g": 78, "b": 255, "w": 0},
        "effect": "static"
    }

    warm_white = {
        "preset_id": 2,
        "name": "Warm White",
        "color": {"r": 0, "g": 0, "b": 0, "w": 255},
        "effect": "fade",
        "duration_ms": 2000
    }

    rainbow = {
        "preset_id": 3,
        "name": "Rainbow Party",
        "color": {"r": 255, "g": 0, "b": 0, "w": 0},
        "effect": "rainbow",
        "duration_ms": 5000
    }

    try:
        # CREATE operations
        print('--- CREATE ---')
        print('Creating preset 1: Cool Blue')
        print(json.dumps(cool_blue, indent=2))
        client.create(cool_blue)
        
        print('\nCreating preset 2: Warm White')
        client.create(warm_white)
        
        print('\nCreating preset 3: Rainbow Party')
        client.create(rainbow)

        print('\n' + '-' * 60 + '\n')

        # READ operations
        print('--- READ ---')
        print('Reading preset 1:')
        preset_1 = client.read(1)
        print(json.dumps(preset_1, indent=2))

        print('\n' + '-' * 60 + '\n')

        # LIST operation
        print('--- LIST ---')
        all_presets = client.list()
        print('All presets:')
        print(json.dumps(all_presets, indent=2))

        print('\n' + '-' * 60 + '\n')

        # UPDATE operation
        print('--- UPDATE ---')
        updated_blue = {
            **cool_blue,
            "name": "Deep Blue",
            "color": {"r": 0, "g": 50, "b": 200, "w": 0}
        }
        print('Updating preset 1 to Deep Blue:')
        client.update(1, updated_blue)

        print('\n' + '-' * 60 + '\n')

        # ACTIVATE operation
        print('--- ACTIVATE ---')
        print('Activating preset 2 (Warm White):')
        client.activate(2)

        print('\n' + '-' * 60 + '\n')

        # DELETE operation
        print('--- DELETE ---')
        print('Deleting preset 3:')
        client.delete(3)

    except Exception as e:
        print('\n(Skipping - node may not be available or API not implemented)')

    print('\n' + '=' * 60)
    print('\nUsage notes:')
    print('- Set NODE_URL environment variable to target a specific node')
    print('- All presets are validated before sending')
    print('- Example: NODE_URL=http://192.168.1.100 python preset_crud.py')
    print('\nAPI endpoints used:')
    print('  POST   /api/presets           - Create preset')
    print('  GET    /api/presets           - List all presets')
    print('  GET    /api/presets/:id       - Read preset')
    print('  PUT    /api/presets/:id       - Update preset')
    print('  DELETE /api/presets/:id       - Delete preset')
    print('  POST   /api/presets/:id/activate - Activate preset')

if __name__ == '__main__':
    main()
