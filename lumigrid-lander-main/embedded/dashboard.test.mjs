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
function snapshot(overrides = {}) {
  return { schema: 3, runtime: { controlOwner: 'ScenePlayback' },
    output: { enabled: true, fault: false, estopActive: false, state: 'healthy', powerMw: 1200,
      power: { limitMw: 10000, estimateComplete: true } },
    scene: { activePreset: 0, transitioning: false, applyPending: false, fallbackActive: false },
    protocols: { artnet: { enabled: true, sources: 0 }, sacn: { enabled: false, sources: 0 } }, ...overrides };
}

const flush = async () => { for (let i = 0; i < 40; i++) await Promise.resolve(); };

function dashboard(responses = {}, stored = {}, storageFailures = []) {
  const elements = new Map();
  const calls = [];
  const reloads = [];
  const timers = new Map();
  const sockets = [];
  let nextTimer = 1;
  let now = 1000;
  const add = (id, value = '') => {
    const element = { value, checked: false, hidden: false, disabled: false, textContent: '', style: {}, dataset: {},
      classList: { add() {}, remove() {} }, addEventListener() {}, focus() {} };
    elements.set(id, element);
    return element;
  };
  for (const id of ['experience-host', 'experience-token', 'first-setup', 'normal-login', 'setup-unavailable',
    'setup-defaults', 'experience-toast', 'scene-editor-title', 'scene-name', 'scene-brightness',
    'scene-brightness-out', 'scene-fade', 'scene-fade-out', 'experience-auth', 'product-app',
    'connect-button', 'refresh-button', 'connection-state', 'connection-detail', 'header-owner',
    'dash-owner-text', 'header-power', 'dash-power-est', 'dash-power-max', 'dash-power-note',
    'dash-power-bar', 'dash-safety-text', 'dash-proto-text', 'dash-trans-text', 'dash-playing',
    'ota-file', 'ota-status', 'restore-file', 'restore-status']) add(id);
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
    localStorage: { getItem: () => null }, sessionStorage: {
      getItem: key => stored[key] ?? null, setItem: (key, value) => {
        if (storageFailures.includes(key)) throw new Error('storage unavailable');
        stored[key] = value;
      }, removeItem: key => { delete stored[key]; }
    },
    location: { protocol: 'http:', host: 'node.test', hostname: 'node.test', reload: () => reloads.push(true) },
    performance: { now: () => now }, URL, AbortController, ArrayBuffer, Uint8Array,
    setTimeout: (callback, delay) => { const id = nextTimer++; timers.set(id, { callback, delay }); return id; },
    clearTimeout: id => timers.delete(id), requestAnimationFrame: () => 0,
    WebSocket: class {
      constructor(url, protocols) { this.url = url; this.protocols = protocols; this.readyState = 0; sockets.push(this); }
      close() { this.readyState = 3; this.onclose?.(); }
    },
    fetch: async (url, options) => {
      const path = new URL(url).pathname;
      calls.push({ path, url, ...options });
      let value = responses[path];
      if (typeof value === 'function') value = await value({ path, url, ...options });
      if (value instanceof Error) throw value;
      const defaults = {
        '/api/v1/auth/status': { provisioned: true }, '/api/v1/auth/session': { sessionToken: 'session', role: 'admin' },
        '/api/v1/auth/ws-ticket': { ticket: 'ticket' }, '/api/v1/status': snapshot(),
        '/api/v1/effects': [], '/api/v1/palettes': [], '/api/v1/presets': [], '/api/v1/schedules': [],
        '/api/v1/cue-lists': [], '/api/v1/channels': { channels: [] }
      };
      return { ok: !value?._httpStatus, status: value?._httpStatus ?? 200,
        json: async () => value?.json ? await value.json() : plain(value ?? defaults[path] ?? {}),
        text: async () => JSON.stringify(value ?? defaults[path] ?? {}),
        blob: async () => value?.blob ? await value.blob() : new Blob(['backup']) };
    },
  });
  const exposed = script.replace('    return {\n        connect, disconnect, refresh, nav, playScene, blackout,',
    '    window.testing = { state, api, acceptStatus, refreshStatus, initWebSocket, startStatusWatchdog, updateTelemetryUI, resolveEffects, resolvePalettes, renderSceneEditor, readSceneEditor, prepareScenePayload, effectDefaults, paletteDefaults };\n    return {\n        connect, disconnect, refresh, nav, playScene, blackout,');
  assert.notEqual(exposed, script, 'test hook must attach to the application return');
  vm.runInContext(exposed, context, { filename: 'dashboard.html' });
  const api = context.window.testing;
  return { ...api, lg: context.window.lg, elements, calls, editor, timers, sockets, responses, stored, reloads,
    advance: ms => { now += ms; },
    async runTimer(delay) {
      const entry = [...timers].find(([, timer]) => timer.delay === delay);
      assert.ok(entry, `expected a ${delay} ms timer`);
      timers.delete(entry[0]); now += delay;
      await entry[1].callback(); await flush();
    }
  };
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

test('every refresh reads current status and updates playback without optimistic state', async () => {
  const responses = { '/api/v1/status': snapshot() };
  const app = dashboard(responses);
  await app.lg.refresh();
  assert.equal(app.elements.get('dash-playing').textContent, 'Scene 1');
  responses['/api/v1/status'] = snapshot({ scene: { activePreset: 7 } });
  await app.lg.refresh();
  assert.equal(app.calls.filter(call => call.path === '/api/v1/status').length, 2);
  assert.equal(app.elements.get('dash-playing').textContent, 'Scene 8');
  await app.lg.playScene(2);
  assert.equal(app.state.status.scene.activePreset, 7, 'accepted command is not applied state');
});

test('failed document reads preserve loaded data and block a destructive save until refresh succeeds', async () => {
  const responses = {
    '/api/v1/config': { deviceName: 'Lobby', channels: [] }, '/api/v1/presets': [scene()],
    '/api/v1/sequence': { bpm: 137, enabled: true, tracks: [{ steps: [{ presetIndex: 2, lengthBeats: 8 }] }] },
    '/api/v1/schedules': [{ enabled: true, hour: 8, minute: 30 }], '/api/v1/cue-lists': [{ index: 1, name: 'Evening' }]
  };
  const app = dashboard(responses);
  await app.lg.refresh();
  const before = plain({ config: app.state.config, presets: app.state.presets, sequence: app.state.sequence,
    schedules: app.state.schedules, cueLists: app.state.cueLists });
  for (const path of ['/api/v1/config', '/api/v1/presets', '/api/v1/sequence', '/api/v1/schedules', '/api/v1/cue-lists']) {
    responses[path] = new Error('temporary network failure');
  }
  await app.lg.refresh();
  assert.deepEqual(plain({ config: app.state.config, presets: app.state.presets, sequence: app.state.sequence,
    schedules: app.state.schedules, cueLists: app.state.cueLists }), before);
  assert.match(app.elements.get('connection-detail').textContent, /Could not refresh:.*Scenes.*Last loaded data is retained/);
  await assert.rejects(app.api('/api/v1/presets', 'PUT', scene()), /Refresh scenes successfully/);
  assert.equal(app.calls.filter(call => call.path === '/api/v1/presets' && call.method === 'PUT').length, 0);
  responses['/api/v1/presets'] = [];
  await app.lg.refresh();
  assert.equal(app.state.presets.length, 0, 'a successfully loaded empty library is authoritative');
  assert.ok(!app.state.resourceErrors.includes('Scenes'));
});

test('refresh is coalesced and a late request cannot repopulate a disconnected node', async () => {
  let finishStatus;
  const app = dashboard({ '/api/v1/status': () => new Promise(resolve => { finishStatus = resolve; }) });
  await flush();
  const first = app.lg.refresh();
  const second = app.lg.refresh();
  assert.equal(app.calls.filter(call => call.path === '/api/v1/status').length, 1);
  app.lg.disconnect();
  finishStatus(snapshot());
  await Promise.all([first, second]);
  assert.equal(app.state.status, null);
  assert.equal(app.state.presets.length, 0);
  assert.equal(app.calls.filter(call => call.path === '/api/v1/config').length, 0);
  assert.equal(app.elements.get('connection-state').textContent, 'Disconnected');
});

test('a newer stream snapshot wins over an in-flight HTTP response', async () => {
  let finishStatus;
  const app = dashboard({ '/api/v1/status': () => new Promise(resolve => { finishStatus = resolve; }) });
  await flush();
  const pending = app.refreshStatus();
  app.acceptStatus(snapshot({ scene: { activePreset: 12 } }), true);
  finishStatus(snapshot({ scene: { activePreset: 2 } }));
  await pending;
  assert.equal(app.state.status.scene.activePreset, 12);
  assert.equal(app.state.connection, 'live');
});

test('delayed websocket tickets cannot reopen a disconnected session', async () => {
  let finishTicket;
  const app = dashboard({ '/api/v1/auth/ws-ticket': () => new Promise(resolve => { finishTicket = resolve; }) });
  await flush();
  app.state.token = 'session';
  app.initWebSocket();
  app.lg.disconnect();
  finishTicket({ ticket: 'old-ticket' });
  await flush();
  assert.equal(app.sockets.length, 0);
  assert.equal(app.timers.size, 0, 'disconnect cancels pending requests and all connection timers');
});

test('late websocket callbacks cannot replace the current socket and target scheme is respected', async () => {
  const app = dashboard();
  await flush();
  app.state.token = 'session';
  app.state.host = 'https://node-proxy.test';
  app.initWebSocket(); await flush();
  const old = app.sockets[0];
  const oldClose = old.onclose;
  const oldMessage = old.onmessage;
  assert.equal(old.url, 'wss://node-proxy.test/ws');
  app.initWebSocket(); await flush();
  oldClose();
  oldMessage({ data: JSON.stringify(snapshot({ scene: { activePreset: 4 } })) });
  assert.equal(app.sockets.length, 2);
  assert.equal(app.state.status, null);
  assert.ok(![...app.timers.values()].some(timer => timer.delay === 2000));
  app.sockets[1].onopen();
  app.sockets[1].onmessage({ data: JSON.stringify(snapshot({ scene: { activePreset: 8 } })) });
  assert.equal(app.state.status.scene.activePreset, 8);
  assert.equal(app.elements.get('connection-state').textContent, 'Live status');
  app.lg.disconnect();
});

test('HTTP fallback recovers status while websocket tickets fail and stops on logout', async () => {
  const app = dashboard({ '/api/v1/auth/ws-ticket': new Error('stream unavailable') });
  await flush();
  app.state.token = 'session';
  app.initWebSocket(); app.startStatusWatchdog(); await flush();
  assert.equal(app.state.connection, 'offline');
  await app.runTimer(5000);
  assert.equal(app.state.connection, 'polling');
  assert.equal(app.state.statusStale, false);
  assert.match(app.elements.get('connection-detail').textContent, /HTTP.*reconnects/);
  app.responses['/api/v1/status'] = new Error('node unreachable');
  await app.runTimer(5000);
  assert.equal(app.state.connection, 'offline');
  assert.match(app.elements.get('connection-detail').textContent, /stale.*last received/);
  assert.equal(app.state.status.output.powerMw, 1200, 'last known telemetry is retained and labelled stale');
  app.lg.disconnect();
  assert.equal(app.timers.size, 0);
});

test('missing or malformed stream status cannot keep the status fresh', async () => {
  const app = dashboard({ '/api/v1/status': new Error('unreachable') });
  await flush();
  app.state.token = 'session';
  app.initWebSocket(); await flush();
  const socket = app.sockets[0]; socket.onopen();
  socket.onmessage({ data: JSON.stringify(snapshot()) });
  const statusAt = app.state.lastStatusAt;
  app.advance(11000);
  socket.onmessage({ data: JSON.stringify({ unrelated: true }) });
  socket.onmessage({ data: new Uint8Array([2, 1, 1, 0, 255, 0, 0]).buffer });
  assert.equal(app.state.lastStatusAt, statusAt);
  app.startStatusWatchdog(); await app.runTimer(5000);
  assert.equal(app.state.statusStale, true);
  assert.equal(app.state.connection, 'offline');
  app.lg.disconnect();
});

test('expired sessions stop reconnects and request a new sign-in', async () => {
  const app = dashboard({ '/api/v1/status': { _httpStatus: 401, error: 'authorization_required' } });
  await flush();
  app.state.token = 'expired';
  app.startStatusWatchdog();
  await app.runTimer(5000);
  assert.equal(app.state.token, '');
  assert.equal(app.elements.get('product-app').hidden, true);
  assert.match(app.elements.get('experience-toast').textContent, /Session expired/);
  assert.ok(![...app.timers.values()].some(timer => timer.delay === 5000));
});

test('request timeouts do not claim an unconfirmed write failed to apply', async () => {
  const app = dashboard({ '/api/v1/activate': ({ signal }) => new Promise((_, reject) => {
    signal.addEventListener('abort', () => reject(new Error('aborted')));
  }) });
  await flush();
  const pending = app.api('/api/v1/activate', 'POST', { preset: 0 });
  const rejected = assert.rejects(pending, /No confirmation.*before repeating/);
  await app.runTimer(8000); await rejected;
  assert.equal(app.calls.filter(call => call.path === '/api/v1/activate').length, 1, 'commands are never retried automatically');
});

test('output faults, pending changes, real budgets and incomplete estimates are visible', () => {
  const app = dashboard();
  app.acceptStatus(snapshot({ output: { enabled: false, fault: true, state: 'output_gate_failed', powerMw: 900,
    power: { limitMw: 3200, estimateComplete: false } }, scene: { activePreset: 255, applyPending: true, transitioning: false },
    protocols: { artnet: { enabled: true, sources: 2 }, sacn: { enabled: true, sources: 1 } } }));
  assert.equal(app.elements.get('dash-safety-text').textContent, 'Output fault');
  assert.equal(app.elements.get('dash-trans-text').textContent, 'Change pending');
  assert.equal(app.elements.get('dash-playing').textContent, 'No scene active');
  assert.equal(app.elements.get('dash-proto-text').textContent, 'Art-Net · sACN');
  assert.equal(app.elements.get('dash-power-est').textContent, 'Partial: 900 mW');
  assert.equal(app.elements.get('dash-power-max').textContent, 'Budget: 3200 mW');
  assert.match(app.elements.get('dash-power-note').textContent, /incomplete/);
  app.state.status.output.estopActive = true; app.updateTelemetryUI();
  assert.equal(app.elements.get('dash-safety-text').textContent, 'Emergency stop');
});

test('restored credentials must have a matching saved host and node sessions do not retain old documents', async () => {
  const unbound = dashboard({}, { lg_node_token: 'unbound' });
  await flush();
  assert.equal(unbound.state.token, '');
  assert.ok(unbound.calls.every(call => call.headers.Authorization !== 'Bearer unbound'));
  const app = dashboard(); await flush();
  app.state.presets = [scene()]; app.state.config = { deviceName: 'Previous node' };
  app.elements.get('experience-host').value = 'other-node.test';
  app.elements.get('experience-token').value = 'operator-key';
  await app.lg.connect(); await flush();
  assert.equal(app.state.presets.length, 0);
  assert.equal(app.state.config.deviceName, undefined);
  assert.equal(app.stored.lg_node_host, 'http://other-node.test');
  assert.deepEqual(JSON.parse(app.stored.lg_node_session), { host: 'http://other-node.test', token: 'session' });
  assert.equal(app.stored.lg_node_token, undefined);
  assert.ok(app.calls.filter(call => call.path !== '/api/v1/auth/status').every(call => call.url.startsWith('http://other-node.test/')));
  app.lg.disconnect();
});

test('diagnostics read the nested health contract without inventing zero readings', async () => {
  const app = dashboard({ '/api/v1/health': { system: { uptime_s: 3661, min_free_heap: 65536 },
    memory: { free8: 131072 }, network: { wifi_rssi: -57 }, dmx: { sources_dropped: 3 } } });
  const grid = { innerHTML: '' }; app.elements.set('health-metrics-grid', grid);
  await app.lg.loadCommissioning();
  assert.match(grid.innerHTML, /1h 1m 1s/);
  assert.match(grid.innerHTML, /128 KB/);
  assert.match(grid.innerHTML, /64 KB/);
  assert.match(grid.innerHTML, /-57 dBm/);
  assert.match(grid.innerHTML, /DMX sources dropped/);
  app.responses['/api/v1/health'] = {};
  await app.lg.loadCommissioning();
  assert.match(grid.innerHTML, /Not reported/);
  assert.doesNotMatch(grid.innerHTML, />0 KB|>-100 dBm/);
});

test('a login response arriving after disconnect cannot restore credentials or reveal the dashboard', async () => {
  let finishLogin;
  const app = dashboard({ '/api/v1/auth/session': () => new Promise(resolve => { finishLogin = resolve; }) });
  await flush();
  app.elements.get('experience-token').value = 'key';
  const login = app.lg.connect();
  app.lg.disconnect();
  finishLogin({ sessionToken: 'old-session', role: 'admin' });
  await login;
  assert.equal(app.state.token, '');
  assert.equal(app.stored.lg_node_token, undefined);
  assert.equal(app.elements.get('product-app').hidden, true);
  assert.equal(app.calls.filter(call => call.path === '/api/v1/status').length, 0);
});

test('new scene slots are not allocated from a loading or failed library', async () => {
  let finishPresets;
  const app = dashboard({ '/api/v1/presets': () => new Promise(resolve => { finishPresets = resolve; }) });
  await flush();
  const refreshing = app.lg.refresh();
  await flush();
  assert.equal(typeof finishPresets, 'function');
  app.lg.createNewScene();
  assert.equal(app.state.editing, null, 'the empty initial cache does not prove slot 0 is free');
  assert.match(app.elements.get('experience-toast').textContent, /Refresh scenes successfully/);
  finishPresets([{ ...scene(), index: 0, name: 'Existing scene' }]);
  await refreshing;
  app.lg.createNewScene();
  assert.equal(app.state.editing.index, 1, 'allocation uses the completed library');
  app.state.editing = null;
  app.responses['/api/v1/presets'] = new Error('unreachable');
  await app.lg.refresh();
  app.lg.createNewScene();
  assert.equal(app.state.editing, null, 'a failed library refresh also blocks allocation');
});

test('restore never commits to another node after delayed validation or body decoding', async () => {
  for (const delayBody of [false, true]) {
    let finish;
    const waiting = new Promise(resolve => { finish = resolve; });
    const app = dashboard({ '/api/v1/restore/validate': delayBody ? { json: () => waiting } : () => waiting });
    await flush();
    app.state.host = 'node-a.test'; app.state.token = 'token-a';
    app.elements.get('restore-file').files = [{ size: 512, name: 'a.lgbak' }];
    const restoring = app.lg.uploadRestore(); await flush();
    const validation = app.calls.find(call => call.path === '/api/v1/restore/validate');
    assert.equal(validation.url, 'http://node-a.test/api/v1/restore/validate');
    assert.ok([...app.timers.values()].some(timer => timer.delay === 120000), 'backup transfer has a bounded upload-specific deadline');
    app.lg.disconnect();
    app.state.host = 'node-b.test'; app.state.token = 'token-b';
    app.elements.get('restore-status').textContent = 'Node B';
    assert.equal(validation.signal.aborted, true);
    finish({ valid: true, plan: {} }); await restoring;
    assert.equal(app.calls.filter(call => call.path === '/api/v1/restore').length, 0);
    assert.equal(app.elements.get('restore-status').textContent, 'Node B');
  }
});

test('a cancelled OTA capacity check cannot upload to the replacement node', async () => {
  let finishCapacity;
  const app = dashboard({ '/api/v1/ota/status': () => new Promise(resolve => { finishCapacity = resolve; }) });
  await flush();
  app.state.host = 'node-a.test'; app.state.token = 'token-a';
  app.elements.get('ota-file').files = [{ size: 512, name: 'firmware.bin' }];
  const uploading = app.lg.uploadOta(); await flush();
  const capacityCall = app.calls.find(call => call.path === '/api/v1/ota/status');
  app.lg.disconnect(); app.state.host = 'node-b.test'; app.state.token = 'token-b';
  app.elements.get('ota-status').textContent = 'Node B';
  finishCapacity({ maxFirmwareBytes: 1024 }); await uploading;
  assert.equal(capacityCall.signal.aborted, true);
  assert.equal(app.calls.filter(call => call.path === '/api/v1/ota').length, 0);
  assert.equal(app.state.otaCapacity, 0);
  assert.equal(app.elements.get('ota-status').textContent, 'Node B');
});

test('OTA transfer and verification stay owned by the starting node and stop on disconnect', async () => {
  let finishUpload;
  const app = dashboard({ '/api/v1/ota/status': { maxFirmwareBytes: 1024 },
    '/api/v1/ota': () => new Promise(resolve => { finishUpload = resolve; }) });
  await flush();
  app.state.host = 'node-a.test'; app.state.token = 'token-a';
  app.elements.get('ota-file').files = [{ size: 512, name: 'firmware.bin' }];
  const uploading = app.lg.uploadOta(); await flush();
  const transfer = app.calls.find(call => call.path === '/api/v1/ota');
  assert.equal(transfer.headers.Authorization, 'Bearer token-a');
  assert.ok([...app.timers.values()].some(timer => timer.delay === 120000));
  assert.ok(![...app.timers.values()].some(timer => timer.delay === 8000), 'the short JSON deadline is not applied to firmware transfer');
  finishUpload({}); await flush();
  assert.ok([...app.timers.values()].some(timer => timer.delay === 2000), 'verification is waiting');
  app.lg.disconnect(); app.state.host = 'node-b.test'; app.state.token = 'token-b';
  await uploading;
  assert.equal(app.calls.filter(call => call.path === '/api/v1/ota/status').length, 1, 'no verification poll reaches node B');
  assert.ok(![...app.timers.values()].some(timer => timer.delay === 2000));
});

test('successful restore reload is cancelled when the operator leaves that node', async () => {
  const app = dashboard({ '/api/v1/restore/validate': { valid: true, plan: {} } });
  await flush();
  app.state.host = 'node-a.test'; app.state.token = 'token-a';
  app.elements.get('restore-file').files = [{ size: 512, name: 'a.lgbak' }];
  await app.lg.uploadRestore();
  const restore = app.calls.find(call => call.path === '/api/v1/restore');
  assert.equal(restore.url, 'http://node-a.test/api/v1/restore');
  assert.equal(restore.headers.Authorization, 'Bearer token-a');
  assert.ok([...app.timers.values()].some(timer => timer.delay === 6000));
  app.lg.disconnect(); app.state.host = 'node-b.test'; app.state.token = 'token-b';
  await flush();
  assert.ok(![...app.timers.values()].some(timer => timer.delay === 6000));
  assert.equal(app.reloads.length, 0);
});

test('backup body reads are aborted and cannot publish completion for a new node', async () => {
  let finishBlob;
  const app = dashboard({ '/api/v1/backup': { blob: () => new Promise(resolve => { finishBlob = resolve; }) } });
  await flush(); app.state.host = 'node-a.test'; app.state.token = 'token-a';
  const downloading = app.lg.downloadBackup(); await flush();
  const request = app.calls.find(call => call.path === '/api/v1/backup');
  app.lg.disconnect(); app.state.host = 'node-b.test'; app.state.token = 'token-b';
  app.elements.get('experience-toast').textContent = 'Node B';
  finishBlob(new Blob(['backup'])); await downloading;
  assert.equal(request.signal.aborted, true);
  assert.equal(app.elements.get('experience-toast').textContent, 'Node B');
});

test('burn-in cancellation fallback cannot abort the newly selected node', async () => {
  const app = dashboard({ '/api/v1/factory/burn-in/stop': ({ signal }) => new Promise((_, reject) => {
    signal.addEventListener('abort', () => reject(new Error('aborted')));
  }) });
  await flush(); app.state.host = 'node-a.test'; app.state.token = 'token-a';
  const stopping = app.lg.stopBurnIn(); await flush();
  app.lg.disconnect(); app.state.host = 'node-b.test'; app.state.token = 'token-b';
  await stopping;
  assert.equal(app.calls.filter(call => call.path === '/api/v1/factory/abort').length, 0);
  app.responses['/api/v1/factory/burn-in/stop'] = { _httpStatus: 503, error: 'busy' };
  await app.lg.stopBurnIn();
  const fallback = app.calls.find(call => call.path === '/api/v1/factory/abort');
  assert.equal(fallback.url, 'http://node-b.test/api/v1/factory/abort', 'a new explicit stop still supports same-node fallback');
  assert.equal(fallback.headers.Authorization, 'Bearer token-b');
});

test('blank host sign-in stores the effective target atomically and restores only its paired token', async () => {
  const stored = { lg_node_host: 'old-node.test', lg_node_token: 'old-token' };
  const responses = { '/api/v1/auth/session': { sessionToken: 'new-token', role: 'admin' } };
  const app = dashboard(responses, stored); await flush();
  app.elements.get('experience-host').value = '';
  app.elements.get('experience-token').value = 'key';
  await app.lg.connect(); await flush();
  assert.deepEqual(JSON.parse(stored.lg_node_session), { host: 'http://node.test', token: 'new-token' });
  const restored = dashboard(responses, { ...stored }); await flush();
  assert.equal(restored.state.host, 'http://node.test');
  assert.equal(restored.state.token, 'new-token');
  assert.ok(restored.calls.every(call => call.url.startsWith('http://node.test/')));
  app.lg.disconnect(); restored.lg.disconnect();
});

test('storage write failures cannot mix a previous host with a new session token', async () => {
  for (const failedKey of ['lg_node_session', 'lg_node_host']) {
    const stored = { lg_node_host: 'old-node.test', lg_node_session: JSON.stringify({ host: 'http://old-node.test', token: 'old-token' }) };
    const app = dashboard({}, stored, [failedKey]); await flush();
    app.elements.get('experience-host').value = 'new-node.test'; app.elements.get('experience-token').value = 'key';
    await app.lg.connect(); await flush();
    const restored = dashboard({}, { ...stored }); await flush();
    assert.ok(restored.calls.every(call => call.headers.Authorization !== 'Bearer session' || call.url.startsWith('http://new-node.test/')));
    if (failedKey === 'lg_node_session') assert.equal(restored.state.token, '');
    else assert.equal(restored.state.host, 'http://new-node.test');
    app.lg.disconnect(); restored.lg.disconnect();
  }
});

test('schedules preserve missing scene targets and cannot save against a failed scene library', async () => {
  const app = dashboard({ '/api/v1/presets': new Error('scene read failed'),
    '/api/v1/schedules': [{ enabled: true, hour: 8, minute: 30, action: 'preset', presetIndex: 7 }] });
  const grid = { innerHTML: '' }; app.elements.set('schedules-grid', grid);
  await app.lg.refresh();
  assert.match(grid.innerHTML, /option value="7" selected>Scene 8 \(not in loaded library\)/);
  await app.lg.saveSchedules();
  assert.equal(app.calls.filter(call => call.path === '/api/v1/schedules' && call.method === 'PUT').length, 0);
  assert.equal(app.state.schedules[0].presetIndex, 7);
  assert.match(app.elements.get('experience-toast').textContent, /Refresh scenes successfully/);
});
