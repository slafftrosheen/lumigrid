"""Advanced frame types for Lumigrid display system.

This module provides advanced frame types including:
- Data visualization frames (charts, graphs)
- Interactive frames
- Template frames with variables
- Conditional frames
- Multi-page frames
- Scrolling frames
- Animated frames
"""

import logging
import math
import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple, Union

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
        _USE_ZONEINFO = None  # No timezone support available

from lumigrid.display.animation import (
    Animation, AnimationPreset, AnimationManager, 
    Easing, EasingType, ParticleEmitter
)
from lumigrid.display.effects import (
    EffectsManager, BackgroundType, EffectType
)

logger = logging.getLogger(__name__)


class AdvancedFrameType(Enum):
    """Types of advanced frames."""
    DATA_VISUALIZATION = "data_visualization"
    INTERACTIVE = "interactive"
    TEMPLATE = "template"
    CONDITIONAL = "conditional"
    MULTI_PAGE = "multi_page"
    SCROLLING = "scrolling"
    ANIMATED = "animated"


class ChartType(Enum):
    """Types of charts for data visualization."""
    BAR = "bar"
    LINE = "line"
    PIE = "pie"
    PROGRESS = "progress"
    GAUGE = "gauge"
    SPARKLINE = "sparkline"


@dataclass
class FrameElement:
    """A visual element within a frame.
    
    Attributes:
        id: Unique element identifier
        type: Element type (text, image, shape, chart)
        x: X position
        y: Y position
        width: Element width
        height: Element height
        properties: Type-specific properties
        layer: Layer order (higher = on top)
        visible: Whether element is visible
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4())[:8])
    type: str = "text"
    x: int = 0
    y: int = 0
    width: int = 0
    height: int = 0
    properties: Dict[str, Any] = field(default_factory=dict)
    layer: int = 0
    visible: bool = True
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert element to dictionary."""
        return {
            'id': self.id,
            'type': self.type,
            'x': self.x,
            'y': self.y,
            'width': self.width,
            'height': self.height,
            'properties': self.properties,
            'layer': self.layer,
            'visible': self.visible
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'FrameElement':
        """Create element from dictionary."""
        return cls(
            id=data.get('id', str(uuid.uuid4())[:8]),
            type=data.get('type', 'text'),
            x=data.get('x', 0),
            y=data.get('y', 0),
            width=data.get('width', 0),
            height=data.get('height', 0),
            properties=data.get('properties', {}),
            layer=data.get('layer', 0),
            visible=data.get('visible', True)
        )


class DataVisualizationFrame:
    """Frame for displaying charts and data visualizations."""
    
    def __init__(
        self,
        width: int,
        height: int,
        chart_type: ChartType = ChartType.BAR,
        data: Optional[List[float]] = None,
        labels: Optional[List[str]] = None,
        colors: Optional[List[Tuple[int, int, int]]] = None,
        bg_color: Tuple[int, int, int] = (0, 0, 0),
        title: str = ""
    ):
        """Initialize data visualization frame.
        
        Args:
            width: Frame width
            height: Frame height
            chart_type: Type of chart to display
            data: Data values
            labels: Data labels
            colors: Colors for data elements
            bg_color: Background color
            title: Chart title
        """
        self.width = width
        self.height = height
        self.chart_type = chart_type
        self.data = data or []
        self.labels = labels or []
        self.colors = colors or [
            (0, 200, 255), (255, 100, 100), (100, 255, 100),
            (255, 255, 100), (255, 100, 255), (100, 255, 255)
        ]
        self.bg_color = bg_color
        self.title = title
        
        self._data_source: Optional[Callable[[], List[float]]] = None
    
    def set_data_source(self, source: Callable[[], List[float]]) -> None:
        """Set dynamic data source function.
        
        Args:
            source: Function that returns data list
        """
        self._data_source = source
    
    def update_data(self) -> None:
        """Update data from source if available."""
        if self._data_source:
            try:
                self.data = self._data_source()
            except Exception as e:
                logger.error(f"Error fetching data: {e}")
    
    def render(self) -> Image.Image:
        """Render the chart.
        
        Returns:
            PIL Image with chart
        """
        self.update_data()
        
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        
        # Draw title if present
        title_height = 0
        if self.title:
            try:
                font = ImageFont.load_default()
                draw.text((2, 2), self.title, fill=(255, 255, 255), font=font)
                title_height = 12
            except Exception:
                pass
        
        # Draw chart based on type
        chart_area = (0, title_height, self.width, self.height)
        
        if self.chart_type == ChartType.BAR:
            self._draw_bar_chart(draw, chart_area)
        elif self.chart_type == ChartType.LINE:
            self._draw_line_chart(draw, chart_area)
        elif self.chart_type == ChartType.PIE:
            self._draw_pie_chart(draw, chart_area)
        elif self.chart_type == ChartType.PROGRESS:
            self._draw_progress_bar(draw, chart_area)
        elif self.chart_type == ChartType.GAUGE:
            self._draw_gauge(draw, chart_area)
        elif self.chart_type == ChartType.SPARKLINE:
            self._draw_sparkline(draw, chart_area)
        
        return image
    
    def _draw_bar_chart(
        self, 
        draw: ImageDraw.ImageDraw, 
        area: Tuple[int, int, int, int]
    ) -> None:
        """Draw bar chart."""
        if not self.data:
            return
        
        x1, y1, x2, y2 = area
        chart_width = x2 - x1 - 4
        chart_height = y2 - y1 - 4
        
        max_val = max(self.data) if self.data else 1
        if max_val == 0:
            max_val = 1
        
        bar_width = max(1, chart_width // len(self.data) - 1)
        
        for i, val in enumerate(self.data):
            bar_height = int((val / max_val) * chart_height)
            bx = x1 + 2 + i * (bar_width + 1)
            by = y2 - 2 - bar_height
            
            color = self.colors[i % len(self.colors)]
            draw.rectangle(
                [bx, by, bx + bar_width, y2 - 2],
                fill=color
            )
    
    def _draw_line_chart(
        self, 
        draw: ImageDraw.ImageDraw, 
        area: Tuple[int, int, int, int]
    ) -> None:
        """Draw line chart."""
        if len(self.data) < 2:
            return
        
        x1, y1, x2, y2 = area
        chart_width = x2 - x1 - 4
        chart_height = y2 - y1 - 4
        
        max_val = max(self.data)
        min_val = min(self.data)
        val_range = max_val - min_val
        if val_range == 0:
            val_range = 1
        
        points = []
        for i, val in enumerate(self.data):
            px = x1 + 2 + int(i * chart_width / (len(self.data) - 1))
            py = y2 - 2 - int((val - min_val) / val_range * chart_height)
            points.append((px, py))
        
        # Draw line
        for i in range(len(points) - 1):
            draw.line([points[i], points[i + 1]], fill=self.colors[0], width=1)
        
        # Draw points
        for px, py in points:
            draw.ellipse([px - 1, py - 1, px + 1, py + 1], fill=self.colors[0])
    
    def _draw_pie_chart(
        self, 
        draw: ImageDraw.ImageDraw, 
        area: Tuple[int, int, int, int]
    ) -> None:
        """Draw pie chart."""
        if not self.data:
            return
        
        x1, y1, x2, y2 = area
        cx = (x1 + x2) // 2
        cy = (y1 + y2) // 2
        radius = min(x2 - x1, y2 - y1) // 2 - 2
        
        total = sum(self.data)
        if total == 0:
            return
        
        start_angle = -90
        for i, val in enumerate(self.data):
            sweep = 360 * val / total
            color = self.colors[i % len(self.colors)]
            
            draw.pieslice(
                [cx - radius, cy - radius, cx + radius, cy + radius],
                start=start_angle,
                end=start_angle + sweep,
                fill=color
            )
            start_angle += sweep
    
    def _draw_progress_bar(
        self, 
        draw: ImageDraw.ImageDraw, 
        area: Tuple[int, int, int, int]
    ) -> None:
        """Draw progress bar."""
        x1, y1, x2, y2 = area
        padding = 2
        
        # Draw background
        draw.rectangle(
            [x1 + padding, y1 + padding, x2 - padding, y2 - padding],
            outline=(50, 50, 50)
        )
        
        # Draw progress
        progress = self.data[0] if self.data else 0
        progress = max(0, min(100, progress))
        bar_width = int((x2 - x1 - padding * 2 - 2) * progress / 100)
        
        if bar_width > 0:
            draw.rectangle(
                [x1 + padding + 1, y1 + padding + 1, 
                 x1 + padding + 1 + bar_width, y2 - padding - 1],
                fill=self.colors[0]
            )
    
    def _draw_gauge(
        self, 
        draw: ImageDraw.ImageDraw, 
        area: Tuple[int, int, int, int]
    ) -> None:
        """Draw gauge/dial chart."""
        x1, y1, x2, y2 = area
        cx = (x1 + x2) // 2
        cy = y2 - 2
        radius = min(x2 - x1, (y2 - y1) * 2) // 2 - 2
        
        # Draw arc background
        draw.arc(
            [cx - radius, cy - radius, cx + radius, cy + radius],
            start=180, end=0,
            fill=(50, 50, 50), width=3
        )
        
        # Draw value arc
        value = self.data[0] if self.data else 0
        value = max(0, min(100, value))
        end_angle = 180 + int(180 * value / 100)
        
        draw.arc(
            [cx - radius, cy - radius, cx + radius, cy + radius],
            start=180, end=end_angle,
            fill=self.colors[0], width=3
        )
        
        # Draw needle
        angle = math.radians(180 + 180 * value / 100)
        nx = cx + int(math.cos(angle) * (radius - 5))
        ny = cy + int(math.sin(angle) * (radius - 5))
        draw.line([(cx, cy), (nx, ny)], fill=(255, 255, 255), width=1)
    
    def _draw_sparkline(
        self, 
        draw: ImageDraw.ImageDraw, 
        area: Tuple[int, int, int, int]
    ) -> None:
        """Draw sparkline (minimal line chart)."""
        if len(self.data) < 2:
            return
        
        x1, y1, x2, y2 = area
        chart_width = x2 - x1
        chart_height = y2 - y1
        
        max_val = max(self.data)
        min_val = min(self.data)
        val_range = max_val - min_val
        if val_range == 0:
            val_range = 1
        
        points = []
        for i, val in enumerate(self.data):
            px = x1 + int(i * chart_width / (len(self.data) - 1))
            py = y2 - int((val - min_val) / val_range * chart_height)
            points.append((px, py))
        
        for i in range(len(points) - 1):
            draw.line([points[i], points[i + 1]], fill=self.colors[0], width=1)


class TemplateFrame:
    """Frame with variable substitution support."""
    
    def __init__(
        self,
        width: int,
        height: int,
        template: str = "",
        variables: Optional[Dict[str, Any]] = None,
        font_path: Optional[str] = None,
        font_size: int = 12,
        text_color: Tuple[int, int, int] = (255, 255, 255),
        bg_color: Tuple[int, int, int] = (0, 0, 0)
    ):
        """Initialize template frame.
        
        Args:
            width: Frame width
            height: Frame height
            template: Template string with {variable} placeholders
            variables: Dictionary of variable values
            font_path: Path to font file
            font_size: Font size
            text_color: Text color
            bg_color: Background color
        """
        self.width = width
        self.height = height
        self.template = template
        self.variables = variables or {}
        self.font_path = font_path
        self.font_size = font_size
        self.text_color = text_color
        self.bg_color = bg_color
        
        self._variable_sources: Dict[str, Callable[[], Any]] = {}
    
    def set_variable(self, name: str, value: Any) -> None:
        """Set a variable value.
        
        Args:
            name: Variable name
            value: Variable value
        """
        self.variables[name] = value
    
    def set_variable_source(self, name: str, source: Callable[[], Any]) -> None:
        """Set a dynamic variable source.
        
        Args:
            name: Variable name
            source: Function that returns variable value
        """
        self._variable_sources[name] = source
    
    def _update_variables(self) -> None:
        """Update variables from sources."""
        for name, source in self._variable_sources.items():
            try:
                self.variables[name] = source()
            except Exception as e:
                logger.error(f"Error getting variable '{name}': {e}")
    
    def _render_template(self) -> str:
        """Render template with variable substitution.
        
        Returns:
            Rendered string
        """
        self._update_variables()
        
        result = self.template
        for name, value in self.variables.items():
            placeholder = "{" + name + "}"
            result = result.replace(placeholder, str(value))
        
        return result
    
    def render(self) -> Image.Image:
        """Render the template frame.
        
        Returns:
            PIL Image with rendered template
        """
        image = Image.new('RGB', (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        
        try:
            if self.font_path:
                font = ImageFont.truetype(self.font_path, self.font_size)
            else:
                font = ImageFont.load_default()
        except Exception:
            font = ImageFont.load_default()
        
        text = self._render_template()
        
        # Center text
        bbox = draw.textbbox((0, 0), text, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        
        x = (self.width - text_width) // 2
        y = (self.height - text_height) // 2
        
        draw.text((x, y), text, fill=self.text_color, font=font)
        
        return image


class ConditionalFrame:
    """Frame that changes based on conditions."""
    
    def __init__(
        self,
        width: int,
        height: int,
        conditions: Optional[List[Dict[str, Any]]] = None,
        default_content: Optional[Dict[str, Any]] = None
    ):
        """Initialize conditional frame.
        
        Args:
            width: Frame width
            height: Frame height
            conditions: List of condition dictionaries with 'check' and 'content'
            default_content: Default content if no conditions match
        """
        self.width = width
        self.height = height
        self.conditions = conditions or []
        self.default_content = default_content or {'text': 'No condition matched'}
        
        self._condition_checks: Dict[str, Callable[[], bool]] = {}
    
    def add_condition(
        self,
        name: str,
        check: Callable[[], bool],
        content: Dict[str, Any]
    ) -> None:
        """Add a condition.
        
        Args:
            name: Condition name
            check: Function returning True if condition is met
            content: Content to display if condition is met
        """
        self._condition_checks[name] = check
        self.conditions.append({'name': name, 'content': content})
    
    def _get_active_content(self) -> Dict[str, Any]:
        """Get content based on conditions.
        
        Returns:
            Content dictionary for active condition
        """
        for cond in self.conditions:
            name = cond.get('name', '')
            if name in self._condition_checks:
                try:
                    if self._condition_checks[name]():
                        return cond['content']
                except Exception as e:
                    logger.error(f"Error checking condition '{name}': {e}")
        
        return self.default_content
    
    def render(self) -> Image.Image:
        """Render based on active condition.
        
        Returns:
            PIL Image with conditional content
        """
        content = self._get_active_content()
        
        image = Image.new('RGB', (self.width, self.height), 
                         content.get('bg_color', (0, 0, 0)))
        draw = ImageDraw.Draw(image)
        
        text = content.get('text', '')
        color = content.get('text_color', (255, 255, 255))
        
        try:
            font = ImageFont.load_default()
            bbox = draw.textbbox((0, 0), text, font=font)
            text_width = bbox[2] - bbox[0]
            text_height = bbox[3] - bbox[1]
            x = (self.width - text_width) // 2
            y = (self.height - text_height) // 2
            draw.text((x, y), text, fill=color, font=font)
        except Exception:
            pass
        
        return image


class MultiPageFrame:
    """Frame with multiple pages and navigation."""
    
    def __init__(
        self,
        width: int,
        height: int,
        pages: Optional[List[Dict[str, Any]]] = None,
        auto_advance: bool = False,
        page_duration: float = 5.0
    ):
        """Initialize multi-page frame.
        
        Args:
            width: Frame width
            height: Frame height
            pages: List of page content dictionaries
            auto_advance: Whether to auto-advance pages
            page_duration: Duration per page in seconds
        """
        self.width = width
        self.height = height
        self.pages = pages or []
        self.auto_advance = auto_advance
        self.page_duration = page_duration
        
        self.current_page = 0
        self._last_advance = time.perf_counter()
    
    def add_page(self, content: Dict[str, Any]) -> None:
        """Add a page.
        
        Args:
            content: Page content dictionary
        """
        self.pages.append(content)
    
    def next_page(self) -> None:
        """Go to next page."""
        if self.pages:
            self.current_page = (self.current_page + 1) % len(self.pages)
            self._last_advance = time.perf_counter()
    
    def prev_page(self) -> None:
        """Go to previous page."""
        if self.pages:
            self.current_page = (self.current_page - 1) % len(self.pages)
            self._last_advance = time.perf_counter()
    
    def go_to_page(self, index: int) -> None:
        """Go to specific page.
        
        Args:
            index: Page index
        """
        if 0 <= index < len(self.pages):
            self.current_page = index
            self._last_advance = time.perf_counter()
    
    def update(self) -> None:
        """Update page if auto-advancing."""
        if self.auto_advance:
            now = time.perf_counter()
            if now - self._last_advance >= self.page_duration:
                self.next_page()
    
    def render(self) -> Image.Image:
        """Render current page.
        
        Returns:
            PIL Image with current page
        """
        self.update()
        
        if not self.pages:
            image = Image.new('RGB', (self.width, self.height), (0, 0, 0))
            return image
        
        page = self.pages[self.current_page]
        
        image = Image.new('RGB', (self.width, self.height), 
                         page.get('bg_color', (0, 0, 0)))
        draw = ImageDraw.Draw(image)
        
        text = page.get('text', '')
        color = page.get('text_color', (255, 255, 255))
        
        try:
            font = ImageFont.load_default()
            bbox = draw.textbbox((0, 0), text, font=font)
            text_width = bbox[2] - bbox[0]
            text_height = bbox[3] - bbox[1]
            x = (self.width - text_width) // 2
            y = (self.height - text_height) // 2
            draw.text((x, y), text, fill=color, font=font)
        except Exception:
            pass
        
        # Draw page indicator
        if len(self.pages) > 1:
            indicator_y = self.height - 3
            dot_spacing = 4
            total_width = len(self.pages) * dot_spacing
            start_x = (self.width - total_width) // 2
            
            for i in range(len(self.pages)):
                dot_x = start_x + i * dot_spacing
                dot_color = (255, 255, 255) if i == self.current_page else (80, 80, 80)
                draw.ellipse([dot_x, indicator_y, dot_x + 2, indicator_y + 2], 
                           fill=dot_color)
        
        return image


class ScrollingFrame:
    """Frame with scrolling content."""
    
    def __init__(
        self,
        width: int,
        height: int,
        content_height: int = 0,
        scroll_speed: float = 20.0,
        direction: str = "vertical",  # vertical or horizontal
        loop: bool = True
    ):
        """Initialize scrolling frame.
        
        Args:
            width: Frame width
            height: Frame height
            content_height: Total content height (for vertical scroll)
            scroll_speed: Scroll speed in pixels per second
            direction: Scroll direction
            loop: Whether to loop content
        """
        self.width = width
        self.height = height
        self.content_height = content_height
        self.scroll_speed = scroll_speed
        self.direction = direction
        self.loop = loop
        
        self.scroll_position = 0.0
        self._content_image: Optional[Image.Image] = None
        self._last_update = time.perf_counter()
    
    def set_content(self, content_image: Image.Image) -> None:
        """Set content image to scroll.
        
        Args:
            content_image: PIL Image with full content
        """
        self._content_image = content_image
        if self.direction == "vertical":
            self.content_height = content_image.height
        else:
            self.content_height = content_image.width
    
    def update(self, dt: Optional[float] = None) -> None:
        """Update scroll position.
        
        Args:
            dt: Delta time (auto-calculated if None)
        """
        now = time.perf_counter()
        if dt is None:
            dt = now - self._last_update
        self._last_update = now
        
        self.scroll_position += self.scroll_speed * dt
        
        if self.loop:
            max_scroll = (self.content_height if self.direction == "vertical" 
                         else self.content_height)
            if self.scroll_position >= max_scroll:
                self.scroll_position = 0
        else:
            max_scroll = max(0, self.content_height - 
                           (self.height if self.direction == "vertical" else self.width))
            self.scroll_position = min(self.scroll_position, max_scroll)
    
    def render(self) -> Image.Image:
        """Render scrolling frame.
        
        Returns:
            PIL Image with visible portion of content
        """
        self.update()
        
        if self._content_image is None:
            return Image.new('RGB', (self.width, self.height), (0, 0, 0))
        
        result = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        
        offset = int(self.scroll_position)
        
        if self.direction == "vertical":
            # Vertical scrolling
            crop_box = (0, offset, self.width, offset + self.height)
            
            if self.loop and offset + self.height > self._content_image.height:
                # Need to wrap around
                first_part_height = self._content_image.height - offset
                first_part = self._content_image.crop(
                    (0, offset, self.width, self._content_image.height)
                )
                result.paste(first_part, (0, 0))
                
                remaining = self.height - first_part_height
                if remaining > 0:
                    second_part = self._content_image.crop(
                        (0, 0, self.width, remaining)
                    )
                    result.paste(second_part, (0, first_part_height))
            else:
                cropped = self._content_image.crop(crop_box)
                result.paste(cropped, (0, 0))
        else:
            # Horizontal scrolling
            crop_box = (offset, 0, offset + self.width, self.height)
            
            if self.loop and offset + self.width > self._content_image.width:
                first_part_width = self._content_image.width - offset
                first_part = self._content_image.crop(
                    (offset, 0, self._content_image.width, self.height)
                )
                result.paste(first_part, (0, 0))
                
                remaining = self.width - first_part_width
                if remaining > 0:
                    second_part = self._content_image.crop(
                        (0, 0, remaining, self.height)
                    )
                    result.paste(second_part, (first_part_width, 0))
            else:
                cropped = self._content_image.crop(crop_box)
                result.paste(cropped, (0, 0))
        
        return result


class AnimatedFrame:
    """Frame with built-in animation support."""
    
    def __init__(
        self,
        width: int,
        height: int,
        elements: Optional[List[FrameElement]] = None,
        bg_color: Tuple[int, int, int] = (0, 0, 0),
        background_effect: Optional[BackgroundType] = None
    ):
        """Initialize animated frame.
        
        Args:
            width: Frame width
            height: Frame height
            elements: List of frame elements
            bg_color: Background color
            background_effect: Optional background effect type
        """
        self.width = width
        self.height = height
        self.elements = elements or []
        self.bg_color = bg_color
        
        self.animation_manager = AnimationManager()
        self.effects_manager = EffectsManager(width, height)
        
        if background_effect:
            self.effects_manager.set_background(background_effect)
        
        self._element_animations: Dict[str, Dict[str, Animation]] = {}
    
    def add_element(self, element: FrameElement) -> None:
        """Add an element to the frame.
        
        Args:
            element: Frame element to add
        """
        self.elements.append(element)
        self.elements.sort(key=lambda e: e.layer)
    
    def remove_element(self, element_id: str) -> bool:
        """Remove an element by ID.
        
        Args:
            element_id: Element ID to remove
            
        Returns:
            True if element was removed
        """
        for i, elem in enumerate(self.elements):
            if elem.id == element_id:
                self.elements.pop(i)
                if element_id in self._element_animations:
                    del self._element_animations[element_id]
                return True
        return False
    
    def animate_element(
        self,
        element_id: str,
        property_name: str,
        animation: Animation
    ) -> None:
        """Add animation to an element property.
        
        Args:
            element_id: Element to animate
            property_name: Property to animate (x, y, opacity, scale, etc.)
            animation: Animation to apply
        """
        if element_id not in self._element_animations:
            self._element_animations[element_id] = {}
        
        self._element_animations[element_id][property_name] = animation
        animation.start()
    
    def add_preset_animation(
        self,
        element_id: str,
        preset: str,
        **kwargs
    ) -> None:
        """Add a preset animation to an element.
        
        Args:
            element_id: Element to animate
            preset: Preset name (fade_in, fade_out, slide_in_left, etc.)
            **kwargs: Additional preset parameters
        """
        preset_map = {
            'fade_in': lambda: AnimationPreset.fade_in(kwargs.get('duration', 0.5)),
            'fade_out': lambda: AnimationPreset.fade_out(kwargs.get('duration', 0.5)),
            'slide_in_left': lambda: AnimationPreset.slide_in_left(
                self.width, kwargs.get('duration', 0.5)
            ),
            'slide_in_right': lambda: AnimationPreset.slide_in_right(
                self.width, kwargs.get('duration', 0.5)
            ),
            'slide_in_top': lambda: AnimationPreset.slide_in_top(
                self.height, kwargs.get('duration', 0.5)
            ),
            'slide_in_bottom': lambda: AnimationPreset.slide_in_bottom(
                self.height, kwargs.get('duration', 0.5)
            ),
            'pulse': lambda: AnimationPreset.pulse(kwargs.get('duration', 1.0)),
            'bounce': lambda: AnimationPreset.bounce(kwargs.get('duration', 0.6)),
            'shake': lambda: AnimationPreset.shake(kwargs.get('duration', 0.5)),
            'breathe': lambda: AnimationPreset.breathe(kwargs.get('duration', 2.0)),
        }
        
        if preset in preset_map:
            anim = preset_map[preset]()
            
            # Determine property based on preset
            if preset in ('fade_in', 'fade_out', 'breathe'):
                prop = 'opacity'
            elif preset in ('slide_in_left', 'slide_in_right'):
                prop = 'x_offset'
            elif preset in ('slide_in_top', 'slide_in_bottom'):
                prop = 'y_offset'
            elif preset == 'pulse':
                prop = 'scale'
            elif preset in ('bounce', 'shake'):
                prop = 'y_offset'
            else:
                prop = 'opacity'
            
            self.animate_element(element_id, prop, anim)
    
    def update(self, dt: Optional[float] = None) -> None:
        """Update all animations.
        
        Args:
            dt: Delta time
        """
        self.animation_manager.update(dt)
        self.effects_manager.update(dt)
    
    def render(self) -> Image.Image:
        """Render animated frame.
        
        Returns:
            PIL Image with all elements and effects
        """
        self.update()
        
        # Start with background
        image = self.effects_manager.render()
        draw = ImageDraw.Draw(image)
        
        # Render elements in layer order
        for element in self.elements:
            if not element.visible:
                continue
            
            # Get animated properties
            x = element.x
            y = element.y
            opacity = 1.0
            scale = 1.0
            
            if element.id in self._element_animations:
                anims = self._element_animations[element.id]
                if 'x_offset' in anims:
                    offset = anims['x_offset'].get_value()
                    if offset is not None:
                        x += int(offset)
                if 'y_offset' in anims:
                    offset = anims['y_offset'].get_value()
                    if offset is not None:
                        y += int(offset)
                if 'opacity' in anims:
                    op = anims['opacity'].get_value()
                    if op is not None:
                        opacity = op
                if 'scale' in anims:
                    sc = anims['scale'].get_value()
                    if sc is not None:
                        scale = sc
            
            # Render element based on type
            self._render_element(draw, element, x, y, opacity, scale)
        
        return image
    
    def _render_element(
        self,
        draw: ImageDraw.ImageDraw,
        element: FrameElement,
        x: int,
        y: int,
        opacity: float,
        scale: float
    ) -> None:
        """Render a single element.
        
        Args:
            draw: ImageDraw object
            element: Element to render
            x: X position
            y: Y position
            opacity: Element opacity
            scale: Element scale
        """
        props = element.properties
        
        if element.type == "text":
            text = props.get('text', '')
            color = props.get('color', (255, 255, 255))
            
            # Apply opacity
            color = tuple(int(c * opacity) for c in color)
            
            try:
                font_path = props.get('font_path')
                font_size = int(props.get('font_size', 12) * scale)
                if font_path:
                    font = ImageFont.truetype(font_path, font_size)
                else:
                    font = ImageFont.load_default()
            except Exception:
                font = ImageFont.load_default()
            
            draw.text((x, y), text, fill=color, font=font)
        
        elif element.type == "rectangle":
            color = props.get('color', (255, 255, 255))
            color = tuple(int(c * opacity) for c in color)
            width = int(element.width * scale)
            height = int(element.height * scale)
            fill = props.get('fill', True)
            
            if fill:
                draw.rectangle([x, y, x + width, y + height], fill=color)
            else:
                draw.rectangle([x, y, x + width, y + height], outline=color)
        
        elif element.type == "circle":
            color = props.get('color', (255, 255, 255))
            color = tuple(int(c * opacity) for c in color)
            radius = int(props.get('radius', 5) * scale)
            fill = props.get('fill', True)
            
            if fill:
                draw.ellipse([x - radius, y - radius, x + radius, y + radius], 
                           fill=color)
            else:
                draw.ellipse([x - radius, y - radius, x + radius, y + radius], 
                           outline=color)
        
        elif element.type == "line":
            color = props.get('color', (255, 255, 255))
            color = tuple(int(c * opacity) for c in color)
            x2 = int(props.get('x2', x + 10) * scale)
            y2 = int(props.get('y2', y) * scale)
            width = props.get('width', 1)
            
            draw.line([(x, y), (x2, y2)], fill=color, width=width)


class FrameLibrary:
    """Library for storing and managing frame templates."""
    
    def __init__(self):
        """Initialize frame library."""
        self.frames: Dict[str, Dict[str, Any]] = {}
        self.categories: Dict[str, List[str]] = {}
    
    def add_frame(
        self,
        frame_id: str,
        frame_config: Dict[str, Any],
        category: str = "default"
    ) -> None:
        """Add a frame to the library.
        
        Args:
            frame_id: Unique frame identifier
            frame_config: Frame configuration dictionary
            category: Category for organization
        """
        self.frames[frame_id] = frame_config
        
        if category not in self.categories:
            self.categories[category] = []
        if frame_id not in self.categories[category]:
            self.categories[category].append(frame_id)
    
    def get_frame(self, frame_id: str) -> Optional[Dict[str, Any]]:
        """Get a frame by ID.
        
        Args:
            frame_id: Frame identifier
            
        Returns:
            Frame configuration or None
        """
        return self.frames.get(frame_id)
    
    def remove_frame(self, frame_id: str) -> bool:
        """Remove a frame from the library.
        
        Args:
            frame_id: Frame identifier
            
        Returns:
            True if frame was removed
        """
        if frame_id in self.frames:
            del self.frames[frame_id]
            for cat_frames in self.categories.values():
                if frame_id in cat_frames:
                    cat_frames.remove(frame_id)
            return True
        return False
    
    def get_frames_in_category(self, category: str) -> List[Dict[str, Any]]:
        """Get all frames in a category.
        
        Args:
            category: Category name
            
        Returns:
            List of frame configurations
        """
        frame_ids = self.categories.get(category, [])
        return [self.frames[fid] for fid in frame_ids if fid in self.frames]
    
    def list_categories(self) -> List[str]:
        """List all categories.
        
        Returns:
            List of category names
        """
        return list(self.categories.keys())
    
    def export_frame(self, frame_id: str) -> Optional[Dict[str, Any]]:
        """Export a frame configuration.
        
        Args:
            frame_id: Frame identifier
            
        Returns:
            Exportable frame dictionary
        """
        frame = self.get_frame(frame_id)
        if frame:
            return {
                'id': frame_id,
                'version': '1.0',
                'config': frame
            }
        return None
    
    def import_frame(
        self,
        frame_data: Dict[str, Any],
        category: str = "imported"
    ) -> Optional[str]:
        """Import a frame from exported data.
        
        Args:
            frame_data: Exported frame data
            category: Category to place imported frame
            
        Returns:
            Imported frame ID or None
        """
        if 'config' not in frame_data:
            return None
        
        frame_id = frame_data.get('id', str(uuid.uuid4())[:8])
        
        # Ensure unique ID
        if frame_id in self.frames:
            frame_id = f"{frame_id}_{str(uuid.uuid4())[:4]}"
        
        self.add_frame(frame_id, frame_data['config'], category)
        return frame_id
    
    def to_dict(self) -> Dict[str, Any]:
        """Export library to dictionary.
        
        Returns:
            Library as dictionary
        """
        return {
            'frames': self.frames,
            'categories': self.categories
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'FrameLibrary':
        """Create library from dictionary.
        
        Args:
            data: Library dictionary
            
        Returns:
            FrameLibrary instance
        """
        library = cls()
        library.frames = data.get('frames', {})
        library.categories = data.get('categories', {})
        return library


class FrameScheduler:
    """Scheduler for time-based frame activation."""
    
    def __init__(self, timezone: str = "UTC"):
        """Initialize frame scheduler.
        
        Args:
            timezone: Timezone for scheduling
        """
        self._timezone_str = timezone
        self._tz = self._get_timezone(timezone)
        self.schedules: List[Dict[str, Any]] = []
    
    def _get_timezone(self, timezone: str):
        """Get timezone object.
        
        Args:
            timezone: Timezone name
            
        Returns:
            Timezone object or None if not available
        """
        if _USE_ZONEINFO is True:
            try:
                return ZoneInfo(timezone)
            except Exception:
                return None
        elif _USE_ZONEINFO is False:
            try:
                return pytz.timezone(timezone)
            except Exception:
                return None
        return None
    
    def _get_current_time(self) -> datetime:
        """Get current time in configured timezone.
        
        Returns:
            Current datetime in timezone
        """
        if self._tz is not None:
            return datetime.now(self._tz)
        return datetime.now()
    
    def add_schedule(
        self,
        frame_id: str,
        start_time: str,
        end_time: str,
        days: Optional[List[int]] = None
    ) -> None:
        """Add a schedule entry.
        
        Args:
            frame_id: Frame to schedule
            start_time: Start time in HH:MM format
            end_time: End time in HH:MM format
            days: List of weekday numbers (0=Monday, 6=Sunday), None for all days
        """
        self.schedules.append({
            'frame_id': frame_id,
            'start_time': start_time,
            'end_time': end_time,
            'days': days
        })
    
    def remove_schedule(self, frame_id: str) -> None:
        """Remove all schedules for a frame.
        
        Args:
            frame_id: Frame identifier
        """
        self.schedules = [s for s in self.schedules if s['frame_id'] != frame_id]
    
    def get_active_frames(self) -> List[str]:
        """Get currently active scheduled frames.
        
        Returns:
            List of active frame IDs
        """
        now = self._get_current_time()
        current_time = now.strftime("%H:%M")
        current_day = now.weekday()
        
        active = []
        for schedule in self.schedules:
            # Check day
            if schedule['days'] is not None and current_day not in schedule['days']:
                continue
            
            # Check time
            start = schedule['start_time']
            end = schedule['end_time']
            
            # Handle overnight schedules
            if start <= end:
                if start <= current_time <= end:
                    active.append(schedule['frame_id'])
            else:
                if current_time >= start or current_time <= end:
                    active.append(schedule['frame_id'])
        
        return active
    
    def is_frame_active(self, frame_id: str) -> bool:
        """Check if a frame is currently scheduled as active.
        
        Args:
            frame_id: Frame identifier
            
        Returns:
            True if frame is active
        """
        return frame_id in self.get_active_frames()


class FrameGroup:
    """Group of related frames."""
    
    def __init__(
        self,
        name: str,
        frame_ids: Optional[List[str]] = None
    ):
        """Initialize frame group.
        
        Args:
            name: Group name
            frame_ids: List of frame IDs in this group
        """
        self.name = name
        self.frame_ids = frame_ids or []
        self.enabled = True
    
    def add_frame(self, frame_id: str) -> None:
        """Add a frame to the group.
        
        Args:
            frame_id: Frame identifier
        """
        if frame_id not in self.frame_ids:
            self.frame_ids.append(frame_id)
    
    def remove_frame(self, frame_id: str) -> None:
        """Remove a frame from the group.
        
        Args:
            frame_id: Frame identifier
        """
        if frame_id in self.frame_ids:
            self.frame_ids.remove(frame_id)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert group to dictionary.
        
        Returns:
            Group as dictionary
        """
        return {
            'name': self.name,
            'frame_ids': self.frame_ids,
            'enabled': self.enabled
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'FrameGroup':
        """Create group from dictionary.
        
        Args:
            data: Group dictionary
            
        Returns:
            FrameGroup instance
        """
        group = cls(
            name=data.get('name', 'Unnamed'),
            frame_ids=data.get('frame_ids', [])
        )
        group.enabled = data.get('enabled', True)
        return group
