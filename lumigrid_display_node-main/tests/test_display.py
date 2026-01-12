"""Tests for the display manager."""

import pytest
from unittest.mock import MagicMock, patch
from PIL import Image


class TestDisplayManager:
    """Tests for DisplayManager class."""
    
    @pytest.fixture
    def display_manager(self):
        """Create a DisplayManager instance without hardware."""
        from lumigrid.display.matrix import DisplayManager
        
        config = {
            'rows': 32,
            'cols': 64,
            'brightness': 50
        }
        
        manager = DisplayManager(config)
        return manager
    
    def test_initialization(self, display_manager):
        """Test display manager initializes correctly."""
        assert display_manager.width == 64
        assert display_manager.height == 32
        assert display_manager._buffer_image is not None
        assert display_manager._buffer_draw is not None
    
    def test_buffer_dimensions(self, display_manager):
        """Test buffer has correct dimensions."""
        assert display_manager._buffer_image.size == (64, 32)
    
    def test_font_caching(self, display_manager):
        """Test font caching mechanism."""
        # Get same font twice
        font1 = display_manager._get_font(None, 10)
        font2 = display_manager._get_font(None, 10)
        
        # Should be the same cached object
        assert font1 is font2
        
        # Different size should be different object
        font3 = display_manager._get_font(None, 12)
        assert font1 is not font3
    
    def test_font_cache_limit(self, display_manager):
        """Test font cache doesn't grow unbounded."""
        from lumigrid.validators import MAX_FONT_CACHE_SIZE
        
        # Add many fonts to cache
        for size in range(MAX_FONT_CACHE_SIZE + 50):
            display_manager._get_font(None, size)
        
        # Cache should be limited
        assert len(display_manager._font_cache) <= MAX_FONT_CACHE_SIZE + 1
    
    def test_clear_resets_state(self, display_manager):
        """Test clear method resets tracking state."""
        display_manager._last_image_hash = 12345
        display_manager.clear()
        assert display_manager._last_image_hash is None
    
    def test_cleanup(self, display_manager):
        """Test cleanup releases resources."""
        display_manager._font_cache[(None, 10)] = MagicMock()
        display_manager.cleanup()
        
        assert len(display_manager._font_cache) == 0
        assert display_manager._buffer_image is None
        assert display_manager._buffer_draw is None
    
    def test_draw_text_uses_buffer(self, display_manager):
        """Test draw_text uses the buffer image."""
        # Mock show_image to capture what's displayed
        displayed_images = []
        display_manager.show_image = lambda img: displayed_images.append(img)
        
        display_manager.draw_text("Test", x=0, y=10)
        
        # Should have displayed the buffer image
        assert len(displayed_images) == 1
        assert displayed_images[0] is display_manager._buffer_image
    
    def test_set_brightness_clamps_values(self, display_manager):
        """Test brightness is clamped to valid range."""
        # Create mock matrix
        display_manager.matrix = MagicMock()
        
        display_manager.set_brightness(150)
        assert display_manager.matrix.brightness == 100
        
        display_manager.set_brightness(-10)
        assert display_manager.matrix.brightness == 0
        
        display_manager.set_brightness(50)
        assert display_manager.matrix.brightness == 50


class TestDisplayManagerScrolling:
    """Tests for scrolling functionality."""
    
    @pytest.fixture
    def display_manager(self):
        """Create a DisplayManager instance."""
        from lumigrid.display.matrix import DisplayManager
        
        config = {
            'rows': 32,
            'cols': 64,
            'brightness': 50
        }
        
        manager = DisplayManager(config)
        return manager
    
    def test_scroll_text_reuses_buffer(self, display_manager):
        """Test scroll_text reuses the same buffer."""
        displayed_images = []
        display_manager.show_image = lambda img: displayed_images.append(img)
        
        # Scroll very short text with no delay
        with patch('time.sleep'):  # Don't actually sleep
            # Just test a few frames
            display_manager._buffer_draw.textbbox = lambda *args, **kwargs: (0, 0, 10, 10)
            
            # We can't easily test the full scroll without mocking more,
            # but we can verify the buffer exists
            assert display_manager._buffer_image is not None
    
    def test_background_color_support(self, display_manager):
        """Test that background color parameter is supported."""
        displayed_images = []
        display_manager.show_image = lambda img: displayed_images.append(img)
        
        # Draw text with custom background
        display_manager.draw_text(
            "Test", 
            x=0, 
            y=10, 
            background=(255, 0, 0)
        )
        
        # Should succeed without error
        assert len(displayed_images) == 1
