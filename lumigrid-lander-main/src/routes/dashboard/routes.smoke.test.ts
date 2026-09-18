import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { readFileSync } from 'node:fs';
import { render } from 'svelte/server';
import { setSelectedNodeIp, getSelectedNodeIp } from '$lib/auth/storage';
import { disconnect, _resetForTesting } from '$lib/state/nodeStore';

// Import all 13 dashboard route components
import DashboardPage from './+page.svelte';
import ScenesPage from './scenes/+page.svelte';
import TimelinePage from './timeline/+page.svelte';
import SchedulePage from './schedule/+page.svelte';
import ChannelsPage from './channels/+page.svelte';
import ThermalPage from './thermal/+page.svelte';
import VisualizerPage from './visualizer/+page.svelte';
import UptimePage from './uptime/+page.svelte';
import DiagnosticsPage from './diagnostics/+page.svelte';
import FactoryPage from './factory/+page.svelte';
import PreviewPage from './preview/+page.svelte';
import FirmwarePage from './firmware/+page.svelte';
import CuesPage from './cues/+page.svelte';

const routes = [
  { path: '/dashboard', component: DashboardPage, name: 'Dashboard Overview' },
  { path: '/dashboard/scenes', component: ScenesPage, name: 'Scenes' },
  { path: '/dashboard/cues', component: CuesPage, name: 'Cues' },
  { path: '/dashboard/timeline', component: TimelinePage, name: 'Timeline' },
  { path: '/dashboard/schedule', component: SchedulePage, name: 'Schedule' },
  { path: '/dashboard/channels', component: ChannelsPage, name: 'Channels' },
  { path: '/dashboard/thermal', component: ThermalPage, name: 'Thermal' },
  { path: '/dashboard/visualizer', component: VisualizerPage, name: 'Visualizer' },
  { path: '/dashboard/uptime', component: UptimePage, name: 'Uptime' },
  { path: '/dashboard/diagnostics', component: DiagnosticsPage, name: 'Diagnostics' },
  { path: '/dashboard/factory', component: FactoryPage, name: 'Factory' },
  { path: '/dashboard/preview', component: PreviewPage, name: 'Preview' },
  { path: '/dashboard/firmware', component: FirmwarePage, name: 'Firmware' },
];
const routeSourceFiles = [
  './+layout.svelte',
  './+page.svelte',
  './scenes/+page.svelte',
  './cues/+page.svelte',
  './timeline/+page.svelte',
  './schedule/+page.svelte',
  './channels/+page.svelte',
  './visualizer/+page.svelte',
  './preview/+page.svelte',
  './thermal/+page.svelte',
  './uptime/+page.svelte',
  './diagnostics/+page.svelte',
  './factory/+page.svelte',
  './firmware/+page.svelte',
];

const landerUtilityClassNames = new Set([
  'card', 'pill', 'grid', 'btn', 'chip', 'strip', 'timeline', 'controls',
  'demo', 'big', 'center', 'section', 'container', 'switch', 'faq',
  'infocard', 'readable',
]);

function sourceFor(relativePath: string): string {
  return readFileSync(new URL(relativePath, import.meta.url), 'utf8');
}


class MockStorage implements Storage {
  private store: Record<string, string> = {};
  get length() { return Object.keys(this.store).length; }
  clear() { this.store = {}; }
  getItem(key: string): string | null { return this.store[key] ?? null; }
  key(index: number): string | null { return Object.keys(this.store)[index] ?? null; }
  removeItem(key: string): void { delete this.store[key]; }
  setItem(key: string, value: string): void { this.store[key] = String(value); }
}

describe('dashboard routes smoke & contract tests', () => {
  let mockStorage: MockStorage;

  it('keeps the dashboard route registry complete and unique', () => {
    expect(routes).toHaveLength(13);
    expect(new Set(routes.map(r => r.path)).size).toBe(routes.length);
    expect(routes.map(r => r.path)).toEqual(expect.arrayContaining([
      '/dashboard',
      '/dashboard/scenes',
      '/dashboard/cues',
      '/dashboard/timeline',
      '/dashboard/schedule',
      '/dashboard/channels',
      '/dashboard/visualizer',
      '/dashboard/preview',
      '/dashboard/thermal',
      '/dashboard/uptime',
      '/dashboard/diagnostics',
      '/dashboard/factory',
      '/dashboard/firmware',
    ]));
  });

  it('keeps the operator palette free of the retired blue accent family', () => {
    const retiredBlue = /#0071e3|#0077ed|#2997ff|#64d2ff|#00f0ff|#3b82f6|#0a84ff/i;
    for (const file of routeSourceFiles) {
      expect(sourceFor(file), file).not.toMatch(retiredBlue);
    }
  });

  it('does not reuse public Lander utility class names inside dashboard routes', () => {
    for (const file of routeSourceFiles) {
      const source = sourceFor(file);
      for (const match of source.matchAll(/class="([^"]+)"/g)) {
        const tokens = match[1].split(/\s+/).filter(Boolean);
        for (const token of tokens) {
          expect(landerUtilityClassNames.has(token), `${file} reuses public class "${token}"`).toBe(false);
        }
      }
    }
  });

  it('keeps destructive maintenance workspaces admin-only in navigation', () => {
    const layout = sourceFor('./+layout.svelte');
    expect(layout).toMatch(/href:\s*'\/dashboard\/factory'[\s\S]{0,120}adminOnly:\s*true/);
    expect(layout).toMatch(/href:\s*'\/dashboard\/firmware'[\s\S]{0,120}adminOnly:\s*true/);
    expect(layout).toContain('Admin access required');
  });

  it('does not use native browser dialogs in operator routes', () => {
    const nativeDialogCall = /\b(?:confirm|prompt|alert)\s*\(/;
    for (const file of routeSourceFiles) {
      expect(sourceFor(file), file).not.toMatch(nativeDialogCall);
    }
  });

  it('keeps dashboard routes off the legacy raw node-list storage key', () => {
    const legacyFleetStorage = /\bNODE_LIST_KEY\b|readStringList\s*\(\s*NODE_LIST_KEY|writeStringList\s*\(\s*NODE_LIST_KEY/;
    for (const file of routeSourceFiles) {
      expect(sourceFor(file), file).not.toMatch(legacyFleetStorage);
    }
  });

  it('keeps removed implementation and legacy operator copy out of the dashboard', () => {
    const retiredCopy = /Hardware Truth|WLED playlists|QLC\+ cue|xLights value|ESP32 RMT Lane|PCA9685 Driver|>\s*Active Preset\s*<|>\s*Preset Scene\s*<|>\s*Sequence Drops\s*</i;
    for (const file of routeSourceFiles) {
      expect(sourceFor(file), file).not.toMatch(retiredCopy);
    }
  });

  beforeEach(() => {
    mockStorage = new MockStorage();
    vi.stubGlobal('localStorage', mockStorage);
    _resetForTesting();
  });

  afterEach(() => {
    disconnect();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  describe('renders all 13 routes with no selected node', () => {
    for (const r of routes) {
      it(`renders ${r.path} (${r.name}) cleanly without a selected node`, () => {
        setSelectedNodeIp('');
        expect(getSelectedNodeIp()).toBe('');

        const rendered = render(r.component);
        expect(rendered.html).toBeDefined();
        expect(typeof rendered.html).toBe('string');
        expect(rendered.html.length).toBeGreaterThan(0);
      });
    }
  });

  describe('renders all 13 routes when a node is selected', () => {
    for (const r of routes) {
      it(`renders ${r.path} (${r.name}) with selected node`, () => {
        setSelectedNodeIp('192.168.1.50');
        expect(getSelectedNodeIp()).toBe('192.168.1.50');

        const rendered = render(r.component);
        expect(rendered.html).toBeDefined();
        expect(typeof rendered.html).toBe('string');
        expect(rendered.html.length).toBeGreaterThan(0);
      });
    }
  });

  it('verifies that no API calls are dispatched with empty or undefined IP', async () => {
    setSelectedNodeIp('');
    const fetchSpy = vi.spyOn(globalThis, 'fetch');

    // Render all pages
    for (const r of routes) {
      render(r.component);
    }

    // Inspect any fetch calls made during rendering or initialization
    for (const call of fetchSpy.mock.calls) {
      const url = String(call[0]);
      expect(url).not.toMatch(/http:\/\/(undefined|null|)\//);
      expect(url).not.toBe('');
    }
  });

  it('handles node unavailable or network failures gracefully without unhandled exceptions', async () => {
    setSelectedNodeIp('192.168.1.99');
    vi.spyOn(globalThis, 'fetch').mockRejectedValue(new Error('Network error / connection refused'));

    // Rendering with a failing node should not throw an unhandled crash
    for (const r of routes) {
      expect(() => render(r.component)).not.toThrow();
    }
  });
});
