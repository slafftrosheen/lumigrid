# LumiGrid Node Interoperability Contracts

This document outlines the discovery, event, and data contracts that enable interoperability between LumiGrid node types (LED, Display, AC, Sensor, Audio).

## 1. Discovery

Node discovery is handled via multicast DNS (mDNS), allowing nodes to find each other on the local network without manual configuration.

- **Service Name**: `_lumigrid-node._tcp`
- **Port**: 80 (or the node's configured HTTP port)
- **TXT Records**:
  - `node_id`: Unique identifier for the node (e.g., `led-controller-001`)
  - `node_type`: Type of node (`led_controller`, `display`, `sensor`, `gateway`)
  - `version`: LumiGrid firmware/software version

## 2. REST API Contracts

All nodes must expose a minimal set of REST endpoints for status, configuration, and control.

### Common Endpoints

- **`GET /api/status`**
  - **Description**: Returns the current status of the node (e.g., `online`, `offline`, `error`).
  - **Response Body**: `{ "status": "online", "node_id": "...", "uptime_ms": 123456 }`

- **`GET /api/config`**
  - **Description**: Retrieves the node's current configuration.
  - **Response Body**: See schema at [`../schemas/config.schema.json`](../schemas/config.schema.json).

- **`POST /api/config`**
  - **Description**: Updates the node's configuration.
  - **Request Body**: See schema at [`../schemas/config.schema.json`](../schemas/config.schema.json).

- **`GET /api/presets`**
  - **Description**: Returns a list of available presets.
  - **Response Body**: Array of presets matching [`../schemas/preset-led.schema.json`](../schemas/preset-led.schema.json) or [`../schemas/preset-display.schema.json`](../schemas/preset-display.schema.json).

- **`POST /api/trigger`**
  - **Description**: Triggers a pre-defined action or event (e.g., an effect, a sensor reading).
  - **Request Body**: `{ "action": "play_effect", "effect_id": "rainbow" }`

### Error Handling

- **HTTP Status Codes**:
  - `200 OK`: Request was successful.
  - `400 Bad Request`: Invalid request body or parameters.
  - `404 Not Found`: Endpoint or resource does not exist.
  - `500 Internal Server Error`: An unexpected error occurred.
- **Error Response Body**:
  ```json
  {
    "error": "Invalid input",
    "message": "The 'node_id' field is required."
  }
  ```

## 3. Sensor Data Flow

Sensor nodes discover data subscribers and push updates to them.

1. **Discovery**: A sensor node broadcasts a discovery request or queries a known endpoint to find nodes that require sensor data. A common pattern is for subscribers to expose a discovery endpoint.
   - **Endpoint**: `GET /api/discover/needs_sensor_data`
   - **Response**: `{ "subscribe_endpoint": "/api/sensor_update" }`

2. **Data Push**: The sensor node POSTs data to the subscriber's endpoint.
   - **Endpoint**: `POST /api/sensor_update`
   - **Request Body**: See schema at [`../schemas/sensor-data.schema.json`](../schemas/sensor-data.schema.json).

## 4. Sync & Event Propagation

Nodes use a Master/Slave model for synchronized effects and state changes, communicating via UDP packets.

- **Protocol**: UDP
- **Port**: Configurable (see `sync.udp_port` in `config.schema.json`)
- **Packet Format**: See schema at [`../schemas/sync-packet.schema.json`](../schemas/sync-packet.schema.json).

### Cascade Trigger

An event can be triggered on a master node and cascaded to slaves.
1. **Initial Trigger**: An HTTP request is sent to the master node's `/api/trigger` endpoint.
2. **UDP Broadcast**: The master node broadcasts a UDP sync packet to all slave nodes.
3. **Slave Action**: Slave nodes receive the packet and perform the corresponding action.

## 5. Workflow for Schema & Documentation Sync

To keep this documentation and the JSON schemas synchronized:

1. **Schema First**: Always update the JSON schema files in [`schemas/`](../schemas/) before implementing the changes.
2. **Update this Document**: After modifying a schema, update the relevant sections in this `INTEROP.md` file to reflect the changes.
3. **Cross-Link**: Ensure that links to schema files are correct and point to the latest versions.
4. **Review**: During code review, verify that both schema and documentation changes are included and aligned.
