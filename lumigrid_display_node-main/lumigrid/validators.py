"""Input validation utilities for Lumigrid Display Node.

This module provides validation functions for API inputs, configuration updates,
and file operations to ensure data integrity and security.
"""

import os
import re
import json
import logging
from datetime import datetime, date
from typing import Any, Dict, List, Optional, Set, Tuple, Union

try:
    import jsonschema
    from jsonschema import validate as json_validate, ValidationError as JSONSchemaValidationError
    JSONSCHEMA_AVAILABLE = True
except ImportError:
    JSONSCHEMA_AVAILABLE = False
    JSONSchemaValidationError = Exception

logger = logging.getLogger(__name__)

# Valid display modes
VALID_MODES: Set[str] = {'clock', 'temperature', 'message', 'slideshow', 'frames'}

# Valid frame types
VALID_FRAME_TYPES: Set[str] = {
    # Basic frame types
    'time', 'date', 'datetime', 'temperature', 'humidity', 'text', 'message',
    # Advanced frame types
    'data_visualization', 'interactive', 'template', 'conditional',
    'multi_page', 'scrolling', 'animated'
}

# Valid temperature units
VALID_TEMP_UNITS: Set[str] = {'celsius', 'fahrenheit'}

# Brightness range
MIN_BRIGHTNESS: int = 0
MAX_BRIGHTNESS: int = 100

# Font file extensions (lowercase)
ALLOWED_FONT_EXTENSIONS: Set[str] = {'ttf', 'otf'}

# Regex patterns for validation
TIME_PATTERN = re.compile(r'^([0-1][0-9]|2[0-3]):([0-5][0-9]):([0-5][0-9])$')

# Maximum file sizes
MAX_FONT_FILE_SIZE: int = 16 * 1024 * 1024  # 16MB
MAX_MESSAGE_LENGTH: int = 500
MAX_FRAME_NAME_LENGTH: int = 100
MAX_TEXT_LENGTH: int = 1000

# Duration limits (seconds)
MIN_DURATION: int = 1
MAX_DURATION: int = 3600

# Font cache size
MAX_FONT_CACHE_SIZE: int = 50

# Color validation pattern (hex format)
HEX_COLOR_PATTERN = re.compile(r'^#?([0-9A-Fa-f]{3}|[0-9A-Fa-f]{6})$')


class ValidationError(Exception):
    """Exception raised for validation errors."""
    
    def __init__(self, message: str, field: Optional[str] = None):
        """Initialize validation error.
        
        Args:
            message: Error message
            field: Optional field name that failed validation
        """
        self.message = message
        self.field = field
        super().__init__(self.message)


def validate_brightness(value: Any) -> int:
    """Validate brightness value.
    
    Args:
        value: Value to validate
        
    Returns:
        Validated brightness as integer
        
    Raises:
        ValidationError: If value is invalid
    """
    try:
        brightness = int(value)
    except (TypeError, ValueError):
        raise ValidationError(
            f"Brightness must be an integer, got {type(value).__name__}",
            field="brightness"
        )
    
    if brightness < MIN_BRIGHTNESS or brightness > MAX_BRIGHTNESS:
        raise ValidationError(
            f"Brightness must be between {MIN_BRIGHTNESS} and {MAX_BRIGHTNESS}, got {brightness}",
            field="brightness"
        )
    
    return brightness


def validate_mode(mode: Any, available_modes: Optional[Set[str]] = None) -> str:
    """Validate display mode.
    
    Args:
        mode: Mode to validate
        available_modes: Optional set of available modes (defaults to VALID_MODES)
        
    Returns:
        Validated mode string
        
    Raises:
        ValidationError: If mode is invalid
    """
    if not isinstance(mode, str):
        raise ValidationError(
            f"Mode must be a string, got {type(mode).__name__}",
            field="mode"
        )
    
    valid_modes = available_modes or VALID_MODES
    if mode not in valid_modes:
        raise ValidationError(
            f"Invalid mode '{mode}'. Valid modes: {', '.join(sorted(valid_modes))}",
            field="mode"
        )
    
    return mode


def validate_message(message: Any) -> str:
    """Validate message content.
    
    Args:
        message: Message to validate
        
    Returns:
        Validated message string
        
    Raises:
        ValidationError: If message is invalid
    """
    if not isinstance(message, str):
        raise ValidationError(
            f"Message must be a string, got {type(message).__name__}",
            field="message"
        )
    
    if len(message) > MAX_MESSAGE_LENGTH:
        raise ValidationError(
            f"Message too long. Maximum length is {MAX_MESSAGE_LENGTH} characters",
            field="message"
        )
    
    return message


def validate_hex_color(color: Any) -> str:
    """Validate hex color string.
    
    Args:
        color: Color string to validate
        
    Returns:
        Normalized hex color string with '#' prefix
        
    Raises:
        ValidationError: If color is invalid
    """
    if not isinstance(color, str):
        raise ValidationError(
            f"Color must be a string, got {type(color).__name__}",
            field="color"
        )
    
    if not HEX_COLOR_PATTERN.match(color):
        raise ValidationError(
            f"Invalid hex color format: '{color}'. Expected #RGB or #RRGGBB",
            field="color"
        )
    
    # Normalize to 6-digit hex with #
    color = color.lstrip('#')
    if len(color) == 3:
        color = ''.join([c * 2 for c in color])
    
    return f"#{color.upper()}"


def validate_font_filename(filename: Any) -> str:
    """Validate font filename.
    
    Args:
        filename: Filename to validate
        
    Returns:
        Validated filename
        
    Raises:
        ValidationError: If filename is invalid
    """
    if not isinstance(filename, str):
        raise ValidationError(
            f"Filename must be a string, got {type(filename).__name__}",
            field="filename"
        )
    
    if not filename:
        raise ValidationError("Filename cannot be empty", field="filename")
    
    # Get extension
    parts = filename.rsplit('.', 1)
    if len(parts) != 2 or parts[1].lower() not in ALLOWED_FONT_EXTENSIONS:
        raise ValidationError(
            f"Invalid font file type. Allowed: {', '.join(ALLOWED_FONT_EXTENSIONS)}",
            field="filename"
        )
    
    # Check for path traversal attempts
    basename = os.path.basename(filename)
    if basename != filename or '..' in filename:
        raise ValidationError(
            "Filename contains invalid path components",
            field="filename"
        )
    
    return basename


def validate_font_path(font_path: Any, allowed_directories: List[str]) -> str:
    """Validate font path is within allowed directories.
    
    Args:
        font_path: Path to validate
        allowed_directories: List of allowed parent directories
        
    Returns:
        Validated absolute path
        
    Raises:
        ValidationError: If path is outside allowed directories
    """
    if not isinstance(font_path, str):
        raise ValidationError(
            f"Font path must be a string, got {type(font_path).__name__}",
            field="font_path"
        )
    
    if not font_path:
        raise ValidationError("Font path cannot be empty", field="font_path")
    
    abs_path = os.path.abspath(font_path)
    
    for allowed_dir in allowed_directories:
        allowed_abs = os.path.abspath(allowed_dir)
        if abs_path.startswith(allowed_abs + os.sep) or abs_path == allowed_abs:
            return abs_path
    
    raise ValidationError(
        "Font path is outside allowed directories",
        field="font_path"
    )


def validate_frame_data(frame_data: Any) -> Dict[str, Any]:
    """Validate frame configuration data.
    
    Args:
        frame_data: Frame data dictionary to validate
        
    Returns:
        Validated frame data
        
    Raises:
        ValidationError: If frame data is invalid
    """
    if not isinstance(frame_data, dict):
        raise ValidationError(
            f"Frame data must be a dictionary, got {type(frame_data).__name__}",
            field="frame"
        )
    
    validated: Dict[str, Any] = {}
    
    # Validate frame type
    if 'type' in frame_data:
        frame_type = frame_data['type']
        if not isinstance(frame_type, str) or frame_type not in VALID_FRAME_TYPES:
            raise ValidationError(
                f"Invalid frame type '{frame_type}'. Valid types: {', '.join(sorted(VALID_FRAME_TYPES))}",
                field="type"
            )
        validated['type'] = frame_type
    
    # Validate name
    if 'name' in frame_data:
        name = frame_data['name']
        if not isinstance(name, str):
            raise ValidationError("Frame name must be a string", field="name")
        if len(name) > MAX_FRAME_NAME_LENGTH:
            raise ValidationError(
                f"Frame name too long. Maximum length is {MAX_FRAME_NAME_LENGTH}",
                field="name"
            )
        validated['name'] = name
    
    # Validate enabled flag
    if 'enabled' in frame_data:
        if not isinstance(frame_data['enabled'], bool):
            raise ValidationError("Enabled must be a boolean", field="enabled")
        validated['enabled'] = frame_data['enabled']
    
    # Validate duration
    if 'duration' in frame_data:
        try:
            duration = int(frame_data['duration'])
            if duration < MIN_DURATION or duration > MAX_DURATION:
                raise ValidationError(
                    f"Duration must be between {MIN_DURATION} and {MAX_DURATION} seconds",
                    field="duration"
                )
            validated['duration'] = duration
        except (TypeError, ValueError):
            raise ValidationError("Duration must be an integer", field="duration")
    
    # Validate text
    if 'text' in frame_data:
        text = frame_data['text']
        if not isinstance(text, str):
            raise ValidationError("Text must be a string", field="text")
        if len(text) > MAX_TEXT_LENGTH:
            raise ValidationError(
                f"Text too long. Maximum length is {MAX_TEXT_LENGTH}",
                field="text"
            )
        validated['text'] = text
    
    # Validate colors
    if 'colors' in frame_data:
        colors = frame_data['colors']
        if not isinstance(colors, dict):
            raise ValidationError("Colors must be a dictionary", field="colors")
        
        validated_colors = {}
        for key, value in colors.items():
            if key in ('text', 'background', 'accent'):
                try:
                    validated_colors[key] = validate_hex_color(value)
                except ValidationError as e:
                    raise ValidationError(f"Invalid {key} color: {e.message}", field=f"colors.{key}")
        validated['colors'] = validated_colors
    
    # Validate unit
    if 'unit' in frame_data:
        unit = frame_data['unit']
        if unit not in VALID_TEMP_UNITS:
            raise ValidationError(
                f"Invalid unit '{unit}'. Valid units: {', '.join(VALID_TEMP_UNITS)}",
                field="unit"
            )
        validated['unit'] = unit
    
    # Validate font
    if 'font' in frame_data:
        font = frame_data['font']
        if not isinstance(font, dict):
            raise ValidationError("Font must be a dictionary", field="font")
        
        validated_font = {}
        if 'size' in font:
            try:
                size = int(font['size'])
                if size < 6 or size > 72:
                    raise ValidationError(
                        "Font size must be between 6 and 72",
                        field="font.size"
                    )
                validated_font['size'] = size
            except (TypeError, ValueError):
                raise ValidationError("Font size must be an integer", field="font.size")
        
        if 'path' in font:
            if not isinstance(font['path'], str):
                raise ValidationError("Font path must be a string", field="font.path")
            validated_font['path'] = font['path']
        
        validated['font'] = validated_font
    
    # Validate position
    if 'position' in frame_data:
        position = frame_data['position']
        if not isinstance(position, dict):
            raise ValidationError("Position must be a dictionary", field="position")
        
        validated_position = {}
        for axis in ('x', 'y'):
            if axis in position:
                value = position[axis]
                if isinstance(value, str):
                    if value not in ('center', 'left', 'right', 'top', 'bottom'):
                        raise ValidationError(
                            f"Invalid position value '{value}' for {axis}",
                            field=f"position.{axis}"
                        )
                elif isinstance(value, int):
                    if value < -1000 or value > 1000:
                        raise ValidationError(
                            f"Position {axis} out of range",
                            field=f"position.{axis}"
                        )
                else:
                    raise ValidationError(
                        f"Position {axis} must be a string or integer",
                        field=f"position.{axis}"
                    )
                validated_position[axis] = value
        validated['position'] = validated_position
    
    # Copy remaining valid fields
    for key in ('id', 'format', 'show_icon', 'scroll'):
        if key in frame_data:
            validated[key] = frame_data[key]
    
    return validated


def validate_led_matrix_config(config: Any) -> Dict[str, Any]:
    """Validate LED matrix configuration.
    
    Args:
        config: Configuration dictionary to validate
        
    Returns:
        Validated configuration
        
    Raises:
        ValidationError: If configuration is invalid
    """
    if not isinstance(config, dict):
        raise ValidationError(
            f"LED matrix config must be a dictionary, got {type(config).__name__}",
            field="led_matrix"
        )
    
    validated: Dict[str, Any] = {}
    
    # Validate numeric fields with ranges
    int_fields = {
        'rows': (8, 128),
        'cols': (16, 256),
        'chain_length': (1, 16),
        'parallel': (1, 6),
        'brightness': (0, 100),
        'gpio_slowdown': (0, 4),
        'pwm_bits': (1, 11),
        'pwm_lsb_nanoseconds': (50, 1000),
        'row_address_type': (0, 4),
        'multiplexing': (0, 18),
        'scan_mode': (0, 1),
        'pwm_dither_bits': (0, 2),
        'limit_refresh_rate_hz': (0, 1000),
    }
    
    for field, (min_val, max_val) in int_fields.items():
        if field in config:
            try:
                value = int(config[field])
                if value < min_val or value > max_val:
                    raise ValidationError(
                        f"{field} must be between {min_val} and {max_val}",
                        field=field
                    )
                validated[field] = value
            except (TypeError, ValueError):
                raise ValidationError(f"{field} must be an integer", field=field)
    
    # Validate string fields
    string_fields = ['hardware_mapping', 'rgb_sequence', 'led_rgb_sequence', 
                     'pixel_mapper_config', 'panel_type']
    for field in string_fields:
        if field in config:
            if not isinstance(config[field], str):
                raise ValidationError(f"{field} must be a string", field=field)
            validated[field] = config[field]
    
    # Validate boolean fields
    bool_fields = ['show_refresh_rate', 'inverse_colors', 'disable_hardware_pulsing', 
                   'drop_privileges']
    for field in bool_fields:
        if field in config:
            if not isinstance(config[field], bool):
                raise ValidationError(f"{field} must be a boolean", field=field)
            validated[field] = config[field]
    
    return validated


def sanitize_path(path: str, base_dir: str) -> Optional[str]:
    """Sanitize a file path to prevent path traversal.
    
    Args:
        path: Path to sanitize
        base_dir: Base directory that the path must be within
        
    Returns:
        Sanitized absolute path if valid, None if invalid
    """
    if not path or not isinstance(path, str):
        return None
    
    # Get absolute paths
    abs_base = os.path.abspath(base_dir)
    abs_path = os.path.abspath(os.path.join(abs_base, path))
    
    # Check that the path is within base directory
    if not abs_path.startswith(abs_base + os.sep) and abs_path != abs_base:
        logger.warning(f"Path traversal attempt detected: {path}")
        return None
    
    return abs_path


# Pattern for stripping potentially dangerous HTML/script characters
HTML_UNSAFE_PATTERN = re.compile(r'[<>"\']')


def sanitize_text(text: str, max_length: Optional[int] = None) -> str:
    """Sanitize text content to prevent XSS and other injection issues.
    
    Args:
        text: Text to sanitize
        max_length: Optional maximum length to truncate to
        
    Returns:
        Sanitized text string
    """
    if not isinstance(text, str):
        text = str(text)
    
    # Remove potentially dangerous characters
    sanitized = HTML_UNSAFE_PATTERN.sub('', text)
    
    # Truncate if max_length specified
    if max_length is not None and len(sanitized) > max_length:
        sanitized = sanitized[:max_length]
    
    return sanitized


def sanitize_frame_config(config: Dict[str, Any]) -> Dict[str, Any]:
    """Sanitize frame configuration to prevent XSS and other issues.
    
    Args:
        config: Frame configuration dictionary to sanitize
        
    Returns:
        Sanitized configuration dictionary
    """
    if not isinstance(config, dict):
        return {}
    
    sanitized = config.copy()
    
    # Sanitize text fields
    if 'text' in sanitized:
        sanitized['text'] = sanitize_text(
            sanitized['text'], 
            max_length=MAX_TEXT_LENGTH
        )
    
    if 'name' in sanitized:
        sanitized['name'] = sanitize_text(
            sanitized['name'], 
            max_length=MAX_FRAME_NAME_LENGTH
        )
    
    if 'message' in sanitized:
        sanitized['message'] = sanitize_text(
            sanitized['message'], 
            max_length=MAX_MESSAGE_LENGTH
        )
    
    # Sanitize format string (only allow safe format specifiers)
    if 'format' in sanitized:
        format_str = sanitized['format']
        if isinstance(format_str, str):
            # Use a more restrictive pattern that only allows specific strftime codes
            # with safe separators between them
            safe_format_pattern = re.compile(
                r'^(%[YymdHIMS%aAbBpjUWwxXzZ]|[:\s\-/.,])+$'
            )
            if not safe_format_pattern.match(format_str):
                logger.warning(f"Invalid format string removed: {format_str}")
                sanitized['format'] = '%H:%M:%S'  # Default safe format
    
    return sanitized


def validate_timeline_entry(entry_data: Any) -> Dict[str, Any]:
    """Validate timeline entry configuration data.
    
    Args:
        entry_data: Timeline entry data dictionary to validate
        
    Returns:
        Validated entry data dictionary
        
    Raises:
        ValueError: If validation fails
    """
    if not isinstance(entry_data, dict):
        raise ValueError("Timeline entry data must be a dictionary")
    
    validated = {}
    
    # Required fields
    if 'preset_name' not in entry_data:
        raise ValueError("Timeline entry must have a preset_name")
    validated['preset_name'] = str(entry_data['preset_name']).strip()
    if not validated['preset_name']:
        raise ValueError("preset_name cannot be empty")
    
    # Trigger type
    valid_trigger_types = {'time_of_day', 'datetime', 'interval', 'manual'}
    trigger_type = entry_data.get('trigger_type', 'time_of_day')
    if trigger_type not in valid_trigger_types:
        raise ValueError(f"Invalid trigger_type: {trigger_type}. Must be one of {valid_trigger_types}")
    validated['trigger_type'] = trigger_type
    
    # Validate trigger-specific fields
    if trigger_type == 'time_of_day':
        if 'time_of_day' in entry_data:
            # Validate time format HH:MM:SS
            if not TIME_PATTERN.match(str(entry_data['time_of_day'])):
                raise ValueError(f"Invalid time_of_day format: {entry_data['time_of_day']}. Expected HH:MM:SS")
            validated['time_of_day'] = str(entry_data['time_of_day'])
    
    elif trigger_type == 'datetime':
        if 'datetime' in entry_data:
            # Validate ISO datetime format
            try:
                datetime.fromisoformat(str(entry_data['datetime']))
                validated['datetime'] = str(entry_data['datetime'])
            except ValueError:
                raise ValueError(f"Invalid datetime format: {entry_data['datetime']}. Expected ISO format (YYYY-MM-DDTHH:MM:SS)")
    
    elif trigger_type == 'interval':
        if 'interval_seconds' in entry_data:
            interval = int(entry_data['interval_seconds'])
            if interval < 1:
                raise ValueError("interval_seconds must be at least 1")
            validated['interval_seconds'] = interval
    
    # Days of week (0=Monday, 6=Sunday)
    if 'days_of_week' in entry_data:
        days = entry_data['days_of_week']
        if not isinstance(days, list):
            raise ValueError("days_of_week must be a list")
        if not all(isinstance(d, int) and 0 <= d <= 6 for d in days):
            raise ValueError("days_of_week must contain integers 0-6 (0=Monday, 6=Sunday)")
        validated['days_of_week'] = days
    
    # Calendar-based scheduling fields
    # Date range validation
    if 'valid_from' in entry_data and entry_data['valid_from']:
        try:
            date.fromisoformat(str(entry_data['valid_from']))
            validated['valid_from'] = str(entry_data['valid_from'])
        except ValueError:
            raise ValueError(f"Invalid valid_from format: {entry_data['valid_from']}. Expected YYYY-MM-DD")
    
    if 'valid_until' in entry_data and entry_data['valid_until']:
        try:
            date.fromisoformat(str(entry_data['valid_until']))
            validated['valid_until'] = str(entry_data['valid_until'])
        except ValueError:
            raise ValueError(f"Invalid valid_until format: {entry_data['valid_until']}. Expected YYYY-MM-DD")
    
    # Validate date range logic
    if 'valid_from' in validated and 'valid_until' in validated:
        from_date = date.fromisoformat(validated['valid_from'])
        until_date = date.fromisoformat(validated['valid_until'])
        if from_date > until_date:
            raise ValueError("valid_from must be before or equal to valid_until")
    
    # Time range validation (for time_of_day triggers)
    if 'start_time' in entry_data and entry_data['start_time']:
        if not TIME_PATTERN.match(str(entry_data['start_time'])):
            raise ValueError(f"Invalid start_time format: {entry_data['start_time']}. Expected HH:MM:SS")
        validated['start_time'] = str(entry_data['start_time'])
    
    if 'end_time' in entry_data and entry_data['end_time']:
        if not TIME_PATTERN.match(str(entry_data['end_time'])):
            raise ValueError(f"Invalid end_time format: {entry_data['end_time']}. Expected HH:MM:SS")
        validated['end_time'] = str(entry_data['end_time'])
    
    # Exception dates validation
    if 'exception_dates' in entry_data:
        if not isinstance(entry_data['exception_dates'], list):
            raise ValueError("exception_dates must be a list")
        validated_dates = []
        for date_str in entry_data['exception_dates']:
            try:
                date.fromisoformat(str(date_str))
                validated_dates.append(str(date_str))
            except ValueError:
                raise ValueError(f"Invalid exception date format: {date_str}. Expected YYYY-MM-DD")
        validated['exception_dates'] = validated_dates
    
    # Duration
    if 'duration' in entry_data:
        duration = int(entry_data['duration'])
        if duration < 0:
            raise ValueError("duration must be non-negative (0 = until next entry)")
        validated['duration'] = duration
    
    # Enabled flag
    if 'enabled' in entry_data:
        validated['enabled'] = bool(entry_data['enabled'])
    
    # Optional: ID and last_triggered
    if 'id' in entry_data:
        validated['id'] = str(entry_data['id'])
    if 'last_triggered' in entry_data:
        validated['last_triggered'] = float(entry_data['last_triggered'])
    
    return validated


def validate_timeline(timeline_data: Any) -> Dict[str, Any]:
    """Validate timeline configuration data.
    
    Args:
        timeline_data: Timeline data dictionary to validate
        
    Returns:
        Validated timeline data dictionary
        
    Raises:
        ValueError: If validation fails
    """
    if not isinstance(timeline_data, dict):
        raise ValueError("Timeline data must be a dictionary")
    
    validated = {}
    
    # Required fields
    if 'name' not in timeline_data:
        raise ValueError("Timeline must have a name")
    validated['name'] = str(timeline_data['name']).strip()
    if not validated['name']:
        raise ValueError("Timeline name cannot be empty")
    
    # Optional fields
    if 'description' in timeline_data:
        validated['description'] = str(timeline_data['description'])
    
    if 'enabled' in timeline_data:
        validated['enabled'] = bool(timeline_data['enabled'])
    
    if 'loop' in timeline_data:
        validated['loop'] = bool(timeline_data['loop'])
    
    # Entries
    if 'entries' not in timeline_data:
        raise ValueError("Timeline must have entries")
    
    if not isinstance(timeline_data['entries'], list):
        raise ValueError("Timeline entries must be a list")
    
    validated['entries'] = []
    for i, entry_data in enumerate(timeline_data['entries']):
        try:
            validated_entry = validate_timeline_entry(entry_data)
            validated['entries'].append(validated_entry)
        except ValueError as e:
            raise ValueError(f"Invalid entry at index {i}: {e}")
    
    # Optional: ID
    if 'id' in timeline_data:
        validated['id'] = str(timeline_data['id'])
    
    return validated


# Schema file path
_SCHEMA_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'schemas')
_PRESET_SCHEMA_PATH = os.path.join(_SCHEMA_DIR, 'preset-display.schema.json')
_CONFIG_SCHEMA_PATH = os.path.join(_SCHEMA_DIR, 'config.schema.json')
_SENSOR_DATA_SCHEMA_PATH = os.path.join(_SCHEMA_DIR, 'sensor-data.schema.json')
_TRIGGER_SCHEMA_PATH = os.path.join(_SCHEMA_DIR, 'trigger.schema.json')
_CHANNEL_GROUP_SCHEMA_PATH = os.path.join(_SCHEMA_DIR, 'channel-group.schema.json')
_PRESET_LINK_SCHEMA_PATH = os.path.join(_SCHEMA_DIR, 'preset-link.schema.json')
_CONDITIONAL_LOGIC_SCHEMA_PATH = os.path.join(_SCHEMA_DIR, 'conditional-logic.schema.json')

_SCHEMA_CACHE: Dict[str, Dict[str, Any]] = {}

# Example valid preset for error messages
_EXAMPLE_VALID_PRESET = {
    "preset_name": "display_time_small_red",
    "description": "Small time display, red color",
    "frame_config": {
        "type": "time",
        "name": "Small Red Time",
        "enabled": True,
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


def _load_schema(schema_path: str) -> Dict[str, Any]:
    """Load a JSON schema from file.
    
    Args:
        schema_path: Path to the schema file

    Returns:
        Schema dictionary
        
    Raises:
        FileNotFoundError: If schema file not found
        ValueError: If schema file is invalid JSON
    """
    global _SCHEMA_CACHE
    
    if schema_path in _SCHEMA_CACHE:
        return _SCHEMA_CACHE[schema_path]
    
    if not os.path.exists(schema_path):
        raise FileNotFoundError(f"Schema file not found at {schema_path}")
    
    try:
        with open(schema_path, 'r', encoding='utf-8') as f:
            schema = json.load(f)
            _SCHEMA_CACHE[schema_path] = schema
        return schema
    except json.JSONDecodeError as e:
        raise ValueError(f"Invalid JSON in schema file: {e}")


def _validate_schema(data: Any, schema_path: str, example: Dict[str, Any]) -> Dict[str, Any]:
    """Validate data against a JSON schema.
    
    Args:
        data: Data to validate
        schema_path: Path to the schema file
        example: Example valid data for error messages
        
    Returns:
        Validated data
        
    Raises:
        ValidationError: If validation fails
    """
    if not JSONSCHEMA_AVAILABLE:
        logger.warning(
            "jsonschema library not available, skipping schema validation. "
            "Install with: pip install jsonschema"
        )
        if not isinstance(data, dict):
            raise ValidationError(
                "Data must be a JSON object (dictionary)",
                field="data"
            )
        return data
    
    try:
        schema = _load_schema(schema_path)
    except (FileNotFoundError, ValueError) as e:
        logger.error(f"Failed to load schema: {e}")
        if not isinstance(data, dict):
            raise ValidationError(
                "Data must be a JSON object (dictionary)",
                field="data"
            )
        return data
    
    try:
        json_validate(instance=data, schema=schema)
        return data
    except JSONSchemaValidationError as e:
        if e.path:
            try:
                error_path = ' -> '.join(str(p) for p in e.path)
            except (TypeError, AttributeError, ValueError):
                error_path = 'validation error'
        else:
            error_path = 'root'
        
        error_message = (
            f"Schema validation failed at '{error_path}': {e.message}. "
            f"Example valid data: {json.dumps(example, indent=2)}"
        )
        
        raise ValidationError(error_message, field=error_path)


def validate_preset_schema(preset_data: Any) -> Dict[str, Any]:
    """Validate preset data against the JSON schema."""
    return _validate_schema(preset_data, _PRESET_SCHEMA_PATH, _EXAMPLE_VALID_PRESET)


def validate_config_schema(config_data: Any) -> Dict[str, Any]:
    """Validate config data against the JSON schema."""
    example = {
        "control_mode": "Sync",
        "sync_role": "Slave",
        "node_id": "lumigrid-display-01",
        "network": {
            "hostname": "lumigrid-01",
            "mdns_enabled": True
        }
    }
    return _validate_schema(config_data, _CONFIG_SCHEMA_PATH, example)


def validate_sensor_data_schema(sensor_data: Any) -> Dict[str, Any]:
    """Validate sensor data against the JSON schema."""
    example = {
        "node_id": "lumigrid-sensor-01",
        "readings": {
            "temperature": 25.5,
            "humidity": 60.0
        },
        "timestamp": 1234567890
    }
    return _validate_schema(sensor_data, _SENSOR_DATA_SCHEMA_PATH, example)


def validate_trigger_schema(trigger_data: Any) -> Dict[str, Any]:
    """Validate trigger data against the JSON schema."""
    example = {
        "action": "play_preset",
        "name": "rainbow_fast"
    }
    return _validate_schema(trigger_data, _TRIGGER_SCHEMA_PATH, example)


def validate_channel_group_schema(group_data: Any) -> Dict[str, Any]:
    """Validate channel group data against the JSON schema."""
    example = {
        "id": "group-1",
        "name": "LED Strip Group",
        "channels": ["led_0", "led_1", "led_2"],
        "enabled": True
    }
    return _validate_schema(group_data, _CHANNEL_GROUP_SCHEMA_PATH, example)


def validate_preset_link_schema(link_data: Any) -> Dict[str, Any]:
    """Validate preset link data against the JSON schema."""
    example = {
        "id": "link-1",
        "source_preset": "Morning Display",
        "target_node": "192.168.1.100",
        "target_preset": "Morning Lights",
        "link_type": "trigger"
    }
    return _validate_schema(link_data, _PRESET_LINK_SCHEMA_PATH, example)


def validate_conditional_logic_schema(logic_data: Any) -> Dict[str, Any]:
    """Validate conditional logic data against the JSON schema."""
    example = {
        "id": "condition-1",
        "name": "Night Motion Detection",
        "conditions": [
            {"type": "sensor", "sensor_type": "motion", "operator": "equals", "value": True},
            {"type": "time", "operator": "in_range", "time_start": "22:00:00", "time_end": "06:00:00"}
        ],
        "logic_operator": "AND",
        "action_if_true": {
            "type": "play_preset",
            "preset_name": "Motion Alert"
        }
    }
    return _validate_schema(logic_data, _CONDITIONAL_LOGIC_SCHEMA_PATH, example)
