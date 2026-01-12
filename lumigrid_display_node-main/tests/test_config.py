"""Tests for the config module."""

import pytest
import tempfile
import os
from lumigrid.config import Config


class TestConfigValidation:
    """Tests for config validation."""
    
    def test_validate_config_valid(self, tmp_path):
        """Test validation passes with valid config."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        errors = config.validate_config()
        assert len(errors) == 0
    
    def test_validate_config_invalid_brightness(self, tmp_path):
        """Test validation catches invalid brightness."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('led_matrix.brightness', 150)
        errors = config.validate_config()
        assert any('Brightness' in e for e in errors)
    
    def test_validate_config_invalid_brightness_negative(self, tmp_path):
        """Test validation catches negative brightness."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('led_matrix.brightness', -10)
        errors = config.validate_config()
        assert any('Brightness' in e for e in errors)
    
    def test_validate_config_invalid_rows(self, tmp_path):
        """Test validation catches invalid rows."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('led_matrix.rows', 1000)
        errors = config.validate_config()
        assert any('rows' in e for e in errors)
    
    def test_validate_config_invalid_cols(self, tmp_path):
        """Test validation catches invalid cols."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('led_matrix.cols', 1)
        errors = config.validate_config()
        assert any('cols' in e for e in errors)
    
    def test_validate_config_invalid_port(self, tmp_path):
        """Test validation catches invalid port."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('web.port', 0)
        errors = config.validate_config()
        assert any('port' in e for e in errors)
    
    def test_validate_config_invalid_temp_unit(self, tmp_path):
        """Test validation catches invalid temperature unit."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('temperature.unit', 'kelvin')
        errors = config.validate_config()
        assert any('Temperature unit' in e for e in errors)
    
    def test_validate_config_invalid_log_level(self, tmp_path):
        """Test validation catches invalid log level."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('logging.level', 'INVALID')
        errors = config.validate_config()
        assert any('Logging level' in e for e in errors)


class TestRateLimitingConfig:
    """Tests for rate limiting configuration in defaults."""
    
    def test_default_config_has_rate_limiting(self, tmp_path):
        """Test that default config includes rate limiting settings."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        rate_limiting = config.get('web.rate_limiting', {})
        assert rate_limiting is not None
        assert 'enabled' in rate_limiting
        assert 'default_limits' in rate_limiting
        assert 'storage_uri' in rate_limiting
    
    def test_rate_limiting_default_enabled(self, tmp_path):
        """Test that rate limiting is enabled by default."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        enabled = config.get('web.rate_limiting.enabled', False)
        assert enabled is True
    
    def test_rate_limiting_has_default_limits(self, tmp_path):
        """Test that default rate limits are set."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        limits = config.get('web.rate_limiting.default_limits', [])
        assert len(limits) > 0
        assert any('per day' in limit for limit in limits)
        assert any('per hour' in limit for limit in limits)


class TestControlModeConfig:
    """Tests for control mode configuration."""
    
    def test_default_config_has_control_mode(self, tmp_path):
        """Test that default config includes control mode settings."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        control = config.get('control', {})
        assert control is not None
        assert 'mode' in control
        assert 'sync_role' in control
        assert 'external_source' in control
    
    def test_control_mode_default_independent(self, tmp_path):
        """Test that control mode defaults to independent."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        mode = config.get('control.mode', '')
        assert mode == 'independent'
    
    def test_validate_valid_control_mode(self, tmp_path):
        """Test validation passes with valid control mode."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('control.mode', 'sync')
        errors = config.validate_config()
        assert not any('Control mode' in e for e in errors)
    
    def test_validate_invalid_control_mode(self, tmp_path):
        """Test validation catches invalid control mode."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('control.mode', 'invalid_mode')
        errors = config.validate_config()
        assert any('Control mode' in e for e in errors)
    
    def test_validate_valid_sync_role(self, tmp_path):
        """Test validation passes with valid sync role."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('control.sync_role', 'master')
        errors = config.validate_config()
        assert not any('Sync role' in e for e in errors)
    
    def test_validate_invalid_sync_role(self, tmp_path):
        """Test validation catches invalid sync role."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('control.sync_role', 'invalid_role')
        errors = config.validate_config()
        assert any('Sync role' in e for e in errors)
    
    def test_validate_valid_external_source(self, tmp_path):
        """Test validation passes with valid external source."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('control.external_source', 'artnet')
        errors = config.validate_config()
        assert not any('External source' in e for e in errors)
    
    def test_validate_invalid_external_source(self, tmp_path):
        """Test validation catches invalid external source."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('control.external_source', 'invalid_source')
        errors = config.validate_config()
        assert any('External source' in e for e in errors)
    
    def test_all_control_modes_valid(self, tmp_path):
        """Test all valid control modes."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        for mode in ['external', 'sync', 'independent']:
            config.set('control.mode', mode)
            errors = config.validate_config()
            assert not any('Control mode' in e for e in errors), f"Mode {mode} should be valid"
    
    def test_all_sync_roles_valid(self, tmp_path):
        """Test all valid sync roles."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        for role in ['master', 'slave']:
            config.set('control.sync_role', role)
            errors = config.validate_config()
            assert not any('Sync role' in e for e in errors), f"Role {role} should be valid"
    
    def test_all_external_sources_valid(self, tmp_path):
        """Test all valid external sources."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        for source in ['mqtt', 'artnet']:
            config.set('control.external_source', source)
            errors = config.validate_config()
            assert not any('External source' in e for e in errors), f"Source {source} should be valid"


class TestSetupCompletion:
    """Tests for setup completion tracking."""
    
    def test_is_setup_complete_no_config_file(self, tmp_path):
        """Test that setup is incomplete when config file doesn't exist."""
        config_path = tmp_path / "nonexistent.yaml"
        config = Config(str(config_path))
        assert config.is_setup_complete() is False
    
    def test_is_setup_complete_with_flag(self, tmp_path):
        """Test that setup is complete when setup_complete flag is True."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.set('setup_complete', True)
        config.save()
        
        # Reload to verify
        config2 = Config(str(config_path))
        assert config2.is_setup_complete() is True
    
    def test_is_setup_complete_default_values(self, tmp_path):
        """Test that setup is incomplete with default values."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        config.save()
        
        # Reload to verify
        config2 = Config(str(config_path))
        # Should be incomplete because using default node_id
        assert config2.is_setup_complete() is False
    
    def test_mark_setup_complete(self, tmp_path):
        """Test marking setup as complete."""
        config_path = tmp_path / "config.yaml"
        config = Config(str(config_path))
        
        assert config.mark_setup_complete() is True
        assert config.get('setup_complete') is True
        
        # Reload and verify
        config2 = Config(str(config_path))
        assert config2.is_setup_complete() is True
