"""Tests for the animation module."""

import pytest
import time
from unittest.mock import patch, MagicMock

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


class TestEasing:
    """Tests for Easing functions."""
    
    def test_linear(self):
        """Test linear easing returns input unchanged."""
        assert Easing.linear(0.0) == 0.0
        assert Easing.linear(0.5) == 0.5
        assert Easing.linear(1.0) == 1.0
    
    def test_ease_in(self):
        """Test ease_in starts slow."""
        # Ease in should be slower at the start
        assert Easing.ease_in(0.0) == pytest.approx(0.0, abs=0.01)
        assert Easing.ease_in(0.5) < 0.5  # Slower at midpoint
        assert Easing.ease_in(1.0) == pytest.approx(1.0, abs=0.01)
    
    def test_ease_out(self):
        """Test ease_out starts fast."""
        # Ease out should be faster at the start
        assert Easing.ease_out(0.0) == pytest.approx(0.0, abs=0.01)
        assert Easing.ease_out(0.5) > 0.5  # Faster at midpoint
        assert Easing.ease_out(1.0) == pytest.approx(1.0, abs=0.01)
    
    def test_ease_in_out(self):
        """Test ease_in_out is symmetric."""
        assert Easing.ease_in_out(0.0) == pytest.approx(0.0, abs=0.01)
        assert Easing.ease_in_out(0.5) == pytest.approx(0.5, abs=0.01)
        assert Easing.ease_in_out(1.0) == pytest.approx(1.0, abs=0.01)
    
    def test_ease_out_bounce(self):
        """Test bounce easing."""
        # Should stay within 0-1 range
        result = Easing.ease_out_bounce(0.5)
        assert 0 <= result <= 1
        assert Easing.ease_out_bounce(0.0) == pytest.approx(0.0, abs=0.01)
    
    def test_ease_in_elastic(self):
        """Test elastic easing handles edge cases."""
        assert Easing.ease_in_elastic(0.0) == 0.0
        assert Easing.ease_in_elastic(1.0) == 1.0
    
    def test_get_function_by_type(self):
        """Test getting easing function by type."""
        func = Easing.get_function(EasingType.LINEAR)
        assert func(0.5) == 0.5
        
        func = Easing.get_function("linear")
        assert func(0.5) == 0.5
    
    def test_get_function_invalid_returns_linear(self):
        """Test invalid easing type returns linear."""
        func = Easing.get_function("nonexistent")
        assert func(0.5) == 0.5


class TestKeyframe:
    """Tests for Keyframe class."""
    
    def test_keyframe_creation(self):
        """Test keyframe creation with defaults."""
        kf = Keyframe(time=0.5, value=100)
        assert kf.time == 0.5
        assert kf.value == 100
        assert kf.easing == EasingType.LINEAR
    
    def test_keyframe_time_clamping(self):
        """Test keyframe time is clamped to 0-1."""
        kf1 = Keyframe(time=-0.5, value=0)
        assert kf1.time == 0.0
        
        kf2 = Keyframe(time=1.5, value=100)
        assert kf2.time == 1.0
    
    def test_keyframe_with_easing(self):
        """Test keyframe with custom easing."""
        kf = Keyframe(time=0.5, value=50, easing=EasingType.EASE_OUT)
        assert kf.easing == EasingType.EASE_OUT


class TestAnimation:
    """Tests for Animation class."""
    
    def test_animation_creation(self):
        """Test animation creation."""
        anim = Animation(duration=2.0, loop=True)
        assert anim.duration == 2.0
        assert anim.loop is True
        assert len(anim.keyframes) == 0
    
    def test_add_keyframe(self):
        """Test adding keyframes."""
        anim = Animation()
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        assert len(anim.keyframes) == 2
    
    def test_keyframes_sorted(self):
        """Test keyframes are sorted by time."""
        anim = Animation()
        anim.add_keyframe(1.0, 100)
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(0.5, 50)
        
        assert anim.keyframes[0].time == 0.0
        assert anim.keyframes[1].time == 0.5
        assert anim.keyframes[2].time == 1.0
    
    def test_animation_start_stop(self):
        """Test animation start and stop."""
        anim = Animation()
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        
        assert anim._start_time is None
        anim.start()
        assert anim._start_time is not None
        anim.stop()
        assert anim._start_time is None
    
    def test_animation_pause_resume(self):
        """Test animation pause and resume."""
        anim = Animation()
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        
        anim.start()
        assert not anim._paused
        
        anim.pause()
        assert anim._paused
        
        anim.resume()
        assert not anim._paused
    
    def test_get_value_interpolation(self):
        """Test value interpolation."""
        anim = Animation(duration=1.0)
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        
        # Mock progress at 50%
        with patch.object(anim, 'get_progress', return_value=0.5):
            value = anim.get_value()
            assert value == pytest.approx(50.0, abs=1)
    
    def test_interpolation_with_tuples(self):
        """Test interpolating tuple values (like colors)."""
        anim = Animation(duration=1.0)
        anim.add_keyframe(0.0, (0, 0, 0))
        anim.add_keyframe(1.0, (255, 255, 255))
        
        with patch.object(anim, 'get_progress', return_value=0.5):
            value = anim.get_value()
            assert value[0] == pytest.approx(127.5, abs=1)
            assert value[1] == pytest.approx(127.5, abs=1)
            assert value[2] == pytest.approx(127.5, abs=1)
    
    def test_interpolation_with_dicts(self):
        """Test interpolating dict values."""
        anim = Animation(duration=1.0)
        anim.add_keyframe(0.0, {'x': 0, 'y': 0})
        anim.add_keyframe(1.0, {'x': 100, 'y': 200})
        
        with patch.object(anim, 'get_progress', return_value=0.5):
            value = anim.get_value()
            assert value['x'] == pytest.approx(50.0, abs=1)
            assert value['y'] == pytest.approx(100.0, abs=1)
    
    def test_is_complete_non_looping(self):
        """Test is_complete for non-looping animation."""
        anim = Animation(duration=1.0, loop=False)
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        
        with patch.object(anim, 'get_progress', return_value=0.5):
            assert not anim.is_complete()
        
        with patch.object(anim, 'get_progress', return_value=1.0):
            assert anim.is_complete()
    
    def test_is_complete_looping(self):
        """Test is_complete always False for looping animation."""
        anim = Animation(duration=1.0, loop=True)
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        
        assert not anim.is_complete()


class TestAnimationPreset:
    """Tests for AnimationPreset class."""
    
    def test_fade_in(self):
        """Test fade_in preset."""
        anim = AnimationPreset.fade_in(duration=0.5)
        assert anim.duration == 0.5
        assert len(anim.keyframes) == 2
        assert anim.keyframes[0].value == 0.0
        assert anim.keyframes[1].value == 1.0
    
    def test_fade_out(self):
        """Test fade_out preset."""
        anim = AnimationPreset.fade_out(duration=0.5)
        assert anim.duration == 0.5
        assert anim.keyframes[0].value == 1.0
        assert anim.keyframes[1].value == 0.0
    
    def test_slide_in_left(self):
        """Test slide_in_left preset."""
        anim = AnimationPreset.slide_in_left(width=64, duration=0.5)
        assert anim.duration == 0.5
        assert anim.keyframes[0].value == -64
        assert anim.keyframes[1].value == 0
    
    def test_pulse(self):
        """Test pulse preset."""
        anim = AnimationPreset.pulse(duration=1.0)
        assert anim.duration == 1.0
        assert anim.loop is True
        assert anim.reverse is True
    
    def test_bounce(self):
        """Test bounce preset."""
        anim = AnimationPreset.bounce(duration=0.6)
        assert anim.duration == 0.6
        assert anim.keyframes[0].easing == EasingType.EASE_OUT_BOUNCE
    
    def test_shake(self):
        """Test shake preset."""
        anim = AnimationPreset.shake(duration=0.5)
        assert anim.duration == 0.5
        assert len(anim.keyframes) > 2  # Multiple keyframes for shake
    
    def test_color_cycle(self):
        """Test color_cycle preset."""
        colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
        anim = AnimationPreset.color_cycle(colors, duration=3.0)
        assert anim.duration == 3.0
        assert anim.loop is True


class TestParticleEmitter:
    """Tests for ParticleEmitter class."""
    
    def test_emitter_creation(self):
        """Test particle emitter creation."""
        emitter = ParticleEmitter(
            x=32, y=16,
            emission_rate=10.0,
            max_particles=50
        )
        assert emitter.x == 32
        assert emitter.y == 16
        assert emitter.emission_rate == 10.0
        assert emitter.max_particles == 50
    
    def test_emitter_start_stop(self):
        """Test starting and stopping emitter."""
        emitter = ParticleEmitter()
        assert emitter._active is True
        
        emitter.stop()
        assert emitter._active is False
        
        emitter.start()
        assert emitter._active is True
    
    def test_emitter_clear(self):
        """Test clearing all particles."""
        emitter = ParticleEmitter()
        # Manually add a particle
        emitter.particles.append(Particle(x=10, y=10))
        assert len(emitter.particles) == 1
        
        emitter.clear()
        assert len(emitter.particles) == 0
    
    def test_emitter_update_creates_particles(self):
        """Test that update creates particles."""
        emitter = ParticleEmitter(
            emission_rate=100.0,
            max_particles=10
        )
        emitter.start()
        
        # Update with a small time step
        emitter.update(dt=0.1)
        
        # Should have created some particles
        assert len(emitter.particles) > 0
    
    def test_emitter_respects_max_particles(self):
        """Test that emitter respects max_particles limit."""
        emitter = ParticleEmitter(
            emission_rate=1000.0,
            max_particles=5
        )
        emitter.start()
        
        # Update multiple times
        for _ in range(10):
            emitter.update(dt=0.1)
        
        assert len(emitter.particles) <= 5
    
    def test_get_particles_returns_data(self):
        """Test get_particles returns renderable data."""
        emitter = ParticleEmitter(
            emission_rate=100.0,
            max_particles=5
        )
        emitter.start()
        emitter.update(dt=0.1)
        
        particles = emitter.get_particles()
        
        if particles:
            p = particles[0]
            assert 'x' in p
            assert 'y' in p
            assert 'color' in p
            assert 'size' in p
            assert 'life' in p


class TestTransitionEffect:
    """Tests for TransitionEffect class."""
    
    def test_fade_transition(self):
        """Test fade transition."""
        result = TransitionEffect.fade(None, None, 0.5)
        assert result[1] == 0.5  # Opacity
    
    def test_slide_left(self):
        """Test slide left transition."""
        from_offset, to_offset = TransitionEffect.slide_left(64, 0.5)
        assert from_offset < 0  # Moving left
        assert to_offset > 0  # Coming from right
    
    def test_slide_right(self):
        """Test slide right transition."""
        from_offset, to_offset = TransitionEffect.slide_right(64, 0.5)
        assert from_offset > 0
        assert to_offset < 0
    
    def test_wipe_left(self):
        """Test wipe left transition."""
        position = TransitionEffect.wipe_left(64, 0.5)
        assert 0 <= position <= 64


class TestAnimationManager:
    """Tests for AnimationManager class."""
    
    def test_manager_creation(self):
        """Test animation manager creation."""
        manager = AnimationManager()
        assert len(manager.animations) == 0
        assert len(manager.particle_systems) == 0
    
    def test_add_animation(self):
        """Test adding animations."""
        manager = AnimationManager()
        anim = Animation()
        manager.add_animation("test", anim)
        assert "test" in manager.animations
    
    def test_remove_animation(self):
        """Test removing animations."""
        manager = AnimationManager()
        anim = Animation()
        manager.add_animation("test", anim)
        manager.remove_animation("test")
        assert "test" not in manager.animations
    
    def test_add_particle_system(self):
        """Test adding particle systems."""
        manager = AnimationManager()
        emitter = ParticleEmitter()
        manager.add_particle_system("particles", emitter)
        assert "particles" in manager.particle_systems
    
    def test_start_all(self):
        """Test starting all animations."""
        manager = AnimationManager()
        anim = Animation()
        manager.add_animation("test", anim)
        
        manager.start_all()
        assert anim._start_time is not None
    
    def test_stop_all(self):
        """Test stopping all animations."""
        manager = AnimationManager()
        anim = Animation()
        anim.start()
        manager.add_animation("test", anim)
        
        manager.stop_all()
        assert anim._start_time is None
    
    def test_get_animation_value(self):
        """Test getting animation value."""
        manager = AnimationManager()
        anim = Animation(duration=1.0)
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        manager.add_animation("test", anim)
        
        value = manager.get_animation_value("test")
        assert value is not None
    
    def test_get_particle_data(self):
        """Test getting particle data."""
        manager = AnimationManager()
        emitter = ParticleEmitter()
        emitter.particles.append(Particle(x=10, y=10))
        manager.add_particle_system("particles", emitter)
        
        data = manager.get_particle_data("particles")
        assert len(data) == 1
    
    def test_clear(self):
        """Test clearing manager."""
        manager = AnimationManager()
        manager.add_animation("anim", Animation())
        manager.add_particle_system("ps", ParticleEmitter())
        
        manager.clear()
        assert len(manager.animations) == 0
        assert len(manager.particle_systems) == 0
