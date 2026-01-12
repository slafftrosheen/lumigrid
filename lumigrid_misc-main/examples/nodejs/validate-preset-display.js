/**
 * Display Preset Validation Example
 * 
 * Demonstrates how to validate display frame presets using JSON Schema
 * for matrix LED displays and animations
 */

const Ajv = require('ajv');
const fs = require('fs');
const path = require('path');

const ajv = new Ajv({ allErrors: true, strict: true });

// Load the preset display schema
const schemaPath = path.join(__dirname, '../../schemas/preset-display.schema.json');
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const validate = ajv.compile(schema);

// Helper function to create a simple pattern
function createSolidColor(width, height, r, g, b, w = undefined) {
  const frame_data = [];
  for (let i = 0; i < width * height; i++) {
    const pixel = { r, g, b };
    if (w !== undefined) pixel.w = w;
    frame_data.push(pixel);
  }
  return frame_data;
}

// Example 1: Valid 8x8 display preset - red frame
const validDisplay1 = {
  preset_id: 1,
  name: "Red Screen",
  width: 8,
  height: 8,
  frame_data: createSolidColor(8, 8, 255, 0, 0)
};

// Example 2: Valid 16x16 RGBW display with duration and loop
const validDisplay2 = {
  preset_id: 2,
  name: "Cool Blue Pattern",
  width: 16,
  height: 16,
  frame_data: createSolidColor(16, 16, 0, 100, 255, 50),
  duration_ms: 1000,
  loop: true
};

// Example 3: Valid small 4x4 display
const validDisplay3 = {
  preset_id: 3,
  name: "Smiley Face",
  width: 4,
  height: 4,
  frame_data: [
    // Row 1
    { r: 0, g: 0, b: 0 },
    { r: 255, g: 255, b: 0 },
    { r: 255, g: 255, b: 0 },
    { r: 0, g: 0, b: 0 },
    // Row 2
    { r: 255, g: 255, b: 0 },
    { r: 0, g: 0, b: 0 },
    { r: 0, g: 0, b: 0 },
    { r: 255, g: 255, b: 0 },
    // Row 3
    { r: 255, g: 255, b: 0 },
    { r: 0, g: 0, b: 0 },
    { r: 0, g: 0, b: 0 },
    { r: 255, g: 255, b: 0 },
    // Row 4
    { r: 0, g: 0, b: 0 },
    { r: 255, g: 255, b: 0 },
    { r: 255, g: 255, b: 0 },
    { r: 0, g: 0, b: 0 }
  ]
};

// Example 4: Invalid - pixel color out of range
const invalidDisplay1 = {
  preset_id: 4,
  name: "Invalid Color",
  width: 2,
  height: 2,
  frame_data: [
    { r: 256, g: 0, b: 0 },  // Invalid: r exceeds 255
    { r: 0, g: 0, b: 0 },
    { r: 0, g: 0, b: 0 },
    { r: 0, g: 0, b: 0 }
  ]
};

// Example 5: Invalid - missing required pixel field
const invalidDisplay2 = {
  preset_id: 5,
  name: "Missing Field",
  width: 2,
  height: 2,
  frame_data: [
    { r: 100, g: 50 },  // Missing 'b' field
    { r: 0, g: 0, b: 0 },
    { r: 0, g: 0, b: 0 },
    { r: 0, g: 0, b: 0 }
  ]
};

console.log('=== LumiGrid Display Preset Validation Examples ===\n');

// Test valid displays
console.log('Example 1: 8x8 red screen');
if (validate(validDisplay1)) {
  console.log('✓ Valid display preset');
  console.log(`  ${validDisplay1.width}x${validDisplay1.height}, ${validDisplay1.frame_data.length} pixels`);
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 2: 16x16 RGBW with loop');
if (validate(validDisplay2)) {
  console.log('✓ Valid display preset');
  console.log(JSON.stringify(validDisplay2, null, 2).substring(0, 400) + '...');
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 3: 4x4 smiley face pattern');
if (validate(validDisplay3)) {
  console.log('✓ Valid display preset');
  console.log(JSON.stringify(validDisplay3, null, 2));
} else {
  console.log('✗ Invalid:', validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

// Test invalid displays
console.log('Example 4: Invalid - color out of range');
if (validate(invalidDisplay1)) {
  console.log('✓ Valid display preset');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '-'.repeat(60) + '\n');

console.log('Example 5: Invalid - missing pixel field');
if (validate(invalidDisplay2)) {
  console.log('✓ Valid display preset');
} else {
  console.log('✗ Invalid (as expected):');
  console.log(validate.errors);
}

console.log('\n' + '='.repeat(60));
console.log('\nValidation complete!');
console.log('\nUsage: Validate display frames before rendering');
console.log('Note: frame_data length should equal width * height');
