"""Display modes for the LED matrix."""

import logging
from datetime import datetime
from typing import Optional
import pytz

logger = logging.getLogger(__name__)


class DisplayMode:
    """Base class for display modes."""

    def __init__(self, display_manager, config: dict):
        """Initialize display mode.

        Args:
            display_manager: DisplayManager instance
            config: Configuration dictionary
        """
        self.display = display_manager
        self.config = config

    def update(self):
        """Update the display. Override in subclasses."""
        raise NotImplementedError


class ClockMode(DisplayMode):
    """Display current time."""

    def __init__(self, display_manager, config: dict):
        """Initialize clock mode."""
        super().__init__(display_manager, config)
        self.timezone = pytz.timezone(config.get('time', {}).get('timezone', 'UTC'))
        self.format_24h = config.get('time', {}).get('format_24h', True)
        self.show_seconds = config.get('time', {}).get('show_seconds', True)
        self.font_path = config.get('display', {}).get('font_path')

    def update(self):
        """Update clock display."""
        now = datetime.now(self.timezone)
        
        if self.format_24h:
            if self.show_seconds:
                time_str = now.strftime("%H:%M:%S")
            else:
                time_str = now.strftime("%H:%M")
        else:
            if self.show_seconds:
                time_str = now.strftime("%I:%M:%S %p")
            else:
                time_str = now.strftime("%I:%M %p")

        self.display.draw_text(time_str, x=2, y=12, 
                              color=(0, 255, 255),
                              font_path=self.font_path, 
                              font_size=12)


class TemperatureMode(DisplayMode):
    """Display temperature reading."""

    def __init__(self, display_manager, config: dict, temp_sensor=None):
        """Initialize temperature mode."""
        super().__init__(display_manager, config)
        self.temp_sensor = temp_sensor
        self.unit = config.get('temperature', {}).get('unit', 'celsius')
        self.font_path = config.get('display', {}).get('font_path')

    def update(self):
        """Update temperature display."""
        if not self.temp_sensor:
            temp_str = "No Sensor"
        else:
            try:
                temp = self.temp_sensor.get_temperature()
                if temp is not None:
                    if self.unit == 'fahrenheit':
                        temp = (temp * 9/5) + 32
                        temp_str = f"{temp:.1f}°F"
                    else:
                        temp_str = f"{temp:.1f}°C"
                else:
                    temp_str = "N/A"
            except Exception as e:
                logger.error(f"Error reading temperature: {e}")
                temp_str = "Error"

        self.display.draw_text(temp_str, x=5, y=12,
                              color=(255, 128, 0),
                              font_path=self.font_path,
                              font_size=12)


class MessageMode(DisplayMode):
    """Display custom message."""

    def __init__(self, display_manager, config: dict, message: str = ""):
        """Initialize message mode."""
        super().__init__(display_manager, config)
        self.message = message
        self.font_path = config.get('display', {}).get('font_path')

    def set_message(self, message: str):
        """Set the message to display."""
        self.message = message

    def update(self):
        """Update message display."""
        if len(self.message) > 10:
            # Scroll long messages
            scroll_speed = self.config.get('display', {}).get('scroll_speed', 0.05)
            self.display.scroll_text(self.message, 
                                    color=(255, 255, 0),
                                    font_path=self.font_path,
                                    font_size=10,
                                    speed=scroll_speed,
                                    y_pos=12)
        else:
            # Display short messages statically
            self.display.draw_text(self.message, x=5, y=12,
                                  color=(255, 255, 0),
                                  font_path=self.font_path,
                                  font_size=10)


class SlideshowMode(DisplayMode):
    """Cycle through multiple display modes."""

    def __init__(self, display_manager, config: dict, modes: list):
        """Initialize slideshow mode.

        Args:
            display_manager: DisplayManager instance
            config: Configuration dictionary
            modes: List of DisplayMode instances to cycle through
        """
        super().__init__(display_manager, config)
        self.modes = modes
        self.current_index = 0
        self.interval = config.get('display', {}).get('slideshow_interval', 10)
        self.last_update = 0

    def update(self):
        """Update slideshow display."""
        import time
        current_time = time.time()
        
        if current_time - self.last_update >= self.interval:
            self.current_index = (self.current_index + 1) % len(self.modes)
            self.last_update = current_time

        if self.modes:
            self.modes[self.current_index].update()
