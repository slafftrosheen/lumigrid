import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const contentPath = resolve(root, 'src/lib/publicContent.ts');
const homePath = resolve(root, 'src/routes/+page.svelte');
const startPath = resolve(root, 'src/routes/start/+page.svelte');
const docsPath = resolve(root, 'src/routes/docs/+page.svelte');
const sources = {
  home: readFileSync(homePath, 'utf8'),
  start: readFileSync(startPath, 'utf8'),
  docs: readFileSync(docsPath, 'utf8')
};
const marketingSource = readFileSync(contentPath, 'utf8');
const prefix = 'export const publicContent = ';
const begin = marketingSource.indexOf(prefix);
const end = marketingSource.lastIndexOf(' as const;');
assert(begin >= 0 && end > begin, 'cannot parse public translations');
const translations = JSON.parse(marketingSource.slice(begin + prefix.length, end));
const locales = ['en', 'lv', 'ru', 'nl', 'de'];

assert.deepEqual(Object.keys(translations), locales, 'supported languages must match');
const keys = Object.keys(translations.en).sort();
const bannedHardwareTerms = /\b(?:esp32|pca9685|ws2812|sk6812|rmt|i2c|pwm|mcu|chipset|firmware|bootloader|microcontroller|flash)\b/i;
const unsupportedPromises = /\b(?:ce certified|ce approved|fully certified|waterproof|guaranteed zero flicker|no power supply needed)\b/i;

function usedCopyFields(source) {
  return [...new Set([...source.matchAll(/copy\.(\w+)/g)].map(m => m[1]))];
}
function visibleMarkup(source) {
  return source
    .replace(/<script\b[^>]*>[\s\S]*?<\/script>/g, '')
    .replace(/<style\b[^>]*>[\s\S]*?<\/style>/g, '')
    .replace(/\{[^{}]*\}/g, '')
    .replace(/<[^>]+>/g, ' ');
}
for (const locale of locales) {
  const t = translations[locale];
  assert.deepEqual(Object.keys(t).sort(), keys, locale + ': missing or extra fields');
  assert.equal(t.nav.length, 4, locale + ': navigation');
  assert.equal(t.features.length, 6, locale + ': feature count');
  assert.equal(t.cases.length, 3, locale + ': case count');
  assert.equal(t.steps.length, 3, locale + ': landing steps');
  assert.equal(t.startGuideSteps.length, 4, locale + ': setup guide steps');
  for (const feature of t.features) assert.equal(feature.length, 2, locale + ': dead engineering link in benefit');
  for (const feature of t.cases) assert.equal(feature.length, 2, locale + ': application story');
  for (const step of t.startGuideSteps) assert.equal(step.length, 2, locale + ': setup content');
  for (const field of [...usedCopyFields(sources.home), ...usedCopyFields(sources.start)])
    assert(field in t, locale + ': missing rendered field ' + field);
  const copy = JSON.stringify(t);
  assert(!bannedHardwareTerms.test(copy), locale + ': internal component name leaked');
  assert(!unsupportedPromises.test(copy), locale + ': unsupported certification or performance promise');
}

for (const route of ['home', 'start']) {
  const visible = visibleMarkup(sources[route]);
  assert(!bannedHardwareTerms.test(visible), route + ': component name leaked into page');
  assert(!unsupportedPromises.test(visible), route + ': unsupported promise');
}
for (const field of ['addressable', 'pwm', 'scenes', 'effects'])
  assert(sources.home.includes('limits.' + field), 'source-backed capacity missing: ' + field);

assert(!sources.home.includes('/docs/GETTING_STARTED'), 'primary customer CTA must not open engineering instructions');
assert(!sources.home.includes('/flasher'), 'software installer must not be a marketing CTA');
assert(sources.home.includes('href={`${base}/start`}'), 'customer setup route not linked');
assert(sources.home.includes('href={`${base}/dashboard`}'), 'control app route not linked');
assert(sources.home.includes('class:motion=') && sources.home.includes('aria-pressed={previewMode'), 'accessible preview selector missing');
assert(sources.home.includes('prefers-reduced-motion'), 'preview must honor reduced motion');
assert(sources.start.includes('http://192.168.4.1/') && sources.start.includes('startGuideAddressNote'),
  'new-device setup access and qualification must be present');
assert(sources.start.includes('docs/GETTING_STARTED'), 'advanced installer route must stay available');
assert(sources.docs.includes("['/start', 'Start with LED Node'"), 'docs landing should prioritize customer setup');
assert(sources.docs.includes('<details class="advanced">'), 'engineering references should not dominate docs landing');

for (const file of ['static/assets/downloads/product-sheet.pdf', 'static/assets/downloads/technical-specs.pdf']) {
  if (existsSync(resolve(root, file))) {
    const buffer = readFileSync(resolve(root, file));
    assert(buffer.subarray(0, 5).toString() === '%PDF-', file + ' must be a genuine PDF, not a placeholder');
  }
}

console.log('PASS: LED Node customer messaging, five-language parity, interactive preview, setup journey, grounded capabilities and no technical chip marketing');
