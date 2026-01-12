"""Tests for the display modes module."""

import pytest
from unittest.mock import MagicMock, patch
from PIL import Image
from datetime import datetime, timedelta

from lumigrid.display.display_modes import (
    DisplayModeType,
    AnimationFrame,
    VideoPlayerMode,
    AdvancedSlideshowMode,
    WorldClock,
    WorldClockMode,
    CalendarEvent,
    CalendarMode,
    WeatherData,
    WeatherDashboardMode,
    TickerItem,
    StockTickerMode,
    NewsItem,
    NewsTickerMode,
    SystemMetric,
    SystemMonitorMode,
    DisplayModeManager,
)


class TestVideoPlayerMode:
    """Tests for VideoPlayerMode class."""
    
    def test_creation(self):
        """Test video player creation."""
        player = VideoPlayerMode(width=64, height=32)
        
        assert player.width == 64
        assert player.height == 32
        assert len(player.frames) == 0
    
    def test_add_frame(self):
        """Test adding frames."""
        player = VideoPlayerMode(width=64, height=32)
        image = Image.new('RGB', (100, 100), (255, 0, 0))
        
        player.add_frame(image, duration=0.1)
        
        assert len(player.frames) == 1
        assert player.frames[0].image.size == (64, 32)
    
    def test_play_pause(self):
        """Test play/pause functionality."""
        player = VideoPlayerMode(width=64, height=32, auto_play=False)
        
        assert player.playing is False
        
        player.play()
        assert player.playing is True
        
        player.pause()
        assert player.playing is False
    
    def test_stop(self):
        """Test stop functionality."""
        player = VideoPlayerMode(width=64, height=32)
        player.add_frame(Image.new('RGB', (64, 32), (255, 0, 0)), 0.1)
        player.add_frame(Image.new('RGB', (64, 32), (0, 255, 0)), 0.1)
        
        player.seek(1)
        player.stop()
        
        assert player.current_frame_index == 0
        assert player.playing is False
    
    def test_render_empty(self):
        """Test rendering with no frames."""
        player = VideoPlayerMode(width=64, height=32)
        image = player.render()
        
        assert image.size == (64, 32)
    
    def test_render_with_frames(self):
        """Test rendering with frames."""
        player = VideoPlayerMode(width=64, height=32)
        player.add_frame(Image.new('RGB', (64, 32), (255, 0, 0)), 0.1)
        
        image = player.render()
        
        assert image.size == (64, 32)


class TestAdvancedSlideshowMode:
    """Tests for AdvancedSlideshowMode class."""
    
    def test_creation(self):
        """Test slideshow creation."""
        slideshow = AdvancedSlideshowMode(width=64, height=32)
        
        assert slideshow.width == 64
        assert slideshow.height == 32
        assert len(slideshow.images) == 0
    
    def test_add_image(self):
        """Test adding images."""
        slideshow = AdvancedSlideshowMode(width=64, height=32)
        image = Image.new('RGB', (100, 100), (255, 0, 0))
        
        slideshow.add_image(image)
        
        assert len(slideshow.images) == 1
        assert slideshow.images[0].size == (64, 32)
    
    def test_navigation(self):
        """Test image navigation."""
        slideshow = AdvancedSlideshowMode(width=64, height=32)
        slideshow.add_image(Image.new('RGB', (64, 32), (255, 0, 0)))
        slideshow.add_image(Image.new('RGB', (64, 32), (0, 255, 0)))
        
        assert slideshow.current_index == 0
        
        slideshow.previous_image()
        assert slideshow.current_index == 1  # Wraps around
    
    def test_transition_fade(self):
        """Test fade transition."""
        slideshow = AdvancedSlideshowMode(
            width=64, height=32,
            transition_type="fade"
        )
        slideshow.add_image(Image.new('RGB', (64, 32), (255, 0, 0)))
        slideshow.add_image(Image.new('RGB', (64, 32), (0, 255, 0)))
        
        slideshow._transitioning = True
        slideshow._transition_progress = 0.5
        
        image = slideshow.render()
        assert image.size == (64, 32)
    
    def test_render(self):
        """Test slideshow rendering."""
        slideshow = AdvancedSlideshowMode(width=64, height=32)
        slideshow.add_image(Image.new('RGB', (64, 32), (255, 0, 0)))
        
        image = slideshow.render()
        
        assert image.size == (64, 32)


class TestWorldClockMode:
    """Tests for WorldClockMode class."""
    
    def test_creation(self):
        """Test world clock creation."""
        clock = WorldClockMode(width=64, height=32)
        
        assert clock.width == 64
        assert clock.height == 32
        assert len(clock.clocks) >= 1  # Has default UTC clock
    
    def test_add_clock(self):
        """Test adding clocks."""
        clock = WorldClockMode(width=64, height=32, clocks=[])
        clock.add_clock("America/New_York", "NYC", (0, 200, 255))
        
        assert len(clock.clocks) >= 1
        # Verify the clock we added is in the list
        nyc_clock = [c for c in clock.clocks if c.label == "NYC"]
        assert len(nyc_clock) == 1
    
    def test_remove_clock(self):
        """Test removing clocks."""
        clock = WorldClockMode(width=64, height=32, clocks=[
            WorldClock("UTC", "UTC"),
            WorldClock("America/New_York", "NYC")
        ])
        
        result = clock.remove_clock(0)
        
        assert result is True
        assert len(clock.clocks) == 1
    
    def test_switch_clock(self):
        """Test clock switching."""
        clock = WorldClockMode(width=64, height=32, clocks=[
            WorldClock("UTC", "UTC"),
            WorldClock("America/New_York", "NYC")
        ])
        
        clock.switch_clock(1)
        assert clock.current_clock_index == 1
        
        clock.switch_clock(1)  # Should wrap
        assert clock.current_clock_index == 0
    
    def test_render(self):
        """Test world clock rendering."""
        clock = WorldClockMode(width=64, height=32)
        
        image = clock.render()
        
        assert image.size == (64, 32)


class TestCalendarMode:
    """Tests for CalendarMode class."""
    
    def test_creation(self):
        """Test calendar creation."""
        calendar = CalendarMode(width=64, height=32)
        
        assert calendar.width == 64
        assert calendar.height == 32
    
    def test_add_event(self):
        """Test adding events."""
        calendar = CalendarMode(width=64, height=32)
        event = CalendarEvent(
            title="Meeting",
            start=datetime.now(),
            end=datetime.now() + timedelta(hours=1)
        )
        
        calendar.add_event(event)
        
        assert len(calendar.events) == 1
    
    def test_get_events_for_date(self):
        """Test getting events for a specific date."""
        calendar = CalendarMode(width=64, height=32)
        today = datetime.now()
        
        calendar.add_event(CalendarEvent(
            title="Today Event",
            start=today,
            end=today + timedelta(hours=1)
        ))
        
        events = calendar.get_events_for_date(today.date())
        
        assert len(events) == 1
    
    def test_render(self):
        """Test calendar rendering."""
        calendar = CalendarMode(width=64, height=32)
        
        image = calendar.render()
        
        assert image.size == (64, 32)


class TestWeatherDashboardMode:
    """Tests for WeatherDashboardMode class."""
    
    def test_creation(self):
        """Test weather dashboard creation."""
        weather = WeatherDashboardMode(width=64, height=32)
        
        assert weather.width == 64
        assert weather.height == 32
    
    def test_with_data(self):
        """Test with weather data."""
        data = WeatherData(
            temperature=25,
            condition="sunny",
            humidity=60,
            wind_speed=10
        )
        weather = WeatherDashboardMode(width=64, height=32, weather_data=data)
        
        assert weather.weather.temperature == 25
        assert weather.weather.condition == "sunny"
    
    def test_unit_conversion(self):
        """Test temperature unit conversion."""
        data = WeatherData(temperature=20, condition="clear")
        weather = WeatherDashboardMode(width=64, height=32, weather_data=data, unit="fahrenheit")
        
        # Rendering should convert to Fahrenheit
        image = weather.render()
        assert image.size == (64, 32)
    
    def test_render_conditions(self):
        """Test rendering different weather conditions."""
        for condition in ["sunny", "cloudy", "rain", "snow", "unknown"]:
            data = WeatherData(temperature=20, condition=condition)
            weather = WeatherDashboardMode(width=64, height=32, weather_data=data)
            
            image = weather.render()
            assert image.size == (64, 32)


class TestStockTickerMode:
    """Tests for StockTickerMode class."""
    
    def test_creation(self):
        """Test stock ticker creation."""
        ticker = StockTickerMode(width=64, height=32)
        
        assert ticker.width == 64
        assert ticker.height == 32
        assert len(ticker.items) == 0
    
    def test_add_item(self):
        """Test adding ticker items."""
        ticker = StockTickerMode(width=64, height=32)
        ticker.add_item(TickerItem(
            symbol="AAPL",
            value=150.00,
            change=2.50,
            change_percent=1.7
        ))
        
        assert len(ticker.items) == 1
        assert ticker.items[0].symbol == "AAPL"
    
    def test_update_scroll(self):
        """Test scroll update."""
        ticker = StockTickerMode(width=64, height=32, scroll_speed=30.0)
        ticker.add_item(TickerItem(symbol="TEST", value=100.0))
        
        initial_pos = ticker._scroll_position
        ticker.update(0.1)
        
        assert ticker._scroll_position > initial_pos
    
    def test_render(self):
        """Test ticker rendering."""
        ticker = StockTickerMode(width=64, height=32)
        ticker.add_item(TickerItem(symbol="AAPL", value=150.00, change=2.0, change_percent=1.5))
        
        image = ticker.render()
        
        assert image.size == (64, 32)


class TestNewsTickerMode:
    """Tests for NewsTickerMode class."""
    
    def test_creation(self):
        """Test news ticker creation."""
        ticker = NewsTickerMode(width=64, height=32)
        
        assert ticker.width == 64
        assert ticker.height == 32
    
    def test_add_item(self):
        """Test adding news items."""
        ticker = NewsTickerMode(width=64, height=32)
        ticker.add_item(NewsItem(title="Breaking News", source="Test"))
        
        assert len(ticker.items) == 1
    
    def test_render_empty(self):
        """Test rendering with no news."""
        ticker = NewsTickerMode(width=64, height=32)
        
        image = ticker.render()
        
        assert image.size == (64, 32)
    
    def test_render_with_news(self):
        """Test rendering with news."""
        ticker = NewsTickerMode(width=64, height=32)
        ticker.add_item(NewsItem(title="Test Headline"))
        
        image = ticker.render()
        
        assert image.size == (64, 32)


class TestSystemMonitorMode:
    """Tests for SystemMonitorMode class."""
    
    def test_creation(self):
        """Test system monitor creation."""
        monitor = SystemMonitorMode(width=64, height=32)
        
        assert monitor.width == 64
        assert monitor.height == 32
    
    def test_add_metric(self):
        """Test adding metrics."""
        monitor = SystemMonitorMode(width=64, height=32)
        monitor.add_metric(SystemMetric(
            name="CPU",
            value=50,
            max_value=100,
            color=(0, 255, 0)
        ))
        
        assert len(monitor.metrics) == 1
    
    def test_render(self):
        """Test monitor rendering."""
        monitor = SystemMonitorMode(width=64, height=32, metrics=[
            SystemMetric(name="CPU", value=50),
            SystemMetric(name="MEM", value=70)
        ])
        
        image = monitor.render()
        
        assert image.size == (64, 32)


class TestDisplayModeManager:
    """Tests for DisplayModeManager class."""
    
    def test_creation(self):
        """Test manager creation."""
        manager = DisplayModeManager(width=64, height=32)
        
        assert manager.width == 64
        assert manager.height == 32
    
    def test_register_mode(self):
        """Test registering modes."""
        manager = DisplayModeManager(width=64, height=32)
        mode = VideoPlayerMode(width=64, height=32)
        
        manager.register_mode("video", mode)
        
        assert "video" in manager.modes
        assert manager.current_mode == "video"  # First registered becomes current
    
    def test_set_current_mode(self):
        """Test setting current mode."""
        manager = DisplayModeManager(width=64, height=32)
        manager.register_mode("video", VideoPlayerMode(64, 32))
        manager.register_mode("clock", WorldClockMode(64, 32))
        
        result = manager.set_current_mode("clock")
        
        assert result is True
        assert manager.current_mode == "clock"
    
    def test_list_modes(self):
        """Test listing modes."""
        manager = DisplayModeManager(width=64, height=32)
        manager.register_mode("video", VideoPlayerMode(64, 32))
        manager.register_mode("clock", WorldClockMode(64, 32))
        
        modes = manager.list_modes()
        
        assert "video" in modes
        assert "clock" in modes
    
    def test_render(self):
        """Test rendering current mode."""
        manager = DisplayModeManager(width=64, height=32)
        manager.register_mode("clock", WorldClockMode(64, 32))
        
        image = manager.render()
        
        assert image.size == (64, 32)
    
    def test_render_no_mode(self):
        """Test rendering with no modes."""
        manager = DisplayModeManager(width=64, height=32)
        
        image = manager.render()
        
        assert image.size == (64, 32)
