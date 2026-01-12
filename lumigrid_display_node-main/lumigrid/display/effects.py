"""Visual effects and backgrounds for Lumigrid display system.

This module provides various visual effects including:
- Animated backgrounds
- Time-based and weather-based backgrounds
- Special effects (glow, shadows, particles)
- Matrix rain effect
- Plasma and fluid simulations
"""

import math
import random
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional, Tuple, Callable

from PIL import Image, ImageDraw, ImageFilter

from lumigrid.display.animation import (
    Animation, AnimationPreset, ParticleEmitter, Easing, EasingType
)


class EffectType(Enum):
    """Types of visual effects."""
    NONE = "none"
    GLOW = "glow"
    SHADOW = "shadow"
    PULSE = "pulse"
    BREATHE = "breathe"
    COLOR_CYCLE = "color_cycle"
    RIPPLE = "ripple"
    MATRIX_RAIN = "matrix_rain"
    FIRE = "fire"
    SNOW = "snow"
    RAIN = "rain"
    PLASMA = "plasma"
    STARFIELD = "starfield"
    SPARKLE = "sparkle"


class BackgroundType(Enum):
    """Types of background effects."""
    SOLID = "solid"
    GRADIENT = "gradient"
    ANIMATED_GRADIENT = "animated_gradient"
    PLASMA = "plasma"
    STARFIELD = "starfield"
    MATRIX = "matrix"
    NOISE = "noise"
    WAVE = "wave"
    GEOMETRIC = "geometric"
    TIME_BASED = "time_based"


@dataclass
class ColorStop:
    """A color stop for gradients.
    
    Attributes:
        position: Position along gradient (0.0 to 1.0)
        color: RGB color tuple
    """
    position: float
    color: Tuple[int, int, int]


class GradientGenerator:
    """Generates gradient images and colors."""
    
    @staticmethod
    def linear_gradient(
        width: int,
        height: int,
        color_stops: List[ColorStop],
        angle: float = 0
    ) -> Image.Image:
        """Generate a linear gradient image.
        
        Args:
            width: Image width
            height: Image height
            color_stops: List of color stops
            angle: Gradient angle in degrees (0 = horizontal left to right)
            
        Returns:
            PIL Image with gradient
        """
        image = Image.new('RGB', (width, height))
        pixels = image.load()
        
        if not color_stops:
            return image
        
        # Sort color stops by position
        color_stops = sorted(color_stops, key=lambda x: x.position)
        
        # Convert angle to radians
        rad = math.radians(angle)
        cos_a = math.cos(rad)
        sin_a = math.sin(rad)
        
        # Calculate diagonal length for normalization
        diagonal = math.sqrt(width * width + height * height)
        
        for y in range(height):
            for x in range(width):
                # Calculate position along gradient direction
                # Normalize to 0-1 range
                px = x - width / 2
                py = y - height / 2
                position = (px * cos_a + py * sin_a) / diagonal + 0.5
                position = max(0, min(1, position))
                
                # Find surrounding color stops
                prev_stop = color_stops[0]
                next_stop = color_stops[-1]
                
                for i, stop in enumerate(color_stops):
                    if stop.position >= position:
                        next_stop = stop
                        if i > 0:
                            prev_stop = color_stops[i - 1]
                        break
                    prev_stop = stop
                
                # Interpolate color
                if prev_stop.position == next_stop.position:
                    color = prev_stop.color
                else:
                    t = (position - prev_stop.position) / (next_stop.position - prev_stop.position)
                    color = tuple(
                        int(prev_stop.color[i] + (next_stop.color[i] - prev_stop.color[i]) * t)
                        for i in range(3)
                    )
                
                pixels[x, y] = color
        
        return image
    
    @staticmethod
    def radial_gradient(
        width: int,
        height: int,
        color_stops: List[ColorStop],
        center_x: Optional[float] = None,
        center_y: Optional[float] = None
    ) -> Image.Image:
        """Generate a radial gradient image.
        
        Args:
            width: Image width
            height: Image height
            color_stops: List of color stops (position 0 = center, 1 = edge)
            center_x: Center X position (0-1, default 0.5)
            center_y: Center Y position (0-1, default 0.5)
            
        Returns:
            PIL Image with radial gradient
        """
        image = Image.new('RGB', (width, height))
        pixels = image.load()
        
        if not color_stops:
            return image
        
        color_stops = sorted(color_stops, key=lambda x: x.position)
        
        cx = (center_x if center_x is not None else 0.5) * width
        cy = (center_y if center_y is not None else 0.5) * height
        max_dist = math.sqrt(max(cx, width - cx) ** 2 + max(cy, height - cy) ** 2)
        
        for y in range(height):
            for x in range(width):
                dist = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
                position = min(1, dist / max_dist) if max_dist > 0 else 0
                
                prev_stop = color_stops[0]
                next_stop = color_stops[-1]
                
                for i, stop in enumerate(color_stops):
                    if stop.position >= position:
                        next_stop = stop
                        if i > 0:
                            prev_stop = color_stops[i - 1]
                        break
                    prev_stop = stop
                
                if prev_stop.position == next_stop.position:
                    color = prev_stop.color
                else:
                    t = (position - prev_stop.position) / (next_stop.position - prev_stop.position)
                    color = tuple(
                        int(prev_stop.color[i] + (next_stop.color[i] - prev_stop.color[i]) * t)
                        for i in range(3)
                    )
                
                pixels[x, y] = color
        
        return image


class PlasmaEffect:
    """Generates animated plasma effect."""
    
    def __init__(
        self,
        width: int,
        height: int,
        color_palette: Optional[List[Tuple[int, int, int]]] = None,
        speed: float = 1.0
    ):
        """Initialize plasma effect.
        
        Args:
            width: Effect width
            height: Effect height
            color_palette: List of colors for the plasma
            speed: Animation speed multiplier
        """
        self.width = width
        self.height = height
        self.speed = speed
        self._time = 0.0
        
        if color_palette is None:
            # Default rainbow-ish palette
            self.color_palette = [
                (255, 0, 0),
                (255, 128, 0),
                (255, 255, 0),
                (0, 255, 0),
                (0, 255, 255),
                (0, 0, 255),
                (128, 0, 255),
            ]
        else:
            self.color_palette = color_palette
    
    def update(self, dt: float) -> None:
        """Update plasma animation.
        
        Args:
            dt: Delta time in seconds
        """
        self._time += dt * self.speed
    
    def render(self) -> Image.Image:
        """Render current plasma frame.
        
        Returns:
            PIL Image with plasma effect
        """
        image = Image.new('RGB', (self.width, self.height))
        pixels = image.load()
        
        for y in range(self.height):
            for x in range(self.width):
                # Generate plasma value using sine waves
                v1 = math.sin(x / 8.0 + self._time)
                v2 = math.sin((y / 8.0 + self._time) / 2.0)
                v3 = math.sin((x + y) / 16.0 + self._time)
                v4 = math.sin(math.sqrt(x * x + y * y) / 8.0 + self._time)
                
                v = (v1 + v2 + v3 + v4) / 4.0
                
                # Map to color palette
                color_idx = (v + 1) / 2  # Normalize to 0-1
                color = self._get_palette_color(color_idx)
                pixels[x, y] = color
        
        return image
    
    def _get_palette_color(self, position: float) -> Tuple[int, int, int]:
        """Get color from palette at given position.
        
        Args:
            position: Position in palette (0-1)
            
        Returns:
            Interpolated RGB color
        """
        if not self.color_palette:
            return (0, 0, 0)
        
        scaled = position * (len(self.color_palette) - 1)
        idx1 = int(scaled)
        idx2 = min(idx1 + 1, len(self.color_palette) - 1)
        t = scaled - idx1
        
        c1 = self.color_palette[idx1]
        c2 = self.color_palette[idx2]
        
        return (
            int(c1[0] + (c2[0] - c1[0]) * t),
            int(c1[1] + (c2[1] - c1[1]) * t),
            int(c1[2] + (c2[2] - c1[2]) * t)
        )


class MatrixRainEffect:
    """Matrix-style falling code rain effect."""
    
    def __init__(
        self,
        width: int,
        height: int,
        color: Tuple[int, int, int] = (0, 255, 0),
        density: float = 0.1,
        speed: float = 1.0,
        trail_length: int = 5
    ):
        """Initialize matrix rain effect.
        
        Args:
            width: Effect width
            height: Effect height
            color: Rain color
            density: Column density (0-1)
            speed: Fall speed multiplier
            trail_length: Length of trail behind drops
        """
        self.width = width
        self.height = height
        self.color = color
        self.density = density
        self.speed = speed
        self.trail_length = trail_length
        
        # Initialize columns
        self.columns: List[Dict] = []
        self._initialize_columns()
    
    def _initialize_columns(self) -> None:
        """Initialize rain columns."""
        self.columns = []
        for x in range(self.width):
            if random.random() < self.density:
                self.columns.append({
                    'x': x,
                    'y': random.uniform(-self.height, 0),
                    'speed': random.uniform(0.5, 1.5) * self.speed,
                    'chars': [random.randint(0, 9) for _ in range(self.trail_length)]
                })
    
    def update(self, dt: float) -> None:
        """Update rain animation.
        
        Args:
            dt: Delta time in seconds
        """
        for col in self.columns:
            col['y'] += col['speed'] * dt * 10
            
            # Reset column if it falls off screen
            if col['y'] > self.height + self.trail_length:
                col['y'] = random.uniform(-self.trail_length, 0)
                col['speed'] = random.uniform(0.5, 1.5) * self.speed
                col['chars'] = [random.randint(0, 9) for _ in range(self.trail_length)]
            
            # Occasionally change characters
            if random.random() < 0.1:
                idx = random.randint(0, self.trail_length - 1)
                col['chars'][idx] = random.randint(0, 9)
    
    def render(self) -> Image.Image:
        """Render current rain frame.
        
        Returns:
            PIL Image with matrix rain effect
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        pixels = image.load()
        
        for col in self.columns:
            x = col['x']
            base_y = int(col['y'])
            
            for i in range(self.trail_length):
                y = base_y - i
                if 0 <= y < self.height and 0 <= x < self.width:
                    # Fade color based on position in trail
                    fade = 1.0 - (i / self.trail_length)
                    r = int(self.color[0] * fade)
                    g = int(self.color[1] * fade)
                    b = int(self.color[2] * fade)
                    pixels[x, y] = (r, g, b)
        
        return image


class StarfieldEffect:
    """Starfield background with twinkling stars."""
    
    def __init__(
        self,
        width: int,
        height: int,
        num_stars: int = 50,
        twinkle_speed: float = 1.0,
        base_color: Tuple[int, int, int] = (255, 255, 255)
    ):
        """Initialize starfield effect.
        
        Args:
            width: Effect width
            height: Effect height
            num_stars: Number of stars
            twinkle_speed: Twinkle animation speed
            base_color: Base star color
        """
        self.width = width
        self.height = height
        self.twinkle_speed = twinkle_speed
        self.base_color = base_color
        
        # Initialize stars
        self.stars: List[Dict] = []
        for _ in range(num_stars):
            self.stars.append({
                'x': random.randint(0, width - 1),
                'y': random.randint(0, height - 1),
                'brightness': random.random(),
                'phase': random.random() * 2 * math.pi,
                'speed': random.uniform(0.5, 2.0)
            })
    
    def update(self, dt: float) -> None:
        """Update star animation.
        
        Args:
            dt: Delta time in seconds
        """
        for star in self.stars:
            star['phase'] += dt * star['speed'] * self.twinkle_speed
            star['brightness'] = (math.sin(star['phase']) + 1) / 2
    
    def render(self) -> Image.Image:
        """Render current starfield frame.
        
        Returns:
            PIL Image with starfield effect
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 10))
        pixels = image.load()
        
        for star in self.stars:
            x, y = star['x'], star['y']
            b = star['brightness']
            color = (
                int(self.base_color[0] * b),
                int(self.base_color[1] * b),
                int(self.base_color[2] * b)
            )
            if 0 <= x < self.width and 0 <= y < self.height:
                pixels[x, y] = color
        
        return image


class FireEffect:
    """Fire/flame effect using cellular automaton."""
    
    def __init__(
        self,
        width: int,
        height: int,
        intensity: float = 0.8,
        decay: float = 0.9
    ):
        """Initialize fire effect.
        
        Args:
            width: Effect width
            height: Effect height
            intensity: Fire intensity (0-1)
            decay: Heat decay rate
        """
        self.width = width
        self.height = height
        self.intensity = intensity
        self.decay = decay
        
        # Heat grid
        self.heat = [[0.0] * width for _ in range(height)]
        
        # Fire color palette (black -> red -> yellow -> white)
        self.palette = self._generate_fire_palette()
    
    def _generate_fire_palette(self) -> List[Tuple[int, int, int]]:
        """Generate fire color palette."""
        palette = []
        for i in range(256):
            if i < 85:
                # Black to red
                r = i * 3
                g = 0
                b = 0
            elif i < 170:
                # Red to yellow
                r = 255
                g = (i - 85) * 3
                b = 0
            else:
                # Yellow to white
                r = 255
                g = 255
                b = (i - 170) * 3
            palette.append((r, g, b))
        return palette
    
    def update(self, dt: float) -> None:
        """Update fire animation.
        
        Args:
            dt: Delta time in seconds
        """
        # Seed bottom row with random heat
        for x in range(self.width):
            self.heat[self.height - 1][x] = random.random() * self.intensity
        
        # Propagate heat upward
        for y in range(self.height - 2, -1, -1):
            for x in range(self.width):
                # Average heat from below and neighbors
                below = self.heat[y + 1][x]
                left = self.heat[y + 1][max(0, x - 1)]
                right = self.heat[y + 1][min(self.width - 1, x + 1)]
                
                avg_heat = (below + left + right) / 3
                self.heat[y][x] = avg_heat * self.decay
    
    def render(self) -> Image.Image:
        """Render current fire frame.
        
        Returns:
            PIL Image with fire effect
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        pixels = image.load()
        
        for y in range(self.height):
            for x in range(self.width):
                heat_val = min(1.0, self.heat[y][x])
                color_idx = int(heat_val * 255)
                pixels[x, y] = self.palette[color_idx]
        
        return image


class WaveEffect:
    """Animated wave pattern effect."""
    
    def __init__(
        self,
        width: int,
        height: int,
        color1: Tuple[int, int, int] = (0, 100, 200),
        color2: Tuple[int, int, int] = (0, 50, 100),
        wave_count: int = 3,
        speed: float = 1.0
    ):
        """Initialize wave effect.
        
        Args:
            width: Effect width
            height: Effect height
            color1: Primary wave color
            color2: Secondary wave color
            wave_count: Number of waves
            speed: Animation speed
        """
        self.width = width
        self.height = height
        self.color1 = color1
        self.color2 = color2
        self.wave_count = wave_count
        self.speed = speed
        self._time = 0.0
    
    def update(self, dt: float) -> None:
        """Update wave animation.
        
        Args:
            dt: Delta time in seconds
        """
        self._time += dt * self.speed
    
    def render(self) -> Image.Image:
        """Render current wave frame.
        
        Returns:
            PIL Image with wave effect
        """
        image = Image.new('RGB', (self.width, self.height))
        pixels = image.load()
        
        for y in range(self.height):
            for x in range(self.width):
                # Generate wave value
                v = 0
                for i in range(self.wave_count):
                    freq = (i + 1) * 0.3
                    phase = i * math.pi / self.wave_count
                    v += math.sin(x * freq / self.width * math.pi * 2 + self._time + phase)
                
                v = (v / self.wave_count + 1) / 2
                
                # Interpolate colors
                color = tuple(
                    int(self.color2[i] + (self.color1[i] - self.color2[i]) * v)
                    for i in range(3)
                )
                pixels[x, y] = color
        
        return image


class GeometricPattern:
    """Animated geometric pattern effect."""
    
    def __init__(
        self,
        width: int,
        height: int,
        pattern_type: str = "checkerboard",  # checkerboard, stripes, circles
        color1: Tuple[int, int, int] = (255, 255, 255),
        color2: Tuple[int, int, int] = (0, 0, 0),
        speed: float = 1.0
    ):
        """Initialize geometric pattern.
        
        Args:
            width: Effect width
            height: Effect height
            pattern_type: Type of pattern
            color1: Primary color
            color2: Secondary color
            speed: Animation speed
        """
        self.width = width
        self.height = height
        self.pattern_type = pattern_type
        self.color1 = color1
        self.color2 = color2
        self.speed = speed
        self._time = 0.0
    
    def update(self, dt: float) -> None:
        """Update pattern animation.
        
        Args:
            dt: Delta time in seconds
        """
        self._time += dt * self.speed
    
    def render(self) -> Image.Image:
        """Render current pattern frame.
        
        Returns:
            PIL Image with geometric pattern
        """
        image = Image.new('RGB', (self.width, self.height))
        pixels = image.load()
        
        for y in range(self.height):
            for x in range(self.width):
                if self.pattern_type == "checkerboard":
                    offset = int(self._time * 2)
                    cell = ((x + offset) // 4 + (y + offset) // 4) % 2
                elif self.pattern_type == "stripes":
                    offset = int(self._time * 4)
                    cell = ((x + y + offset) // 4) % 2
                elif self.pattern_type == "circles":
                    cx, cy = self.width / 2, self.height / 2
                    dist = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
                    cell = int((dist + self._time * 5) / 4) % 2
                else:
                    cell = 0
                
                pixels[x, y] = self.color1 if cell else self.color2
        
        return image


class TimeBased:
    """Time-based background that changes throughout the day."""
    
    def __init__(self, width: int, height: int):
        """Initialize time-based background.
        
        Args:
            width: Effect width
            height: Effect height
        """
        self.width = width
        self.height = height
        
        # Define colors for different times of day
        self.time_colors = {
            0: ((10, 10, 30), (20, 20, 50)),      # Midnight - dark blue
            6: ((50, 30, 80), (100, 70, 120)),    # Dawn - purple
            8: ((100, 150, 220), (180, 210, 255)), # Morning - light blue
            12: ((135, 206, 235), (200, 230, 255)), # Noon - sky blue
            17: ((255, 150, 100), (255, 200, 150)), # Sunset - orange
            19: ((80, 50, 100), (150, 100, 150)),   # Dusk - purple
            21: ((20, 20, 60), (40, 40, 80)),      # Night - dark blue
        }
    
    def get_current_colors(self) -> Tuple[Tuple[int, int, int], Tuple[int, int, int]]:
        """Get colors based on current time.
        
        Returns:
            Tuple of (top_color, bottom_color)
        """
        import datetime
        hour = datetime.datetime.now().hour + datetime.datetime.now().minute / 60
        
        # Find surrounding time points
        times = sorted(self.time_colors.keys())
        prev_time = times[-1]
        next_time = times[0]
        
        for t in times:
            if t <= hour:
                prev_time = t
            if t > hour:
                next_time = t
                break
        
        # Handle day wrap-around
        if next_time <= prev_time:
            next_time += 24
        
        adjusted_hour = hour if hour >= prev_time else hour + 24
        
        # Calculate interpolation factor
        if next_time == prev_time:
            t = 0
        else:
            t = (adjusted_hour - prev_time) / (next_time - prev_time)
        
        prev_colors = self.time_colors[prev_time % 24]
        next_colors = self.time_colors[next_time % 24]
        
        # Interpolate colors
        top = tuple(
            int(prev_colors[0][i] + (next_colors[0][i] - prev_colors[0][i]) * t)
            for i in range(3)
        )
        bottom = tuple(
            int(prev_colors[1][i] + (next_colors[1][i] - prev_colors[1][i]) * t)
            for i in range(3)
        )
        
        return top, bottom
    
    def render(self) -> Image.Image:
        """Render time-based background.
        
        Returns:
            PIL Image with gradient based on time
        """
        top, bottom = self.get_current_colors()
        return GradientGenerator.linear_gradient(
            self.width, self.height,
            [ColorStop(0, top), ColorStop(1, bottom)],
            angle=90
        )


class EffectsManager:
    """Manages visual effects and backgrounds."""
    
    def __init__(self, width: int, height: int):
        """Initialize effects manager.
        
        Args:
            width: Display width
            height: Display height
        """
        self.width = width
        self.height = height
        
        # Active effects
        self.background_effect: Optional[Any] = None
        self.overlay_effects: List[Any] = []
        self.particle_emitters: List[ParticleEmitter] = []
        
        self._last_update = time.perf_counter()
    
    def set_background(
        self,
        effect_type: BackgroundType,
        **kwargs
    ) -> None:
        """Set background effect.
        
        Args:
            effect_type: Type of background effect
            **kwargs: Effect-specific parameters
        """
        if effect_type == BackgroundType.SOLID:
            color = kwargs.get('color', (0, 0, 0))
            self.background_effect = {'type': 'solid', 'color': color}
        
        elif effect_type == BackgroundType.GRADIENT:
            color_stops = kwargs.get('color_stops', [
                ColorStop(0, (0, 0, 50)),
                ColorStop(1, (0, 0, 100))
            ])
            angle = kwargs.get('angle', 90)
            self.background_effect = {
                'type': 'gradient',
                'color_stops': color_stops,
                'angle': angle
            }
        
        elif effect_type == BackgroundType.PLASMA:
            self.background_effect = PlasmaEffect(
                self.width, self.height,
                color_palette=kwargs.get('colors'),
                speed=kwargs.get('speed', 1.0)
            )
        
        elif effect_type == BackgroundType.MATRIX:
            self.background_effect = MatrixRainEffect(
                self.width, self.height,
                color=kwargs.get('color', (0, 255, 0)),
                density=kwargs.get('density', 0.1),
                speed=kwargs.get('speed', 1.0)
            )
        
        elif effect_type == BackgroundType.STARFIELD:
            self.background_effect = StarfieldEffect(
                self.width, self.height,
                num_stars=kwargs.get('num_stars', 50),
                twinkle_speed=kwargs.get('speed', 1.0)
            )
        
        elif effect_type == BackgroundType.WAVE:
            self.background_effect = WaveEffect(
                self.width, self.height,
                color1=kwargs.get('color1', (0, 100, 200)),
                color2=kwargs.get('color2', (0, 50, 100)),
                speed=kwargs.get('speed', 1.0)
            )
        
        elif effect_type == BackgroundType.GEOMETRIC:
            self.background_effect = GeometricPattern(
                self.width, self.height,
                pattern_type=kwargs.get('pattern', 'checkerboard'),
                color1=kwargs.get('color1', (255, 255, 255)),
                color2=kwargs.get('color2', (0, 0, 0)),
                speed=kwargs.get('speed', 1.0)
            )
        
        elif effect_type == BackgroundType.TIME_BASED:
            self.background_effect = TimeBased(self.width, self.height)
    
    def add_particle_effect(
        self,
        effect_type: EffectType,
        x: float,
        y: float,
        **kwargs
    ) -> ParticleEmitter:
        """Add a particle effect.
        
        Args:
            effect_type: Type of particle effect
            x: Emitter X position
            y: Emitter Y position
            **kwargs: Effect-specific parameters
            
        Returns:
            ParticleEmitter instance
        """
        if effect_type == EffectType.FIRE:
            emitter = ParticleEmitter(
                x=x, y=y,
                emission_rate=kwargs.get('rate', 20),
                max_particles=kwargs.get('max', 100),
                particle_life=kwargs.get('life', 0.5),
                initial_velocity=(0, -10),
                velocity_variance=(3, 2),
                gravity=-5,
                color=(255, 100, 0),
                color_variance=(50, 50, 0)
            )
        elif effect_type == EffectType.SNOW:
            emitter = ParticleEmitter(
                x=self.width / 2, y=0,
                emission_rate=kwargs.get('rate', 10),
                max_particles=kwargs.get('max', 50),
                particle_life=kwargs.get('life', 3.0),
                initial_velocity=(0, 5),
                velocity_variance=(2, 1),
                gravity=2,
                color=(255, 255, 255),
                emission_shape="line"
            )
        elif effect_type == EffectType.RAIN:
            emitter = ParticleEmitter(
                x=self.width / 2, y=0,
                emission_rate=kwargs.get('rate', 30),
                max_particles=kwargs.get('max', 100),
                particle_life=kwargs.get('life', 1.0),
                initial_velocity=(0, 20),
                velocity_variance=(1, 5),
                gravity=10,
                color=(100, 150, 255),
                emission_shape="line"
            )
        elif effect_type == EffectType.SPARKLE:
            emitter = ParticleEmitter(
                x=x, y=y,
                emission_rate=kwargs.get('rate', 5),
                max_particles=kwargs.get('max', 30),
                particle_life=kwargs.get('life', 0.8),
                initial_velocity=(0, 0),
                velocity_variance=(10, 10),
                gravity=0,
                color=(255, 255, 200),
                color_variance=(50, 50, 55),
                fade_out=True,
                emission_shape="circle"
            )
        else:
            emitter = ParticleEmitter(x=x, y=y, **kwargs)
        
        self.particle_emitters.append(emitter)
        emitter.start()
        return emitter
    
    def remove_particle_effect(self, emitter: ParticleEmitter) -> None:
        """Remove a particle effect.
        
        Args:
            emitter: Emitter to remove
        """
        if emitter in self.particle_emitters:
            emitter.stop()
            self.particle_emitters.remove(emitter)
    
    def clear_effects(self) -> None:
        """Clear all effects."""
        self.background_effect = None
        self.overlay_effects.clear()
        for emitter in self.particle_emitters:
            emitter.stop()
        self.particle_emitters.clear()
    
    def update(self, dt: Optional[float] = None) -> None:
        """Update all effects.
        
        Args:
            dt: Delta time (auto-calculated if None)
        """
        current_time = time.perf_counter()
        if dt is None:
            dt = current_time - self._last_update
        self._last_update = current_time
        
        # Update background (only if it's an object with update method, not a dict)
        if self.background_effect is not None and not isinstance(self.background_effect, dict):
            if hasattr(self.background_effect, 'update'):
                self.background_effect.update(dt)
        
        # Update particle emitters
        for emitter in self.particle_emitters:
            emitter.update(dt)
    
    def render_background(self) -> Image.Image:
        """Render current background.
        
        Returns:
            PIL Image with background
        """
        if self.background_effect is None:
            return Image.new('RGB', (self.width, self.height), (0, 0, 0))
        
        if isinstance(self.background_effect, dict):
            if self.background_effect['type'] == 'solid':
                return Image.new('RGB', (self.width, self.height), 
                               self.background_effect['color'])
            elif self.background_effect['type'] == 'gradient':
                return GradientGenerator.linear_gradient(
                    self.width, self.height,
                    self.background_effect['color_stops'],
                    self.background_effect['angle']
                )
        
        if hasattr(self.background_effect, 'render'):
            return self.background_effect.render()
        
        return Image.new('RGB', (self.width, self.height), (0, 0, 0))
    
    def render_particles(self, image: Image.Image) -> Image.Image:
        """Render particles onto an image.
        
        Args:
            image: Base image to draw particles on
            
        Returns:
            Image with particles drawn
        """
        pixels = image.load()
        
        for emitter in self.particle_emitters:
            for p in emitter.get_particles():
                x, y = p['x'], p['y']
                if 0 <= x < self.width and 0 <= y < self.height:
                    pixels[x, y] = p['color']
        
        return image
    
    def render(self) -> Image.Image:
        """Render complete effects frame.
        
        Returns:
            PIL Image with all effects
        """
        # Start with background
        image = self.render_background()
        
        # Add particles
        image = self.render_particles(image)
        
        return image


# Utility functions for common effects

def create_glow_effect(
    image: Image.Image,
    glow_color: Tuple[int, int, int],
    radius: int = 3
) -> Image.Image:
    """Apply glow effect to an image.
    
    Args:
        image: Source image
        glow_color: Glow color
        radius: Glow radius
        
    Returns:
        Image with glow effect
    """
    # Create glow layer
    glow = image.copy()
    glow = glow.filter(ImageFilter.GaussianBlur(radius=radius))
    
    # Tint glow with color
    glow_pixels = glow.load()
    for y in range(glow.height):
        for x in range(glow.width):
            r, g, b = glow_pixels[x, y]
            brightness = (r + g + b) / 3 / 255
            glow_pixels[x, y] = (
                int(glow_color[0] * brightness),
                int(glow_color[1] * brightness),
                int(glow_color[2] * brightness)
            )
    
    # Composite
    result = Image.new('RGB', image.size, (0, 0, 0))
    result.paste(glow, (0, 0))
    result.paste(image, (0, 0), image.convert('RGBA'))
    
    return result


def apply_color_transform(
    image: Image.Image,
    hue_shift: float = 0,
    saturation: float = 1.0,
    brightness: float = 1.0
) -> Image.Image:
    """Apply color transformation to image.
    
    Args:
        image: Source image
        hue_shift: Hue shift in degrees (0-360)
        saturation: Saturation multiplier
        brightness: Brightness multiplier
        
    Returns:
        Transformed image
    """
    import colorsys
    
    result = image.copy()
    pixels = result.load()
    
    for y in range(result.height):
        for x in range(result.width):
            r, g, b = pixels[x, y]
            
            # Convert to HSV
            h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255)
            
            # Apply transforms
            h = (h + hue_shift/360) % 1.0
            s = min(1.0, max(0, s * saturation))
            v = min(1.0, max(0, v * brightness))
            
            # Convert back to RGB
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            pixels[x, y] = (int(r*255), int(g*255), int(b*255))
    
    return result
