"""
UDP Sync Example

Demonstrates sending and receiving UDP synchronization packets
for inter-node communication in LumiGrid network
"""

import json
import os
import socket
import time
import threading
from pathlib import Path
from jsonschema import validate, ValidationError

# Load schema
schema_path = Path(__file__).parent.parent.parent / 'schemas' / 'sync-packet.schema.json'
with open(schema_path) as f:
    schema = json.load(f)

def validate_sync_packet(data):
    """Validate sync packet against schema"""
    try:
        validate(instance=data, schema=schema)
        return True, None
    except ValidationError as e:
        return False, str(e)

class UDPSyncClient:
    """UDP Sync Client for LumiGrid"""
    
    def __init__(self, node_id, port=5555, broadcast_address='255.255.255.255'):
        self.node_id = node_id
        self.port = port
        self.broadcast_address = broadcast_address
        self.sequence_number = 0
        self.sock = None
        self.running = False
        self.message_handlers = []
    
    def start_listening(self, callback=None):
        """Start listening for UDP packets"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        
        self.sock.bind(('', self.port))
        print(f'📡 UDP Sync listening on port {self.port}')
        
        self.running = True
        
        def listen():
            while self.running:
                try:
                    self.sock.settimeout(1.0)
                    data, addr = self.sock.recvfrom(4096)
                    
                    try:
                        packet = json.loads(data.decode('utf-8'))
                        
                        # Validate received packet
                        is_valid, error = validate_sync_packet(packet)
                        if not is_valid:
                            print(f'Invalid packet from {addr}: {error}')
                            continue
                        
                        print(f'\n📨 Received from {addr[0]}:{addr[1]}')
                        print(f'   Type: {packet["packet_type"]}')
                        print(f'   Node: {packet["node_id"]}')
                        print(f'   Payload: {json.dumps(packet.get("payload", {}))}')
                        
                        # Call registered handlers
                        for handler in self.message_handlers:
                            handler(packet, addr)
                        
                        if callback:
                            callback(packet, addr)
                            
                    except json.JSONDecodeError as e:
                        print(f'Error parsing packet: {e}')
                        
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f'Error receiving: {e}')
        
        # Start listening thread
        self.listen_thread = threading.Thread(target=listen, daemon=True)
        self.listen_thread.start()
    
    def send(self, packet_type, payload, target_nodes=None):
        """Send a sync packet"""
        if target_nodes is None:
            target_nodes = []
            
        packet = {
            "packet_type": packet_type,
            "node_id": self.node_id,
            "timestamp": int(time.time() * 1000),
            "sequence_number": self.sequence_number,
            "payload": payload,
            "target_nodes": target_nodes
        }
        
        self.sequence_number += 1
        
        # Validate before sending
        is_valid, error = validate_sync_packet(packet)
        if not is_valid:
            raise ValueError(f'Invalid packet: {error}')
        
        message = json.dumps(packet).encode('utf-8')
        
        try:
            self.sock.sendto(message, (self.broadcast_address, self.port))
            print(f'\n📤 Sent {packet_type} packet (seq: {packet["sequence_number"]})')
            print(f'   Payload: {json.dumps(payload)}')
        except Exception as e:
            print(f'Send error: {e}')
    
    def send_heartbeat(self, status='ok', uptime_ms=None):
        """
        Send a heartbeat packet
        
        Note: uptime_ms can be process uptime, wall clock time, or any meaningful
        time metric. The schema doesn't strictly define this field's meaning,
        allowing flexibility across implementations.
        """
        if uptime_ms is None:
            # Default to wall clock time in milliseconds
            uptime_ms = int(time.time() * 1000)
        
        self.send('heartbeat', {
            'status': status,
            'uptime_ms': uptime_ms
        })
    
    def send_color_change(self, color):
        """Send a color change packet"""
        self.send('color_change', {'color': color})
    
    def send_effect_trigger(self, effect, duration_ms, target_nodes=None):
        """Send an effect trigger packet"""
        if target_nodes is None:
            target_nodes = []
            
        self.send('effect_trigger', {
            'effect': effect,
            'duration_ms': duration_ms
        }, target_nodes)
    
    def send_state_update(self, state):
        """Send a state update packet"""
        self.send('state_update', state)
    
    def on_message(self, handler):
        """Register a message handler"""
        self.message_handlers.append(handler)
    
    def stop(self):
        """Stop listening and close socket"""
        self.running = False
        if self.sock:
            self.sock.close()
        print('\n📡 UDP Sync stopped')

def main():
    node_id = os.environ.get('NODE_ID', 'example-node-001')
    udp_port = int(os.environ.get('UDP_PORT', '5555'))
    
    print('=== LumiGrid UDP Sync Example ===\n')
    print(f'Node ID: {node_id}')
    print(f'UDP Port: {udp_port}\n')

    client = UDPSyncClient(node_id, udp_port)

    # Register message handler
    def handle_message(packet, addr):
        packet_type = packet['packet_type']
        if packet_type == 'color_change':
            print('   🎨 Color change detected')
        elif packet_type == 'effect_trigger':
            print('   ✨ Effect trigger detected')
        elif packet_type == 'heartbeat':
            print('   💓 Heartbeat received')
    
    client.on_message(handle_message)

    # Start listening
    client.start_listening()

    # Send example packets
    print('\nSending example packets...\n')
    time.sleep(0.5)

    # Example 1: Send heartbeat
    client.send_heartbeat()
    time.sleep(1)

    # Example 2: Send color change
    client.send_color_change({
        'r': 255,
        'g': 100,
        'b': 50,
        'w': 0
    })
    time.sleep(1)

    # Example 3: Send effect trigger to specific nodes
    client.send_effect_trigger('rainbow', 5000, ['led-001', 'led-002'])
    time.sleep(1)

    # Example 4: Send state update (broadcast)
    client.send_state_update({
        'preset_id': 3,
        'active': True,
        'brightness': 80
    })

    # Keep running to receive packets
    print('\n' + '=' * 60)
    print('\nListening for packets... (Press Ctrl+C to exit)')
    print('\nUsage notes:')
    print('- Set NODE_ID environment variable for your node identifier')
    print('- Set UDP_PORT to use a different port (default: 5555)')
    print('- Run multiple instances to test inter-node communication')
    print('\nExample: NODE_ID=led-001 UDP_PORT=5555 python udp_sync.py')

    try:
        # Keep main thread alive
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print('\n\nShutting down...')
        client.stop()

if __name__ == '__main__':
    main()
