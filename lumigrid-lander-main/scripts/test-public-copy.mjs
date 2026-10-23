import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const marketingSource = readFileSync(resolve(root, 'src/lib/publicContent.ts'), 'utf8');
const landingSource = readFileSync(resolve(root, 'src/routes/+page.svelte'), 'utf8');
const start = 'export const publicContent = ';
const begin = marketingSource.indexOf(start);
const end = marketingSource.lastIndexOf(' as const;');
assert(begin !== -1 && end > begin, 'marketing content could not be parsed');
const translations = JSON.parse(marketingSource.slice(begin + start.length, end));
const locales = ['en', 'lv', 'ru', 'nl', 'de'];

assert.deepEqual(Object.keys(translations), locales, 'all supported languages must be present');
const keys = Object.keys(translations.en).sort();
const requestedFields = [...new Set([...landingSource.matchAll(/copy\.(\w+)/g)].map(match => match[1]))];
for (const locale of locales) {
  const translation = translations[locale];
  assert.deepEqual(Object.keys(translation).sort(), keys, locale + ' must have matching copy fields');
  for (const field of requestedFields) assert(field in translation, locale + ': missing ' + field);
  assert(translation.nav.length === 4, locale + ': navigation must have four items');
  assert(translation.features.length === 6, locale + ': expected six benefits');
  assert(translation.steps.length === 3, locale + ': expected three setup steps');
  for (const feature of translation.features) {
    assert(feature.length === 3 && ['LED_NODE','EFFECTS','PROTOCOLS'].includes(feature[2]), 'invalid customer guide link');
  }
}
const chipTerms = /\b(?:esp32|pca9685|ws2812|sk6812|rmt|i2c|pwm|mcu|chipset|firmware|bootloader|microcontroller|flash)\b/i;
for (const locale of locales) {
  const plain = JSON.stringify(translations[locale]);
  assert(!chipTerms.test(plain), locale + ': internal hardware terminology appeared in product copy');
}
const markup = landingSource
  .replace(/<script\b[^>]*>[\s\S]*?<\/script>/g, '')
  .replace(/<style\b[^>]*>[\s\S]*?<\/style>/g, '')
  .replace(/\{[^{}]*\}/g, '')
  .replace(/<[^>]+>/g, ' ');
assert(!chipTerms.test(markup), 'internal hardware terminology appeared in visible landing markup');
for (const field of ['addressable', 'pwm', 'scenes', 'effects'])
  assert(landingSource.includes('limits.' + field), 'source-backed capacity missing: ' + field);
assert(!landingSource.includes('/flasher'), 'firmware installation must not be a primary marketing CTA');
assert(landingSource.includes('GETTING_STARTED') && landingSource.includes('/dashboard'), 'customer actions must be usable');
console.log('PASS: LED Node product positioning, five-language parity, honest capacities, customer links, zero component terms');
