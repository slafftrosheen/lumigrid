/**
 * Run All Validation Examples
 * 
 * Executes all validation scripts to verify schemas are working correctly
 */

const { execSync } = require('child_process');
const path = require('path');

const examples = [
  'validate-sensor-data.js',
  'validate-preset-led.js',
  'validate-preset-display.js',
  'validate-config.js',
  'validate-sync-packet.js'
];

console.log('=== Running All LumiGrid Validation Examples ===\n');

let passed = 0;
let failed = 0;

examples.forEach((example, index) => {
  console.log(`\n${'='.repeat(70)}`);
  console.log(`Running ${index + 1}/${examples.length}: ${example}`);
  console.log('='.repeat(70) + '\n');
  
  try {
    execSync(`node ${example}`, {
      cwd: __dirname,
      stdio: 'inherit'
    });
    passed++;
  } catch (error) {
    console.error(`\n✗ Failed to run ${example}`);
    failed++;
  }
});

console.log('\n' + '='.repeat(70));
console.log('\n📊 Summary:');
console.log(`   ✓ Passed: ${passed}/${examples.length}`);
console.log(`   ✗ Failed: ${failed}/${examples.length}`);
console.log('='.repeat(70) + '\n');

if (failed > 0) {
  process.exit(1);
}
