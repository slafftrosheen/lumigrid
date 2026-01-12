"""Frame-based display system for rotating content."""

import logging
import time
import uuid
from datetime import datetime
from typing import Optional, Dict, Any, List, Tuple, Callable

from PIL import Image, ImageDraw, ImageFont
import pytz

logger = logging.getLogger(__name__)


def hex_to_rgb(hex_color: str) -> Tuple[int, int, int]:
    """Convert hex color to RGB tuple."""
    hex_color = hex_color.lstrip('#')
    if len(hex_color) == 3:
        hex_color = ''.join([c*2 for c in hex_color])
    return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))


def get_position(pos_value: Any, dimension: int, element_size: int) -> int:
    """Calculate position based on config value."""
    if isinstance(pos_value, str):
        if pos_value == 'center':
            return (dimension - element_size) // 2
        elif pos_value == 'left' or pos_value == 'top':
            return 0
        elif pos_value == 'right' or pos_value == 'bottom':
            return dimension - element_size
    elif isinstance(pos_value, int):
        return pos_value
    return 0


class Frame:
    """Represents a single display frame with customization options."""

    def __init__(self, frame_config: Dict[str, Any]):
        """Initialize frame from configuration.

        Args:
            frame_config: Frame configuration dictionary
        """
        self.id = frame_config.get('id', str(uuid.uuid4())[:8])
        self.type = frame_config.get('type', 'text')
        self.name = frame_config.get('name', 'Unnamed Frame')
        self.enabled = frame_config.get('enabled', True)
        self.duration = frame_config.get('duration', 5)
        
        # Font configuration
        font_config = frame_config.get('font', {})
        self.font_path = font_config.get('path', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
        self.font_size = font_config.get('size', 14)
        
        # Color configuration
        colors = frame_config.get('colors', {})
        self.text_color = hex_to_rgb(colors.get('text', '#FFFFFF'))
        self.bg_color = hex_to_rgb(colors.get('background', '#000000'))
        self.accent_color = hex_to_rgb(colors.get('accent', '#00FFFF'))
        
        # Position
        self.position = frame_config.get('position', {'x': 'center', 'y': 'center'})
        
        # Type-specific options
        self.format = frame_config.get('format', '')
        self.unit = frame_config.get('unit', 'celsius')
        self.show_icon = frame_config.get('show_icon', False)
        self.text = frame_config.get('text', '')
        self.scroll = frame_config.get('scroll', False)
        
        # Full config for reference
        self.config = frame_config

    def to_dict(self) -> Dict[str, Any]:
        """Convert frame to dictionary for API/config."""
        return {
            'id': self.id,
            'type': self.type,
            'name': self.name,
            'enabled': self.enabled,
            'duration': self.duration,
            'font': {
                'path': self.font_path,
                'size': self.font_size
            },
            'colors': {
                'text': '#{:02x}{:02x}{:02x}'.format(*self.text_color),
                'background': '#{:02x}{:02x}{:02x}'.format(*self.bg_color),
                'accent': '#{:02x}{:02x}{:02x}'.format(*self.accent_color)
            },
            'position': self.position,
            'format': self.format,
            'unit': self.unit,
            'show_icon': self.show_icon,
            'text': self.text,
            'scroll': self.scroll
        }


class FrameManager:
    """Manages frame queue and rendering."""

    def __init__(self, display_manager, config: Dict[str, Any], 
                 temp_sensor=None, humidity_getter: Optional[Callable] = None):
        """Initialize frame manager.

        Args:
            display_manager: DisplayManager instance
            config: Full configuration dictionary
            temp_sensor: Temperature sensor instance
            humidity_getter: Callable that returns humidity value
        """
        self.display = display_manager
        self.full_config = config
        self.temp_sensor = temp_sensor
        self.humidity_getter = humidity_getter
        
        # Frame queue
        self.frames: List[Frame] = []
        self.current_index = 0
        self.last_switch_time = 0
        
        # Display dimensions
        self.width = display_manager.width if display_manager else 64
        self.height = display_manager.height if display_manager else 32
        
        # Timezone
        tz_name = config.get('time', {}).get('timezone', 'UTC')
        try:
            self.timezone = pytz.timezone(tz_name)
        except Exception:
            self.timezone = pytz.UTC
        
        # Load frames from config
        self._load_frames()

    def _load_frames(self):
        """Load frames from configuration."""
        frames_config = self.full_config.get('frames', {})
        frame_queue = frames_config.get('queue', [])
        
        self.frames = []
        for frame_config in frame_queue:
            frame = Frame(frame_config)
            self.frames.append(frame)
            logger.debug(f"Loaded frame: {frame.name} ({frame.type})")
        
        logger.info(f"Loaded {len(self.frames)} frames")

    def reload_frames(self, config: Dict[str, Any]):
        """Reload frames from new configuration."""
        self.full_config = config
        self._load_frames()

    def get_enabled_frames(self) -> List[Frame]:
        """Get list of enabled frames."""
        return [f for f in self.frames if f.enabled]

    def get_current_frame(self) -> Optional[Frame]:
        """Get the currently active frame."""
        enabled = self.get_enabled_frames()
        if not enabled:
            return None
        
        if self.current_index >= len(enabled):
            self.current_index = 0
        
        return enabled[self.current_index]

    def should_switch(self) -> bool:
        """Check if it's time to switch to the next frame."""
        current_frame = self.get_current_frame()
        if not current_frame:
            return False
        
        current_time = time.time()
        elapsed = current_time - self.last_switch_time
        return elapsed >= current_frame.duration

    def next_frame(self):
        """Switch to the next frame."""
        enabled = self.get_enabled_frames()
        if enabled:
            self.current_index = (self.current_index + 1) % len(enabled)
            self.last_switch_time = time.time()
            logger.debug(f"Switched to frame: {enabled[self.current_index].name}")

    def jump_to_frame(self, frame_id: str) -> bool:
        """Jump to a specific frame by ID.
        
        Args:
            frame_id: ID of the frame to jump to
            
        Returns:
            True if frame found and jumped to, False otherwise
        """
        enabled = self.get_enabled_frames()
        for i, frame in enumerate(enabled):
            if frame.id == frame_id:
                self.current_index = i
                self.last_switch_time = time.time()
                logger.info(f"Jumped to frame: {frame.name}")
                return True
        return False

    def update(self):
        """Update the display with the current frame."""
        if self.should_switch():
            self.next_frame()
        
        current_frame = self.get_current_frame()
        if not current_frame:
            # No enabled frames - show default
            self._render_default()
            return
        
        image = self._render_frame(current_frame)
        if image and self.display:
            self.display.show_image(image)

    def _render_frame(self, frame: Frame) -> Optional[Image.Image]:
        """Render a frame to an image."""
        # Create base image with background
        image = Image.new('RGB', (self.width, self.height), frame.bg_color)
        draw = ImageDraw.Draw(image)
        
        # Load font
        try:
            font = ImageFont.truetype(frame.font_path, frame.font_size)
        except Exception as e:
            logger.warning(f"Error loading font {frame.font_path}: {e}")
            font = ImageFont.load_default()
        
        # Get content based on frame type
        content = self._get_frame_content(frame)
        
        # Calculate text position
        bbox = draw.textbbox((0, 0), content, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        
        x = get_position(frame.position.get('x', 'center'), self.width, text_width)
        y = get_position(frame.position.get('y', 'center'), self.height, text_height)
        
        # Draw text
        draw.text((x, y), content, font=font, fill=frame.text_color)
        
        return image

    def _get_frame_content(self, frame: Frame) -> str:
        """Get the content string for a frame based on its type."""
        now = datetime.now(self.timezone)
        
        if frame.type == 'time':
            fmt = frame.format or '%H:%M:%S'
            return now.strftime(fmt)
        
        elif frame.type == 'date':
            fmt = frame.format or '%Y-%m-%d'
            return now.strftime(fmt)
        
        elif frame.type == 'datetime':
            fmt = frame.format or '%Y-%m-%d %H:%M'
            return now.strftime(fmt)
        
        elif frame.type == 'temperature':
            if self.temp_sensor:
                try:
                    temp = self.temp_sensor.get_temperature()
                    if temp is not None:
                        if frame.unit == 'fahrenheit':
                            temp = (temp * 9/5) + 32
                            return f"{temp:.1f}°F"
                        return f"{temp:.1f}°C"
                except Exception as e:
                    logger.error(f"Error reading temperature: {e}")
            return "N/A"
        
        elif frame.type == 'humidity':
            if self.humidity_getter:
                try:
                    humidity = self.humidity_getter()
                    if humidity is not None:
                        return f"{humidity:.0f}%"
                except Exception as e:
                    logger.error(f"Error reading humidity: {e}")
            return "N/A"
        
        elif frame.type == 'text':
            return frame.text or frame.name
        
        elif frame.type == 'message':
            return frame.text or ''
        
        else:
            return frame.name

    def _render_default(self):
        """Render a default display when no frames are available."""
        if not self.display:
            return
        
        image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
            draw.text((5, self.height // 2 - 4), "No frames", font=font, fill=(255, 255, 255))
        except Exception:
            pass
        
        self.display.show_image(image)


class FrameMode:
    """Display mode that uses the frame system."""

    def __init__(self, display_manager, config: Dict[str, Any], 
                 temp_sensor=None, humidity_getter: Optional[Callable] = None):
        """Initialize frame mode.

        Args:
            display_manager: DisplayManager instance
            config: Full configuration dictionary
            temp_sensor: Temperature sensor instance
            humidity_getter: Callable that returns humidity
        """
        self.display = display_manager
        self.config = config
        self.frame_manager = FrameManager(
            display_manager, config, temp_sensor, humidity_getter
        )

    def update(self):
        """Update the frame display."""
        self.frame_manager.update()

    def reload_config(self, config: Dict[str, Any]):
        """Reload configuration."""
        self.config = config
        self.frame_manager.reload_frames(config)

    def reset(self):
        """Reset to first frame."""
        self.frame_manager.current_index = 0
        self.frame_manager.last_switch_time = time.time()
