# LumiGrid LED Node Controller Guide

This guide covers the features, configuration, and operation of the **LumiGrid LED Node** firmware.

## Hardware Overview

The LumiGrid LED Node is built on the ESP32-WROOM-32U microcontroller. 

### Peripheral Layout
- **ESP32-WROOM-32U**: The main processor providing Wi-Fi, web server, and effects processing.
- **PCA9685 (I2C Address 0x40)**: An 8-channel PWM driver managing `LED0` through `LED7`.
- **SN74HCT245DWR**: Level shifters (3.3V to 5V) for the 8 addressable LED data lines.
- **Emergency Stop Button**: Configured on GPIO32 by default, used to immediately disable all LED outputs.
- **DS18B20 Temperature Sensor**: Recommended for thermal monitoring, configurable to any free GPIO.

## Output Modes

The LumiGrid controller uses the PCA9685 to support multiple output modes on a per-channel basis. These modes can be configured in the Web UI Settings:

1. **Off**: The channel is disabled.
2. **Dumb PWM**: Standard single-channel PWM where brightness corresponds directly to the maximum of the RGBW components.
3. **CCT (Correlated Color Temperature)**: Uses 2 channels for blending Warm White and Cool White.
4. **RGB**: Uses 3 channels for standard Red, Green, and Blue control.
5. **RGBW**: Uses 4 channels for Red, Green, Blue, and a dedicated White channel.

## Feature Modules (Usermods)

### Content Scheduler

The controller features a time-based scheduler that reads an `animations.json` file from the internal filesystem to automate playback. 

- **Schedule Format**: Time blocks are defined in `HH:MM` format.
- **Duration**: You can specify playback length using `duration_minutes`.
- **Fallbacks**: If no animation is scheduled, the controller defaults to a predefined fallback preset.

### Health Telemetry & Auto-Reboot

Designed for reliable commercial signage, the controller periodically phones home:
- **Heartbeat Ping**: Sends a status update (including MAC address and firmware version) to the central endpoint every 5 minutes.
- **Daily Auto-Reboot**: To prevent memory fragmentation over long uptimes, the controller reboots automatically at a designated time (default 03:00).

## Web UI Configuration

To configure the controller:

1. Connect to the LumiGrid Node via Wi-Fi or local network.
2. Navigate to the IP address in your web browser. You will see the unified dark red LumiGrid interface.
3. Click on the **Config** (gear icon) and go to the **Usermods** page.
4. **PCA9685 Configuration**: Here you can set the PWM frequency (default is 1000 Hz), assign labels, and calibrate the maximum percentage (0-100%) for each channel to tune brightness perfectly.
5. **Telemetry Settings**: You can adjust the auto-reboot time and heartbeat endpoint URLs in the telemetry section.

## Troubleshooting

- **Emergency Stop Triggered**: If the emergency stop button is pressed or shorted, all PWM outputs will halt. The button is active-low with an internal pull-up.
- **Overheat Shutdown**: If the optional DS18B20 sensor detects temperatures exceeding the configured limits, it will throttle brightness or trigger an overheat shutdown.
- **Loss of I2C Connection**: The I2C watchdog will automatically re-initialize the PCA9685 if it stops responding.
