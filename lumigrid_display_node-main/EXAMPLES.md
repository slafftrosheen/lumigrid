# Example Usage Scripts

This directory contains example scripts demonstrating how to use the Lumigrid Display Node API programmatically.

## Send Message Script

```python
#!/usr/bin/env python3
"""Example: Send a custom message to the display."""

import requests

LUMIGRID_URL = "http://localhost:5000"

def send_message(message):
    """Send a custom message to the display."""
    response = requests.post(
        f"{LUMIGRID_URL}/api/message",
        json={"message": message}
    )
    
    if response.status_code == 200:
        print(f"Message sent: {message}")
    else:
        print(f"Error: {response.text}")

if __name__ == "__main__":
    send_message("Hello from Python!")
```

## Change Mode Script

```python
#!/usr/bin/env python3
"""Example: Change the display mode."""

import requests
import time

LUMIGRID_URL = "http://localhost:5000"

def set_mode(mode):
    """Set the display mode."""
    response = requests.post(
        f"{LUMIGRID_URL}/api/mode",
        json={"mode": mode}
    )
    
    if response.status_code == 200:
        print(f"Mode changed to: {mode}")
    else:
        print(f"Error: {response.text}")

if __name__ == "__main__":
    # Cycle through modes
    modes = ["clock", "temperature", "message"]
    
    for mode in modes:
        set_mode(mode)
        time.sleep(10)
```

## Get Status Script

```python
#!/usr/bin/env python3
"""Example: Get current status from the display."""

import requests
import json

LUMIGRID_URL = "http://localhost:5000"

def get_status():
    """Get the current status."""
    response = requests.get(f"{LUMIGRID_URL}/api/status")
    
    if response.status_code == 200:
        status = response.json()
        print("Current Status:")
        print(json.dumps(status, indent=2))
    else:
        print(f"Error: {response.text}")

if __name__ == "__main__":
    get_status()
```

## Set Brightness Script

```python
#!/usr/bin/env python3
"""Example: Adjust display brightness."""

import requests
import sys

LUMIGRID_URL = "http://localhost:5000"

def set_brightness(brightness):
    """Set the display brightness (0-100)."""
    response = requests.post(
        f"{LUMIGRID_URL}/api/brightness",
        json={"brightness": brightness}
    )
    
    if response.status_code == 200:
        print(f"Brightness set to: {brightness}%")
    else:
        print(f"Error: {response.text}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: set_brightness.py <brightness 0-100>")
        sys.exit(1)
    
    brightness = int(sys.argv[1])
    set_brightness(brightness)
```

## Weather Display Integration Example

```python
#!/usr/bin/env python3
"""Example: Display weather information from an API."""

import requests
import time

LUMIGRID_URL = "http://localhost:5000"
WEATHER_API_KEY = "your_api_key_here"
CITY = "London"

def get_weather():
    """Get weather data from API."""
    url = f"http://api.openweathermap.org/data/2.5/weather?q={CITY}&appid={WEATHER_API_KEY}&units=metric"
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        temp = data['main']['temp']
        description = data['weather'][0]['description']
        return f"{CITY}: {temp}°C, {description}"
    return None

def update_display():
    """Update display with weather information."""
    weather = get_weather()
    
    if weather:
        # Send weather as message
        requests.post(
            f"{LUMIGRID_URL}/api/message",
            json={"message": weather}
        )
        
        # Switch to message mode
        requests.post(
            f"{LUMIGRID_URL}/api/mode",
            json={"mode": "message"}
        )
        
        print(f"Display updated: {weather}")

if __name__ == "__main__":
    while True:
        update_display()
        time.sleep(600)  # Update every 10 minutes
```

## Schedule-Based Display Script

```python
#!/usr/bin/env python3
"""Example: Schedule different modes at different times."""

import requests
import time
from datetime import datetime

LUMIGRID_URL = "http://localhost:5000"

SCHEDULE = {
    # Format: (start_hour, end_hour, mode)
    (6, 9): "clock",        # Morning: show clock
    (9, 17): "temperature", # Day: show temperature
    (17, 22): "slideshow",  # Evening: slideshow
    (22, 6): "clock"        # Night: clock (dim)
}

def get_mode_for_time():
    """Get the appropriate mode for current time."""
    current_hour = datetime.now().hour
    
    for (start, end), mode in SCHEDULE.items():
        if start <= end:
            if start <= current_hour < end:
                return mode
        else:  # Crosses midnight
            if current_hour >= start or current_hour < end:
                return mode
    
    return "clock"  # Default

def update_schedule():
    """Update display mode based on schedule."""
    mode = get_mode_for_time()
    
    requests.post(
        f"{LUMIGRID_URL}/api/mode",
        json={"mode": mode}
    )
    
    print(f"{datetime.now()}: Mode set to {mode}")

if __name__ == "__main__":
    while True:
        update_schedule()
        time.sleep(300)  # Check every 5 minutes
```

## Installation

To use these examples:

1. Save the script you want to use
2. Make it executable: `chmod +x script_name.py`
3. Install requests if needed: `pip3 install requests`
4. Run the script: `./script_name.py`

## Notes

- Replace `http://localhost:5000` with your Lumigrid Display Node's actual address
- For weather integration, get an API key from OpenWeatherMap
- These scripts can be run as cron jobs for automation
- Consider error handling and retry logic for production use
