import socket
import json
import threading
import time
import logging
from typing import Callable, Optional, Dict, Any
from enum import Enum

logger = logging.getLogger(__name__)

MCAST_GRP = "239.10.7.42"
MCAST_PORT = 45454

# Packet types matching ESP32 protocol
SYNC_PACKET_TICK = "tick"
SYNC_PACKET_CUE = "cue"

class ClockState(Enum):
    """Clock synchronization states."""
    LOCAL = "local"           # Using local clock (independent)
    SYNCING = "syncing"       # Attempting to sync with master
    SYNCED = "synced"         # Successfully synced with master
    DESYNC = "desync"         # Lost sync, falling back to local

class SyncProtocol:
    """UDP-based synchronization protocol for Master/Slave display nodes.
    
    Implements JSON-based sync packets compatible with ESP32 LED nodes.
    Supports clock state management, fallback to local clock on packet loss,
    and comprehensive logging of sync events.
    """
    
    # Packet loss detection thresholds
    PACKET_TIMEOUT_MS = 200  # Expected packet interval (at 20Hz = 50ms, with 4x margin)
    MAX_JITTER_MS = 100      # Maximum acceptable timing jitter
    DESYNC_THRESHOLD = 5     # Number of missed packets before desync
    
    def __init__(self):
        self.running = False
        self.role = None  # 'master' or 'slave'
        self.tx_socket = None
        self.rx_socket = None
        self.tempo_bpm = 120.0
        self.phase = 0.0
        self.drift_ewma = 0.0
        self._on_tick: Optional[Callable] = None
        self._on_cue: Optional[Callable] = None
        
        # Clock state management
        self.clock_state = ClockState.LOCAL
        self._last_packet_time = 0
        self._missed_packets = 0
        self._total_packets_received = 0
        self._total_packets_sent = 0
        self._jitter_values = []  # Track recent jitter for statistics
    
    def _set_clock_state(self, new_state: ClockState):
        """Update clock state and log transition.
        
        Args:
            new_state: New clock state to transition to
        """
        if self.clock_state != new_state:
            old_state = self.clock_state
            self.clock_state = new_state
            logger.info(f"Clock state transition: {old_state.value} -> {new_state.value}")
            
            # Log additional context based on transition
            if new_state == ClockState.DESYNC:
                logger.warning(f"Desync detected after {self._missed_packets} missed packets")
            elif new_state == ClockState.SYNCED:
                logger.info(f"Sync established (total packets received: {self._total_packets_received})")
    
    def _calculate_avg_jitter(self) -> float:
        """Calculate average jitter from recent drift values.
        
        Returns:
            Average jitter in milliseconds
        """
        if len(self._jitter_values) < 2:
            return 0.0
        
        jitter_sum = sum(abs(self._jitter_values[i] - self._jitter_values[i-1]) 
                        for i in range(1, len(self._jitter_values)))
        return jitter_sum / (len(self._jitter_values) - 1)
    
    def _create_sync_packet(self, packet_type: str) -> bytes:
        """Create a JSON sync packet.
        
        Args:
            packet_type: Type of packet (tick or cue)
            
        Returns:
            JSON packet as bytes
        """
        packet = {
            "timestamp_ms": int(time.time() * 1000) & 0xFFFFFFFF,
            "type": packet_type
        }
        return json.dumps(packet).encode('utf-8')
    
    def _parse_sync_packet(self, data: bytes) -> Optional[Dict[str, Any]]:
        """Parse a JSON sync packet.
        
        Args:
            data: Raw packet data
            
        Returns:
            Parsed packet dictionary or None if invalid
        """
        try:
            packet = json.loads(data.decode('utf-8'))
            if 'timestamp_ms' in packet and 'type' in packet:
                return packet
            else:
                logger.warning("Invalid sync packet: missing required fields")
                return None
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            logger.warning(f"Failed to parse sync packet: {e}")
            return None
    
    def start_master(self):
        """Start as sync master (broadcasts ticks)."""
        self.role = 'master'
        self.running = True
        self._set_clock_state(ClockState.LOCAL)  # Master uses local clock
        self.tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self.tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        logger.info(f"Starting sync master (multicast: {MCAST_GRP}:{MCAST_PORT})")
        
        def tx_loop():
            while self.running:
                try:
                    packet = self._create_sync_packet(SYNC_PACKET_TICK)
                    self.tx_socket.sendto(packet, (MCAST_GRP, MCAST_PORT))
                    self._total_packets_sent += 1
                    
                    if self._total_packets_sent % 100 == 0:
                        logger.debug(f"Master: sent {self._total_packets_sent} packets")
                    
                    time.sleep(0.05)  # 20 Hz
                except Exception as e:
                    logger.error(f"Error in master TX loop: {e}")
                    time.sleep(0.1)
        
        threading.Thread(target=tx_loop, daemon=True).start()
        logger.info("Sync master started successfully")
    
    def start_slave(self):
        """Start as sync slave (receives ticks)."""
        self.role = 'slave'
        self.running = True
        self._set_clock_state(ClockState.SYNCING)
        self.rx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self.rx_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.rx_socket.settimeout(self.PACKET_TIMEOUT_MS / 1000.0)
        
        # Bind to all interfaces ('') is required for multicast reception
        self.rx_socket.bind(('', MCAST_PORT))
        
        mreq = socket.inet_aton(MCAST_GRP) + socket.inet_aton('0.0.0.0')
        self.rx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        
        logger.info(f"Starting sync slave (listening on {MCAST_GRP}:{MCAST_PORT})")
        
        def rx_loop():
            while self.running:
                try:
                    data, addr = self.rx_socket.recvfrom(1024)
                    packet = self._parse_sync_packet(data)
                    
                    if packet:
                        self._process_sync_packet(packet, addr)
                        
                except socket.timeout:
                    # Packet timeout - check for desync
                    self._handle_packet_timeout()
                except Exception as e:
                    logger.error(f"Error in slave RX loop: {e}")
                    time.sleep(0.1)
        
        threading.Thread(target=rx_loop, daemon=True).start()
        logger.info("Sync slave started successfully")
    
    def _process_sync_packet(self, packet: Dict[str, Any], addr):
        """Process a received sync packet.
        
        Args:
            packet: Parsed packet dictionary
            addr: Source address tuple
        """
        timestamp_ms = packet['timestamp_ms']
        packet_type = packet['type']
        local_ms = int(time.time() * 1000) & 0xFFFFFFFF
        
        # Calculate drift and jitter
        drift = local_ms - timestamp_ms
        self.drift_ewma = 0.9 * self.drift_ewma + 0.1 * drift
        
        # Track jitter (variation in drift)
        if len(self._jitter_values) > 0:
            jitter = abs(drift - self._jitter_values[-1])
            self._jitter_values.append(drift)
            if len(self._jitter_values) > 10:
                self._jitter_values.pop(0)
            
            # Log excessive jitter
            if jitter > self.MAX_JITTER_MS:
                logger.warning(f"High jitter detected: {jitter}ms")
        else:
            self._jitter_values.append(drift)
        
        # Update packet statistics
        self._total_packets_received += 1
        self._missed_packets = 0
        self._last_packet_time = time.time()
        
        # Update clock state to synced if we're syncing
        if self.clock_state == ClockState.SYNCING:
            self._set_clock_state(ClockState.SYNCED)
        elif self.clock_state == ClockState.DESYNC:
            self._set_clock_state(ClockState.SYNCED)
        
        # Log periodic statistics
        if self._total_packets_received % 100 == 0:
            avg_jitter = self._calculate_avg_jitter()
            logger.debug(f"Slave: received {self._total_packets_received} packets, "
                        f"drift_ewma={self.drift_ewma:.1f}ms, avg_jitter={avg_jitter:.1f}ms")
        
        # Invoke callbacks
        if packet_type == SYNC_PACKET_TICK and self._on_tick:
            self._on_tick(timestamp_ms, self.drift_ewma)
        elif packet_type == SYNC_PACKET_CUE and self._on_cue:
            self._on_cue(timestamp_ms)
    
    def _handle_packet_timeout(self):
        """Handle packet reception timeout (potential packet loss)."""
        self._missed_packets += 1
        
        # Log packet loss
        if self._missed_packets == 1:
            logger.warning("Packet loss detected (first missed packet)")
        elif self._missed_packets % 10 == 0:
            logger.warning(f"Extended packet loss: {self._missed_packets} consecutive missed packets")
        
        # Check for desync threshold
        if self._missed_packets >= self.DESYNC_THRESHOLD and self.clock_state == ClockState.SYNCED:
            self._set_clock_state(ClockState.DESYNC)
            logger.warning("Falling back to local clock due to packet loss")
    
    def stop(self):
        """Stop the sync protocol."""
        logger.info(f"Stopping sync protocol (role: {self.role})")
        self.running = False
        if self.tx_socket:
            self.tx_socket.close()
        if self.rx_socket:
            self.rx_socket.close()
        
        # Log final statistics
        if self.role == 'master':
            logger.info(f"Master stopped: sent {self._total_packets_sent} packets")
        elif self.role == 'slave':
            loss_rate = (self._missed_packets / max(1, self._total_packets_received + self._missed_packets)) * 100
            logger.info(f"Slave stopped: received {self._total_packets_received} packets, "
                       f"missed {self._missed_packets} ({loss_rate:.1f}% loss)")
    
    def get_status(self) -> Dict[str, Any]:
        """Get current sync protocol status.
        
        Returns:
            Dictionary with status information
        """
        status = {
            'running': self.running,
            'role': self.role,
            'clock_state': self.clock_state.value if self.clock_state else None,
            'drift_ewma_ms': round(self.drift_ewma, 2),
        }
        
        if self.role == 'master':
            status['packets_sent'] = self._total_packets_sent
        elif self.role == 'slave':
            status['packets_received'] = self._total_packets_received
            status['missed_packets'] = self._missed_packets
            if len(self._jitter_values) > 1:
                status['avg_jitter_ms'] = round(self._calculate_avg_jitter(), 2)
        
        return status
    
    def on_tick(self, callback: Callable):
        """Register callback for tick events.
        
        Args:
            callback: Callable that accepts (timestamp_ms: int, drift_ewma: float)
        """
        self._on_tick = callback
    
    def on_cue(self, callback: Callable):
        """Register callback for cue events.
        
        Args:
            callback: Callable that accepts (timestamp_ms: int)
        """
        self._on_cue = callback
