"""Flask web application for Lumigrid Display Node."""

import os
import logging
from typing import Any, Dict, Optional, Tuple, Union

from flask import Flask, render_template, request, jsonify, send_from_directory, Response, redirect
from flask_cors import CORS
from flask_limiter import Limiter
from flask_limiter.util import get_remote_address
from werkzeug.utils import secure_filename

from lumigrid.validators import (
    ValidationError,
    validate_brightness,
    validate_mode,
    validate_message,
    validate_frame_data,
    validate_led_matrix_config,
    validate_font_filename,
    sanitize_frame_config,
    validate_preset_schema,
    validate_config_schema,
    validate_sensor_data_schema,
    validate_trigger_schema,
    ALLOWED_FONT_EXTENSIONS,
    MAX_FONT_FILE_SIZE,
)
from lumigrid.constants import (
    VALID_CONTROL_MODES,
    VALID_SYNC_ROLES,
    VALID_EXTERNAL_SOURCES
)

logger = logging.getLogger(__name__)


def allowed_font_file(filename: str) -> bool:
    """Check if the file extension is allowed for fonts.
    
    Args:
        filename: Name of the file to check
        
    Returns:
        True if file extension is allowed, False otherwise
    """
    return '.' in filename and \
           filename.rsplit('.', 1)[1].lower() in ALLOWED_FONT_EXTENSIONS


def error_response(message: str, status_code: int = 400) -> Tuple[Response, int]:
    """Create a standardized error response.
    
    Args:
        message: Error message
        status_code: HTTP status code
        
    Returns:
        Tuple of (jsonified response, status code)
    """
    return jsonify({'error': message}), status_code


def success_response(data: Optional[Dict[str, Any]] = None, 
                     message: Optional[str] = None) -> Response:
    """Create a standardized success response.
    
    Args:
        data: Optional data to include in response
        message: Optional success message
        
    Returns:
        Jsonified response
    """
    response: Dict[str, Any] = {'success': True}
    if message:
        response['message'] = message
    if data:
        response.update(data)
    return jsonify(response)


class WebInterface:
    """Web interface for controlling the display."""

    def __init__(self, config: Dict[str, Any], app_controller: Any) -> None:
        """Initialize web interface.

        Args:
            config: Web configuration dictionary
            app_controller: Main application controller
        """
        self.config = config
        self.controller = app_controller
        self.app = Flask(__name__, 
                        template_folder='templates',
                        static_folder='static')
        CORS(self.app)
        self.app.config['MAX_CONTENT_LENGTH'] = MAX_FONT_FILE_SIZE
        
        # Setup rate limiting
        rate_limit_config = config.get('rate_limiting', {})
        rate_limiting_enabled = rate_limit_config.get('enabled', True)
        
        if rate_limiting_enabled:
            default_limits = rate_limit_config.get(
                'default_limits', 
                ['200 per day', '50 per hour']
            )
            storage_uri = rate_limit_config.get('storage_uri', 'memory://')
            
            self.limiter = Limiter(
                get_remote_address,
                app=self.app,
                default_limits=default_limits,
                storage_uri=storage_uri
            )
            logger.info(f"Rate limiting enabled with limits: {default_limits}")
        else:
            self.limiter = None
            logger.info("Rate limiting disabled")
        
        self._setup_routes()
        self._setup_setup_wizard()

    def _setup_setup_wizard(self) -> None:
        """Setup wizard redirect middleware."""
        
        @self.app.before_request
        def check_setup_complete():
            """Redirect to setup wizard if initial setup is not complete."""
            # Skip check for setup wizard routes and static files
            if request.path.startswith('/setup-wizard') or \
               request.path.startswith('/static/') or \
               request.path.startswith('/api/wizard/'):
                return None
            
            # Check if setup is complete
            if not self.controller.config.is_setup_complete():
                return redirect('/setup-wizard')
            
            return None

    def _setup_routes(self) -> None:
        """Setup Flask routes."""

        # ==================== SETUP WIZARD ROUTES ====================
        
        @self.app.route('/setup-wizard')
        def setup_wizard():
            """Setup wizard page."""
            return render_template('setup_wizard.html')
        
        @self.app.route('/api/wizard/network-info')
        def wizard_network_info():
            """Get network information for setup wizard."""
            try:
                import socket
                import subprocess
                
                # Get hostname
                hostname = socket.gethostname()
                
                # Get IP address
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                try:
                    s.connect(('8.8.8.8', 80))
                    ip = s.getsockname()[0]
                except Exception:
                    ip = '127.0.0.1'
                finally:
                    s.close()
                
                # Try to detect connection type
                connection = 'Unknown'
                try:
                    result = subprocess.run(['nmcli', '-t', '-f', 'TYPE,DEVICE', 'device'], 
                                          capture_output=True, text=True, timeout=2)
                    if result.returncode == 0:
                        lines = result.stdout.strip().split('\n')
                        for line in lines:
                            if 'ethernet' in line.lower():
                                connection = 'Ethernet'
                                break
                            elif 'wifi' in line.lower():
                                connection = 'Wi-Fi'
                                break
                except Exception:
                    pass
                
                return jsonify({
                    'hostname': hostname,
                    'ip': ip,
                    'connection': connection
                })
            except Exception as e:
                logger.error(f"Error getting network info: {e}")
                return error_response(str(e), 500)
        
        @self.app.route('/api/wizard/save', methods=['POST'])
        def wizard_save():
            """Save configuration from setup wizard."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                # Update configuration
                if 'network' in data:
                    current_network = self.controller.config.get('network', {})
                    current_network.update(data['network'])
                    self.controller.config.set('network', current_network)
                
                if 'control' in data:
                    current_control = self.controller.config.get('control', {})
                    current_control.update(data['control'])
                    self.controller.config.set('control', current_control)
                
                if 'led_matrix' in data:
                    current_led = self.controller.config.get('led_matrix', {})
                    current_led.update(data['led_matrix'])
                    self.controller.config.set('led_matrix', current_led)
                
                # Mark setup as complete
                self.controller.config.mark_setup_complete()
                
                return success_response(message='Configuration saved successfully')
            except Exception as e:
                logger.error(f"Error saving wizard configuration: {e}")
                return error_response(str(e), 500)

        # ==================== PAGE ROUTES ====================
        
        @self.app.route('/')
        def index():
            """Main dashboard page."""
            return render_template('index.html')

        @self.app.route('/display-settings')
        def display_settings():
            """Display settings page."""
            return render_template('display_settings.html')

        @self.app.route('/content')
        def content():
            """Content/Frames management page."""
            return render_template('content.html')

        @self.app.route('/network')
        def network():
            """Network settings page."""
            return render_template('network.html')

        @self.app.route('/timelines')
        def timelines():
            """Timeline/Sequencer management page."""
            return render_template('timelines.html')

        @self.app.route('/faq')
        def faq():
            """FAQ page."""
            return render_template('faq.html')

        @self.app.route('/help')
        def help_page():
            """Help & Documentation page."""
            return render_template('help.html')

        @self.app.route('/presets')
        def presets():
            """Preset management page."""
            return render_template('presets.html')

        # ==================== STATUS API ====================

        @self.app.route('/api/status')
        def get_status():
            """Get current device and system status."""
            try:
                status = self.controller.get_status()
                return jsonify({
                    "id": status.get("node_id"),
                    "type": "display",
                    "ip": request.remote_addr,
                    "state": "running" if status.get("running") else "stopped",
                    "uptime": status.get("uptime"),
                    "metrics": {
                        "temperature": status.get("temperature"),
                        "brightness": status.get("brightness")
                    }
                })
            except Exception as e:
                logger.error(f"Error getting status: {e}")
                return error_response(str(e), 500)

        # ==================== MODE API ====================

        @self.app.route('/api/mode', methods=['GET', 'POST'])
        def display_mode():
            """Get or set display mode."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    mode = data.get('mode')
                    if not mode:
                        return error_response('Mode required')
                    
                    # Validate mode against available modes
                    available_modes = set(self.controller.modes.keys())
                    try:
                        validated_mode = validate_mode(mode, available_modes)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    self.controller.set_mode(validated_mode)
                    return success_response({'mode': validated_mode})
                except ValidationError as e:
                    logger.warning(f"Validation error setting mode: {e.message}")
                    return error_response(e.message)
                except Exception as e:
                    logger.error(f"Error setting mode: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    mode = self.controller.get_current_mode()
                    return jsonify({'mode': mode})
                except Exception as e:
                    logger.error(f"Error getting mode: {e}")
                    return error_response(str(e), 500)

        @self.app.route('/api/message', methods=['POST'])
        def set_message():
            """Set custom message."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                message = data.get('message', '')
                try:
                    validated_message = validate_message(message)
                except ValidationError as e:
                    return error_response(e.message)
                
                self.controller.set_message(validated_message)
                return success_response({'message': validated_message})
            except ValidationError as e:
                logger.warning(f"Validation error setting message: {e.message}")
                return error_response(e.message)
            except Exception as e:
                logger.error(f"Error setting message: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/brightness', methods=['GET', 'POST'])
        def brightness():
            """Get or set brightness."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    value = data.get('brightness')
                    if value is None:
                        return error_response('Brightness value required')
                    
                    try:
                        validated_brightness = validate_brightness(value)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    self.controller.set_brightness(validated_brightness)
                    return success_response({'brightness': validated_brightness})
                except ValidationError as e:
                    logger.warning(f"Validation error setting brightness: {e.message}")
                    return error_response(e.message)
                except Exception as e:
                    logger.error(f"Error setting brightness: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    brightness_val = self.controller.get_brightness()
                    return jsonify({'brightness': brightness_val})
                except Exception as e:
                    logger.error(f"Error getting brightness: {e}")
                    return error_response(str(e), 500)

        # ==================== SENSOR API ====================

        @self.app.route('/api/temperature')
        def get_temperature():
            """Get current temperature."""
            try:
                temp = self.controller.get_temperature()
                if temp is not None:
                    unit = self.controller.config.get('temperature.unit', 'celsius')
                    return jsonify({'temperature': temp, 'unit': unit})
                else:
                    return error_response('Temperature not available', 404)
            except Exception as e:
                logger.error(f"Error getting temperature: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/humidity')
        def get_humidity():
            """Get current humidity."""
            try:
                humidity = self.controller.get_humidity()
                if humidity is not None:
                    return jsonify({'humidity': humidity, 'unit': 'percent'})
                else:
                    return error_response('Humidity not available', 404)
            except Exception as e:
                logger.error(f"Error getting humidity: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/discover/needs_sensor_data')
        def needs_sensor_data():
            """Discover sensor data requirements for this display node.
            
            Returns information about what sensor data this display node needs
            from sensor nodes in the LumiGrid network. Used by sensor nodes to
            determine which display nodes to push data to.
            
            This endpoint enables the sensor bridging functionality where ESP32
            sensor nodes can push data to Raspberry Pi display nodes for
            visualization.
            
            Example Response:
                {
                    "needs_data": true,
                    "types": ["temperature", "humidity", "motion", "light"],
                    "node_id": "lumigrid-display-01",
                    "node_type": "display"
                }
            
            Response Fields:
                - needs_data: Boolean indicating if node accepts sensor data
                - types: Array of sensor types this node is interested in
                - node_id: Unique identifier for this display node
                - node_type: Type of node (always "display" for display nodes)
            
            Returns:
                200: Sensor data requirements
            """
            return jsonify({
                'needs_data': True,
                'types': ['temperature', 'humidity', 'motion', 'light'],
                'node_id': self.controller.config.get('node_id'),
                'node_type': 'display'
            })

        @self.app.route('/api/sensor_update', methods=['POST'])
        def sensor_update():
            """Receive sensor data from sensor nodes."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')

                validate_sensor_data_schema(data)

                # Store sensor data
                self.controller.update_sensor_data(data)
                return success_response(message='Sensor data received')
            except ValidationError as e:
                return error_response(e.message)
            except Exception as e:
                logger.error(f"Error processing sensor update: {e}")
                return error_response(str(e), 500)

        # ==================== CONFIG API ====================

        @self.app.route('/api/config', methods=['GET', 'POST'])
        def configuration():
            """Get or update device configuration."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if data is None:
                        return error_response('Request body required')

                    validate_config_schema(data)

                    self.controller.update_config(data)
                    return success_response()
                except ValidationError as e:
                    return error_response(e.message)
                except Exception as e:
                    logger.error(f"Error updating config: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    config = self.controller.get_config()
                    return jsonify(config)
                except Exception as e:
                    logger.error(f"Error getting config: {e}")
                    return error_response(str(e), 500)

        @self.app.route('/api/config/reload', methods=['POST'])
        def reload_config():
            """Reload configuration from file."""
            try:
                self.controller.reload_config()
                return success_response()
            except Exception as e:
                logger.error(f"Error reloading config: {e}")
                return error_response(str(e), 500)

        # ==================== LED MATRIX SETTINGS API ====================

        @self.app.route('/api/led-matrix', methods=['GET', 'POST'])
        def led_matrix_settings():
            """Get or update LED matrix settings."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    try:
                        validated_config = validate_led_matrix_config(data)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    self.controller.update_led_matrix_config(validated_config)
                    return success_response(
                        message='LED matrix settings updated. Restart required for some changes.'
                    )
                except ValidationError as e:
                    logger.warning(f"Validation error updating LED matrix: {e.message}")
                    return error_response(e.message)
                except Exception as e:
                    logger.error(f"Error updating LED matrix settings: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    config = self.controller.config.get('led_matrix', {})
                    return jsonify(config)
                except Exception as e:
                    logger.error(f"Error getting LED matrix settings: {e}")
                    return error_response(str(e), 500)

        # ==================== FRAMES API ====================

        @self.app.route('/api/frames', methods=['GET', 'POST'])
        def frames():
            """Get all frames or create a new frame."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    # Sanitize input before validation
                    sanitized_data = sanitize_frame_config(data)
                    
                    try:
                        validated_frame = validate_frame_data(sanitized_data)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    if self.controller.add_frame(validated_frame):
                        return success_response(message='Frame created')
                    return error_response('Failed to create frame', 500)
                except ValidationError as e:
                    logger.warning(f"Validation error creating frame: {e.message}")
                    return error_response(e.message)
                except Exception as e:
                    logger.error(f"Error creating frame: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    frames_list = self.controller.get_frames()
                    return jsonify({'frames': frames_list})
                except Exception as e:
                    logger.error(f"Error getting frames: {e}")
                    return error_response(str(e), 500)

        @self.app.route('/api/frames/<frame_id>', methods=['GET', 'PUT', 'DELETE'])
        def frame_detail(frame_id: str):
            """Get, update, or delete a specific frame."""
            if request.method == 'GET':
                try:
                    frame = self.controller.get_frame(frame_id)
                    if frame:
                        return jsonify(frame)
                    return error_response('Frame not found', 404)
                except Exception as e:
                    logger.error(f"Error getting frame: {e}")
                    return error_response(str(e), 500)
            
            elif request.method == 'PUT':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    # Sanitize input before validation
                    sanitized_data = sanitize_frame_config(data)
                    
                    try:
                        validated_frame = validate_frame_data(sanitized_data)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    if self.controller.update_frame(frame_id, validated_frame):
                        return success_response(message='Frame updated')
                    return error_response('Failed to update frame', 500)
                except ValidationError as e:
                    logger.warning(f"Validation error updating frame: {e.message}")
                    return error_response(e.message)
                except Exception as e:
                    logger.error(f"Error updating frame: {e}")
                    return error_response(str(e), 500)
            
            elif request.method == 'DELETE':
                try:
                    if self.controller.delete_frame(frame_id):
                        return success_response(message='Frame deleted')
                    return error_response('Failed to delete frame', 500)
                except Exception as e:
                    logger.error(f"Error deleting frame: {e}")
                    return error_response(str(e), 500)

        @self.app.route('/api/frames/reorder', methods=['POST'])
        def reorder_frames():
            """Reorder frames."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                frame_ids = data.get('order', [])
                if not isinstance(frame_ids, list):
                    return error_response('Order must be a list of frame IDs')
                
                if self.controller.reorder_frames(frame_ids):
                    return success_response(message='Frames reordered')
                return error_response('Failed to reorder frames', 500)
            except Exception as e:
                logger.error(f"Error reordering frames: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/frames/<frame_id>/toggle', methods=['POST'])
        def toggle_frame(frame_id: str):
            """Enable or disable a frame."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                enabled = data.get('enabled')
                if not isinstance(enabled, bool):
                    return error_response('Enabled must be a boolean')
                
                if self.controller.toggle_frame(frame_id, enabled):
                    return success_response({'enabled': enabled})
                return error_response('Failed to toggle frame', 500)
            except Exception as e:
                logger.error(f"Error toggling frame: {e}")
                return error_response(str(e), 500)

        # ==================== PRESETS API ====================

        @self.app.route('/api/presets')
        def get_presets():
            """List available display presets.
            
            Returns all configured display presets with their frame configurations.
            Presets define reusable display configurations that can be triggered
            via the /api/trigger endpoint.
            
            Example Response:
                {
                    "presets": [
                        {
                            "preset_name": "display_time_small_red",
                            "description": "Small time display, red color",
                            "frame_config": {
                                "type": "time",
                                "name": "Small Red Time",
                                "enabled": true,
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
                    ]
                }
            
            Returns:
                200: List of presets
                500: Server error
            """
            try:
                frames = self.controller.get_frames()
                presets = [
                    {
                        'preset_name': f['id'],
                        'description': f.get('name', ''),
                        'frame_config': f
                    }
                    for f in frames
                ]
                return jsonify({'presets': presets})
            except Exception as e:
                logger.error(f"Error getting presets: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/presets', methods=['POST'])
        def create_preset():
            """Create a new display preset.
            
            Presets can be created with either a direct frame configuration or
            wrapped in a 'frame_config' object. Both formats are supported for
            compatibility with different client implementations.
            
            Example Request (wrapped format):
                {
                    "preset_name": "display_time_small_red",
                    "description": "Small time display, red color",
                    "frame_config": {
                        "type": "time",
                        "name": "Small Red Time",
                        "enabled": true,
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
            
            Example Request (direct format):
                {
                    "type": "text",
                    "name": "Welcome Message",
                    "text": "Welcome!",
                    "enabled": true,
                    "duration": 15,
                    "colors": {
                        "background": "#000000",
                        "text": "#00FF00"
                    }
                }
            
            Supported Frame Types:
                - time: Clock display with customizable format
                - date: Date display
                - datetime: Combined date and time
                - temperature: Temperature reading display
                - humidity: Humidity reading display
                - text: Static text message
                - message: Scrolling message
                - data_visualization: Data charts and graphs
                - interactive: Interactive displays
                - template: Template-based displays
                - conditional: Conditional displays based on sensor data
                - multi_page: Multi-page displays
                - scrolling: Scrolling content
                - animated: Animated content
            
            Example Response:
                {
                    "success": true,
                    "message": "Preset created"
                }
            
            Returns:
                200: Preset created successfully
                400: Invalid preset data
                500: Server error
            """
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                # Validate against JSON schema first
                try:
                    validate_preset_schema(data)
                except ValidationError as e:
                    logger.warning(f"Schema validation error creating preset: {e.message}")
                    return error_response(e.message)
                
                # Extract frame_config if provided, otherwise use data as-is
                frame_data = data.get('frame_config', data)
                
                # Sanitize input before validation
                sanitized_data = sanitize_frame_config(frame_data)
                
                try:
                    validated_frame = validate_frame_data(sanitized_data)
                except ValidationError as e:
                    logger.warning(f"Validation error creating preset: {e.message}")
                    return error_response(e.message)
                
                if self.controller.add_frame(validated_frame):
                    return success_response(message='Preset created')
                return error_response('Failed to create preset', 500)
            except Exception as e:
                logger.error(f"Error creating preset: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/presets/<preset_name>', methods=['DELETE'])
        def delete_preset(preset_name: str):
            """Delete a display preset by name.
            
            Removes a preset from the system. The preset will no longer be
            available for playback or listing.
            
            Args:
                preset_name: Unique identifier/name of the preset to delete
            
            Example Request:
                DELETE /api/presets/display_time_small_red
            
            Example Response (success):
                {
                    "success": true,
                    "message": "Preset deleted"
                }
            
            Example Response (failure):
                {
                    "error": "Failed to delete preset"
                }
            
            Returns:
                200: Preset deleted successfully
                500: Preset not found or deletion failed
            """
            try:
                if self.controller.delete_frame(preset_name):
                    return success_response(message='Preset deleted')
                return error_response('Failed to delete preset', 500)
            except Exception as e:
                logger.error(f"Error deleting preset: {e}")
                return error_response(str(e), 500)

        # ==================== FONTS API ====================

        @self.app.route('/api/fonts', methods=['GET'])
        def get_fonts():
            """Get list of available fonts."""
            try:
                fonts = self.controller.get_fonts()
                return jsonify({'fonts': fonts})
            except Exception as e:
                logger.error(f"Error getting fonts: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/fonts/upload', methods=['POST'])
        def upload_font():
            """Upload a custom font."""
            try:
                if 'font' not in request.files:
                    return error_response('No font file provided')
                
                file = request.files['font']
                if not file.filename:
                    return error_response('No file selected')
                
                # Validate filename
                try:
                    validated_filename = validate_font_filename(file.filename)
                except ValidationError as e:
                    return error_response(e.message)
                
                # Use secure_filename as additional protection
                filename = secure_filename(validated_filename)
                if not filename:
                    return error_response('Invalid filename')
                
                font_data = file.read()
                
                # Validate file size (already limited by Flask but double-check)
                if len(font_data) > MAX_FONT_FILE_SIZE:
                    return error_response(
                        f'Font file too large. Maximum size is {MAX_FONT_FILE_SIZE // (1024*1024)}MB'
                    )
                
                font_path = self.controller.upload_font(filename, font_data)
                if font_path:
                    return success_response(
                        {'path': font_path},
                        message='Font uploaded successfully'
                    )
                return error_response('Failed to save font', 500)
            except ValidationError as e:
                logger.warning(f"Validation error uploading font: {e.message}")
                return error_response(e.message)
            except Exception as e:
                logger.error(f"Error uploading font: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/fonts/delete', methods=['POST'])
        def delete_font():
            """Delete a custom font."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                font_path = data.get('path')
                if not font_path:
                    return error_response('Font path required')
                
                if not isinstance(font_path, str):
                    return error_response('Font path must be a string')
                
                if self.controller.delete_font(font_path):
                    return success_response(message='Font deleted')
                return error_response(
                    'Failed to delete font. Only custom fonts can be deleted.', 500
                )
            except Exception as e:
                logger.error(f"Error deleting font: {e}")
                return error_response(str(e), 500)

        # ==================== NETWORK SETTINGS API ====================

        @self.app.route('/api/network', methods=['GET', 'POST'])
        def network_settings():
            """Get or update network settings."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    current = self.controller.config.get('network', {})
                    current.update(data)
                    self.controller.config.set('network', current)
                    self.controller.config.save()
                    return success_response(message='Network settings saved')
                except Exception as e:
                    logger.error(f"Error updating network settings: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    config = self.controller.config.get('network', {})
                    return jsonify(config)
                except Exception as e:
                    logger.error(f"Error getting network settings: {e}")
                    return error_response(str(e), 500)

        # ==================== TIME SETTINGS API ====================

        @self.app.route('/api/time', methods=['GET', 'POST'])
        def time_settings():
            """Get or update time settings."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    current = self.controller.config.get('time', {})
                    current.update(data)
                    self.controller.config.set('time', current)
                    self.controller.config.save()
                    return success_response(message='Time settings saved')
                except Exception as e:
                    logger.error(f"Error updating time settings: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    config = self.controller.config.get('time', {})
                    return jsonify(config)
                except Exception as e:
                    logger.error(f"Error getting time settings: {e}")
                    return error_response(str(e), 500)

        # ==================== CONTROL MODE API ====================

        @self.app.route('/api/control', methods=['GET', 'POST'])
        def control_settings():
            """Get or update control mode settings."""
            if request.method == 'POST':
                try:
                    data = request.get_json()
                    if not data:
                        return error_response('Request body required')
                    
                    mode = data.get('mode')
                    if not mode:
                        return error_response('Control mode required')
                    
                    # Validate control mode
                    if mode not in VALID_CONTROL_MODES:
                        return error_response(
                            f'Invalid control mode. Must be one of: {", ".join(VALID_CONTROL_MODES)}'
                        )
                    
                    # Validate sync_role if provided
                    sync_role = data.get('sync_role')
                    if sync_role:
                        if sync_role not in VALID_SYNC_ROLES:
                            return error_response(
                                f'Invalid sync role. Must be one of: {", ".join(VALID_SYNC_ROLES)}'
                            )
                    
                    # Validate external_source if provided
                    external_source = data.get('external_source')
                    if external_source:
                        if external_source not in VALID_EXTERNAL_SOURCES:
                            return error_response(
                                f'Invalid external source. Must be one of: {", ".join(VALID_EXTERNAL_SOURCES)}'
                            )
                    
                    # Set control mode
                    if self.controller.set_control_mode(
                        mode, 
                        sync_role=sync_role, 
                        external_source=external_source
                    ):
                        return success_response(
                            data=self.controller.get_control_settings(),
                            message='Control mode updated'
                        )
                    return error_response('Failed to set control mode', 500)
                except Exception as e:
                    logger.error(f"Error updating control settings: {e}")
                    return error_response(str(e), 500)
            else:
                try:
                    settings = self.controller.get_control_settings()
                    return jsonify(settings)
                except Exception as e:
                    logger.error(f"Error getting control settings: {e}")
                    return error_response(str(e), 500)

        # ==================== TIMELINE/SEQUENCER API ====================

        @self.app.route('/api/timelines', methods=['GET'])
        def get_timelines():
            """Get all timelines."""
            try:
                timelines = self.controller.get_timelines()
                return jsonify({'timelines': timelines})
            except Exception as e:
                logger.error(f"Error getting timelines: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/timelines', methods=['POST'])
        def create_timeline():
            """Create a new timeline."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                timeline_id = self.controller.add_timeline(data)
                if timeline_id:
                    return success_response({'timeline_id': timeline_id}, 'Timeline created')
                return error_response('Failed to create timeline', 500)
            except Exception as e:
                logger.error(f"Error creating timeline: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/timelines/<timeline_id>', methods=['GET'])
        def get_timeline(timeline_id: str):
            """Get a specific timeline."""
            try:
                timeline = self.controller.get_timeline(timeline_id)
                if timeline:
                    return jsonify(timeline)
                return error_response('Timeline not found', 404)
            except Exception as e:
                logger.error(f"Error getting timeline: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/timelines/<timeline_id>', methods=['PUT'])
        def update_timeline(timeline_id: str):
            """Update an existing timeline."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                if self.controller.update_timeline(timeline_id, data):
                    return success_response(message='Timeline updated')
                return error_response('Timeline not found or update failed', 404)
            except Exception as e:
                logger.error(f"Error updating timeline: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/timelines/<timeline_id>', methods=['DELETE'])
        def delete_timeline(timeline_id: str):
            """Delete a timeline."""
            try:
                if self.controller.delete_timeline(timeline_id):
                    return success_response(message='Timeline deleted')
                return error_response('Timeline not found', 404)
            except Exception as e:
                logger.error(f"Error deleting timeline: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/timelines/<timeline_id>/activate', methods=['POST'])
        def activate_timeline(timeline_id: str):
            """Activate a timeline."""
            try:
                if self.controller.activate_timeline(timeline_id):
                    return success_response(message='Timeline activated')
                return error_response('Timeline not found', 404)
            except Exception as e:
                logger.error(f"Error activating timeline: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/timelines/<timeline_id>/deactivate', methods=['POST'])
        def deactivate_timeline(timeline_id: str):
            """Deactivate a timeline."""
            try:
                if self.controller.deactivate_timeline(timeline_id):
                    return success_response(message='Timeline deactivated')
                return error_response('Timeline not found', 404)
            except Exception as e:
                logger.error(f"Error deactivating timeline: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/sequencer/status', methods=['GET'])
        def get_sequencer_status():
            """Get sequencer status."""
            try:
                status = self.controller.get_sequencer_status()
                return jsonify(status)
            except Exception as e:
                logger.error(f"Error getting sequencer status: {e}")
                return error_response(str(e), 500)

        # ==================== CHANNEL GROUP API ====================

        @self.app.route('/api/channel-groups', methods=['GET'])
        def get_channel_groups():
            """Get all channel groups."""
            try:
                groups = self.controller.get_channel_groups()
                return jsonify({'channel_groups': groups})
            except Exception as e:
                logger.error(f"Error getting channel groups: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/channel-groups', methods=['POST'])
        def create_channel_group():
            """Create a new channel group."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                group_id = self.controller.add_channel_group(data)
                if group_id:
                    return success_response({'group_id': group_id}, 'Channel group created')
                return error_response('Failed to create channel group', 500)
            except Exception as e:
                logger.error(f"Error creating channel group: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/channel-groups/<group_id>', methods=['GET'])
        def get_channel_group(group_id: str):
            """Get a specific channel group."""
            try:
                group = self.controller.get_channel_group(group_id)
                if group:
                    return jsonify(group)
                return error_response('Channel group not found', 404)
            except Exception as e:
                logger.error(f"Error getting channel group: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/channel-groups/<group_id>', methods=['PUT'])
        def update_channel_group(group_id: str):
            """Update an existing channel group."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                if self.controller.update_channel_group(group_id, data):
                    return success_response(message='Channel group updated')
                return error_response('Channel group not found or update failed', 404)
            except Exception as e:
                logger.error(f"Error updating channel group: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/channel-groups/<group_id>', methods=['DELETE'])
        def delete_channel_group(group_id: str):
            """Delete a channel group."""
            try:
                if self.controller.delete_channel_group(group_id):
                    return success_response(message='Channel group deleted')
                return error_response('Channel group not found', 404)
            except Exception as e:
                logger.error(f"Error deleting channel group: {e}")
                return error_response(str(e), 500)

        # ==================== PRESET LINK API ====================

        @self.app.route('/api/preset-links', methods=['GET'])
        def get_preset_links():
            """Get all preset links."""
            try:
                links = self.controller.get_preset_links()
                return jsonify({'preset_links': links})
            except Exception as e:
                logger.error(f"Error getting preset links: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/preset-links', methods=['POST'])
        def create_preset_link():
            """Create a new preset link."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                link_id = self.controller.add_preset_link(data)
                if link_id:
                    return success_response({'link_id': link_id}, 'Preset link created')
                return error_response('Failed to create preset link', 500)
            except Exception as e:
                logger.error(f"Error creating preset link: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/preset-links/<link_id>', methods=['GET'])
        def get_preset_link(link_id: str):
            """Get a specific preset link."""
            try:
                link = self.controller.get_preset_link(link_id)
                if link:
                    return jsonify(link)
                return error_response('Preset link not found', 404)
            except Exception as e:
                logger.error(f"Error getting preset link: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/preset-links/<link_id>', methods=['PUT'])
        def update_preset_link(link_id: str):
            """Update an existing preset link."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                if self.controller.update_preset_link(link_id, data):
                    return success_response(message='Preset link updated')
                return error_response('Preset link not found or update failed', 404)
            except Exception as e:
                logger.error(f"Error updating preset link: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/preset-links/<link_id>', methods=['DELETE'])
        def delete_preset_link(link_id: str):
            """Delete a preset link."""
            try:
                if self.controller.delete_preset_link(link_id):
                    return success_response(message='Preset link deleted')
                return error_response('Preset link not found', 404)
            except Exception as e:
                logger.error(f"Error deleting preset link: {e}")
                return error_response(str(e), 500)

        # ==================== CONDITIONAL LOGIC API ====================

        @self.app.route('/api/conditional-rules', methods=['GET'])
        def get_conditional_rules():
            """Get all conditional logic rules."""
            try:
                rules = self.controller.get_conditional_rules()
                return jsonify({'conditional_rules': rules})
            except Exception as e:
                logger.error(f"Error getting conditional rules: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/conditional-rules', methods=['POST'])
        def create_conditional_rule():
            """Create a new conditional rule."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                rule_id = self.controller.add_conditional_rule(data)
                if rule_id:
                    return success_response({'rule_id': rule_id}, 'Conditional rule created')
                return error_response('Failed to create conditional rule', 500)
            except Exception as e:
                logger.error(f"Error creating conditional rule: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/conditional-rules/<rule_id>', methods=['GET'])
        def get_conditional_rule(rule_id: str):
            """Get a specific conditional rule."""
            try:
                rule = self.controller.get_conditional_rule(rule_id)
                if rule:
                    return jsonify(rule)
                return error_response('Conditional rule not found', 404)
            except Exception as e:
                logger.error(f"Error getting conditional rule: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/conditional-rules/<rule_id>', methods=['PUT'])
        def update_conditional_rule(rule_id: str):
            """Update an existing conditional rule."""
            try:
                data = request.get_json()
                if not data:
                    return error_response('Request body required')
                
                if self.controller.update_conditional_rule(rule_id, data):
                    return success_response(message='Conditional rule updated')
                return error_response('Conditional rule not found or update failed', 404)
            except Exception as e:
                logger.error(f"Error updating conditional rule: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/conditional-rules/<rule_id>', methods=['DELETE'])
        def delete_conditional_rule(rule_id: str):
            """Delete a conditional rule."""
            try:
                if self.controller.delete_conditional_rule(rule_id):
                    return success_response(message='Conditional rule deleted')
                return error_response('Conditional rule not found', 404)
            except Exception as e:
                logger.error(f"Error deleting conditional rule: {e}")
                return error_response(str(e), 500)

        # ==================== SYSTEM API ====================

        @self.app.route('/api/system/restart', methods=['POST'])
        def restart_service():
            """Request a service restart (needs to be handled by systemd or similar)."""
            try:
                # This is a placeholder - actual restart would need to be handled
                # by the systemd service or similar mechanism
                return success_response(
                    message='Restart requested. Please restart the service manually or wait for automatic restart.'
                )
            except Exception as e:
                logger.error(f"Error requesting restart: {e}")
                return error_response(str(e), 500)

        @self.app.route('/api/system/info')
        def system_info():
            """Get system information."""
            try:
                import platform
                info = {
                    'version': '1.0.0',
                    'platform': platform.platform(),
                    'python_version': platform.python_version(),
                    'hostname': platform.node()
                }
                return jsonify(info)
            except Exception as e:
                logger.error(f"Error getting system info: {e}")
                return error_response(str(e), 500)

        # ==================== TRIGGER API ====================

        @self.app.route('/api/trigger', methods=['POST'])
        def trigger_action():
            """Trigger a specific action or schedule playback."""
            try:
                data = request.get_json()
                if data is None:
                    return error_response('Request body required')
                
                validate_trigger_schema(data)

                action = data.get('action')
                
                if action == 'play_preset':
                    name = data.get('name')
                    if not name:
                        return error_response('Preset name required')
                    if self.controller.play_preset(name):
                        return success_response({'action': 'play_preset', 'name': name})
                    return error_response('Preset not found', 404)
                
                elif action == 'set_mode':
                    mode = data.get('mode')
                    if not mode:
                        return error_response('Mode required')
                    
                    available_modes = set(self.controller.modes.keys())
                    try:
                        validated_mode = validate_mode(mode, available_modes)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    self.controller.set_mode(validated_mode)
                    return success_response({'action': 'set_mode', 'mode': validated_mode})
                
                elif action == 'set_brightness':
                    brightness_val = data.get('value')
                    if brightness_val is None:
                        return error_response('Brightness value required')
                    
                    try:
                        validated_brightness = validate_brightness(brightness_val)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    self.controller.set_brightness(validated_brightness)
                    return success_response({'action': 'set_brightness', 'value': validated_brightness})
                
                elif action == 'show_message':
                    message = data.get('message')
                    if message is None:
                        return error_response('Message required')
                    
                    try:
                        validated_message = validate_message(message)
                    except ValidationError as e:
                        return error_response(e.message)
                    
                    self.controller.set_message(validated_message)
                    return success_response({'action': 'show_message', 'message': validated_message})
                
                else:
                    return error_response(f'Unknown action: {action}')
            except ValidationError as e:
                return error_response(e.message)
            except Exception as e:
                logger.error(f"Error triggering action: {e}")
                return error_response(str(e), 500)

    def run(self) -> None:
        """Run the web server.
        
        Note: This uses Flask's built-in development server which is suitable
        for development and light use. For production deployment, use a WSGI
        server like Gunicorn or uWSGI for better performance and reliability.
        
        Example with Gunicorn:
            gunicorn -w 4 -b 0.0.0.0:5000 lumigrid.wsgi:app
        
        Example with uWSGI:
            uwsgi --http 0.0.0.0:5000 --module lumigrid.wsgi:app --master
        """
        host = self.config.get('host', '0.0.0.0')
        port = self.config.get('port', 5000)
        debug = self.config.get('debug', False)
        
        logger.info(f"Starting web server on {host}:{port}")
        if not debug:
            logger.warning("Running with Flask development server. "
                         "For production, use Gunicorn or uWSGI.")
        self.app.run(host=host, port=port, debug=debug, threaded=True)
