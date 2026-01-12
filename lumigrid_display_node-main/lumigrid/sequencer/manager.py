"""Sequencer manager for timeline-based playback."""

import logging
import time
import threading
from datetime import datetime
from typing import List, Dict, Any, Optional, Callable

from .timeline import Timeline, TimelineEntry

logger = logging.getLogger(__name__)


class SequencerManager:
    """Manages timeline-based preset sequencing."""
    
    def __init__(self, config: Dict[str, Any], play_preset_callback: Callable[[str], bool], rtc_manager: Optional[Any] = None):
        """Initialize sequencer manager.
        
        Args:
            config: Configuration dictionary
            play_preset_callback: Callback function to play a preset by name
            rtc_manager: Optional RTCManager instance for RTC-based time
        """
        self.config = config
        self.play_preset = play_preset_callback
        self.rtc_manager = rtc_manager
        
        # Timelines
        self.timelines: List[Timeline] = []
        
        # Current active timeline and entry
        self.active_timeline: Optional[Timeline] = None
        self.active_entry: Optional[TimelineEntry] = None
        
        # Threading
        self.running = False
        self.update_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        
        # Load timelines from config
        self._load_timelines()
    
    def _load_timelines(self):
        """Load timelines from configuration."""
        timelines_config = self.config.get('timelines', [])
        
        with self._lock:
            self.timelines = []
            for timeline_config in timelines_config:
                timeline = Timeline(timeline_config)
                self.timelines.append(timeline)
                logger.debug(f"Loaded timeline: {timeline.name}")
        
        logger.info(f"Loaded {len(self.timelines)} timelines")
    
    def reload_config(self, new_config: Dict[str, Any]):
        """Reload configuration.
        
        Args:
            new_config: New configuration dictionary
        """
        self.config = new_config
        self._load_timelines()
    
    def start(self):
        """Start the sequencer."""
        if self.running:
            logger.warning("Sequencer already running")
            return
        
        self.running = True
        self.update_thread = threading.Thread(target=self._update_loop, daemon=True)
        self.update_thread.start()
        logger.info("Sequencer started")
    
    def stop(self):
        """Stop the sequencer."""
        self.running = False
        if self.update_thread:
            self.update_thread.join(timeout=2)
        logger.info("Sequencer stopped")
    
    def _get_current_time(self) -> datetime:
        """Get current time from RTC if available, otherwise use system time.
        
        Returns:
            Current datetime
        """
        if self.rtc_manager:
            rtc_time = self.rtc_manager.get_time()
            if rtc_time:
                logger.debug(f"Using RTC time: {rtc_time}")
                return rtc_time
        
        # Fall back to system time
        return datetime.now()
    
    def _update_loop(self):
        """Main update loop for checking timeline triggers."""
        while self.running:
            try:
                current_time = self._get_current_time()
                
                with self._lock:
                    # Check all enabled timelines for triggers
                    for timeline in self.timelines:
                        if not timeline.enabled:
                            continue
                        
                        # Check for triggered entries
                        for entry in timeline.entries:
                            if entry.should_trigger(current_time):
                                # Mark as triggered
                                entry.mark_triggered(current_time.timestamp())
                                
                                # Activate this timeline and entry
                                self.active_timeline = timeline
                                self.active_entry = entry
                                
                                # Play the preset
                                logger.info(f"Timeline trigger: Playing preset '{entry.preset_name}' from timeline '{timeline.name}'")
                                try:
                                    self.play_preset(entry.preset_name)
                                except Exception as e:
                                    logger.error(f"Error playing preset '{entry.preset_name}': {e}")
                
                # Sleep for 1 second before next check
                time.sleep(1)
                
            except Exception as e:
                logger.error(f"Error in sequencer update loop: {e}")
                time.sleep(1)
    
    def get_timelines(self) -> List[Dict[str, Any]]:
        """Get all timelines.
        
        Returns:
            List of timeline dictionaries
        """
        with self._lock:
            return [t.to_dict() for t in self.timelines]
    
    def get_timeline(self, timeline_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific timeline.
        
        Args:
            timeline_id: ID of the timeline
            
        Returns:
            Timeline dictionary or None
        """
        with self._lock:
            for timeline in self.timelines:
                if timeline.id == timeline_id:
                    return timeline.to_dict()
        return None
    
    def add_timeline(self, timeline_data: Dict[str, Any]) -> Optional[str]:
        """Add a new timeline.
        
        Args:
            timeline_data: Timeline data dictionary
            
        Returns:
            Timeline ID if successful, None otherwise
        """
        try:
            timeline = Timeline(timeline_data)
            
            with self._lock:
                self.timelines.append(timeline)
            
            logger.info(f"Added timeline: {timeline.name}")
            return timeline.id
        except Exception as e:
            logger.error(f"Error adding timeline: {e}")
            return None
    
    def update_timeline(self, timeline_id: str, timeline_data: Dict[str, Any]) -> bool:
        """Update an existing timeline.
        
        Args:
            timeline_id: ID of the timeline to update
            timeline_data: New timeline data
            
        Returns:
            True if successful, False otherwise
        """
        with self._lock:
            for i, timeline in enumerate(self.timelines):
                if timeline.id == timeline_id:
                    # Preserve ID
                    timeline_data['id'] = timeline_id
                    new_timeline = Timeline(timeline_data)
                    self.timelines[i] = new_timeline
                    
                    # Update active references if needed
                    if self.active_timeline and self.active_timeline.id == timeline_id:
                        self.active_timeline = new_timeline
                    
                    logger.info(f"Updated timeline: {new_timeline.name}")
                    return True
        
        logger.warning(f"Timeline not found: {timeline_id}")
        return False
    
    def delete_timeline(self, timeline_id: str) -> bool:
        """Delete a timeline.
        
        Args:
            timeline_id: ID of the timeline to delete
            
        Returns:
            True if successful, False otherwise
        """
        with self._lock:
            for i, timeline in enumerate(self.timelines):
                if timeline.id == timeline_id:
                    # Clear active references if needed
                    if self.active_timeline and self.active_timeline.id == timeline_id:
                        self.active_timeline = None
                        self.active_entry = None
                    
                    del self.timelines[i]
                    logger.info(f"Deleted timeline: {timeline.name}")
                    return True
        
        logger.warning(f"Timeline not found: {timeline_id}")
        return False
    
    def activate_timeline(self, timeline_id: str) -> bool:
        """Activate a timeline.
        
        Args:
            timeline_id: ID of the timeline to activate
            
        Returns:
            True if successful, False otherwise
        """
        with self._lock:
            for timeline in self.timelines:
                if timeline.id == timeline_id:
                    timeline.enabled = True
                    logger.info(f"Activated timeline: {timeline.name}")
                    return True
        
        logger.warning(f"Timeline not found: {timeline_id}")
        return False
    
    def deactivate_timeline(self, timeline_id: str) -> bool:
        """Deactivate a timeline.
        
        Args:
            timeline_id: ID of the timeline to deactivate
            
        Returns:
            True if successful, False otherwise
        """
        with self._lock:
            for timeline in self.timelines:
                if timeline.id == timeline_id:
                    timeline.enabled = False
                    
                    # Clear active references if this is the active timeline
                    if self.active_timeline and self.active_timeline.id == timeline_id:
                        self.active_timeline = None
                        self.active_entry = None
                    
                    logger.info(f"Deactivated timeline: {timeline.name}")
                    return True
        
        logger.warning(f"Timeline not found: {timeline_id}")
        return False
    
    def get_status(self) -> Dict[str, Any]:
        """Get current sequencer status.
        
        Returns:
            Status dictionary
        """
        with self._lock:
            status = {
                'running': self.running,
                'active_timeline': self.active_timeline.name if self.active_timeline else None,
                'active_timeline_id': self.active_timeline.id if self.active_timeline else None,
                'active_entry': self.active_entry.preset_name if self.active_entry else None,
                'timeline_count': len(self.timelines),
                'enabled_timeline_count': sum(1 for t in self.timelines if t.enabled)
            }
        
        return status
