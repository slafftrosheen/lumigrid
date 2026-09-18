import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
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
