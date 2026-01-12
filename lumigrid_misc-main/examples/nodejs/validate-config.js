/**
 * Node Configuration Validation Example
 * 
 * Demonstrates how to validate node configuration using JSON Schema
 * for initial setup and runtime configuration updates
 */

const Ajv = require('ajv');
const addFormats = require('ajv-formats');
const fs = require('fs');
const path = require('path');

const ajv = new Ajv({ allErrors: true, strict: true });
addFormats(ajv);  // Add format validation for IPv4

// Load the config schema
const schemaPath = path.join(__dirname, '../../schemas/config.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const validate = ajv.compile(schema);

// Example 1: Minimal valid configuration
const validConfig1 = {
  node_id: "node-001",
  node_type: "led_controller"
};

// Example 2: Complete LED controller configuration
const validConfig2 = {
  node_id: "led-kitchen-001",
  node_name: "Kitchen LED Controller",
  node_type: "led_controller",
  network: {
    ssid: "MyWiFi",
    password: "secret123",
    ip_address: "192.168.1.100",
    gateway: "192.168.1.1",
    subnet_mask: "255.255.255.0"
  },
  hardware: {
    led_pins: {
      red: 25,
      green: 26,
      blue: 27,
      white: 32
    },
    rf_frequency_mhz: 433.92,
    common_anode: false
  },
  sync: {
    enabled: true,
    udp_port: 5555,
    interval_ms: 1000
  },
  presets: {
    button1: {
      r: 28,
      g: 78,
      b: 255,
      w: 0
    },
    button2: {
      r: 0,
      g: 0,
      b: 0,
      w: 255
    }
  }
};

// Example 3: Sensor node configuration
const validConfig3 = {
  node_id: "sensor-garage-001",
  node_name: "Garage Sensor",
  node_type: "sensor",
  network: {
    ssid: "IoT_Network",
    ip_address: "192.168.1.50",
    gateway: "192.168.1.1",
    subnet_mask: "255.255.255.0"
  },
  sync: {
    enabled: true,
    udp_port: 5555,
    interval_ms: 5000
  }
};

// Example 4: Invalid - wrong node type
const invalidConfig1 = {
  node_id: "node-invalid",
  node_type: "invalid_type"  // Not in enum
};

// Example 5: Invalid - malformed IP address
const invalidConfig2 = {
  node_id: "node-bad-ip",
  node_type: "gateway",
  network: {
    ip_address: "999.999.999.999"  // Invalid IPv4
  }
};

console.log('=== LumiGrid Node Configuration Validation Examples ===\n');

// Test valid configurations
console.log('Example 1: Minimal configuration');
if (validate(validConfig1)) {
  console.log('✓ Valid configuration');
  console.log(JSON.stringify(validConfig1, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 2: Complete LED controller');
if (validate(validConfig2)) {
  console.log('✓ Valid configuration');
  console.log(JSON.stringify(validConfig2, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 3: Sensor node');
if (validate(validConfig3)) {
  console.log('✓ Valid configuration');
  console.log(JSON.stringify(validConfig3, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

// Test invalid configurations
console.log('Example 4: Invalid - wrong node type');
if (validate(invalidConfig1)) {
  console.log('✓ Valid configuration');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 5: Invalid - malformed IP');
if (validate(invalidConfig2)) {
  console.log('✓ Valid configuration');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '='.repeat(60));
console.log('\nValidation complete!');
console.log('\nNode types: led_controller, display, sensor, gateway');
console.log('Usage: Validate before saving/loading node configuration');
