"""New display modes for Lumigrid display system.

This module provides various display modes including:
- Entertainment modes (video player, slideshow, clock with world timezones, calendar, weather)
- Information modes (stock ticker, social media, news ticker, RSS feed, system monitor)
"""

import logging
import math
import random
import time
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple

from PIL import Image, ImageDraw, ImageFont

# Try to use standard library zoneinfo (Python 3.9+), fall back to pytz
try:
    from zoneinfo import ZoneInfo
    _USE_ZONEINFO = True
except ImportError:
    try:
        import pytz
        _USE_ZONEINFO = False
    except ImportError:
        _USE_ZONEINFO = None

logger = logging.getLogger(__name__)


def get_timezone(tz_name: str):
    """Get timezone object.
    
    Args:
        tz_name: Timezone name
        
    Returns:
        Timezone object or None
    """
    if _USE_ZONEINFO is True:
        try:
            return ZoneInfo(tz_name)
        except Exception:
            return None
    elif _USE_ZONEINFO is False:
        try:
            return pytz.timezone(tz_name)
        except Exception:
            return None
    return None


class DisplayModeType(Enum):
    """Types of display modes."""
    # Entertainment
    VIDEO_PLAYER = "video_player"
    SLIDESHOW = "slideshow"
    WORLD_CLOCK = "world_clock"
    CALENDAR = "calendar"
    WEATHER = "weather"
    # Information
    STOCK_TICKER = "stock_ticker"
    SOCIAL_MEDIA = "social_media"
    NEWS_TICKER = "news_ticker"
    RSS_FEED = "rss_feed"
    SYSTEM_MONITOR = "system_monitor"


@dataclass
class AnimationFrame:
    """A single frame in an animation or video.
    
    Attributes:
        image: PIL Image for this frame
        duration: How long to display this frame in seconds
    """
    image: Image.Image
    duration: float = 0.1


class VideoPlayerMode:
    """Video player mode for simple animations and GIFs."""
    
    def __init__(
        self,
        width: int,
        height: int,
        frames: Optional[List[AnimationFrame]] = None,
        loop: bool = True,
        auto_play: bool = True
    ):
        """Initialize video player.
        
        Args:
            width: Display width
            height: Display height
            frames: List of animation frames
            loop: Whether to loop playback
            auto_play: Start playing automatically
        """
        self.width = width
        self.height = height
        self.frames = frames or []
        self.loop = loop
        self.playing = auto_play
        
        self.current_frame_index = 0
        self._frame_start_time = time.perf_counter()
    
    def load_gif(self, image: Image.Image) -> None:
        """Load frames from a GIF image.
        
        Args:
            image: PIL Image (GIF format)
        """
        self.frames = []
        try:
            for frame_num in range(image.n_frames):
                image.seek(frame_num)
                frame = image.copy().convert('RGB')
                frame = frame.resize((self.width, self.height), Image.Resampling.NEAREST)
                
                duration = image.info.get('duration', 100) / 1000.0
                self.frames.append(AnimationFrame(frame, duration))
        except Exception as e:
            logger.error(f"Error loading GIF: {e}")
    
    def add_frame(self, image: Image.Image, duration: float = 0.1) -> None:
        """Add a frame to the video.
        
        Args:
            image: Frame image
            duration: Frame duration
        """
        resized = image.resize((self.width, self.height), Image.Resampling.NEAREST)
        self.frames.append(AnimationFrame(resized.convert('RGB'), duration))
    
    def play(self) -> None:
        """Start playback."""
        self.playing = True
        self._frame_start_time = time.perf_counter()
    
    def pause(self) -> None:
        """Pause playback."""
        self.playing = False
    
    def stop(self) -> None:
        """Stop playback and reset to beginning."""
        self.playing = False
        self.current_frame_index = 0
        self._frame_start_time = time.perf_counter()
    
    def seek(self, frame_index: int) -> None:
        """Seek to a specific frame.
        
        Args:
            frame_index: Target frame index
        """
        if self.frames:
            self.current_frame_index = frame_index % len(self.frames)
            self._frame_start_time = time.perf_counter()
    
    def update(self) -> None:
        """Update playback state."""
        if not self.playing or not self.frames:
            return
        
        current_time = time.perf_counter()
        current_frame = self.frames[self.current_frame_index]
        
        if current_time - self._frame_start_time >= current_frame.duration:
            self.current_frame_index += 1
            self._frame_start_time = current_time
            
            if self.current_frame_index >= len(self.frames):
                if self.loop:
                    self.current_frame_index = 0
                else:
                    self.current_frame_index = len(self.frames) - 1
                    self.playing = False
    
    def render(self) -> Image.Image:
        """Render current frame.
        
        Returns:
            Current frame image
        """
        self.update()
        
        if not self.frames:
            return Image.new('RGB', (self.width, self.height), (0, 0, 0))
        
        return self.frames[self.current_frame_index].image.copy()


class AdvancedSlideshowMode:
    """Advanced slideshow with transitions."""
    
    def __init__(
        self,
        width: int,
        height: int,
        images: Optional[List[Image.Image]] = None,
        duration: float = 5.0,
        transition_duration: float = 0.5,
        transition_type: str = "fade"  # fade, slide_left, slide_right, wipe, zoom
    ):
        """Initialize slideshow.
        
        Args:
            width: Display width
            height: Display height
            images: List of images
            duration: Time to display each image
            transition_duration: Transition animation duration
            transition_type: Type of transition effect
        """
        self.width = width
        self.height = height
        self.images = []
        self.duration = duration
        self.transition_duration = transition_duration
        self.transition_type = transition_type
        
        self.current_index = 0
        self._last_switch = time.perf_counter()
        self._transitioning = False
        self._transition_progress = 0
        
        if images:
            for img in images:
                self.add_image(img)
    
    def add_image(self, image: Image.Image) -> None:
        """Add an image to the slideshow.
        
        Args:
            image: Image to add
        """
        resized = image.resize((self.width, self.height), Image.Resampling.NEAREST)
        self.images.append(resized.convert('RGB'))
    
    def clear_images(self) -> None:
        """Remove all images."""
        self.images.clear()
        self.current_index = 0
    
    def next_image(self) -> None:
        """Go to next image."""
        if self.images:
            self._transitioning = True
            self._transition_progress = 0
    
    def previous_image(self) -> None:
        """Go to previous image."""
        if self.images:
            self.current_index = (self.current_index - 1) % len(self.images)
            self._last_switch = time.perf_counter()
    
    def update(self, dt: float) -> None:
        """Update slideshow state.
        
        Args:
            dt: Delta time
        """
        if not self.images:
            return
        
        current_time = time.perf_counter()
        
        if self._transitioning:
            self._transition_progress += dt / self.transition_duration
            if self._transition_progress >= 1:
                self._transitioning = False
                self._transition_progress = 0
                self.current_index = (self.current_index + 1) % len(self.images)
                self._last_switch = current_time
        elif current_time - self._last_switch >= self.duration:
            self.next_image()
    
    def _apply_transition(
        self,
        from_image: Image.Image,
        to_image: Image.Image,
        progress: float
    ) -> Image.Image:
        """Apply transition effect between images.
        
        Args:
            from_image: Source image
            to_image: Destination image
            progress: Transition progress (0-1)
            
        Returns:
            Blended image
        """
        result = Image.new('RGB', (self.width, self.height))
        
        if self.transition_type == "fade":
            # Cross-fade
            result = Image.blend(from_image, to_image, progress)
        
        elif self.transition_type == "slide_left":
            offset = int(self.width * progress)
            result.paste(from_image.crop((offset, 0, self.width, self.height)), (0, 0))
            result.paste(to_image.crop((0, 0, offset, self.height)), (self.width - offset, 0))
        
        elif self.transition_type == "slide_right":
            offset = int(self.width * progress)
            result.paste(from_image.crop((0, 0, self.width - offset, self.height)), (offset, 0))
            result.paste(to_image.crop((self.width - offset, 0, self.width, self.height)), (0, 0))
        
        elif self.transition_type == "wipe":
            wipe_x = int(self.width * progress)
            result.paste(from_image.crop((wipe_x, 0, self.width, self.height)), (wipe_x, 0))
            result.paste(to_image.crop((0, 0, wipe_x, self.height)), (0, 0))
        
        elif self.transition_type == "zoom":
            # Zoom out from center
            scale = 1 + progress * 0.3
            zoomed = from_image.resize(
                (int(self.width * scale), int(self.height * scale)),
                Image.Resampling.NEAREST
            )
            offset_x = (zoomed.width - self.width) // 2
            offset_y = (zoomed.height - self.height) // 2
            cropped = zoomed.crop((offset_x, offset_y, offset_x + self.width, offset_y + self.height))
            result = Image.blend(cropped, to_image, progress)
        
        else:
            result = to_image if progress > 0.5 else from_image
        
        return result
    
    def render(self) -> Image.Image:
        """Render current slideshow state.
        
        Returns:
            Current display image
        """
        if not self.images:
            return Image.new('RGB', (self.width, self.height), (0, 0, 0))
        
        current_image = self.images[self.current_index]
        
        if self._transitioning:
            next_index = (self.current_index + 1) % len(self.images)
            next_image = self.images[next_index]
            return self._apply_transition(current_image, next_image, self._transition_progress)
        
        return current_image.copy()


@dataclass
class WorldClock:
    """A single clock in the world clock display.
    
    Attributes:
        timezone: Timezone name
        label: Display label
        color: Text color
    """
    timezone: str
    label: str
    color: Tuple[int, int, int] = (255, 255, 255)


class WorldClockMode:
    """World clock with multiple time zones."""
    
    def __init__(
        self,
        width: int,
        height: int,
        clocks: Optional[List[WorldClock]] = None,
        format_24h: bool = True,
        show_date: bool = False,
        show_seconds: bool = True
    ):
        """Initialize world clock.
        
        Args:
            width: Display width
            height: Display height
            clocks: List of world clocks
            format_24h: Use 24-hour format
            show_date: Show date alongside time
            show_seconds: Show seconds
        """
        self.width = width
        self.height = height
        self.clocks = clocks or [WorldClock("UTC", "UTC")]
        self.format_24h = format_24h
        self.show_date = show_date
        self.show_seconds = show_seconds
        
        self.current_clock_index = 0
        self.display_mode = "single"  # single, split, rotate
        self._last_rotate = time.perf_counter()
        self.rotate_interval = 5.0
    
    def add_clock(self, timezone: str, label: str, color: Tuple[int, int, int] = (255, 255, 255)) -> None:
        """Add a world clock.
        
        Args:
            timezone: Timezone name
            label: Display label
            color: Text color
        """
        self.clocks.append(WorldClock(timezone, label, color))
    
    def remove_clock(self, index: int) -> bool:
        """Remove a clock by index.
        
        Args:
            index: Clock index
            
        Returns:
            True if removed
        """
        if 0 <= index < len(self.clocks):
            self.clocks.pop(index)
            return True
        return False
    
    def switch_clock(self, direction: int = 1) -> None:
        """Switch to next/previous clock.
        
        Args:
            direction: 1 for next, -1 for previous
        """
        if self.clocks:
            self.current_clock_index = (self.current_clock_index + direction) % len(self.clocks)
    
    def _get_time_for_clock(self, clock: WorldClock) -> datetime:
        """Get current time for a clock.
        
        Args:
            clock: World clock
            
        Returns:
            Current datetime in that timezone
        """
        tz = get_timezone(clock.timezone)
        if tz:
            return datetime.now(tz)
        return datetime.utcnow()
    
    def _format_time(self, dt: datetime) -> str:
        """Format datetime for display.
        
        Args:
            dt: Datetime to format
            
        Returns:
            Formatted time string
        """
        if self.format_24h:
            fmt = "%H:%M:%S" if self.show_seconds else "%H:%M"
        else:
            fmt = "%I:%M:%S %p" if self.show_seconds else "%I:%M %p"
        
        if self.show_date:
            fmt = "%m/%d " + fmt
        
        return dt.strftime(fmt)
    
    def update(self, dt: float) -> None:
        """Update clock state.
        
        Args:
            dt: Delta time
        """
        if self.display_mode == "rotate" and len(self.clocks) > 1:
            current_time = time.perf_counter()
            if current_time - self._last_rotate >= self.rotate_interval:
                self.switch_clock(1)
                self._last_rotate = current_time
    
    def render(self) -> Image.Image:
        """Render world clock display.
        
        Returns:
            Clock display image
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        if not self.clocks:
            return image
        
        if self.display_mode == "split" and len(self.clocks) >= 2:
            # Show two clocks split vertically
            half_height = self.height // 2
            
            for i, idx in enumerate([0, 1]):
                if idx < len(self.clocks):
                    clock = self.clocks[idx]
                    dt = self._get_time_for_clock(clock)
                    time_str = self._format_time(dt)
                    
                    y_offset = i * half_height
                    
                    # Label
                    if font:
                        draw.text((2, y_offset + 1), clock.label, fill=clock.color, font=font)
                    
                    # Time
                    if font:
                        draw.text((2, y_offset + 10), time_str, fill=clock.color, font=font)
        else:
            # Single clock display
            clock = self.clocks[self.current_clock_index]
            dt = self._get_time_for_clock(clock)
            time_str = self._format_time(dt)
            
            # Center the display
            if font:
                # Label
                bbox = draw.textbbox((0, 0), clock.label, font=font)
                label_width = bbox[2] - bbox[0]
                label_x = (self.width - label_width) // 2
                draw.text((label_x, 4), clock.label, fill=clock.color, font=font)
                
                # Time
                bbox = draw.textbbox((0, 0), time_str, font=font)
                time_width = bbox[2] - bbox[0]
                time_x = (self.width - time_width) // 2
                draw.text((time_x, self.height // 2), time_str, fill=clock.color, font=font)
        
        return image


@dataclass
class CalendarEvent:
    """A calendar event.
    
    Attributes:
        title: Event title
        start: Event start datetime
        end: Event end datetime
        color: Event color
        all_day: Whether event is all-day
    """
    title: str
    start: datetime
    end: datetime
    color: Tuple[int, int, int] = (100, 150, 255)
    all_day: bool = False


class CalendarMode:
    """Calendar display with event visualization."""
    
    def __init__(
        self,
        width: int,
        height: int,
        events: Optional[List[CalendarEvent]] = None,
        view_mode: str = "today"  # today, week, month
    ):
        """Initialize calendar.
        
        Args:
            width: Display width
            height: Display height
            events: List of calendar events
            view_mode: Display view mode
        """
        self.width = width
        self.height = height
        self.events = events or []
        self.view_mode = view_mode
        self.selected_date = datetime.now().date()
    
    def add_event(self, event: CalendarEvent) -> None:
        """Add an event.
        
        Args:
            event: Calendar event
        """
        self.events.append(event)
        self.events.sort(key=lambda e: e.start)
    
    def get_events_for_date(self, date) -> List[CalendarEvent]:
        """Get events for a specific date.
        
        Args:
            date: Date to check
            
        Returns:
            List of events on that date
        """
        return [e for e in self.events 
                if e.start.date() <= date <= e.end.date()]
    
    def get_upcoming_events(self, count: int = 5) -> List[CalendarEvent]:
        """Get upcoming events.
        
        Args:
            count: Maximum events to return
            
        Returns:
            List of upcoming events
        """
        now = datetime.now()
        upcoming = [e for e in self.events if e.end >= now]
        return upcoming[:count]
    
    def render(self) -> Image.Image:
        """Render calendar display.
        
        Returns:
            Calendar image
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        now = datetime.now()
        
        if self.view_mode == "today":
            # Show today's date and events
            date_str = now.strftime("%a, %b %d")
            if font:
                draw.text((2, 2), date_str, fill=(255, 255, 255), font=font)
            
            # List today's events
            today_events = self.get_events_for_date(now.date())
            y_offset = 14
            for event in today_events[:3]:  # Max 3 events
                if font:
                    time_str = event.start.strftime("%H:%M")
                    text = f"{time_str} {event.title[:10]}"
                    draw.text((2, y_offset), text, fill=event.color, font=font)
                    y_offset += 8
            
            if not today_events and font:
                draw.text((2, 14), "No events", fill=(128, 128, 128), font=font)
        
        elif self.view_mode == "week":
            # Show week overview
            cell_width = self.width // 7
            
            # Draw day headers
            days = ['M', 'T', 'W', 'T', 'F', 'S', 'S']
            for i, day in enumerate(days):
                x = i * cell_width + cell_width // 2 - 2
                if font:
                    draw.text((x, 1), day, fill=(200, 200, 200), font=font)
            
            # Draw events as dots
            week_start = now - timedelta(days=now.weekday())
            for i in range(7):
                day = week_start + timedelta(days=i)
                day_events = self.get_events_for_date(day.date())
                if day_events:
                    x = i * cell_width + cell_width // 2
                    y = 12
                    for j, event in enumerate(day_events[:3]):
                        draw.ellipse(
                            [x - 2, y + j * 4, x + 2, y + j * 4 + 3],
                            fill=event.color
                        )
        
        return image


@dataclass
class WeatherData:
    """Weather data for display.
    
    Attributes:
        temperature: Current temperature
        condition: Weather condition (sunny, cloudy, rain, etc.)
        humidity: Humidity percentage
        wind_speed: Wind speed
        icon: Weather icon identifier
    """
    temperature: float
    condition: str
    humidity: float = 0
    wind_speed: float = 0
    icon: str = "unknown"


class WeatherDashboardMode:
    """Weather dashboard with animated icons."""
    
    def __init__(
        self,
        width: int,
        height: int,
        weather_data: Optional[WeatherData] = None,
        unit: str = "celsius",  # celsius or fahrenheit
        animated_icons: bool = True
    ):
        """Initialize weather dashboard.
        
        Args:
            width: Display width
            height: Display height
            weather_data: Current weather data
            unit: Temperature unit
            animated_icons: Enable animated weather icons
        """
        self.width = width
        self.height = height
        self.weather = weather_data or WeatherData(20, "unknown")
        self.unit = unit
        self.animated_icons = animated_icons
        
        self._animation_time = 0
        self._data_source: Optional[Callable[[], WeatherData]] = None
    
    def set_data_source(self, source: Callable[[], WeatherData]) -> None:
        """Set weather data source.
        
        Args:
            source: Function that returns weather data
        """
        self._data_source = source
    
    def update_weather(self) -> None:
        """Update weather from data source."""
        if self._data_source:
            try:
                self.weather = self._data_source()
            except Exception as e:
                logger.error(f"Error fetching weather: {e}")
    
    def update(self, dt: float) -> None:
        """Update animation state.
        
        Args:
            dt: Delta time
        """
        self._animation_time += dt
    
    def _draw_weather_icon(self, draw: ImageDraw.ImageDraw, x: int, y: int, size: int) -> None:
        """Draw animated weather icon.
        
        Args:
            draw: ImageDraw object
            x: Center X position
            y: Center Y position
            size: Icon size
        """
        condition = self.weather.condition.lower()
        t = self._animation_time
        
        if "sun" in condition or "clear" in condition:
            # Animated sun
            radius = size // 2 - 2
            draw.ellipse(
                [x - radius, y - radius, x + radius, y + radius],
                fill=(255, 200, 0)
            )
            # Animated rays
            num_rays = 8
            for i in range(num_rays):
                angle = (i * 360 / num_rays) + (t * 30) if self.animated_icons else (i * 360 / num_rays)
                rad = math.radians(angle)
                inner = radius + 2
                outer = radius + 5
                x1 = x + int(math.cos(rad) * inner)
                y1 = y + int(math.sin(rad) * inner)
                x2 = x + int(math.cos(rad) * outer)
                y2 = y + int(math.sin(rad) * outer)
                draw.line([(x1, y1), (x2, y2)], fill=(255, 200, 0), width=1)
        
        elif "cloud" in condition:
            # Cloud
            offset_y = int(math.sin(t * 2) * 1) if self.animated_icons else 0
            draw.ellipse([x - 8, y - 4 + offset_y, x - 2, y + 2 + offset_y], fill=(200, 200, 200))
            draw.ellipse([x - 4, y - 6 + offset_y, x + 4, y + 2 + offset_y], fill=(220, 220, 220))
            draw.ellipse([x, y - 4 + offset_y, x + 6, y + 2 + offset_y], fill=(200, 200, 200))
        
        elif "rain" in condition:
            # Cloud with rain drops
            draw.ellipse([x - 6, y - 6, x + 6, y], fill=(150, 150, 150))
            # Animated rain drops
            for i in range(3):
                drop_x = x - 4 + i * 4
                drop_y = y + 4 + int((t * 20 + i * 10) % 10) if self.animated_icons else y + 6
                draw.line([(drop_x, drop_y), (drop_x, drop_y + 3)], fill=(100, 150, 255), width=1)
        
        elif "snow" in condition:
            # Snowflakes
            for i in range(5):
                flake_x = x - 6 + i * 3
                flake_y = y + int((t * 10 + i * 5) % 12 - 6) if self.animated_icons else y
                draw.point((flake_x, flake_y), fill=(255, 255, 255))
        
        else:
            # Unknown - question mark
            draw.text((x - 3, y - 4), "?", fill=(128, 128, 128))
    
    def render(self) -> Image.Image:
        """Render weather dashboard.
        
        Returns:
            Weather display image
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 20))
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        # Draw weather icon
        icon_x = self.width // 4
        icon_y = self.height // 3
        self._draw_weather_icon(draw, icon_x, icon_y, 20)
        
        # Temperature
        temp = self.weather.temperature
        if self.unit == "fahrenheit":
            temp = temp * 9 / 5 + 32
        temp_str = f"{temp:.0f}°{'F' if self.unit == 'fahrenheit' else 'C'}"
        
        if font:
            draw.text((self.width // 2 + 2, 4), temp_str, fill=(255, 255, 255), font=font)
        
        # Condition
        if font:
            condition_text = self.weather.condition[:10]
            draw.text((self.width // 2 + 2, 14), condition_text, fill=(180, 180, 180), font=font)
        
        # Humidity
        if self.weather.humidity > 0 and font:
            humidity_str = f"{self.weather.humidity:.0f}%"
            draw.text((2, self.height - 10), humidity_str, fill=(100, 200, 255), font=font)
        
        return image


@dataclass
class TickerItem:
    """Item in a ticker display.
    
    Attributes:
        symbol: Stock/crypto symbol
        value: Current value
        change: Change amount
        change_percent: Change percentage
    """
    symbol: str
    value: float
    change: float = 0
    change_percent: float = 0


class StockTickerMode:
    """Stock/cryptocurrency ticker with animated price movements."""
    
    def __init__(
        self,
        width: int,
        height: int,
        items: Optional[List[TickerItem]] = None,
        scroll_speed: float = 30.0,
        show_change: bool = True
    ):
        """Initialize stock ticker.
        
        Args:
            width: Display width
            height: Display height
            items: Ticker items to display
            scroll_speed: Scroll speed in pixels per second
            show_change: Show price change
        """
        self.width = width
        self.height = height
        self.items = items or []
        self.scroll_speed = scroll_speed
        self.show_change = show_change
        
        self._scroll_position = 0
        self._data_source: Optional[Callable[[], List[TickerItem]]] = None
    
    def set_data_source(self, source: Callable[[], List[TickerItem]]) -> None:
        """Set ticker data source.
        
        Args:
            source: Function that returns ticker items
        """
        self._data_source = source
    
    def update_data(self) -> None:
        """Update ticker data from source."""
        if self._data_source:
            try:
                self.items = self._data_source()
            except Exception as e:
                logger.error(f"Error fetching ticker data: {e}")
    
    def add_item(self, item: TickerItem) -> None:
        """Add a ticker item.
        
        Args:
            item: Ticker item to add
        """
        self.items.append(item)
    
    def update(self, dt: float) -> None:
        """Update ticker scroll.
        
        Args:
            dt: Delta time
        """
        self._scroll_position += self.scroll_speed * dt
    
    def _get_total_content_width(self, draw: ImageDraw.ImageDraw, font) -> int:
        """Calculate total content width.
        
        Args:
            draw: ImageDraw object
            font: Font to use
            
        Returns:
            Total width in pixels
        """
        total = 0
        spacing = 20
        
        for item in self.items:
            text = f"{item.symbol}: ${item.value:.2f}"
            if self.show_change:
                sign = '+' if item.change >= 0 else ''
                text += f" ({sign}{item.change_percent:.1f}%)"
            
            bbox = draw.textbbox((0, 0), text, font=font)
            total += (bbox[2] - bbox[0]) + spacing
        
        return total
    
    def render(self) -> Image.Image:
        """Render ticker display.
        
        Returns:
            Ticker image
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        if not self.items or not font:
            return image
        
        # Calculate content width
        total_width = self._get_total_content_width(draw, font)
        
        # Wrap scroll position
        if total_width > 0:
            self._scroll_position = self._scroll_position % total_width
        
        # Draw items
        x = -self._scroll_position
        spacing = 20
        text_y = (self.height - 8) // 2
        
        # Draw twice for seamless scrolling
        for _ in range(2):
            for item in self.items:
                # Symbol and value
                text = f"{item.symbol}: ${item.value:.2f}"
                color = (255, 255, 255)
                
                if x + 100 > 0 and x < self.width:
                    draw.text((x, text_y), text, fill=color, font=font)
                
                bbox = draw.textbbox((0, 0), text, font=font)
                x += (bbox[2] - bbox[0]) + 4
                
                # Change percentage
                if self.show_change:
                    sign = '+' if item.change >= 0 else ''
                    change_text = f"({sign}{item.change_percent:.1f}%)"
                    change_color = (0, 255, 0) if item.change >= 0 else (255, 0, 0)
                    
                    if x + 50 > 0 and x < self.width:
                        draw.text((x, text_y), change_text, fill=change_color, font=font)
                    
                    bbox = draw.textbbox((0, 0), change_text, font=font)
                    x += (bbox[2] - bbox[0])
                
                x += spacing
        
        return image


@dataclass
class NewsItem:
    """News item for ticker display.
    
    Attributes:
        title: News headline
        source: News source
        timestamp: Publication time
        category: News category
    """
    title: str
    source: str = ""
    timestamp: Optional[datetime] = None
    category: str = ""


class NewsTickerMode:
    """News ticker with scrolling headlines."""
    
    def __init__(
        self,
        width: int,
        height: int,
        items: Optional[List[NewsItem]] = None,
        scroll_speed: float = 25.0
    ):
        """Initialize news ticker.
        
        Args:
            width: Display width
            height: Display height
            items: News items
            scroll_speed: Scroll speed
        """
        self.width = width
        self.height = height
        self.items = items or []
        self.scroll_speed = scroll_speed
        
        self._scroll_position = 0
        self._data_source: Optional[Callable[[], List[NewsItem]]] = None
    
    def set_data_source(self, source: Callable[[], List[NewsItem]]) -> None:
        """Set news data source.
        
        Args:
            source: Function that returns news items
        """
        self._data_source = source
    
    def add_item(self, item: NewsItem) -> None:
        """Add a news item.
        
        Args:
            item: News item
        """
        self.items.append(item)
    
    def update(self, dt: float) -> None:
        """Update scroll position.
        
        Args:
            dt: Delta time
        """
        self._scroll_position += self.scroll_speed * dt
    
    def render(self) -> Image.Image:
        """Render news ticker.
        
        Returns:
            Ticker image
        """
        image = Image.new('RGB', (self.width, self.height), (20, 20, 40))
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            return image
        
        if not self.items:
            draw.text((2, self.height // 2 - 4), "No news", fill=(128, 128, 128), font=font)
            return image
        
        # Build combined text
        separator = " • "
        combined_text = separator.join([item.title for item in self.items])
        combined_text += separator  # For seamless loop
        
        # Calculate total width
        bbox = draw.textbbox((0, 0), combined_text, font=font)
        total_width = bbox[2] - bbox[0]
        
        # Wrap scroll position
        if total_width > 0:
            self._scroll_position = self._scroll_position % total_width
        
        # Draw scrolling text
        x = -self._scroll_position
        text_y = (self.height - 8) // 2
        
        # Draw twice for seamless scrolling
        for _ in range(2):
            if x < self.width:
                draw.text((x, text_y), combined_text, fill=(255, 255, 255), font=font)
            x += total_width
        
        return image


@dataclass
class SystemMetric:
    """System monitoring metric.
    
    Attributes:
        name: Metric name
        value: Current value
        max_value: Maximum value (for percentage calculation)
        unit: Display unit
        color: Display color
    """
    name: str
    value: float
    max_value: float = 100
    unit: str = "%"
    color: Tuple[int, int, int] = (0, 255, 0)


class SystemMonitorMode:
    """System monitor with animated resource usage."""
    
    def __init__(
        self,
        width: int,
        height: int,
        metrics: Optional[List[SystemMetric]] = None,
        history_length: int = 20,
        show_graph: bool = True
    ):
        """Initialize system monitor.
        
        Args:
            width: Display width
            height: Display height
            metrics: System metrics to display
            history_length: Number of history points to keep
            show_graph: Show mini graph for metrics
        """
        self.width = width
        self.height = height
        self.metrics = metrics or []
        self.history_length = history_length
        self.show_graph = show_graph
        
        self._history: Dict[str, List[float]] = {}
        self._data_source: Optional[Callable[[], List[SystemMetric]]] = None
    
    def set_data_source(self, source: Callable[[], List[SystemMetric]]) -> None:
        """Set data source for metrics.
        
        Args:
            source: Function that returns system metrics
        """
        self._data_source = source
    
    def update_metrics(self) -> None:
        """Update metrics from data source."""
        if self._data_source:
            try:
                self.metrics = self._data_source()
                
                # Update history
                for metric in self.metrics:
                    if metric.name not in self._history:
                        self._history[metric.name] = []
                    
                    self._history[metric.name].append(metric.value)
                    
                    # Trim history
                    if len(self._history[metric.name]) > self.history_length:
                        self._history[metric.name] = self._history[metric.name][-self.history_length:]
            except Exception as e:
                logger.error(f"Error fetching system metrics: {e}")
    
    def add_metric(self, metric: SystemMetric) -> None:
        """Add a metric to monitor.
        
        Args:
            metric: System metric
        """
        self.metrics.append(metric)
        self._history[metric.name] = []
    
    def render(self) -> Image.Image:
        """Render system monitor display.
        
        Returns:
            Monitor display image
        """
        image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        if not self.metrics:
            return image
        
        # Calculate layout
        num_metrics = min(len(self.metrics), 4)  # Max 4 metrics
        metric_height = self.height // num_metrics
        
        for i, metric in enumerate(self.metrics[:4]):
            y_offset = i * metric_height
            
            # Calculate percentage
            percentage = (metric.value / metric.max_value * 100) if metric.max_value > 0 else 0
            percentage = min(100, max(0, percentage))
            
            # Draw metric name
            if font:
                draw.text((2, y_offset + 1), metric.name[:6], fill=(200, 200, 200), font=font)
            
            # Draw bar
            bar_x = 30
            bar_width = self.width - bar_x - 20
            bar_height = max(4, metric_height - 6)
            
            # Background bar
            draw.rectangle(
                [bar_x, y_offset + 2, bar_x + bar_width, y_offset + 2 + bar_height],
                fill=(50, 50, 50)
            )
            
            # Fill bar
            fill_width = int(bar_width * percentage / 100)
            if fill_width > 0:
                draw.rectangle(
                    [bar_x, y_offset + 2, bar_x + fill_width, y_offset + 2 + bar_height],
                    fill=metric.color
                )
            
            # Draw value
            value_text = f"{percentage:.0f}%"
            if font:
                draw.text((self.width - 18, y_offset + 1), value_text, fill=metric.color, font=font)
            
            # Draw mini history graph if enabled
            if self.show_graph and metric.name in self._history and len(self._history[metric.name]) > 1:
                history = self._history[metric.name]
                graph_x = bar_x
                graph_y = y_offset + bar_height - 2
                
                for j in range(1, len(history)):
                    x1 = graph_x + int((j - 1) * bar_width / self.history_length)
                    x2 = graph_x + int(j * bar_width / self.history_length)
                    y1 = graph_y - int(history[j - 1] / metric.max_value * (bar_height - 2))
                    y2 = graph_y - int(history[j] / metric.max_value * (bar_height - 2))
                    
                    # Clamp to visible area
                    y1 = max(y_offset + 2, min(y_offset + metric_height - 2, y1))
                    y2 = max(y_offset + 2, min(y_offset + metric_height - 2, y2))
                    
                    draw.line([(x1, y1), (x2, y2)], fill=(255, 255, 255), width=1)
        
        return image


class DisplayModeManager:
    """Manager for multiple display modes."""
    
    def __init__(self, width: int, height: int):
        """Initialize mode manager.
        
        Args:
            width: Display width
            height: Display height
        """
        self.width = width
        self.height = height
        self.modes: Dict[str, Any] = {}
        self.current_mode: Optional[str] = None
        
        self._last_update = time.perf_counter()
    
    def register_mode(self, name: str, mode: Any) -> None:
        """Register a display mode.
        
        Args:
            name: Mode name
            mode: Mode instance
        """
        self.modes[name] = mode
        if self.current_mode is None:
            self.current_mode = name
    
    def get_mode(self, name: str) -> Optional[Any]:
        """Get a registered mode.
        
        Args:
            name: Mode name
            
        Returns:
            Mode instance or None
        """
        return self.modes.get(name)
    
    def set_current_mode(self, name: str) -> bool:
        """Set the current active mode.
        
        Args:
            name: Mode name
            
        Returns:
            True if mode was set
        """
        if name in self.modes:
            self.current_mode = name
            return True
        return False
    
    def list_modes(self) -> List[str]:
        """List all registered mode names.
        
        Returns:
            List of mode names
        """
        return list(self.modes.keys())
    
    def update(self) -> None:
        """Update current mode."""
        current_time = time.perf_counter()
        dt = current_time - self._last_update
        self._last_update = current_time
        
        if self.current_mode and self.current_mode in self.modes:
            mode = self.modes[self.current_mode]
            if hasattr(mode, 'update'):
                mode.update(dt)
    
    def render(self) -> Image.Image:
        """Render current mode.
        
        Returns:
            Rendered image
        """
        if self.current_mode and self.current_mode in self.modes:
            mode = self.modes[self.current_mode]
            if hasattr(mode, 'render'):
                return mode.render()
        
        return Image.new('RGB', (self.width, self.height), (0, 0, 0))
