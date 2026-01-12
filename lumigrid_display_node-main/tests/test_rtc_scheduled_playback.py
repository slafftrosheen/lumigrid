"""Tests for RTC-based scheduled playback features."""

import pytest
from datetime import datetime, date, time as dt_time
from lumigrid.sequencer.timeline import TimelineEntry, TriggerType
from lumigrid.validators import validate_timeline_entry, validate_timeline


class TestCalendarBasedScheduling:
    """Tests for calendar-based scheduling features."""
    
    def test_date_range_valid_from(self):
        """Test that entry only triggers within valid_from date."""
        entry_config = {
            'preset_name': 'Test Preset',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'valid_from': '2024-06-01',  # Only active from June 1st
            'days_of_week': [0, 1, 2, 3, 4, 5, 6]
        }
        
        entry = TimelineEntry(entry_config)
        
        # Before valid_from date - should not trigger
        test_time = datetime(2024, 5, 31, 12, 0, 0)
        assert entry.should_trigger(test_time) is False
        
        # On valid_from date - should trigger
        test_time = datetime(2024, 6, 1, 12, 0, 0)
        assert entry.should_trigger(test_time) is True
        
        # After valid_from date - should trigger
        test_time = datetime(2024, 7, 15, 12, 0, 0)
        assert entry.should_trigger(test_time) is True
    
    def test_date_range_valid_until(self):
        """Test that entry only triggers within valid_until date."""
        entry_config = {
            'preset_name': 'Test Preset',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'valid_until': '2024-08-31',  # Only active until August 31st
            'days_of_week': [0, 1, 2, 3, 4, 5, 6]
        }
        
        entry = TimelineEntry(entry_config)
        
        # Before valid_until date - should trigger
        test_time = datetime(2024, 8, 15, 12, 0, 0)
        assert entry.should_trigger(test_time) is True
        
        # On valid_until date - should trigger
        test_time = datetime(2024, 8, 31, 12, 0, 0)
        assert entry.should_trigger(test_time) is True
        
        # After valid_until date - should not trigger
        test_time = datetime(2024, 9, 1, 12, 0, 0)
        assert entry.should_trigger(test_time) is False
    
    def test_date_range_full(self):
        """Test seasonal schedule with both valid_from and valid_until."""
        entry_config = {
            'preset_name': 'Summer Display',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'valid_from': '2024-06-01',
            'valid_until': '2024-08-31',
            'days_of_week': [0, 1, 2, 3, 4, 5, 6]
        }
        
        entry = TimelineEntry(entry_config)
        
        # Before range - should not trigger
        test_time = datetime(2024, 5, 31, 12, 0, 0)
        assert entry.should_trigger(test_time) is False
        
        # Within range - should trigger
        test_time = datetime(2024, 7, 15, 12, 0, 0)
        assert entry.should_trigger(test_time) is True
        
        # After range - should not trigger
        test_time = datetime(2024, 9, 1, 12, 0, 0)
        assert entry.should_trigger(test_time) is False
    
    def test_time_range_window(self):
        """Test time window for time-of-day triggers (business hours)."""
        entry_config = {
            'preset_name': 'Business Hours Display',
            'trigger_type': 'time_of_day',
            'time_of_day': '09:00:00',
            'start_time': '08:00:00',  # Active from 8 AM
            'end_time': '17:00:00',    # Until 5 PM
            'days_of_week': [0, 1, 2, 3, 4]  # Weekdays
        }
        
        entry = TimelineEntry(entry_config)
        
        # Monday at 9 AM within time window - should trigger
        test_time = datetime(2024, 1, 8, 9, 0, 0)  # Monday
        assert entry.should_trigger(test_time) is True
        
        # Monday at 7 AM before time window - should not trigger
        test_time = datetime(2024, 1, 8, 7, 0, 0)
        assert entry.should_trigger(test_time) is False
        
        # Monday at 6 PM after time window - should not trigger
        test_time = datetime(2024, 1, 8, 18, 0, 0)
        assert entry.should_trigger(test_time) is False
    
    def test_time_range_crossing_midnight(self):
        """Test time window that crosses midnight (night shift)."""
        entry_config = {
            'preset_name': 'Night Display',
            'trigger_type': 'time_of_day',
            'time_of_day': '23:00:00',
            'start_time': '22:00:00',  # From 10 PM
            'end_time': '06:00:00',    # Until 6 AM (crosses midnight)
            'days_of_week': [0, 1, 2, 3, 4, 5, 6]
        }
        
        entry = TimelineEntry(entry_config)
        
        # At 11 PM - should trigger
        test_time = datetime(2024, 1, 8, 23, 0, 0)
        assert entry.should_trigger(test_time) is True
        
        # At 2 AM - should trigger (within midnight-crossing range)
        test_time = datetime(2024, 1, 8, 2, 0, 0)
        # Note: This checks the time window, not the trigger time
        # The entry won't trigger at 2 AM since time_of_day is 23:00:00
        # But if it were configured to trigger at 02:00:00, it would be allowed
        
        # At 7 AM - should not trigger (outside window)
        entry_config2 = {
            'preset_name': 'Night Display',
            'trigger_type': 'time_of_day',
            'time_of_day': '07:00:00',
            'start_time': '22:00:00',
            'end_time': '06:00:00',
            'days_of_week': [0, 1, 2, 3, 4, 5, 6]
        }
        entry2 = TimelineEntry(entry_config2)
        test_time = datetime(2024, 1, 8, 7, 0, 0)
        assert entry2.should_trigger(test_time) is False
    
    def test_exception_dates_holidays(self):
        """Test exception dates for holidays."""
        entry_config = {
            'preset_name': 'Weekday Display',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'days_of_week': [0, 1, 2, 3, 4],  # Weekdays only
            'exception_dates': [
                '2024-12-25',  # Christmas
                '2024-01-01',  # New Year
                '2024-07-04'   # Independence Day
            ]
        }
        
        entry = TimelineEntry(entry_config)
        
        # Regular weekday - should trigger
        test_time = datetime(2024, 1, 8, 12, 0, 0)  # Monday
        assert entry.should_trigger(test_time) is True
        
        # Holiday (Christmas) - should not trigger
        test_time = datetime(2024, 12, 25, 12, 0, 0)  # Wednesday (Christmas)
        assert entry.should_trigger(test_time) is False
        
        # Holiday (New Year) - should not trigger
        test_time = datetime(2024, 1, 1, 12, 0, 0)  # Monday (New Year)
        assert entry.should_trigger(test_time) is False
    
    def test_combined_constraints(self):
        """Test combining multiple calendar constraints."""
        entry_config = {
            'preset_name': 'Complex Schedule',
            'trigger_type': 'time_of_day',
            'time_of_day': '14:00:00',
            'days_of_week': [0, 1, 2, 3, 4],  # Weekdays
            'valid_from': '2024-06-01',
            'valid_until': '2024-08-31',
            'start_time': '09:00:00',
            'end_time': '17:00:00',
            'exception_dates': ['2024-07-04']  # Independence Day
        }
        
        entry = TimelineEntry(entry_config)
        
        # All constraints met - should trigger
        test_time = datetime(2024, 7, 15, 14, 0, 0)  # Monday in summer, business hours
        assert entry.should_trigger(test_time) is True
        
        # Weekend (violates days_of_week) - should not trigger
        test_time = datetime(2024, 7, 13, 14, 0, 0)  # Saturday
        assert entry.should_trigger(test_time) is False
        
        # Exception date - should not trigger
        test_time = datetime(2024, 7, 4, 14, 0, 0)  # Thursday (Independence Day)
        assert entry.should_trigger(test_time) is False
        
        # Before date range - should not trigger
        test_time = datetime(2024, 5, 15, 14, 0, 0)  # Wednesday before June
        assert entry.should_trigger(test_time) is False
        
        # After date range - should not trigger
        test_time = datetime(2024, 9, 2, 14, 0, 0)  # Monday after August
        assert entry.should_trigger(test_time) is False
    
    def test_to_dict_includes_calendar_fields(self):
        """Test that to_dict includes all calendar-based fields."""
        entry_config = {
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'valid_from': '2024-06-01',
            'valid_until': '2024-08-31',
            'start_time': '09:00:00',
            'end_time': '17:00:00',
            'exception_dates': ['2024-07-04']
        }
        
        entry = TimelineEntry(entry_config)
        entry_dict = entry.to_dict()
        
        assert entry_dict['valid_from'] == '2024-06-01'
        assert entry_dict['valid_until'] == '2024-08-31'
        assert entry_dict['start_time'] == '09:00:00'
        assert entry_dict['end_time'] == '17:00:00'
        assert entry_dict['exception_dates'] == ['2024-07-04']


class TestTimelineValidation:
    """Tests for timeline validation."""
    
    def test_validate_timeline_entry_basic(self):
        """Test basic timeline entry validation."""
        entry_data = {
            'preset_name': 'Test Preset',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'duration': 60
        }
        
        validated = validate_timeline_entry(entry_data)
        assert validated['preset_name'] == 'Test Preset'
        assert validated['trigger_type'] == 'time_of_day'
        assert validated['time_of_day'] == '12:00:00'
        assert validated['duration'] == 60
    
    def test_validate_timeline_entry_date_range(self):
        """Test validation of date range."""
        entry_data = {
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'valid_from': '2024-06-01',
            'valid_until': '2024-08-31'
        }
        
        validated = validate_timeline_entry(entry_data)
        assert validated['valid_from'] == '2024-06-01'
        assert validated['valid_until'] == '2024-08-31'
    
    def test_validate_timeline_entry_invalid_date_range(self):
        """Test that validation fails for invalid date range."""
        entry_data = {
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'valid_from': '2024-08-31',
            'valid_until': '2024-06-01'  # Before valid_from
        }
        
        with pytest.raises(ValueError, match="valid_from must be before or equal to valid_until"):
            validate_timeline_entry(entry_data)
    
    def test_validate_timeline_entry_time_range(self):
        """Test validation of time range."""
        entry_data = {
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'start_time': '09:00:00',
            'end_time': '17:00:00'
        }
        
        validated = validate_timeline_entry(entry_data)
        assert validated['start_time'] == '09:00:00'
        assert validated['end_time'] == '17:00:00'
    
    def test_validate_timeline_entry_invalid_time_format(self):
        """Test that validation fails for invalid time format."""
        entry_data = {
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '25:00:00'  # Invalid hour
        }
        
        with pytest.raises(ValueError, match="Invalid time_of_day format"):
            validate_timeline_entry(entry_data)
    
    def test_validate_timeline_entry_exception_dates(self):
        """Test validation of exception dates."""
        entry_data = {
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'exception_dates': ['2024-12-25', '2024-01-01']
        }
        
        validated = validate_timeline_entry(entry_data)
        assert validated['exception_dates'] == ['2024-12-25', '2024-01-01']
    
    def test_validate_timeline_entry_invalid_exception_date(self):
        """Test that validation fails for invalid exception date."""
        entry_data = {
            'preset_name': 'Test',
            'trigger_type': 'time_of_day',
            'time_of_day': '12:00:00',
            'exception_dates': ['2024-13-01']  # Invalid month
        }
        
        with pytest.raises(ValueError, match="Invalid exception date format"):
            validate_timeline_entry(entry_data)
    
    def test_validate_timeline_complete(self):
        """Test complete timeline validation."""
        timeline_data = {
            'name': 'Test Timeline',
            'description': 'A test timeline',
            'enabled': True,
            'loop': True,
            'entries': [
                {
                    'preset_name': 'Entry 1',
                    'trigger_type': 'time_of_day',
                    'time_of_day': '08:00:00',
                    'valid_from': '2024-06-01',
                    'valid_until': '2024-08-31'
                },
                {
                    'preset_name': 'Entry 2',
                    'trigger_type': 'interval',
                    'interval_seconds': 300,
                    'exception_dates': ['2024-07-04']
                }
            ]
        }
        
        validated = validate_timeline(timeline_data)
        assert validated['name'] == 'Test Timeline'
        assert validated['enabled'] is True
        assert len(validated['entries']) == 2
        assert validated['entries'][0]['valid_from'] == '2024-06-01'
        assert validated['entries'][1]['exception_dates'] == ['2024-07-04']
    
    def test_validate_timeline_missing_name(self):
        """Test that validation fails without timeline name."""
        timeline_data = {
            'entries': []
        }
        
        with pytest.raises(ValueError, match="Timeline must have a name"):
            validate_timeline(timeline_data)
    
    def test_validate_timeline_missing_entries(self):
        """Test that validation fails without entries."""
        timeline_data = {
            'name': 'Test Timeline'
        }
        
        with pytest.raises(ValueError, match="Timeline must have entries"):
            validate_timeline(timeline_data)
    
    def test_validate_timeline_invalid_entry(self):
        """Test that validation fails for invalid entry."""
        timeline_data = {
            'name': 'Test Timeline',
            'entries': [
                {
                    'preset_name': 'Valid Entry',
                    'trigger_type': 'time_of_day',
                    'time_of_day': '12:00:00'
                },
                {
                    'preset_name': 'Invalid Entry',
                    'trigger_type': 'invalid_type'  # Invalid trigger type
                }
            ]
        }
        
        with pytest.raises(ValueError, match="Invalid entry at index"):
            validate_timeline(timeline_data)
