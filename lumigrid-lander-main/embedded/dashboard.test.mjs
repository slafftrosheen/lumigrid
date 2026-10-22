import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('./dashboard.html', import.meta.url), 'utf8');
const script = [...source.matchAll(/<script>([\s\S]*?)<\/script>/g)]
  .map(match => match[1]).find(code => code.includes('window.lg = (() =>'));
const plain = value => JSON.parse(JSON.stringify(value));

// Run the actual embedded script with a small DOM and mocked node API. Only
// expose the closure for assertions; the production functions are unchanged.
function dashboard(responses = {}) {
  const elements = new Map();
  const calls = [];
  const add = (id, value = '') => {
    const element = { value, checked: false, hidden: false, textContent: '', style: {},
      classList: { add() {}, remove() {} }, addEventListener() {} };
    elements.set(id, element);
    return element;
  };
  for (const id of ['experience-host', 'experience-token', 'first-setup', 'normal-login', 'setup-unavailable',
    'setup-defaults', 'experience-toast', 'scene-editor-title', 'scene-name', 'scene-brightness',
    'scene-brightness-out', 'scene-fade', 'scene-fade-out']) add(id);
  const editor = add('scene-zones-editor');
  let markup = '';
  Object.defineProperty(editor, 'innerHTML', {
    get: () => markup,
    set(html) {
      markup = html;
      for (const id of elements.keys()) if (/^l\d-/.test(id)) elements.delete(id);
      for (const match of html.matchAll(/<input\b([^>]*)>/g)) {
        const attrs = match[1];
        const id = attrs.match(/\bid="([^"]+)"/)?.[1];
        if (id) {
          const input = add(id, attrs.match(/\bvalue="([^"]*)"/)?.[1] ?? '');
          input.checked = /\bchecked\b/.test(attrs);
        }
      }
      for (const match of html.matchAll(/<select\b[^>]*\bid="([^"]+)"[^>]*>([\s\S]*?)<\/select>/g)) {
        const options = [...match[2].matchAll(/<option\b([^>]*)>/g)].map(m => m[1]);
        const selected = options.find(attrs => /\bselected\b/.test(attrs)) ?? options[0] ?? '';
        add(match[1], selected.match(/\bvalue="([^"]*)"/)?.[1] ?? '');
      }
    },
  });
  const context = vm.createContext({
    window: {}, document: { getElementById: id => elements.get(id) ?? null, querySelectorAll: () => [], querySelector: () => null },
    localStorage: { getItem: () => null }, sessionStorage: { getItem: () => null },
    location: { protocol: 'http:', host: 'node.test', hostname: 'node.test' },
    performance: { now: () => 0 }, setTimeout: () => 0, clearTimeout() {},
    fetch: async (url, options) => {
      const path = new URL(url).pathname;
      calls.push({ path, ...options });
      const value = responses[path];
      if (value instanceof Error) throw value;
      return { ok: true, json: async () => plain(value ?? (path === '/api/v1/auth/status' ? { provisioned: true } : {})) };
    },
  });
  const exposed = script.replace('    return {\n        connect, disconnect, refresh, nav, playScene, blackout,',
    '    window.testing = { state, resolveEffects, resolvePalettes, renderSceneEditor, readSceneEditor, prepareScenePayload, effectDefaults, paletteDefaults };\n    return {\n        connect, disconnect, refresh, nav, playScene, blackout,');
  assert.notEqual(exposed, script, 'test hook must attach to the application return');
  vm.runInContext(exposed, context, { filename: 'dashboard.html' });
  const api = context.window.testing;
  return { ...api, lg: context.window.lg, elements, calls, editor };
}

function layer(overrides = {}) {
  return { effect: 'solid', blendMode: 'normal', primaryColor: { r: 10, g: 20, b: 30 },
    secondaryColor: { r: 4, g: 5, b: 6 }, paletteId: 0, speedMs: 2000, fxParam: 0,
    opacity: 255, enabled: true, ...overrides };
}

function scene(first = {}) {
  return { index: 2, name: 'FX test', globalBrightness: 255, transitionMs: 1000,
    channels: Array.from({ length: 16 }, (_, c) => ({ layers: [layer(c === 0 ? first : {}), layer({ enabled: false }), layer({ enabled: false })] })),
    // Current firmware deliberately emits an abbreviated legacy mirror.
    zones: Array.from({ length: 8 }, () => ({ layers: [{ effect: 'solid', blendMode: 'normal', enabled: true }] })),
  };
}

function startEditing(app, first = {}) {
  app.state.effects = app.resolveEffects(app.effectDefaults);
  app.state.palettes = app.resolvePalettes(app.paletteDefaults);
  app.state.editing = scene(first);
  app.state.editing.zones = plain(app.state.editing.channels.slice(0, 8));
  app.renderSceneEditor();
}

test('reported catalogues are authoritative, including empty and older lists', () => {
  const app = dashboard();
  assert.equal(app.resolveEffects(app.effectDefaults).length, 24);
  assert.equal(app.resolvePalettes(app.paletteDefaults).length, 14);
  assert.equal(app.resolveEffects(app.effectDefaults.slice(0, 12)).length, 12);
  assert.equal(app.resolvePalettes(app.paletteDefaults.slice(0, 6)).length, 6);
  assert.equal(app.resolveEffects([]).length, 0);
  assert.equal(app.resolvePalettes([]).length, 0);
  assert.equal(app.resolveEffects(null).length, 12);
  assert.equal(app.resolvePalettes(null).length, 6);
});

test('partial returned metadata keeps explicit flags and zero defaults', () => {
  const app = dashboard();
  const result = app.resolveEffects([{ id: 'candle', parameterDefault: 0 }, { id: 'confetti', supportsPalette: false }]);
  assert.equal(result.length, 2);
  assert.equal(result[0].parameterDefault, 0);
  assert.equal(result[0].pwmRecommended, true);
  assert.equal(result[1].supportsPalette, false);
});

test('refresh fetches all palettes and edits full channel values', async () => {
  const catalogue = dashboard();
  const preset = scene({ effect: 'confetti', paletteId: 13, fxParam: 15, speedMs: 4500 });
  const app = dashboard({ '/api/v1/effects': catalogue.effectDefaults, '/api/v1/palettes': catalogue.paletteDefaults,
    '/api/v1/presets': [preset] });
  await app.lg.refresh();
  assert.ok(app.calls.some(call => call.path === '/api/v1/palettes'));
  assert.equal(app.state.palettes.length, 14);
  assert.equal(app.state.presets[0].zones[0].layers[0].paletteId, 13);
  assert.equal(app.state.presets[0].zones[0].layers[0].speedMs, 4500);
  app.lg.editScene(2);
  assert.equal(app.elements.get('l0-palette').value, '13');
  assert.match(app.editor.innerHTML, />Twilight<\/option>/);
});

test('zero parameters survive rendering and gated controls preserve stored settings', () => {
  const app = dashboard();
  startEditing(app, { effect: 'candle', fxParam: 0, paletteId: 13 });
  assert.equal(app.elements.get('l0-param').value, '0');
  assert.equal(app.elements.has('l0-palette'), false);
  assert.equal(app.elements.has('l0-scolor'), false);
  app.readSceneEditor();
  assert.equal(app.state.editing.zones[0].layers[0].fxParam, 0);
  assert.equal(app.state.editing.zones[0].layers[0].paletteId, 13);
  assert.deepEqual(plain(app.state.editing.zones[0].layers[0].secondaryColor), { r: 4, g: 5, b: 6 });

  startEditing(app, { effect: 'solid', speedMs: 2345, fxParam: 37, paletteId: 13 });
  assert.equal(app.elements.has('l0-speed'), false);
  app.readSceneEditor();
  assert.equal(app.state.editing.zones[0].layers[0].speedMs, 2345);
  assert.equal(app.state.editing.zones[0].layers[0].fxParam, 37);
});

test('unknown selected effects stay explicit and cannot silently become solid', () => {
  const app = dashboard();
  startEditing(app, { effect: 'future-fx', fxParam: 42 });
  assert.equal(app.elements.get('l0-effect').value, 'future-fx');
  assert.match(app.editor.innerHTML, /future-fx \(Unavailable on this node\)/);
  app.readSceneEditor();
  assert.equal(app.state.editing.zones[0].layers[0].effect, 'future-fx');
  assert.equal(app.state.editing.zones[0].layers[0].fxParam, 42);
  assert.throws(() => app.prepareScenePayload(app.state.editing), /future-fx.*unavailable/);
});

test('unsupported saved palette IDs stay selected and block incompatible saves', () => {
  const app = dashboard();
  startEditing(app, { effect: 'confetti', paletteId: 13 });
  app.state.palettes = app.resolvePalettes(app.paletteDefaults.slice(0, 6));
  app.renderSceneEditor();
  assert.equal(app.elements.get('l0-palette').value, '13');
  assert.match(app.editor.innerHTML, /Palette 13 \(Unavailable on this node\)/);
  assert.throws(() => app.prepareScenePayload(app.state.editing), /palette 13.*unavailable/);
});

test('saving writes edited channel objects and preserves every PWM channel', async () => {
  const app = dashboard();
  startEditing(app, { effect: 'confetti', paletteId: 13, fxParam: 15 });
  const pwm = plain(app.state.editing.channels.slice(8));
  app.elements.get('l0-param').value = '30';
  await app.lg.saveScene();
  const request = app.calls.find(call => call.path === '/api/v1/presets' && call.method === 'PUT');
  assert.ok(request, 'a scene save must send a PUT');
  const payload = JSON.parse(request.body);
  assert.equal(payload.channels[0].layers[0].effect, 'confetti');
  assert.equal(payload.channels[0].layers[0].paletteId, 13);
  assert.equal(payload.channels[0].layers[0].fxParam, 30);
  assert.deepEqual(payload.channels.slice(8), pwm);
});

test('legacy zones-only scenes remain editable and keep the legacy payload', () => {
  const app = dashboard();
  startEditing(app, { effect: 'breathe', fxParam: 4 });
  delete app.state.editing.channels;
  app.readSceneEditor();
  const payload = app.prepareScenePayload(app.state.editing);
  assert.equal(payload.channels, undefined);
  assert.equal(payload.zones[0].layers[0].effect, 'breathe');
});

test('effect changes apply the reported default and PWM suitability is visible', () => {
  const app = dashboard();
  startEditing(app);
  delete app.state.editing.channels;
  app.state.effects = app.resolveEffects([{ id: 'solid' }, { id: 'candle', parameterDefault: 0 }, { id: 'confetti' }]);
  app.state.config = { fixtures: [{ zoneId: 0, profile: 3 }] };
  app.lg.changeLayerEffect(0, 'candle');
  assert.equal(app.state.editing.zones[0].layers[0].fxParam, 0);
  assert.doesNotMatch(app.editor.innerHTML, /not recommended.*PWM/);
  app.lg.changeLayerEffect(0, 'confetti');
  assert.match(app.editor.innerHTML, /not recommended.*PWM/);
});

test('current scenes expose sixteen named outputs and a PWM level control', () => {
  const app = dashboard();
  app.state.config = { channels: [
    { index: 8, port: 'P1', kind: 'pwm', name: 'Lobby dimmer' },
    { index: 15, port: 'P8', kind: 'pwm', name: 'Last dimmer' },
  ] };
  startEditing(app);
  assert.match(app.editor.innerHTML, /A1/);
  assert.match(app.editor.innerHTML, /A8/);
  assert.match(app.editor.innerHTML, /P1 · Lobby dimmer/);
  assert.match(app.editor.innerHTML, /P8 · Last dimmer/);
  app.lg.toggleTargetZone(8);
  assert.equal(app.state.targetZones.filter(Boolean).length, 1);
  assert.equal(app.state.targetZones[8], true);
  assert.match(app.editor.innerHTML, /Layer level/);
  assert.equal(app.elements.has('l0-pcolor'), false);
  assert.equal(app.elements.has('l0-scolor'), false);
  assert.equal(app.elements.has('l0-palette'), false);
});

test('editing and saving P1 and P8 preserves every unrelated output', async () => {
  const app = dashboard();
  startEditing(app, { effect: 'confetti', paletteId: 13, speedMs: 2345, opacity: 123, fxParam: 15 });
  const before = plain(app.state.editing.channels);
  app.lg.toggleTargetZone(8);
  app.lg.changeLayerEffect(0, 'candle');
  app.elements.get('l0-param').value = '0';
  app.elements.get('l0-opacity').value = '0';
  app.lg.toggleTargetZone(15);
  app.lg.changeLayerEffect(0, 'heartbeat');
  app.elements.get('l0-param').value = '8';
  app.lg.changeLayerEffect(1, 'lightning');
  app.lg.changeLayerEnabled(1, true);
  await app.lg.saveScene();
  const request = app.calls.find(call => call.path === '/api/v1/presets' && call.method === 'PUT');
  assert.ok(request);
  const payload = JSON.parse(request.body);
  assert.equal(payload.channels.length, 16);
  assert.equal(payload.zones, undefined, 'current-node payload must omit the redundant zones mirror');
  assert.equal(payload.channels[8].layers[0].effect, 'candle');
  assert.equal(payload.channels[8].layers[0].fxParam, 0);
  assert.equal(payload.channels[8].layers[0].opacity, 0);
  assert.equal(payload.channels[15].layers[0].effect, 'heartbeat');
  assert.equal(payload.channels[15].layers[0].fxParam, 8);
  assert.equal(payload.channels[15].layers[1].effect, 'lightning');
  assert.equal(payload.channels[15].layers[1].enabled, true);
  for (let c = 0; c < 16; c++) {
    if (c !== 8 && c !== 15) assert.deepEqual(payload.channels[c], before[c], `output ${c} must remain unchanged`);
  }
});

test('an unrelated edit preserves exact speeds and invalid parameter sentinels', async () => {
  const app = dashboard();
  startEditing(app, { effect: 'twinkle', fxParam: 0, speedMs: 2345, opacity: 123 });
  assert.equal(app.elements.get('l0-param').value, '25', 'invalid zero must display Twinkle’s advertised default');
  const before = plain(app.state.editing.channels);
  app.elements.get('scene-name').value = 'Renamed';
  await app.lg.saveScene();
  const request = app.calls.find(call => call.path === '/api/v1/presets' && call.method === 'PUT');
  const payload = JSON.parse(request.body);
  assert.equal(payload.name, 'Renamed');
  assert.deepEqual(payload.channels, before, 'saving a name must not quantize speed, opacity or parameters');
});

test('changing one control in a multi-selection preserves other layer settings', () => {
  const app = dashboard();
  startEditing(app, { effect: 'candle', speedMs: 2345, fxParam: 0 });
  app.state.editing.channels[8].layers[0] = layer({ effect: 'heartbeat', speedMs: 6789, fxParam: 8, paletteId: 13 });
  const beforeP1 = plain(app.state.editing.channels[8].layers[0]);
  app.lg.toggleTargetZone(8, true);
  app.elements.get('l0-opacity').value = '25';
  app.readSceneEditor();
  assert.equal(app.state.editing.channels[0].layers[0].opacity, 64);
  assert.deepEqual(plain(app.state.editing.channels[8].layers[0]), { ...beforeP1, opacity: 64 });
});

test('new scenes use sixteen channels when descriptors are present and default PWM off', () => {
  const app = dashboard();
  app.state.config = { channels: Array.from({ length: 16 }, (_, index) => ({ index, kind: index < 8 ? 'addressable' : 'pwm' })) };
  app.lg.createNewScene();
  assert.equal(app.state.editing.channels.length, 16);
  assert.equal(app.state.targetZones.length, 16);
  assert.ok(app.state.editing.channels.slice(8).every(channel => channel.layers.every(layer => !layer.enabled)));
});

test('legacy scenes retain eight zone names and a zones-only save payload', () => {
  const app = dashboard();
  app.state.config = { zones: [{ name: 'Legacy lobby' }] };
  app.lg.createNewScene();
  assert.equal(app.state.editing.channels, undefined);
  assert.equal(app.state.editing.zones.length, 8);
  assert.match(app.editor.innerHTML, /Legacy lobby/);
  assert.doesNotMatch(app.editor.innerHTML, />[^<]*P8/);
  app.lg.toggleTargetZone(7);
  app.lg.changeLayerEffect(0, 'breathe');
  const payload = app.prepareScenePayload(app.state.editing);
  assert.equal(payload.channels, undefined);
  assert.equal(payload.zones[7].layers[0].effect, 'breathe');
  assert.equal(payload.zones[0].layers[0].effect, 'solid');
});
