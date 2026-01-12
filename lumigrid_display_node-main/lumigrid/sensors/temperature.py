"""Temperature sensor support."""

import logging
from typing import Optional

logger = logging.getLogger(__name__)


class TemperatureSensor:
    """Base class for temperature sensors."""

    def __init__(self, config: dict):
        """Initialize temperature sensor.

        Args:
            config: Configuration dictionary
        """
        self.config = config
        self.sensor_type = config.get('sensor_type', 'DS18B20')
        self.enabled = config.get('enabled', True)

    def get_temperature(self) -> Optional[float]:
        """Get current temperature in Celsius.

        Returns:
            Temperature in Celsius, or None if unavailable
        """
        raise NotImplementedError


class DS18B20Sensor(TemperatureSensor):
    """Dallas DS18B20 temperature sensor (1-Wire)."""

    def __init__(self, config: dict):
        """Initialize DS18B20 sensor."""
        super().__init__(config)
        self.sensor = None
        self._initialize_sensor()

    def _initialize_sensor(self):
        """Initialize the DS18B20 sensor."""
        if not self.enabled:
            logger.info("Temperature sensor disabled in config")
            return

        try:
            from w1thermsensor import W1ThermSensor, Sensor
            self.sensor = W1ThermSensor(sensor_type=Sensor.DS18B20)
            logger.info(f"DS18B20 sensor initialized: {self.sensor.id}")
        except ImportError:
            logger.warning("w1thermsensor library not available")
            self.sensor = None
        except Exception as e:
            logger.error(f"Error initializing DS18B20 sensor: {e}")
            self.sensor = None

    def get_temperature(self) -> Optional[float]:
        """Get current temperature from DS18B20.

        Returns:
            Temperature in Celsius, or None if unavailable
        """
        if not self.sensor:
            return None

        try:
            temp = self.sensor.get_temperature()
            logger.debug(f"DS18B20 temperature: {temp}°C")
            return temp
        except Exception as e:
            logger.error(f"Error reading DS18B20: {e}")
            return None


class DHTSensor(TemperatureSensor):
    """DHT11/DHT22 temperature and humidity sensor.
    
    Uses the adafruit-circuitpython-dht library (replaces deprecated Adafruit-DHT).
    """

    def __init__(self, config: dict):
        """Initialize DHT sensor."""
        super().__init__(config)
        self.pin = config.get('pin', 4)
        self.dht_device = None
        self._last_temperature = None
        self._last_humidity = None
        self._initialize_sensor()

    def _get_board_pin(self, gpio_number: int):
        """Get the board pin object for a given GPIO number.
        
        On Raspberry Pi with adafruit-blinka, GPIO pins are accessed via board.D{number}.
        
        Args:
            gpio_number: GPIO pin number (e.g., 4 for GPIO4)
            
        Returns:
            Board pin object or None if not found
        """
        import board
        
        # Try D{number} format first (standard on RPi with Blinka)
        pin_name = f"D{gpio_number}"
        board_pin = getattr(board, pin_name, None)
        
        if board_pin is not None:
            return board_pin
        
        # Fallback: try some common pin names
        common_names = [f"GPIO{gpio_number}", f"BCM{gpio_number}"]
        for name in common_names:
            board_pin = getattr(board, name, None)
            if board_pin is not None:
                return board_pin
        
        return None

    def _initialize_sensor(self):
        """Initialize the DHT sensor using CircuitPython library."""
        if not self.enabled:
            logger.info("Temperature sensor disabled in config")
            return

        try:
            import board
            import adafruit_dht
            
            # Get the board pin based on GPIO number
            board_pin = self._get_board_pin(self.pin)
            
            if board_pin is None:
                logger.error(f"Invalid GPIO pin: {self.pin}. Available pins: {[p for p in dir(board) if p.startswith('D')]}")
                self.dht_device = None
                return
            
            if self.sensor_type == 'DHT22':
                self.dht_device = adafruit_dht.DHT22(board_pin, use_pulseio=False)
            else:
                self.dht_device = adafruit_dht.DHT11(board_pin, use_pulseio=False)
                
            logger.info(f"{self.sensor_type} sensor initialized on GPIO{self.pin}")
        except ImportError:
            logger.warning("adafruit-circuitpython-dht library not available")
            self.dht_device = None
        except Exception as e:
            logger.error(f"Error initializing DHT sensor: {e}")
            self.dht_device = None

    def _read_sensor(self):
        """Read both temperature and humidity from the sensor."""
        if not self.dht_device:
            return
            
        try:
            # The CircuitPython library reads both values at once
            self._last_temperature = self.dht_device.temperature
            self._last_humidity = self.dht_device.humidity
            
            if self._last_temperature is not None:
                if self._last_humidity is not None:
                    logger.debug(f"{self.sensor_type} temperature: {self._last_temperature}°C, humidity: {self._last_humidity}%")
                else:
                    logger.debug(f"{self.sensor_type} temperature: {self._last_temperature}°C")
        except RuntimeError as e:
            # DHT sensors can fail to read sometimes, this is normal
            logger.debug(f"DHT read error (normal): {e}")
        except Exception as e:
            logger.error(f"Error reading DHT sensor: {e}")

    def get_temperature(self) -> Optional[float]:
        """Get current temperature from DHT sensor.

        Returns:
            Temperature in Celsius, or None if unavailable
        """
        if not self.dht_device:
            return None

        self._read_sensor()
        return self._last_temperature

    def get_humidity(self) -> Optional[float]:
        """Get current humidity from DHT sensor.

        Returns:
            Humidity percentage, or None if unavailable
        """
        if not self.dht_device:
            return None

        self._read_sensor()
        return self._last_humidity
    
    def cleanup(self):
        """Clean up DHT sensor resources."""
        if self.dht_device:
            try:
                # CircuitPython DHT library doesn't require explicit cleanup
                # but some versions may have deinit() method
                if hasattr(self.dht_device, 'deinit'):
                    self.dht_device.deinit()
            except Exception as e:
                logger.debug(f"Error cleaning up DHT sensor: {e}")


def create_temperature_sensor(config: dict) -> TemperatureSensor:
    """Factory function to create the appropriate temperature sensor.

    Args:
        config: Temperature sensor configuration

    Returns:
        TemperatureSensor instance
    """
    sensor_type = config.get('sensor_type', 'DS18B20')
    
    if sensor_type == 'DS18B20':
        return DS18B20Sensor(config)
    elif sensor_type in ['DHT11', 'DHT22']:
        return DHTSensor(config)
    else:
        logger.warning(f"Unknown sensor type: {sensor_type}, using DS18B20")
        return DS18B20Sensor(config)
