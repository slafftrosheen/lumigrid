"""Timeline data structures for preset scheduling."""

import logging
import uuid
from datetime import datetime, time as dt_time, date as dt_date
from typing import List, Dict, Any, Optional
from enum import Enum

logger = logging.getLogger(__name__)


class TriggerType(Enum):
    """Timeline trigger types."""
    TIME_OF_DAY = "time_of_day"  # Specific time each day
    DATETIME = "datetime"         # Specific date and time
    INTERVAL = "interval"         # Regular interval
    MANUAL = "manual"             # Manual trigger only


class TimelineEntry:
    """Represents a single entry in a timeline."""
    
    def __init__(self, entry_config: Dict[str, Any]):
        """Initialize timeline entry.
        
        Args:
            entry_config: Entry configuration dictionary
        """
        self.id = entry_config.get('id', str(uuid.uuid4())[:8])
        self.preset_name = entry_config.get('preset_name', '')
        self.trigger_type = TriggerType(entry_config.get('trigger_type', 'time_of_day'))
        
        # Time-based triggers
        self.time_of_day = entry_config.get('time_of_day')  # "HH:MM:SS" format
        self.datetime = entry_config.get('datetime')        # ISO format datetime string
        self.interval_seconds = entry_config.get('interval_seconds', 0)
        
        # Optional: Days of week (for time_of_day trigger)
        self.days_of_week = entry_config.get('days_of_week', [0, 1, 2, 3, 4, 5, 6])  # 0=Monday
        
        # Calendar-based scheduling (RTC-enabled)
        # Date range: entry is only active within this date range
        self.valid_from = entry_config.get('valid_from')  # ISO date format "YYYY-MM-DD" or None
        self.valid_until = entry_config.get('valid_until')  # ISO date format "YYYY-MM-DD" or None
        
        # Time range: for time_of_day triggers, specify a time window
        self.start_time = entry_config.get('start_time')  # "HH:MM:SS" format or None
        self.end_time = entry_config.get('end_time')      # "HH:MM:SS" format or None
        
        # Exception dates: list of dates when this entry should NOT trigger
        self.exception_dates = entry_config.get('exception_dates', [])  # List of "YYYY-MM-DD" strings
        
        # Duration (0 = indefinite, until next entry)
        self.duration = entry_config.get('duration', 0)
        
        # Enabled flag
        self.enabled = entry_config.get('enabled', True)
        
        # Last triggered timestamp
        self.last_triggered = entry_config.get('last_triggered', 0)
    
    def should_trigger(self, current_time: datetime) -> bool:
        """Check if this entry should trigger at the given time.
        
        Args:
            current_time: Current datetime to check against
            
        Returns:
            True if entry should trigger, False otherwise
        """
        if not self.enabled:
            return False
        
        # Check calendar-based constraints
        if not self._check_calendar_constraints(current_time):
            return False
        
        if self.trigger_type == TriggerType.TIME_OF_DAY:
            return self._check_time_of_day(current_time)
        elif self.trigger_type == TriggerType.DATETIME:
            return self._check_datetime(current_time)
        elif self.trigger_type == TriggerType.INTERVAL:
            return self._check_interval(current_time)
        
        return False
    
    def _check_calendar_constraints(self, current_time: datetime) -> bool:
        """Check if current time meets calendar-based constraints.
        
        Args:
            current_time: Current datetime to check
            
        Returns:
            True if constraints are met, False otherwise
        """
        current_date = current_time.date()
        
        # Check date range
        if self.valid_from:
            try:
                valid_from_date = dt_date.fromisoformat(self.valid_from)
                if current_date < valid_from_date:
                    logger.debug(f"Entry {self.id}: current date {current_date} before valid_from {valid_from_date}")
                    return False
            except ValueError as e:
                logger.error(f"Error parsing valid_from date {self.valid_from}: {e}")
        
        if self.valid_until:
            try:
                valid_until_date = dt_date.fromisoformat(self.valid_until)
                if current_date > valid_until_date:
                    logger.debug(f"Entry {self.id}: current date {current_date} after valid_until {valid_until_date}")
                    return False
            except ValueError as e:
                logger.error(f"Error parsing valid_until date {self.valid_until}: {e}")
        
        # Check exception dates
        if self.exception_dates:
            current_date_str = current_date.isoformat()
            if current_date_str in self.exception_dates:
                logger.debug(f"Entry {self.id}: current date {current_date_str} is in exception_dates")
                return False
        
        # Check time range (for time_of_day triggers)
        if self.trigger_type == TriggerType.TIME_OF_DAY and (self.start_time or self.end_time):
            current_time_only = current_time.time()
            
            if self.start_time and self.end_time:
                try:
                    start_t = dt_time.fromisoformat(self.start_time)
                    end_t = dt_time.fromisoformat(self.end_time)
                    
                    # Handle time ranges that cross midnight
                    if start_t <= end_t:
                        # Normal range (e.g., 08:00 to 17:00)
                        if not (start_t <= current_time_only <= end_t):
                            logger.debug(f"Entry {self.id}: current time {current_time_only} outside range {start_t}-{end_t}")
                            return False
                    else:
                        # Range crosses midnight (e.g., 22:00 to 06:00)
                        if not (current_time_only >= start_t or current_time_only <= end_t):
                            logger.debug(f"Entry {self.id}: current time {current_time_only} outside midnight-crossing range {start_t}-{end_t}")
                            return False
                except ValueError as e:
                    logger.error(f"Error parsing time range: {e}")
            elif self.start_time:
                try:
                    start_t = dt_time.fromisoformat(self.start_time)
                    if current_time_only < start_t:
                        logger.debug(f"Entry {self.id}: current time {current_time_only} before start_time {start_t}")
                        return False
                except ValueError as e:
                    logger.error(f"Error parsing start_time {self.start_time}: {e}")
            elif self.end_time:
                try:
                    end_t = dt_time.fromisoformat(self.end_time)
                    if current_time_only > end_t:
                        logger.debug(f"Entry {self.id}: current time {current_time_only} after end_time {end_t}")
                        return False
                except ValueError as e:
                    logger.error(f"Error parsing end_time {self.end_time}: {e}")
        
        return True
    
    def _check_time_of_day(self, current_time: datetime) -> bool:
        """Check if time-of-day trigger matches."""
        if not self.time_of_day:
            return False
        
        # Check day of week
        if current_time.weekday() not in self.days_of_week:
            return False
        
        # Parse time string (HH:MM:SS)
        try:
            from datetime import datetime as dt
            target_time = dt_time.fromisoformat(self.time_of_day)
            current_time_only = current_time.time()
            
            # Check if current time matches (within 1 minute window)
            # Create datetime objects for both times to calculate difference
            today = current_time.date()
            target_dt = dt.combine(today, target_time)
            current_dt = dt.combine(today, current_time_only)
            time_diff = abs((current_dt - target_dt).total_seconds())
            
            # Trigger if within 60 seconds and not recently triggered
            if time_diff < 60:
                # Check if not triggered in last 2 minutes to avoid repeat triggers
                if (current_time.timestamp() - self.last_triggered) > 120:
                    return True
        except (ValueError, AttributeError) as e:
            logger.error(f"Error parsing time_of_day {self.time_of_day}: {e}")
        
        return False
    
    def _check_datetime(self, current_time: datetime) -> bool:
        """Check if datetime trigger matches."""
        if not self.datetime:
            return False
        
        try:
            target_dt = datetime.fromisoformat(self.datetime)
            
            # Check if current time matches (within 1 minute window)
            time_diff = abs((current_time - target_dt).total_seconds())
            
            if time_diff < 60:
                # Check if not recently triggered
                if (current_time.timestamp() - self.last_triggered) > 120:
                    return True
        except (ValueError, AttributeError) as e:
            logger.error(f"Error parsing datetime {self.datetime}: {e}")
        
        return False
    
    def _check_interval(self, current_time: datetime) -> bool:
        """Check if interval trigger should fire."""
        if self.interval_seconds <= 0:
            return False
        
        current_timestamp = current_time.timestamp()
        
        # Check if enough time has passed since last trigger
        if (current_timestamp - self.last_triggered) >= self.interval_seconds:
            return True
        
        return False
    
    def mark_triggered(self, timestamp: float):
        """Mark this entry as triggered.
        
        Args:
            timestamp: Timestamp when triggered
        """
        self.last_triggered = timestamp
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for API/config.
        
        Returns:
            Dictionary representation
        """
        return {
            'id': self.id,
            'preset_name': self.preset_name,
            'trigger_type': self.trigger_type.value,
            'time_of_day': self.time_of_day,
            'datetime': self.datetime,
            'interval_seconds': self.interval_seconds,
            'days_of_week': self.days_of_week,
            'valid_from': self.valid_from,
            'valid_until': self.valid_until,
            'start_time': self.start_time,
            'end_time': self.end_time,
            'exception_dates': self.exception_dates,
            'duration': self.duration,
            'enabled': self.enabled,
            'last_triggered': self.last_triggered
        }


class Timeline:
    """Represents a timeline with scheduled preset entries."""
    
    def __init__(self, timeline_config: Dict[str, Any]):
        """Initialize timeline.
        
        Args:
            timeline_config: Timeline configuration dictionary
        """
        self.id = timeline_config.get('id', str(uuid.uuid4())[:8])
        self.name = timeline_config.get('name', 'Untitled Timeline')
        self.description = timeline_config.get('description', '')
        self.enabled = timeline_config.get('enabled', False)
        self.loop = timeline_config.get('loop', False)
        
        # Load entries
        self.entries: List[TimelineEntry] = []
        entries_config = timeline_config.get('entries', [])
        for entry_config in entries_config:
            entry = TimelineEntry(entry_config)
            self.entries.append(entry)
        
        logger.debug(f"Timeline '{self.name}' initialized with {len(self.entries)} entries")
    
    def get_active_entry(self, current_time: datetime) -> Optional[TimelineEntry]:
        """Get the entry that should be active at the given time.
        
        Args:
            current_time: Current datetime
            
        Returns:
            Active TimelineEntry or None
        """
        if not self.enabled:
            return None
        
        # Check all entries for triggers
        triggered_entries = []
        for entry in self.entries:
            if entry.should_trigger(current_time):
                triggered_entries.append(entry)
        
        # Return the most recently triggered entry
        if triggered_entries:
            # Sort by last_triggered and return the latest
            return max(triggered_entries, key=lambda e: e.last_triggered if e.last_triggered > 0 else 0)
        
        return None
    
    def mark_entry_triggered(self, entry_id: str, timestamp: float) -> bool:
        """Mark an entry as triggered.
        
        Args:
            entry_id: ID of the entry to mark
            timestamp: Timestamp when triggered
            
        Returns:
            True if entry found and marked, False otherwise
        """
        for entry in self.entries:
            if entry.id == entry_id:
                entry.mark_triggered(timestamp)
                logger.debug(f"Entry '{entry.preset_name}' marked as triggered")
                return True
        return False
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for API/config.
        
        Returns:
            Dictionary representation
        """
        return {
            'id': self.id,
            'name': self.name,
            'description': self.description,
            'enabled': self.enabled,
            'loop': self.loop,
            'entries': [entry.to_dict() for entry in self.entries]
        }
