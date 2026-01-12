"""Tests for the effects module."""

import pytest
from PIL import Image

from lumigrid.display.effects import (
    EffectsManager,
    EffectType,
    BackgroundType,
    GradientGenerator,
    ColorStop,
    PlasmaEffect,
    MatrixRainEffect,
    StarfieldEffect,
    FireEffect,
    WaveEffect,
    GeometricPattern,
    TimeBased,
    create_glow_effect,
    apply_color_transform,
)


class TestColorStop:
    """Tests for ColorStop class."""
    
    def test_color_stop_creation(self):
        """Test creating a color stop."""
        stop = ColorStop(position=0.5, color=(255, 128, 0))
        assert stop.position == 0.5
        assert stop.color == (255, 128, 0)


class TestGradientGenerator:
    """Tests for GradientGenerator class."""
    
    def test_linear_gradient_basic(self):
        """Test basic linear gradient generation."""
        stops = [
            ColorStop(0, (0, 0, 0)),
            ColorStop(1, (255, 255, 255))
        ]
        image = GradientGenerator.linear_gradient(64, 32, stops)
        
        assert image.size == (64, 32)
        assert image.mode == 'RGB'
    
    def test_linear_gradient_angle(self):
        """Test linear gradient with different angles."""
        stops = [
            ColorStop(0, (255, 0, 0)),
            ColorStop(1, (0, 0, 255))
        ]
        
        # Horizontal gradient
        img1 = GradientGenerator.linear_gradient(64, 32, stops, angle=0)
        assert img1.size == (64, 32)
        
        # Vertical gradient
        img2 = GradientGenerator.linear_gradient(64, 32, stops, angle=90)
        assert img2.size == (64, 32)
    
    def test_linear_gradient_empty_stops(self):
        """Test linear gradient with no color stops."""
        image = GradientGenerator.linear_gradient(64, 32, [])
        assert image.size == (64, 32)
    
    def test_radial_gradient_basic(self):
        """Test basic radial gradient generation."""
        stops = [
            ColorStop(0, (255, 255, 255)),  # Center
            ColorStop(1, (0, 0, 0))         # Edge
        ]
        image = GradientGenerator.radial_gradient(64, 32, stops)
        
        assert image.size == (64, 32)
        assert image.mode == 'RGB'
    
    def test_radial_gradient_custom_center(self):
        """Test radial gradient with custom center."""
        stops = [
            ColorStop(0, (255, 0, 0)),
            ColorStop(1, (0, 0, 255))
        ]
        image = GradientGenerator.radial_gradient(64, 32, stops, 
                                                   center_x=0.25, center_y=0.25)
        assert image.size == (64, 32)


class TestPlasmaEffect:
    """Tests for PlasmaEffect class."""
    
    def test_plasma_creation(self):
        """Test plasma effect creation."""
        plasma = PlasmaEffect(width=64, height=32)
        assert plasma.width == 64
        assert plasma.height == 32
    
    def test_plasma_custom_palette(self):
        """Test plasma with custom color palette."""
        colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
        plasma = PlasmaEffect(width=64, height=32, color_palette=colors)
        assert plasma.color_palette == colors
    
    def test_plasma_update(self):
        """Test plasma animation update."""
        plasma = PlasmaEffect(width=64, height=32)
        initial_time = plasma._time
        plasma.update(dt=0.1)
        assert plasma._time > initial_time
    
    def test_plasma_render(self):
        """Test plasma rendering."""
        plasma = PlasmaEffect(width=64, height=32)
        image = plasma.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestMatrixRainEffect:
    """Tests for MatrixRainEffect class."""
    
    def test_matrix_creation(self):
        """Test matrix rain creation."""
        matrix = MatrixRainEffect(width=64, height=32)
        assert matrix.width == 64
        assert matrix.height == 32
    
    def test_matrix_custom_color(self):
        """Test matrix rain with custom color."""
        matrix = MatrixRainEffect(width=64, height=32, color=(0, 200, 100))
        assert matrix.color == (0, 200, 100)
    
    def test_matrix_update(self):
        """Test matrix rain animation update."""
        matrix = MatrixRainEffect(width=64, height=32, density=0.5)
        initial_positions = [c['y'] for c in matrix.columns] if matrix.columns else []
        
        matrix.update(dt=0.1)
        
        # Positions should have changed
        if matrix.columns and initial_positions:
            new_positions = [c['y'] for c in matrix.columns]
            assert new_positions != initial_positions
    
    def test_matrix_render(self):
        """Test matrix rain rendering."""
        matrix = MatrixRainEffect(width=64, height=32)
        image = matrix.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestStarfieldEffect:
    """Tests for StarfieldEffect class."""
    
    def test_starfield_creation(self):
        """Test starfield creation."""
        starfield = StarfieldEffect(width=64, height=32, num_stars=20)
        assert starfield.width == 64
        assert starfield.height == 32
        assert len(starfield.stars) == 20
    
    def test_starfield_update(self):
        """Test starfield animation update."""
        starfield = StarfieldEffect(width=64, height=32, num_stars=5)
        initial_brightness = [s['brightness'] for s in starfield.stars]
        
        starfield.update(dt=0.5)
        
        # Some brightness values should have changed
        new_brightness = [s['brightness'] for s in starfield.stars]
        assert new_brightness != initial_brightness
    
    def test_starfield_render(self):
        """Test starfield rendering."""
        starfield = StarfieldEffect(width=64, height=32)
        image = starfield.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestFireEffect:
    """Tests for FireEffect class."""
    
    def test_fire_creation(self):
        """Test fire effect creation."""
        fire = FireEffect(width=64, height=32)
        assert fire.width == 64
        assert fire.height == 32
    
    def test_fire_palette_generation(self):
        """Test fire color palette."""
        fire = FireEffect(width=64, height=32)
        assert len(fire.palette) == 256
        
        # First color should be dark/black
        assert fire.palette[0][0] == 0  # No red at start
        
        # Last color should be bright
        assert fire.palette[255][0] == 255  # Full red
        assert fire.palette[255][1] == 255  # Full green
        assert fire.palette[255][2] == 255  # Full blue (white)
    
    def test_fire_update(self):
        """Test fire animation update."""
        fire = FireEffect(width=64, height=32)
        fire.update(dt=0.1)
        
        # Bottom row should have some heat
        has_heat = any(h > 0 for h in fire.heat[-1])
        assert has_heat
    
    def test_fire_render(self):
        """Test fire rendering."""
        fire = FireEffect(width=64, height=32)
        fire.update(dt=0.1)
        image = fire.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestWaveEffect:
    """Tests for WaveEffect class."""
    
    def test_wave_creation(self):
        """Test wave effect creation."""
        wave = WaveEffect(width=64, height=32)
        assert wave.width == 64
        assert wave.height == 32
    
    def test_wave_custom_colors(self):
        """Test wave with custom colors."""
        wave = WaveEffect(
            width=64, height=32,
            color1=(255, 0, 0),
            color2=(0, 0, 255)
        )
        assert wave.color1 == (255, 0, 0)
        assert wave.color2 == (0, 0, 255)
    
    def test_wave_update(self):
        """Test wave animation update."""
        wave = WaveEffect(width=64, height=32)
        initial_time = wave._time
        wave.update(dt=0.1)
        assert wave._time > initial_time
    
    def test_wave_render(self):
        """Test wave rendering."""
        wave = WaveEffect(width=64, height=32)
        image = wave.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestGeometricPattern:
    """Tests for GeometricPattern class."""
    
    def test_geometric_creation(self):
        """Test geometric pattern creation."""
        geo = GeometricPattern(width=64, height=32)
        assert geo.width == 64
        assert geo.height == 32
    
    def test_geometric_checkerboard(self):
        """Test checkerboard pattern."""
        geo = GeometricPattern(width=64, height=32, pattern_type="checkerboard")
        image = geo.render()
        assert image.size == (64, 32)
    
    def test_geometric_stripes(self):
        """Test stripes pattern."""
        geo = GeometricPattern(width=64, height=32, pattern_type="stripes")
        image = geo.render()
        assert image.size == (64, 32)
    
    def test_geometric_circles(self):
        """Test circles pattern."""
        geo = GeometricPattern(width=64, height=32, pattern_type="circles")
        image = geo.render()
        assert image.size == (64, 32)
    
    def test_geometric_update(self):
        """Test geometric animation update."""
        geo = GeometricPattern(width=64, height=32)
        initial_time = geo._time
        geo.update(dt=0.1)
        assert geo._time > initial_time


class TestTimeBased:
    """Tests for TimeBased background."""
    
    def test_time_based_creation(self):
        """Test time-based background creation."""
        tb = TimeBased(width=64, height=32)
        assert tb.width == 64
        assert tb.height == 32
    
    def test_time_based_has_time_colors(self):
        """Test time-based has colors defined for different hours."""
        tb = TimeBased(width=64, height=32)
        assert len(tb.time_colors) > 0
        assert 0 in tb.time_colors  # Midnight
        assert 12 in tb.time_colors  # Noon
    
    def test_time_based_get_colors(self):
        """Test getting current time colors."""
        tb = TimeBased(width=64, height=32)
        top, bottom = tb.get_current_colors()
        
        assert len(top) == 3
        assert len(bottom) == 3
        assert all(0 <= c <= 255 for c in top)
        assert all(0 <= c <= 255 for c in bottom)
    
    def test_time_based_render(self):
        """Test time-based rendering."""
        tb = TimeBased(width=64, height=32)
        image = tb.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestEffectsManager:
    """Tests for EffectsManager class."""
    
    def test_manager_creation(self):
        """Test effects manager creation."""
        manager = EffectsManager(width=64, height=32)
        assert manager.width == 64
        assert manager.height == 32
    
    def test_set_solid_background(self):
        """Test setting solid background."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.SOLID, color=(255, 0, 0))
        
        assert manager.background_effect is not None
        assert manager.background_effect['type'] == 'solid'
    
    def test_set_gradient_background(self):
        """Test setting gradient background."""
        manager = EffectsManager(width=64, height=32)
        stops = [ColorStop(0, (0, 0, 0)), ColorStop(1, (255, 255, 255))]
        manager.set_background(BackgroundType.GRADIENT, color_stops=stops)
        
        assert manager.background_effect is not None
        assert manager.background_effect['type'] == 'gradient'
    
    def test_set_plasma_background(self):
        """Test setting plasma background."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.PLASMA)
        
        assert isinstance(manager.background_effect, PlasmaEffect)
    
    def test_set_matrix_background(self):
        """Test setting matrix rain background."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.MATRIX)
        
        assert isinstance(manager.background_effect, MatrixRainEffect)
    
    def test_set_starfield_background(self):
        """Test setting starfield background."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.STARFIELD)
        
        assert isinstance(manager.background_effect, StarfieldEffect)
    
    def test_add_particle_effect_fire(self):
        """Test adding fire particle effect."""
        manager = EffectsManager(width=64, height=32)
        emitter = manager.add_particle_effect(EffectType.FIRE, x=32, y=28)
        
        assert emitter in manager.particle_emitters
    
    def test_add_particle_effect_snow(self):
        """Test adding snow particle effect."""
        manager = EffectsManager(width=64, height=32)
        emitter = manager.add_particle_effect(EffectType.SNOW, x=32, y=0)
        
        assert emitter in manager.particle_emitters
    
    def test_remove_particle_effect(self):
        """Test removing particle effect."""
        manager = EffectsManager(width=64, height=32)
        emitter = manager.add_particle_effect(EffectType.SPARKLE, x=32, y=16)
        
        assert len(manager.particle_emitters) == 1
        
        manager.remove_particle_effect(emitter)
        assert len(manager.particle_emitters) == 0
    
    def test_clear_effects(self):
        """Test clearing all effects."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.PLASMA)
        manager.add_particle_effect(EffectType.FIRE, x=32, y=28)
        
        manager.clear_effects()
        
        assert manager.background_effect is None
        assert len(manager.particle_emitters) == 0
    
    def test_update(self):
        """Test updating effects."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.PLASMA)
        
        # Should not raise
        manager.update(dt=0.1)
    
    def test_render_background_solid(self):
        """Test rendering solid background."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.SOLID, color=(100, 150, 200))
        
        image = manager.render_background()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)
        
        # Check center pixel color
        pixel = image.getpixel((32, 16))
        assert pixel == (100, 150, 200)
    
    def test_render_complete(self):
        """Test complete render with all effects."""
        manager = EffectsManager(width=64, height=32)
        manager.set_background(BackgroundType.SOLID, color=(0, 0, 0))
        manager.add_particle_effect(EffectType.SPARKLE, x=32, y=16)
        manager.update(dt=0.1)
        
        image = manager.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestUtilityFunctions:
    """Tests for utility functions."""
    
    def test_create_glow_effect(self):
        """Test glow effect creation."""
        # Create a simple test image
        image = Image.new('RGB', (64, 32), (0, 0, 0))
        
        result = create_glow_effect(image, glow_color=(0, 255, 255), radius=2)
        
        assert isinstance(result, Image.Image)
        assert result.size == (64, 32)
    
    def test_apply_color_transform_hue(self):
        """Test color transform with hue shift."""
        image = Image.new('RGB', (64, 32), (255, 0, 0))  # Red
        
        result = apply_color_transform(image, hue_shift=120)  # Should shift to green-ish
        
        assert isinstance(result, Image.Image)
        assert result.size == (64, 32)
    
    def test_apply_color_transform_brightness(self):
        """Test color transform with brightness change."""
        image = Image.new('RGB', (64, 32), (100, 100, 100))
        
        # Increase brightness
        result = apply_color_transform(image, brightness=1.5)
        pixel = result.getpixel((32, 16))
        
        # Should be brighter
        assert pixel[0] > 100 or pixel[1] > 100 or pixel[2] > 100
    
    def test_apply_color_transform_saturation(self):
        """Test color transform with saturation change."""
        image = Image.new('RGB', (64, 32), (200, 100, 100))
        
        result = apply_color_transform(image, saturation=0.5)
        
        assert isinstance(result, Image.Image)
