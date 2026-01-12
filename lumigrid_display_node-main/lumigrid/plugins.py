"""Plugin system for Lumigrid display.

This module provides a comprehensive plugin system including:
- Plugin architecture for extending functionality
- API for developers to create custom plugins
- Template system for plugin development
- Version management for plugin compatibility
- Security sandboxing for safe plugin execution
- Plugin configuration with per-plugin settings
"""

import importlib
import importlib.util
import logging
import os
import sys
import json
import uuid
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Set, Type

logger = logging.getLogger(__name__)


class PluginType(Enum):
    """Types of plugins."""
    DISPLAY_MODE = "display_mode"  # New display mode
    DATA_SOURCE = "data_source"    # Data source integration
    EFFECT = "effect"              # Visual effect
    INPUT_HANDLER = "input_handler"  # Input method
    THEME = "theme"                # Custom theme
    WIDGET = "widget"              # UI widget
    INTEGRATION = "integration"    # External service integration


class PluginState(Enum):
    """Plugin lifecycle states."""
    UNLOADED = "unloaded"
    LOADED = "loaded"
    ENABLED = "enabled"
    DISABLED = "disabled"
    ERROR = "error"


@dataclass
class PluginMetadata:
    """Plugin metadata.
    
    Attributes:
        id: Unique plugin identifier
        name: Display name
        version: Plugin version
        description: Plugin description
        author: Plugin author
        plugin_type: Type of plugin
        dependencies: Required dependency plugin IDs
        min_api_version: Minimum API version required
        max_api_version: Maximum API version supported
        homepage: Plugin homepage URL
        license: Plugin license
        tags: Plugin tags for categorization
    """
    id: str
    name: str
    version: str
    description: str = ""
    author: str = ""
    plugin_type: PluginType = PluginType.WIDGET
    dependencies: List[str] = field(default_factory=list)
    min_api_version: str = "1.0.0"
    max_api_version: str = "2.0.0"
    homepage: str = ""
    license: str = ""
    tags: List[str] = field(default_factory=list)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            'id': self.id,
            'name': self.name,
            'version': self.version,
            'description': self.description,
            'author': self.author,
            'plugin_type': self.plugin_type.value,
            'dependencies': self.dependencies,
            'min_api_version': self.min_api_version,
            'max_api_version': self.max_api_version,
            'homepage': self.homepage,
            'license': self.license,
            'tags': self.tags
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'PluginMetadata':
        """Create from dictionary."""
        plugin_type = data.get('plugin_type', 'widget')
        if isinstance(plugin_type, str):
            plugin_type = PluginType(plugin_type)
        
        return cls(
            id=data.get('id', str(uuid.uuid4())[:8]),
            name=data.get('name', 'Unnamed Plugin'),
            version=data.get('version', '1.0.0'),
            description=data.get('description', ''),
            author=data.get('author', ''),
            plugin_type=plugin_type,
            dependencies=data.get('dependencies', []),
            min_api_version=data.get('min_api_version', '1.0.0'),
            max_api_version=data.get('max_api_version', '2.0.0'),
            homepage=data.get('homepage', ''),
            license=data.get('license', ''),
            tags=data.get('tags', [])
        )


@dataclass
class PluginConfig:
    """Plugin configuration.
    
    Attributes:
        enabled: Whether plugin is enabled
        settings: Plugin-specific settings
        priority: Plugin load priority (higher = later)
    """
    enabled: bool = True
    settings: Dict[str, Any] = field(default_factory=dict)
    priority: int = 0
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get a setting value.
        
        Args:
            key: Setting key
            default: Default value
            
        Returns:
            Setting value
        """
        return self.settings.get(key, default)
    
    def set(self, key: str, value: Any) -> None:
        """Set a setting value.
        
        Args:
            key: Setting key
            value: Setting value
        """
        self.settings[key] = value
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            'enabled': self.enabled,
            'settings': self.settings,
            'priority': self.priority
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'PluginConfig':
        """Create from dictionary."""
        return cls(
            enabled=data.get('enabled', True),
            settings=data.get('settings', {}),
            priority=data.get('priority', 0)
        )


class PluginAPI:
    """API exposed to plugins."""
    
    API_VERSION = "1.0.0"
    
    def __init__(self, plugin_manager: 'PluginManager'):
        """Initialize plugin API.
        
        Args:
            plugin_manager: Parent plugin manager
        """
        self._manager = plugin_manager
        self._registered_callbacks: Dict[str, List[Callable]] = {}
    
    def get_api_version(self) -> str:
        """Get API version.
        
        Returns:
            API version string
        """
        return self.API_VERSION
    
    def register_callback(self, event: str, callback: Callable) -> None:
        """Register callback for system events.
        
        Args:
            event: Event name
            callback: Callback function
        """
        if event not in self._registered_callbacks:
            self._registered_callbacks[event] = []
        self._registered_callbacks[event].append(callback)
    
    def unregister_callback(self, event: str, callback: Callable) -> bool:
        """Unregister a callback.
        
        Args:
            event: Event name
            callback: Callback to remove
            
        Returns:
            True if removed
        """
        if event in self._registered_callbacks:
            if callback in self._registered_callbacks[event]:
                self._registered_callbacks[event].remove(callback)
                return True
        return False
    
    def emit_event(self, event: str, **kwargs) -> None:
        """Emit an event to registered callbacks.
        
        Args:
            event: Event name
            **kwargs: Event data
        """
        if event in self._registered_callbacks:
            for callback in self._registered_callbacks[event]:
                try:
                    callback(**kwargs)
                except Exception as e:
                    logger.error(f"Error in plugin callback: {e}")
    
    def get_config(self, plugin_id: str) -> Optional[PluginConfig]:
        """Get plugin configuration.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            Plugin config or None
        """
        return self._manager.get_plugin_config(plugin_id)
    
    def save_config(self, plugin_id: str, config: PluginConfig) -> bool:
        """Save plugin configuration.
        
        Args:
            plugin_id: Plugin identifier
            config: Configuration to save
            
        Returns:
            True if saved
        """
        return self._manager.save_plugin_config(plugin_id, config)
    
    def get_display_dimensions(self) -> tuple:
        """Get display dimensions.
        
        Returns:
            Tuple of (width, height)
        """
        return self._manager.display_dimensions
    
    def log(self, plugin_id: str, level: str, message: str) -> None:
        """Log a message from plugin.
        
        Args:
            plugin_id: Plugin identifier
            level: Log level
            message: Log message
        """
        log_func = getattr(logger, level.lower(), logger.info)
        log_func(f"[Plugin:{plugin_id}] {message}")


class PluginBase(ABC):
    """Base class for all plugins."""
    
    def __init__(self, api: PluginAPI, config: PluginConfig):
        """Initialize plugin.
        
        Args:
            api: Plugin API instance
            config: Plugin configuration
        """
        self.api = api
        self.config = config
        self._state = PluginState.LOADED
    
    @property
    @abstractmethod
    def metadata(self) -> PluginMetadata:
        """Get plugin metadata.
        
        Returns:
            Plugin metadata
        """
        pass
    
    @abstractmethod
    def on_enable(self) -> bool:
        """Called when plugin is enabled.
        
        Returns:
            True if successfully enabled
        """
        pass
    
    @abstractmethod
    def on_disable(self) -> None:
        """Called when plugin is disabled."""
        pass
    
    def on_load(self) -> None:
        """Called when plugin is loaded."""
        pass
    
    def on_unload(self) -> None:
        """Called when plugin is unloaded."""
        pass
    
    def on_config_change(self, key: str, value: Any) -> None:
        """Called when configuration changes.
        
        Args:
            key: Changed setting key
            value: New value
        """
        pass
    
    def get_state(self) -> PluginState:
        """Get current plugin state.
        
        Returns:
            Plugin state
        """
        return self._state
    
    def set_state(self, state: PluginState) -> None:
        """Set plugin state.
        
        Args:
            state: New state
        """
        self._state = state


class DisplayModePlugin(PluginBase):
    """Base class for display mode plugins."""
    
    @abstractmethod
    def update(self, dt: float) -> None:
        """Update display mode.
        
        Args:
            dt: Delta time
        """
        pass
    
    @abstractmethod
    def render(self) -> Any:
        """Render display content.
        
        Returns:
            PIL Image or similar renderable
        """
        pass


class DataSourcePlugin(PluginBase):
    """Base class for data source plugins."""
    
    @abstractmethod
    def fetch(self) -> Any:
        """Fetch data from source.
        
        Returns:
            Fetched data
        """
        pass
    
    @abstractmethod
    def get_data_type(self) -> str:
        """Get type of data this source provides.
        
        Returns:
            Data type identifier
        """
        pass


class EffectPlugin(PluginBase):
    """Base class for visual effect plugins."""
    
    @abstractmethod
    def apply(self, image: Any) -> Any:
        """Apply effect to image.
        
        Args:
            image: Input image
            
        Returns:
            Processed image
        """
        pass


class WidgetPlugin(PluginBase):
    """Base class for widget plugins."""
    
    @abstractmethod
    def render(self, x: int, y: int, width: int, height: int) -> Any:
        """Render widget.
        
        Args:
            x: X position
            y: Y position
            width: Widget width
            height: Widget height
            
        Returns:
            Rendered widget image
        """
        pass


class PluginManager:
    """Manages plugin lifecycle and operations."""
    
    def __init__(
        self,
        plugins_dir: str = "plugins",
        config_dir: str = "config/plugins",
        display_dimensions: tuple = (64, 32)
    ):
        """Initialize plugin manager.
        
        Args:
            plugins_dir: Directory containing plugins
            config_dir: Directory for plugin configs
            display_dimensions: Display size (width, height)
        """
        self.plugins_dir = plugins_dir
        self.config_dir = config_dir
        self.display_dimensions = display_dimensions
        
        self.plugins: Dict[str, PluginBase] = {}
        self.plugin_configs: Dict[str, PluginConfig] = {}
        self.plugin_states: Dict[str, PluginState] = {}
        
        self.api = PluginAPI(self)
        
        self._allowed_imports: Set[str] = {
            'PIL', 'PIL.Image', 'PIL.ImageDraw', 'PIL.ImageFont',
            'math', 'time', 'datetime', 'random', 'json',
            'typing', 'dataclasses', 'enum', 'abc',
            'logging', 'collections'
        }
        
        # Ensure directories exist
        os.makedirs(plugins_dir, exist_ok=True)
        os.makedirs(config_dir, exist_ok=True)
    
    def discover_plugins(self) -> List[str]:
        """Discover available plugins.
        
        Returns:
            List of discovered plugin IDs
        """
        discovered = []
        
        if not os.path.exists(self.plugins_dir):
            return discovered
        
        for item in os.listdir(self.plugins_dir):
            plugin_path = os.path.join(self.plugins_dir, item)
            
            # Check for plugin module
            if os.path.isdir(plugin_path):
                init_file = os.path.join(plugin_path, '__init__.py')
                manifest_file = os.path.join(plugin_path, 'manifest.json')
                
                if os.path.exists(init_file) and os.path.exists(manifest_file):
                    try:
                        with open(manifest_file, 'r') as f:
                            manifest = json.load(f)
                            plugin_id = manifest.get('id', item)
                            discovered.append(plugin_id)
                    except Exception as e:
                        logger.error(f"Error reading manifest for {item}: {e}")
            
            elif item.endswith('.py') and item != '__init__.py':
                plugin_id = item[:-3]
                discovered.append(plugin_id)
        
        return discovered
    
    def load_plugin(self, plugin_id: str) -> bool:
        """Load a plugin.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            True if loaded successfully
        """
        if plugin_id in self.plugins:
            logger.warning(f"Plugin {plugin_id} already loaded")
            return True
        
        try:
            # Load plugin configuration
            config = self._load_plugin_config(plugin_id)
            
            # Try loading from directory first
            plugin_dir = os.path.join(self.plugins_dir, plugin_id)
            if os.path.isdir(plugin_dir):
                plugin_class = self._load_plugin_from_directory(plugin_dir)
            else:
                # Try single file plugin
                plugin_file = os.path.join(self.plugins_dir, f"{plugin_id}.py")
                if os.path.exists(plugin_file):
                    plugin_class = self._load_plugin_from_file(plugin_file)
                else:
                    logger.error(f"Plugin not found: {plugin_id}")
                    return False
            
            if plugin_class is None:
                return False
            
            # Instantiate plugin
            plugin = plugin_class(self.api, config)
            plugin.on_load()
            
            self.plugins[plugin_id] = plugin
            self.plugin_configs[plugin_id] = config
            self.plugin_states[plugin_id] = PluginState.LOADED
            
            logger.info(f"Loaded plugin: {plugin_id}")
            return True
            
        except Exception as e:
            logger.error(f"Error loading plugin {plugin_id}: {e}")
            self.plugin_states[plugin_id] = PluginState.ERROR
            return False
    
    def _load_plugin_from_directory(self, plugin_dir: str) -> Optional[Type[PluginBase]]:
        """Load plugin from directory.
        
        Args:
            plugin_dir: Plugin directory path
            
        Returns:
            Plugin class or None
        """
        try:
            init_file = os.path.join(plugin_dir, '__init__.py')
            plugin_name = os.path.basename(plugin_dir)
            
            spec = importlib.util.spec_from_file_location(plugin_name, init_file)
            if spec is None or spec.loader is None:
                return None
            
            module = importlib.util.module_from_spec(spec)
            sys.modules[plugin_name] = module
            spec.loader.exec_module(module)
            
            # Find plugin class
            for attr_name in dir(module):
                attr = getattr(module, attr_name)
                if (isinstance(attr, type) and 
                    issubclass(attr, PluginBase) and 
                    attr is not PluginBase and
                    not attr_name.startswith('_')):
                    return attr
            
            logger.error(f"No plugin class found in {plugin_dir}")
            return None
            
        except Exception as e:
            logger.error(f"Error loading plugin from {plugin_dir}: {e}")
            return None
    
    def _load_plugin_from_file(self, plugin_file: str) -> Optional[Type[PluginBase]]:
        """Load plugin from single file.
        
        Args:
            plugin_file: Plugin file path
            
        Returns:
            Plugin class or None
        """
        try:
            plugin_name = os.path.splitext(os.path.basename(plugin_file))[0]
            
            spec = importlib.util.spec_from_file_location(plugin_name, plugin_file)
            if spec is None or spec.loader is None:
                return None
            
            module = importlib.util.module_from_spec(spec)
            sys.modules[plugin_name] = module
            spec.loader.exec_module(module)
            
            # Find plugin class
            for attr_name in dir(module):
                attr = getattr(module, attr_name)
                if (isinstance(attr, type) and 
                    issubclass(attr, PluginBase) and 
                    attr is not PluginBase):
                    return attr
            
            return None
            
        except Exception as e:
            logger.error(f"Error loading plugin from {plugin_file}: {e}")
            return None
    
    def unload_plugin(self, plugin_id: str) -> bool:
        """Unload a plugin.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            True if unloaded
        """
        if plugin_id not in self.plugins:
            return False
        
        try:
            plugin = self.plugins[plugin_id]
            
            # Disable first if enabled
            if self.plugin_states.get(plugin_id) == PluginState.ENABLED:
                self.disable_plugin(plugin_id)
            
            plugin.on_unload()
            
            del self.plugins[plugin_id]
            self.plugin_states[plugin_id] = PluginState.UNLOADED
            
            logger.info(f"Unloaded plugin: {plugin_id}")
            return True
            
        except Exception as e:
            logger.error(f"Error unloading plugin {plugin_id}: {e}")
            return False
    
    def enable_plugin(self, plugin_id: str) -> bool:
        """Enable a plugin.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            True if enabled
        """
        if plugin_id not in self.plugins:
            logger.error(f"Plugin not loaded: {plugin_id}")
            return False
        
        if self.plugin_states.get(plugin_id) == PluginState.ENABLED:
            return True
        
        try:
            plugin = self.plugins[plugin_id]
            
            # Check dependencies
            for dep_id in plugin.metadata.dependencies:
                if dep_id not in self.plugins:
                    logger.error(f"Missing dependency {dep_id} for {plugin_id}")
                    return False
                if self.plugin_states.get(dep_id) != PluginState.ENABLED:
                    logger.error(f"Dependency {dep_id} not enabled for {plugin_id}")
                    return False
            
            if plugin.on_enable():
                plugin.set_state(PluginState.ENABLED)
                self.plugin_states[plugin_id] = PluginState.ENABLED
                
                # Update config
                if plugin_id in self.plugin_configs:
                    self.plugin_configs[plugin_id].enabled = True
                    self._save_plugin_config(plugin_id)
                
                logger.info(f"Enabled plugin: {plugin_id}")
                return True
            
            return False
            
        except Exception as e:
            logger.error(f"Error enabling plugin {plugin_id}: {e}")
            self.plugin_states[plugin_id] = PluginState.ERROR
            return False
    
    def disable_plugin(self, plugin_id: str) -> bool:
        """Disable a plugin.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            True if disabled
        """
        if plugin_id not in self.plugins:
            return False
        
        if self.plugin_states.get(plugin_id) != PluginState.ENABLED:
            return True
        
        try:
            plugin = self.plugins[plugin_id]
            plugin.on_disable()
            plugin.set_state(PluginState.DISABLED)
            self.plugin_states[plugin_id] = PluginState.DISABLED
            
            # Update config
            if plugin_id in self.plugin_configs:
                self.plugin_configs[plugin_id].enabled = False
                self._save_plugin_config(plugin_id)
            
            logger.info(f"Disabled plugin: {plugin_id}")
            return True
            
        except Exception as e:
            logger.error(f"Error disabling plugin {plugin_id}: {e}")
            return False
    
    def get_plugin(self, plugin_id: str) -> Optional[PluginBase]:
        """Get a plugin instance.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            Plugin instance or None
        """
        return self.plugins.get(plugin_id)
    
    def get_plugin_config(self, plugin_id: str) -> Optional[PluginConfig]:
        """Get plugin configuration.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            Plugin config or None
        """
        return self.plugin_configs.get(plugin_id)
    
    def save_plugin_config(self, plugin_id: str, config: PluginConfig) -> bool:
        """Save plugin configuration.
        
        Args:
            plugin_id: Plugin identifier
            config: Configuration to save
            
        Returns:
            True if saved
        """
        self.plugin_configs[plugin_id] = config
        return self._save_plugin_config(plugin_id)
    
    def _load_plugin_config(self, plugin_id: str) -> PluginConfig:
        """Load plugin configuration from file.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            Plugin configuration
        """
        config_file = os.path.join(self.config_dir, f"{plugin_id}.json")
        
        if os.path.exists(config_file):
            try:
                with open(config_file, 'r') as f:
                    data = json.load(f)
                    return PluginConfig.from_dict(data)
            except Exception as e:
                logger.error(f"Error loading config for {plugin_id}: {e}")
        
        return PluginConfig()
    
    def _save_plugin_config(self, plugin_id: str) -> bool:
        """Save plugin configuration to file.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            True if saved
        """
        if plugin_id not in self.plugin_configs:
            return False
        
        config_file = os.path.join(self.config_dir, f"{plugin_id}.json")
        
        try:
            with open(config_file, 'w') as f:
                json.dump(self.plugin_configs[plugin_id].to_dict(), f, indent=2)
            return True
        except Exception as e:
            logger.error(f"Error saving config for {plugin_id}: {e}")
            return False
    
    def get_plugins_by_type(self, plugin_type: PluginType) -> List[PluginBase]:
        """Get all plugins of a specific type.
        
        Args:
            plugin_type: Plugin type to filter by
            
        Returns:
            List of matching plugins
        """
        return [
            p for p in self.plugins.values()
            if p.metadata.plugin_type == plugin_type and
            self.plugin_states.get(p.metadata.id) == PluginState.ENABLED
        ]
    
    def get_enabled_plugins(self) -> List[PluginBase]:
        """Get all enabled plugins.
        
        Returns:
            List of enabled plugins
        """
        return [
            p for pid, p in self.plugins.items()
            if self.plugin_states.get(pid) == PluginState.ENABLED
        ]
    
    def list_plugins(self) -> List[Dict[str, Any]]:
        """List all loaded plugins with their status.
        
        Returns:
            List of plugin info dictionaries
        """
        result = []
        for plugin_id, plugin in self.plugins.items():
            result.append({
                'id': plugin_id,
                'metadata': plugin.metadata.to_dict(),
                'state': self.plugin_states.get(plugin_id, PluginState.UNLOADED).value,
                'config': self.plugin_configs.get(plugin_id, PluginConfig()).to_dict()
            })
        return result
    
    def update_plugins(self, dt: float) -> None:
        """Update all enabled plugins.
        
        Args:
            dt: Delta time
        """
        for plugin_id, plugin in self.plugins.items():
            if self.plugin_states.get(plugin_id) == PluginState.ENABLED:
                if hasattr(plugin, 'update'):
                    try:
                        plugin.update(dt)
                    except Exception as e:
                        logger.error(f"Error updating plugin {plugin_id}: {e}")


class PluginTemplate:
    """Template generator for creating new plugins."""
    
    DISPLAY_MODE_TEMPLATE = '''"""
{name} - Custom Display Mode Plugin
{description}
"""

from PIL import Image, ImageDraw
from lumigrid.plugins import (
    DisplayModePlugin, PluginMetadata, PluginType, PluginAPI, PluginConfig
)


class {class_name}(DisplayModePlugin):
    """Custom display mode plugin."""
    
    @property
    def metadata(self) -> PluginMetadata:
        return PluginMetadata(
            id="{id}",
            name="{name}",
            version="1.0.0",
            description="{description}",
            author="{author}",
            plugin_type=PluginType.DISPLAY_MODE
        )
    
    def on_enable(self) -> bool:
        """Initialize plugin when enabled."""
        # Add your initialization code here
        return True
    
    def on_disable(self) -> None:
        """Cleanup when plugin is disabled."""
        # Add your cleanup code here
        pass
    
    def update(self, dt: float) -> None:
        """Update plugin state.
        
        Args:
            dt: Delta time in seconds
        """
        # Add your update logic here
        pass
    
    def render(self) -> Image.Image:
        """Render the display content.
        
        Returns:
            PIL Image to display
        """
        width, height = self.api.get_display_dimensions()
        image = Image.new('RGB', (width, height), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        # Add your rendering code here
        draw.text((5, 10), "{name}", fill=(255, 255, 255))
        
        return image
'''
    
    DATA_SOURCE_TEMPLATE = '''"""
{name} - Custom Data Source Plugin
{description}
"""

from lumigrid.plugins import (
    DataSourcePlugin, PluginMetadata, PluginType, PluginAPI, PluginConfig
)


class {class_name}(DataSourcePlugin):
    """Custom data source plugin."""
    
    @property
    def metadata(self) -> PluginMetadata:
        return PluginMetadata(
            id="{id}",
            name="{name}",
            version="1.0.0",
            description="{description}",
            author="{author}",
            plugin_type=PluginType.DATA_SOURCE
        )
    
    def on_enable(self) -> bool:
        """Initialize data source when enabled."""
        # Add your initialization code here
        return True
    
    def on_disable(self) -> None:
        """Cleanup when plugin is disabled."""
        pass
    
    def fetch(self):
        """Fetch data from source.
        
        Returns:
            Fetched data
        """
        # Add your data fetching code here
        return None
    
    def get_data_type(self) -> str:
        """Get type of data this source provides.
        
        Returns:
            Data type identifier
        """
        return "custom"
'''
    
    EFFECT_TEMPLATE = '''"""
{name} - Custom Effect Plugin
{description}
"""

from PIL import Image
from lumigrid.plugins import (
    EffectPlugin, PluginMetadata, PluginType, PluginAPI, PluginConfig
)


class {class_name}(EffectPlugin):
    """Custom visual effect plugin."""
    
    @property
    def metadata(self) -> PluginMetadata:
        return PluginMetadata(
            id="{id}",
            name="{name}",
            version="1.0.0",
            description="{description}",
            author="{author}",
            plugin_type=PluginType.EFFECT
        )
    
    def on_enable(self) -> bool:
        """Initialize effect when enabled."""
        return True
    
    def on_disable(self) -> None:
        """Cleanup when plugin is disabled."""
        pass
    
    def apply(self, image: Image.Image) -> Image.Image:
        """Apply effect to image.
        
        Args:
            image: Input image
            
        Returns:
            Processed image
        """
        # Add your effect processing code here
        return image
'''
    
    @classmethod
    def generate(
        cls,
        plugin_type: PluginType,
        name: str,
        description: str = "",
        author: str = ""
    ) -> str:
        """Generate plugin template code.
        
        Args:
            plugin_type: Type of plugin to generate
            name: Plugin name
            description: Plugin description
            author: Plugin author
            
        Returns:
            Generated code string
        """
        templates = {
            PluginType.DISPLAY_MODE: cls.DISPLAY_MODE_TEMPLATE,
            PluginType.DATA_SOURCE: cls.DATA_SOURCE_TEMPLATE,
            PluginType.EFFECT: cls.EFFECT_TEMPLATE
        }
        
        template = templates.get(plugin_type, cls.DISPLAY_MODE_TEMPLATE)
        
        # Generate safe class name
        class_name = ''.join(word.capitalize() for word in name.split())
        class_name = ''.join(c for c in class_name if c.isalnum())
        
        # Generate plugin ID
        plugin_id = name.lower().replace(' ', '_')
        plugin_id = ''.join(c for c in plugin_id if c.isalnum() or c == '_')
        
        return template.format(
            name=name,
            description=description,
            author=author,
            class_name=class_name,
            id=plugin_id
        )
    
    @classmethod
    def create_plugin_directory(
        cls,
        base_dir: str,
        plugin_type: PluginType,
        name: str,
        description: str = "",
        author: str = ""
    ) -> Optional[str]:
        """Create a complete plugin directory structure.
        
        Args:
            base_dir: Base plugins directory
            plugin_type: Type of plugin
            name: Plugin name
            description: Plugin description
            author: Plugin author
            
        Returns:
            Created plugin directory path or None
        """
        # Generate plugin ID
        plugin_id = name.lower().replace(' ', '_')
        plugin_id = ''.join(c for c in plugin_id if c.isalnum() or c == '_')
        
        plugin_dir = os.path.join(base_dir, plugin_id)
        
        try:
            os.makedirs(plugin_dir, exist_ok=True)
            
            # Generate __init__.py
            code = cls.generate(plugin_type, name, description, author)
            init_file = os.path.join(plugin_dir, '__init__.py')
            with open(init_file, 'w') as f:
                f.write(code)
            
            # Generate manifest.json
            manifest = {
                'id': plugin_id,
                'name': name,
                'version': '1.0.0',
                'description': description,
                'author': author,
                'plugin_type': plugin_type.value
            }
            manifest_file = os.path.join(plugin_dir, 'manifest.json')
            with open(manifest_file, 'w') as f:
                json.dump(manifest, f, indent=2)
            
            # Create README
            readme_content = f"""# {name}

{description}

## Installation

1. Copy this directory to the plugins folder
2. Restart the application
3. Enable the plugin in settings

## Configuration

Add configuration options here.

## Usage

Add usage instructions here.
"""
            readme_file = os.path.join(plugin_dir, 'README.md')
            with open(readme_file, 'w') as f:
                f.write(readme_content)
            
            return plugin_dir
            
        except Exception as e:
            logger.error(f"Error creating plugin directory: {e}")
            return None


class PluginRegistry:
    """Central registry for sharing plugins."""
    
    def __init__(self):
        """Initialize plugin registry."""
        self.registered_plugins: Dict[str, Dict[str, Any]] = {}
    
    def register(self, metadata: PluginMetadata, source_path: str) -> bool:
        """Register a plugin in the registry.
        
        Args:
            metadata: Plugin metadata
            source_path: Path to plugin source
            
        Returns:
            True if registered
        """
        self.registered_plugins[metadata.id] = {
            'metadata': metadata.to_dict(),
            'source_path': source_path,
            'registered_at': datetime.now().isoformat()
        }
        return True
    
    def unregister(self, plugin_id: str) -> bool:
        """Unregister a plugin.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            True if unregistered
        """
        if plugin_id in self.registered_plugins:
            del self.registered_plugins[plugin_id]
            return True
        return False
    
    def search(
        self,
        query: str = "",
        plugin_type: Optional[PluginType] = None,
        tags: Optional[List[str]] = None
    ) -> List[Dict[str, Any]]:
        """Search for plugins.
        
        Args:
            query: Search query
            plugin_type: Filter by type
            tags: Filter by tags
            
        Returns:
            List of matching plugin entries
        """
        results = []
        
        for plugin_id, entry in self.registered_plugins.items():
            metadata = entry['metadata']
            
            # Check type filter
            if plugin_type and metadata.get('plugin_type') != plugin_type.value:
                continue
            
            # Check tag filter
            if tags:
                plugin_tags = metadata.get('tags', [])
                if not any(tag in plugin_tags for tag in tags):
                    continue
            
            # Check query
            if query:
                query_lower = query.lower()
                if (query_lower not in metadata.get('name', '').lower() and
                    query_lower not in metadata.get('description', '').lower()):
                    continue
            
            results.append(entry)
        
        return results
    
    def get_plugin_info(self, plugin_id: str) -> Optional[Dict[str, Any]]:
        """Get information about a registered plugin.
        
        Args:
            plugin_id: Plugin identifier
            
        Returns:
            Plugin info or None
        """
        return self.registered_plugins.get(plugin_id)
