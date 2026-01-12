"""Tests for the controller control mode functionality."""

import pytest
from unittest.mock import MagicMock, patch
from lumigrid.controller import ApplicationController


@pytest.fixture
def mock_config():
    """Create a mock config."""
    with patch('lumigrid.controller.Config') as MockConfig:
        config_instance = MagicMock()
        config_instance.get.side_effect = lambda key, default=None: {
            'control.mode': 'independent',
            'control.sync_role': 'slave',
            'control.external_source': 'mqtt',
            'led_matrix': {},
            'temperature': {'enabled': False},
            'rtc': {'enabled': False},
            'display.default_mode': 'clock'
        }.get(key, default)
        config_instance.config = {}
        MockConfig.return_value = config_instance
        yield config_instance


@pytest.fixture
def mock_display_manager():
    """Create a mock display manager."""
    with patch('lumigrid.controller.DisplayManager') as MockDisplayManager:
        display_instance = MagicMock()
        MockDisplayManager.return_value = display_instance
        yield display_instance


@pytest.fixture
def mock_sync_protocol():
    """Create a mock sync protocol."""
    with patch('lumigrid.controller.SyncProtocol') as MockSyncProtocol:
        sync_instance = MagicMock()
        MockSyncProtocol.return_value = sync_instance
        yield MockSyncProtocol, sync_instance


class TestControlModeInitialization:
    """Tests for control mode initialization."""
    
    def test_independent_mode_initialization(self, mock_config, mock_display_manager):
        """Test that independent mode initializes correctly."""
        mock_config.get.side_effect = lambda key, default=None: {
            'control.mode': 'independent',
            'led_matrix': {},
            'temperature': {'enabled': False},
            'rtc': {'enabled': False},
            'display.default_mode': 'clock'
        }.get(key, default)
        
        with patch('lumigrid.controller.SyncProtocol') as MockSyncProtocol:
            controller = ApplicationController()
            assert controller.control_mode == 'independent'
            assert controller.sync_protocol is None
            MockSyncProtocol.assert_not_called()
    
    def test_sync_slave_mode_initialization(self, mock_config, mock_display_manager, mock_sync_protocol):
        """Test that sync slave mode initializes correctly."""
        MockSyncProtocol, sync_instance = mock_sync_protocol
        mock_config.get.side_effect = lambda key, default=None: {
            'control.mode': 'sync',
            'control.sync_role': 'slave',
            'led_matrix': {},
            'temperature': {'enabled': False},
            'rtc': {'enabled': False},
            'display.default_mode': 'clock'
        }.get(key, default)
        
        controller = ApplicationController()
        assert controller.control_mode == 'sync'
        assert controller.sync_protocol is not None
        sync_instance.start_slave.assert_called_once()
        sync_instance.start_master.assert_not_called()
    
    def test_sync_master_mode_initialization(self, mock_config, mock_display_manager, mock_sync_protocol):
        """Test that sync master mode initializes correctly."""
        MockSyncProtocol, sync_instance = mock_sync_protocol
        mock_config.get.side_effect = lambda key, default=None: {
            'control.mode': 'sync',
            'control.sync_role': 'master',
            'led_matrix': {},
            'temperature': {'enabled': False},
            'rtc': {'enabled': False},
            'display.default_mode': 'clock'
        }.get(key, default)
        
        controller = ApplicationController()
        assert controller.control_mode == 'sync'
        assert controller.sync_protocol is not None
        sync_instance.start_master.assert_called_once()
        sync_instance.start_slave.assert_not_called()
    
    def test_external_mode_initialization(self, mock_config, mock_display_manager):
        """Test that external mode initializes correctly (with warning)."""
        mock_config.get.side_effect = lambda key, default=None: {
            'control.mode': 'external',
            'control.external_source': 'mqtt',
            'led_matrix': {},
            'temperature': {'enabled': False},
            'rtc': {'enabled': False},
            'display.default_mode': 'clock'
        }.get(key, default)
        
        with patch('lumigrid.controller.SyncProtocol') as MockSyncProtocol:
            controller = ApplicationController()
            assert controller.control_mode == 'external'
            assert controller.sync_protocol is None
            MockSyncProtocol.assert_not_called()


class TestControlModeGetters:
    """Tests for control mode getter methods."""
    
    def test_get_control_mode(self, mock_config, mock_display_manager):
        """Test getting control mode."""
        controller = ApplicationController()
        controller.control_mode = 'sync'
        assert controller.get_control_mode() == 'sync'
    
    def test_get_control_settings(self, mock_config, mock_display_manager):
        """Test getting control settings."""
        controller = ApplicationController()
        controller.control_mode = 'sync'
        
        settings = controller.get_control_settings()
        assert 'mode' in settings
        assert 'sync_role' in settings
        assert 'external_source' in settings
        assert settings['mode'] == 'sync'


class TestControlModeSwitching:
    """Tests for control mode switching."""
    
    def test_set_control_mode_to_independent(self, mock_config, mock_display_manager):
        """Test switching to independent mode."""
        controller = ApplicationController()
        
        # Update mock to return independent when checked
        def get_side_effect(key, default=None):
            mapping = {
                'control.mode': 'independent',
                'control.sync_role': 'slave',
                'control.external_source': 'mqtt',
                'led_matrix': {},
                'temperature': {'enabled': False},
                'rtc': {'enabled': False},
                'display.default_mode': 'clock'
            }
            return mapping.get(key, default)
        
        mock_config.get.side_effect = get_side_effect
        
        result = controller.set_control_mode('independent')
        assert result is True
        assert controller.control_mode == 'independent'
    
    def test_set_control_mode_to_sync_master(self, mock_config, mock_display_manager):
        """Test switching to sync master mode."""
        controller = ApplicationController()
        
        # Update mock to return sync mode after it's set
        def get_side_effect_after_set(key, default=None):
            mapping = {
                'control.mode': 'sync',
                'control.sync_role': 'master',
                'control.external_source': 'mqtt',
                'led_matrix': {},
                'temperature': {'enabled': False},
                'rtc': {'enabled': False},
                'display.default_mode': 'clock'
            }
            return mapping.get(key, default)
        
        result = controller.set_control_mode('sync', sync_role='master')
        assert result is True
        
        # Verify that config was updated with correct values
        mock_config.set.assert_any_call('control.mode', 'sync')
        mock_config.set.assert_any_call('control.sync_role', 'master')
        mock_config.save.assert_called()
    
    def test_set_control_mode_to_sync_slave(self, mock_config, mock_display_manager, mock_sync_protocol):
        """Test switching to sync slave mode."""
        MockSyncProtocol, sync_instance = mock_sync_protocol
        controller = ApplicationController()
        
        result = controller.set_control_mode('sync', sync_role='slave')
        assert result is True
        # Verify that config.set was called with correct values
        mock_config.set.assert_any_call('control.mode', 'sync')
        mock_config.set.assert_any_call('control.sync_role', 'slave')
    
    def test_set_control_mode_to_external(self, mock_config, mock_display_manager):
        """Test switching to external mode."""
        controller = ApplicationController()
        result = controller.set_control_mode('external', external_source='mqtt')
        assert result is True
        # Verify that config.set was called
        mock_config.set.assert_any_call('control.mode', 'external')
        mock_config.set.assert_any_call('control.external_source', 'mqtt')
    
    def test_set_invalid_control_mode(self, mock_config, mock_display_manager):
        """Test setting an invalid control mode."""
        controller = ApplicationController()
        result = controller.set_control_mode('invalid_mode')
        assert result is False
    
    def test_control_mode_switch_stops_sync_protocol(self, mock_config, mock_display_manager, mock_sync_protocol):
        """Test that switching modes stops existing sync protocol."""
        MockSyncProtocol, sync_instance = mock_sync_protocol
        controller = ApplicationController()
        
        # Manually set sync protocol to test cleanup
        controller.sync_protocol = sync_instance
        
        # Switch to independent
        controller.set_control_mode('independent')
        sync_instance.stop.assert_called()


class TestControlModeInStatus:
    """Tests for control mode in status."""
    
    def test_status_includes_control_mode(self, mock_config, mock_display_manager):
        """Test that status includes control mode information."""
        controller = ApplicationController()
        controller.control_mode = 'sync'
        
        status = controller.get_status()
        assert 'control_mode' in status
        assert status['control_mode'] == 'sync'
        assert 'control_settings' in status
        assert 'mode' in status['control_settings']
    
    def test_status_includes_sync_role(self, mock_config, mock_display_manager):
        """Test that status includes sync role."""
        controller = ApplicationController()
        
        status = controller.get_status()
        assert 'control_settings' in status
        assert 'sync_role' in status['control_settings']
    
    def test_status_includes_external_source(self, mock_config, mock_display_manager):
        """Test that status includes external source."""
        controller = ApplicationController()
        
        status = controller.get_status()
        assert 'control_settings' in status
        assert 'external_source' in status['control_settings']


class TestControlModeCleanup:
    """Tests for control mode cleanup."""
    
    def test_stop_cleans_up_sync_protocol(self, mock_config, mock_display_manager, mock_sync_protocol):
        """Test that stop() cleans up sync protocol."""
        MockSyncProtocol, sync_instance = mock_sync_protocol
        controller = ApplicationController()
        
        # Manually set sync protocol
        controller.sync_protocol = sync_instance
        
        controller.stop()
        sync_instance.stop.assert_called()
    
    def test_stop_without_sync_protocol(self, mock_config, mock_display_manager):
        """Test that stop() works without sync protocol."""
        controller = ApplicationController()
        controller.control_mode = 'independent'
        controller.sync_protocol = None
        
        # Should not raise an exception
        controller.stop()
