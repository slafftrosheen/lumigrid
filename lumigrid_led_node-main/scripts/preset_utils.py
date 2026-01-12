"""
Common utilities for preset validation scripts.
"""

import sys
from pathlib import Path


def find_schema_file(schema_filename="preset-led.schema.json"):
    """
    Find the schema file by searching common locations.
    
    Args:
        schema_filename: Name of the schema file to find
        
    Returns:
        Path object to the schema file
        
    Raises:
        FileNotFoundError: If schema file cannot be found
    """
    script_dir = Path(__file__).parent
    
    # List of possible locations to search
    search_paths = [
        # Script in scripts/ subdirectory
        script_dir.parent / "firmware" / "schemas" / schema_filename,
        # Script in root directory
        script_dir / "firmware" / "schemas" / schema_filename,
        # Absolute path if running from anywhere
        Path.cwd() / "firmware" / "schemas" / schema_filename,
    ]
    
    for path in search_paths:
        if path.exists():
            return path
    
    # Not found, raise with helpful message
    error_msg = f"Error: Schema file '{schema_filename}' not found.\nSearched in:\n"
    for path in search_paths:
        error_msg += f"  {path}\n"
    
    raise FileNotFoundError(error_msg)


def load_schema(schema_filename="preset-led.schema.json"):
    """
    Load a JSON schema file.
    
    Args:
        schema_filename: Name of the schema file to load
        
    Returns:
        Parsed JSON schema as a dict
    """
    import json
    
    schema_path = find_schema_file(schema_filename)
    with open(schema_path, 'r') as f:
        return json.load(f)
