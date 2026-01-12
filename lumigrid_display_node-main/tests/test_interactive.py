"""Tests for the interactive elements module."""

import pytest
from unittest.mock import MagicMock, patch
from PIL import Image

from lumigrid.display.interactive import (
    InputType,
    ElementState,
    InputEvent,
    ElementStyle,
    InteractiveElement,
    Button,
    Slider,
    MenuItem,
    Menu,
    TextInput,
    ProgressIndicator,
    InteractiveManager,
    FeedbackEffect,
    NavigationBar,
)


class TestInputEvent:
    """Tests for InputEvent class."""
    
    def test_creation_minimal(self):
        """Test input event creation with minimal args."""
        event = InputEvent(type=InputType.TOUCH, action="press")
        assert event.type == InputType.TOUCH
        assert event.action == "press"
        assert event.x is None
        assert event.y is None
    
    def test_creation_full(self):
        """Test input event creation with all args."""
        event = InputEvent(
            type=InputType.TOUCH,
            action="press",
            x=10.0,
            y=20.0,
            value="custom"
        )
        assert event.x == 10.0
        assert event.y == 20.0
        assert event.value == "custom"


class TestButton:
    """Tests for Button class."""
    
    def test_creation(self):
        """Test button creation."""
        btn = Button(x=10, y=20, width=30, height=15, text="Test")
        assert btn.x == 10
        assert btn.y == 20
        assert btn.width == 30
        assert btn.height == 15
        assert btn.text == "Test"
        assert btn.state == ElementState.NORMAL
    
    def test_contains_point(self):
        """Test point containment check."""
        btn = Button(x=10, y=10, width=20, height=10)
        
        assert btn.contains_point(15, 15) is True
        assert btn.contains_point(10, 10) is True
        assert btn.contains_point(29, 19) is True
        assert btn.contains_point(5, 15) is False
        assert btn.contains_point(35, 15) is False
    
    def test_press_event(self):
        """Test button press handling."""
        btn = Button(x=0, y=0, width=20, height=10)
        
        event = InputEvent(type=InputType.TOUCH, action="press", x=10, y=5)
        result = btn.handle_input(event)
        
        assert result is True
        assert btn.state == ElementState.PRESSED
    
    def test_click_callback(self):
        """Test click callback is called."""
        btn = Button(x=0, y=0, width=20, height=10)
        clicked = []
        
        btn.on('click', lambda e: clicked.append(True))
        
        # Press
        btn.handle_input(InputEvent(type=InputType.TOUCH, action="press", x=10, y=5))
        # Release
        btn.handle_input(InputEvent(type=InputType.TOUCH, action="release", x=10, y=5))
        
        assert len(clicked) == 1
    
    def test_disabled_button(self):
        """Test disabled button doesn't respond."""
        btn = Button(x=0, y=0, width=20, height=10, enabled=False)
        
        event = InputEvent(type=InputType.TOUCH, action="press", x=10, y=5)
        result = btn.handle_input(event)
        
        assert result is False
    
    def test_render(self):
        """Test button rendering."""
        btn = Button(x=5, y=5, width=30, height=15, text="OK")
        image = Image.new('RGB', (64, 32), (0, 0, 0))
        
        btn.render(image)
        
        # Just verify no exception is raised
        assert image.size == (64, 32)
    
    def test_to_dict(self):
        """Test button serialization."""
        btn = Button(x=10, y=20, width=30, height=15, text="Test")
        data = btn.to_dict()
        
        assert data['x'] == 10
        assert data['y'] == 20
        assert data['text'] == "Test"


class TestSlider:
    """Tests for Slider class."""
    
    def test_creation(self):
        """Test slider creation."""
        slider = Slider(x=0, y=0, width=50, height=10, min_value=0, max_value=100, value=50)
        
        assert slider.value == 50
        assert slider.min_value == 0
        assert slider.max_value == 100
    
    def test_value_clamping(self):
        """Test slider value clamping."""
        slider = Slider(x=0, y=0, width=50, height=10, min_value=0, max_value=100)
        
        slider.value = 150
        assert slider.value == 100
        
        slider.value = -50
        assert slider.value == 0
    
    def test_normalized_value(self):
        """Test normalized value calculation."""
        slider = Slider(x=0, y=0, width=50, height=10, min_value=0, max_value=100, value=50)
        
        assert slider.get_normalized_value() == 0.5
        
        slider.value = 0
        assert slider.get_normalized_value() == 0
        
        slider.value = 100
        assert slider.get_normalized_value() == 1.0
    
    def test_value_change_callback(self):
        """Test value change callback."""
        slider = Slider(x=0, y=0, width=50, height=10)
        changes = []
        
        slider.on('change', lambda e, value: changes.append(value))
        slider.value = 75
        
        assert len(changes) == 1
        assert changes[0] == 75
    
    def test_drag_interaction(self):
        """Test slider drag interaction."""
        slider = Slider(x=0, y=0, width=100, height=10, orientation="horizontal")
        
        # Press at start
        slider.handle_input(InputEvent(type=InputType.TOUCH, action="press", x=50, y=5))
        
        # Should set value to 50%
        assert slider.value == 50
    
    def test_render(self):
        """Test slider rendering."""
        slider = Slider(x=5, y=10, width=50, height=10)
        image = Image.new('RGB', (64, 32), (0, 0, 0))
        
        slider.render(image)
        
        assert image.size == (64, 32)


class TestMenu:
    """Tests for Menu class."""
    
    def test_creation(self):
        """Test menu creation."""
        items = [MenuItem("Option 1"), MenuItem("Option 2")]
        menu = Menu(x=0, y=0, width=50, items=items)
        
        assert len(menu.items) == 2
        assert menu.selected_index == 0
    
    def test_add_item(self):
        """Test adding menu items."""
        menu = Menu(x=0, y=0, width=50)
        menu.add_item(MenuItem("New Item"))
        
        assert len(menu.items) == 1
    
    def test_navigation(self):
        """Test menu navigation."""
        items = [MenuItem("A"), MenuItem("B"), MenuItem("C")]
        menu = Menu(x=0, y=0, width=50, items=items)
        
        assert menu.selected_index == 0
        
        menu.select_next()
        assert menu.selected_index == 1
        
        menu.select_previous()
        assert menu.selected_index == 0
    
    def test_navigation_wrap(self):
        """Test menu navigation wrapping."""
        items = [MenuItem("A"), MenuItem("B")]
        menu = Menu(x=0, y=0, width=50, items=items)
        
        menu.select_next()
        menu.select_next()  # Should wrap to 0
        assert menu.selected_index == 0
    
    def test_item_activation(self):
        """Test menu item activation."""
        activated = []
        
        def on_activate():
            activated.append(True)
        
        items = [MenuItem("Test", action=on_activate)]
        menu = Menu(x=0, y=0, width=50, items=items)
        
        menu.activate_selected()
        
        assert len(activated) == 1
    
    def test_button_input(self):
        """Test menu button input handling."""
        items = [MenuItem("A"), MenuItem("B")]
        menu = Menu(x=0, y=0, width=50, items=items)
        
        event = InputEvent(type=InputType.BUTTON, action="press", value="down")
        result = menu.handle_input(event)
        
        assert result is True
        assert menu.selected_index == 1


class TestTextInput:
    """Tests for TextInput class."""
    
    def test_creation(self):
        """Test text input creation."""
        text_input = TextInput(x=0, y=0, width=50, height=12, text="Hello")
        
        assert text_input.text == "Hello"
        assert text_input.cursor_position == 5
    
    def test_insert_char(self):
        """Test character insertion."""
        text_input = TextInput(x=0, y=0, width=50, height=12, text="")
        
        text_input.set_focus(True)
        text_input.insert_char('A')
        
        assert text_input.text == "A"
        assert text_input.cursor_position == 1
    
    def test_delete_char(self):
        """Test character deletion."""
        text_input = TextInput(x=0, y=0, width=50, height=12, text="ABC")
        text_input.cursor_position = 2
        
        text_input.delete_char(forward=False)  # Backspace
        
        assert text_input.text == "AC"
        assert text_input.cursor_position == 1
    
    def test_max_length(self):
        """Test max length enforcement."""
        text_input = TextInput(x=0, y=0, width=50, height=12, max_length=5)
        
        for char in "ABCDEFG":
            text_input.insert_char(char)
        
        assert len(text_input.text) == 5
    
    def test_cursor_movement(self):
        """Test cursor movement."""
        text_input = TextInput(x=0, y=0, width=50, height=12, text="Hello")
        
        text_input.move_cursor(-1)
        assert text_input.cursor_position == 4
        
        text_input.move_cursor(-10)  # Should clamp to 0
        assert text_input.cursor_position == 0


class TestProgressIndicator:
    """Tests for ProgressIndicator class."""
    
    def test_creation(self):
        """Test progress indicator creation."""
        progress = ProgressIndicator(x=0, y=0, width=50, height=10, value=50)
        
        assert progress.value == 50
    
    def test_value_clamping(self):
        """Test value clamping."""
        progress = ProgressIndicator(x=0, y=0, width=50, height=10)
        
        progress.value = 150
        assert progress.value == 100
        
        progress.value = -10
        assert progress.value == 0
    
    def test_animation_update(self):
        """Test progress animation."""
        progress = ProgressIndicator(x=0, y=0, width=50, height=10, value=100)
        progress._animation_value = 0
        
        progress.update(0.5)
        
        # Animation should have progressed
        assert progress._animation_value > 0
    
    def test_render_bar(self):
        """Test bar rendering."""
        progress = ProgressIndicator(x=0, y=0, width=50, height=10, indicator_type="bar")
        image = Image.new('RGB', (64, 32), (0, 0, 0))
        
        progress.render(image)
        assert image.size == (64, 32)
    
    def test_render_circle(self):
        """Test circle indicator rendering."""
        progress = ProgressIndicator(x=0, y=0, width=20, height=20, indicator_type="circle")
        image = Image.new('RGB', (64, 32), (0, 0, 0))
        
        progress.render(image)
        assert image.size == (64, 32)


class TestInteractiveManager:
    """Tests for InteractiveManager class."""
    
    def test_creation(self):
        """Test manager creation."""
        manager = InteractiveManager(width=64, height=32)
        
        assert manager.width == 64
        assert manager.height == 32
        assert len(manager.elements) == 0
    
    def test_add_element(self):
        """Test adding elements."""
        manager = InteractiveManager(width=64, height=32)
        btn = Button(x=0, y=0, width=20, height=10)
        
        manager.add_element(btn)
        
        assert len(manager.elements) == 1
    
    def test_remove_element(self):
        """Test removing elements."""
        manager = InteractiveManager(width=64, height=32)
        btn = Button(x=0, y=0, width=20, height=10)
        manager.add_element(btn)
        
        result = manager.remove_element(btn.id)
        
        assert result is True
        assert len(manager.elements) == 0
    
    def test_get_element_at(self):
        """Test finding element at position."""
        manager = InteractiveManager(width=64, height=32)
        btn = Button(x=10, y=10, width=20, height=10)
        manager.add_element(btn)
        
        found = manager.get_element_at(15, 15)
        assert found == btn
        
        not_found = manager.get_element_at(0, 0)
        assert not_found is None
    
    def test_input_dispatch(self):
        """Test input event dispatching."""
        manager = InteractiveManager(width=64, height=32)
        btn = Button(x=0, y=0, width=20, height=10)
        manager.add_element(btn)
        
        clicks = []
        btn.on('click', lambda e: clicks.append(True))
        
        manager.handle_input(InputEvent(type=InputType.TOUCH, action="press", x=10, y=5))
        manager.handle_input(InputEvent(type=InputType.TOUCH, action="release", x=10, y=5))
        
        assert len(clicks) == 1
    
    def test_render(self):
        """Test rendering all elements."""
        manager = InteractiveManager(width=64, height=32)
        manager.add_element(Button(x=0, y=0, width=20, height=10, text="A"))
        manager.add_element(Button(x=25, y=0, width=20, height=10, text="B"))
        
        image = Image.new('RGB', (64, 32), (0, 0, 0))
        manager.render(image)
        
        assert image.size == (64, 32)
    
    def test_clear(self):
        """Test clearing all elements."""
        manager = InteractiveManager(width=64, height=32)
        manager.add_element(Button(x=0, y=0, width=20, height=10))
        manager.add_element(Button(x=25, y=0, width=20, height=10))
        
        manager.clear()
        
        assert len(manager.elements) == 0


class TestNavigationBar:
    """Tests for NavigationBar class."""
    
    def test_creation(self):
        """Test navigation bar creation."""
        nav = NavigationBar(x=0, y=0, width=64, height=10, items=["A", "B", "C"])
        
        assert len(nav.items) == 3
        assert nav.selected_index == 0
    
    def test_select_item(self):
        """Test item selection."""
        nav = NavigationBar(x=0, y=0, width=64, height=10, items=["A", "B", "C"])
        
        nav.select_item(2)
        assert nav.selected_index == 2
    
    def test_navigation_callback(self):
        """Test navigation callback."""
        nav = NavigationBar(x=0, y=0, width=64, height=10, items=["A", "B", "C"])
        navigated = []
        
        nav.on('navigate', lambda e, index, item: navigated.append((index, item)))
        nav.select_item(1)
        
        assert len(navigated) == 1
        assert navigated[0] == (1, "B")


class TestFeedbackEffect:
    """Tests for FeedbackEffect class."""
    
    def test_creation(self):
        """Test feedback effect creation."""
        effect = FeedbackEffect(effect_type="pulse", duration=0.3)
        
        assert effect.effect_type == "pulse"
        assert effect.duration == 0.3
        assert effect.active is False
    
    def test_trigger(self):
        """Test triggering effect."""
        effect = FeedbackEffect()
        
        effect.trigger(10, 20)
        
        assert effect.active is True
        assert effect.position == (10, 20)
    
    def test_update_completion(self):
        """Test effect completion."""
        effect = FeedbackEffect(duration=0.3)
        effect.trigger(10, 20)
        
        effect.update(0.4)  # Longer than duration
        
        assert effect.active is False
