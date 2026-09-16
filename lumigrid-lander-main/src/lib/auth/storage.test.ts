import { describe, expect, it, beforeEach, afterEach, vi } from 'vitest';
import { getSelectedNodeIp, setSelectedNodeIp, NODE_IP_KEY } from './storage';

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
});
