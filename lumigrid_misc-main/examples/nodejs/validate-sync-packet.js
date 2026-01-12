/**
 * UDP Sync Packet Validation Example
 * 
 * Demonstrates how to validate UDP synchronization packets using JSON Schema
 * for inter-node communication and state synchronization
 */

const Ajv = require('ajv');
const fs = require('fs');
const path = require('path');

const ajv = new Ajv({ allErrors: true, strict: true });

// Load the sync packet schema
const schemaPath = path.join(__dirname, '../../schemas/sync-packet.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const validate = ajv.compile(schema);

// Example 1: Color change sync packet
const validPacket1 = {
  packet_type: "color_change",
  node_id: "led-001",
  timestamp: Date.now(),
  payload: {
    color: {
      r: 255,
      g: 100,
      b: 50,
      w: 0
    }
  }
};

// Example 2: Heartbeat packet with sequence number
const validPacket2 = {
  packet_type: "heartbeat",
  node_id: "sensor-kitchen",
  timestamp: Date.now(),
  sequence_number: 42,
  payload: {
    uptime_ms: 123456789,
    status: "ok"
  }
};

// Example 3: Effect trigger targeting specific nodes
const validPacket3 = {
  packet_type: "effect_trigger",
  node_id: "gateway-001",
  timestamp: Date.now(),
  sequence_number: 100,
  payload: {
    effect: "rainbow",
    duration_ms: 5000
  },
  target_nodes: ["led-001", "led-002", "led-003"]
};

// Example 4: State update broadcast (all nodes)
const validPacket4 = {
  packet_type: "state_update",
  node_id: "controller-main",
  timestamp: Date.now(),
  payload: {
    preset_id: 5,
    active: true
  },
  target_nodes: []  // Empty array = broadcast to all
};

// Example 5: Config change notification
const validPacket5 = {
  packet_type: "config_change",
  node_id: "led-bedroom",
  timestamp: Date.now(),
  payload: {
    changed_fields: ["brightness", "sync.enabled"],
    brightness: 80,
    sync_enabled: false
  }
};

// Example 6: Invalid - wrong packet type
const invalidPacket1 = {
  packet_type: "unknown_type",  // Not in enum
  node_id: "node-001",
  timestamp: Date.now()
};

// Example 7: Invalid - missing required field
const invalidPacket2 = {
  packet_type: "heartbeat",
  node_id: "node-002"
  // missing 'timestamp'
};

console.log('=== LumiGrid UDP Sync Packet Validation Examples ===\n');

// Test valid packets
console.log('Example 1: Color change packet');
if (validate(validPacket1)) {
  console.log('✓ Valid sync packet');
  console.log(JSON.stringify(validPacket1, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 2: Heartbeat with sequence');
if (validate(validPacket2)) {
  console.log('✓ Valid sync packet');
  console.log(JSON.stringify(validPacket2, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 3: Effect trigger (targeted)');
if (validate(validPacket3)) {
  console.log('✓ Valid sync packet');
  console.log(JSON.stringify(validPacket3, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 4: State update (broadcast)');
if (validate(validPacket4)) {
  console.log('✓ Valid sync packet');
  console.log(JSON.stringify(validPacket4, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 5: Config change notification');
if (validate(validPacket5)) {
  console.log('✓ Valid sync packet');
  console.log(JSON.stringify(validPacket5, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

// Test invalid packets
console.log('Example 6: Invalid - unknown packet type');
if (validate(invalidPacket1)) {
  console.log('✓ Valid sync packet');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 7: Invalid - missing timestamp');
if (validate(invalidPacket2)) {
  console.log('✓ Valid sync packet');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '='.repeat(60));
console.log('\nValidation complete!');
console.log('\nPacket types: state_update, color_change, effect_trigger, heartbeat, config_change');
console.log('Usage: Validate before sending/receiving UDP packets');
