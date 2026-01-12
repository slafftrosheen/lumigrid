"""Tests for the advanced frames module."""

import pytest
from unittest.mock import patch, MagicMock
from PIL import Image

from lumigrid.display.advanced_frames import (
    AdvancedFrameType,
    ChartType,
    FrameElement,
    DataVisualizationFrame,
    TemplateFrame,
    ConditionalFrame,
    MultiPageFrame,
    ScrollingFrame,
    AnimatedFrame,
    FrameLibrary,
    FrameScheduler,
    FrameGroup,
)


class TestFrameElement:
    """Tests for FrameElement class."""
    
    def test_element_creation_defaults(self):
        """Test frame element creation with defaults."""
        elem = FrameElement()
        assert elem.type == "text"
        assert elem.x == 0
        assert elem.y == 0
        assert elem.visible is True
        assert elem.layer == 0
    
    def test_element_creation_custom(self):
        """Test frame element creation with custom values."""
        elem = FrameElement(
            type="rectangle",
            x=10, y=20,
            width=50, height=30,
            layer=5,
            properties={'color': (255, 0, 0)}
        )
        assert elem.type == "rectangle"
        assert elem.x == 10
        assert elem.y == 20
        assert elem.width == 50
        assert elem.height == 30
        assert elem.layer == 5
        assert elem.properties['color'] == (255, 0, 0)
    
    def test_element_to_dict(self):
        """Test converting element to dictionary."""
        elem = FrameElement(type="text", x=5, y=10)
        d = elem.to_dict()
        
        assert d['type'] == "text"
        assert d['x'] == 5
        assert d['y'] == 10
        assert 'id' in d
    
    def test_element_from_dict(self):
        """Test creating element from dictionary."""
        data = {
            'type': 'circle',
            'x': 32,
            'y': 16,
            'width': 10,
            'height': 10,
            'properties': {'radius': 5}
        }
        elem = FrameElement.from_dict(data)
        
        assert elem.type == 'circle'
        assert elem.x == 32
        assert elem.y == 16


class TestDataVisualizationFrame:
    """Tests for DataVisualizationFrame class."""
    
    def test_creation(self):
        """Test data visualization frame creation."""
        frame = DataVisualizationFrame(width=64, height=32)
        assert frame.width == 64
        assert frame.height == 32
        assert frame.chart_type == ChartType.BAR
    
    def test_bar_chart_render(self):
        """Test bar chart rendering."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.BAR,
            data=[10, 20, 30, 40, 50]
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)
    
    def test_line_chart_render(self):
        """Test line chart rendering."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.LINE,
            data=[10, 20, 15, 30, 25]
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)
    
    def test_pie_chart_render(self):
        """Test pie chart rendering."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.PIE,
            data=[25, 25, 50]
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)
    
    def test_progress_bar_render(self):
        """Test progress bar rendering."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.PROGRESS,
            data=[75]  # 75% progress
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)
    
    def test_gauge_render(self):
        """Test gauge rendering."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.GAUGE,
            data=[60]  # 60%
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)
    
    def test_sparkline_render(self):
        """Test sparkline rendering."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.SPARKLINE,
            data=[5, 10, 7, 15, 12, 20]
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)
    
    def test_data_source(self):
        """Test dynamic data source."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.BAR
        )
        
        def get_data():
            return [1, 2, 3, 4, 5]
        
        frame.set_data_source(get_data)
        frame.update_data()
        
        assert frame.data == [1, 2, 3, 4, 5]
    
    def test_empty_data_render(self):
        """Test rendering with empty data."""
        frame = DataVisualizationFrame(
            width=64, height=32,
            chart_type=ChartType.BAR,
            data=[]
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)


class TestTemplateFrame:
    """Tests for TemplateFrame class."""
    
    def test_creation(self):
        """Test template frame creation."""
        frame = TemplateFrame(
            width=64, height=32,
            template="Hello {name}!",
            variables={'name': 'World'}
        )
        assert frame.width == 64
        assert frame.height == 32
        assert frame.template == "Hello {name}!"
    
    def test_variable_substitution(self):
        """Test variable substitution."""
        frame = TemplateFrame(
            width=64, height=32,
            template="Temp: {temp}°C",
            variables={'temp': 25}
        )
        
        result = frame._render_template()
        assert result == "Temp: 25°C"
    
    def test_set_variable(self):
        """Test setting variables."""
        frame = TemplateFrame(
            width=64, height=32,
            template="{greeting} {name}!"
        )
        frame.set_variable('greeting', 'Hello')
        frame.set_variable('name', 'Test')
        
        result = frame._render_template()
        assert result == "Hello Test!"
    
    def test_variable_source(self):
        """Test dynamic variable source."""
        frame = TemplateFrame(
            width=64, height=32,
            template="Count: {count}"
        )
        
        counter = [0]
        def get_count():
            counter[0] += 1
            return counter[0]
        
        frame.set_variable_source('count', get_count)
        
        result1 = frame._render_template()
        assert result1 == "Count: 1"
        
        result2 = frame._render_template()
        assert result2 == "Count: 2"
    
    def test_render(self):
        """Test template rendering."""
        frame = TemplateFrame(
            width=64, height=32,
            template="Test",
            text_color=(255, 255, 255)
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestConditionalFrame:
    """Tests for ConditionalFrame class."""
    
    def test_creation(self):
        """Test conditional frame creation."""
        frame = ConditionalFrame(
            width=64, height=32,
            default_content={'text': 'Default'}
        )
        assert frame.width == 64
        assert frame.height == 32
    
    def test_add_condition(self):
        """Test adding conditions."""
        frame = ConditionalFrame(width=64, height=32)
        
        frame.add_condition(
            'is_day',
            lambda: True,
            {'text': 'Day Mode', 'bg_color': (255, 255, 200)}
        )
        
        assert len(frame.conditions) == 1
        assert 'is_day' in frame._condition_checks
    
    def test_condition_matching(self):
        """Test condition matching."""
        frame = ConditionalFrame(
            width=64, height=32,
            default_content={'text': 'Default'}
        )
        
        frame.add_condition(
            'high_temp',
            lambda: True,  # Always true
            {'text': 'Hot!', 'text_color': (255, 0, 0)}
        )
        
        content = frame._get_active_content()
        assert content['text'] == 'Hot!'
    
    def test_fallback_to_default(self):
        """Test fallback to default when no condition matches."""
        frame = ConditionalFrame(
            width=64, height=32,
            default_content={'text': 'Default'}
        )
        
        frame.add_condition(
            'never_true',
            lambda: False,  # Always false
            {'text': 'Never shown'}
        )
        
        content = frame._get_active_content()
        assert content['text'] == 'Default'
    
    def test_render(self):
        """Test conditional rendering."""
        frame = ConditionalFrame(
            width=64, height=32,
            default_content={'text': 'Test'}
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestMultiPageFrame:
    """Tests for MultiPageFrame class."""
    
    def test_creation(self):
        """Test multi-page frame creation."""
        frame = MultiPageFrame(width=64, height=32)
        assert frame.width == 64
        assert frame.height == 32
        assert frame.current_page == 0
    
    def test_add_page(self):
        """Test adding pages."""
        frame = MultiPageFrame(width=64, height=32)
        frame.add_page({'text': 'Page 1'})
        frame.add_page({'text': 'Page 2'})
        
        assert len(frame.pages) == 2
    
    def test_next_page(self):
        """Test navigating to next page."""
        frame = MultiPageFrame(width=64, height=32, pages=[
            {'text': 'Page 1'},
            {'text': 'Page 2'},
            {'text': 'Page 3'}
        ])
        
        assert frame.current_page == 0
        frame.next_page()
        assert frame.current_page == 1
        frame.next_page()
        assert frame.current_page == 2
        frame.next_page()  # Should wrap around
        assert frame.current_page == 0
    
    def test_prev_page(self):
        """Test navigating to previous page."""
        frame = MultiPageFrame(width=64, height=32, pages=[
            {'text': 'Page 1'},
            {'text': 'Page 2'}
        ])
        
        frame.prev_page()  # Should wrap to end
        assert frame.current_page == 1
    
    def test_go_to_page(self):
        """Test going to specific page."""
        frame = MultiPageFrame(width=64, height=32, pages=[
            {'text': 'Page 1'},
            {'text': 'Page 2'},
            {'text': 'Page 3'}
        ])
        
        frame.go_to_page(2)
        assert frame.current_page == 2
    
    def test_auto_advance(self):
        """Test auto-advancing pages."""
        frame = MultiPageFrame(
            width=64, height=32,
            pages=[{'text': '1'}, {'text': '2'}],
            auto_advance=True,
            page_duration=0.1
        )
        
        # Fast forward time
        import time
        time.sleep(0.15)
        frame.update()
        
        assert frame.current_page == 1
    
    def test_render(self):
        """Test multi-page rendering."""
        frame = MultiPageFrame(
            width=64, height=32,
            pages=[{'text': 'Test'}]
        )
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestScrollingFrame:
    """Tests for ScrollingFrame class."""
    
    def test_creation(self):
        """Test scrolling frame creation."""
        frame = ScrollingFrame(width=64, height=32)
        assert frame.width == 64
        assert frame.height == 32
        assert frame.scroll_position == 0.0
    
    def test_set_content(self):
        """Test setting content image."""
        frame = ScrollingFrame(width=64, height=32)
        content = Image.new('RGB', (64, 100), (255, 0, 0))
        
        frame.set_content(content)
        
        assert frame._content_image is not None
        assert frame.content_height == 100
    
    def test_update_scroll_position(self):
        """Test scroll position updates."""
        frame = ScrollingFrame(
            width=64, height=32,
            scroll_speed=20.0
        )
        content = Image.new('RGB', (64, 100), (0, 0, 0))
        frame.set_content(content)
        
        initial_pos = frame.scroll_position
        frame.update(dt=0.1)
        
        assert frame.scroll_position > initial_pos
    
    def test_scroll_loop(self):
        """Test scroll looping."""
        frame = ScrollingFrame(
            width=64, height=32,
            scroll_speed=1000.0,  # Very fast
            loop=True
        )
        content = Image.new('RGB', (64, 50), (0, 0, 0))
        frame.set_content(content)
        
        # Update many times to exceed content height
        for _ in range(10):
            frame.update(dt=0.1)
        
        # Position should wrap around
        assert frame.scroll_position < frame.content_height
    
    def test_render(self):
        """Test scrolling rendering."""
        frame = ScrollingFrame(width=64, height=32)
        content = Image.new('RGB', (64, 100), (100, 100, 100))
        frame.set_content(content)
        
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestAnimatedFrame:
    """Tests for AnimatedFrame class."""
    
    def test_creation(self):
        """Test animated frame creation."""
        frame = AnimatedFrame(width=64, height=32)
        assert frame.width == 64
        assert frame.height == 32
        assert len(frame.elements) == 0
    
    def test_add_element(self):
        """Test adding elements."""
        frame = AnimatedFrame(width=64, height=32)
        elem = FrameElement(type="text", x=10, y=10)
        
        frame.add_element(elem)
        
        assert len(frame.elements) == 1
        assert frame.elements[0] == elem
    
    def test_remove_element(self):
        """Test removing elements."""
        frame = AnimatedFrame(width=64, height=32)
        elem = FrameElement(type="text", x=10, y=10)
        frame.add_element(elem)
        
        success = frame.remove_element(elem.id)
        
        assert success is True
        assert len(frame.elements) == 0
    
    def test_element_sorting_by_layer(self):
        """Test elements are sorted by layer."""
        frame = AnimatedFrame(width=64, height=32)
        frame.add_element(FrameElement(layer=3))
        frame.add_element(FrameElement(layer=1))
        frame.add_element(FrameElement(layer=2))
        
        layers = [e.layer for e in frame.elements]
        assert layers == [1, 2, 3]
    
    def test_animate_element(self):
        """Test animating an element."""
        from lumigrid.display.animation import Animation
        
        frame = AnimatedFrame(width=64, height=32)
        elem = FrameElement(id="test_elem", type="text", x=10, y=10)
        frame.add_element(elem)
        
        anim = Animation(duration=1.0)
        anim.add_keyframe(0.0, 0)
        anim.add_keyframe(1.0, 100)
        
        frame.animate_element("test_elem", "x_offset", anim)
        
        assert "test_elem" in frame._element_animations
        assert "x_offset" in frame._element_animations["test_elem"]
    
    def test_add_preset_animation(self):
        """Test adding preset animation."""
        frame = AnimatedFrame(width=64, height=32)
        elem = FrameElement(id="test_elem", type="text")
        frame.add_element(elem)
        
        frame.add_preset_animation("test_elem", "fade_in", duration=0.5)
        
        assert "test_elem" in frame._element_animations
    
    def test_render(self):
        """Test animated frame rendering."""
        frame = AnimatedFrame(width=64, height=32)
        elem = FrameElement(
            type="text", x=5, y=10,
            properties={'text': 'Test', 'color': (255, 255, 255)}
        )
        frame.add_element(elem)
        
        image = frame.render()
        
        assert isinstance(image, Image.Image)
        assert image.size == (64, 32)


class TestFrameLibrary:
    """Tests for FrameLibrary class."""
    
    def test_creation(self):
        """Test frame library creation."""
        library = FrameLibrary()
        assert len(library.frames) == 0
        assert len(library.categories) == 0
    
    def test_add_frame(self):
        """Test adding frames."""
        library = FrameLibrary()
        config = {'type': 'time', 'name': 'Clock'}
        
        library.add_frame('clock_1', config, category='clocks')
        
        assert 'clock_1' in library.frames
        assert 'clocks' in library.categories
        assert 'clock_1' in library.categories['clocks']
    
    def test_get_frame(self):
        """Test getting frame."""
        library = FrameLibrary()
        config = {'type': 'time', 'name': 'Clock'}
        library.add_frame('clock_1', config)
        
        result = library.get_frame('clock_1')
        
        assert result == config
    
    def test_remove_frame(self):
        """Test removing frame."""
        library = FrameLibrary()
        library.add_frame('test', {'type': 'text'}, category='test')
        
        success = library.remove_frame('test')
        
        assert success is True
        assert 'test' not in library.frames
        assert 'test' not in library.categories.get('test', [])
    
    def test_get_frames_in_category(self):
        """Test getting frames by category."""
        library = FrameLibrary()
        library.add_frame('f1', {'name': 'F1'}, category='cat1')
        library.add_frame('f2', {'name': 'F2'}, category='cat1')
        library.add_frame('f3', {'name': 'F3'}, category='cat2')
        
        cat1_frames = library.get_frames_in_category('cat1')
        
        assert len(cat1_frames) == 2
    
    def test_list_categories(self):
        """Test listing categories."""
        library = FrameLibrary()
        library.add_frame('f1', {}, category='cat1')
        library.add_frame('f2', {}, category='cat2')
        
        categories = library.list_categories()
        
        assert 'cat1' in categories
        assert 'cat2' in categories
    
    def test_export_import_frame(self):
        """Test exporting and importing frames."""
        library = FrameLibrary()
        config = {'type': 'time', 'name': 'Clock'}
        library.add_frame('clock_1', config)
        
        exported = library.export_frame('clock_1')
        
        assert exported['id'] == 'clock_1'
        assert exported['config'] == config
        
        # Import to new library
        library2 = FrameLibrary()
        imported_id = library2.import_frame(exported)
        
        assert imported_id is not None
        assert library2.get_frame(imported_id) == config
    
    def test_to_dict_from_dict(self):
        """Test serializing and deserializing library."""
        library = FrameLibrary()
        library.add_frame('f1', {'name': 'F1'}, category='test')
        
        data = library.to_dict()
        
        library2 = FrameLibrary.from_dict(data)
        
        assert library2.get_frame('f1') == {'name': 'F1'}
        assert 'test' in library2.categories


class TestFrameScheduler:
    """Tests for FrameScheduler class."""
    
    def test_creation(self):
        """Test frame scheduler creation."""
        scheduler = FrameScheduler(timezone="UTC")
        assert len(scheduler.schedules) == 0
    
    def test_add_schedule(self):
        """Test adding schedule."""
        scheduler = FrameScheduler()
        scheduler.add_schedule(
            frame_id='morning_clock',
            start_time='06:00',
            end_time='12:00',
            days=[0, 1, 2, 3, 4]  # Weekdays
        )
        
        assert len(scheduler.schedules) == 1
    
    def test_remove_schedule(self):
        """Test removing schedule."""
        scheduler = FrameScheduler()
        scheduler.add_schedule('test', '00:00', '23:59')
        
        scheduler.remove_schedule('test')
        
        assert len(scheduler.schedules) == 0
    
    def test_get_active_frames(self):
        """Test getting active frames."""
        scheduler = FrameScheduler()
        # Schedule that's always active
        scheduler.add_schedule(
            frame_id='always_on',
            start_time='00:00',
            end_time='23:59',
            days=None  # All days
        )
        
        active = scheduler.get_active_frames()
        
        assert 'always_on' in active
    
    def test_is_frame_active(self):
        """Test checking if frame is active."""
        scheduler = FrameScheduler()
        scheduler.add_schedule('always_on', '00:00', '23:59')
        
        assert scheduler.is_frame_active('always_on') is True
        assert scheduler.is_frame_active('nonexistent') is False


class TestFrameGroup:
    """Tests for FrameGroup class."""
    
    def test_creation(self):
        """Test frame group creation."""
        group = FrameGroup(name="Test Group")
        assert group.name == "Test Group"
        assert len(group.frame_ids) == 0
        assert group.enabled is True
    
    def test_add_frame(self):
        """Test adding frame to group."""
        group = FrameGroup(name="Test")
        group.add_frame('frame_1')
        group.add_frame('frame_2')
        
        assert 'frame_1' in group.frame_ids
        assert 'frame_2' in group.frame_ids
        assert len(group.frame_ids) == 2
    
    def test_add_frame_no_duplicates(self):
        """Test adding same frame twice doesn't duplicate."""
        group = FrameGroup(name="Test")
        group.add_frame('frame_1')
        group.add_frame('frame_1')
        
        assert group.frame_ids.count('frame_1') == 1
    
    def test_remove_frame(self):
        """Test removing frame from group."""
        group = FrameGroup(name="Test", frame_ids=['f1', 'f2', 'f3'])
        group.remove_frame('f2')
        
        assert 'f2' not in group.frame_ids
        assert len(group.frame_ids) == 2
    
    def test_to_dict(self):
        """Test converting to dictionary."""
        group = FrameGroup(name="Test", frame_ids=['f1', 'f2'])
        group.enabled = False
        
        data = group.to_dict()
        
        assert data['name'] == "Test"
        assert data['frame_ids'] == ['f1', 'f2']
        assert data['enabled'] is False
    
    def test_from_dict(self):
        """Test creating from dictionary."""
        data = {
            'name': 'Test Group',
            'frame_ids': ['a', 'b', 'c'],
            'enabled': True
        }
        
        group = FrameGroup.from_dict(data)
        
        assert group.name == 'Test Group'
        assert group.frame_ids == ['a', 'b', 'c']
        assert group.enabled is True
