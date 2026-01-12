/**
 * Sensor Data Validation Example
 * 
 * Demonstrates how to validate sensor data payloads using JSON Schema
 * before sending to /api/sensor_update endpoint
 */

const Ajv = require('ajv');
const fs = require('fs');
const path = require('path');

// Initialize AJV with strict mode
const ajv = new Ajv({ allErrors: true, strict: true });

// Load the sensor data schema
const schemaPath = path.join(__dirname, '../../schemas/sensor-data.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));

// Compile the schema for validation
const validate = ajv.compile(schema);

// Example 1: Valid sensor data with temperature and humidity
const validData1 = {
  node_id: "node-001",
  readings: {
    temperature_c: 23.5,
    humidity_percent: 45.2
  }
};

// Example 2: Valid sensor data with all fields
const validData2 = {
  node_id: "sensor-kitchen",
  node_name: "Kitchen Sensor",
  timestamp: Math.floor(Date.now() / 1000),
  readings: {
    temperature_c: 22.1,
    humidity_percent: 52.3,
    pressure_hpa: 1013.25,
    light_lux: 350.5,
    motion_detected: true
  }
};

// Example 3: Invalid data - missing required field
const invalidData1 = {
  node_id: "node-003",
  // missing 'readings' field
};

// Example 4: Invalid data - humidity out of range
const invalidData2 = {
  node_id: "node-004",
  readings: {
    temperature_c: 25.0,
    humidity_percent: 150  // Invalid: exceeds maximum of 100
  }
};

console.log('=== LumiGrid Sensor Data Validation Examples ===\n');

// Test valid data
console.log('Example 1: Basic sensor data');
if (validate(validData1)) {
  console.log('✓ Valid sensor data');
  console.log(JSON.stringify(validData1, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 2: Complete sensor data');
if (validate(validData2)) {
  console.log('✓ Valid sensor data');
  console.log(JSON.stringify(validData2, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

// Test invalid data
console.log('Example 3: Invalid - missing readings');
if (validate(invalidData1)) {
  console.log('✓ Valid sensor data');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 4: Invalid - humidity out of range');
if (validate(invalidData2)) {
  console.log('✓ Valid sensor data');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '='.repeat(60));
console.log('\nValidation complete!');
console.log('\nUsage: Integrate this validation before POSTing to /api/sensor_update');
console.log('Example: if (validate(data)) { await axios.post(url, data); }');
