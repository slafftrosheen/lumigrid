"""Tests for timeline-based sequencer."""

import pytest
from datetime import datetime, time as dt_time
from unittest.mock import MagicMock, patch
from lumigrid.sequencer.timeline import Timeline, TimelineEntry, TriggerType
from lumigrid.sequencer.manager import SequencerManager


class TestTimelineEntry:
    """Tests for TimelineEntry class."""
    
    def test_create_entry(self):
        """Test creating a timeline entry."""
        entry_config = {
            'id': 'test-entry',
            'preset_name': 'Test Preset',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'days_of_week': [0, 1, 2, 3, 4],
            'duration': 300,
            'enabled': True
        }
        
        entry = TimelineEntry(entry_config)
        
        assert entry.id == 'test-entry'
        assert entry.preset_name == 'Test Preset'
        assert entry.trigger_type == TriggerType.TIME_OF_DAY
        assert entry.time_of_day == '12:00:00'
        assert entry.days_of_week == [0, 1, 2, 3, 4]
        assert entry.duration == 300
        assert entry.enabled is True
    
    def test_time_of_day_trigger(self):
        """Test time of day trigger."""
        entry_config = {
            'preset_name': 'Morning',
            'trigger_type': 'time_of_day',
            'time_of_day': '08:00:00',
            'days_of_week': [0, 1, 2, 3, 4]  # Weekdays
        }
        
        entry = TimelineEntry(entry_config)
        
        # Should trigger on weekday at 8:00 AM
        test_time = datetime(2024, 1, 8, 8, 0, 0)  # Monday
        assert entry.should_trigger(test_time) is True
        
        # Should not trigger on weekend
        test_time = datetime(2024, 1, 13, 8, 0, 0)  # Saturday
        assert entry.should_trigger(test_time) is False
        
        # Should not trigger at wrong time
        test_time = datetime(2024, 1, 8, 9, 0, 0)  # Monday 9 AM
        assert entry.should_trigger(test_time) is False
    
    def test_datetime_trigger(self):
        """Test specific datetime trigger."""
        entry_config = {
            'preset_name': 'Event',
            'trigger_type': 'datetime',
            'datetime': '2024-01-15T14:30:00'
        }
        
        entry = TimelineEntry(entry_config)
        
        # Should trigger at exact datetime (within 1 minute window)
        test_time = datetime(2024, 1, 15, 14, 30, 0)
        assert entry.should_trigger(test_time) is True
        
        # Should not trigger at different datetime
        test_time = datetime(2024, 1, 15, 15, 30, 0)
        assert entry.should_trigger(test_time) is False
    
    def test_interval_trigger(self):
        """Test interval trigger."""
        entry_config = {
            'preset_name': 'Periodic',
            'trigger_type': 'interval',
            'interval_seconds': 300,  # 5 minutes
            'last_triggered': 0
        }
        
        entry = TimelineEntry(entry_config)
        
        # Should trigger if enough time has passed
        test_time = datetime(2024, 1, 15, 14, 30, 0)
        assert entry.should_trigger(test_time) is True
        
        # Mark as triggered
        entry.mark_triggered(test_time.timestamp())
        
        # Should not trigger immediately after
        test_time = datetime(2024, 1, 15, 14, 30, 30)
        assert entry.should_trigger(test_time) is False
        
        # Should trigger after interval
        test_time = datetime(2024, 1, 15, 14, 36, 0)
        assert entry.should_trigger(test_time) is True
    
    def test_disabled_entry(self):
        """Test that disabled entries don't trigger."""
        entry_config = {
            'preset_name': 'Disabled',
            'trigger_type': 'time_of_day',
            'time_of_day': '08:00:00',
            'enabled': False
        }
        
        entry = TimelineEntry(entry_config)
        
        test_time = datetime(2024, 1, 8, 8, 0, 0)
        assert entry.should_trigger(test_time) is False
    
    def test_to_dict(self):
        """Test converting entry to dictionary."""
        entry_config = {
            'id': 'test-entry',
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'duration': 300,
            'enabled': True
        }
        
        entry = TimelineEntry(entry_config)
        entry_dict = entry.to_dict()
        
        assert entry_dict['id'] == 'test-entry'
        assert entry_dict['preset_name'] == 'Test'
        assert entry_dict['trigger_type'] == 'time_of_day'


class TestTimeline:
    """Tests for Timeline class."""
    
    def test_create_timeline(self):
        """Test creating a timeline."""
        timeline_config = {
            'id': 'test-timeline',
            'name': 'Test Timeline',
            'description': 'A test timeline',
            'enabled': True,
            'loop': True,
            'entries': [
                {
                    'preset_name': 'Entry1',
                    'trigger_type': 'time_of_day',
                    'time_of_day': '08:00:00'
                }
            ]
        }
        
        timeline = Timeline(timeline_config)
        
        assert timeline.id == 'test-timeline'
        assert timeline.name == 'Test Timeline'
        assert timeline.description == 'A test timeline'
        assert timeline.enabled is True
        assert timeline.loop is True
        assert len(timeline.entries) == 1
    
    def test_disabled_timeline(self):
        """Test that disabled timelines don't return active entries."""
        timeline_config = {
            'name': 'Disabled Timeline',
            'enabled': False,
            'entries': [
                {
                    'preset_name': 'Entry1',
                    'trigger_type': 'time_of_day',
                    'time_of_day': '08:00:00'
                }
            ]
        }
        
        timeline = Timeline(timeline_config)
        test_time = datetime(2024, 1, 8, 8, 0, 0)
        
        assert timeline.get_active_entry(test_time) is None
    
    def test_to_dict(self):
        """Test converting timeline to dictionary."""
        timeline_config = {
            'id': 'test-timeline',
            'name': 'Test Timeline',
            'enabled': True,
            'loop': False,
            'entries': []
        }
        
        timeline = Timeline(timeline_config)
        timeline_dict = timeline.to_dict()
        
        assert timeline_dict['id'] == 'test-timeline'
        assert timeline_dict['name'] == 'Test Timeline'
        assert timeline_dict['enabled'] is True
        assert timeline_dict['loop'] is False


class TestSequencerManager:
    """Tests for SequencerManager class."""
    
    def test_create_manager(self):
        """Test creating a sequencer manager."""
        config = {'timelines': []}
        play_preset_callback = MagicMock(return_value=True)
        
        manager = SequencerManager(config, play_preset_callback)
        
        assert manager.running is False
        assert len(manager.timelines) == 0
    
    def test_load_timelines(self):
        """Test loading timelines from config."""
        config = {
            'timelines': [
                {
                    'id': 'timeline1',
                    'name': 'Timeline 1',
                    'enabled': True,
                    'entries': []
                },
                {
                    'id': 'timeline2',
                    'name': 'Timeline 2',
                    'enabled': False,
                    'entries': []
                }
            ]
        }
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        
        assert len(manager.timelines) == 2
        assert manager.timelines[0].name == 'Timeline 1'
        assert manager.timelines[1].name == 'Timeline 2'
    
    def test_get_timelines(self):
        """Test getting all timelines."""
        config = {
            'timelines': [
                {
                    'id': 'timeline1',
                    'name': 'Timeline 1',
                    'enabled': True,
                    'entries': []
                }
            ]
        }
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        timelines = manager.get_timelines()
        
        assert len(timelines) == 1
        assert timelines[0]['name'] == 'Timeline 1'
    
    def test_add_timeline(self):
        """Test adding a timeline."""
        config = {'timelines': []}
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        
        timeline_data = {
            'name': 'New Timeline',
            'enabled': True,
            'entries': []
        }
        
        timeline_id = manager.add_timeline(timeline_data)
        
        assert timeline_id is not None
        assert len(manager.timelines) == 1
        assert manager.timelines[0].name == 'New Timeline'
    
    def test_update_timeline(self):
        """Test updating a timeline."""
        config = {
            'timelines': [
                {
                    'id': 'timeline1',
                    'name': 'Timeline 1',
                    'enabled': True,
                    'entries': []
                }
            ]
        }
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        
        updated_data = {
            'name': 'Updated Timeline',
            'enabled': False,
            'entries': []
        }
        
        result = manager.update_timeline('timeline1', updated_data)
        
        assert result is True
        assert manager.timelines[0].name == 'Updated Timeline'
        assert manager.timelines[0].enabled is False
    
    def test_delete_timeline(self):
        """Test deleting a timeline."""
        config = {
            'timelines': [
                {
                    'id': 'timeline1',
                    'name': 'Timeline 1',
                    'enabled': True,
                    'entries': []
                }
            ]
        }
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        
        result = manager.delete_timeline('timeline1')
        
        assert result is True
        assert len(manager.timelines) == 0
    
    def test_activate_timeline(self):
        """Test activating a timeline."""
        config = {
            'timelines': [
                {
                    'id': 'timeline1',
                    'name': 'Timeline 1',
                    'enabled': False,
                    'entries': []
                }
            ]
        }
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        
        result = manager.activate_timeline('timeline1')
        
        assert result is True
        assert manager.timelines[0].enabled is True
    
    def test_deactivate_timeline(self):
        """Test deactivating a timeline."""
        config = {
            'timelines': [
                {
                    'id': 'timeline1',
                    'name': 'Timeline 1',
                    'enabled': True,
                    'entries': []
                }
            ]
        }
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        
        result = manager.deactivate_timeline('timeline1')
        
        assert result is True
        assert manager.timelines[0].enabled is False
    
    def test_get_status(self):
        """Test getting sequencer status."""
        config = {
            'timelines': [
                {
                    'id': 'timeline1',
                    'name': 'Timeline 1',
                    'enabled': True,
                    'entries': []
                },
                {
                    'id': 'timeline2',
                    'name': 'Timeline 2',
                    'enabled': False,
                    'entries': []
                }
            ]
        }
        play_preset_callback = MagicMock()
        
        manager = SequencerManager(config, play_preset_callback)
        status = manager.get_status()
        
        assert status['running'] is False
        assert status['timeline_count'] == 2
        assert status['enabled_timeline_count'] == 1
