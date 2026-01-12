"""Interactive elements for Lumigrid display system.

This module provides interactive UI elements including:
- Button elements that respond to input
- Slider controls for adjusting settings
- Menu systems for navigation
- Input forms for data entry
- Navigation elements for frame browsing
- Feedback systems for user interaction confirmation
"""

import logging
import time
import uuid
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple

from PIL import Image, ImageDraw, ImageFont

logger = logging.getLogger(__name__)


class InputType(Enum):
    """Types of input methods supported."""
    TOUCH = "touch"
    PROXIMITY = "proximity"
    BUTTON = "button"
    WEB = "web"
    MOTION = "motion"


class ElementState(Enum):
    """States for interactive elements."""
    NORMAL = "normal"
    HOVER = "hover"
    PRESSED = "pressed"
    DISABLED = "disabled"
    FOCUSED = "focused"


@dataclass
class InputEvent:
    """Represents an input event from any input source.
    
    Attributes:
        type: Type of input (touch, button, etc.)
        action: Action performed (press, release, move, etc.)
        x: X coordinate (for positional inputs)
        y: Y coordinate (for positional inputs)
        value: Additional value (button ID, gesture type, etc.)
        timestamp: Event timestamp
    """
    type: InputType
    action: str
    x: Optional[float] = None
    y: Optional[float] = None
    value: Any = None
    timestamp: float = field(default_factory=time.time)


@dataclass
class ElementStyle:
    """Style configuration for interactive elements.
    
    Attributes:
        bg_color: Background color
        fg_color: Foreground/text color
        border_color: Border color
        hover_bg_color: Background color on hover
        pressed_bg_color: Background color when pressed
        disabled_bg_color: Background color when disabled
        border_width: Border width in pixels
        corner_radius: Corner radius for rounded elements
        font_size: Text font size
        padding: Internal padding
    """
    bg_color: Tuple[int, int, int] = (50, 50, 50)
    fg_color: Tuple[int, int, int] = (255, 255, 255)
    border_color: Tuple[int, int, int] = (100, 100, 100)
    hover_bg_color: Tuple[int, int, int] = (70, 70, 70)
    pressed_bg_color: Tuple[int, int, int] = (30, 30, 30)
    disabled_bg_color: Tuple[int, int, int] = (40, 40, 40)
    disabled_fg_color: Tuple[int, int, int] = (100, 100, 100)
    border_width: int = 1
    corner_radius: int = 0
    font_size: int = 10
    padding: int = 2


class InteractiveElement:
    """Base class for interactive UI elements."""
    
    def __init__(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        element_id: Optional[str] = None,
        style: Optional[ElementStyle] = None,
        enabled: bool = True
    ):
        """Initialize interactive element.
        
        Args:
            x: X position
            y: Y position
            width: Element width
            height: Element height
            element_id: Unique identifier
            style: Visual style
            enabled: Whether element is interactive
        """
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.id = element_id or str(uuid.uuid4())[:8]
        self.style = style or ElementStyle()
        self.enabled = enabled
        self.state = ElementState.NORMAL
        self.visible = True
        
        self._callbacks: Dict[str, List[Callable]] = {}
    
    def contains_point(self, px: float, py: float) -> bool:
        """Check if a point is within the element bounds.
        
        Args:
            px: Point X coordinate
            py: Point Y coordinate
            
        Returns:
            True if point is within bounds
        """
        return (self.x <= px < self.x + self.width and
                self.y <= py < self.y + self.height)
    
    def on(self, event_name: str, callback: Callable) -> None:
        """Register an event callback.
        
        Args:
            event_name: Event to listen for (click, change, etc.)
            callback: Function to call when event occurs
        """
        if event_name not in self._callbacks:
            self._callbacks[event_name] = []
        self._callbacks[event_name].append(callback)
    
    def off(self, event_name: str, callback: Optional[Callable] = None) -> None:
        """Remove an event callback.
        
        Args:
            event_name: Event name
            callback: Specific callback to remove, or None for all
        """
        if event_name in self._callbacks:
            if callback is None:
                self._callbacks[event_name] = []
            elif callback in self._callbacks[event_name]:
                self._callbacks[event_name].remove(callback)
    
    def _emit(self, event_name: str, **kwargs) -> None:
        """Emit an event to registered callbacks.
        
        Args:
            event_name: Event name
            **kwargs: Event data
        """
        if event_name in self._callbacks:
            for callback in self._callbacks[event_name]:
                try:
                    callback(self, **kwargs)
                except Exception as e:
                    logger.error(f"Error in callback for {event_name}: {e}")
    
    def handle_input(self, event: InputEvent) -> bool:
        """Handle an input event.
        
        Args:
            event: Input event to handle
            
        Returns:
            True if event was handled
        """
        if not self.enabled or not self.visible:
            return False
        
        # Override in subclasses
        return False
    
    def set_state(self, state: ElementState) -> None:
        """Set element state.
        
        Args:
            state: New state
        """
        if not self.enabled and state != ElementState.DISABLED:
            return
        old_state = self.state
        self.state = state
        if old_state != state:
            self._emit('state_change', old_state=old_state, new_state=state)
    
    def render(self, image: Image.Image) -> None:
        """Render element to an image.
        
        Args:
            image: PIL Image to draw on
        """
        if not self.visible:
            return
        
        # Override in subclasses
        pass
    
    def get_bounds(self) -> Tuple[int, int, int, int]:
        """Get element bounds.
        
        Returns:
            Tuple of (x, y, width, height)
        """
        return (self.x, self.y, self.width, self.height)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert element to dictionary.
        
        Returns:
            Element configuration dictionary
        """
        return {
            'id': self.id,
            'type': self.__class__.__name__,
            'x': self.x,
            'y': self.y,
            'width': self.width,
            'height': self.height,
            'enabled': self.enabled,
            'visible': self.visible
        }


class Button(InteractiveElement):
    """Button element with click handling."""
    
    def __init__(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        text: str = "",
        icon: Optional[str] = None,
        **kwargs
    ):
        """Initialize button.
        
        Args:
            x: X position
            y: Y position
            width: Button width
            height: Button height
            text: Button label text
            icon: Optional icon identifier
            **kwargs: Additional InteractiveElement args
        """
        super().__init__(x, y, width, height, **kwargs)
        self.text = text
        self.icon = icon
        self._press_start = 0
    
    def handle_input(self, event: InputEvent) -> bool:
        """Handle button input."""
        if not self.enabled or not self.visible:
            return False
        
        if event.x is None or event.y is None:
            return False
        
        if not self.contains_point(event.x, event.y):
            if self.state in (ElementState.HOVER, ElementState.PRESSED):
                self.set_state(ElementState.NORMAL)
            return False
        
        if event.action == "move":
            if self.state == ElementState.NORMAL:
                self.set_state(ElementState.HOVER)
            return True
        
        elif event.action == "press":
            self.set_state(ElementState.PRESSED)
            self._press_start = event.timestamp
            self._emit('press')
            return True
        
        elif event.action == "release":
            if self.state == ElementState.PRESSED:
                self.set_state(ElementState.HOVER)
                self._emit('click')
                self._emit('release')
            return True
        
        return False
    
    def render(self, image: Image.Image) -> None:
        """Render button."""
        if not self.visible:
            return
        
        draw = ImageDraw.Draw(image)
        
        # Determine colors based on state
        if not self.enabled:
            bg = self.style.disabled_bg_color
            fg = self.style.disabled_fg_color
        elif self.state == ElementState.PRESSED:
            bg = self.style.pressed_bg_color
            fg = self.style.fg_color
        elif self.state == ElementState.HOVER:
            bg = self.style.hover_bg_color
            fg = self.style.fg_color
        else:
            bg = self.style.bg_color
            fg = self.style.fg_color
        
        # Draw background
        draw.rectangle(
            [self.x, self.y, self.x + self.width - 1, self.y + self.height - 1],
            fill=bg,
            outline=self.style.border_color,
            width=self.style.border_width
        )
        
        # Draw text
        if self.text:
            try:
                font = ImageFont.load_default()
                bbox = draw.textbbox((0, 0), self.text, font=font)
                text_width = bbox[2] - bbox[0]
                text_height = bbox[3] - bbox[1]
                
                tx = self.x + (self.width - text_width) // 2
                ty = self.y + (self.height - text_height) // 2
                
                draw.text((tx, ty), self.text, fill=fg, font=font)
            except Exception:
                pass
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert button to dictionary."""
        data = super().to_dict()
        data['text'] = self.text
        data['icon'] = self.icon
        return data


class Slider(InteractiveElement):
    """Slider control for adjusting values."""
    
    def __init__(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        min_value: float = 0,
        max_value: float = 100,
        value: float = 50,
        step: float = 1,
        orientation: str = "horizontal",
        **kwargs
    ):
        """Initialize slider.
        
        Args:
            x: X position
            y: Y position
            width: Slider width
            height: Slider height
            min_value: Minimum value
            max_value: Maximum value
            value: Current value
            step: Value step increment
            orientation: "horizontal" or "vertical"
            **kwargs: Additional InteractiveElement args
        """
        super().__init__(x, y, width, height, **kwargs)
        self.min_value = min_value
        self.max_value = max_value
        self._value = max(min_value, min(max_value, value))
        self.step = step
        self.orientation = orientation
        self._dragging = False
    
    @property
    def value(self) -> float:
        """Get current value."""
        return self._value
    
    @value.setter
    def value(self, val: float) -> None:
        """Set current value."""
        old_value = self._value
        # Snap to step
        steps = round((val - self.min_value) / self.step) if self.step else 0
        self._value = self.min_value + steps * self.step
        self._value = max(self.min_value, min(self.max_value, self._value))
        
        if old_value != self._value:
            self._emit('change', value=self._value)
    
    def get_normalized_value(self) -> float:
        """Get value normalized to 0-1 range.
        
        Returns:
            Normalized value
        """
        range_val = self.max_value - self.min_value
        if range_val == 0:
            return 0
        return (self._value - self.min_value) / range_val
    
    def _value_from_position(self, px: float, py: float) -> float:
        """Calculate value from pointer position.
        
        Args:
            px: Pointer X
            py: Pointer Y
            
        Returns:
            Calculated value
        """
        if self.orientation == "horizontal":
            ratio = (px - self.x) / self.width
        else:
            ratio = 1 - (py - self.y) / self.height
        
        ratio = max(0, min(1, ratio))
        return self.min_value + ratio * (self.max_value - self.min_value)
    
    def handle_input(self, event: InputEvent) -> bool:
        """Handle slider input."""
        if not self.enabled or not self.visible:
            return False
        
        if event.x is None or event.y is None:
            return False
        
        if event.action == "press" and self.contains_point(event.x, event.y):
            self._dragging = True
            self.set_state(ElementState.PRESSED)
            self.value = self._value_from_position(event.x, event.y)
            return True
        
        elif event.action == "move" and self._dragging:
            self.value = self._value_from_position(event.x, event.y)
            return True
        
        elif event.action == "release":
            if self._dragging:
                self._dragging = False
                self.set_state(ElementState.NORMAL)
                return True
        
        elif event.action == "move" and self.contains_point(event.x, event.y):
            if not self._dragging:
                self.set_state(ElementState.HOVER)
            return True
        
        elif self.state == ElementState.HOVER and not self._dragging:
            self.set_state(ElementState.NORMAL)
        
        return False
    
    def render(self, image: Image.Image) -> None:
        """Render slider."""
        if not self.visible:
            return
        
        draw = ImageDraw.Draw(image)
        
        # Draw track
        track_color = self.style.border_color
        if self.orientation == "horizontal":
            track_y = self.y + self.height // 2
            draw.line(
                [(self.x, track_y), (self.x + self.width, track_y)],
                fill=track_color, width=2
            )
        else:
            track_x = self.x + self.width // 2
            draw.line(
                [(track_x, self.y), (track_x, self.y + self.height)],
                fill=track_color, width=2
            )
        
        # Draw filled portion
        ratio = self.get_normalized_value()
        fill_color = self.style.fg_color
        
        if self.orientation == "horizontal":
            fill_end = self.x + int(self.width * ratio)
            track_y = self.y + self.height // 2
            draw.line(
                [(self.x, track_y), (fill_end, track_y)],
                fill=fill_color, width=2
            )
            # Draw handle
            handle_x = fill_end
            handle_y = track_y
        else:
            fill_start = self.y + int(self.height * (1 - ratio))
            track_x = self.x + self.width // 2
            draw.line(
                [(track_x, self.y + self.height), (track_x, fill_start)],
                fill=fill_color, width=2
            )
            handle_x = track_x
            handle_y = fill_start
        
        # Draw handle
        handle_radius = min(self.width, self.height) // 3
        handle_color = self.style.hover_bg_color if self._dragging else self.style.bg_color
        draw.ellipse(
            [handle_x - handle_radius, handle_y - handle_radius,
             handle_x + handle_radius, handle_y + handle_radius],
            fill=handle_color, outline=fill_color
        )
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert slider to dictionary."""
        data = super().to_dict()
        data['min_value'] = self.min_value
        data['max_value'] = self.max_value
        data['value'] = self._value
        data['step'] = self.step
        data['orientation'] = self.orientation
        return data


class MenuItem:
    """Item in a menu."""
    
    def __init__(
        self,
        text: str,
        item_id: Optional[str] = None,
        icon: Optional[str] = None,
        enabled: bool = True,
        action: Optional[Callable] = None,
        submenu: Optional[List['MenuItem']] = None
    ):
        """Initialize menu item.
        
        Args:
            text: Item label text
            item_id: Unique identifier
            icon: Optional icon identifier
            enabled: Whether item is selectable
            action: Callback when item is selected
            submenu: Nested submenu items
        """
        self.id = item_id or str(uuid.uuid4())[:8]
        self.text = text
        self.icon = icon
        self.enabled = enabled
        self.action = action
        self.submenu = submenu or []
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            'id': self.id,
            'text': self.text,
            'icon': self.icon,
            'enabled': self.enabled,
            'submenu': [item.to_dict() for item in self.submenu]
        }


class Menu(InteractiveElement):
    """Menu system for navigation."""
    
    def __init__(
        self,
        x: int,
        y: int,
        width: int,
        items: Optional[List[MenuItem]] = None,
        item_height: int = 12,
        **kwargs
    ):
        """Initialize menu.
        
        Args:
            x: X position
            y: Y position
            width: Menu width
            items: Menu items
            item_height: Height of each menu item
            **kwargs: Additional InteractiveElement args
        """
        height = item_height * len(items or [])
        super().__init__(x, y, width, max(height, item_height), **kwargs)
        self.items = items or []
        self.item_height = item_height
        self.selected_index = 0
        self._hover_index = -1
    
    def add_item(self, item: MenuItem) -> None:
        """Add a menu item.
        
        Args:
            item: Menu item to add
        """
        self.items.append(item)
        self.height = self.item_height * len(self.items)
    
    def remove_item(self, item_id: str) -> bool:
        """Remove a menu item by ID.
        
        Args:
            item_id: Item identifier
            
        Returns:
            True if item was removed
        """
        for i, item in enumerate(self.items):
            if item.id == item_id:
                self.items.pop(i)
                self.height = self.item_height * len(self.items)
                return True
        return False
    
    def get_item_at_position(self, py: float) -> int:
        """Get item index at Y position.
        
        Args:
            py: Y coordinate
            
        Returns:
            Item index or -1 if none
        """
        if py < self.y or py >= self.y + self.height:
            return -1
        return int((py - self.y) / self.item_height)
    
    def select_next(self) -> None:
        """Select next enabled item."""
        start = self.selected_index
        while True:
            self.selected_index = (self.selected_index + 1) % len(self.items)
            if self.items[self.selected_index].enabled:
                break
            if self.selected_index == start:
                break
        self._emit('selection_change', index=self.selected_index)
    
    def select_previous(self) -> None:
        """Select previous enabled item."""
        start = self.selected_index
        while True:
            self.selected_index = (self.selected_index - 1) % len(self.items)
            if self.items[self.selected_index].enabled:
                break
            if self.selected_index == start:
                break
        self._emit('selection_change', index=self.selected_index)
    
    def activate_selected(self) -> None:
        """Activate the currently selected item."""
        if not self.items:
            return
        
        item = self.items[self.selected_index]
        if item.enabled and item.action:
            item.action()
            self._emit('item_activate', item=item)
    
    def handle_input(self, event: InputEvent) -> bool:
        """Handle menu input."""
        if not self.enabled or not self.visible:
            return False
        
        # Handle button navigation
        if event.type == InputType.BUTTON:
            if event.value == "up":
                self.select_previous()
                return True
            elif event.value == "down":
                self.select_next()
                return True
            elif event.value == "select":
                self.activate_selected()
                return True
        
        # Handle positional input
        if event.x is not None and event.y is not None:
            if not self.contains_point(event.x, event.y):
                self._hover_index = -1
                return False
            
            index = self.get_item_at_position(event.y)
            
            if event.action == "move":
                self._hover_index = index
                return True
            
            elif event.action == "press":
                if 0 <= index < len(self.items):
                    self.selected_index = index
                    self._emit('selection_change', index=index)
                return True
            
            elif event.action == "release":
                if 0 <= index < len(self.items):
                    self.selected_index = index
                    self.activate_selected()
                return True
        
        return False
    
    def render(self, image: Image.Image) -> None:
        """Render menu."""
        if not self.visible:
            return
        
        draw = ImageDraw.Draw(image)
        
        # Draw background
        draw.rectangle(
            [self.x, self.y, self.x + self.width - 1, self.y + self.height - 1],
            fill=self.style.bg_color,
            outline=self.style.border_color
        )
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        # Draw items
        for i, item in enumerate(self.items):
            item_y = self.y + i * self.item_height
            
            # Determine item colors
            if not item.enabled:
                bg = self.style.disabled_bg_color
                fg = self.style.disabled_fg_color
            elif i == self.selected_index:
                bg = self.style.hover_bg_color
                fg = self.style.fg_color
            elif i == self._hover_index:
                bg = self.style.pressed_bg_color
                fg = self.style.fg_color
            else:
                bg = self.style.bg_color
                fg = self.style.fg_color
            
            # Draw item background
            draw.rectangle(
                [self.x + 1, item_y, self.x + self.width - 2, item_y + self.item_height - 1],
                fill=bg
            )
            
            # Draw item text
            if font:
                text_y = item_y + (self.item_height - 8) // 2
                draw.text(
                    (self.x + self.style.padding + 2, text_y),
                    item.text, fill=fg, font=font
                )
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert menu to dictionary."""
        data = super().to_dict()
        data['items'] = [item.to_dict() for item in self.items]
        data['item_height'] = self.item_height
        data['selected_index'] = self.selected_index
        return data


class TextInput(InteractiveElement):
    """Text input field for data entry."""
    
    def __init__(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        text: str = "",
        placeholder: str = "",
        max_length: int = 100,
        **kwargs
    ):
        """Initialize text input.
        
        Args:
            x: X position
            y: Y position
            width: Field width
            height: Field height
            text: Initial text value
            placeholder: Placeholder text when empty
            max_length: Maximum text length
            **kwargs: Additional InteractiveElement args
        """
        super().__init__(x, y, width, height, **kwargs)
        self.text = text
        self.placeholder = placeholder
        self.max_length = max_length
        self.cursor_position = len(text)
        self.focused = False
        self._cursor_visible = True
        self._cursor_blink_time = 0
    
    def set_focus(self, focused: bool) -> None:
        """Set focus state.
        
        Args:
            focused: Whether field is focused
        """
        self.focused = focused
        self.state = ElementState.FOCUSED if focused else ElementState.NORMAL
        self._emit('focus' if focused else 'blur')
    
    def insert_char(self, char: str) -> None:
        """Insert a character at cursor position.
        
        Args:
            char: Character to insert
        """
        if len(self.text) >= self.max_length:
            return
        
        self.text = (self.text[:self.cursor_position] + 
                    char + 
                    self.text[self.cursor_position:])
        self.cursor_position += 1
        self._emit('change', text=self.text)
    
    def delete_char(self, forward: bool = False) -> None:
        """Delete a character.
        
        Args:
            forward: Delete forward (Del) vs backward (Backspace)
        """
        if forward:
            if self.cursor_position < len(self.text):
                self.text = (self.text[:self.cursor_position] + 
                           self.text[self.cursor_position + 1:])
        else:
            if self.cursor_position > 0:
                self.text = (self.text[:self.cursor_position - 1] + 
                           self.text[self.cursor_position:])
                self.cursor_position -= 1
        self._emit('change', text=self.text)
    
    def move_cursor(self, direction: int) -> None:
        """Move cursor position.
        
        Args:
            direction: -1 for left, 1 for right
        """
        self.cursor_position = max(0, min(len(self.text), 
                                         self.cursor_position + direction))
    
    def handle_input(self, event: InputEvent) -> bool:
        """Handle text input events."""
        if not self.enabled or not self.visible:
            return False
        
        if event.x is not None and event.y is not None:
            if event.action == "press":
                if self.contains_point(event.x, event.y):
                    self.set_focus(True)
                    return True
                else:
                    if self.focused:
                        self.set_focus(False)
        
        if not self.focused:
            return False
        
        # Handle keyboard input
        if event.type == InputType.BUTTON:
            if event.action == "press":
                value = event.value
                if value == "left":
                    self.move_cursor(-1)
                    return True
                elif value == "right":
                    self.move_cursor(1)
                    return True
                elif value == "backspace":
                    self.delete_char(forward=False)
                    return True
                elif value == "delete":
                    self.delete_char(forward=True)
                    return True
                elif value == "enter":
                    self._emit('submit', text=self.text)
                    return True
                elif isinstance(value, str) and len(value) == 1:
                    self.insert_char(value)
                    return True
        
        return False
    
    def update(self, dt: float) -> None:
        """Update cursor blink.
        
        Args:
            dt: Delta time
        """
        if self.focused:
            self._cursor_blink_time += dt
            if self._cursor_blink_time >= 0.5:
                self._cursor_visible = not self._cursor_visible
                self._cursor_blink_time = 0
    
    def render(self, image: Image.Image) -> None:
        """Render text input."""
        if not self.visible:
            return
        
        draw = ImageDraw.Draw(image)
        
        # Draw background
        bg = self.style.hover_bg_color if self.focused else self.style.bg_color
        draw.rectangle(
            [self.x, self.y, self.x + self.width - 1, self.y + self.height - 1],
            fill=bg,
            outline=self.style.fg_color if self.focused else self.style.border_color
        )
        
        try:
            font = ImageFont.load_default()
        except Exception:
            return
        
        # Draw text or placeholder
        display_text = self.text or self.placeholder
        text_color = self.style.fg_color if self.text else self.style.disabled_fg_color
        
        text_x = self.x + self.style.padding
        text_y = self.y + (self.height - 8) // 2
        
        draw.text((text_x, text_y), display_text, fill=text_color, font=font)
        
        # Draw cursor if focused
        if self.focused and self._cursor_visible:
            # Calculate cursor position
            cursor_text = self.text[:self.cursor_position]
            bbox = draw.textbbox((0, 0), cursor_text, font=font)
            cursor_x = text_x + (bbox[2] - bbox[0])
            
            draw.line(
                [(cursor_x, text_y), (cursor_x, text_y + 8)],
                fill=self.style.fg_color, width=1
            )
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        data = super().to_dict()
        data['text'] = self.text
        data['placeholder'] = self.placeholder
        data['max_length'] = self.max_length
        return data


class ProgressIndicator(InteractiveElement):
    """Visual feedback indicator for progress or status."""
    
    def __init__(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        value: float = 0,
        show_text: bool = True,
        indicator_type: str = "bar",  # bar, circle, dots
        **kwargs
    ):
        """Initialize progress indicator.
        
        Args:
            x: X position
            y: Y position
            width: Indicator width
            height: Indicator height
            value: Current progress (0-100)
            show_text: Whether to show percentage text
            indicator_type: Type of progress display
            **kwargs: Additional InteractiveElement args
        """
        super().__init__(x, y, width, height, **kwargs)
        self._value = max(0, min(100, value))
        self.show_text = show_text
        self.indicator_type = indicator_type
        self._animation_value = 0
    
    @property
    def value(self) -> float:
        """Get current value."""
        return self._value
    
    @value.setter
    def value(self, val: float) -> None:
        """Set current value."""
        old_value = self._value
        self._value = max(0, min(100, val))
        if old_value != self._value:
            self._emit('change', value=self._value)
    
    def update(self, dt: float) -> None:
        """Update animation.
        
        Args:
            dt: Delta time
        """
        # Smooth animation towards target value
        diff = self._value - self._animation_value
        self._animation_value += diff * min(1, dt * 5)
    
    def render(self, image: Image.Image) -> None:
        """Render progress indicator."""
        if not self.visible:
            return
        
        draw = ImageDraw.Draw(image)
        
        ratio = self._animation_value / 100
        
        if self.indicator_type == "bar":
            # Background
            draw.rectangle(
                [self.x, self.y, self.x + self.width - 1, self.y + self.height - 1],
                fill=self.style.bg_color,
                outline=self.style.border_color
            )
            
            # Fill
            fill_width = int((self.width - 2) * ratio)
            if fill_width > 0:
                draw.rectangle(
                    [self.x + 1, self.y + 1,
                     self.x + 1 + fill_width, self.y + self.height - 2],
                    fill=self.style.fg_color
                )
        
        elif self.indicator_type == "circle":
            cx = self.x + self.width // 2
            cy = self.y + self.height // 2
            radius = min(self.width, self.height) // 2 - 2
            
            # Background circle
            draw.ellipse(
                [cx - radius, cy - radius, cx + radius, cy + radius],
                outline=self.style.border_color
            )
            
            # Arc for progress
            if ratio > 0:
                draw.arc(
                    [cx - radius, cy - radius, cx + radius, cy + radius],
                    start=-90, end=-90 + 360 * ratio,
                    fill=self.style.fg_color, width=2
                )
        
        elif self.indicator_type == "dots":
            num_dots = 5
            dot_radius = min(self.width // (num_dots * 2), self.height // 2 - 1)
            spacing = self.width // num_dots
            active_dots = int(num_dots * ratio)
            
            for i in range(num_dots):
                dx = self.x + spacing // 2 + i * spacing
                dy = self.y + self.height // 2
                color = self.style.fg_color if i < active_dots else self.style.border_color
                draw.ellipse(
                    [dx - dot_radius, dy - dot_radius,
                     dx + dot_radius, dy + dot_radius],
                    fill=color
                )
        
        # Draw text
        if self.show_text and self.indicator_type == "bar":
            try:
                font = ImageFont.load_default()
                text = f"{int(self._animation_value)}%"
                bbox = draw.textbbox((0, 0), text, font=font)
                text_width = bbox[2] - bbox[0]
                text_x = self.x + (self.width - text_width) // 2
                text_y = self.y + (self.height - 8) // 2
                draw.text((text_x, text_y), text, 
                         fill=self.style.bg_color if ratio > 0.5 else self.style.fg_color,
                         font=font)
            except Exception:
                pass
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        data = super().to_dict()
        data['value'] = self._value
        data['show_text'] = self.show_text
        data['indicator_type'] = self.indicator_type
        return data


class InteractiveManager:
    """Manages interactive elements and input handling."""
    
    def __init__(self, width: int, height: int):
        """Initialize interactive manager.
        
        Args:
            width: Display width
            height: Display height
        """
        self.width = width
        self.height = height
        self.elements: List[InteractiveElement] = []
        self.focused_element: Optional[InteractiveElement] = None
        
        self._callbacks: Dict[str, List[Callable]] = {}
    
    def add_element(self, element: InteractiveElement) -> None:
        """Add an interactive element.
        
        Args:
            element: Element to add
        """
        self.elements.append(element)
    
    def remove_element(self, element_id: str) -> bool:
        """Remove an element by ID.
        
        Args:
            element_id: Element identifier
            
        Returns:
            True if element was removed
        """
        for i, elem in enumerate(self.elements):
            if elem.id == element_id:
                self.elements.pop(i)
                return True
        return False
    
    def get_element(self, element_id: str) -> Optional[InteractiveElement]:
        """Get element by ID.
        
        Args:
            element_id: Element identifier
            
        Returns:
            Element or None
        """
        for elem in self.elements:
            if elem.id == element_id:
                return elem
        return None
    
    def get_element_at(self, x: float, y: float) -> Optional[InteractiveElement]:
        """Get topmost element at position.
        
        Args:
            x: X coordinate
            y: Y coordinate
            
        Returns:
            Element at position or None
        """
        # Reverse order for top element first
        for elem in reversed(self.elements):
            if elem.visible and elem.contains_point(x, y):
                return elem
        return None
    
    def handle_input(self, event: InputEvent) -> bool:
        """Dispatch input event to elements.
        
        Args:
            event: Input event
            
        Returns:
            True if event was handled
        """
        # Try focused element first
        if self.focused_element:
            if self.focused_element.handle_input(event):
                return True
        
        # Try all elements
        for elem in reversed(self.elements):
            if elem != self.focused_element and elem.handle_input(event):
                return True
        
        return False
    
    def update(self, dt: float) -> None:
        """Update all elements.
        
        Args:
            dt: Delta time
        """
        for elem in self.elements:
            if hasattr(elem, 'update'):
                elem.update(dt)
    
    def render(self, image: Image.Image) -> None:
        """Render all elements to image.
        
        Args:
            image: PIL Image to draw on
        """
        for elem in self.elements:
            elem.render(image)
    
    def on(self, event_name: str, callback: Callable) -> None:
        """Register global event callback.
        
        Args:
            event_name: Event name
            callback: Callback function
        """
        if event_name not in self._callbacks:
            self._callbacks[event_name] = []
        self._callbacks[event_name].append(callback)
    
    def clear(self) -> None:
        """Remove all elements."""
        self.elements.clear()
        self.focused_element = None
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert manager state to dictionary.
        
        Returns:
            State dictionary
        """
        return {
            'width': self.width,
            'height': self.height,
            'elements': [elem.to_dict() for elem in self.elements],
            'focused_element_id': self.focused_element.id if self.focused_element else None
        }


class FeedbackEffect:
    """Visual feedback effect for user interactions."""
    
    def __init__(
        self,
        effect_type: str = "pulse",  # pulse, flash, ripple
        duration: float = 0.3,
        color: Tuple[int, int, int] = (255, 255, 255)
    ):
        """Initialize feedback effect.
        
        Args:
            effect_type: Type of effect
            duration: Effect duration in seconds
            color: Effect color
        """
        self.effect_type = effect_type
        self.duration = duration
        self.color = color
        self.active = False
        self.elapsed = 0
        self.position = (0, 0)
    
    def trigger(self, x: int, y: int) -> None:
        """Trigger the effect at position.
        
        Args:
            x: X position
            y: Y position
        """
        self.active = True
        self.elapsed = 0
        self.position = (x, y)
    
    def update(self, dt: float) -> None:
        """Update effect animation.
        
        Args:
            dt: Delta time
        """
        if self.active:
            self.elapsed += dt
            if self.elapsed >= self.duration:
                self.active = False
    
    def render(self, image: Image.Image) -> None:
        """Render effect.
        
        Args:
            image: Image to draw on
        """
        if not self.active:
            return
        
        draw = ImageDraw.Draw(image)
        progress = self.elapsed / self.duration
        
        x, y = self.position
        
        if self.effect_type == "pulse":
            # Expanding circle that fades
            max_radius = 20
            radius = int(max_radius * progress)
            alpha = 1 - progress
            color = tuple(int(c * alpha) for c in self.color)
            draw.ellipse(
                [x - radius, y - radius, x + radius, y + radius],
                outline=color
            )
        
        elif self.effect_type == "flash":
            # Quick flash at point
            if progress < 0.5:
                brightness = 1 - progress * 2
                color = tuple(int(c * brightness) for c in self.color)
                draw.ellipse(
                    [x - 3, y - 3, x + 3, y + 3],
                    fill=color
                )
        
        elif self.effect_type == "ripple":
            # Multiple expanding rings
            for i in range(3):
                ring_progress = (progress - i * 0.15) * 2
                if 0 < ring_progress < 1:
                    radius = int(30 * ring_progress)
                    alpha = 1 - ring_progress
                    color = tuple(int(c * alpha) for c in self.color)
                    draw.ellipse(
                        [x - radius, y - radius, x + radius, y + radius],
                        outline=color
                    )


class NavigationBar(InteractiveElement):
    """Navigation bar for frame browsing."""
    
    def __init__(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        items: Optional[List[str]] = None,
        **kwargs
    ):
        """Initialize navigation bar.
        
        Args:
            x: X position
            y: Y position
            width: Bar width
            height: Bar height
            items: Navigation item labels
            **kwargs: Additional InteractiveElement args
        """
        super().__init__(x, y, width, height, **kwargs)
        self.items = items or []
        self.selected_index = 0
    
    def select_item(self, index: int) -> None:
        """Select a navigation item.
        
        Args:
            index: Item index
        """
        if 0 <= index < len(self.items):
            old_index = self.selected_index
            self.selected_index = index
            if old_index != index:
                self._emit('navigate', index=index, item=self.items[index])
    
    def handle_input(self, event: InputEvent) -> bool:
        """Handle navigation input."""
        if not self.enabled or not self.visible:
            return False
        
        # Handle button navigation
        if event.type == InputType.BUTTON:
            if event.value == "left":
                self.select_item(max(0, self.selected_index - 1))
                return True
            elif event.value == "right":
                self.select_item(min(len(self.items) - 1, self.selected_index + 1))
                return True
        
        # Handle positional input
        if event.x is not None and self.contains_point(event.x, event.y or 0):
            if event.action == "press" and len(self.items) > 0:
                item_width = self.width // len(self.items)
                index = int((event.x - self.x) / item_width)
                self.select_item(index)
                return True
        
        return False
    
    def render(self, image: Image.Image) -> None:
        """Render navigation bar."""
        if not self.visible or not self.items:
            return
        
        draw = ImageDraw.Draw(image)
        item_width = self.width // len(self.items)
        
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        
        for i, item in enumerate(self.items):
            ix = self.x + i * item_width
            
            # Background
            bg = self.style.hover_bg_color if i == self.selected_index else self.style.bg_color
            draw.rectangle(
                [ix, self.y, ix + item_width - 1, self.y + self.height - 1],
                fill=bg
            )
            
            # Text
            if font:
                bbox = draw.textbbox((0, 0), item, font=font)
                text_width = bbox[2] - bbox[0]
                tx = ix + (item_width - text_width) // 2
                ty = self.y + (self.height - 8) // 2
                draw.text((tx, ty), item, fill=self.style.fg_color, font=font)
        
        # Selection indicator
        indicator_x = self.x + self.selected_index * item_width
        draw.rectangle(
            [indicator_x, self.y + self.height - 2,
             indicator_x + item_width - 1, self.y + self.height - 1],
            fill=self.style.fg_color
        )
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        data = super().to_dict()
        data['items'] = self.items
        data['selected_index'] = self.selected_index
        return data
