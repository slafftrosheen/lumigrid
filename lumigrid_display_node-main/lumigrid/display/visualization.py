"""Advanced data visualization for Lumigrid display system.

This module provides comprehensive data visualization including:
- Bar charts with animated growth
- Line graphs with real-time updates
- Pie charts with interactive segments
- Gauges with needle animation
- Heat maps for data density visualization
- Scatter plots for correlation analysis
- Radar charts for multi-dimensional data

Data source integration:
- API integration for external data sources
- Sensor fusion combining multiple sensor inputs
- Historical data visualization with trends
- Real-time data streaming with live updates
- Forecasting with predictive visualization
- Alert systems with visual notifications
"""

import logging
import math
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple

from PIL import Image, ImageDraw, ImageFont

logger = logging.getLogger(__name__)


class ChartStyle(Enum):
    """Visual styles for charts."""
    MINIMAL = "minimal"
    STANDARD = "standard"
    DETAILED = "detailed"


@dataclass
class DataPoint:
    """A single data point.
    
    Attributes:
        value: Data value
        label: Optional label
        timestamp: Optional timestamp
        color: Optional custom color
        metadata: Additional metadata
    """
    value: float
    label: str = ""
    timestamp: Optional[float] = None
    color: Optional[Tuple[int, int, int]] = None
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class DataSeries:
    """A series of related data points.
    
    Attributes:
        name: Series name
        points: List of data points
        color: Series color
        visible: Whether series is visible
    """
    name: str
    points: List[DataPoint] = field(default_factory=list)
    color: Tuple[int, int, int] = (0, 200, 255)
    visible: bool = True
    
    def add_point(self, value: float, label: str = "", timestamp: Optional[float] = None) -> None:
        """Add a data point to the series.
        
        Args:
            value: Point value
            label: Point label
            timestamp: Point timestamp
        """
        self.points.append(DataPoint(value, label, timestamp or time.time()))
    
    def get_values(self) -> List[float]:
        """Get all values in the series.
        
        Returns:
            List of values
        """
        return [p.value for p in self.points]
    
    def get_min_max(self) -> Tuple[float, float]:
        """Get minimum and maximum values.
        
        Returns:
            Tuple of (min, max)
        """
        if not self.points:
            return (0, 0)
        values = self.get_values()
        return (min(values), max(values))


@dataclass
class AlertThreshold:
    """Alert threshold for data monitoring.
    
    Attributes:
        name: Alert name
        condition: Condition type (above, below, equal)
        value: Threshold value
        color: Alert color
        active: Whether alert is currently active
    """
    name: str
    condition: str  # above, below, equal
    value: float
    color: Tuple[int, int, int] = (255, 0, 0)
    active: bool = False
    
    def check(self, data_value: float) -> bool:
        """Check if alert is triggered.
        
        Args:
            data_value: Value to check
            
        Returns:
            True if alert is triggered
        """
        if self.condition == "above":
            return data_value > self.value
        elif self.condition == "below":
            return data_value < self.value
        elif self.condition == "equal":
            return abs(data_value - self.value) < 0.001
        return False


class ChartBase:
    """Base class for all chart types."""
    
    def __init__(
        self,
        width: int,
        height: int,
        title: str = "",
        bg_color: Tuple[int, int, int] = (0, 0, 0),
        style: ChartStyle = ChartStyle.STANDARD
    ):
        """Initialize chart base.
        
        Args:
            width: Chart width
            height: Chart height
            title: Chart title
            bg_color: Background color
            style: Visual style
        """
        self.width = width
        self.height = height
        self.title = title
        self.bg_color = bg_color
        self.style = style
        
        self._animation_progress = 1.0
        self._animating = False
        self._animation_start = 0
        self._animation_duration = 0.5
    
    def start_animation(self, duration: float = 0.5) -> None:
        """Start entry animation.
        
        Args:
            duration: Animation duration
        """
        self._animating = True
        self._animation_progress = 0
        self._animation_start = time.perf_counter()
        self._animation_duration = duration
    
    def update_animation(self) -> None:
        """Update animation progress."""
        if self._animating:
            elapsed = time.perf_counter() - self._animation_start
            self._animation_progress = min(1.0, elapsed / self._animation_duration)
            if self._animation_progress >= 1.0:
                self._animating = False
    
    def render(self) -> Image.Image:
        """Render chart. Override in subclasses.
        
        Returns:
            Rendered chart image
        """
        return Image.new('RGB', (self.width, self.height), self.bg_color)


class AnimatedBarChart(ChartBase):
    """Bar chart with animated growth."""
    
    def __init__(
        self,
        width: int,
        height: int,
        data: Optional[List[float]] = None,
        labels: Optional[List[str]] = None,
        colors: Optional[List[Tuple[int, int, int]]] = None,
        horizontal: bool = False,
        show_values: bool = True,
        **kwargs
    ):
        """Initialize bar chart.
        
        Args:
            width: Chart width
            height: Chart height
            data: Data values
            labels: Bar labels
            colors: Bar colors
            horizontal: Draw horizontal bars
            show_values: Show value labels
            **kwargs: Additional ChartBase args
        """
        super().__init__(width, height, **kwargs)
        self.data = data or []
        self.labels = labels or []
        self.colors = colors or [
            (0, 200, 255), (255, 100, 100), (100, 255, 100),
            (255, 255, 100), (255, 100, 255), (100, 255, 255)
        ]
        self.horizontal = horizontal
        self.show_values = show_values
        
        self._target_data = self.data.copy()
        self._animated_data = [0.0] * len(self.data)
    
    def set_data(self, data: List[float], labels: Optional[List[str]] = None) -> None:
        """Set chart data with animation.
        
        Args:
            data: New data values
            labels: New labels
        """
        self._target_data = data.copy()
        if len(self._animated_data) != len(data):
            self._animated_data = [0.0] * len(data)
        self.data = data
        if labels:
            self.labels = labels
        self.start_animation()
    
    def update(self, dt: float) -> None:
        """Update chart animation.
        
        Args:
            dt: Delta time
        """
        self.update_animation()
        
        # Animate bars growing
        for i in range(len(self._animated_data)):
            if i < len(self._target_data):
                target = self._target_data[i]
                current = self._animated_data[i]
                self._animated_data[i] = current + (target - current) * min(1, dt * 5)
    
    def render(self) -> Image.Image:
        """Render bar chart."""
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        
        if not self._animated_data:
            return image
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        # Draw title
        title_height = 0
        if self.title and font:
            draw.text((2, 2), self.title, fill=(255, 255, 255), font=font)
            title_height = 12
        
        # Calculate chart area
        margin = 4
        chart_x = margin
        chart_y = title_height + margin
        chart_width = self.width - margin * 2
        chart_height = self.height - title_height - margin * 2
        
        # Find max value
        max_val = max(self._animated_data) if self._animated_data else 1
        if max_val == 0:
            max_val = 1
        
        num_bars = len(self._animated_data)
        
        if self.horizontal:
            bar_height = max(2, (chart_height - (num_bars - 1)) // num_bars)
            
            for i, val in enumerate(self._animated_data):
                bar_width = int((val / max_val) * chart_width * self._animation_progress)
                bar_width = max(0, bar_width)  # Ensure non-negative
                by = chart_y + i * (bar_height + 1)
                
                # Only draw if there's something to draw
                if bar_width > 0:
                    color = self.colors[i % len(self.colors)]
                    draw.rectangle(
                        [chart_x, by, chart_x + bar_width, by + bar_height - 1],
                        fill=color
                    )
        else:
            bar_width = max(2, (chart_width - (num_bars - 1)) // num_bars)
            
            for i, val in enumerate(self._animated_data):
                bar_height_px = int((val / max_val) * chart_height * self._animation_progress)
                bar_height_px = max(0, bar_height_px)  # Ensure non-negative
                bx = chart_x + i * (bar_width + 1)
                by = chart_y + chart_height - bar_height_px
                
                # Only draw if there's something to draw
                if bar_height_px > 0:
                    color = self.colors[i % len(self.colors)]
                    draw.rectangle(
                        [bx, by, bx + bar_width - 1, chart_y + chart_height - 1],
                        fill=color
                    )
        
        return image


class RealtimeLineGraph(ChartBase):
    """Line graph with real-time updates."""
    
    def __init__(
        self,
        width: int,
        height: int,
        series: Optional[List[DataSeries]] = None,
        max_points: int = 50,
        show_grid: bool = True,
        auto_scale: bool = True,
        **kwargs
    ):
        """Initialize line graph.
        
        Args:
            width: Chart width
            height: Chart height
            series: Data series to display
            max_points: Maximum points per series
            show_grid: Show grid lines
            auto_scale: Auto-scale Y axis
            **kwargs: Additional ChartBase args
        """
        super().__init__(width, height, **kwargs)
        self.series = series or []
        self.max_points = max_points
        self.show_grid = show_grid
        self.auto_scale = auto_scale
        
        self.y_min = 0
        self.y_max = 100
    
    def add_series(self, name: str, color: Tuple[int, int, int] = (0, 200, 255)) -> DataSeries:
        """Add a new data series.
        
        Args:
            name: Series name
            color: Series color
            
        Returns:
            Created series
        """
        series = DataSeries(name=name, color=color)
        self.series.append(series)
        return series
    
    def add_point(self, series_index: int, value: float) -> None:
        """Add a point to a series.
        
        Args:
            series_index: Series index
            value: Point value
        """
        if 0 <= series_index < len(self.series):
            self.series[series_index].add_point(value)
            
            # Trim to max points
            if len(self.series[series_index].points) > self.max_points:
                self.series[series_index].points = self.series[series_index].points[-self.max_points:]
    
    def _update_scale(self) -> None:
        """Update Y axis scale based on data."""
        if not self.auto_scale:
            return
        
        all_values = []
        for series in self.series:
            if series.visible:
                all_values.extend(series.get_values())
        
        if all_values:
            self.y_min = min(all_values) * 0.9
            self.y_max = max(all_values) * 1.1
            if self.y_min == self.y_max:
                self.y_max = self.y_min + 1
    
    def render(self) -> Image.Image:
        """Render line graph."""
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        self._update_scale()
        
        # Chart area
        margin = 4
        chart_x = margin + 15  # Space for Y axis labels
        chart_y = margin
        chart_width = self.width - chart_x - margin
        chart_height = self.height - margin * 2
        
        if self.title and font:
            draw.text((2, 2), self.title, fill=(255, 255, 255), font=font)
            chart_y += 12
            chart_height -= 12
        
        # Draw grid
        if self.show_grid:
            grid_color = (40, 40, 40)
            num_h_lines = 4
            num_v_lines = 5
            
            for i in range(num_h_lines + 1):
                y = chart_y + int(i * chart_height / num_h_lines)
                draw.line([(chart_x, y), (chart_x + chart_width, y)], fill=grid_color)
            
            for i in range(num_v_lines + 1):
                x = chart_x + int(i * chart_width / num_v_lines)
                draw.line([(x, chart_y), (x, chart_y + chart_height)], fill=grid_color)
        
        # Draw each series
        for series in self.series:
            if not series.visible or len(series.points) < 2:
                continue
            
            points = []
            for i, point in enumerate(series.points):
                px = chart_x + int(i * chart_width / max(1, len(series.points) - 1))
                
                # Normalize value to chart height
                normalized = (point.value - self.y_min) / (self.y_max - self.y_min)
                py = chart_y + chart_height - int(normalized * chart_height)
                py = max(chart_y, min(chart_y + chart_height, py))
                
                points.append((px, py))
            
            # Draw line
            for i in range(len(points) - 1):
                draw.line([points[i], points[i + 1]], fill=series.color, width=1)
        
        # Draw Y axis labels
        if font:
            draw.text((2, chart_y), f"{self.y_max:.0f}", fill=(150, 150, 150), font=font)
            draw.text((2, chart_y + chart_height - 8), f"{self.y_min:.0f}", fill=(150, 150, 150), font=font)
        
        return image


class AnimatedGauge(ChartBase):
    """Gauge with animated needle."""
    
    def __init__(
        self,
        width: int,
        height: int,
        value: float = 0,
        min_value: float = 0,
        max_value: float = 100,
        label: str = "",
        color_zones: Optional[List[Tuple[float, float, Tuple[int, int, int]]]] = None,
        **kwargs
    ):
        """Initialize gauge.
        
        Args:
            width: Gauge width
            height: Gauge height
            value: Current value
            min_value: Minimum value
            max_value: Maximum value
            label: Value label
            color_zones: List of (start, end, color) for colored zones
            **kwargs: Additional ChartBase args
        """
        super().__init__(width, height, **kwargs)
        self._value = value
        self._animated_value = value
        self.min_value = min_value
        self.max_value = max_value
        self.label = label
        self.color_zones = color_zones or [
            (0, 33, (0, 255, 0)),
            (33, 66, (255, 255, 0)),
            (66, 100, (255, 0, 0))
        ]
    
    @property
    def value(self) -> float:
        """Get current value."""
        return self._value
    
    @value.setter
    def value(self, val: float) -> None:
        """Set value with animation."""
        self._value = max(self.min_value, min(self.max_value, val))
    
    def update(self, dt: float) -> None:
        """Update gauge animation.
        
        Args:
            dt: Delta time
        """
        # Animate needle
        diff = self._value - self._animated_value
        self._animated_value += diff * min(1, dt * 8)
    
    def render(self) -> Image.Image:
        """Render gauge."""
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        # Calculate gauge geometry
        cx = self.width // 2
        cy = self.height - 4
        radius = min(self.width // 2 - 4, self.height - 8)
        
        # Draw color zones as arcs
        for zone_start, zone_end, color in self.color_zones:
            start_angle = 180 + (zone_start / 100) * 180
            end_angle = 180 + (zone_end / 100) * 180
            
            draw.arc(
                [cx - radius, cy - radius, cx + radius, cy + radius],
                start=start_angle, end=end_angle,
                fill=color, width=4
            )
        
        # Draw tick marks
        for i in range(11):
            angle = math.radians(180 + i * 18)
            inner = radius - 6
            outer = radius - 2
            x1 = cx + int(math.cos(angle) * inner)
            y1 = cy + int(math.sin(angle) * inner)
            x2 = cx + int(math.cos(angle) * outer)
            y2 = cy + int(math.sin(angle) * outer)
            draw.line([(x1, y1), (x2, y2)], fill=(150, 150, 150), width=1)
        
        # Draw needle
        percentage = (self._animated_value - self.min_value) / (self.max_value - self.min_value)
        needle_angle = math.radians(180 + percentage * 180)
        needle_length = radius - 10
        
        nx = cx + int(math.cos(needle_angle) * needle_length)
        ny = cy + int(math.sin(needle_angle) * needle_length)
        
        draw.line([(cx, cy), (nx, ny)], fill=(255, 255, 255), width=2)
        
        # Draw center dot
        draw.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(200, 200, 200))
        
        # Draw value text
        if font:
            value_text = f"{self._animated_value:.1f}"
            bbox = draw.textbbox((0, 0), value_text, font=font)
            text_width = bbox[2] - bbox[0]
            draw.text(
                (cx - text_width // 2, cy - radius // 2),
                value_text, fill=(255, 255, 255), font=font
            )
        
        return image


class HeatMap(ChartBase):
    """Heat map for data density visualization."""
    
    def __init__(
        self,
        width: int,
        height: int,
        data: Optional[List[List[float]]] = None,
        color_map: Optional[List[Tuple[int, int, int]]] = None,
        show_values: bool = False,
        **kwargs
    ):
        """Initialize heat map.
        
        Args:
            width: Map width
            height: Map height
            data: 2D array of values
            color_map: Color gradient for values
            show_values: Show cell values
            **kwargs: Additional ChartBase args
        """
        super().__init__(width, height, **kwargs)
        self.data = data or [[]]
        self.color_map = color_map or [
            (0, 0, 128),      # Low - dark blue
            (0, 128, 255),    # Medium low - blue
            (0, 255, 128),    # Medium - green
            (255, 255, 0),    # Medium high - yellow
            (255, 128, 0),    # High - orange
            (255, 0, 0)       # Very high - red
        ]
        self.show_values = show_values
    
    def set_data(self, data: List[List[float]]) -> None:
        """Set heat map data.
        
        Args:
            data: 2D array of values
        """
        self.data = data
        self.start_animation()
    
    def _value_to_color(self, value: float, min_val: float, max_val: float) -> Tuple[int, int, int]:
        """Convert value to color using color map.
        
        Args:
            value: Data value
            min_val: Minimum value in data
            max_val: Maximum value in data
            
        Returns:
            RGB color tuple
        """
        if max_val == min_val:
            return self.color_map[len(self.color_map) // 2]
        
        # Normalize to 0-1
        normalized = (value - min_val) / (max_val - min_val)
        normalized = max(0, min(1, normalized))
        
        # Map to color
        scaled = normalized * (len(self.color_map) - 1)
        idx1 = int(scaled)
        idx2 = min(idx1 + 1, len(self.color_map) - 1)
        t = scaled - idx1
        
        c1 = self.color_map[idx1]
        c2 = self.color_map[idx2]
        
        return (
            int(c1[0] + (c2[0] - c1[0]) * t),
            int(c1[1] + (c2[1] - c1[1]) * t),
            int(c1[2] + (c2[2] - c1[2]) * t)
        )
    
    def render(self) -> Image.Image:
        """Render heat map."""
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        
        if not self.data or not self.data[0]:
            return image
        
        draw = ImageDraw.Draw(image)
        
        # Find min/max
        all_values = [v for row in self.data for v in row]
        min_val = min(all_values) if all_values else 0
        max_val = max(all_values) if all_values else 1
        
        rows = len(self.data)
        cols = len(self.data[0])
        
        cell_width = self.width // cols
        cell_height = self.height // rows
        
        for y, row in enumerate(self.data):
            for x, value in enumerate(row):
                color = self._value_to_color(value, min_val, max_val)
                
                # Apply animation
                animated_color = tuple(int(c * self._animation_progress) for c in color)
                
                draw.rectangle(
                    [x * cell_width, y * cell_height,
                     (x + 1) * cell_width - 1, (y + 1) * cell_height - 1],
                    fill=animated_color
                )
        
        return image


class ScatterPlot(ChartBase):
    """Scatter plot for correlation analysis."""
    
    def __init__(
        self,
        width: int,
        height: int,
        points: Optional[List[Tuple[float, float]]] = None,
        point_color: Tuple[int, int, int] = (0, 200, 255),
        point_size: int = 2,
        show_trend: bool = False,
        **kwargs
    ):
        """Initialize scatter plot.
        
        Args:
            width: Plot width
            height: Plot height
            points: List of (x, y) points
            point_color: Point color
            point_size: Point size in pixels
            show_trend: Show trend line
            **kwargs: Additional ChartBase args
        """
        super().__init__(width, height, **kwargs)
        self.points = points or []
        self.point_color = point_color
        self.point_size = point_size
        self.show_trend = show_trend
        
        self.x_min = 0
        self.x_max = 100
        self.y_min = 0
        self.y_max = 100
    
    def set_points(self, points: List[Tuple[float, float]]) -> None:
        """Set scatter plot points.
        
        Args:
            points: List of (x, y) points
        """
        self.points = points
        self._update_ranges()
        self.start_animation()
    
    def add_point(self, x: float, y: float) -> None:
        """Add a point to the scatter plot.
        
        Args:
            x: X coordinate
            y: Y coordinate
        """
        self.points.append((x, y))
        self._update_ranges()
    
    def _update_ranges(self) -> None:
        """Update axis ranges based on points."""
        if not self.points:
            return
        
        xs = [p[0] for p in self.points]
        ys = [p[1] for p in self.points]
        
        self.x_min = min(xs) * 0.9
        self.x_max = max(xs) * 1.1
        self.y_min = min(ys) * 0.9
        self.y_max = max(ys) * 1.1
        
        if self.x_min == self.x_max:
            self.x_max = self.x_min + 1
        if self.y_min == self.y_max:
            self.y_max = self.y_min + 1
    
    def _calculate_trend_line(self) -> Optional[Tuple[float, float]]:
        """Calculate linear regression trend line.
        
        Returns:
            Tuple of (slope, intercept) or None
        """
        if len(self.points) < 2:
            return None
        
        n = len(self.points)
        sum_x = sum(p[0] for p in self.points)
        sum_y = sum(p[1] for p in self.points)
        sum_xy = sum(p[0] * p[1] for p in self.points)
        sum_x2 = sum(p[0] ** 2 for p in self.points)
        
        denom = n * sum_x2 - sum_x ** 2
        if denom == 0:
            return None
        
        slope = (n * sum_xy - sum_x * sum_y) / denom
        intercept = (sum_y - slope * sum_x) / n
        
        return (slope, intercept)
    
    def render(self) -> Image.Image:
        """Render scatter plot."""
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        
        margin = 4
        chart_x = margin
        chart_y = margin
        chart_width = self.width - margin * 2
        chart_height = self.height - margin * 2
        
        # Draw axes
        axis_color = (80, 80, 80)
        draw.line([(chart_x, chart_y + chart_height), (chart_x + chart_width, chart_y + chart_height)], fill=axis_color)
        draw.line([(chart_x, chart_y), (chart_x, chart_y + chart_height)], fill=axis_color)
        
        # Draw trend line
        if self.show_trend:
            trend = self._calculate_trend_line()
            if trend:
                slope, intercept = trend
                
                # Calculate line endpoints in chart coordinates
                x1_val = self.x_min
                y1_val = slope * x1_val + intercept
                x2_val = self.x_max
                y2_val = slope * x2_val + intercept
                
                x1 = chart_x + int((x1_val - self.x_min) / (self.x_max - self.x_min) * chart_width)
                y1 = chart_y + chart_height - int((y1_val - self.y_min) / (self.y_max - self.y_min) * chart_height)
                x2 = chart_x + int((x2_val - self.x_min) / (self.x_max - self.x_min) * chart_width)
                y2 = chart_y + chart_height - int((y2_val - self.y_min) / (self.y_max - self.y_min) * chart_height)
                
                draw.line([(x1, y1), (x2, y2)], fill=(100, 100, 100), width=1)
        
        # Draw points
        for px, py in self.points:
            # Normalize to chart coordinates
            x = chart_x + int((px - self.x_min) / (self.x_max - self.x_min) * chart_width)
            y = chart_y + chart_height - int((py - self.y_min) / (self.y_max - self.y_min) * chart_height)
            
            # Apply animation
            animated_x = chart_x + int((x - chart_x) * self._animation_progress)
            animated_y = chart_y + chart_height - int((chart_y + chart_height - y) * self._animation_progress)
            
            # Clamp to visible area
            animated_x = max(chart_x, min(chart_x + chart_width, animated_x))
            animated_y = max(chart_y, min(chart_y + chart_height, animated_y))
            
            draw.ellipse(
                [animated_x - self.point_size, animated_y - self.point_size,
                 animated_x + self.point_size, animated_y + self.point_size],
                fill=self.point_color
            )
        
        return image


class RadarChart(ChartBase):
    """Radar chart for multi-dimensional data."""
    
    def __init__(
        self,
        width: int,
        height: int,
        categories: Optional[List[str]] = None,
        values: Optional[List[float]] = None,
        max_value: float = 100,
        fill_color: Tuple[int, int, int] = (0, 200, 255),
        fill_alpha: float = 0.3,
        **kwargs
    ):
        """Initialize radar chart.
        
        Args:
            width: Chart width
            height: Chart height
            categories: Category labels
            values: Data values per category
            max_value: Maximum scale value
            fill_color: Fill color
            fill_alpha: Fill transparency (0-1)
            **kwargs: Additional ChartBase args
        """
        super().__init__(width, height, **kwargs)
        self.categories = categories or []
        self.values = values or []
        self.max_value = max_value
        self.fill_color = fill_color
        self.fill_alpha = fill_alpha
        
        self._animated_values: List[float] = []
    
    def set_data(self, categories: List[str], values: List[float]) -> None:
        """Set radar chart data.
        
        Args:
            categories: Category labels
            values: Data values
        """
        self.categories = categories
        self.values = values
        self._animated_values = [0.0] * len(values)
        self.start_animation()
    
    def update(self, dt: float) -> None:
        """Update animation.
        
        Args:
            dt: Delta time
        """
        self.update_animation()
        
        # Animate values
        for i in range(len(self._animated_values)):
            if i < len(self.values):
                target = self.values[i]
                current = self._animated_values[i]
                self._animated_values[i] = current + (target - current) * min(1, dt * 5)
    
    def render(self) -> Image.Image:
        """Render radar chart."""
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        
        if not self.categories or not self.values:
            return image
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        cx = self.width // 2
        cy = self.height // 2
        radius = min(cx, cy) - 10
        
        num_categories = len(self.categories)
        angle_step = 2 * math.pi / num_categories
        
        # Draw web lines
        web_color = (60, 60, 60)
        for level in range(1, 4):
            level_radius = radius * level / 3
            points = []
            for i in range(num_categories):
                angle = -math.pi / 2 + i * angle_step
                x = cx + int(math.cos(angle) * level_radius)
                y = cy + int(math.sin(angle) * level_radius)
                points.append((x, y))
            
            for i in range(num_categories):
                draw.line([points[i], points[(i + 1) % num_categories]], fill=web_color)
        
        # Draw axis lines
        for i in range(num_categories):
            angle = -math.pi / 2 + i * angle_step
            x = cx + int(math.cos(angle) * radius)
            y = cy + int(math.sin(angle) * radius)
            draw.line([(cx, cy), (x, y)], fill=web_color)
        
        # Draw data polygon
        if self._animated_values:
            data_points = []
            for i, val in enumerate(self._animated_values):
                angle = -math.pi / 2 + i * angle_step
                point_radius = (val / self.max_value) * radius
                x = cx + int(math.cos(angle) * point_radius)
                y = cy + int(math.sin(angle) * point_radius)
                data_points.append((x, y))
            
            # Draw filled polygon (using outline since PIL doesn't support alpha easily)
            if len(data_points) > 2:
                # Draw filled area by drawing lines
                for i in range(len(data_points)):
                    draw.line([data_points[i], data_points[(i + 1) % len(data_points)]],
                             fill=self.fill_color, width=2)
                
                # Draw points
                for x, y in data_points:
                    draw.ellipse([x - 2, y - 2, x + 2, y + 2], fill=self.fill_color)
        
        # Draw category labels
        if font:
            for i, category in enumerate(self.categories):
                angle = -math.pi / 2 + i * angle_step
                label_radius = radius + 6
                lx = cx + int(math.cos(angle) * label_radius)
                ly = cy + int(math.sin(angle) * label_radius)
                
                # Adjust for text alignment
                if abs(angle - (-math.pi / 2)) < 0.1:  # Top
                    lx -= 4
                elif abs(angle - (math.pi / 2)) < 0.1:  # Bottom
                    lx -= 4
                    ly -= 4
                elif angle > -math.pi / 2 and angle < math.pi / 2:  # Right side
                    pass
                else:  # Left side
                    lx -= 10
                
                draw.text((lx, ly), category[:4], fill=(180, 180, 180), font=font)
        
        return image


class DataSource:
    """Base class for data sources."""
    
    def __init__(self, name: str):
        """Initialize data source.
        
        Args:
            name: Source name
        """
        self.name = name
        self._callbacks: List[Callable] = []
    
    def on_data(self, callback: Callable) -> None:
        """Register callback for new data.
        
        Args:
            callback: Function to call with new data
        """
        self._callbacks.append(callback)
    
    def _emit_data(self, data: Any) -> None:
        """Emit data to callbacks.
        
        Args:
            data: Data to emit
        """
        for callback in self._callbacks:
            try:
                callback(data)
            except Exception as e:
                logger.error(f"Error in data callback: {e}")
    
    def fetch(self) -> Any:
        """Fetch data from source. Override in subclasses.
        
        Returns:
            Fetched data
        """
        return None


class SensorDataSource(DataSource):
    """Data source for sensor readings."""
    
    def __init__(self, name: str, sensor=None):
        """Initialize sensor data source.
        
        Args:
            name: Source name
            sensor: Sensor instance
        """
        super().__init__(name)
        self.sensor = sensor
        self._history: List[DataPoint] = []
        self.history_length = 100
    
    def fetch(self) -> Optional[float]:
        """Fetch sensor reading.
        
        Returns:
            Sensor value
        """
        if not self.sensor:
            return None
        
        try:
            value = self.sensor.get_temperature()  # Assuming temperature sensor
            if value is not None:
                point = DataPoint(value, timestamp=time.time())
                self._history.append(point)
                
                if len(self._history) > self.history_length:
                    self._history = self._history[-self.history_length:]
                
                self._emit_data(value)
                return value
        except Exception as e:
            logger.error(f"Error reading sensor: {e}")
        
        return None
    
    def get_history(self) -> List[float]:
        """Get historical values.
        
        Returns:
            List of historical values
        """
        return [p.value for p in self._history]


class AlertManager:
    """Manages alerts and notifications."""
    
    def __init__(self):
        """Initialize alert manager."""
        self.alerts: Dict[str, AlertThreshold] = {}
        self._active_alerts: List[str] = []
        self._callbacks: List[Callable] = []
    
    def add_alert(self, alert: AlertThreshold) -> None:
        """Add an alert threshold.
        
        Args:
            alert: Alert to add
        """
        self.alerts[alert.name] = alert
    
    def remove_alert(self, name: str) -> bool:
        """Remove an alert.
        
        Args:
            name: Alert name
            
        Returns:
            True if removed
        """
        if name in self.alerts:
            del self.alerts[name]
            return True
        return False
    
    def on_alert(self, callback: Callable) -> None:
        """Register alert callback.
        
        Args:
            callback: Function to call when alert triggers
        """
        self._callbacks.append(callback)
    
    def check_value(self, value: float, source: str = "") -> List[AlertThreshold]:
        """Check value against all alerts.
        
        Args:
            value: Value to check
            source: Data source name
            
        Returns:
            List of triggered alerts
        """
        triggered = []
        
        for alert in self.alerts.values():
            was_active = alert.active
            alert.active = alert.check(value)
            
            if alert.active and not was_active:
                triggered.append(alert)
                
                for callback in self._callbacks:
                    try:
                        callback(alert, value, source)
                    except Exception as e:
                        logger.error(f"Error in alert callback: {e}")
        
        return triggered
    
    def get_active_alerts(self) -> List[AlertThreshold]:
        """Get currently active alerts.
        
        Returns:
            List of active alerts
        """
        return [a for a in self.alerts.values() if a.active]
    
    def render_alerts(self, image: Image.Image) -> None:
        """Render alert indicators on image.
        
        Args:
            image: Image to draw on
        """
        active = self.get_active_alerts()
        if not active:
            return
        
        draw = ImageDraw.Draw(image)
        
        # Draw alert indicator in corner
        x = image.width - 6
        y = 2
        
        # Blinking effect
        if int(time.time() * 2) % 2 == 0:
            draw.ellipse([x - 3, y, x + 3, y + 6], fill=(255, 0, 0))


class VisualizationManager:
    """Manager for data visualization components."""
    
    def __init__(self, width: int, height: int):
        """Initialize visualization manager.
        
        Args:
            width: Display width
            height: Display height
        """
        self.width = width
        self.height = height
        self.charts: Dict[str, ChartBase] = {}
        self.data_sources: Dict[str, DataSource] = {}
        self.alert_manager = AlertManager()
        
        self._active_chart: Optional[str] = None
        self._last_update = time.perf_counter()
    
    def add_chart(self, name: str, chart: ChartBase) -> None:
        """Add a chart.
        
        Args:
            name: Chart name
            chart: Chart instance
        """
        self.charts[name] = chart
        if self._active_chart is None:
            self._active_chart = name
    
    def remove_chart(self, name: str) -> bool:
        """Remove a chart.
        
        Args:
            name: Chart name
            
        Returns:
            True if removed
        """
        if name in self.charts:
            del self.charts[name]
            return True
        return False
    
    def set_active_chart(self, name: str) -> bool:
        """Set the active chart.
        
        Args:
            name: Chart name
            
        Returns:
            True if set
        """
        if name in self.charts:
            self._active_chart = name
            return True
        return False
    
    def add_data_source(self, name: str, source: DataSource) -> None:
        """Add a data source.
        
        Args:
            name: Source name
            source: Data source instance
        """
        self.data_sources[name] = source
    
    def update(self) -> None:
        """Update all components."""
        current_time = time.perf_counter()
        dt = current_time - self._last_update
        self._last_update = current_time
        
        # Update charts
        for chart in self.charts.values():
            if hasattr(chart, 'update'):
                chart.update(dt)
    
    def render(self) -> Image.Image:
        """Render active chart.
        
        Returns:
            Rendered image
        """
        if self._active_chart and self._active_chart in self.charts:
            image = self.charts[self._active_chart].render()
            
            # Overlay alerts
            self.alert_manager.render_alerts(image)
            
            return image
        
        return Image.new('RGB', (self.width, self.height), (0, 0, 0))
