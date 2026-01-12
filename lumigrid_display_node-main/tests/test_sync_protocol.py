"""Tests for the sync protocol module."""

import pytest
import time
import json
import socket
from lumigrid.sync.protocol import (
    SyncProtocol, MCAST_GRP, MCAST_PORT, 
    SYNC_PACKET_TICK, SYNC_PACKET_CUE, ClockState
)


# Determine if multicast is available in this environment
def check_multicast_available():
    """Check if multicast is available (requires elevated privileges)."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        sock.sendto(b'test', (MCAST_GRP, MCAST_PORT))
        sock.close()
        return True
    except (PermissionError, OSError):
        return False


MULTICAST_AVAILABLE = check_multicast_available()


class TestSyncProtocolInit:
    """Tests for SyncProtocol initialization."""
    
    def test_init_default_state(self):
        """Test that SyncProtocol initializes with correct defaults."""
        sync = SyncProtocol()
        assert sync.running is False
        assert sync.role is None
        assert sync.tx_socket is None
        assert sync.rx_socket is None
        assert sync.tempo_bpm == 120.0
        assert sync.phase == 0.0
        assert sync.drift_ewma == 0.0
        assert sync._on_tick is None
        assert sync._on_cue is None
        assert sync.clock_state == ClockState.LOCAL
        assert sync._missed_packets == 0
        assert sync._total_packets_received == 0
        assert sync._total_packets_sent == 0


class TestSyncProtocolMaster:
    """Tests for SyncProtocol master mode."""
    
    def test_start_master_sets_role(self):
        """Test that starting master sets role correctly."""
        sync = SyncProtocol()
        sync.start_master()
        try:
            assert sync.role == 'master'
            assert sync.running is True
            assert sync.tx_socket is not None
        finally:
            sync.stop()
    
    def test_start_master_creates_socket(self):
        """Test that master creates UDP socket."""
        sync = SyncProtocol()
        sync.start_master()
        try:
            assert sync.tx_socket is not None
            assert sync.tx_socket.type == socket.SOCK_DGRAM
        finally:
            sync.stop()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_master_broadcasts_packets(self):
        """Test that master broadcasts sync packets."""
        master = SyncProtocol()
        slave_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        slave_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        slave_socket.bind(('', MCAST_PORT))
        slave_socket.settimeout(1.0)
        
        mreq = socket.inet_aton(MCAST_GRP) + socket.inet_aton('0.0.0.0')
        slave_socket.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        
        try:
            master.start_master()
            time.sleep(0.2)  # Wait for at least one packet
            
            data, addr = slave_socket.recvfrom(1024)
            packet = json.loads(data.decode('utf-8'))
            
            assert 'timestamp_ms' in packet
            assert 'type' in packet
            assert packet['type'] == SYNC_PACKET_TICK
            assert packet['timestamp_ms'] > 0
        finally:
            master.stop()
            slave_socket.close()


class TestSyncProtocolSlave:
    """Tests for SyncProtocol slave mode."""
    
    def test_start_slave_sets_role(self):
        """Test that starting slave sets role correctly."""
        sync = SyncProtocol()
        sync.start_slave()
        try:
            assert sync.role == 'slave'
            assert sync.running is True
            assert sync.rx_socket is not None
        finally:
            sync.stop()
    
    def test_start_slave_creates_socket(self):
        """Test that slave creates UDP socket."""
        sync = SyncProtocol()
        sync.start_slave()
        try:
            assert sync.rx_socket is not None
            assert sync.rx_socket.type == socket.SOCK_DGRAM
        finally:
            sync.stop()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_slave_receives_tick_packets(self):
        """Test that slave receives and processes tick packets."""
        slave = SyncProtocol()
        received_ticks = []
        
        def on_tick(timestamp_ms, drift_ewma):
            received_ticks.append((timestamp_ms, drift_ewma))
        
        slave.on_tick(on_tick)
        slave.start_slave()
        
        # Create a simple sender to broadcast tick packets
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            time.sleep(0.1)  # Let slave start up
            
            # Send a tick packet
            timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_TICK})
            tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
            
            time.sleep(0.2)  # Wait for processing
            
            assert len(received_ticks) > 0
            assert received_ticks[0][0] == timestamp
        finally:
            slave.stop()
            tx_socket.close()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_slave_receives_cue_packets(self):
        """Test that slave receives and processes cue packets."""
        slave = SyncProtocol()
        received_cues = []
        
        def on_cue(timestamp_ms):
            received_cues.append(timestamp_ms)
        
        slave.on_cue(on_cue)
        slave.start_slave()
        
        # Create a simple sender to broadcast cue packets
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            time.sleep(0.1)  # Let slave start up
            
            # Send a cue packet
            timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_CUE})
            tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
            
            time.sleep(0.2)  # Wait for processing
            
            assert len(received_cues) > 0
            assert received_cues[0] == timestamp
        finally:
            slave.stop()
            tx_socket.close()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_slave_calculates_drift_ewma(self):
        """Test that slave calculates drift EWMA correctly."""
        slave = SyncProtocol()
        received_drifts = []
        
        def on_tick(timestamp_ms, drift_ewma):
            received_drifts.append(drift_ewma)
        
        slave.on_tick(on_tick)
        slave.start_slave()
        
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            time.sleep(0.1)
            
            # Send multiple packets to test EWMA calculation
            for _ in range(3):
                timestamp = int(time.time() * 1000) & 0xFFFFFFFF
                packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_TICK})
                tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
                time.sleep(0.1)
            
            time.sleep(0.2)
            
            # Should have received multiple ticks and calculated EWMA
            assert len(received_drifts) >= 2
        finally:
            slave.stop()
            tx_socket.close()


class TestSyncProtocolCallbacks:
    """Tests for callback registration."""
    
    def test_on_tick_registers_callback(self):
        """Test that on_tick registers callback correctly."""
        sync = SyncProtocol()
        
        def my_callback(timestamp_ms, drift_ewma):
            pass
        
        sync.on_tick(my_callback)
        assert sync._on_tick is my_callback
    
    def test_on_cue_registers_callback(self):
        """Test that on_cue registers callback correctly."""
        sync = SyncProtocol()
        
        def my_callback(timestamp_ms):
            pass
        
        sync.on_cue(my_callback)
        assert sync._on_cue is my_callback


class TestSyncProtocolStop:
    """Tests for stopping sync protocol."""
    
    def test_stop_master(self):
        """Test that stop closes master socket."""
        sync = SyncProtocol()
        sync.start_master()
        socket_ref = sync.tx_socket
        sync.stop()
        
        assert sync.running is False
        # Socket should be closed, attempting to use it should raise error
        if socket_ref:
            with pytest.raises(OSError):
                socket_ref.sendto(b'test', ('127.0.0.1', 12345))
    
    def test_stop_slave(self):
        """Test that stop closes slave socket."""
        sync = SyncProtocol()
        sync.start_slave()
        socket_ref = sync.rx_socket
        sync.stop()
        
        assert sync.running is False
        # Socket should be closed, attempting to use it should raise error
        if socket_ref:
            with pytest.raises(OSError):
                socket_ref.recvfrom(1024)
    
    def test_stop_without_start(self):
        """Test that stop works even if start was never called."""
        sync = SyncProtocol()
        sync.stop()  # Should not raise any errors
        assert sync.running is False


class TestSyncProtocolConstants:
    """Tests for protocol constants."""
    
    def test_multicast_group(self):
        """Test that multicast group is correctly defined."""
        assert MCAST_GRP == "239.10.7.42"
    
    def test_multicast_port(self):
        """Test that multicast port is correctly defined."""
        assert MCAST_PORT == 45454
    
    def test_packet_types(self):
        """Test that packet types are correctly defined."""
        assert SYNC_PACKET_TICK == "tick"
        assert SYNC_PACKET_CUE == "cue"


class TestSyncProtocolIntegration:
    """Integration tests for master-slave communication."""
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_master_slave_communication(self):
        """Test that master and slave can communicate."""
        master = SyncProtocol()
        slave = SyncProtocol()
        received_ticks = []
        
        def on_tick(timestamp_ms, drift_ewma):
            received_ticks.append((timestamp_ms, drift_ewma))
        
        slave.on_tick(on_tick)
        
        try:
            slave.start_slave()
            time.sleep(0.1)  # Let slave bind
            
            master.start_master()
            time.sleep(0.3)  # Wait for a few packets
            
            assert len(received_ticks) > 0
            # Verify we received valid timestamps
            for timestamp, drift in received_ticks:
                assert timestamp > 0
        finally:
            master.stop()
            slave.stop()


class TestSyncProtocolClockStates:
    """Tests for clock state management."""
    
    def test_master_starts_in_local_state(self):
        """Test that master starts in LOCAL clock state."""
        sync = SyncProtocol()
        sync.start_master()
        try:
            assert sync.clock_state == ClockState.LOCAL
        finally:
            sync.stop()
    
    def test_slave_starts_in_syncing_state(self):
        """Test that slave starts in SYNCING clock state."""
        sync = SyncProtocol()
        sync.start_slave()
        try:
            assert sync.clock_state == ClockState.SYNCING
        finally:
            sync.stop()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_slave_transitions_to_synced(self):
        """Test that slave transitions to SYNCED when receiving packets."""
        slave = SyncProtocol()
        slave.start_slave()
        
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            time.sleep(0.1)
            
            # Send a packet to trigger sync
            timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_TICK})
            tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
            
            time.sleep(0.2)
            
            assert slave.clock_state == ClockState.SYNCED
        finally:
            slave.stop()
            tx_socket.close()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_slave_transitions_to_desync_on_packet_loss(self):
        """Test that slave transitions to DESYNC after missing packets."""
        slave = SyncProtocol()
        slave.start_slave()
        
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            # First, get slave into SYNCED state
            time.sleep(0.1)
            timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_TICK})
            tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
            time.sleep(0.2)
            assert slave.clock_state == ClockState.SYNCED
            
            # Now wait for timeout to trigger DESYNC
            # DESYNC_THRESHOLD is 5, timeout is 0.2s, so ~1 second should be enough
            time.sleep(1.5)
            
            assert slave.clock_state == ClockState.DESYNC
        finally:
            slave.stop()
            tx_socket.close()


class TestSyncProtocolPacketLoss:
    """Tests for packet loss detection and handling."""
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_packet_loss_increments_missed_counter(self):
        """Test that missing packets increments the missed counter."""
        slave = SyncProtocol()
        slave.start_slave()
        
        try:
            # Wait for several timeout periods without sending packets
            time.sleep(1.0)
            
            # Should have registered missed packets
            assert slave._missed_packets > 0
        finally:
            slave.stop()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_packet_reception_resets_missed_counter(self):
        """Test that receiving a packet resets the missed counter."""
        slave = SyncProtocol()
        slave.start_slave()
        
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            # Wait for some missed packets
            time.sleep(0.5)
            assert slave._missed_packets > 0
            
            # Send a packet
            timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_TICK})
            tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
            
            time.sleep(0.2)
            
            # Missed counter should be reset
            assert slave._missed_packets == 0
        finally:
            slave.stop()
            tx_socket.close()


class TestSyncProtocolStatus:
    """Tests for status reporting."""
    
    def test_master_status(self):
        """Test master status reporting."""
        sync = SyncProtocol()
        sync.start_master()
        
        try:
            time.sleep(0.2)
            status = sync.get_status()
            
            assert status['running'] is True
            assert status['role'] == 'master'
            assert status['clock_state'] == 'local'
            assert 'packets_sent' in status
            # Don't check if packets were sent (requires multicast privileges)
        finally:
            sync.stop()
    
    def test_slave_status(self):
        """Test slave status reporting."""
        sync = SyncProtocol()
        sync.start_slave()
        
        try:
            time.sleep(0.2)
            status = sync.get_status()
            
            assert status['running'] is True
            assert status['role'] == 'slave'
            assert 'packets_received' in status
            assert 'missed_packets' in status
            assert 'drift_ewma_ms' in status
        finally:
            sync.stop()
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_slave_status_with_packets(self):
        """Test slave status after receiving packets."""
        slave = SyncProtocol()
        slave.start_slave()
        
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            time.sleep(0.1)
            
            # Send multiple packets
            for _ in range(5):
                timestamp = int(time.time() * 1000) & 0xFFFFFFFF
                packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_TICK})
                tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
                time.sleep(0.05)
            
            time.sleep(0.2)
            status = slave.get_status()
            
            assert status['packets_received'] >= 5
            assert status['clock_state'] == 'synced'
            assert 'avg_jitter_ms' in status  # Should have jitter data after multiple packets
        finally:
            slave.stop()
            tx_socket.close()


class TestSyncProtocolJitter:
    """Tests for jitter tracking and detection."""
    
    @pytest.mark.skipif(not MULTICAST_AVAILABLE, reason="Multicast requires elevated privileges")
    def test_jitter_tracking(self):
        """Test that jitter is tracked correctly."""
        slave = SyncProtocol()
        slave.start_slave()
        
        tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        tx_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        
        try:
            time.sleep(0.1)
            
            # Send packets with varying timestamps to simulate jitter
            for i in range(5):
                timestamp = (int(time.time() * 1000) + i * 10) & 0xFFFFFFFF
                packet = json.dumps({"timestamp_ms": timestamp, "type": SYNC_PACKET_TICK})
                tx_socket.sendto(packet.encode('utf-8'), (MCAST_GRP, MCAST_PORT))
                time.sleep(0.05)
            
            time.sleep(0.2)
            
            # Should have accumulated jitter values
            assert len(slave._jitter_values) > 0
            assert len(slave._jitter_values) <= 10  # Max size is 10
        finally:
            slave.stop()
            tx_socket.close()
