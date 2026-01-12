"""Configuration loader for Lumigrid Display Node."""

import os
import yaml
import uuid
import logging
from typing import Dict, Any, List, Optional
from datetime import datetime

from lumigrid.constants import VALID_CONTROL_MODES, VALID_SYNC_ROLES, VALID_EXTERNAL_SOURCES

logger = logging.getLogger(__name__)


class Config:
    """Configuration manager for the application."""

    def __init__(self, config_path: str = "config.yaml"):
        """Initialize configuration from YAML file."""
        self.config_path = config_path
        self.config = self._load_config()
        self._ensure_directories()

    def _ensure_directories(self):
        """Ensure required directories exist."""
        # Create fonts directory for custom fonts
        fonts_dir = self.get('fonts.custom_fonts_dir', 'data/fonts')
        if not os.path.exists(fonts_dir):
            os.makedirs(fonts_dir, exist_ok=True)
            logger.info(f"Created fonts directory: {fonts_dir}")
        
        # Create data directory
        data_dir = 'data'
        if not os.path.exists(data_dir):
            os.makedirs(data_dir, exist_ok=True)
            logger.info(f"Created data directory: {data_dir}")

    def _load_config(self) -> Dict[str, Any]:
        """Load configuration from YAML file."""
        if not os.path.exists(self.config_path):
            logger.warning(f"Config file {self.config_path} not found, using defaults")
            return self._get_default_config()

        try:
            with open(self.config_path, 'r') as f:
                config = yaml.safe_load(f)
                logger.info(f"Loaded configuration from {self.config_path}")
                # Merge with defaults to ensure all keys exist
                default_config = self._get_default_config()
                return self._deep_merge(default_config, config)
        except Exception as e:
            logger.error(f"Error loading config: {e}, using defaults")
            return self._get_default_config()

    def _deep_merge(self, base: Dict, override: Dict) -> Dict:
        """Deep merge two dictionaries."""
        result = base.copy()
        for key, value in override.items():
            if key in result and isinstance(result[key], dict) and isinstance(value, dict):
                result[key] = self._deep_merge(result[key], value)
            else:
                result[key] = value
        return result

    def _get_default_config(self) -> Dict[str, Any]:
        """Return default configuration with full hzeller matrix support."""
        return {
            'led_matrix': {
                # Basic dimensions
                'rows': 32,
                'cols': 64,
                'chain_length': 1,
                'parallel': 1,
                # Hardware configuration
                'hardware_mapping': 'regular',
                'rgb_sequence': 'RGB',
                'row_address_type': 0,
                'multiplexing': 0,
                # PWM settings
                'pwm_bits': 11,
                'pwm_lsb_nanoseconds': 130,
                'pwm_dither_bits': 0,
                # Display quality
                'brightness': 50,
                'scan_mode': 0,
                'gpio_slowdown': 4,
                # Advanced options
                'show_refresh_rate': False,
                'limit_refresh_rate_hz': 0,
                'inverse_colors': False,
                'led_rgb_sequence': 'RGB',
                'pixel_mapper_config': '',
                'panel_type': '',
                'disable_hardware_pulsing': False,
                'drop_privileges': True
            },
            'display': {
                'default_mode': 'frames',
                'rotation': 0,
                'font_path': '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                'slideshow_interval': 10,
                'scroll_speed': 0.05
            },
            'frames': {
                'enabled': True,
                'default_duration': 5,
                'transition_effect': 'fade',
                'queue': [
                    {
                        'id': 'default-time',
                        'type': 'time',
                        'name': 'Current Time',
                        'enabled': True,
                        'duration': 5,
                        'font': {
                            'path': '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
                            'size': 16
                        },
                        'colors': {
                            'text': '#00FFFF',
                            'background': '#000000'
                        },
                        'position': {'x': 'center', 'y': 'center'},
                        'format': '%H:%M:%S'
                    },
                    {
                        'id': 'default-date',
                        'type': 'date',
                        'name': 'Current Date',
                        'enabled': True,
                        'duration': 5,
                        'font': {
                            'path': '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                            'size': 12
                        },
                        'colors': {
                            'text': '#FFFFFF',
                            'background': '#000000'
                        },
                        'position': {'x': 'center', 'y': 'center'},
                        'format': '%Y-%m-%d'
                    },
                    {
                        'id': 'default-temperature',
                        'type': 'temperature',
                        'name': 'Temperature',
                        'enabled': True,
                        'duration': 5,
                        'font': {
                            'path': '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
                            'size': 14
                        },
                        'colors': {
                            'text': '#FF8000',
                            'background': '#000000'
                        },
                        'position': {'x': 'center', 'y': 'center'},
                        'unit': 'celsius',
                        'show_icon': True
                    }
                ]
            },
            'fonts': {
                'system_fonts_dir': '/usr/share/fonts/truetype',
                'custom_fonts_dir': 'data/fonts',
                'default_font': '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                'installed': []
            },
            'temperature': {
                'enabled': True,
                'sensor_type': 'DS18B20',
                'pin': 4,
                'update_interval': 30,
                'unit': 'celsius'
            },
            'rtc': {
                'enabled': True,
                'type': 'DS3231',
                'sync_interval': 3600
            },
            'web': {
                'host': '0.0.0.0',
                'port': 5000,
                'debug': False,
                'rate_limiting': {
                    'enabled': True,
                    'default_limits': ['200 per day', '50 per hour'],
                    'storage_uri': 'memory://'
                }
            },
            'network': {
                'hostname': 'lumigrid',
                'node_id': 'display-node',
                'wifi': {
                    'enabled': False,
                    'ssid': '',
                    'password': ''
                },
                'static_ip': {
                    'enabled': False,
                    'ip': '',
                    'gateway': '',
                    'dns': ''
                }
            },
            'time': {
                'timezone': 'UTC',
                'format_24h': True,
                'show_seconds': True,
                'ntp_enabled': True,
                'ntp_server': 'pool.ntp.org'
            },
            'logging': {
                'level': 'INFO',
                'file': 'logs/lumigrid.log',
                'max_bytes': 10485760,
                'backup_count': 5
            },
            'control': {
                'mode': 'independent',
                'sync_role': 'slave',
                'external_source': 'mqtt'
            }
        }

    def get(self, key: str, default: Any = None) -> Any:
        """Get configuration value by key path (e.g., 'web.port')."""
        keys = key.split('.')
        value = self.config
        for k in keys:
            if isinstance(value, dict):
                value = value.get(k)
            else:
                return default
        return value if value is not None else default

    def set(self, key: str, value: Any):
        """Set configuration value by key path."""
        keys = key.split('.')
        config = self.config
        for k in keys[:-1]:
            if k not in config:
                config[k] = {}
            config = config[k]
        config[keys[-1]] = value

    def save(self):
        """Save configuration to file."""
        try:
            with open(self.config_path, 'w') as f:
                yaml.dump(self.config, f, default_flow_style=False)
                logger.info(f"Configuration saved to {self.config_path}")
            return True
        except Exception as e:
            logger.error(f"Error saving config: {e}")
            return False

    def reload(self):
        """Reload configuration from file."""
        self.config = self._load_config()
        logger.info("Configuration reloaded")

    def is_setup_complete(self) -> bool:
        """Check if initial setup has been completed.
        
        Returns:
            True if setup is complete, False otherwise.
        """
        # Check if config file exists
        if not os.path.exists(self.config_path):
            return False
        
        # Check for setup_complete flag
        if self.get('setup_complete', False):
            return True
        
        # Check for required settings that would only be set during setup
        node_id = self.get('network.node_id', 'display-node')
        control_mode = self.get('control.mode', 'independent')
        
        # If node_id is still default and no setup flag, consider setup incomplete
        # This is a heuristic - if user customized node_id, they likely configured the device
        if node_id == 'display-node' and control_mode == 'independent':
            # Check if this is genuinely first boot or just using defaults
            # If config file exists but setup_complete is not set, assume incomplete
            return False
        
        return True

    def mark_setup_complete(self) -> bool:
        """Mark initial setup as complete.
        
        Returns:
            True if successfully saved, False otherwise.
        """
        self.set('setup_complete', True)
        return self.save()

    def validate_config(self) -> List[str]:
        """Validate configuration and return list of errors.
        
        Returns:
            List of validation error messages, empty if configuration is valid.
        """
        errors = []
        
        # Validate LED matrix settings
        brightness = self.get('led_matrix.brightness', 50)
        if not isinstance(brightness, (int, float)) or not 0 <= brightness <= 100:
            errors.append("Brightness must be between 0 and 100")
        
        rows = self.get('led_matrix.rows', 32)
        if not isinstance(rows, int) or not 8 <= rows <= 128:
            errors.append("LED matrix rows must be between 8 and 128")
        
        cols = self.get('led_matrix.cols', 64)
        if not isinstance(cols, int) or not 16 <= cols <= 256:
            errors.append("LED matrix cols must be between 16 and 256")
        
        chain_length = self.get('led_matrix.chain_length', 1)
        if not isinstance(chain_length, int) or not 1 <= chain_length <= 16:
            errors.append("Chain length must be between 1 and 16")
        
        # Validate web settings
        port = self.get('web.port', 5000)
        if not isinstance(port, int) or not 1 <= port <= 65535:
            errors.append("Web port must be between 1 and 65535")
        
        # Validate temperature settings
        temp_unit = self.get('temperature.unit', 'celsius')
        if temp_unit not in ('celsius', 'fahrenheit'):
            errors.append("Temperature unit must be 'celsius' or 'fahrenheit'")
        
        # Validate time settings
        timezone = self.get('time.timezone', 'UTC')
        if not isinstance(timezone, str) or not timezone:
            errors.append("Timezone must be a non-empty string")
        
        # Validate logging settings
        log_level = self.get('logging.level', 'INFO')
        valid_levels = ('DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL')
        if log_level not in valid_levels:
            errors.append(f"Logging level must be one of: {', '.join(valid_levels)}")
        
        # Validate control settings
        control_mode = self.get('control.mode', 'independent')
        if control_mode not in VALID_CONTROL_MODES:
            errors.append(f"Control mode must be one of: {', '.join(VALID_CONTROL_MODES)}")
        
        sync_role = self.get('control.sync_role', 'slave')
        if sync_role not in VALID_SYNC_ROLES:
            errors.append(f"Sync role must be one of: {', '.join(VALID_SYNC_ROLES)}")
        
        external_source = self.get('control.external_source', 'mqtt')
        if external_source not in VALID_EXTERNAL_SOURCES:
            errors.append(f"External source must be one of: {', '.join(VALID_EXTERNAL_SOURCES)}")
        
        return errors

    # Frame management methods
    def get_frames(self) -> List[Dict[str, Any]]:
        """Get all frames from configuration."""
        return self.get('frames.queue', [])

    def get_frame(self, frame_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific frame by ID."""
        frames = self.get_frames()
        for frame in frames:
            if frame.get('id') == frame_id:
                return frame
        return None

    def add_frame(self, frame: Dict[str, Any]) -> bool:
        """Add a new frame to the queue."""
        if 'id' not in frame:
            frame['id'] = str(uuid.uuid4())[:8]
        
        frames = self.get_frames()
        frames.append(frame)
        self.set('frames.queue', frames)
        return self.save()

    def update_frame(self, frame_id: str, frame_data: Dict[str, Any]) -> bool:
        """Update an existing frame."""
        frames = self.get_frames()
        for i, frame in enumerate(frames):
            if frame.get('id') == frame_id:
                frame_data['id'] = frame_id  # Preserve ID
                frames[i] = frame_data
                self.set('frames.queue', frames)
                return self.save()
        return False

    def delete_frame(self, frame_id: str) -> bool:
        """Delete a frame from the queue."""
        frames = self.get_frames()
        frames = [f for f in frames if f.get('id') != frame_id]
        self.set('frames.queue', frames)
        return self.save()

    def reorder_frames(self, frame_ids: List[str]) -> bool:
        """Reorder frames based on provided ID list."""
        frames = self.get_frames()
        frame_dict = {f.get('id'): f for f in frames}
        
        new_order = []
        for fid in frame_ids:
            if fid in frame_dict:
                new_order.append(frame_dict[fid])
        
        # Add any frames not in the provided list at the end
        for frame in frames:
            if frame.get('id') not in frame_ids:
                new_order.append(frame)
        
        self.set('frames.queue', new_order)
        return self.save()

    def toggle_frame(self, frame_id: str, enabled: bool) -> bool:
        """Enable or disable a frame."""
        frames = self.get_frames()
        for frame in frames:
            if frame.get('id') == frame_id:
                frame['enabled'] = enabled
                self.set('frames.queue', frames)
                return self.save()
        return False

    # Font management methods
    def get_installed_fonts(self) -> List[Dict[str, str]]:
        """Get list of installed fonts (system + custom)."""
        fonts = []
        
        # System fonts
        system_dir = self.get('fonts.system_fonts_dir', '/usr/share/fonts/truetype')
        if os.path.exists(system_dir):
            for root, dirs, files in os.walk(system_dir):
                for file in files:
                    if file.endswith(('.ttf', '.otf', '.TTF', '.OTF')):
                        full_path = os.path.join(root, file)
                        fonts.append({
                            'name': os.path.splitext(file)[0],
                            'path': full_path,
                            'type': 'system'
                        })
        
        # Custom fonts
        custom_dir = self.get('fonts.custom_fonts_dir', 'data/fonts')
        if os.path.exists(custom_dir):
            for file in os.listdir(custom_dir):
                if file.endswith(('.ttf', '.otf', '.TTF', '.OTF')):
                    full_path = os.path.join(custom_dir, file)
                    fonts.append({
                        'name': os.path.splitext(file)[0],
                        'path': full_path,
                        'type': 'custom'
                    })
        
        return fonts

    def add_custom_font(self, filename: str, font_data: bytes) -> Optional[str]:
        """Save a custom font file."""
        custom_dir = self.get('fonts.custom_fonts_dir', 'data/fonts')
        os.makedirs(custom_dir, exist_ok=True)
        
        # Sanitize filename
        safe_filename = os.path.basename(filename)
        if not safe_filename.endswith(('.ttf', '.otf', '.TTF', '.OTF')):
            logger.error(f"Invalid font file extension: {safe_filename}")
            return None
        
        font_path = os.path.join(custom_dir, safe_filename)
        try:
            with open(font_path, 'wb') as f:
                f.write(font_data)
            logger.info(f"Custom font saved: {font_path}")
            return font_path
        except Exception as e:
            logger.error(f"Error saving font: {e}")
            return None

    def delete_custom_font(self, font_path: str) -> bool:
        """Delete a custom font file."""
        custom_dir = self.get('fonts.custom_fonts_dir', 'data/fonts')
        
        # Security check - only allow deleting from custom fonts directory
        if not os.path.abspath(font_path).startswith(os.path.abspath(custom_dir)):
            logger.error(f"Cannot delete font outside custom directory: {font_path}")
            return False
        
        try:
            if os.path.exists(font_path):
                os.remove(font_path)
                logger.info(f"Custom font deleted: {font_path}")
                return True
            return False
        except Exception as e:
            logger.error(f"Error deleting font: {e}")
            return False
