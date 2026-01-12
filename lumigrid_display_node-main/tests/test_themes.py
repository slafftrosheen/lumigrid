"""Tests for the theme system module."""

import pytest
from unittest.mock import MagicMock

from lumigrid.display.themes import (
    ThemeType,
    ColorPalette,
    AnimationSettings,
    LayoutSettings,
    AccessibilitySettings,
    Theme,
    PresetThemes,
    DynamicTheme,
    ThemeManager,
    ThemeEditor,
)


class TestColorPalette:
    """Tests for ColorPalette class."""
    
    def test_creation_defaults(self):
        """Test palette creation with defaults."""
        palette = ColorPalette()
        
        assert palette.primary is not None
        assert palette.background is not None
        assert palette.text is not None
    
    def test_creation_custom(self):
        """Test palette creation with custom colors."""
        palette = ColorPalette(
            primary=(255, 0, 0),
            secondary=(0, 255, 0),
            accent=(0, 0, 255)
        )
        
        assert palette.primary == (255, 0, 0)
        assert palette.secondary == (0, 255, 0)
        assert palette.accent == (0, 0, 255)
    
    def test_to_dict(self):
        """Test palette serialization."""
        palette = ColorPalette(primary=(255, 0, 0))
        data = palette.to_dict()
        
        assert data['primary'] == (255, 0, 0)
        assert 'secondary' in data
        assert 'background' in data
    
    def test_from_dict(self):
        """Test palette deserialization."""
        data = {
            'primary': [255, 0, 0],
            'secondary': [0, 255, 0]
        }
        palette = ColorPalette.from_dict(data)
        
        assert palette.primary == (255, 0, 0)
        assert palette.secondary == (0, 255, 0)


class TestAnimationSettings:
    """Tests for AnimationSettings class."""
    
    def test_creation_defaults(self):
        """Test animation settings with defaults."""
        settings = AnimationSettings()
        
        assert settings.transition_duration == 0.3
        assert settings.particle_enabled is False
    
    def test_creation_custom(self):
        """Test animation settings with custom values."""
        settings = AnimationSettings(
            transition_duration=0.5,
            particle_enabled=True,
            glow_enabled=True
        )
        
        assert settings.transition_duration == 0.5
        assert settings.particle_enabled is True
        assert settings.glow_enabled is True
    
    def test_to_dict(self):
        """Test animation settings serialization."""
        settings = AnimationSettings(scroll_speed=50.0)
        data = settings.to_dict()
        
        assert data['scroll_speed'] == 50.0


class TestLayoutSettings:
    """Tests for LayoutSettings class."""
    
    def test_creation_defaults(self):
        """Test layout settings with defaults."""
        settings = LayoutSettings()
        
        assert settings.padding == 2
        assert settings.alignment == "center"
    
    def test_to_dict(self):
        """Test layout settings serialization."""
        settings = LayoutSettings(padding=5, margin=10)
        data = settings.to_dict()
        
        assert data['padding'] == 5
        assert data['margin'] == 10


class TestAccessibilitySettings:
    """Tests for AccessibilitySettings class."""
    
    def test_creation_defaults(self):
        """Test accessibility settings with defaults."""
        settings = AccessibilitySettings()
        
        assert settings.high_contrast is False
        assert settings.large_text is False
        assert settings.reduced_motion is False
    
    def test_creation_high_contrast(self):
        """Test high contrast settings."""
        settings = AccessibilitySettings(
            high_contrast=True,
            large_text=True,
            reduced_motion=True
        )
        
        assert settings.high_contrast is True
        assert settings.large_text is True
        assert settings.reduced_motion is True


class TestTheme:
    """Tests for Theme class."""
    
    def test_creation_minimal(self):
        """Test theme creation with minimal args."""
        theme = Theme(name="Test Theme")
        
        assert theme.name == "Test Theme"
        assert theme.colors is not None
        assert theme.animations is not None
        assert theme.layout is not None
        assert theme.accessibility is not None
    
    def test_creation_full(self):
        """Test theme creation with all args."""
        theme = Theme(
            name="Custom",
            theme_type=ThemeType.COLOR,
            colors=ColorPalette(primary=(255, 0, 0)),
            animations=AnimationSettings(glow_enabled=True),
            layout=LayoutSettings(padding=10),
            accessibility=AccessibilitySettings(high_contrast=True),
            description="A custom theme"
        )
        
        assert theme.name == "Custom"
        assert theme.colors.primary == (255, 0, 0)
        assert theme.animations.glow_enabled is True
    
    def test_get_color(self):
        """Test getting color by name."""
        theme = Theme(
            name="Test",
            colors=ColorPalette(primary=(100, 150, 200))
        )
        
        color = theme.get_color("primary")
        
        assert color == (100, 150, 200)
    
    def test_get_color_high_contrast(self):
        """Test getting color with high contrast."""
        theme = Theme(
            name="Test",
            colors=ColorPalette(primary=(200, 200, 200)),
            accessibility=AccessibilitySettings(high_contrast=True)
        )
        
        color = theme.get_color("primary")
        
        # High contrast should make bright colors white
        assert color == (255, 255, 255)
    
    def test_get_color_brightness_boost(self):
        """Test brightness boost."""
        theme = Theme(
            name="Test",
            colors=ColorPalette(primary=(100, 100, 100)),
            accessibility=AccessibilitySettings(brightness_boost=1.5)
        )
        
        color = theme.get_color("primary")
        
        assert color[0] > 100  # Should be brighter
    
    def test_get_font_size(self):
        """Test getting font size."""
        theme = Theme(
            name="Test",
            layout=LayoutSettings(font_size_medium=12)
        )
        
        size = theme.get_font_size("medium")
        
        assert size == 12
    
    def test_get_font_size_large_text(self):
        """Test large text accessibility."""
        theme = Theme(
            name="Test",
            layout=LayoutSettings(font_size_medium=12),
            accessibility=AccessibilitySettings(large_text=True)
        )
        
        size = theme.get_font_size("medium")
        
        assert size == 18  # 12 * 1.5
    
    def test_should_animate(self):
        """Test animation check."""
        theme = Theme(name="Test")
        
        assert theme.should_animate() is True
        
        theme.accessibility.reduced_motion = True
        
        assert theme.should_animate() is False
    
    def test_to_dict(self):
        """Test theme serialization."""
        theme = Theme(
            name="Test",
            description="Test theme"
        )
        data = theme.to_dict()
        
        assert data['name'] == "Test"
        assert data['description'] == "Test theme"
        assert 'colors' in data
        assert 'animations' in data
    
    def test_from_dict(self):
        """Test theme deserialization."""
        data = {
            'name': 'Imported Theme',
            'theme_type': 'color',
            'description': 'An imported theme',
            'colors': {'primary': [255, 0, 0]},
            'animations': {'scroll_speed': 50.0},
            'layout': {'padding': 5},
            'accessibility': {'high_contrast': True}
        }
        
        theme = Theme.from_dict(data)
        
        assert theme.name == "Imported Theme"
        assert theme.colors.primary == (255, 0, 0)


class TestPresetThemes:
    """Tests for PresetThemes class."""
    
    def test_default_theme(self):
        """Test default theme."""
        theme = PresetThemes.default()
        
        assert theme.name == "Default"
    
    def test_dark_theme(self):
        """Test dark theme."""
        theme = PresetThemes.dark()
        
        assert theme.name == "Dark"
        # Dark theme should have dark background
        brightness = sum(theme.colors.background) / 3
        assert brightness < 50
    
    def test_light_theme(self):
        """Test light theme."""
        theme = PresetThemes.light()
        
        assert theme.name == "Light"
        # Light theme should have light background
        brightness = sum(theme.colors.background) / 3
        assert brightness > 200
    
    def test_neon_theme(self):
        """Test neon theme."""
        theme = PresetThemes.neon()
        
        assert theme.name == "Neon"
        assert theme.animations.glow_enabled is True
    
    def test_high_contrast_theme(self):
        """Test high contrast theme."""
        theme = PresetThemes.high_contrast()
        
        assert theme.name == "High Contrast"
        assert theme.accessibility.high_contrast is True
        assert theme.accessibility.reduced_motion is True
    
    def test_seasonal_theme(self):
        """Test seasonal theme generation."""
        theme = PresetThemes.get_seasonal_theme()
        
        assert theme is not None
        assert theme.theme_type == ThemeType.SEASONAL


class TestDynamicTheme:
    """Tests for DynamicTheme class."""
    
    def test_creation(self):
        """Test dynamic theme creation."""
        base = PresetThemes.default()
        dynamic = DynamicTheme(base)
        
        assert dynamic.base_theme == base
        assert dynamic.adapt_to_time is True
    
    def test_get_current_theme(self):
        """Test getting current adapted theme."""
        base = PresetThemes.default()
        dynamic = DynamicTheme(base)
        
        theme = dynamic.get_current_theme()
        
        assert theme is not None
        assert "Dynamic" in theme.name


class TestThemeManager:
    """Tests for ThemeManager class."""
    
    def test_creation(self):
        """Test theme manager creation."""
        manager = ThemeManager()
        
        assert len(manager.themes) > 0  # Presets registered
    
    def test_register_theme(self):
        """Test registering custom theme."""
        manager = ThemeManager()
        custom = Theme(name="My Custom Theme")
        
        manager.register_theme(custom)
        
        assert "my custom theme" in manager.themes
    
    def test_get_theme(self):
        """Test getting theme by name."""
        manager = ThemeManager()
        
        theme = manager.get_theme("default")
        
        assert theme is not None
        assert theme.name == "Default"
    
    def test_get_theme_case_insensitive(self):
        """Test case insensitive theme lookup."""
        manager = ThemeManager()
        
        theme1 = manager.get_theme("DEFAULT")
        theme2 = manager.get_theme("Default")
        theme3 = manager.get_theme("default")
        
        assert theme1 is not None
        assert theme2 is not None
        assert theme3 is not None
    
    def test_set_theme(self):
        """Test setting current theme."""
        manager = ThemeManager()
        
        result = manager.set_theme("dark")
        
        assert result is True
        assert manager.current_theme_name == "dark"
    
    def test_set_invalid_theme(self):
        """Test setting invalid theme."""
        manager = ThemeManager()
        
        result = manager.set_theme("nonexistent")
        
        assert result is False
    
    def test_get_current_theme(self):
        """Test getting current theme."""
        manager = ThemeManager()
        manager.set_theme("neon")
        
        theme = manager.get_current_theme()
        
        assert theme.name == "Neon"
    
    def test_enable_dynamic_theme(self):
        """Test enabling dynamic theme."""
        manager = ThemeManager()
        
        result = manager.enable_dynamic_theme("default")
        
        assert result is True
        assert manager.dynamic_theme is not None
    
    def test_list_themes(self):
        """Test listing themes."""
        manager = ThemeManager()
        
        themes = manager.list_themes()
        
        assert "default" in themes
        assert "dark" in themes
        assert "light" in themes
    
    def test_theme_change_callback(self):
        """Test theme change callback."""
        manager = ThemeManager()
        changes = []
        
        manager.on_theme_change(lambda theme: changes.append(theme.name))
        manager.set_theme("dark")
        
        assert len(changes) == 1
    
    def test_export_theme(self):
        """Test exporting theme."""
        manager = ThemeManager()
        
        data = manager.export_theme("default")
        
        assert data is not None
        assert data['name'] == "Default"
    
    def test_import_theme(self):
        """Test importing theme."""
        manager = ThemeManager()
        data = {
            'name': 'Imported',
            'colors': {'primary': [255, 128, 0]}
        }
        
        name = manager.import_theme(data)
        
        assert name == "Imported"
        assert manager.get_theme("imported") is not None


class TestThemeEditor:
    """Tests for ThemeEditor class."""
    
    def test_creation(self):
        """Test editor creation."""
        editor = ThemeEditor()
        
        assert editor.working_theme is not None
    
    def test_creation_with_base(self):
        """Test editor creation with base theme."""
        base = PresetThemes.neon()
        editor = ThemeEditor(base)
        
        assert editor.working_theme.name == "Neon"
    
    def test_set_name(self):
        """Test setting theme name."""
        editor = ThemeEditor()
        
        editor.set_name("My Theme")
        
        assert editor.working_theme.name == "My Theme"
    
    def test_set_color(self):
        """Test setting color."""
        editor = ThemeEditor()
        
        result = editor.set_color("primary", (255, 0, 0))
        
        assert result is True
        assert editor.working_theme.colors.primary == (255, 0, 0)
    
    def test_set_invalid_color(self):
        """Test setting invalid color name."""
        editor = ThemeEditor()
        
        result = editor.set_color("nonexistent", (255, 0, 0))
        
        assert result is False
    
    def test_undo(self):
        """Test undo functionality."""
        editor = ThemeEditor()
        editor.set_name("First")
        editor.set_name("Second")
        
        result = editor.undo()
        
        assert result is True
        assert editor.working_theme.name == "First"
    
    def test_redo(self):
        """Test redo functionality."""
        editor = ThemeEditor()
        editor.set_name("First")
        editor.set_name("Second")
        editor.undo()
        
        result = editor.redo()
        
        assert result is True
        assert editor.working_theme.name == "Second"
    
    def test_generate_complementary_palette(self):
        """Test complementary palette generation."""
        editor = ThemeEditor()
        
        editor.generate_complementary_palette((100, 150, 200))
        
        assert editor.working_theme.colors.primary == (100, 150, 200)
        # Accent should be complementary
        assert editor.working_theme.colors.accent != (100, 150, 200)
    
    def test_save(self):
        """Test saving theme."""
        editor = ThemeEditor()
        editor.set_name("Saved Theme")
        
        theme = editor.save()
        
        assert theme.name == "Saved Theme"
    
    def test_reset(self):
        """Test resetting editor."""
        editor = ThemeEditor()
        editor.set_name("Modified")
        
        editor.reset()
        
        assert editor.working_theme.name == "Default"
