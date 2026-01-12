"""Theme system for Lumigrid display.

This module provides a comprehensive theme system including:
- Color themes with customizable palettes
- Animation themes with preset effect sets
- Layout themes with different visual styles
- Accessibility themes for visual impairments
- Seasonal themes that change automatically
- User preference themes for personalization
- Dynamic themes that adapt to content
"""

import logging
import math
import time
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)


class ThemeType(Enum):
    """Types of themes available."""
    COLOR = "color"
    ANIMATION = "animation"
    LAYOUT = "layout"
    ACCESSIBILITY = "accessibility"
    SEASONAL = "seasonal"
    USER_PREFERENCE = "user_preference"
    DYNAMIC = "dynamic"


@dataclass
class ColorPalette:
    """Color palette definition.
    
    Attributes:
        primary: Primary color
        secondary: Secondary color
        accent: Accent color
        background: Background color
        surface: Surface/card color
        text: Primary text color
        text_secondary: Secondary text color
        error: Error color
        warning: Warning color
        success: Success color
        info: Info color
    """
    primary: Tuple[int, int, int] = (0, 150, 255)
    secondary: Tuple[int, int, int] = (100, 100, 200)
    accent: Tuple[int, int, int] = (255, 100, 100)
    background: Tuple[int, int, int] = (0, 0, 0)
    surface: Tuple[int, int, int] = (30, 30, 30)
    text: Tuple[int, int, int] = (255, 255, 255)
    text_secondary: Tuple[int, int, int] = (180, 180, 180)
    error: Tuple[int, int, int] = (255, 50, 50)
    warning: Tuple[int, int, int] = (255, 200, 0)
    success: Tuple[int, int, int] = (50, 255, 100)
    info: Tuple[int, int, int] = (50, 150, 255)
    
    def to_dict(self) -> Dict[str, Tuple[int, int, int]]:
        """Convert palette to dictionary.
        
        Returns:
            Dictionary of color names to RGB tuples
        """
        return {
            'primary': self.primary,
            'secondary': self.secondary,
            'accent': self.accent,
            'background': self.background,
            'surface': self.surface,
            'text': self.text,
            'text_secondary': self.text_secondary,
            'error': self.error,
            'warning': self.warning,
            'success': self.success,
            'info': self.info
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'ColorPalette':
        """Create palette from dictionary.
        
        Args:
            data: Dictionary data
            
        Returns:
            ColorPalette instance
        """
        return cls(**{k: tuple(v) if isinstance(v, list) else v 
                     for k, v in data.items() if hasattr(cls, k)})


@dataclass
class AnimationSettings:
    """Animation settings for a theme.
    
    Attributes:
        transition_duration: Default transition duration
        transition_easing: Default easing function
        entrance_effect: Default entrance animation
        exit_effect: Default exit animation
        scroll_speed: Text scroll speed
        particle_enabled: Enable particle effects
        glow_enabled: Enable glow effects
    """
    transition_duration: float = 0.3
    transition_easing: str = "ease_out"
    entrance_effect: str = "fade_in"
    exit_effect: str = "fade_out"
    scroll_speed: float = 30.0
    particle_enabled: bool = False
    glow_enabled: bool = False
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            'transition_duration': self.transition_duration,
            'transition_easing': self.transition_easing,
            'entrance_effect': self.entrance_effect,
            'exit_effect': self.exit_effect,
            'scroll_speed': self.scroll_speed,
            'particle_enabled': self.particle_enabled,
            'glow_enabled': self.glow_enabled
        }


@dataclass
class LayoutSettings:
    """Layout settings for a theme.
    
    Attributes:
        padding: Default padding
        margin: Default margin
        border_radius: Default border radius
        font_size_small: Small font size
        font_size_medium: Medium font size
        font_size_large: Large font size
        alignment: Default text alignment
        spacing: Element spacing
    """
    padding: int = 2
    margin: int = 2
    border_radius: int = 0
    font_size_small: int = 8
    font_size_medium: int = 12
    font_size_large: int = 16
    alignment: str = "center"  # left, center, right
    spacing: int = 2
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            'padding': self.padding,
            'margin': self.margin,
            'border_radius': self.border_radius,
            'font_size_small': self.font_size_small,
            'font_size_medium': self.font_size_medium,
            'font_size_large': self.font_size_large,
            'alignment': self.alignment,
            'spacing': self.spacing
        }


@dataclass
class AccessibilitySettings:
    """Accessibility settings.
    
    Attributes:
        high_contrast: Enable high contrast mode
        large_text: Use larger text
        reduced_motion: Reduce animations
        color_blind_mode: Color blind mode type
        brightness_boost: Brightness multiplier
    """
    high_contrast: bool = False
    large_text: bool = False
    reduced_motion: bool = False
    color_blind_mode: str = "none"  # none, protanopia, deuteranopia, tritanopia
    brightness_boost: float = 1.0
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            'high_contrast': self.high_contrast,
            'large_text': self.large_text,
            'reduced_motion': self.reduced_motion,
            'color_blind_mode': self.color_blind_mode,
            'brightness_boost': self.brightness_boost
        }


class Theme:
    """Complete theme definition."""
    
    def __init__(
        self,
        name: str,
        theme_type: ThemeType = ThemeType.COLOR,
        colors: Optional[ColorPalette] = None,
        animations: Optional[AnimationSettings] = None,
        layout: Optional[LayoutSettings] = None,
        accessibility: Optional[AccessibilitySettings] = None,
        description: str = ""
    ):
        """Initialize theme.
        
        Args:
            name: Theme name
            theme_type: Theme type
            colors: Color palette
            animations: Animation settings
            layout: Layout settings
            accessibility: Accessibility settings
            description: Theme description
        """
        self.name = name
        self.theme_type = theme_type
        self.colors = colors or ColorPalette()
        self.animations = animations or AnimationSettings()
        self.layout = layout or LayoutSettings()
        self.accessibility = accessibility or AccessibilitySettings()
        self.description = description
    
    def get_color(self, name: str) -> Tuple[int, int, int]:
        """Get a color by name.
        
        Args:
            name: Color name
            
        Returns:
            RGB color tuple
        """
        palette_dict = self.colors.to_dict()
        color = palette_dict.get(name, self.colors.text)
        
        # Apply accessibility adjustments
        if self.accessibility.high_contrast:
            color = self._apply_high_contrast(color)
        
        if self.accessibility.color_blind_mode != "none":
            color = self._apply_color_blind_correction(color)
        
        if self.accessibility.brightness_boost != 1.0:
            color = tuple(min(255, int(c * self.accessibility.brightness_boost)) for c in color)
        
        return color
    
    def _apply_high_contrast(self, color: Tuple[int, int, int]) -> Tuple[int, int, int]:
        """Apply high contrast adjustment.
        
        Args:
            color: Original color
            
        Returns:
            Adjusted color
        """
        # Increase saturation and brightness
        brightness = sum(color) / 3
        if brightness > 127:
            return (255, 255, 255)
        return color
    
    def _apply_color_blind_correction(self, color: Tuple[int, int, int]) -> Tuple[int, int, int]:
        """Apply color blind correction.
        
        Args:
            color: Original color
            
        Returns:
            Corrected color
        """
        r, g, b = color
        mode = self.accessibility.color_blind_mode
        
        if mode == "protanopia":
            # Red-blind correction
            new_r = int(0.567 * r + 0.433 * g)
            new_g = int(0.558 * r + 0.442 * g)
            new_b = int(0.242 * g + 0.758 * b)
            return (new_r, new_g, new_b)
        
        elif mode == "deuteranopia":
            # Green-blind correction
            new_r = int(0.625 * r + 0.375 * g)
            new_g = int(0.7 * r + 0.3 * g)
            new_b = int(0.3 * g + 0.7 * b)
            return (new_r, new_g, new_b)
        
        elif mode == "tritanopia":
            # Blue-blind correction
            new_r = int(0.95 * r + 0.05 * g)
            new_g = int(0.433 * g + 0.567 * b)
            new_b = int(0.475 * g + 0.525 * b)
            return (new_r, new_g, new_b)
        
        return color
    
    def get_font_size(self, size_name: str = "medium") -> int:
        """Get font size by name.
        
        Args:
            size_name: Size name (small, medium, large)
            
        Returns:
            Font size in pixels
        """
        size = getattr(self.layout, f'font_size_{size_name}', self.layout.font_size_medium)
        
        if self.accessibility.large_text:
            size = int(size * 1.5)
        
        return size
    
    def should_animate(self) -> bool:
        """Check if animations should be shown.
        
        Returns:
            True if animations are enabled
        """
        return not self.accessibility.reduced_motion
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert theme to dictionary.
        
        Returns:
            Theme as dictionary
        """
        return {
            'name': self.name,
            'theme_type': self.theme_type.value,
            'colors': self.colors.to_dict(),
            'animations': self.animations.to_dict(),
            'layout': self.layout.to_dict(),
            'accessibility': self.accessibility.to_dict(),
            'description': self.description
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'Theme':
        """Create theme from dictionary.
        
        Args:
            data: Dictionary data
            
        Returns:
            Theme instance
        """
        return cls(
            name=data.get('name', 'Unnamed'),
            theme_type=ThemeType(data.get('theme_type', 'color')),
            colors=ColorPalette.from_dict(data.get('colors', {})),
            animations=AnimationSettings(**data.get('animations', {})),
            layout=LayoutSettings(**data.get('layout', {})),
            accessibility=AccessibilitySettings(**data.get('accessibility', {})),
            description=data.get('description', '')
        )


class PresetThemes:
    """Collection of preset themes."""
    
    @staticmethod
    def default() -> Theme:
        """Get default theme."""
        return Theme(
            name="Default",
            description="Default Lumigrid theme"
        )
    
    @staticmethod
    def dark() -> Theme:
        """Get dark theme."""
        return Theme(
            name="Dark",
            colors=ColorPalette(
                primary=(100, 150, 255),
                secondary=(80, 80, 120),
                accent=(255, 100, 150),
                background=(0, 0, 0),
                surface=(20, 20, 30),
                text=(220, 220, 220),
                text_secondary=(150, 150, 160)
            ),
            description="Dark mode theme"
        )
    
    @staticmethod
    def light() -> Theme:
        """Get light theme."""
        return Theme(
            name="Light",
            colors=ColorPalette(
                primary=(0, 100, 200),
                secondary=(100, 100, 150),
                accent=(200, 50, 100),
                background=(240, 240, 250),
                surface=(255, 255, 255),
                text=(30, 30, 40),
                text_secondary=(100, 100, 110)
            ),
            description="Light mode theme"
        )
    
    @staticmethod
    def neon() -> Theme:
        """Get neon theme."""
        return Theme(
            name="Neon",
            colors=ColorPalette(
                primary=(0, 255, 255),
                secondary=(255, 0, 255),
                accent=(255, 255, 0),
                background=(0, 0, 20),
                surface=(20, 0, 40),
                text=(255, 255, 255),
                text_secondary=(200, 200, 255)
            ),
            animations=AnimationSettings(
                glow_enabled=True,
                particle_enabled=True
            ),
            description="Vibrant neon theme"
        )
    
    @staticmethod
    def retro() -> Theme:
        """Get retro theme."""
        return Theme(
            name="Retro",
            colors=ColorPalette(
                primary=(0, 255, 0),
                secondary=(0, 200, 0),
                accent=(255, 100, 0),
                background=(0, 20, 0),
                surface=(0, 40, 0),
                text=(0, 255, 0),
                text_secondary=(0, 180, 0)
            ),
            description="Retro green terminal theme"
        )
    
    @staticmethod
    def high_contrast() -> Theme:
        """Get high contrast accessibility theme."""
        return Theme(
            name="High Contrast",
            theme_type=ThemeType.ACCESSIBILITY,
            colors=ColorPalette(
                primary=(255, 255, 255),
                secondary=(255, 255, 0),
                accent=(255, 0, 255),
                background=(0, 0, 0),
                surface=(0, 0, 0),
                text=(255, 255, 255),
                text_secondary=(255, 255, 255)
            ),
            accessibility=AccessibilitySettings(
                high_contrast=True,
                large_text=True,
                reduced_motion=True
            ),
            description="High contrast accessibility theme"
        )
    
    @staticmethod
    def get_seasonal_theme() -> Theme:
        """Get theme based on current season."""
        now = datetime.now()
        month = now.month
        
        if month in (12, 1, 2):
            # Winter
            return Theme(
                name="Winter",
                theme_type=ThemeType.SEASONAL,
                colors=ColorPalette(
                    primary=(100, 150, 255),
                    secondary=(200, 220, 255),
                    accent=(255, 100, 100),
                    background=(10, 20, 40),
                    surface=(20, 40, 60),
                    text=(230, 240, 255)
                ),
                description="Winter seasonal theme"
            )
        
        elif month in (3, 4, 5):
            # Spring
            return Theme(
                name="Spring",
                theme_type=ThemeType.SEASONAL,
                colors=ColorPalette(
                    primary=(100, 255, 150),
                    secondary=(255, 200, 255),
                    accent=(255, 255, 100),
                    background=(20, 40, 20),
                    surface=(30, 60, 30),
                    text=(220, 255, 220)
                ),
                description="Spring seasonal theme"
            )
        
        elif month in (6, 7, 8):
            # Summer
            return Theme(
                name="Summer",
                theme_type=ThemeType.SEASONAL,
                colors=ColorPalette(
                    primary=(255, 200, 50),
                    secondary=(255, 150, 50),
                    accent=(50, 200, 255),
                    background=(40, 30, 10),
                    surface=(60, 50, 20),
                    text=(255, 240, 200)
                ),
                description="Summer seasonal theme"
            )
        
        else:
            # Autumn
            return Theme(
                name="Autumn",
                theme_type=ThemeType.SEASONAL,
                colors=ColorPalette(
                    primary=(255, 150, 50),
                    secondary=(200, 100, 50),
                    accent=(255, 50, 50),
                    background=(30, 20, 10),
                    surface=(50, 30, 15),
                    text=(255, 220, 180)
                ),
                description="Autumn seasonal theme"
            )


class DynamicTheme:
    """Theme that adapts based on content or time."""
    
    def __init__(
        self,
        base_theme: Theme,
        adapt_to_time: bool = True,
        adapt_to_content: bool = False
    ):
        """Initialize dynamic theme.
        
        Args:
            base_theme: Base theme to adapt
            adapt_to_time: Adapt colors based on time of day
            adapt_to_content: Adapt based on displayed content
        """
        self.base_theme = base_theme
        self.adapt_to_time = adapt_to_time
        self.adapt_to_content = adapt_to_content
        
        self._current_colors: Optional[ColorPalette] = None
    
    def _get_time_adjustment(self) -> Tuple[float, float, float]:
        """Get color adjustment based on time of day.
        
        Returns:
            RGB multipliers
        """
        now = datetime.now()
        hour = now.hour + now.minute / 60
        
        if 6 <= hour < 9:
            # Morning - warm tones
            return (1.0, 0.95, 0.85)
        elif 9 <= hour < 17:
            # Day - neutral
            return (1.0, 1.0, 1.0)
        elif 17 <= hour < 20:
            # Evening - warmer
            return (1.0, 0.9, 0.8)
        else:
            # Night - cooler, dimmer
            return (0.8, 0.85, 1.0)
    
    def get_current_theme(self) -> Theme:
        """Get current adapted theme.
        
        Returns:
            Adapted theme
        """
        theme = Theme(
            name=f"{self.base_theme.name} (Dynamic)",
            theme_type=ThemeType.DYNAMIC,
            colors=self.base_theme.colors,
            animations=self.base_theme.animations,
            layout=self.base_theme.layout,
            accessibility=self.base_theme.accessibility
        )
        
        if self.adapt_to_time:
            r_mult, g_mult, b_mult = self._get_time_adjustment()
            
            # Apply to all colors in palette
            original = self.base_theme.colors.to_dict()
            adjusted = {}
            
            for name, color in original.items():
                adjusted[name] = (
                    min(255, int(color[0] * r_mult)),
                    min(255, int(color[1] * g_mult)),
                    min(255, int(color[2] * b_mult))
                )
            
            theme.colors = ColorPalette.from_dict(adjusted)
        
        return theme


class ThemeManager:
    """Manages themes and theme switching."""
    
    def __init__(self):
        """Initialize theme manager."""
        self.themes: Dict[str, Theme] = {}
        self.current_theme_name: str = "default"
        self.dynamic_theme: Optional[DynamicTheme] = None
        
        self._callbacks: List[Callable] = []
        self._transition_progress = 1.0
        self._from_theme: Optional[Theme] = None
        self._to_theme: Optional[Theme] = None
        
        # Register preset themes
        self._register_presets()
    
    def _register_presets(self) -> None:
        """Register preset themes."""
        self.register_theme(PresetThemes.default())
        self.register_theme(PresetThemes.dark())
        self.register_theme(PresetThemes.light())
        self.register_theme(PresetThemes.neon())
        self.register_theme(PresetThemes.retro())
        self.register_theme(PresetThemes.high_contrast())
    
    def register_theme(self, theme: Theme) -> None:
        """Register a theme.
        
        Args:
            theme: Theme to register
        """
        self.themes[theme.name.lower()] = theme
    
    def unregister_theme(self, name: str) -> bool:
        """Unregister a theme.
        
        Args:
            name: Theme name
            
        Returns:
            True if removed
        """
        name_lower = name.lower()
        if name_lower in self.themes and name_lower != "default":
            del self.themes[name_lower]
            return True
        return False
    
    def get_theme(self, name: str) -> Optional[Theme]:
        """Get a theme by name.
        
        Args:
            name: Theme name
            
        Returns:
            Theme or None
        """
        return self.themes.get(name.lower())
    
    def get_current_theme(self) -> Theme:
        """Get the current active theme.
        
        Returns:
            Current theme
        """
        if self.dynamic_theme:
            return self.dynamic_theme.get_current_theme()
        
        return self.themes.get(self.current_theme_name, PresetThemes.default())
    
    def set_theme(self, name: str, animate: bool = True) -> bool:
        """Set the current theme.
        
        Args:
            name: Theme name
            animate: Animate transition
            
        Returns:
            True if theme was set
        """
        name_lower = name.lower()
        if name_lower not in self.themes:
            return False
        
        if animate:
            self._from_theme = self.get_current_theme()
            self._to_theme = self.themes[name_lower]
            self._transition_progress = 0
        
        self.current_theme_name = name_lower
        self.dynamic_theme = None
        
        self._notify_change()
        return True
    
    def enable_dynamic_theme(self, base_theme_name: str = "default") -> bool:
        """Enable dynamic theme adaptation.
        
        Args:
            base_theme_name: Base theme name
            
        Returns:
            True if enabled
        """
        base_theme = self.get_theme(base_theme_name)
        if not base_theme:
            return False
        
        self.dynamic_theme = DynamicTheme(base_theme)
        self._notify_change()
        return True
    
    def disable_dynamic_theme(self) -> None:
        """Disable dynamic theme."""
        self.dynamic_theme = None
        self._notify_change()
    
    def enable_seasonal_theme(self) -> None:
        """Enable seasonal theme."""
        seasonal = PresetThemes.get_seasonal_theme()
        self.register_theme(seasonal)
        self.set_theme(seasonal.name)
    
    def on_theme_change(self, callback: Callable) -> None:
        """Register theme change callback.
        
        Args:
            callback: Function to call on theme change
        """
        self._callbacks.append(callback)
    
    def _notify_change(self) -> None:
        """Notify callbacks of theme change."""
        theme = self.get_current_theme()
        for callback in self._callbacks:
            try:
                callback(theme)
            except Exception as e:
                logger.error(f"Error in theme change callback: {e}")
    
    def update(self, dt: float) -> None:
        """Update theme transitions.
        
        Args:
            dt: Delta time
        """
        if self._transition_progress < 1.0:
            self._transition_progress = min(1.0, self._transition_progress + dt * 2)
    
    def get_interpolated_color(self, color_name: str) -> Tuple[int, int, int]:
        """Get color with transition interpolation.
        
        Args:
            color_name: Color name
            
        Returns:
            RGB color tuple
        """
        if self._transition_progress >= 1.0 or not self._from_theme or not self._to_theme:
            return self.get_current_theme().get_color(color_name)
        
        from_color = self._from_theme.get_color(color_name)
        to_color = self._to_theme.get_color(color_name)
        
        t = self._transition_progress
        return (
            int(from_color[0] + (to_color[0] - from_color[0]) * t),
            int(from_color[1] + (to_color[1] - from_color[1]) * t),
            int(from_color[2] + (to_color[2] - from_color[2]) * t)
        )
    
    def list_themes(self) -> List[str]:
        """List all registered theme names.
        
        Returns:
            List of theme names
        """
        return list(self.themes.keys())
    
    def export_theme(self, name: str) -> Optional[Dict[str, Any]]:
        """Export a theme to dictionary.
        
        Args:
            name: Theme name
            
        Returns:
            Theme dictionary or None
        """
        theme = self.get_theme(name)
        if theme:
            return theme.to_dict()
        return None
    
    def import_theme(self, data: Dict[str, Any]) -> Optional[str]:
        """Import a theme from dictionary.
        
        Args:
            data: Theme dictionary
            
        Returns:
            Imported theme name or None
        """
        try:
            theme = Theme.from_dict(data)
            self.register_theme(theme)
            return theme.name
        except Exception as e:
            logger.error(f"Error importing theme: {e}")
            return None


class ThemeEditor:
    """Editor for creating and modifying themes."""
    
    def __init__(self, base_theme: Optional[Theme] = None):
        """Initialize theme editor.
        
        Args:
            base_theme: Theme to start editing from
        """
        self.working_theme = base_theme or PresetThemes.default()
        self._history: List[Dict[str, Any]] = []
        self._redo_stack: List[Dict[str, Any]] = []
        # Save initial state
        self._history.append(self.working_theme.to_dict())
    
    def _save_state(self) -> None:
        """Save current state to history."""
        self._history.append(self.working_theme.to_dict())
        self._redo_stack.clear()
        
        # Limit history size
        if len(self._history) > 50:
            self._history = self._history[-50:]
    
    def undo(self) -> bool:
        """Undo last change.
        
        Returns:
            True if undone
        """
        if len(self._history) > 1:
            self._redo_stack.append(self._history.pop())
            self.working_theme = Theme.from_dict(self._history[-1])
            return True
        return False
    
    def redo(self) -> bool:
        """Redo last undone change.
        
        Returns:
            True if redone
        """
        if self._redo_stack:
            state = self._redo_stack.pop()
            self._history.append(state)
            self.working_theme = Theme.from_dict(state)
            return True
        return False
    
    def set_name(self, name: str) -> None:
        """Set theme name.
        
        Args:
            name: New name
        """
        self.working_theme.name = name
        self._save_state()
    
    def set_color(self, color_name: str, color: Tuple[int, int, int]) -> bool:
        """Set a color in the palette.
        
        Args:
            color_name: Color name
            color: RGB color tuple
            
        Returns:
            True if color was set
        """
        if not hasattr(self.working_theme.colors, color_name):
            return False
        
        setattr(self.working_theme.colors, color_name, color)
        self._save_state()
        return True
    
    def set_animation_setting(self, setting_name: str, value: Any) -> bool:
        """Set an animation setting.
        
        Args:
            setting_name: Setting name
            value: Setting value
            
        Returns:
            True if setting was set
        """
        if not hasattr(self.working_theme.animations, setting_name):
            return False
        
        setattr(self.working_theme.animations, setting_name, value)
        self._save_state()
        return True
    
    def set_layout_setting(self, setting_name: str, value: Any) -> bool:
        """Set a layout setting.
        
        Args:
            setting_name: Setting name
            value: Setting value
            
        Returns:
            True if setting was set
        """
        if not hasattr(self.working_theme.layout, setting_name):
            return False
        
        setattr(self.working_theme.layout, setting_name, value)
        self._save_state()
        return True
    
    def set_accessibility_setting(self, setting_name: str, value: Any) -> bool:
        """Set an accessibility setting.
        
        Args:
            setting_name: Setting name
            value: Setting value
            
        Returns:
            True if setting was set
        """
        if not hasattr(self.working_theme.accessibility, setting_name):
            return False
        
        setattr(self.working_theme.accessibility, setting_name, value)
        self._save_state()
        return True
    
    def generate_complementary_palette(self, base_color: Tuple[int, int, int]) -> None:
        """Generate a complementary color palette from base color.
        
        Args:
            base_color: Base color to generate from
        """
        # Convert to HSV-like representation
        r, g, b = base_color
        max_c = max(r, g, b)
        min_c = min(r, g, b)
        
        # Generate complementary colors
        complementary = (255 - r, 255 - g, 255 - b)
        
        # Generate lighter/darker versions
        lighter = (min(255, r + 50), min(255, g + 50), min(255, b + 50))
        darker = (max(0, r - 50), max(0, g - 50), max(0, b - 50))
        
        self.working_theme.colors.primary = base_color
        self.working_theme.colors.secondary = darker
        self.working_theme.colors.accent = complementary
        self.working_theme.colors.text = lighter
        self._save_state()
    
    def preview(self) -> Theme:
        """Get current working theme for preview.
        
        Returns:
            Working theme
        """
        return self.working_theme
    
    def save(self) -> Theme:
        """Save and return the edited theme.
        
        Returns:
            Completed theme
        """
        return Theme.from_dict(self.working_theme.to_dict())
    
    def reset(self, base_theme: Optional[Theme] = None) -> None:
        """Reset editor to base theme.
        
        Args:
            base_theme: Theme to reset to
        """
        self.working_theme = base_theme or PresetThemes.default()
        self._history.clear()
        self._redo_stack.clear()
        self._save_state()
