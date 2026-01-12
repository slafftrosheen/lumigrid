"""Display module package."""

from lumigrid.display.matrix import DisplayManager
from lumigrid.display.modes import DisplayMode, ClockMode, TemperatureMode, MessageMode, SlideshowMode
from lumigrid.display.frames import Frame, FrameManager, FrameMode

# Animation system
from lumigrid.display.animation import (
    Animation,
    AnimationManager,
    AnimationPreset,
    Easing,
    EasingType,
    Keyframe,
    ParticleEmitter,
    Particle,
    TransitionEffect,
)

# Visual effects
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
)

# Advanced frame types
from lumigrid.display.advanced_frames import (
    AdvancedFrameType,
    ChartType,
    FrameElement,
    DataVisualizationFrame,
    TemplateFrame,
    ConditionalFrame,
    MultiPageFrame,
    ScrollingFrame,
    AnimatedFrame,
    FrameLibrary,
    FrameScheduler,
    FrameGroup,
)

__all__ = [
    # Core display
    'DisplayManager',
    'DisplayMode',
    'ClockMode',
    'TemperatureMode',
    'MessageMode',
    'SlideshowMode',
    'Frame',
    'FrameManager',
    'FrameMode',
    # Animation
    'Animation',
    'AnimationManager',
    'AnimationPreset',
    'Easing',
    'EasingType',
    'Keyframe',
    'ParticleEmitter',
    'Particle',
    'TransitionEffect',
    # Effects
    'EffectsManager',
    'EffectType',
    'BackgroundType',
    'GradientGenerator',
    'ColorStop',
    'PlasmaEffect',
    'MatrixRainEffect',
    'StarfieldEffect',
    'FireEffect',
    'WaveEffect',
    'GeometricPattern',
    'TimeBased',
    # Advanced frames
    'AdvancedFrameType',
    'ChartType',
    'FrameElement',
    'DataVisualizationFrame',
    'TemplateFrame',
    'ConditionalFrame',
    'MultiPageFrame',
    'ScrollingFrame',
    'AnimatedFrame',
    'FrameLibrary',
    'FrameScheduler',
    'FrameGroup',
]
