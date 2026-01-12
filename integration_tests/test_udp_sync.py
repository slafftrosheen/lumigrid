import json
import socket
import time
import pytest

# This is a simplified version of the UDPSyncClient from the example,
# adapted for testing.
class MockUDPSyncClient:
    def __init__(self, port):
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    def send(self, packet):
        message = json.dumps(packet).encode('utf-8')
        self.sock.sendto(message, ('127.0.0.1', self.port))

    def close(self):
        self.sock.close()

# This is a simplified version of the listener from the example.
class MockUDPSyncServer:
    def __init__(self, port):
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(('', self.port))
        self.sock.settimeout(2.0)

    def receive(self):
        try:
            data, _ = self.sock.recvfrom(4096)
            return json.loads(data.decode('utf-8'))
        except socket.timeout:
            return None
        finally:
            self.sock.close()

def test_udp_sync_packet_transmission():
    """
    Tests that a UDP sync packet can be sent by a client and received by a server.
    """
    port = 5556  # Use a different port than the default to avoid conflicts
    client = MockUDPSyncClient(port)
    server = MockUDPSyncServer(port)

    # A sample packet that conforms to the sync-packet.schema.json
    test_packet = {
        "packet_type": "heartbeat",
        "node_id": "test_sender",
        "timestamp": int(time.time() * 1000),
        "sequence_number": 0,
        "payload": {"status": "ok"},
        "target_nodes": []
    }

    try:
        client.send(test_packet)
        received_packet = server.receive()

        assert received_packet is not None, "Server did not receive a packet"
        assert received_packet["packet_type"] == "heartbeat"
        assert received_packet["node_id"] == "test_sender"
        assert received_packet["payload"]["status"] == "ok"

    finally:
        client.close()
        # The server socket is closed in the receive method
