/**
 * UDP Sync Example
 * 
 * Demonstrates sending and receiving UDP synchronization packets
 * for inter-node communication in LumiGrid network
 */

const dgram = require('dgram');
const Ajv = require('ajv');
const fs = require('fs');
const path = require('path');

// Load and compile sync packet schema
const ajv = new Ajv({ allErrors: true, strict: true });
const schemaPath = path.join(__dirname, '../../schemas/sync-packet.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const validate = ajv.compile(schema);

/**
 * UDP Sync Client for LumiGrid
 */
class UDPSyncClient {
  constructor(nodeId, port = 5555, broadcastAddress = '255.255.255.255') {
    this.nodeId = nodeId;
    this.port = port;
    this.broadcastAddress = broadcastAddress;
    this.sequenceNumber = 0;
    this.socket = null;
    this.messageHandlers = [];
  }

  /**
   * Start listening for UDP packets
   */
  startListening(callback) {
    this.socket = dgram.createSocket('udp4');

    this.socket.on('error', (err) => {
      console.error(`Socket error: ${err.message}`);
      this.socket.close();
    });

    this.socket.on('message', (msg, rinfo) => {
      try {
        const packet = JSON.parse(msg.toString());
        
        // Validate received packet
        if (!validate(packet)) {
          console.warn(`Invalid packet from ${rinfo.address}:`, validate.errors);
          return;
        }

        console.log(`\n📨 Received from ${rinfo.address}:${rinfo.port}`);
        console.log(`   Type: ${packet.packet_type}`);
        console.log(`   Node: ${packet.node_id}`);
        console.log(`   Payload:`, JSON.stringify(packet.payload));

        // Call registered handlers
        this.messageHandlers.forEach(handler => handler(packet, rinfo));
        
        if (callback) callback(packet, rinfo);
      } catch (error) {
        console.error('Error parsing packet:', error.message);
      }
    });

    this.socket.on('listening', () => {
      const address = this.socket.address();
      console.log(`📡 UDP Sync listening on ${address.address}:${address.port}`);
      this.socket.setBroadcast(true);
    });

    this.socket.bind(this.port);
  }

  /**
   * Send a sync packet
   */
  send(packetType, payload, targetNodes = []) {
    const packet = {
      packet_type: packetType,
      node_id: this.nodeId,
      timestamp: Date.now(),
      sequence_number: this.sequenceNumber++,
      payload: payload,
      target_nodes: targetNodes
    };

    // Validate before sending
    if (!validate(packet)) {
      throw new Error(`Invalid packet: ${JSON.stringify(validate.errors)}`);
    }

    const message = Buffer.from(JSON.stringify(packet));
    
    this.socket.send(message, 0, message.length, this.port, this.broadcastAddress, (err) => {
      if (err) {
        console.error(`Send error: ${err.message}`);
      } else {
        console.log(`\n📤 Sent ${packetType} packet (seq: ${packet.sequence_number})`);
        console.log(`   Payload:`, JSON.stringify(payload));
      }
    });
  }

  /**
   * Send a heartbeat packet
   * 
   * Note: uptimeMs can be process uptime (process.uptime() * 1000) or any
   * meaningful time metric. The UDP sync packet schema doesn't strictly
   * define what this represents, allowing flexibility across implementations.
   */
  sendHeartbeat(status = 'ok', uptimeMs = process.uptime() * 1000) {
    this.send('heartbeat', {
      status: status,
      uptime_ms: Math.floor(uptimeMs)
    });
  }

  /**
   * Send a color change packet
   */
  sendColorChange(color) {
    this.send('color_change', { color });
  }

  /**
   * Send an effect trigger packet
   */
  sendEffectTrigger(effect, durationMs, targetNodes = []) {
    this.send('effect_trigger', {
      effect: effect,
      duration_ms: durationMs
    }, targetNodes);
  }

  /**
   * Send a state update packet
   */
  sendStateUpdate(state) {
    this.send('state_update', state);
  }

  /**
   * Register a message handler
   */
  onMessage(handler) {
    this.messageHandlers.push(handler);
  }

  /**
   * Stop listening and close socket
   */
  stop() {
    if (this.socket) {
      this.socket.close();
      console.log('\n📡 UDP Sync stopped');
    }
  }
}

// Example usage
async function main() {
  const NODE_ID = process.env.NODE_ID || 'example-node-001';
  const UDP_PORT = parseInt(process.env.UDP_PORT || '5555');
  
  console.log('=== LumiGrid UDP Sync Example ===\n');
  console.log(`Node ID: ${NODE_ID}`);
  console.log(`UDP Port: ${UDP_PORT}\n`);

  const client = new UDPSyncClient(NODE_ID, UDP_PORT);

  // Register message handler
  client.onMessage((packet, rinfo) => {
    // Handle specific packet types
    switch (packet.packet_type) {
      case 'color_change':
        console.log('   🎨 Color change detected');
        break;
      case 'effect_trigger':
        console.log('   ✨ Effect trigger detected');
        break;
      case 'heartbeat':
        console.log('   💓 Heartbeat received');
        break;
    }
  });

  // Start listening
  client.startListening();

  // Send example packets
  console.log('\nSending example packets...\n');

  // Wait a bit for socket to be ready
  await new Promise(resolve => setTimeout(resolve, 500));

  // Example 1: Send heartbeat
  client.sendHeartbeat();

  await new Promise(resolve => setTimeout(resolve, 1000));

  // Example 2: Send color change
  client.sendColorChange({
    r: 255,
    g: 100,
    b: 50,
    w: 0
  });

  await new Promise(resolve => setTimeout(resolve, 1000));

  // Example 3: Send effect trigger to specific nodes
  client.sendEffectTrigger('rainbow', 5000, ['led-001', 'led-002']);

  await new Promise(resolve => setTimeout(resolve, 1000));

  // Example 4: Send state update (broadcast)
  client.sendStateUpdate({
    preset_id: 3,
    active: true,
    brightness: 80
  });

  // Keep running to receive packets
  console.log('\n' + '='.repeat(60));
  console.log('\nListening for packets... (Press Ctrl+C to exit)');
  console.log('\nUsage notes:');
  console.log('- Set NODE_ID environment variable for your node identifier');
  console.log('- Set UDP_PORT to use a different port (default: 5555)');
  console.log('- Run multiple instances to test inter-node communication');
  console.log('\nExample: NODE_ID=led-001 UDP_PORT=5555 node udp-sync.js');

  // Graceful shutdown
  process.on('SIGINT', () => {
    console.log('\n\nShutting down...');
    client.stop();
    process.exit(0);
  });
}

// Run if executed directly
if (require.main === module) {
  main().catch(console.error);
}

module.exports = { UDPSyncClient };
