# UDP Synchronization - Master/Slave Display Node Sync

## Overview

The Lumigrid Display Node implements UDP-based synchronization for coordinating multiple display nodes. This allows for synchronized playback of sequences, animations, and display updates across multiple devices.

## Architecture

### Master/Slave Model

- **Master Node**: Broadcasts sync packets to all slave nodes via UDP multicast
- **Slave Node(s)**: Listen for sync packets and synchronize their display timing

### Protocol Compatibility

The protocol is compatible with ESP32 LED nodes, using a JSON-based packet format for interoperability.

## UDP Multicast Configuration

- **Multicast Group**: `239.10.7.42`
- **Port**: `45454`
- **Packet Rate**: 20 Hz (50ms interval)
- **Protocol**: UDP multicast

## Packet Format

### Sync Packet Schema

Packets are sent as JSON objects with the following structure:

```json
{
  "timestamp_ms": 1894351,
  "type": "tick"
}
```

**Fields:**
- `timestamp_ms` (integer): Timestamp in milliseconds from the master's clock (32-bit unsigned)
- `type` (string): Packet type, either `"tick"` or `"cue"`

### Packet Types

1. **tick**: Regular synchronization pulse (sent at 20 Hz)
   - Used for maintaining time sync between nodes
   - Carries master's timestamp for drift calculation

2. **cue**: Special event trigger (future use)
   - Can be used to trigger specific events across all nodes
   - Allows for coordinated state changes

## Clock State Management

Slave nodes maintain one of four clock states:

### States

1. **LOCAL** (`local`)
   - Using local clock independently
   - Default state for master nodes
   - Fallback state when sync is unavailable

2. **SYNCING** (`syncing`)
   - Initial state when slave starts
   - Attempting to establish sync with master

3. **SYNCED** (`synced`)
   - Successfully synchronized with master
   - Receiving packets regularly
   - Display timing aligned with master

4. **DESYNC** (`desync`)
   - Lost synchronization with master
   - Fallen back to local clock
   - Will attempt to re-sync when packets resume

### State Transitions

```
       ┌─────────┐
  ┌───►│  LOCAL  │◄────┐
  │    └─────────┘     │
  │                    │
  │    ┌─────────┐     │
  └────│ SYNCING │     │
       └────┬────┘     │
            │          │
            ▼          │
       ┌─────────┐     │
       │ SYNCED  │     │
       └────┬────┘     │
            │          │
            ▼          │
       ┌─────────┐     │
       │ DESYNC  │─────┘
       └─────────┘
```

All state transitions are logged for monitoring and debugging.

## Packet Loss Handling

### Detection

The system monitors for packet loss using:
- **Timeout Detection**: 200ms timeout (4x the expected 50ms interval)
- **Consecutive Missed Packets**: Counter tracks sequential packet loss

### Thresholds

- **DESYNC_THRESHOLD**: 5 missed packets triggers fallback to local clock
- **MAX_JITTER_MS**: 100ms maximum acceptable timing variation

### Fallback Behavior

When packet loss exceeds threshold:
1. Slave transitions to DESYNC state
2. Falls back to local clock
3. Logs warning with packet loss statistics
4. Continues monitoring for sync restoration
5. Automatically re-syncs when packets resume

## Drift and Jitter Monitoring

### Drift Calculation

**Drift** is the time difference between master and slave clocks:
```
drift = local_timestamp - master_timestamp
```

The system calculates an **Exponentially Weighted Moving Average (EWMA)** of drift:
```
drift_ewma = 0.9 * drift_ewma + 0.1 * current_drift
```

This smooths out short-term variations and provides a stable drift metric.

### Jitter Tracking

**Jitter** is the variation in drift over time:
- Tracked over the last 10 packets
- High jitter (>100ms) triggers warnings
- Indicates network instability or timing issues

### Statistics Logging

The system logs statistics every 100 packets:
- Total packets received
- Average drift
- Average jitter
- Packet loss rate

## Configuration

### config.yaml Settings

```yaml
control:
  mode: "sync"              # Enable sync mode
  sync_role: "slave"        # "master" or "slave"
```

### Control Modes

- **independent**: Autonomous operation (default)
- **sync**: Master/Slave synchronized playback
- **external**: Controlled by Art-Net/MQTT (future)

## Usage

### Starting as Master

```python
from lumigrid.sync import SyncProtocol

sync = SyncProtocol()
sync.start_master()
# Master will broadcast tick packets at 20 Hz
```

### Starting as Slave

```python
from lumigrid.sync import SyncProtocol

sync = SyncProtocol()

def on_tick(timestamp_ms, drift_ewma):
    print(f"Received tick: timestamp={timestamp_ms}, drift={drift_ewma}ms")

sync.on_tick(on_tick)
sync.start_slave()
# Slave will receive and process tick packets
```

### Getting Status

```python
status = sync.get_status()
print(f"Clock state: {status['clock_state']}")
print(f"Drift: {status['drift_ewma_ms']}ms")
print(f"Packets received: {status['packets_received']}")
```

## Network Requirements

### Multicast Support

UDP multicast must be enabled on your network:
- Ensure routers/switches support IGMP
- Multicast traffic allowed between nodes
- Firewall rules permit UDP port 45454

### Network Performance

For best results:
- Low-latency network (< 10ms between nodes)
- Stable network without packet loss
- Dedicated VLAN for display nodes (recommended)

## Logging

### Log Levels

The sync protocol logs at different levels:

**INFO**: State transitions, start/stop events
```
Clock state transition: syncing -> synced
Sync established (total packets received: 42)
```

**WARNING**: Packet loss, desync events, high jitter
```
Packet loss detected (first missed packet)
High jitter detected: 150ms
Falling back to local clock due to packet loss
```

**DEBUG**: Periodic statistics (every 100 packets)
```
Slave: received 100 packets, drift_ewma=12.3ms, avg_jitter=5.2ms
```

**ERROR**: Socket errors, protocol failures
```
Error in slave RX loop: [Errno 111] Connection refused
```

### Example Log Output

```
2026-01-08 12:00:00 - lumigrid.sync.protocol - INFO - Starting sync slave (listening on 239.10.7.42:45454)
2026-01-08 12:00:00 - lumigrid.sync.protocol - INFO - Sync slave started successfully
2026-01-08 12:00:00 - lumigrid.sync.protocol - INFO - Clock state transition: syncing -> synced
2026-01-08 12:00:00 - lumigrid.sync.protocol - INFO - Sync established (total packets received: 3)
2026-01-08 12:00:05 - lumigrid.sync.protocol - DEBUG - Slave: received 100 packets, drift_ewma=8.5ms, avg_jitter=3.2ms
2026-01-08 12:00:15 - lumigrid.sync.protocol - WARNING - Packet loss detected (first missed packet)
2026-01-08 12:00:16 - lumigrid.sync.protocol - WARNING - Desync detected after 5 missed packets
2026-01-08 12:00:16 - lumigrid.sync.protocol - INFO - Clock state transition: synced -> desync
2026-01-08 12:00:16 - lumigrid.sync.protocol - WARNING - Falling back to local clock due to packet loss
```

## Testing

### Simulating Packet Loss

Use network tools to simulate packet loss:

```bash
# Using tc (traffic control) to add 10% packet loss
sudo tc qdisc add dev eth0 root netem loss 10%

# Remove packet loss
sudo tc qdisc del dev eth0 root
```

### Simulating Network Jitter

```bash
# Add 50ms +/- 10ms jitter
sudo tc qdisc add dev eth0 root netem delay 50ms 10ms

# Remove jitter
sudo tc qdisc del dev eth0 root
```

### Testing Tools

Monitor multicast traffic:
```bash
# Listen to multicast packets
sudo tcpdump -i eth0 -n 'dst host 239.10.7.42 and port 45454'
```

Send test packets:
```python
import socket
import json
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)

while True:
    packet = {"timestamp_ms": int(time.time() * 1000), "type": "tick"}
    sock.sendto(json.dumps(packet).encode('utf-8'), ("239.10.7.42", 45454))
    time.sleep(0.05)
```

## Troubleshooting

### No Packets Received

1. **Check network connectivity**
   ```bash
   ping <master_ip>
   ```

2. **Verify multicast routing**
   ```bash
   ip mroute show
   netstat -g
   ```

3. **Check firewall rules**
   ```bash
   sudo iptables -L -n | grep 45454
   ```

4. **Test multicast reception**
   ```bash
   # On slave node
   sudo tcpdump -i eth0 dst 239.10.7.42 and port 45454
   ```

### High Drift/Jitter

1. **Check network latency**
   ```bash
   ping <master_ip>
   ```

2. **Verify CPU load**
   ```bash
   top
   ```

3. **Check for network congestion**
   ```bash
   iftop
   ```

### Frequent Desyncs

1. **Increase DESYNC_THRESHOLD** (modify in code if needed)
2. **Check network stability** (packet loss, latency variation)
3. **Review logs** for patterns in desync events
4. **Consider dedicated network** for display nodes

## Future Enhancements

Planned features for future releases:

- [ ] Automatic master election (if master fails)
- [ ] Precision Time Protocol (PTP) support
- [ ] Adjustable packet rate based on network conditions
- [ ] Synchronized sequencer playback
- [ ] Coordinated frame transitions
- [ ] Cue-based event triggering
- [ ] Web UI for sync monitoring and control
- [ ] Metrics export (Prometheus/Grafana)

## References

- [JSON Schema Specification](https://github.com/algorhythmicdev/lumigrid_misc/blob/main/schemas/sync-packet.schema.json)
- [Multicast UDP Programming](https://www.tldp.org/HOWTO/Multicast-HOWTO.html)
- [ESP32 LED Node Protocol](https://github.com/algorhythmicdev/)
