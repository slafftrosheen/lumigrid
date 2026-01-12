# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.2] - 2026-01-06

### Fixed
- Fixed systemd service to use virtual environment Python
- Fixed brightness configuration issue that caused service startup failures
- Updated service to use Gunicorn for production deployment
- Added proper PATH environment variable to systemd service

### Added
- Documentation for Gunicorn production setup
- Documentation for remote access in headless setups
- Troubleshooting section for brightness configuration issues
- Remote access instructions for headless setups

## [0.1.1] - 2026-01-06

### Fixed
- Fixed RGB Matrix hardware pulsing configuration to properly use disable_hardware_pulsing setting from config.yaml
- Updated DisplayManager to correctly pass disable_hardware_pulsing option to RGBMatrixOptions

### Added
- Documentation updates for running without root privileges
- Information about disable_hardware_pulsing configuration option

## [0.1.0] - 2026-01-06

### Added
- Initial release of Lumigrid Display Node
- LED matrix display support using hzeller's rpi-rgb-led-matrix library
- Multiple display modes: Clock, Temperature, Message, and Slideshow
- RTC (Real-Time Clock) support for DS3231 and DS1307
- Temperature sensor support for DS18B20, DHT11, and DHT22
- Web interface for remote control
- RESTful API for integration with other systems
- YAML-based configuration system
- Systemd service integration for automatic startup
- Comprehensive installation script
- Text scrolling for long messages
- Adjustable brightness control
- Detailed documentation and setup guide
