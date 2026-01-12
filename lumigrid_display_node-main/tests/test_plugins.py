"""Tests for the plugin system module."""

import pytest
import os
import json
import tempfile
from unittest.mock import MagicMock, patch

from lumigrid.plugins import (
    PluginType,
    PluginState,
    PluginMetadata,
    PluginConfig,
    PluginAPI,
    PluginBase,
    DisplayModePlugin,
    DataSourcePlugin,
    EffectPlugin,
    WidgetPlugin,
    PluginManager,
    PluginTemplate,
    PluginRegistry,
)


class TestPluginMetadata:
    """Tests for PluginMetadata class."""
    
    def test_creation_minimal(self):
        """Test metadata creation with minimal args."""
        metadata = PluginMetadata(
            id="test_plugin",
            name="Test Plugin",
            version="1.0.0"
        )
        
        assert metadata.id == "test_plugin"
        assert metadata.name == "Test Plugin"
        assert metadata.version == "1.0.0"
    
    def test_creation_full(self):
        """Test metadata creation with all args."""
        metadata = PluginMetadata(
            id="test_plugin",
            name="Test Plugin",
            version="1.0.0",
            description="A test plugin",
            author="Test Author",
            plugin_type=PluginType.DISPLAY_MODE,
            dependencies=["other_plugin"],
            tags=["test", "demo"]
        )
        
        assert metadata.description == "A test plugin"
        assert metadata.author == "Test Author"
        assert metadata.plugin_type == PluginType.DISPLAY_MODE
        assert "other_plugin" in metadata.dependencies
    
    def test_to_dict(self):
        """Test metadata serialization."""
        metadata = PluginMetadata(
            id="test",
            name="Test",
            version="1.0.0"
        )
        data = metadata.to_dict()
        
        assert data['id'] == "test"
        assert data['name'] == "Test"
        assert 'plugin_type' in data
    
    def test_from_dict(self):
        """Test metadata deserialization."""
        data = {
            'id': 'imported',
            'name': 'Imported Plugin',
            'version': '2.0.0',
            'plugin_type': 'display_mode',
            'tags': ['imported']
        }
        
        metadata = PluginMetadata.from_dict(data)
        
        assert metadata.id == "imported"
        assert metadata.name == "Imported Plugin"
        assert metadata.plugin_type == PluginType.DISPLAY_MODE


class TestPluginConfig:
    """Tests for PluginConfig class."""
    
    def test_creation_defaults(self):
        """Test config creation with defaults."""
        config = PluginConfig()
        
        assert config.enabled is True
        assert config.settings == {}
        assert config.priority == 0
    
    def test_get_set(self):
        """Test getting and setting values."""
        config = PluginConfig()
        
        config.set("key1", "value1")
        config.set("key2", 42)
        
        assert config.get("key1") == "value1"
        assert config.get("key2") == 42
        assert config.get("nonexistent", "default") == "default"
    
    def test_to_dict(self):
        """Test config serialization."""
        config = PluginConfig(enabled=False, priority=10)
        config.set("option", "value")
        
        data = config.to_dict()
        
        assert data['enabled'] is False
        assert data['priority'] == 10
        assert data['settings']['option'] == "value"
    
    def test_from_dict(self):
        """Test config deserialization."""
        data = {
            'enabled': False,
            'settings': {'key': 'value'},
            'priority': 5
        }
        
        config = PluginConfig.from_dict(data)
        
        assert config.enabled is False
        assert config.get('key') == 'value'
        assert config.priority == 5


class TestPluginAPI:
    """Tests for PluginAPI class."""
    
    def test_get_api_version(self):
        """Test getting API version."""
        manager = MagicMock()
        api = PluginAPI(manager)
        
        version = api.get_api_version()
        
        assert version is not None
        assert '.' in version  # Version format x.y.z
    
    def test_register_callback(self):
        """Test registering callback."""
        manager = MagicMock()
        api = PluginAPI(manager)
        
        callback = MagicMock()
        api.register_callback("test_event", callback)
        
        assert "test_event" in api._registered_callbacks
        assert callback in api._registered_callbacks["test_event"]
    
    def test_emit_event(self):
        """Test emitting event."""
        manager = MagicMock()
        api = PluginAPI(manager)
        
        callback = MagicMock()
        api.register_callback("test_event", callback)
        
        api.emit_event("test_event", data="test")
        
        callback.assert_called_once_with(data="test")
    
    def test_get_display_dimensions(self):
        """Test getting display dimensions."""
        manager = MagicMock()
        manager.display_dimensions = (64, 32)
        api = PluginAPI(manager)
        
        dims = api.get_display_dimensions()
        
        assert dims == (64, 32)


class MockPlugin(PluginBase):
    """Mock plugin for testing."""
    
    @property
    def metadata(self):
        return PluginMetadata(
            id="mock_plugin",
            name="Mock Plugin",
            version="1.0.0",
            plugin_type=PluginType.WIDGET
        )
    
    def on_enable(self):
        return True
    
    def on_disable(self):
        pass


class TestPluginManager:
    """Tests for PluginManager class."""
    
    def test_creation(self):
        """Test manager creation."""
        with tempfile.TemporaryDirectory() as tmpdir:
            plugins_dir = os.path.join(tmpdir, "plugins")
            config_dir = os.path.join(tmpdir, "config")
            
            manager = PluginManager(
                plugins_dir=plugins_dir,
                config_dir=config_dir
            )
            
            assert os.path.exists(plugins_dir)
            assert os.path.exists(config_dir)
    
    def test_discover_plugins_empty(self):
        """Test discovering plugins in empty directory."""
        with tempfile.TemporaryDirectory() as tmpdir:
            manager = PluginManager(plugins_dir=tmpdir)
            
            discovered = manager.discover_plugins()
            
            assert discovered == []
    
    def test_get_plugin_config(self):
        """Test getting plugin configuration."""
        with tempfile.TemporaryDirectory() as tmpdir:
            manager = PluginManager(plugins_dir=tmpdir)
            manager.plugin_configs["test"] = PluginConfig(enabled=True)
            
            config = manager.get_plugin_config("test")
            
            assert config is not None
            assert config.enabled is True
    
    def test_list_plugins_empty(self):
        """Test listing plugins when empty."""
        with tempfile.TemporaryDirectory() as tmpdir:
            manager = PluginManager(plugins_dir=tmpdir)
            
            plugins = manager.list_plugins()
            
            assert plugins == []
    
    def test_get_plugins_by_type(self):
        """Test getting plugins by type."""
        with tempfile.TemporaryDirectory() as tmpdir:
            manager = PluginManager(plugins_dir=tmpdir)
            
            # No plugins loaded
            result = manager.get_plugins_by_type(PluginType.DISPLAY_MODE)
            
            assert result == []


class TestPluginTemplate:
    """Tests for PluginTemplate class."""
    
    def test_generate_display_mode(self):
        """Test generating display mode template."""
        code = PluginTemplate.generate(
            plugin_type=PluginType.DISPLAY_MODE,
            name="My Display",
            description="A custom display mode",
            author="Test Author"
        )
        
        assert "My Display" in code
        assert "DisplayModePlugin" in code
        assert "def render" in code
        assert "def update" in code
    
    def test_generate_data_source(self):
        """Test generating data source template."""
        code = PluginTemplate.generate(
            plugin_type=PluginType.DATA_SOURCE,
            name="My Data Source"
        )
        
        assert "DataSourcePlugin" in code
        assert "def fetch" in code
        assert "def get_data_type" in code
    
    def test_generate_effect(self):
        """Test generating effect template."""
        code = PluginTemplate.generate(
            plugin_type=PluginType.EFFECT,
            name="My Effect"
        )
        
        assert "EffectPlugin" in code
        assert "def apply" in code
    
    def test_create_plugin_directory(self):
        """Test creating plugin directory structure."""
        with tempfile.TemporaryDirectory() as tmpdir:
            plugin_dir = PluginTemplate.create_plugin_directory(
                base_dir=tmpdir,
                plugin_type=PluginType.DISPLAY_MODE,
                name="Test Plugin",
                description="A test plugin"
            )
            
            assert plugin_dir is not None
            assert os.path.exists(plugin_dir)
            assert os.path.exists(os.path.join(plugin_dir, "__init__.py"))
            assert os.path.exists(os.path.join(plugin_dir, "manifest.json"))
            assert os.path.exists(os.path.join(plugin_dir, "README.md"))
            
            # Check manifest content
            with open(os.path.join(plugin_dir, "manifest.json")) as f:
                manifest = json.load(f)
                assert manifest['name'] == "Test Plugin"


class TestPluginRegistry:
    """Tests for PluginRegistry class."""
    
    def test_creation(self):
        """Test registry creation."""
        registry = PluginRegistry()
        
        assert len(registry.registered_plugins) == 0
    
    def test_register(self):
        """Test registering plugin."""
        registry = PluginRegistry()
        metadata = PluginMetadata(
            id="test_plugin",
            name="Test Plugin",
            version="1.0.0"
        )
        
        result = registry.register(metadata, "/path/to/plugin")
        
        assert result is True
        assert "test_plugin" in registry.registered_plugins
    
    def test_unregister(self):
        """Test unregistering plugin."""
        registry = PluginRegistry()
        metadata = PluginMetadata(
            id="test_plugin",
            name="Test Plugin",
            version="1.0.0"
        )
        registry.register(metadata, "/path/to/plugin")
        
        result = registry.unregister("test_plugin")
        
        assert result is True
        assert "test_plugin" not in registry.registered_plugins
    
    def test_search_by_name(self):
        """Test searching by name."""
        registry = PluginRegistry()
        registry.register(
            PluginMetadata(id="clock_plugin", name="Clock Display", version="1.0.0"),
            "/path/1"
        )
        registry.register(
            PluginMetadata(id="weather_plugin", name="Weather Display", version="1.0.0"),
            "/path/2"
        )
        
        results = registry.search("Clock")
        
        assert len(results) == 1
        assert results[0]['metadata']['id'] == "clock_plugin"
    
    def test_search_by_type(self):
        """Test searching by plugin type."""
        registry = PluginRegistry()
        registry.register(
            PluginMetadata(
                id="display1", name="Display 1", version="1.0.0",
                plugin_type=PluginType.DISPLAY_MODE
            ),
            "/path/1"
        )
        registry.register(
            PluginMetadata(
                id="effect1", name="Effect 1", version="1.0.0",
                plugin_type=PluginType.EFFECT
            ),
            "/path/2"
        )
        
        results = registry.search(plugin_type=PluginType.DISPLAY_MODE)
        
        assert len(results) == 1
        assert results[0]['metadata']['id'] == "display1"
    
    def test_get_plugin_info(self):
        """Test getting plugin info."""
        registry = PluginRegistry()
        metadata = PluginMetadata(
            id="test_plugin",
            name="Test Plugin",
            version="1.0.0",
            description="Test description"
        )
        registry.register(metadata, "/path/to/plugin")
        
        info = registry.get_plugin_info("test_plugin")
        
        assert info is not None
        assert info['metadata']['name'] == "Test Plugin"
        assert info['source_path'] == "/path/to/plugin"
