/**
 * Preset CRUD Operations Example
 * 
 * Demonstrates Create, Read, Update, Delete operations for LED presets
 * with validation using JSON Schema
 */

const Ajv = require('ajv');
const axios = require('axios');
const fs = require('fs');
const path = require('path');

// Load and compile LED preset schema
const ajv = new Ajv({ allErrors: true, strict: true });
const schemaPath = path.join(__dirname, '../../schemas/preset-led.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const validate = ajv.compile(schema);

/**
 * LumiGrid Preset API Client
 */
class PresetClient {
  constructor(nodeUrl) {
    this.nodeUrl = nodeUrl;
    this.baseUrl = `${nodeUrl}/api/presets`;
  }

  /**
   * Create a new preset
   */
  async create(preset) {
    if (!validate(preset)) {
      throw new Error(`Validation failed: ${JSON.stringify(validate.errors)}`);
    }

    try {
      const response = await axios.post(this.baseUrl, preset, {
        headers: { 'Content-Type': 'application/json' },
        timeout: 5000
      });
      console.log(`✓ Created preset ${preset.preset_id}: "${preset.name}"`);
      return response.data;
    } catch (error) {
      this.handleError('create', error);
    }
  }

  /**
   * Read a preset by ID
   */
  async read(presetId) {
    try {
      const response = await axios.get(`${this.baseUrl}/${presetId}`, {
        timeout: 5000
      });
      console.log(`✓ Read preset ${presetId}`);
      return response.data;
    } catch (error) {
      this.handleError('read', error);
    }
  }

  /**
   * List all presets
   */
  async list() {
    try {
      const response = await axios.get(this.baseUrl, {
        timeout: 5000
      });
      console.log(`✓ Retrieved ${response.data.length || 0} presets`);
      return response.data;
    } catch (error) {
      this.handleError('list', error);
    }
  }

  /**
   * Update an existing preset
   */
  async update(presetId, preset) {
    if (!validate(preset)) {
      throw new Error(`Validation failed: ${JSON.stringify(validate.errors)}`);
    }

    try {
      const response = await axios.put(`${this.baseUrl}/${presetId}`, preset, {
        headers: { 'Content-Type': 'application/json' },
        timeout: 5000
      });
      console.log(`✓ Updated preset ${presetId}`);
      return response.data;
    } catch (error) {
      this.handleError('update', error);
    }
  }

  /**
   * Delete a preset
   */
  async delete(presetId) {
    try {
      const response = await axios.delete(`${this.baseUrl}/${presetId}`, {
        timeout: 5000
      });
      console.log(`✓ Deleted preset ${presetId}`);
      return response.data;
    } catch (error) {
      this.handleError('delete', error);
    }
  }

  /**
   * Activate a preset (apply it to LEDs)
   */
  async activate(presetId) {
    try {
      const response = await axios.post(`${this.baseUrl}/${presetId}/activate`, {}, {
        timeout: 5000
      });
      console.log(`✓ Activated preset ${presetId}`);
      return response.data;
    } catch (error) {
      this.handleError('activate', error);
    }
  }

  handleError(operation, error) {
    if (error.response) {
      console.error(`✗ ${operation} failed: ${error.response.status}`, error.response.data);
    } else if (error.request) {
      console.error(`✗ ${operation} failed: No response from server`);
    } else {
      console.error(`✗ ${operation} error:`, error.message);
    }
    throw error;
  }
}

// Example usage
async function main() {
  const NODE_URL = process.env.NODE_URL || 'http://192.168.4.1';
  
  console.log('=== LumiGrid Preset CRUD Example ===\n');
  console.log(`Target node: ${NODE_URL}/api/presets\n`);

  const client = new PresetClient(NODE_URL);

  // Define some example presets
  const coolBlue = {
    preset_id: 1,
    name: "Cool Blue",
    color: { r: 28, g: 78, b: 255, w: 0 },
    effect: "static"
  };

  const warmWhite = {
    preset_id: 2,
    name: "Warm White",
    color: { r: 0, g: 0, b: 0, w: 255 },
    effect: "fade",
    duration_ms: 2000
  };

  const rainbow = {
    preset_id: 3,
    name: "Rainbow Party",
    color: { r: 255, g: 0, b: 0, w: 0 },
    effect: "rainbow",
    duration_ms: 5000
  };

  try {
    // CREATE operations
    console.log('--- CREATE ---');
    console.log('Creating preset 1: Cool Blue');
    console.log(JSON.stringify(coolBlue, null, 2));
    await client.create(coolBlue);
    
    console.log('\nCreating preset 2: Warm White');
    await client.create(warmWhite);
    
    console.log('\nCreating preset 3: Rainbow Party');
    await client.create(rainbow);

    console.log('\n' + '-'.repeat(60) + '\n');

    // READ operations
    console.log('--- READ ---');
    console.log('Reading preset 1:');
    const preset1 = await client.read(1);
    console.log(JSON.stringify(preset1, null, 2));

    console.log('\n' + '-'.repeat(60) + '\n');

    // LIST operation
    console.log('--- LIST ---');
    const allPresets = await client.list();
    console.log('All presets:');
    console.log(JSON.stringify(allPresets, null, 2));

    console.log('\n' + '-'.repeat(60) + '\n');

    // UPDATE operation
    console.log('--- UPDATE ---');
    const updatedBlue = {
      ...coolBlue,
      name: "Deep Blue",
      color: { r: 0, g: 50, b: 200, w: 0 }
    };
    console.log('Updating preset 1 to Deep Blue:');
    await client.update(1, updatedBlue);

    console.log('\n' + '-'.repeat(60) + '\n');

    // ACTIVATE operation
    console.log('--- ACTIVATE ---');
    console.log('Activating preset 2 (Warm White):');
    await client.activate(2);

    console.log('\n' + '-'.repeat(60) + '\n');

    // DELETE operation
    console.log('--- DELETE ---');
    console.log('Deleting preset 3:');
    await client.delete(3);

  } catch (error) {
    console.log('\n(Skipping - node may not be available or API not implemented)');
  }

  console.log('\n' + '='.repeat(60));
  console.log('\nUsage notes:');
  console.log('- Set NODE_URL environment variable to target a specific node');
  console.log('- All presets are validated before sending');
  console.log('- Example: NODE_URL=http://192.168.1.100 node preset-crud.js');
  console.log('\nAPI endpoints used:');
  console.log('  POST   /api/presets           - Create preset');
  console.log('  GET    /api/presets           - List all presets');
  console.log('  GET    /api/presets/:id       - Read preset');
  console.log('  PUT    /api/presets/:id       - Update preset');
  console.log('  DELETE /api/presets/:id       - Delete preset');
  console.log('  POST   /api/presets/:id/activate - Activate preset');
}

// Run if executed directly
if (require.main === module) {
  main().catch(console.error);
}

module.exports = { PresetClient };
