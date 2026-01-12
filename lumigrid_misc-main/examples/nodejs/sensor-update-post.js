/**
 * Sensor Update POST Example
 * 
 * Demonstrates how to POST sensor data to a LumiGrid node's /api/sensor_update endpoint
 * with proper validation
 */

const Ajv = require('ajv');
const axios = require('axios');
const fs = require('fs');
const path = require('path');

// Load and compile schema
const ajv = new Ajv({ allErrors: true, strict: true });
const schemaPath = path.join(__dirname, '../../schemas/sensor-data.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const validate = ajv.compile(schema);

/**
 * Post sensor data to a LumiGrid node
 * @param {string} nodeUrl - Base URL of the node (e.g., 'http://192.168.1.100')
 * @param {object} sensorData - Sensor data payload to send
 * @returns {Promise<object>} Response from the server
 */
async function postSensorUpdate(nodeUrl, sensorData) {
  // Validate data before sending
  if (!validate(sensorData)) {
    throw new Error(`Validation failed: ${JSON.stringify(validate.errors)}`);
  }

  const url = `${nodeUrl}/api/sensor_update`;
  
  try {
    const response = await axios.post(url, sensorData, {
      headers: {
        'Content-Type': 'application/json'
      },
      timeout: 5000
    });
    
    console.log(`✓ Successfully posted to ${url}`);
    console.log(`  Status: ${response.status}`);
    console.log(`  Response:`, response.data);
    
    return response.data;
  } catch (error) {
    if (error.response) {
      console.error(`✗ Server error: ${error.response.status}`, error.response.data);
    } else if (error.request) {
      console.error(`✗ No response from server (${url})`);
    } else {
      console.error(`✗ Request error:`, error.message);
    }
    throw error;
  }
}

// Example usage
async function main() {
  // Configuration - replace with your node's IP
  const NODE_URL = process.env.NODE_URL || 'http://192.168.4.1';
  
  console.log('=== LumiGrid Sensor Update POST Example ===\n');
  console.log(`Target node: ${NODE_URL}/api/sensor_update\n`);

  // Example 1: Basic temperature and humidity reading
  const sensorData1 = {
    node_id: "sensor-room-001",
    node_name: "Living Room Sensor",
    timestamp: Math.floor(Date.now() / 1000),
    readings: {
      temperature_c: 22.5,
      humidity_percent: 48.3
    }
  };

  console.log('Example 1: Basic sensor reading');
  console.log(JSON.stringify(sensorData1, null, 2));
  
  try {
    await postSensorUpdate(NODE_URL, sensorData1);
  } catch (error) {
    console.log('(Skipping - node may not be available)\n');
  }

  console.log('\n' + '-'.repeat(60) + '\n');

  // Example 2: Complete sensor reading with all fields
  const sensorData2 = {
    node_id: "sensor-outdoor-001",
    node_name: "Outdoor Sensor",
    timestamp: Math.floor(Date.now() / 1000),
    readings: {
      temperature_c: 18.2,
      humidity_percent: 65.7,
      pressure_hpa: 1015.3,
      light_lux: 850.5,
      motion_detected: false
    }
  };

  console.log('Example 2: Complete sensor reading');
  console.log(JSON.stringify(sensorData2, null, 2));
  
  try {
    await postSensorUpdate(NODE_URL, sensorData2);
  } catch (error) {
    console.log('(Skipping - node may not be available)\n');
  }

  console.log('\n' + '-'.repeat(60) + '\n');

  // Example 3: Motion detection event
  const sensorData3 = {
    node_id: "sensor-security-001",
    timestamp: Math.floor(Date.now() / 1000),
    readings: {
      motion_detected: true,
      light_lux: 120.0
    }
  };

  console.log('Example 3: Motion detection');
  console.log(JSON.stringify(sensorData3, null, 2));
  
  try {
    await postSensorUpdate(NODE_URL, sensorData3);
  } catch (error) {
    console.log('(Skipping - node may not be available)\n');
  }

  console.log('\n' + '='.repeat(60));
  console.log('\nUsage notes:');
  console.log('- Set NODE_URL environment variable to target a specific node');
  console.log('- All data is validated against schema before sending');
  console.log('- Example: NODE_URL=http://192.168.1.100 node sensor-update-post.js');
}

// Run if executed directly
if (require.main === module) {
  main().catch(console.error);
}

module.exports = { postSensorUpdate };
