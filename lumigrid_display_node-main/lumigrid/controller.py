"""Application controller for Lumigrid Display Node."""

import logging
import time
import threading
from typing import Optional, Dict, Any, List

from lumigrid.config import Config
from lumigrid.display.matrix import DisplayManager
from lumigrid.display.modes import ClockMode, TemperatureMode, MessageMode, SlideshowMode
from lumigrid.display.frames import FrameMode, FrameManager
from lumigrid.sensors.temperature import create_temperature_sensor, DHTSensor
from lumigrid.sensors.rtc import RTCManager
from lumigrid.sync.protocol import SyncProtocol
from lumigrid.sequencer.manager import SequencerManager
from lumigrid.constants import VALID_CONTROL_MODES

logger = logging.getLogger(__name__)


class ApplicationController:
    """Main application controller."""

    def __init__(self, config_path: str = "config.yaml"):
        """Initialize the application controller.

        Args:
            config_path: Path to configuration file
        """
        self.config = Config(config_path)
        self.running = False
        self.update_thread = None
        
        # Thread synchronization
        self._mode_lock = threading.Lock()
        
        # Control mode and sync protocol
        self.control_mode = self.config.get('control.mode', 'independent')
        self.sync_protocol = None
        self._total_sync_ticks = 0
        
        # Initialize components
        self.display_manager = None
        self.temp_sensor = None
        self.rtc_manager = None
        self.current_mode = None
        self.message_mode = None
        self.frame_mode = None
        
        # Display modes
        self.modes = {}
        
        # Sensor data storage
        self._sensor_data = {}
        
        # Sequencer
        self.sequencer = None
        
        self._initialize_components()

    def _initialize_components(self):
        """Initialize all hardware components and display modes."""
        # Initialize display
        led_config = self.config.get('led_matrix', {})
        self.display_manager = DisplayManager(led_config)
        logger.info("Display manager initialized")

        # Initialize temperature sensor
        temp_config = self.config.get('temperature', {})
        if temp_config.get('enabled', True):
            try:
                self.temp_sensor = create_temperature_sensor(temp_config)
                logger.info("Temperature sensor initialized")
            except Exception as e:
                logger.error(f"Error initializing temperature sensor: {e}")
                self.temp_sensor = None

        # Initialize RTC
        rtc_config = self.config.get('rtc', {})
        if rtc_config.get('enabled', True):
            try:
                self.rtc_manager = RTCManager(rtc_config)
                logger.info("RTC manager initialized")
            except Exception as e:
                logger.error(f"Error initializing RTC: {e}")
                self.rtc_manager = None

        # Initialize display modes
        self._initialize_modes()
        
        # Initialize sequencer
        self._initialize_sequencer()
        
        # Initialize control mode (sync protocol)
        self._initialize_control_mode()

    def _get_humidity(self) -> Optional[float]:
        """Get humidity from DHT sensor if available."""
        if self.temp_sensor and isinstance(self.temp_sensor, DHTSensor):
            return self.temp_sensor.get_humidity()
        return None

    def _initialize_modes(self):
        """Initialize display modes."""
        config_dict = self.config.config
        
        # Clock mode
        self.modes['clock'] = ClockMode(self.display_manager, config_dict)
        
        # Temperature mode
        self.modes['temperature'] = TemperatureMode(
            self.display_manager, config_dict, self.temp_sensor
        )
        
        # Message mode
        self.message_mode = MessageMode(self.display_manager, config_dict)
        self.modes['message'] = self.message_mode
        
        # Frame mode (new rotating content system)
        self.frame_mode = FrameMode(
            self.display_manager, 
            config_dict, 
            self.temp_sensor,
            self._get_humidity
        )
        self.modes['frames'] = self.frame_mode
        
        # Slideshow mode (cycles through other modes)
        slideshow_modes = [
            self.modes['clock'],
            self.modes['temperature']
        ]
        self.modes['slideshow'] = SlideshowMode(
            self.display_manager, config_dict, slideshow_modes
        )
        
        # Set default mode
        default_mode = self.config.get('display.default_mode', 'frames')
        self.current_mode = self.modes.get(default_mode, self.modes.get('frames') or self.modes['clock'])
        logger.info(f"Default mode set to: {default_mode}")

    def _initialize_control_mode(self):
        """Initialize control mode and sync protocol if needed."""
        self.control_mode = self.config.get('control.mode', 'independent')
        logger.info(f"Control mode: {self.control_mode}")
        
        if self.control_mode == 'sync':
            # Initialize sync protocol
            self.sync_protocol = SyncProtocol()
            sync_role = self.config.get('control.sync_role', 'slave')
            
            if sync_role == 'master':
                logger.info("Starting as sync master")
                self.sync_protocol.start_master()
            else:
                logger.info("Starting as sync slave")
                self.sync_protocol.start_slave()
                # Register sync callbacks
                self.sync_protocol.on_tick(self._on_sync_tick)
        elif self.control_mode == 'external':
            external_source = self.config.get('control.external_source', 'mqtt')
            logger.info(f"External control mode enabled (source: {external_source}).")
            # Placeholder for future MQTT/Art-Net implementation
            logger.warning("External control sources (MQTT/Art-Net) are not yet implemented")
        else:
            logger.info("Independent control mode enabled.")
    
    def _initialize_sequencer(self):
        """Initialize timeline-based sequencer."""
        try:
            self.sequencer = SequencerManager(
                self.config.config,
                self.play_preset,
                self.rtc_manager  # Pass RTC manager for accurate time
            )
            logger.info("Sequencer initialized")
        except Exception as e:
            logger.error(f"Error initializing sequencer: {e}")
            self.sequencer = None
    
    def _on_sync_tick(self, timestamp_ms: int, drift_ewma: float):
        """Callback for sync protocol tick events.
        
        Args:
            timestamp_ms: Timestamp from master in milliseconds
            drift_ewma: Exponentially weighted moving average of drift
        """
        # Log periodic sync events (every 20 ticks = 1 second at 20Hz)
        if self._total_sync_ticks % 20 == 0:
            logger.debug(f"Sync tick: timestamp={timestamp_ms}, drift={drift_ewma:.1f}ms")
        
        self._total_sync_ticks += 1
        
        # This is where synchronized display updates would be triggered
        # For now, this logs the sync event for monitoring
        # Future implementation could trigger sequencer events or frame transitions

    def start(self):
        """Start the application."""
        if self.running:
            logger.warning("Application already running")
            return

        self.running = True
        
        # Start update thread
        self.update_thread = threading.Thread(target=self._update_loop, daemon=True)
        self.update_thread.start()
        
        # Start sequencer
        if self.sequencer:
            self.sequencer.start()
        
        logger.info("Application started")

    def stop(self):
        """Stop the application."""
        self.running = False
        
        if self.update_thread:
            self.update_thread.join(timeout=2)
        
        # Stop sequencer
        if self.sequencer:
            self.sequencer.stop()
        
        # Stop sync protocol if running
        if self.sync_protocol:
            self.sync_protocol.stop()
            self.sync_protocol = None
            logger.info("Sync protocol stopped")
        
        if self.display_manager:
            self.display_manager.cleanup()
        
        logger.info("Application stopped")

    def _update_loop(self):
        """Main update loop."""
        while self.running:
            try:
                # Check RTC sync
                if self.rtc_manager and self.rtc_manager.should_sync():
                    self.rtc_manager.sync_system_time()
                
                # Update display (with lock to prevent race conditions)
                with self._mode_lock:
                    if self.current_mode:
                        self.current_mode.update()
                
                # Small delay to prevent CPU spinning
                time.sleep(0.1)
                
            except Exception as e:
                logger.error(f"Error in update loop: {e}")
                time.sleep(1)

    def set_mode(self, mode_name: str):
        """Set the current display mode.

        Args:
            mode_name: Name of the mode to activate
        """
        if mode_name not in self.modes:
            logger.warning(f"Unknown mode: {mode_name}")
            return

        # Use lock to prevent race conditions with update loop
        with self._mode_lock:
            self.current_mode = self.modes[mode_name]
            logger.info(f"Mode changed to: {mode_name}")
            
            # Clear display before switching modes
            if self.display_manager:
                self.display_manager.clear()

    def get_current_mode(self) -> str:
        """Get the current display mode name.

        Returns:
            Current mode name
        """
        for name, mode in self.modes.items():
            if mode == self.current_mode:
                return name
        return 'unknown'

    def set_message(self, message: str):
        """Set custom message.

        Args:
            message: Message to display
        """
        with self._mode_lock:
            if self.message_mode:
                self.message_mode.set_message(message)
                logger.info(f"Message set: {message}")

    def set_brightness(self, brightness: int):
        """Set display brightness.

        Args:
            brightness: Brightness value (0-100)
        """
        with self._mode_lock:
            if self.display_manager:
                self.display_manager.set_brightness(brightness)
                self.config.set('led_matrix.brightness', brightness)
                self.config.save()
                logger.info(f"Brightness set to: {brightness}")

    def get_brightness(self) -> int:
        """Get current brightness.

        Returns:
            Current brightness value
        """
        return self.config.get('led_matrix.brightness', 50)

    def get_temperature(self) -> Optional[float]:
        """Get current temperature.

        Returns:
            Temperature in Celsius, or None if unavailable
        """
        if self.temp_sensor:
            return self.temp_sensor.get_temperature()
        return None

    def get_humidity(self) -> Optional[float]:
        """Get current humidity.

        Returns:
            Humidity percentage, or None if unavailable
        """
        return self._get_humidity()

    def get_status(self) -> Dict[str, Any]:
        """Get current status.

        Returns:
            Status dictionary
        """
        status = {
            'mode': self.get_current_mode(),
            'brightness': self.get_brightness(),
            'temperature': self.get_temperature(),
            'humidity': self.get_humidity(),
            'running': self.running,
            'available_modes': list(self.modes.keys()),
            'control_mode': self.control_mode,
            'control_settings': {
                'mode': self.control_mode,
                'sync_role': self.config.get('control.sync_role', 'slave'),
                'external_source': self.config.get('control.external_source', 'mqtt')
            }
        }
        
        # Add sync protocol status if running
        if self.sync_protocol:
            status['sync_status'] = self.sync_protocol.get_status()
        
        if self.rtc_manager:
            rtc_time = self.rtc_manager.get_time()
            status['rtc_time'] = rtc_time.isoformat() if rtc_time else None
        
        # Add frame info if in frames mode
        if self.frame_mode and self.get_current_mode() == 'frames':
            current_frame = self.frame_mode.frame_manager.get_current_frame()
            if current_frame:
                status['current_frame'] = {
                    'id': current_frame.id,
                    'name': current_frame.name,
                    'type': current_frame.type
                }
        
        return status

    def get_config(self) -> Dict[str, Any]:
        """Get current configuration.

        Returns:
            Configuration dictionary
        """
        return self.config.config

    def update_config(self, new_config: Dict[str, Any]):
        """Update configuration.

        Args:
            new_config: New configuration values
        """
        for key, value in new_config.items():
            self.config.set(key, value)
        
        self.config.save()
        logger.info("Configuration updated")

    def update_led_matrix_config(self, led_config: Dict[str, Any]):
        """Update LED matrix configuration.

        Args:
            led_config: New LED matrix settings
        """
        current = self.config.get('led_matrix', {})
        current.update(led_config)
        self.config.set('led_matrix', current)
        self.config.save()
        logger.info("LED matrix configuration updated")

    # Frame management methods
    def get_frames(self) -> List[Dict[str, Any]]:
        """Get all frames."""
        return self.config.get_frames()

    def get_frame(self, frame_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific frame."""
        return self.config.get_frame(frame_id)

    def add_frame(self, frame_data: Dict[str, Any]) -> bool:
        """Add a new frame."""
        result = self.config.add_frame(frame_data)
        if result and self.frame_mode:
            self.frame_mode.reload_config(self.config.config)
        return result

    def update_frame(self, frame_id: str, frame_data: Dict[str, Any]) -> bool:
        """Update an existing frame."""
        result = self.config.update_frame(frame_id, frame_data)
        if result and self.frame_mode:
            self.frame_mode.reload_config(self.config.config)
        return result

    def delete_frame(self, frame_id: str) -> bool:
        """Delete a frame."""
        result = self.config.delete_frame(frame_id)
        if result and self.frame_mode:
            self.frame_mode.reload_config(self.config.config)
        return result

    def reorder_frames(self, frame_ids: List[str]) -> bool:
        """Reorder frames."""
        result = self.config.reorder_frames(frame_ids)
        if result and self.frame_mode:
            self.frame_mode.reload_config(self.config.config)
        return result

    def toggle_frame(self, frame_id: str, enabled: bool) -> bool:
        """Enable or disable a frame."""
        result = self.config.toggle_frame(frame_id, enabled)
        if result and self.frame_mode:
            self.frame_mode.reload_config(self.config.config)
        return result

    # Font management methods
    def get_fonts(self) -> List[Dict[str, str]]:
        """Get installed fonts."""
        return self.config.get_installed_fonts()

    def upload_font(self, filename: str, font_data: bytes) -> Optional[str]:
        """Upload a custom font."""
        return self.config.add_custom_font(filename, font_data)

    def delete_font(self, font_path: str) -> bool:
        """Delete a custom font."""
        return self.config.delete_custom_font(font_path)

    def reload_config(self):
        """Reload configuration from file."""
        self.config.reload()
        if self.frame_mode:
            self.frame_mode.reload_config(self.config.config)
        logger.info("Configuration reloaded")

    def get_control_mode(self) -> str:
        """Get the current control mode.
        
        Returns:
            Current control mode (external, sync, or independent)
        """
        return self.control_mode
    
    def get_control_settings(self) -> Dict[str, Any]:
        """Get control mode settings.
        
        Returns:
            Dictionary with control mode settings
        """
        return {
            'mode': self.control_mode,
            'sync_role': self.config.get('control.sync_role', 'slave'),
            'external_source': self.config.get('control.external_source', 'mqtt')
        }
    
    def set_control_mode(self, mode: str, sync_role: Optional[str] = None, 
                        external_source: Optional[str] = None) -> bool:
        """Set the control mode.
        
        Args:
            mode: Control mode to set (external, sync, or independent)
            sync_role: Optional sync role (master or slave), only used when mode is 'sync'
            external_source: Optional external source (mqtt or artnet), only used when mode is 'external'
            
        Returns:
            True if successful, False otherwise
        """
        if mode not in VALID_CONTROL_MODES:
            logger.error(f"Invalid control mode: {mode}")
            return False
        
        # Use lock to prevent race conditions during mode switching
        with self._mode_lock:
            # Stop existing sync protocol if running
            if self.sync_protocol:
                self.sync_protocol.stop()
                self.sync_protocol = None
            
            # Update configuration
            self.config.set('control.mode', mode)
            if sync_role and mode == 'sync':
                self.config.set('control.sync_role', sync_role)
            if external_source and mode == 'external':
                self.config.set('control.external_source', external_source)
            
            self.config.save()
            
            # Update current control mode
            self.control_mode = mode
            
            # Reinitialize control mode
            self._initialize_control_mode()
        
        logger.info(f"Control mode set to: {mode}")
        return True

    def update_sensor_data(self, data: dict):
        """Store sensor data for display modes.
        
        Args:
            data: Sensor data dictionary containing node_id, readings, and optional timestamp
        """
        self._sensor_data[data['node_id']] = {
            'timestamp': data.get('timestamp', time.time()),
            'readings': data['readings']
        }
        # Trigger display update if in sensor mode
        if self.current_mode == 'sensor':
            self._update_sensor_display()

    def _update_sensor_display(self):
        """Update display with sensor data."""
        # This is a placeholder for future sensor display mode implementation
        # When a dedicated sensor mode is created, this will trigger its update
        pass

    def play_preset(self, name: str) -> bool:
        """Play a preset (frame) by name.
        
        Args:
            name: Name of the preset/frame to play
            
        Returns:
            True if preset found and activated, False otherwise
        """
        frames = self.get_frames()
        for frame in frames:
            if frame.get('name') == name:
                # Switch to frames mode if not already
                if self.get_current_mode() != 'frames':
                    self.set_mode('frames')
                
                # Verify frame_mode is available
                if not self.frame_mode:
                    logger.error("Frame mode not available")
                    return False
                
                # Enable the specific frame and set it as current
                frame_id = frame.get('id')
                if frame_id:
                    # Jump to the specified frame
                    if self.frame_mode.frame_manager.jump_to_frame(frame_id):
                        logger.info(f"Playing preset: {name}")
                        return True
        
        logger.warning(f"Preset not found: {name}")
        return False

    # Timeline management methods
    def get_timelines(self) -> List[Dict[str, Any]]:
        """Get all timelines.
        
        Returns:
            List of timeline dictionaries
        """
        if self.sequencer:
            return self.sequencer.get_timelines()
        return []

    def get_timeline(self, timeline_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific timeline.
        
        Args:
            timeline_id: ID of the timeline
            
        Returns:
            Timeline dictionary or None
        """
        if self.sequencer:
            return self.sequencer.get_timeline(timeline_id)
        return None

    def add_timeline(self, timeline_data: Dict[str, Any]) -> Optional[str]:
        """Add a new timeline.
        
        Args:
            timeline_data: Timeline data dictionary
            
        Returns:
            Timeline ID if successful, None otherwise
        """
        if self.sequencer:
            timeline_id = self.sequencer.add_timeline(timeline_data)
            if timeline_id:
                # Save to config
                self._save_timelines_to_config()
            return timeline_id
        return None

    def update_timeline(self, timeline_id: str, timeline_data: Dict[str, Any]) -> bool:
        """Update an existing timeline.
        
        Args:
            timeline_id: ID of the timeline to update
            timeline_data: New timeline data
            
        Returns:
            True if successful, False otherwise
        """
        if self.sequencer:
            result = self.sequencer.update_timeline(timeline_id, timeline_data)
            if result:
                # Save to config
                self._save_timelines_to_config()
            return result
        return False

    def delete_timeline(self, timeline_id: str) -> bool:
        """Delete a timeline.
        
        Args:
            timeline_id: ID of the timeline to delete
            
        Returns:
            True if successful, False otherwise
        """
        if self.sequencer:
            result = self.sequencer.delete_timeline(timeline_id)
            if result:
                # Save to config
                self._save_timelines_to_config()
            return result
        return False

    def activate_timeline(self, timeline_id: str) -> bool:
        """Activate a timeline.
        
        Args:
            timeline_id: ID of the timeline to activate
            
        Returns:
            True if successful, False otherwise
        """
        if self.sequencer:
            result = self.sequencer.activate_timeline(timeline_id)
            if result:
                # Save to config
                self._save_timelines_to_config()
            return result
        return False

    def deactivate_timeline(self, timeline_id: str) -> bool:
        """Deactivate a timeline.
        
        Args:
            timeline_id: ID of the timeline to deactivate
            
        Returns:
            True if successful, False otherwise
        """
        if self.sequencer:
            result = self.sequencer.deactivate_timeline(timeline_id)
            if result:
                # Save to config
                self._save_timelines_to_config()
            return result
        return False

    def get_sequencer_status(self) -> Dict[str, Any]:
        """Get sequencer status.
        
        Returns:
            Sequencer status dictionary
        """
        if self.sequencer:
            return self.sequencer.get_status()
        return {'running': False, 'active_timeline': None}

    def _save_timelines_to_config(self):
        """Save timelines to configuration file."""
        if self.sequencer:
            timelines = self.sequencer.get_timelines()
            self.config.set('timelines', timelines)
            self.config.save()
            logger.debug("Timelines saved to config")

    # ==================== CHANNEL GROUP MANAGEMENT ====================

    def get_channel_groups(self) -> List[Dict[str, Any]]:
        """Get all channel groups.
        
        Returns:
            List of channel group dictionaries
        """
        return self.config.get('channel_groups', [])

    def get_channel_group(self, group_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific channel group.
        
        Args:
            group_id: ID of the channel group
            
        Returns:
            Channel group dictionary or None
        """
        groups = self.get_channel_groups()
        for group in groups:
            if group.get('id') == group_id:
                return group
        return None

    def add_channel_group(self, group_data: Dict[str, Any]) -> Optional[str]:
        """Add a new channel group.
        
        Args:
            group_data: Channel group data dictionary
            
        Returns:
            Group ID if successful, None otherwise
        """
        from lumigrid.validators import validate_channel_group_schema
        try:
            validated_data = validate_channel_group_schema(group_data)
            groups = self.get_channel_groups()
            groups.append(validated_data)
            self.config.set('channel_groups', groups)
            self.config.save()
            return validated_data['id']
        except Exception as e:
            logger.error(f"Error adding channel group: {e}")
            return None

    def update_channel_group(self, group_id: str, group_data: Dict[str, Any]) -> bool:
        """Update an existing channel group.
        
        Args:
            group_id: ID of the group to update
            group_data: New group data
            
        Returns:
            True if successful, False otherwise
        """
        from lumigrid.validators import validate_channel_group_schema
        try:
            validated_data = validate_channel_group_schema(group_data)
            groups = self.get_channel_groups()
            for i, group in enumerate(groups):
                if group.get('id') == group_id:
                    validated_data['id'] = group_id
                    groups[i] = validated_data
                    self.config.set('channel_groups', groups)
                    self.config.save()
                    return True
            return False
        except Exception as e:
            logger.error(f"Error updating channel group: {e}")
            return False

    def delete_channel_group(self, group_id: str) -> bool:
        """Delete a channel group.
        
        Args:
            group_id: ID of the group to delete
            
        Returns:
            True if successful, False otherwise
        """
        groups = self.get_channel_groups()
        for i, group in enumerate(groups):
            if group.get('id') == group_id:
                groups.pop(i)
                self.config.set('channel_groups', groups)
                self.config.save()
                return True
        return False

    # ==================== PRESET LINK MANAGEMENT ====================

    def get_preset_links(self) -> List[Dict[str, Any]]:
        """Get all preset links.
        
        Returns:
            List of preset link dictionaries
        """
        return self.config.get('preset_links', [])

    def get_preset_link(self, link_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific preset link.
        
        Args:
            link_id: ID of the preset link
            
        Returns:
            Preset link dictionary or None
        """
        links = self.get_preset_links()
        for link in links:
            if link.get('id') == link_id:
                return link
        return None

    def add_preset_link(self, link_data: Dict[str, Any]) -> Optional[str]:
        """Add a new preset link.
        
        Args:
            link_data: Preset link data dictionary
            
        Returns:
            Link ID if successful, None otherwise
        """
        from lumigrid.validators import validate_preset_link_schema
        try:
            validated_data = validate_preset_link_schema(link_data)
            links = self.get_preset_links()
            links.append(validated_data)
            self.config.set('preset_links', links)
            self.config.save()
            return validated_data['id']
        except Exception as e:
            logger.error(f"Error adding preset link: {e}")
            return None

    def update_preset_link(self, link_id: str, link_data: Dict[str, Any]) -> bool:
        """Update an existing preset link.
        
        Args:
            link_id: ID of the link to update
            link_data: New link data
            
        Returns:
            True if successful, False otherwise
        """
        from lumigrid.validators import validate_preset_link_schema
        try:
            validated_data = validate_preset_link_schema(link_data)
            links = self.get_preset_links()
            for i, link in enumerate(links):
                if link.get('id') == link_id:
                    validated_data['id'] = link_id
                    links[i] = validated_data
                    self.config.set('preset_links', links)
                    self.config.save()
                    return True
            return False
        except Exception as e:
            logger.error(f"Error updating preset link: {e}")
            return False

    def delete_preset_link(self, link_id: str) -> bool:
        """Delete a preset link.
        
        Args:
            link_id: ID of the link to delete
            
        Returns:
            True if successful, False otherwise
        """
        links = self.get_preset_links()
        for i, link in enumerate(links):
            if link.get('id') == link_id:
                links.pop(i)
                self.config.set('preset_links', links)
                self.config.save()
                return True
        return False

    # ==================== CONDITIONAL LOGIC MANAGEMENT ====================

    def get_conditional_rules(self) -> List[Dict[str, Any]]:
        """Get all conditional logic rules.
        
        Returns:
            List of conditional rule dictionaries
        """
        return self.config.get('conditional_rules', [])

    def get_conditional_rule(self, rule_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific conditional rule.
        
        Args:
            rule_id: ID of the conditional rule
            
        Returns:
            Conditional rule dictionary or None
        """
        rules = self.get_conditional_rules()
        for rule in rules:
            if rule.get('id') == rule_id:
                return rule
        return None

    def add_conditional_rule(self, rule_data: Dict[str, Any]) -> Optional[str]:
        """Add a new conditional rule.
        
        Args:
            rule_data: Conditional rule data dictionary
            
        Returns:
            Rule ID if successful, None otherwise
        """
        from lumigrid.validators import validate_conditional_logic_schema
        try:
            validated_data = validate_conditional_logic_schema(rule_data)
            rules = self.get_conditional_rules()
            rules.append(validated_data)
            self.config.set('conditional_rules', rules)
            self.config.save()
            return validated_data['id']
        except Exception as e:
            logger.error(f"Error adding conditional rule: {e}")
            return None

    def update_conditional_rule(self, rule_id: str, rule_data: Dict[str, Any]) -> bool:
        """Update an existing conditional rule.
        
        Args:
            rule_id: ID of the rule to update
            rule_data: New rule data
            
        Returns:
            True if successful, False otherwise
        """
        from lumigrid.validators import validate_conditional_logic_schema
        try:
            validated_data = validate_conditional_logic_schema(rule_data)
            rules = self.get_conditional_rules()
            for i, rule in enumerate(rules):
                if rule.get('id') == rule_id:
                    validated_data['id'] = rule_id
                    rules[i] = validated_data
                    self.config.set('conditional_rules', rules)
                    self.config.save()
                    return True
            return False
        except Exception as e:
            logger.error(f"Error updating conditional rule: {e}")
            return False

    def delete_conditional_rule(self, rule_id: str) -> bool:
        """Delete a conditional rule.
        
        Args:
            rule_id: ID of the rule to delete
            
        Returns:
            True if successful, False otherwise
        """
        rules = self.get_conditional_rules()
        for i, rule in enumerate(rules):
            if rule.get('id') == rule_id:
                rules.pop(i)
                self.config.set('conditional_rules', rules)
                self.config.save()
                return True
        return False
