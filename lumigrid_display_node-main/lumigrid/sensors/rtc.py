"""Real-Time Clock (RTC) support."""

import logging
import subprocess
from datetime import datetime
from typing import Optional

logger = logging.getLogger(__name__)


class RTCManager:
    """Manages Real-Time Clock operations."""

    def __init__(self, config: dict):
        """Initialize RTC manager.

        Args:
            config: RTC configuration dictionary
        """
        self.config = config
        self.rtc_type = config.get('type', 'DS3231')
        self.enabled = config.get('enabled', True)
        self.sync_interval = config.get('sync_interval', 3600)
        self.last_sync = 0
        self.rtc = None
        self._initialize_rtc()

    def _initialize_rtc(self):
        """Initialize the RTC module."""
        if not self.enabled:
            logger.info("RTC disabled in config")
            return

        try:
            import board
            import busio
            
            if self.rtc_type == 'DS3231':
                from adafruit_ds3231 import DS3231
                i2c = busio.I2C(board.SCL, board.SDA)
                self.rtc = DS3231(i2c)
                logger.info("DS3231 RTC initialized")
            else:
                logger.warning(f"Unsupported RTC type: {self.rtc_type}")
                self.rtc = None
        except ImportError:
            logger.warning("RTC library not available")
            self.rtc = None
        except Exception as e:
            logger.error(f"Error initializing RTC: {e}")
            self.rtc = None

    def get_time(self) -> Optional[datetime]:
        """Get current time from RTC.

        Returns:
            datetime object, or None if unavailable
        """
        if not self.rtc:
            return None

        try:
            rtc_time = self.rtc.datetime
            # Convert from time.struct_time to datetime
            dt = datetime(
                rtc_time.tm_year, rtc_time.tm_mon, rtc_time.tm_mday,
                rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec
            )
            logger.debug(f"RTC time: {dt}")
            return dt
        except Exception as e:
            logger.error(f"Error reading RTC: {e}")
            return None

    def set_time(self, dt: datetime) -> bool:
        """Set RTC time.

        Args:
            dt: datetime object to set

        Returns:
            True if successful, False otherwise
        """
        if not self.rtc:
            logger.warning("RTC not available, cannot set time")
            return False

        try:
            import time
            t = time.struct_time((
                dt.year, dt.month, dt.day,
                dt.hour, dt.minute, dt.second,
                dt.weekday(), -1, -1
            ))
            self.rtc.datetime = t
            logger.info(f"RTC time set to {dt}")
            return True
        except Exception as e:
            logger.error(f"Error setting RTC time: {e}")
            return False

    def sync_system_time(self) -> bool:
        """Sync system time from RTC.

        Returns:
            True if successful, False otherwise
        """
        rtc_time = self.get_time()
        if not rtc_time:
            logger.warning("Cannot sync system time: RTC not available")
            return False

        try:
            # Set system time using date command
            time_str = rtc_time.strftime("%Y-%m-%d %H:%M:%S")
            cmd = ['sudo', 'date', '-s', time_str]
            result = subprocess.run(cmd, capture_output=True, text=True)
            
            if result.returncode == 0:
                logger.info(f"System time synced from RTC: {time_str}")
                self.last_sync = datetime.now().timestamp()
                return True
            else:
                logger.error(f"Error syncing system time: {result.stderr}")
                return False
        except Exception as e:
            logger.error(f"Error syncing system time: {e}")
            return False

    def sync_rtc_from_system(self) -> bool:
        """Sync RTC from system time.

        Returns:
            True if successful, False otherwise
        """
        system_time = datetime.now()
        return self.set_time(system_time)

    def should_sync(self) -> bool:
        """Check if it's time to sync system time from RTC.

        Returns:
            True if sync is needed, False otherwise
        """
        import time
        current_time = time.time()
        return (current_time - self.last_sync) >= self.sync_interval
