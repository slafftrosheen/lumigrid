"""Animation engine for Lumigrid display system.

This module provides a comprehensive animation system including:
- Keyframe animation with interpolation
- Easing functions for smooth motion
- Physics-based animations
- Particle systems
- Animation presets
"""

import math
import random
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple, Union


class EasingType(Enum):
    """Available easing function types."""
    LINEAR = "linear"
    EASE_IN = "ease_in"
    EASE_OUT = "ease_out"
    EASE_IN_OUT = "ease_in_out"
    EASE_IN_QUAD = "ease_in_quad"
    EASE_OUT_QUAD = "ease_out_quad"
    EASE_IN_OUT_QUAD = "ease_in_out_quad"
    EASE_IN_CUBIC = "ease_in_cubic"
    EASE_OUT_CUBIC = "ease_out_cubic"
    EASE_IN_OUT_CUBIC = "ease_in_out_cubic"
    EASE_IN_ELASTIC = "ease_in_elastic"
    EASE_OUT_ELASTIC = "ease_out_elastic"
    EASE_IN_OUT_ELASTIC = "ease_in_out_elastic"
    EASE_IN_BOUNCE = "ease_in_bounce"
    EASE_OUT_BOUNCE = "ease_out_bounce"
    EASE_IN_OUT_BOUNCE = "ease_in_out_bounce"
    SPRING = "spring"


class Easing:
    """Collection of easing functions for smooth animations."""
    
    @staticmethod
    def linear(t: float) -> float:
        """Linear interpolation (no easing)."""
        return t
    
    @staticmethod
    def ease_in(t: float) -> float:
        """Ease in with sine curve."""
        return 1 - math.cos((t * math.pi) / 2)
    
    @staticmethod
    def ease_out(t: float) -> float:
        """Ease out with sine curve."""
        return math.sin((t * math.pi) / 2)
    
    @staticmethod
    def ease_in_out(t: float) -> float:
        """Ease in and out with sine curve."""
        return -(math.cos(math.pi * t) - 1) / 2
    
    @staticmethod
    def ease_in_quad(t: float) -> float:
        """Quadratic ease in."""
        return t * t
    
    @staticmethod
    def ease_out_quad(t: float) -> float:
        """Quadratic ease out."""
        return 1 - (1 - t) * (1 - t)
    
    @staticmethod
    def ease_in_out_quad(t: float) -> float:
        """Quadratic ease in and out."""
        if t < 0.5:
            return 2 * t * t
        return 1 - pow(-2 * t + 2, 2) / 2
    
    @staticmethod
    def ease_in_cubic(t: float) -> float:
        """Cubic ease in."""
        return t * t * t
    
    @staticmethod
    def ease_out_cubic(t: float) -> float:
        """Cubic ease out."""
        return 1 - pow(1 - t, 3)
    
    @staticmethod
    def ease_in_out_cubic(t: float) -> float:
        """Cubic ease in and out."""
        if t < 0.5:
            return 4 * t * t * t
        return 1 - pow(-2 * t + 2, 3) / 2
    
    @staticmethod
    def ease_in_elastic(t: float) -> float:
        """Elastic ease in."""
        if t == 0 or t == 1:
            return t
        c4 = (2 * math.pi) / 3
        return -pow(2, 10 * t - 10) * math.sin((t * 10 - 10.75) * c4)
    
    @staticmethod
    def ease_out_elastic(t: float) -> float:
        """Elastic ease out."""
        if t == 0 or t == 1:
            return t
        c4 = (2 * math.pi) / 3
        return pow(2, -10 * t) * math.sin((t * 10 - 0.75) * c4) + 1
    
    @staticmethod
    def ease_in_out_elastic(t: float) -> float:
        """Elastic ease in and out."""
        if t == 0 or t == 1:
            return t
        c5 = (2 * math.pi) / 4.5
        if t < 0.5:
            return -(pow(2, 20 * t - 10) * math.sin((20 * t - 11.125) * c5)) / 2
        return (pow(2, -20 * t + 10) * math.sin((20 * t - 11.125) * c5)) / 2 + 1
    
    @staticmethod
    def ease_out_bounce(t: float) -> float:
        """Bounce ease out."""
        n1 = 7.5625
        d1 = 2.75
        
        if t < 1 / d1:
            return n1 * t * t
        elif t < 2 / d1:
            t -= 1.5 / d1
            return n1 * t * t + 0.75
        elif t < 2.5 / d1:
            t -= 2.25 / d1
            return n1 * t * t + 0.9375
        else:
            t -= 2.625 / d1
            return n1 * t * t + 0.984375
    
    @staticmethod
    def ease_in_bounce(t: float) -> float:
        """Bounce ease in."""
        return 1 - Easing.ease_out_bounce(1 - t)
    
    @staticmethod
    def ease_in_out_bounce(t: float) -> float:
        """Bounce ease in and out."""
        if t < 0.5:
            return (1 - Easing.ease_out_bounce(1 - 2 * t)) / 2
        return (1 + Easing.ease_out_bounce(2 * t - 1)) / 2
    
    @staticmethod
    def spring(t: float, stiffness: float = 100, damping: float = 10) -> float:
        """Spring physics-based easing."""
        if t == 0 or t == 1:
            return t
        omega = math.sqrt(stiffness)
        zeta = damping / (2 * omega)
        
        if zeta < 1:  # Underdamped
            omega_d = omega * math.sqrt(1 - zeta * zeta)
            return 1 - math.exp(-zeta * omega * t) * (
                math.cos(omega_d * t) + (zeta * omega / omega_d) * math.sin(omega_d * t)
            )
        else:  # Critically damped or overdamped
            return 1 - (1 + omega * t) * math.exp(-omega * t)
    
    @classmethod
    def get_function(cls, easing_type: Union[EasingType, str]) -> Callable[[float], float]:
        """Get easing function by type.
        
        Args:
            easing_type: Type of easing function
            
        Returns:
            Easing function
        """
        if isinstance(easing_type, str):
            try:
                easing_type = EasingType(easing_type)
            except ValueError:
                return cls.linear
        
        functions = {
            EasingType.LINEAR: cls.linear,
            EasingType.EASE_IN: cls.ease_in,
            EasingType.EASE_OUT: cls.ease_out,
            EasingType.EASE_IN_OUT: cls.ease_in_out,
            EasingType.EASE_IN_QUAD: cls.ease_in_quad,
            EasingType.EASE_OUT_QUAD: cls.ease_out_quad,
            EasingType.EASE_IN_OUT_QUAD: cls.ease_in_out_quad,
            EasingType.EASE_IN_CUBIC: cls.ease_in_cubic,
            EasingType.EASE_OUT_CUBIC: cls.ease_out_cubic,
            EasingType.EASE_IN_OUT_CUBIC: cls.ease_in_out_cubic,
            EasingType.EASE_IN_ELASTIC: cls.ease_in_elastic,
            EasingType.EASE_OUT_ELASTIC: cls.ease_out_elastic,
            EasingType.EASE_IN_OUT_ELASTIC: cls.ease_in_out_elastic,
            EasingType.EASE_IN_BOUNCE: cls.ease_in_bounce,
            EasingType.EASE_OUT_BOUNCE: cls.ease_out_bounce,
            EasingType.EASE_IN_OUT_BOUNCE: cls.ease_in_out_bounce,
            EasingType.SPRING: cls.spring,
        }
        return functions.get(easing_type, cls.linear)


@dataclass
class Keyframe:
    """A single keyframe in an animation.
    
    Attributes:
        time: Time position of the keyframe (0.0 to 1.0)
        value: Value at this keyframe (can be number, tuple, dict)
        easing: Easing type to use when interpolating to next keyframe
    """
    time: float
    value: Any
    easing: EasingType = EasingType.LINEAR
    
    def __post_init__(self):
        """Validate keyframe time."""
        self.time = max(0.0, min(1.0, self.time))


@dataclass
class Animation:
    """Keyframe-based animation.
    
    Attributes:
        keyframes: List of keyframes defining the animation
        duration: Total duration in seconds
        loop: Whether animation should loop
        reverse: Whether to reverse when looping (ping-pong)
    """
    keyframes: List[Keyframe] = field(default_factory=list)
    duration: float = 1.0
    loop: bool = False
    reverse: bool = False
    
    _start_time: Optional[float] = field(default=None, repr=False)
    _paused: bool = field(default=False, repr=False)
    _pause_time: float = field(default=0.0, repr=False)
    
    def __post_init__(self):
        """Sort keyframes by time."""
        self.keyframes.sort(key=lambda k: k.time)
    
    def add_keyframe(self, time: float, value: Any, 
                     easing: EasingType = EasingType.LINEAR) -> None:
        """Add a keyframe to the animation.
        
        Args:
            time: Normalized time position (0.0 to 1.0)
            value: Value at this keyframe
            easing: Easing function for interpolation
        """
        self.keyframes.append(Keyframe(time, value, easing))
        self.keyframes.sort(key=lambda k: k.time)
    
    def start(self) -> None:
        """Start or restart the animation."""
        self._start_time = time.perf_counter()
        self._paused = False
        self._pause_time = 0.0
    
    def pause(self) -> None:
        """Pause the animation."""
        if not self._paused and self._start_time is not None:
            self._paused = True
            self._pause_time = time.perf_counter() - self._start_time
    
    def resume(self) -> None:
        """Resume paused animation."""
        if self._paused:
            self._start_time = time.perf_counter() - self._pause_time
            self._paused = False
    
    def stop(self) -> None:
        """Stop the animation."""
        self._start_time = None
        self._paused = False
        self._pause_time = 0.0
    
    def get_progress(self) -> float:
        """Get current animation progress (0.0 to 1.0).
        
        Returns:
            Current progress value
        """
        if self._start_time is None:
            return 0.0
        
        if self._paused:
            elapsed = self._pause_time
        else:
            elapsed = time.perf_counter() - self._start_time
        
        if self.duration <= 0:
            return 1.0
        
        progress = elapsed / self.duration
        
        if not self.loop:
            return min(1.0, progress)
        
        if self.reverse:
            # Ping-pong effect
            cycle = int(progress)
            progress = progress - cycle
            if cycle % 2 == 1:
                progress = 1.0 - progress
        else:
            progress = progress % 1.0
        
        return progress
    
    def is_complete(self) -> bool:
        """Check if animation has completed.
        
        Returns:
            True if animation is complete (for non-looping animations)
        """
        if self.loop:
            return False
        return self.get_progress() >= 1.0
    
    def get_value(self) -> Any:
        """Get interpolated value at current time.
        
        Returns:
            Interpolated value
        """
        if not self.keyframes:
            return None
        
        progress = self.get_progress()
        return self._interpolate_value(progress)
    
    def _interpolate_value(self, progress: float) -> Any:
        """Interpolate value at given progress.
        
        Args:
            progress: Animation progress (0.0 to 1.0)
            
        Returns:
            Interpolated value
        """
        if not self.keyframes:
            return None
        
        # Find surrounding keyframes
        prev_kf = self.keyframes[0]
        next_kf = self.keyframes[-1]
        
        for i, kf in enumerate(self.keyframes):
            if kf.time >= progress:
                next_kf = kf
                if i > 0:
                    prev_kf = self.keyframes[i - 1]
                break
            prev_kf = kf
        
        # Calculate local progress between keyframes
        if prev_kf.time == next_kf.time:
            local_progress = 0.0
        else:
            local_progress = (progress - prev_kf.time) / (next_kf.time - prev_kf.time)
        
        # Apply easing
        easing_func = Easing.get_function(prev_kf.easing)
        eased_progress = easing_func(local_progress)
        
        # Interpolate value
        return self._lerp(prev_kf.value, next_kf.value, eased_progress)
    
    def _lerp(self, a: Any, b: Any, t: float) -> Any:
        """Linear interpolation between two values.
        
        Args:
            a: Start value
            b: End value
            t: Interpolation factor (0.0 to 1.0)
            
        Returns:
            Interpolated value
        """
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            return a + (b - a) * t
        
        if isinstance(a, tuple) and isinstance(b, tuple):
            return tuple(
                self._lerp(av, bv, t)
                for av, bv in zip(a, b)
            )
        
        if isinstance(a, dict) and isinstance(b, dict):
            result = {}
            for key in set(a.keys()) | set(b.keys()):
                if key in a and key in b:
                    result[key] = self._lerp(a[key], b[key], t)
                elif key in a:
                    result[key] = a[key]
                else:
                    result[key] = b[key]
            return result
        
        # For non-interpolatable types, return a or b based on progress
        return a if t < 0.5 else b


@dataclass
class Particle:
    """A single particle in a particle system.
    
    Attributes:
        x: X position
        y: Y position
        vx: X velocity
        vy: Y velocity
        life: Remaining life (0.0 to 1.0)
        color: Particle color (R, G, B)
        size: Particle size
    """
    x: float
    y: float
    vx: float = 0.0
    vy: float = 0.0
    life: float = 1.0
    color: Tuple[int, int, int] = (255, 255, 255)
    size: int = 1
    age: float = 0.0


class ParticleEmitter:
    """Particle emitter for creating particle effects.
    
    Creates and manages particles with configurable emission patterns,
    physics, and visual properties.
    """
    
    def __init__(
        self,
        x: float = 0,
        y: float = 0,
        emission_rate: float = 10.0,
        max_particles: int = 100,
        particle_life: float = 1.0,
        initial_velocity: Tuple[float, float] = (0, -5),
        velocity_variance: Tuple[float, float] = (2, 2),
        gravity: float = 0.0,
        color: Tuple[int, int, int] = (255, 255, 255),
        color_variance: Tuple[int, int, int] = (0, 0, 0),
        size: int = 1,
        size_variance: int = 0,
        fade_out: bool = True,
        emission_shape: str = "point"  # point, circle, line
    ):
        """Initialize particle emitter.
        
        Args:
            x: Emitter X position
            y: Emitter Y position
            emission_rate: Particles per second
            max_particles: Maximum active particles
            particle_life: Particle lifetime in seconds
            initial_velocity: Base velocity (vx, vy)
            velocity_variance: Random velocity variation
            gravity: Gravity applied to particles
            color: Base particle color
            color_variance: Random color variation
            size: Base particle size
            size_variance: Random size variation
            fade_out: Whether particles fade as they age
            emission_shape: Shape of emission area
        """
        self.x = x
        self.y = y
        self.emission_rate = emission_rate
        self.max_particles = max_particles
        self.particle_life = particle_life
        self.initial_velocity = initial_velocity
        self.velocity_variance = velocity_variance
        self.gravity = gravity
        self.color = color
        self.color_variance = color_variance
        self.size = size
        self.size_variance = size_variance
        self.fade_out = fade_out
        self.emission_shape = emission_shape
        
        self.particles: List[Particle] = []
        self._emission_accumulator = 0.0
        self._last_update = time.perf_counter()
        self._active = True
    
    def start(self) -> None:
        """Start emitting particles."""
        self._active = True
        self._last_update = time.perf_counter()
    
    def stop(self) -> None:
        """Stop emitting new particles."""
        self._active = False
    
    def clear(self) -> None:
        """Clear all particles."""
        self.particles.clear()
    
    def update(self, dt: Optional[float] = None) -> None:
        """Update particle system.
        
        Args:
            dt: Delta time in seconds (auto-calculated if None)
        """
        current_time = time.perf_counter()
        if dt is None:
            dt = current_time - self._last_update
        self._last_update = current_time
        
        # Update existing particles
        for particle in self.particles[:]:
            particle.x += particle.vx * dt
            particle.y += particle.vy * dt
            particle.vy += self.gravity * dt
            particle.age += dt
            particle.life = max(0, 1 - particle.age / self.particle_life)
            
            if particle.life <= 0:
                self.particles.remove(particle)
        
        # Emit new particles
        if self._active:
            self._emission_accumulator += self.emission_rate * dt
            while self._emission_accumulator >= 1 and len(self.particles) < self.max_particles:
                self._emit_particle()
                self._emission_accumulator -= 1
    
    def _emit_particle(self) -> None:
        """Emit a single particle."""
        # Calculate spawn position
        px, py = self.x, self.y
        if self.emission_shape == "circle":
            angle = random.random() * 2 * math.pi
            radius = random.random() * 5
            px += math.cos(angle) * radius
            py += math.sin(angle) * radius
        
        # Calculate velocity
        vx = self.initial_velocity[0] + (random.random() - 0.5) * 2 * self.velocity_variance[0]
        vy = self.initial_velocity[1] + (random.random() - 0.5) * 2 * self.velocity_variance[1]
        
        # Calculate color
        r = max(0, min(255, self.color[0] + int((random.random() - 0.5) * 2 * self.color_variance[0])))
        g = max(0, min(255, self.color[1] + int((random.random() - 0.5) * 2 * self.color_variance[1])))
        b = max(0, min(255, self.color[2] + int((random.random() - 0.5) * 2 * self.color_variance[2])))
        
        # Calculate size
        size = max(1, self.size + int((random.random() - 0.5) * 2 * self.size_variance))
        
        self.particles.append(Particle(
            x=px, y=py, vx=vx, vy=vy,
            color=(r, g, b), size=size
        ))
    
    def get_particles(self) -> List[Dict[str, Any]]:
        """Get particle data for rendering.
        
        Returns:
            List of particle dictionaries with position, color, size
        """
        result = []
        for p in self.particles:
            color = p.color
            if self.fade_out:
                # Fade color based on life
                color = (
                    int(p.color[0] * p.life),
                    int(p.color[1] * p.life),
                    int(p.color[2] * p.life)
                )
            result.append({
                'x': int(p.x),
                'y': int(p.y),
                'color': color,
                'size': p.size,
                'life': p.life
            })
        return result


class AnimationPreset:
    """Pre-built animation presets for common effects."""
    
    @staticmethod
    def fade_in(duration: float = 0.5) -> Animation:
        """Create a fade-in animation (opacity 0 to 1)."""
        anim = Animation(duration=duration)
        anim.add_keyframe(0.0, 0.0, EasingType.EASE_OUT)
        anim.add_keyframe(1.0, 1.0)
        return anim
    
    @staticmethod
    def fade_out(duration: float = 0.5) -> Animation:
        """Create a fade-out animation (opacity 1 to 0)."""
        anim = Animation(duration=duration)
        anim.add_keyframe(0.0, 1.0, EasingType.EASE_IN)
        anim.add_keyframe(1.0, 0.0)
        return anim
    
    @staticmethod
    def slide_in_left(width: int, duration: float = 0.5) -> Animation:
        """Create a slide-in from left animation."""
        anim = Animation(duration=duration)
        anim.add_keyframe(0.0, -width, EasingType.EASE_OUT_CUBIC)
        anim.add_keyframe(1.0, 0)
        return anim
    
    @staticmethod
    def slide_in_right(width: int, duration: float = 0.5) -> Animation:
        """Create a slide-in from right animation."""
        anim = Animation(duration=duration)
        anim.add_keyframe(0.0, width, EasingType.EASE_OUT_CUBIC)
        anim.add_keyframe(1.0, 0)
        return anim
    
    @staticmethod
    def slide_in_top(height: int, duration: float = 0.5) -> Animation:
        """Create a slide-in from top animation."""
        anim = Animation(duration=duration)
        anim.add_keyframe(0.0, -height, EasingType.EASE_OUT_CUBIC)
        anim.add_keyframe(1.0, 0)
        return anim
    
    @staticmethod
    def slide_in_bottom(height: int, duration: float = 0.5) -> Animation:
        """Create a slide-in from bottom animation."""
        anim = Animation(duration=duration)
        anim.add_keyframe(0.0, height, EasingType.EASE_OUT_CUBIC)
        anim.add_keyframe(1.0, 0)
        return anim
    
    @staticmethod
    def pulse(duration: float = 1.0, min_scale: float = 0.9, 
              max_scale: float = 1.1) -> Animation:
        """Create a pulsing animation."""
        anim = Animation(duration=duration, loop=True, reverse=True)
        anim.add_keyframe(0.0, min_scale, EasingType.EASE_IN_OUT)
        anim.add_keyframe(1.0, max_scale)
        return anim
    
    @staticmethod
    def bounce(duration: float = 0.6) -> Animation:
        """Create a bounce animation."""
        anim = Animation(duration=duration)
        anim.add_keyframe(0.0, 0.0, EasingType.EASE_OUT_BOUNCE)
        anim.add_keyframe(1.0, 1.0)
        return anim
    
    @staticmethod
    def shake(duration: float = 0.5, intensity: float = 5.0) -> Animation:
        """Create a shake animation."""
        anim = Animation(duration=duration)
        # Create multiple keyframes for shake effect
        steps = 8
        for i in range(steps + 1):
            t = i / steps
            if i == 0 or i == steps:
                value = 0
            else:
                value = intensity * (1 if i % 2 == 0 else -1) * (1 - t)
            anim.add_keyframe(t, value, EasingType.LINEAR)
        return anim
    
    @staticmethod
    def breathe(duration: float = 2.0) -> Animation:
        """Create a breathing animation (gentle pulse)."""
        anim = Animation(duration=duration, loop=True, reverse=True)
        anim.add_keyframe(0.0, 0.7, EasingType.EASE_IN_OUT)
        anim.add_keyframe(1.0, 1.0)
        return anim
    
    @staticmethod
    def color_cycle(colors: List[Tuple[int, int, int]], 
                    duration: float = 3.0) -> Animation:
        """Create a color cycling animation.
        
        Args:
            colors: List of RGB color tuples to cycle through
            duration: Total cycle duration
            
        Returns:
            Color cycling animation
        """
        if not colors:
            colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
        
        anim = Animation(duration=duration, loop=True)
        step = 1.0 / len(colors)
        for i, color in enumerate(colors):
            anim.add_keyframe(i * step, color, EasingType.LINEAR)
        # Close the loop
        anim.add_keyframe(1.0, colors[0])
        return anim


class TransitionEffect:
    """Transition effects between frames."""
    
    @staticmethod
    def fade(
        from_image: Any, 
        to_image: Any, 
        progress: float
    ) -> Tuple[Any, float]:
        """Fade transition - cross-fade between images.
        
        Args:
            from_image: Source image
            to_image: Destination image
            progress: Transition progress (0.0 to 1.0)
            
        Returns:
            Tuple of (blended image, opacity)
        """
        return (to_image, progress)
    
    @staticmethod
    def slide_left(
        width: int,
        progress: float
    ) -> Tuple[int, int]:
        """Slide left transition - returns offset positions.
        
        Args:
            width: Display width
            progress: Transition progress
            
        Returns:
            Tuple of (from_offset, to_offset)
        """
        eased = Easing.ease_in_out_cubic(progress)
        offset = int(width * eased)
        return (-offset, width - offset)
    
    @staticmethod
    def slide_right(
        width: int,
        progress: float
    ) -> Tuple[int, int]:
        """Slide right transition."""
        eased = Easing.ease_in_out_cubic(progress)
        offset = int(width * eased)
        return (offset, -width + offset)
    
    @staticmethod
    def slide_up(
        height: int,
        progress: float
    ) -> Tuple[int, int]:
        """Slide up transition - returns offset positions."""
        eased = Easing.ease_in_out_cubic(progress)
        offset = int(height * eased)
        return (-offset, height - offset)
    
    @staticmethod
    def slide_down(
        height: int,
        progress: float
    ) -> Tuple[int, int]:
        """Slide down transition."""
        eased = Easing.ease_in_out_cubic(progress)
        offset = int(height * eased)
        return (offset, -height + offset)
    
    @staticmethod
    def wipe_left(
        width: int,
        progress: float
    ) -> int:
        """Wipe left transition - returns wipe position."""
        return int(width * (1 - progress))
    
    @staticmethod
    def wipe_right(
        width: int,
        progress: float
    ) -> int:
        """Wipe right transition."""
        return int(width * progress)


class AnimationManager:
    """Manages multiple animations and their updates."""
    
    def __init__(self):
        """Initialize animation manager."""
        self.animations: Dict[str, Animation] = {}
        self.particle_systems: Dict[str, ParticleEmitter] = {}
    
    def add_animation(self, name: str, animation: Animation) -> None:
        """Add an animation.
        
        Args:
            name: Unique name for the animation
            animation: Animation instance
        """
        self.animations[name] = animation
    
    def remove_animation(self, name: str) -> None:
        """Remove an animation.
        
        Args:
            name: Animation name to remove
        """
        if name in self.animations:
            del self.animations[name]
    
    def add_particle_system(self, name: str, emitter: ParticleEmitter) -> None:
        """Add a particle system.
        
        Args:
            name: Unique name for the particle system
            emitter: ParticleEmitter instance
        """
        self.particle_systems[name] = emitter
    
    def remove_particle_system(self, name: str) -> None:
        """Remove a particle system.
        
        Args:
            name: Particle system name to remove
        """
        if name in self.particle_systems:
            del self.particle_systems[name]
    
    def start_animation(self, name: str) -> None:
        """Start a specific animation.
        
        Args:
            name: Animation name to start
        """
        if name in self.animations:
            self.animations[name].start()
    
    def stop_animation(self, name: str) -> None:
        """Stop a specific animation.
        
        Args:
            name: Animation name to stop
        """
        if name in self.animations:
            self.animations[name].stop()
    
    def start_all(self) -> None:
        """Start all animations and particle systems."""
        for anim in self.animations.values():
            anim.start()
        for emitter in self.particle_systems.values():
            emitter.start()
    
    def stop_all(self) -> None:
        """Stop all animations and particle systems."""
        for anim in self.animations.values():
            anim.stop()
        for emitter in self.particle_systems.values():
            emitter.stop()
    
    def update(self, dt: Optional[float] = None) -> None:
        """Update all animations and particle systems.
        
        Args:
            dt: Delta time (optional)
        """
        for emitter in self.particle_systems.values():
            emitter.update(dt)
    
    def get_animation_value(self, name: str) -> Any:
        """Get current value of an animation.
        
        Args:
            name: Animation name
            
        Returns:
            Current interpolated value
        """
        if name in self.animations:
            return self.animations[name].get_value()
        return None
    
    def get_particle_data(self, name: str) -> List[Dict[str, Any]]:
        """Get particle data for rendering.
        
        Args:
            name: Particle system name
            
        Returns:
            List of particle dictionaries
        """
        if name in self.particle_systems:
            return self.particle_systems[name].get_particles()
        return []
    
    def clear(self) -> None:
        """Clear all animations and particle systems."""
        self.animations.clear()
        self.particle_systems.clear()
