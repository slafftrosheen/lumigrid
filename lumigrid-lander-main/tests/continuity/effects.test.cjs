const {test} = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const fx = require(path.join(process.env.LG_COMPILED, 'effectCatalog.cjs'));

test('successful empty device catalogue stays empty', () => {
  assert.deepEqual(fx.normalizeEffectCatalog([]), []);
  assert.deepEqual(fx.normalizePaletteCatalog([]), []);
});
test('legacy fallback never invents expanded device IDs', () => {
  assert.equal(fx.normalizeEffectCatalog(null).length, 12);
  assert.equal(fx.normalizePaletteCatalog(undefined).length, 6);
});
test('unknown non-spatial effect does not imply dimmer support', () => {
  assert.equal(fx.normalizeEffectCatalog([{id:'future-color', spatial:false}])[0].pwmRecommended, false);
});
test('unknown effect can explicitly declare dimmer support', () => {
  assert.equal(fx.normalizeEffectCatalog([{id:'future-dimmer', pwmRecommended:true}])[0].pwmRecommended, true);
});
test('explicit false and zero from device override local hints', () => {
  const [e] = fx.normalizeEffectCatalog([{id:'candle',pwmRecommended:false, parameterDefault:0, supportsSpeed:false}]);
  assert.equal(e.pwmRecommended, false); assert.equal(e.parameterDefault, 0); assert.equal(e.supportsSpeed, false);
});
test('malformed text cannot become object text in the editor', () => {
  const [e] = fx.normalizeEffectCatalog([{id:'candle',description:{x:1},parameterLabel:[],parameterUnit:42}]);
  assert.equal(typeof e.description, 'string');
  assert.equal(typeof e.parameterLabel, 'string');
  assert.equal(typeof e.parameterUnit, 'string');
});
test('IDs are normalized and duplicates rejected without modifying raw response', () => {
  const raw = [{id:' candle ',label:'Warm'}, {id:'candle'}, {id:''}, null];
  const before = JSON.stringify(raw);
  const data = fx.normalizeEffectCatalog(raw);
  assert.equal(data.length,1); assert.equal(data[0].id,'candle'); assert.equal(JSON.stringify(raw),before);
});
test('bounded parameter metadata never leaks NaN or inverted ranges', () => {
  const [e] = fx.normalizeEffectCatalog([{id:'scanner',parameterMin:240,parameterMax:10,parameterDefault:NaN}]);
  assert.equal(e.parameterMin,240); assert.equal(e.parameterMax,240); assert.equal(e.parameterDefault,240);
});
test('local catalogue keeps all 24 stable effect IDs and 14 palette IDs', () => {
  assert.equal(fx.EFFECT_METADATA.length,24); assert.equal(new Set(fx.EFFECT_METADATA.map(e=>e.id)).size,24);
  assert.equal(fx.PALETTE_METADATA.length,14);
  assert.equal(fx.EFFECT_METADATA[0].id,'solid'); assert.equal(fx.EFFECT_METADATA[23].id,'confetti');
});
test('missing saved effect is explained without selecting a replacement', () => {
  assert.equal(fx.effectGuidance(undefined,8).state,'unavailable');
});
test('a single dimmer is not represented as an animated color strip', () => {
  const e=fx.EFFECT_METADATA.find(e=>e.id==='rainbow');
  assert.equal(fx.effectGuidance(e,8).state,'limited');
  assert.match(fx.effectGuidance(e,8).message,/brightness/i);
});
test('dimmer-compatible brightness rhythm is recommended', () => {
  assert.equal(fx.effectGuidance(fx.EFFECT_METADATA.find(e=>e.id==='candle'),15).state,'recommended');
});
test('one-pixel spatial effect explains lack of movement', () => {
  assert.equal(fx.effectGuidance(fx.EFFECT_METADATA.find(e=>e.id==='scanner'),0,1).state,'limited');
});
test('unknown output mapping never claims compatibility', () => {
  assert.equal(fx.effectGuidance(fx.EFFECT_METADATA[0],16).state,'unknown');
  assert.equal(fx.effectGuidance(fx.EFFECT_METADATA[0],NaN).state,'unknown');
});
test('guidance leaves stored effect values untouched', () => {
  const meta={id:'future',label:'Future',description:'',spatial:false};
  const before=JSON.stringify(meta);
  assert.equal(fx.effectGuidance(meta,8).state,'unknown');
  assert.equal(JSON.stringify(meta),before);
});
