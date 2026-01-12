"""Tests for the web API."""

import pytest
import json
from unittest.mock import MagicMock, patch


@pytest.fixture
def mock_controller():
    """Create a mock application controller."""
    controller = MagicMock()
    controller.modes = {
        'clock': MagicMock(),
        'temperature': MagicMock(),
        'message': MagicMock(),
        'frames': MagicMock(),
        'slideshow': MagicMock()
    }
    controller.get_current_mode.return_value = 'clock'
    controller.get_brightness.return_value = 50
    controller.get_status.return_value = {
        'mode': 'clock',
        'brightness': 50,
        'temperature': 25.0,
        'running': True
    }
    controller.get_frames.return_value = []
    controller.get_fonts.return_value = []
    controller.config = MagicMock()
    controller.config.get.return_value = {}
    return controller


@pytest.fixture
def client(mock_controller):
    """Create a test client for the web interface."""
    from lumigrid.web.app import WebInterface
    
    web_interface = WebInterface({}, mock_controller)
    web_interface.app.testing = True
    with web_interface.app.test_client() as client:
        yield client


class TestStatusAPI:
    """Tests for the status API endpoint."""
    
    def test_get_status(self, client, mock_controller):
        """Test getting status."""
        response = client.get('/api/status')
        assert response.status_code == 200
        data = response.get_json()
        assert 'id' in data
        assert 'type' in data
        assert 'ip' in data
        assert 'state' in data
        assert 'uptime' in data
        assert 'metrics' in data


class TestModeAPI:
    """Tests for the mode API endpoint."""
    
    def test_get_mode(self, client, mock_controller):
        """Test getting current mode."""
        response = client.get('/api/mode')
        assert response.status_code == 200
        data = response.get_json()
        assert data['mode'] == 'clock'
    
    def test_set_valid_mode(self, client, mock_controller):
        """Test setting a valid mode."""
        response = client.post('/api/mode',
                              data=json.dumps({'mode': 'temperature'}),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['mode'] == 'temperature'
    
    def test_set_invalid_mode(self, client, mock_controller):
        """Test setting an invalid mode."""
        response = client.post('/api/mode',
                              data=json.dumps({'mode': 'invalid_mode'}),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for invalid mode
        data = response.get_json()
        assert 'error' in data
    
    def test_set_mode_missing_body(self, client):
        """Test setting mode without request body."""
        response = client.post('/api/mode',
                              content_type='application/json')
        # Flask will return 500 for missing JSON body or 400 depending on handling
        assert response.status_code in [400, 500]
        data = response.get_json()
        assert 'error' in data
    
    def test_set_mode_missing_mode(self, client):
        """Test setting mode without mode field."""
        response = client.post('/api/mode',
                              data=json.dumps({}),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for missing mode
        data = response.get_json()
        assert 'error' in data


class TestBrightnessAPI:
    """Tests for the brightness API endpoint."""
    
    def test_get_brightness(self, client, mock_controller):
        """Test getting brightness."""
        response = client.get('/api/brightness')
        assert response.status_code == 200
        data = response.get_json()
        assert data['brightness'] == 50
    
    def test_set_valid_brightness(self, client, mock_controller):
        """Test setting valid brightness."""
        response = client.post('/api/brightness',
                              data=json.dumps({'brightness': 75}),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['brightness'] == 75
    
    def test_set_brightness_out_of_range(self, client):
        """Test setting brightness out of range."""
        response = client.post('/api/brightness',
                              data=json.dumps({'brightness': 150}),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for out of range
        data = response.get_json()
        assert 'error' in data
        assert 'between' in data['error']
    
    def test_set_brightness_invalid_type(self, client):
        """Test setting brightness with invalid type."""
        response = client.post('/api/brightness',
                              data=json.dumps({'brightness': 'invalid'}),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for invalid type
        data = response.get_json()
        assert 'error' in data
    
    def test_set_brightness_negative(self, client):
        """Test setting negative brightness."""
        response = client.post('/api/brightness',
                              data=json.dumps({'brightness': -10}),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for negative value
        data = response.get_json()
        assert 'error' in data


class TestMessageAPI:
    """Tests for the message API endpoint."""
    
    def test_set_valid_message(self, client, mock_controller):
        """Test setting a valid message."""
        response = client.post('/api/message',
                              data=json.dumps({'message': 'Hello World'}),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['message'] == 'Hello World'
    
    def test_set_empty_message(self, client, mock_controller):
        """Test setting an empty message."""
        response = client.post('/api/message',
                              data=json.dumps({'message': ''}),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
    
    def test_set_message_too_long(self, client):
        """Test setting a message that's too long."""
        long_message = 'x' * 501
        response = client.post('/api/message',
                              data=json.dumps({'message': long_message}),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for message too long
        data = response.get_json()
        assert 'error' in data


class TestFramesAPI:
    """Tests for the frames API endpoints."""
    
    def test_get_frames(self, client, mock_controller):
        """Test getting frames."""
        response = client.get('/api/frames')
        assert response.status_code == 200
        data = response.get_json()
        assert 'frames' in data
    
    def test_create_valid_frame(self, client, mock_controller):
        """Test creating a valid frame."""
        mock_controller.add_frame.return_value = True
        frame_data = {
            'type': 'time',
            'name': 'Test Frame',
            'enabled': True,
            'duration': 5
        }
        response = client.post('/api/frames',
                              data=json.dumps(frame_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
    
    def test_create_frame_invalid_type(self, client):
        """Test creating frame with invalid type."""
        frame_data = {'type': 'invalid_type'}
        response = client.post('/api/frames',
                              data=json.dumps(frame_data),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for invalid frame type
        data = response.get_json()
        assert 'error' in data
    
    def test_create_frame_invalid_duration(self, client):
        """Test creating frame with invalid duration."""
        frame_data = {'type': 'time', 'duration': 0}
        response = client.post('/api/frames',
                              data=json.dumps(frame_data),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for invalid duration
        data = response.get_json()
        assert 'error' in data


class TestLedMatrixAPI:
    """Tests for the LED matrix settings API."""
    
    def test_get_led_matrix_settings(self, client, mock_controller):
        """Test getting LED matrix settings."""
        response = client.get('/api/led-matrix')
        assert response.status_code == 200
    
    def test_update_valid_led_matrix(self, client, mock_controller):
        """Test updating LED matrix with valid config."""
        config = {
            'rows': 32,
            'cols': 64,
            'brightness': 50
        }
        response = client.post('/api/led-matrix',
                              data=json.dumps(config),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
    
    def test_update_led_matrix_invalid_brightness(self, client):
        """Test updating LED matrix with invalid brightness."""
        config = {'brightness': 200}
        response = client.post('/api/led-matrix',
                              data=json.dumps(config),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for invalid brightness
        data = response.get_json()
        assert 'error' in data


class TestFontsAPI:
    """Tests for the fonts API endpoints."""
    
    def test_get_fonts(self, client, mock_controller):
        """Test getting fonts list."""
        response = client.get('/api/fonts')
        assert response.status_code == 200
        data = response.get_json()
        assert 'fonts' in data
    
    def test_upload_font_no_file(self, client):
        """Test uploading font without file."""
        response = client.post('/api/fonts/upload')
        assert response.status_code == 400  # Bad request for missing file
        data = response.get_json()
        assert 'error' in data
    
    def test_delete_font_missing_path(self, client):
        """Test deleting font without path."""
        response = client.post('/api/fonts/delete',
                              data=json.dumps({}),
                              content_type='application/json')
        assert response.status_code == 400  # Bad request for missing path
        data = response.get_json()
        assert 'error' in data


class TestSystemAPI:
    """Tests for the system API endpoints."""
    
    def test_system_info(self, client):
        """Test getting system info."""
        response = client.get('/api/system/info')
        assert response.status_code == 200
        data = response.get_json()
        assert 'version' in data
        assert 'platform' in data
    
    def test_restart_request(self, client):
        """Test restart request."""
        response = client.post('/api/system/restart')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True


class TestRateLimiting:
    """Tests for rate limiting functionality."""
    
    def test_rate_limiter_initialization_enabled(self, mock_controller):
        """Test rate limiter is initialized when enabled."""
        from lumigrid.web.app import WebInterface
        
        config = {
            'rate_limiting': {
                'enabled': True,
                'default_limits': ['100 per hour'],
                'storage_uri': 'memory://'
            }
        }
        web_interface = WebInterface(config, mock_controller)
        assert web_interface.limiter is not None
    
    def test_rate_limiter_initialization_disabled(self, mock_controller):
        """Test rate limiter is None when disabled."""
        from lumigrid.web.app import WebInterface
        
        config = {
            'rate_limiting': {
                'enabled': False
            }
        }
        web_interface = WebInterface(config, mock_controller)
        assert web_interface.limiter is None
    
    def test_rate_limiter_default_config(self, mock_controller):
        """Test rate limiter uses default config when not specified."""
        from lumigrid.web.app import WebInterface
        
        config = {}  # No rate_limiting config
        web_interface = WebInterface(config, mock_controller)
        # Should use default enabled=True
        assert web_interface.limiter is not None


class TestInputSanitization:
    """Tests for input sanitization in frame endpoints."""
    
    def test_frame_creation_sanitizes_xss(self, client, mock_controller):
        """Test that XSS attempts are sanitized in frame creation."""
        mock_controller.add_frame.return_value = True
        frame_data = {
            'type': 'text',
            'name': '<script>alert("xss")</script>',
            'text': '<img src=x onerror=alert(1)>',
            'enabled': True,
            'duration': 5
        }
        response = client.post('/api/frames',
                              data=json.dumps(frame_data),
                              content_type='application/json')
        assert response.status_code == 200
        # Verify that the controller received sanitized data
        call_args = mock_controller.add_frame.call_args
        if call_args:
            frame = call_args[0][0]
            if 'name' in frame:
                assert '<' not in frame['name']
                assert '>' not in frame['name']


class TestSensorUpdateAPI:
    """Tests for sensor update API endpoints."""
    
    def test_needs_sensor_data(self, client, mock_controller):
        """Test getting sensor data needs."""
        mock_controller.config.get.return_value = 'display-node-1'
        response = client.get('/api/discover/needs_sensor_data')
        assert response.status_code == 200
        data = response.get_json()
        assert data['needs_data'] is True
        assert 'types' in data
        assert 'temperature' in data['types']
        assert 'humidity' in data['types']
        assert 'motion' in data['types']
        assert 'light' in data['types']
        assert data['node_type'] == 'display'
    
    def test_sensor_update_valid_data(self, client, mock_controller):
        """Test receiving valid sensor data."""
        sensor_data = {
            'node_id': 'sensor-node-1',
            'readings': {
                'temperature': 25.5,
                'humidity': 60.0
            },
            'timestamp': 1234567890
        }
        response = client.post('/api/sensor_update',
                              data=json.dumps(sensor_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['message'] == 'Sensor data received'
        mock_controller.update_sensor_data.assert_called_once_with(sensor_data)
    
    def test_sensor_update_without_timestamp(self, client, mock_controller):
        """Test receiving sensor data without timestamp."""
        sensor_data = {
            'node_id': 'sensor-node-1',
            'readings': {
                'temperature': 25.5
            }
        }
        response = client.post('/api/sensor_update',
                              data=json.dumps(sensor_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        mock_controller.update_sensor_data.assert_called_once()

    def test_sensor_update_missing_body(self, client):
        """Test sensor update without request body."""
        response = client.post('/api/sensor_update',
                              content_type='application/json')
        assert response.status_code in [400, 500]
        data = response.get_json()
        assert 'error' in data

    def test_sensor_update_missing_node_id(self, client):
        """Test sensor update without node_id."""
        sensor_data = {
            'readings': {
                'temperature': 25.5
            }
        }
        response = client.post('/api/sensor_update',
                              data=json.dumps(sensor_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'node_id' in data['error'].lower()

    def test_sensor_update_missing_readings(self, client):
        """Test sensor update without readings."""
        sensor_data = {
            'node_id': 'sensor-node-1'
        }
        response = client.post('/api/sensor_update',
                              data=json.dumps(sensor_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'readings' in data['error'].lower()

    def test_sensor_update_multiple_sensors(self, client, mock_controller):
        """Test receiving data from multiple sensor nodes."""
        sensor_data_1 = {
            'node_id': 'sensor-node-1',
            'readings': {'temperature': 25.5}
        }
        sensor_data_2 = {
            'node_id': 'sensor-node-2',
            'readings': {'humidity': 60.0}
        }

        response1 = client.post('/api/sensor_update',
                               data=json.dumps(sensor_data_1),
                               content_type='application/json')
        assert response1.status_code == 200

        response2 = client.post('/api/sensor_update',
                               data=json.dumps(sensor_data_2),
                               content_type='application/json')
        assert response2.status_code == 200

        assert mock_controller.update_sensor_data.call_count == 2
    
    def test_sensor_update_various_reading_types(self, client, mock_controller):
        """Test sensor update with various reading types."""
        sensor_data = {
            'node_id': 'sensor-node-1',
            'readings': {
                'temperature': 25.5,
                'humidity': 60.0,
                'motion': True,
                'light': 500
            }
        }
        response = client.post('/api/sensor_update',
                              data=json.dumps(sensor_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True


class TestTriggerAPI:
    """Tests for trigger API endpoint."""
    
    def test_trigger_play_preset_success(self, client, mock_controller):
        """Test triggering play_preset action successfully."""
        mock_controller.play_preset.return_value = True
        trigger_data = {
            'action': 'play_preset',
            'name': 'Test Preset'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['action'] == 'play_preset'
        assert data['name'] == 'Test Preset'
        mock_controller.play_preset.assert_called_once_with('Test Preset')
    
    def test_trigger_play_preset_not_found(self, client, mock_controller):
        """Test triggering play_preset with non-existent preset."""
        mock_controller.play_preset.return_value = False
        trigger_data = {
            'action': 'play_preset',
            'name': 'NonExistent'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 404
        data = response.get_json()
        assert 'error' in data
        assert 'not found' in data['error'].lower()
    
    def test_trigger_play_preset_missing_name(self, client):
        """Test triggering play_preset without name."""
        trigger_data = {
            'action': 'play_preset'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'name required' in data['error'].lower()
    
    def test_trigger_set_mode_success(self, client, mock_controller):
        """Test triggering set_mode action successfully."""
        trigger_data = {
            'action': 'set_mode',
            'mode': 'clock'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['action'] == 'set_mode'
        assert data['mode'] == 'clock'
        mock_controller.set_mode.assert_called_once_with('clock')
    
    def test_trigger_set_mode_invalid(self, client, mock_controller):
        """Test triggering set_mode with invalid mode."""
        trigger_data = {
            'action': 'set_mode',
            'mode': 'invalid_mode'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_trigger_set_mode_missing_mode(self, client):
        """Test triggering set_mode without mode."""
        trigger_data = {
            'action': 'set_mode'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'mode required' in data['error'].lower()
    
    def test_trigger_set_brightness_success(self, client, mock_controller):
        """Test triggering set_brightness action successfully."""
        trigger_data = {
            'action': 'set_brightness',
            'value': 75
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['action'] == 'set_brightness'
        assert data['value'] == 75
        mock_controller.set_brightness.assert_called_once_with(75)
    
    def test_trigger_set_brightness_invalid(self, client):
        """Test triggering set_brightness with invalid value."""
        trigger_data = {
            'action': 'set_brightness',
            'value': 150
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_trigger_set_brightness_missing_value(self, client):
        """Test triggering set_brightness without value."""
        trigger_data = {
            'action': 'set_brightness'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'value required' in data['error'].lower()
    
    def test_trigger_show_message_success(self, client, mock_controller):
        """Test triggering show_message action successfully."""
        trigger_data = {
            'action': 'show_message',
            'message': 'Hello World'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['action'] == 'show_message'
        assert data['message'] == 'Hello World'
        mock_controller.set_message.assert_called_once_with('Hello World')
    
    def test_trigger_show_message_empty(self, client, mock_controller):
        """Test triggering show_message with empty message."""
        trigger_data = {
            'action': 'show_message',
            'message': ''
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
    
    def test_trigger_show_message_missing(self, client):
        """Test triggering show_message without message."""
        trigger_data = {
            'action': 'show_message'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'message required' in data['error'].lower()
    
    def test_trigger_unknown_action(self, client):
        """Test triggering unknown action."""
        trigger_data = {
            'action': 'unknown_action'
        }
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'is not one of' in data['error']

    def test_trigger_missing_action(self, client):
        """Test triggering without action field."""
        trigger_data = {}
        response = client.post('/api/trigger',
                              data=json.dumps(trigger_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert "'action' is a required property" in data['error']
    
    def test_trigger_missing_body(self, client):
        """Test triggering without request body."""
        response = client.post('/api/trigger',
                              content_type='application/json')
        assert response.status_code in [400, 500]
        data = response.get_json()
        assert 'error' in data


class TestControlModeAPI:
    """Tests for the control mode API endpoint."""
    
    def test_get_control_settings(self, client, mock_controller):
        """Test getting control mode settings."""
        mock_controller.get_control_settings.return_value = {
            'mode': 'independent',
            'sync_role': 'slave',
            'external_source': 'mqtt'
        }
        
        response = client.get('/api/control')
        assert response.status_code == 200
        data = response.get_json()
        assert 'mode' in data
        assert data['mode'] == 'independent'
        assert data['sync_role'] == 'slave'
        assert data['external_source'] == 'mqtt'
    
    def test_set_control_mode_independent(self, client, mock_controller):
        """Test setting control mode to independent."""
        mock_controller.set_control_mode.return_value = True
        mock_controller.get_control_settings.return_value = {
            'mode': 'independent',
            'sync_role': 'slave',
            'external_source': 'mqtt'
        }
        
        response = client.post('/api/control',
                              data=json.dumps({'mode': 'independent'}),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        mock_controller.set_control_mode.assert_called_once()
    
    def test_set_control_mode_sync_with_role(self, client, mock_controller):
        """Test setting control mode to sync with master role."""
        mock_controller.set_control_mode.return_value = True
        mock_controller.get_control_settings.return_value = {
            'mode': 'sync',
            'sync_role': 'master',
            'external_source': 'mqtt'
        }
        
        response = client.post('/api/control',
                              data=json.dumps({
                                  'mode': 'sync',
                                  'sync_role': 'master'
                              }),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        mock_controller.set_control_mode.assert_called_once()
    
    def test_set_control_mode_external_with_source(self, client, mock_controller):
        """Test setting control mode to external with artnet source."""
        mock_controller.set_control_mode.return_value = True
        mock_controller.get_control_settings.return_value = {
            'mode': 'external',
            'sync_role': 'slave',
            'external_source': 'artnet'
        }
        
        response = client.post('/api/control',
                              data=json.dumps({
                                  'mode': 'external',
                                  'external_source': 'artnet'
                              }),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        mock_controller.set_control_mode.assert_called_once()
    
    def test_set_invalid_control_mode(self, client, mock_controller):
        """Test setting an invalid control mode."""
        response = client.post('/api/control',
                              data=json.dumps({'mode': 'invalid_mode'}),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'Invalid control mode' in data['error']
    
    def test_set_invalid_sync_role(self, client, mock_controller):
        """Test setting an invalid sync role."""
        response = client.post('/api/control',
                              data=json.dumps({
                                  'mode': 'sync',
                                  'sync_role': 'invalid_role'
                              }),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'Invalid sync role' in data['error']
    
    def test_set_invalid_external_source(self, client, mock_controller):
        """Test setting an invalid external source."""
        response = client.post('/api/control',
                              data=json.dumps({
                                  'mode': 'external',
                                  'external_source': 'invalid_source'
                              }),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'Invalid external source' in data['error']
    
    def test_set_control_mode_missing_body(self, client):
        """Test setting control mode without request body."""
        response = client.post('/api/control',
                              content_type='application/json')
        assert response.status_code in [400, 500]
        data = response.get_json()
        assert 'error' in data
    
    def test_set_control_mode_missing_mode(self, client):
        """Test setting control mode without mode field."""
        response = client.post('/api/control',
                              data=json.dumps({'other_field': 'value'}),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'mode required' in data['error'].lower()


class TestConfigAPI:
    """Tests for the configuration API endpoint."""
    
    def test_get_config(self, client, mock_controller):
        """Test getting device configuration."""
        mock_config = {
            'led_matrix': {'rows': 32, 'cols': 64, 'brightness': 50},
            'network': {'hostname': 'lumigrid-01', 'mdns_enabled': True},
            'control': {'mode': 'independent'},
            'time': {'timezone': 'UTC', 'ntp_enabled': True}
        }
        mock_controller.get_config.return_value = mock_config
        
        response = client.get('/api/config')
        assert response.status_code == 200
        data = response.get_json()
        assert 'led_matrix' in data
        assert 'network' in data
        assert 'control' in data
        assert data['led_matrix']['brightness'] == 50
    
    def test_update_config(self, client, mock_controller):
        """Test updating device configuration."""
        config_update = {
            'network': {'hostname': 'lumigrid-new'}
        }

        response = client.post('/api/config',
                              data=json.dumps(config_update),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        mock_controller.update_config.assert_called_once_with(config_update)

    def test_update_config_empty_body(self, client, mock_controller):
        """Test updating config with empty body."""
        response = client.post('/api/config',
                              data=json.dumps({}),
                              content_type='application/json')
        assert response.status_code == 200
        mock_controller.update_config.assert_called_once_with({})

    def test_update_config_validation_error(self, client, mock_controller):
        """Test updating config with invalid data."""
        config_update = {'control_mode': 'invalid'}

        response = client.post('/api/config',
                              data=json.dumps(config_update),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data

    def test_update_config_missing_body(self, client):
        """Test updating config without request body."""
        response = client.post('/api/config',
                              content_type='application/json')
        assert response.status_code in [400, 500]
        data = response.get_json()
        assert 'error' in data


class TestPresetsAPI:
    """Tests for the presets API endpoints."""
    
    def test_get_presets(self, client, mock_controller):
        """Test getting all presets."""
        # Mock frames data
        mock_controller.get_frames.return_value = [
            {
                'id': 'frame1',
                'name': 'Test Frame 1',
                'type': 'time',
                'enabled': True,
                'duration': 5
            },
            {
                'id': 'frame2',
                'name': 'Test Frame 2',
                'type': 'text',
                'enabled': True,
                'duration': 10,
                'text': 'Hello'
            }
        ]
        
        response = client.get('/api/presets')
        assert response.status_code == 200
        data = response.get_json()
        assert 'presets' in data
        assert len(data['presets']) == 2
        
        # Verify structure of first preset
        preset1 = data['presets'][0]
        assert 'preset_name' in preset1
        assert 'description' in preset1
        assert 'frame_config' in preset1
        assert preset1['preset_name'] == 'frame1'
        assert preset1['description'] == 'Test Frame 1'
        assert preset1['frame_config']['id'] == 'frame1'
    
    def test_get_presets_empty(self, client, mock_controller):
        """Test getting presets when none exist."""
        mock_controller.get_frames.return_value = []
        
        response = client.get('/api/presets')
        assert response.status_code == 200
        data = response.get_json()
        assert 'presets' in data
        assert len(data['presets']) == 0
    
    def test_create_preset_with_frame_config(self, client, mock_controller):
        """Test creating a preset with frame_config wrapper."""
        mock_controller.add_frame.return_value = True
        preset_data = {
            'frame_config': {
                'type': 'time',
                'name': 'New Preset',
                'enabled': True,
                'duration': 5
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['message'] == 'Preset created'
        mock_controller.add_frame.assert_called_once()
    
    def test_create_preset_without_wrapper(self, client, mock_controller):
        """Test creating a preset with direct frame data."""
        mock_controller.add_frame.return_value = True
        preset_data = {
            'type': 'text',
            'name': 'Direct Preset',
            'text': 'Test',
            'enabled': True,
            'duration': 5
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['message'] == 'Preset created'
        mock_controller.add_frame.assert_called_once()
    
    def test_create_preset_invalid_type(self, client):
        """Test creating preset with invalid type."""
        preset_data = {
            'type': 'invalid_type',
            'name': 'Bad Preset'
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_missing_body(self, client):
        """Test creating preset without request body."""
        response = client.post('/api/presets',
                              content_type='application/json')
        assert response.status_code in [400, 500]
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_controller_failure(self, client, mock_controller):
        """Test creating preset when controller fails."""
        mock_controller.add_frame.return_value = False
        preset_data = {
            'type': 'time',
            'name': 'Test Preset',
            'enabled': True,
            'duration': 5
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 500
        data = response.get_json()
        assert 'error' in data
        assert 'Failed to create preset' in data['error']
    
    def test_delete_preset_success(self, client, mock_controller):
        """Test deleting a preset successfully."""
        mock_controller.delete_frame.return_value = True
        
        response = client.delete('/api/presets/frame1')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['message'] == 'Preset deleted'
        mock_controller.delete_frame.assert_called_once_with('frame1')
    
    def test_delete_preset_not_found(self, client, mock_controller):
        """Test deleting a non-existent preset."""
        mock_controller.delete_frame.return_value = False
        
        response = client.delete('/api/presets/nonexistent')
        assert response.status_code == 500
        data = response.get_json()
        assert 'error' in data
        assert 'Failed to delete preset' in data['error']
        mock_controller.delete_frame.assert_called_once_with('nonexistent')
    
    def test_create_preset_full_schema_example(self, client, mock_controller):
        """Test creating a preset with full schema as per LumiGrid specification.
        
        Example from preset-display.schema.json:
        A time display preset with custom colors and font settings.
        """
        mock_controller.add_frame.return_value = True
        
        # Full preset example matching LumiGrid spec
        preset_data = {
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
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        assert data['message'] == 'Preset created'
        
        # Verify the frame was added with correct data
        mock_controller.add_frame.assert_called_once()
        call_args = mock_controller.add_frame.call_args[0][0]
        assert call_args['type'] == 'time'
        assert 'colors' in call_args
        assert call_args['colors']['text'] == '#FF0000'
    
    def test_create_preset_text_display(self, client, mock_controller):
        """Test creating a text display preset."""
        mock_controller.add_frame.return_value = True
        
        preset_data = {
            "preset_name": "welcome_message",
            "description": "Welcome message display",
            "frame_config": {
                "type": "text",
                "name": "Welcome",
                "text": "Welcome to LumiGrid!",
                "enabled": True,
                "duration": 15,
                "colors": {
                    "background": "#000000",
                    "text": "#00FF00"
                },
                "font": {
                    "size": 12
                },
                "scroll": True
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
    
    def test_create_preset_temperature_display(self, client, mock_controller):
        """Test creating a temperature display preset."""
        mock_controller.add_frame.return_value = True
        
        preset_data = {
            "preset_name": "temp_blue",
            "description": "Temperature in blue",
            "frame_config": {
                "type": "temperature",
                "name": "Blue Temperature",
                "enabled": True,
                "duration": 5,
                "unit": "celsius",
                "colors": {
                    "background": "#000000",
                    "text": "#0000FF"
                },
                "show_icon": True
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
    
    def test_create_preset_malformed_missing_type(self, client):
        """Test creating preset with missing required 'type' field."""
        preset_data = {
            "preset_name": "invalid_preset",
            "description": "Missing type field",
            "frame_config": {
                "name": "No Type Frame",
                "enabled": True,
                "duration": 10
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        # Should include schema example in error message
        assert 'Example valid preset' in data['error'] or 'type' in data['error'].lower()
    
    def test_create_preset_malformed_invalid_frame_type(self, client):
        """Test creating preset with invalid frame type."""
        preset_data = {
            "preset_name": "invalid_type_preset",
            "description": "Invalid frame type",
            "frame_config": {
                "type": "invalid_frame_type",
                "name": "Bad Type",
                "enabled": True,
                "duration": 10
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        # Should mention the validation error
        assert 'invalid_frame_type' in data['error'].lower() or 'type' in data['error'].lower()
    
    def test_create_preset_malformed_invalid_color_format(self, client):
        """Test creating preset with invalid color format."""
        preset_data = {
            "preset_name": "bad_color_preset",
            "description": "Invalid color format",
            "frame_config": {
                "type": "time",
                "name": "Bad Color",
                "enabled": True,
                "duration": 10,
                "colors": {
                    "background": "not-a-hex-color",
                    "text": "#FF0000"
                }
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_malformed_invalid_duration(self, client):
        """Test creating preset with invalid duration (out of range)."""
        preset_data = {
            "preset_name": "bad_duration_preset",
            "description": "Invalid duration",
            "frame_config": {
                "type": "text",
                "name": "Bad Duration",
                "text": "Test",
                "enabled": True,
                "duration": 5000  # exceeds MAX_DURATION of 3600
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
        assert 'duration' in data['error'].lower() or '3600' in data['error']
    
    def test_create_preset_malformed_invalid_font_size(self, client):
        """Test creating preset with invalid font size."""
        preset_data = {
            "preset_name": "bad_font_preset",
            "description": "Invalid font size",
            "frame_config": {
                "type": "time",
                "name": "Bad Font",
                "enabled": True,
                "duration": 10,
                "font": {
                    "path": "test.ttf",
                    "size": 150  # exceeds maximum of 72
                }
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_malformed_invalid_position_type(self, client):
        """Test creating preset with invalid position type."""
        preset_data = {
            "preset_name": "bad_position_preset",
            "description": "Invalid position",
            "frame_config": {
                "type": "text",
                "name": "Bad Position",
                "text": "Test",
                "enabled": True,
                "duration": 10,
                "position": {
                    "x": "invalid",  # should be integer or specific string
                    "y": 10
                }
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_malformed_invalid_unit(self, client):
        """Test creating preset with invalid temperature unit."""
        preset_data = {
            "preset_name": "bad_unit_preset",
            "description": "Invalid unit",
            "frame_config": {
                "type": "temperature",
                "name": "Bad Unit",
                "enabled": True,
                "duration": 10,
                "unit": "kelvin"  # not in allowed enum
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_malformed_wrong_data_type(self, client):
        """Test creating preset with wrong data type for enabled field."""
        preset_data = {
            "preset_name": "bad_bool_preset",
            "description": "Invalid boolean field",
            "frame_config": {
                "type": "time",
                "name": "Bad Boolean",
                "enabled": "yes",  # should be boolean
                "duration": 10
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_malformed_text_too_long(self, client):
        """Test creating preset with text exceeding maximum length."""
        preset_data = {
            "preset_name": "long_text_preset",
            "description": "Text too long",
            "frame_config": {
                "type": "text",
                "name": "Long Text",
                "text": "a" * 2000,  # exceeds MAX_TEXT_LENGTH of 1000
                "enabled": True,
                "duration": 10
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
    
    def test_create_preset_malformed_empty_preset_name(self, client):
        """Test creating preset with empty preset_name."""
        preset_data = {
            "preset_name": "",  # empty string
            "description": "Empty name",
            "frame_config": {
                "type": "time",
                "name": "Test",
                "enabled": True,
                "duration": 10
            }
        }
        
        response = client.post('/api/presets',
                              data=json.dumps(preset_data),
                              content_type='application/json')
        assert response.status_code == 400
        data = response.get_json()
        assert 'error' in data
