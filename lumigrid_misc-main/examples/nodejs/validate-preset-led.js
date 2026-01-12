/**
 * LED Preset Validation Example
 * 
 * Demonstrates how to validate LED preset data using JSON Schema
 * for CRUD operations on LED presets
 */

const Ajv = require('ajv');
const fs = require('fs');
const path = require('path');

const ajv = new Ajv({ allErrors: true, strict: true });

// Load the preset LED schema
const schemaPath = path.join(__dirname, '../../schemas/preset-led.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const validate = ajv.compile(schema);

// Example 1: Valid LED preset - static blue
const validPreset1 = {
  preset_id: 1,
  name: "Cool Blue",
  color: {
    r: 28,
    g: 78,
    b: 255,
    w: 0
  },
  effect: "static"
};

// Example 2: Valid LED preset - warm white with fade effect
const validPreset2 = {
  preset_id: 2,
  name: "Warm White Fade",
  color: {
    r: 0,
    g: 0,
    b: 0,
    w: 255
  },
  effect: "fade",
  duration_ms: 2000
};

// Example 3: Valid LED preset - rainbow effect
const validPreset3 = {
  preset_id: 3,
  name: "Rainbow",
  color: {
    r: 255,
    g: 0,
    b: 0,
    w: 0
  },
  effect: "rainbow",
  duration_ms: 5000
};

// Example 4: Invalid preset - color value out of range
const invalidPreset1 = {
  preset_id: 4,
  name: "Invalid Red",
  color: {
    r: 300,  // Invalid: exceeds 255
    g: 0,
    b: 0,
    w: 0
  }
};

// Example 5: Invalid preset - missing required color field
const invalidPreset2 = {
  preset_id: 5,
  name: "Incomplete",
  color: {
    r: 100,
    g: 50,
    b: 200
    // missing 'w' field
  }
};

console.log('=== LumiGrid LED Preset Validation Examples ===\n');

// Test valid presets
console.log('Example 1: Static blue preset');
if (validate(validPreset1)) {
  console.log('✓ Valid LED preset');
  console.log(JSON.stringify(validPreset1, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 2: Warm white with fade');
if (validate(validPreset2)) {
  console.log('✓ Valid LED preset');
  console.log(JSON.stringify(validPreset2, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 3: Rainbow effect');
if (validate(validPreset3)) {
  console.log('✓ Valid LED preset');
  console.log(JSON.stringify(validPreset3, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

// Test invalid presets
console.log('Example 4: Invalid - color out of range');
if (validate(invalidPreset1)) {
  console.log('✓ Valid LED preset');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 5: Invalid - missing required field');
if (validate(invalidPreset2)) {
  console.log('✓ Valid LED preset');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '='.repeat(60));
console.log('\nValidation complete!');
console.log('\nUsage: Validate before saving/loading presets');
console.log('Available effects: static, fade, pulse, strobe, rainbow');
