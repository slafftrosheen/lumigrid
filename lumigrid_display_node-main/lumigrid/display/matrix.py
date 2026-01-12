"""LED Matrix Display Manager using hzeller's rpi-rgb-led-matrix library."""

import logging
import time
from typing import Any, Dict, Optional, Tuple

from PIL import Image, ImageDraw, ImageFont

from lumigrid.validators import MAX_FONT_CACHE_SIZE

logger = logging.getLogger(__name__)


class DisplayManager:
    """Manages the LED matrix display.
    
    This class handles all interactions with the RGB LED matrix hardware,
    including drawing text, images, and scrolling content.
    """

    def __init__(self, config: Dict[str, Any]) -> None:
        """Initialize the display manager.

        Args:
            config: Configuration dictionary for LED matrix settings
        """
        self.config = config
        self.matrix: Any = None
        self.canvas: Any = None
        self.width: int = config.get('cols', 64)
        self.height: int = config.get('rows', 32)
        
        # Reusable image buffer to avoid creating new images every frame
        self._buffer_image: Optional[Image.Image] = None
        self._buffer_draw: Optional[ImageDraw.ImageDraw] = None
        
        # Font cache to avoid reloading fonts repeatedly
        self._font_cache: Dict[Tuple[Optional[str], int], ImageFont.FreeTypeFont] = {}
        
        # Track last displayed content to avoid unnecessary updates
        self._last_image_hash: Optional[int] = None
        
        self._initialize_matrix()
        self._create_buffer()

    def _create_buffer(self) -> None:
        """Create reusable image buffer."""
        self._buffer_image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        self._buffer_draw = ImageDraw.Draw(self._buffer_image)

    def _initialize_matrix(self) -> None:
        """Initialize the RGB LED matrix."""
        try:
            from rgbmatrix import RGBMatrix, RGBMatrixOptions
            
            options = RGBMatrixOptions()
            options.rows = self.config.get('rows', 32)
            options.cols = self.config.get('cols', 64)
            options.chain_length = self.config.get('chain_length', 1)
            options.parallel = self.config.get('parallel', 1)
            options.hardware_mapping = self.config.get('hardware_mapping', 'regular')
            options.brightness = self.config.get('brightness', 50)
            options.gpio_slowdown = self.config.get('gpio_slowdown', 4)
            options.pwm_bits = self.config.get('pwm_bits', 11)
            options.pwm_lsb_nanoseconds = self.config.get('pwm_lsb_nanoseconds', 130)
            options.disable_hardware_pulsing = self.config.get('disable_hardware_pulsing', False)

            self.matrix = RGBMatrix(options=options)
            self.canvas = self.matrix.CreateFrameCanvas()
            logger.info("LED Matrix initialized successfully")
        except ImportError:
            logger.warning("rgbmatrix library not available, using simulation mode")
            self.matrix = None
            self.canvas = None
        except Exception as e:
            logger.error(f"Error initializing LED matrix: {e}")
            self.matrix = None
            self.canvas = None

    def _get_font(self, font_path: Optional[str], font_size: int) -> ImageFont.FreeTypeFont:
        """Get font from cache or load it.
        
        Args:
            font_path: Path to TrueType font file
            font_size: Font size
            
        Returns:
            Font object
        """
        cache_key = (font_path, font_size)
        
        if cache_key not in self._font_cache:
            try:
                if font_path:
                    font = ImageFont.truetype(font_path, font_size)
                else:
                    font = ImageFont.load_default()
            except Exception as e:
                logger.warning(f"Error loading font {font_path}: {e}, using default")
                font = ImageFont.load_default()
            
            # Limit cache size to prevent memory issues
            if len(self._font_cache) > MAX_FONT_CACHE_SIZE:
                # Remove oldest entry (first key)
                oldest_key = next(iter(self._font_cache))
                del self._font_cache[oldest_key]
            
            self._font_cache[cache_key] = font
        
        return self._font_cache[cache_key]

    def clear(self) -> None:
        """Clear the display."""
        if self.canvas:
            self.canvas.Clear()
        
        # Also clear the buffer
        if self._buffer_draw and self._buffer_image:
            self._buffer_draw.rectangle(
                [(0, 0), (self.width, self.height)], 
                fill=(0, 0, 0)
            )
        
        self._last_image_hash = None
        logger.debug("Display cleared")

    def show_image(self, image: Image.Image) -> None:
        """Display a PIL Image on the matrix.

        Args:
            image: PIL Image to display
        """
        if not self.matrix:
            logger.debug("Matrix not available, cannot display image")
            return

        # Resize image to fit matrix if needed
        if image.size != (self.width, self.height):
            image = image.resize((self.width, self.height))

        # Convert image to RGB if necessary
        if image.mode != 'RGB':
            image = image.convert('RGB')

        # Set pixels on canvas
        self.canvas.SetImage(image)
        self.canvas = self.matrix.SwapOnVSync(self.canvas)

    def draw_text(self, text: str, x: int = 0, y: int = 10, 
                  color: Tuple[int, int, int] = (255, 255, 255),
                  font_path: Optional[str] = None, font_size: int = 10,
                  background: Tuple[int, int, int] = (0, 0, 0)) -> None:
        """Draw text on the display.

        Args:
            text: Text to display
            x: X coordinate
            y: Y coordinate
            color: RGB color tuple
            font_path: Path to TrueType font file
            font_size: Font size
            background: Background color RGB tuple
        """
        if not self._buffer_draw or not self._buffer_image:
            self._create_buffer()
        
        # Clear buffer with background color
        self._buffer_draw.rectangle(
            [(0, 0), (self.width, self.height)], 
            fill=background
        )

        # Get cached font
        font = self._get_font(font_path, font_size)

        # Draw text
        self._buffer_draw.text((x, y), text, font=font, fill=color)

        # Display image
        self.show_image(self._buffer_image)

    def scroll_text(self, text: str, color: Tuple[int, int, int] = (255, 255, 255),
                    font_path: Optional[str] = None, font_size: int = 10,
                    speed: float = 0.05, y_pos: int = 10,
                    background: Tuple[int, int, int] = (0, 0, 0)) -> None:
        """Scroll text across the display.

        This method has been optimized to reuse the image buffer instead of
        creating a new image for each frame, reducing memory allocations.

        Args:
            text: Text to scroll
            color: RGB color tuple
            font_path: Path to TrueType font file
            font_size: Font size
            speed: Scroll speed (seconds per pixel)
            y_pos: Y position for text
            background: Background color RGB tuple
        """
        if not self._buffer_draw or not self._buffer_image:
            self._create_buffer()
        
        # Get cached font
        font = self._get_font(font_path, font_size)

        # Get text width using textbbox
        bbox = self._buffer_draw.textbbox((0, 0), text, font=font)
        text_width = bbox[2] - bbox[0]

        # Scroll from right to left, reusing the buffer
        for x in range(self.width, -text_width, -1):
            # Clear buffer with background color
            self._buffer_draw.rectangle(
                [(0, 0), (self.width, self.height)], 
                fill=background
            )
            # Draw text at current position
            self._buffer_draw.text((x, y_pos), text, font=font, fill=color)
            # Display the buffer
            self.show_image(self._buffer_image)
            time.sleep(speed)

    def set_brightness(self, brightness: int) -> None:
        """Set display brightness.

        Args:
            brightness: Brightness value (0-100)
        """
        if self.matrix:
            self.matrix.brightness = max(0, min(100, brightness))
            logger.info(f"Brightness set to {brightness}")

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.canvas:
            self.canvas.Clear()
        if self.matrix:
            self.matrix.Clear()
        
        # Clear font cache
        self._font_cache.clear()
        
        # Clear buffer references
        self._buffer_image = None
        self._buffer_draw = None
        
        logger.info("Display cleaned up")
