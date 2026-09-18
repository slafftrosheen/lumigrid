import { describe, expect, it, beforeEach, afterEach, vi } from 'vitest';
import {
  getSelectedNodeIp,
  setSelectedNodeIp,
  normalizeNodeAddress,
  NODE_IP_KEY,
  NODE_REGISTRY_CHANGED_EVENT,
} from './storage';

class MockStorage implements Storage {
  private store: Record<string, string> = {};
  get length() {
    return Object.keys(this.store).length;
  }
  clear() {
    this.store = {};
  }
  getItem(key: string): string | null {
    return this.store[key] ?? null;
  }
  key(index: number): string | null {
    return Object.keys(this.store)[index] ?? null;
  }
  removeItem(key: string): void {
    delete this.store[key];
  }
  setItem(key: string, value: string): void {
    this.store[key] = String(value);
  }
}

describe('selected node storage', () => {
  let mockStorage: MockStorage;

  beforeEach(() => {
    mockStorage = new MockStorage();
    vi.stubGlobal('localStorage', mockStorage);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('returns empty string or fallback on empty storage', () => {
    expect(getSelectedNodeIp()).toBe('');
    expect(getSelectedNodeIp('192.168.1.1')).toBe('192.168.1.1');
  });

  it('returns stored IP when present', () => {
    mockStorage.setItem(NODE_IP_KEY, '10.0.0.42');
    expect(getSelectedNodeIp()).toBe('10.0.0.42');
  });

  it('trims whitespace when reading stored IP', () => {
    mockStorage.setItem(NODE_IP_KEY, '   192.168.1.105 \n');
    expect(getSelectedNodeIp()).toBe('192.168.1.105');
  });

  it('sets IP and trims whitespace when writing', () => {
    setSelectedNodeIp('  172.16.0.25  ');
    expect(mockStorage.getItem(NODE_IP_KEY)).toBe('172.16.0.25');
    expect(getSelectedNodeIp()).toBe('172.16.0.25');
  });

  it('clears stored IP when given empty string or whitespace', () => {
    setSelectedNodeIp('192.168.1.50');
    expect(getSelectedNodeIp()).toBe('192.168.1.50');

    setSelectedNodeIp('   ');
    expect(mockStorage.getItem(NODE_IP_KEY)).toBeNull();
    expect(getSelectedNodeIp()).toBe('');
  });

  it('normalizes operator-entered node addresses without accepting paths', () => {
    expect(normalizeNodeAddress('192.168.1.50')).toBe('192.168.1.50');
    expect(normalizeNodeAddress(' https://lumigrid.local/ ')).toBe('lumigrid.local');
    expect(normalizeNodeAddress('10.0.0.4:8080')).toBe('10.0.0.4:8080');
    expect(normalizeNodeAddress('lumigrid.local/api/v1')).toBeNull();
    expect(normalizeNodeAddress('192.168.1.20?x=1')).toBeNull();
    expect(normalizeNodeAddress('   ')).toBeNull();
  });

  it('emits fleet-change events for structural edits but not presence heartbeats', async () => {
    const win = new EventTarget();
    vi.stubGlobal('window', win);

    const {
      addSavedNode,
      updateNodeLastSeen,
    } = await import('./storage');

    let changes = 0;
    win.addEventListener(NODE_REGISTRY_CHANGED_EVENT, () => { changes += 1; });

    addSavedNode('192.168.1.10', 'Stage Left');
    expect(changes).toBe(1);

    updateNodeLastSeen('192.168.1.10');
    expect(changes).toBe(1);
  });

  it('manages saved fleet nodes, metadata, renaming, and removal', async () => {
    const {
      getSavedNodes,
      addSavedNode,
      renameSavedNode,
      removeSavedNode,
      updateNodeLastSeen,
    } = await import('./storage');

    expect(getSavedNodes()).toEqual([]);

    addSavedNode('192.168.1.10', 'Stage Left');
    addSavedNode('192.168.1.20', 'Stage Right');

    let nodes = getSavedNodes();
    expect(nodes).toHaveLength(2);
    expect(nodes[0].name).toBe('Stage Left');
    expect(nodes[1].name).toBe('Stage Right');

    renameSavedNode('192.168.1.10', 'Main Truss');
    nodes = getSavedNodes();
    expect(nodes[0].name).toBe('Main Truss');

    updateNodeLastSeen('192.168.1.20');
    expect(getSavedNodes()[1].lastSeen).toBeGreaterThan(0);

    removeSavedNode('192.168.1.10');
    nodes = getSavedNodes();
    expect(nodes).toHaveLength(1);
    expect(nodes[0].ip).toBe('192.168.1.20');
  });
});

