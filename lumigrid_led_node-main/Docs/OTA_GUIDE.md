# OTA Firmware Update Guide

## Overview

The LumiGrid LED Node supports Over-The-Air (OTA) firmware updates with automatic rollback capability. This allows you to update firmware remotely without physical access to the device.

## Features

- **Upload from file**: Upload a compiled firmware binary (.bin) directly via the web UI
- **Download from URL**: Provide a URL and the device will download and install the firmware
- **Automatic rollback**: If the new firmware fails to boot properly, the device automatically rolls back to the previous working firmware
- **Validation mechanism**: New firmware must successfully initialize and mark itself as valid, otherwise it's considered failed

## Safety Mechanism

The OTA update process includes a safety rollback mechanism:

1. When new firmware is uploaded, it's marked as "pending verification"
2. The device reboots and attempts to run the new firmware
3. The new firmware must successfully initialize and call `ota_mark_valid()` to become permanent
4. If the firmware crashes, fails to initialize, or doesn't mark itself valid, the device will rollback to the previous firmware on the next boot
5. This ensures you can always recover from a bad firmware upload

## Using the Web UI

1. Navigate to the **Configuration** page in the web UI
2. Click the **"Show OTA Update"** button to reveal the OTA upload section
3. Click **"Choose File"** and select your firmware `.bin` file
4. Review the warning about device reboot
5. Click **"Upload Firmware"**
6. Wait for the upload to complete (progress bar will show status)
7. The device will automatically reboot with the new firmware

⚠️ **Warning**: The device will be unavailable during the update and reboot process (typically 30-60 seconds).

## Using the REST API

### Upload Binary File

```bash
# Upload firmware.bin to the device
curl -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @firmware.bin \
  http://lumigrid.local/api/ota/upload
```

### Download from URL

```bash
# Provide a URL to download firmware from
curl -X POST \
  -H "Content-Type: text/plain" \
  -d "http://example.com/firmware/latest.bin" \
  http://lumigrid.local/api/ota/url
```

## Response Codes

- **200 OK**: Firmware uploaded successfully, device will reboot
- **400 Bad Request**: Invalid request (e.g., empty file, invalid URL)
- **500 Internal Server Error**: OTA update failed (e.g., no partition available, write error)

## Building Firmware for OTA

To build a firmware binary suitable for OTA updates:

```bash
# From project root
cd firmware

# Build the firmware
idf.py build

# The OTA-compatible binary will be at:
# build/lumigrid_led_node.bin
```

## Partition Table Requirements

The device must be configured with an OTA-capable partition table. The default ESP-IDF partition table includes:

- Factory partition (initial firmware)
- OTA_0 partition (first OTA slot)
- OTA_1 partition (second OTA slot)

This allows the device to switch between two firmware versions for rollback capability.

## Troubleshooting

### Upload fails with "no partition" error

The device doesn't have an OTA partition configured. Check the partition table in `sdkconfig` and ensure it's set to an OTA-capable layout.

### Device doesn't boot after update

The automatic rollback should handle this. Power cycle the device and it should revert to the previous firmware.

### Upload succeeds but device doesn't update

Check the device logs via serial monitor to see if there were errors during the OTA write or commit process.

### Progress shows 100% but upload fails

This usually indicates a problem with the firmware image validation. Ensure the binary is built for the correct target (ESP32) and is a valid firmware image.

## Best Practices

1. **Test firmware thoroughly** before deploying via OTA
2. **Always maintain a backup** of known-good firmware
3. **Verify the device is on stable power** before initiating an update
4. **Don't interrupt** the update process once started
5. **Monitor the first boot** after an OTA update to ensure the firmware is working correctly
6. **Keep track of firmware versions** to know when a rollback has occurred

## Security Considerations

- OTA updates are transmitted unencrypted over HTTP
- For production deployments, consider implementing:
  - HTTPS for encrypted transfers
  - Firmware signature verification
  - Authentication for the OTA endpoint
  - Rate limiting to prevent abuse

## Technical Details

### Rollback Implementation

The rollback mechanism uses ESP-IDF's built-in OTA app rollback support:

```c
// On first boot after OTA update
esp_ota_mark_app_valid_cancel_rollback();  // Mark as valid

// If firmware fails to boot
esp_ota_mark_app_invalid_rollback_and_reboot();  // Trigger rollback
```

### State Machine

```
Normal Operation
    ↓
Upload New Firmware → Mark as "pending verification"
    ↓
Reboot → Attempt to run new firmware
    ↓
    ├─→ Success → Mark valid → Continue with new firmware
    └─→ Fail → Rollback to previous firmware on next boot
```

## API Reference

See `Docs/api_spec.yaml` for complete API documentation of OTA endpoints.
