# Lumigrid Content Pipeline & Animation Schema

This document outlines the workflow for deploying and scheduling animated signage content (GIFs and video loops) on Lumigrid LED Nodes using PixelForge.

## 1. Animation JSON Schema (`animations.json`)

The scheduling engine uses `animations.json` to determine what content plays at what time. 
Create or update this file via the WLED `/edit` interface.

```json
{
  "animations": [
    {
      "id": "morning_greeting",
      "file": "/morning_greeting.gif",
      "start_time": "06:00",
      "duration_minutes": 120,
      "fallback_preset": 1
    },
    {
      "id": "night_loop",
      "file": "/night_loop.gif",
      "start_time": "20:00",
      "duration_minutes": 600,
      "fallback_preset": 2
    }
  ]
}
```

### Fields:
- `id`: A unique identifier for the schedule block.
- `file`: The absolute path to the `.gif` on the ESP32 filesystem.
- `start_time`: Time of day to trigger the animation (24-hour `HH:MM` format).
- `duration_minutes`: How long the animation should loop before returning to the previous state or `fallback_preset`.
- `fallback_preset`: The WLED Scene/Preset ID to load if the animation file is missing or fails to decode.

## 2. Upload Workflow for Sign Fitters

Sign installers and operators should follow these steps to deploy new content:

1. **Prepare Content:**
   - Export animations as standard `.gif` files.
   - Keep resolutions small (e.g., matching the exact physical pixel dimensions of the sign matrix, like `64x32`).
   - File sizes must be kept under 500KB to ensure smooth playback and avoid exhausting the ESP32 LittleFS partition.

2. **Connect to Lumigrid Node:**
   - Connect to the local `Lumigrid-AP` WiFi network or access the sign via its local network IP.
   - Open the Operator Dashboard, click **Advanced Editor** (or go to `http://<ip>/edit`).

3. **Upload Files:**
   - Click the **Choose File** button in the `/edit` interface.
   - Select the prepared `.gif` files.
   - Click **Upload**.

4. **Update Schedule:**
   - In the same `/edit` interface, download `animations.json` if it exists, or create a new one.
   - Add the new animation blocks to the JSON array with the corresponding `start_time`.
   - Save and upload `animations.json` back to the ESP32.

5. **Verify Playback:**
   - Go to `http://<ip>/pixelforge.htm` to manually trigger and verify the GIF playback.
   - Alternatively, change the system time to the scheduled time to test the timeline orchestration.
