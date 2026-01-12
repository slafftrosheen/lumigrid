# LumiGrid API Documentation

This document provides an overview of the LumiGrid REST API endpoints and the JSON schemas used for validation.

## API Endpoints

### GET /api/status

Returns the current status of the node.

-   **ID**: Node ID
-   **Type**: Node type (e.g., "display")
-   **IP**: Node IP address
-   **State**: Node state (e.g., "running")
-   **Uptime**: Node uptime in seconds
-   **Metrics**: Node-specific metrics (e.g., temperature, brightness)

### GET /api/config

Returns the current configuration of the node.

### POST /api/config

Updates the configuration of the node. The request body must be a valid JSON object that conforms to the `config.schema.json` schema.

### GET /api/presets

Returns a list of available presets.

### POST /api/presets

Creates or uploads a preset. The request body must be a valid JSON object that conforms to the `preset-display.schema.json` schema.

### DELETE /api/presets/{preset_name}

Deletes a preset.

### POST /api/trigger

Triggers an action on the node. The request body must be a valid JSON object that conforms to the `trigger.schema.json` schema.

### GET /api/discover/needs_sensor_data

Returns the sensor data requirements for the node.

### POST /api/sensor_update

Receives sensor data from sensor nodes. The request body must be a valid JSON object that conforms to the `sensor-data.schema.json` schema.

## Schema Coverage

The following JSON schemas are used to validate the API payloads:

-   **config.schema.json**: Validates the configuration data for the `POST /api/config` endpoint.
-   **preset-display.schema.json**: Validates the preset data for the `POST /api/presets` endpoint.
-   **sensor-data.schema.json**: Validates the sensor data for the `POST /api/sensor_update` endpoint.
-   **trigger.schema.json**: Validates the trigger data for the `POST /api/trigger` endpoint.
