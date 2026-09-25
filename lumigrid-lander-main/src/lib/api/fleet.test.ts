import { describe, it, expect, vi, beforeEach } from 'vitest';
import { getFleetNodes, type FleetNodesResponse } from './fleet';

describe('fleet discovery API client', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('fetches discovered fleet nodes from target node', async () => {
    const mockResponse: FleetNodesResponse = {
      self: {
        id: 'lg-84f703c29110',
        name: 'Stage Front',
        hostname: 'lg-84f703c29110',
        family: 'led-node',
        model: 'lg-led-v1',
        firmware: '1.0.0',
        hardwareRevision: 'A',
      },
      nodes: [
        {
          id: 'lg-001122334455',
          name: 'Stage Left',
          hostname: 'lg-001122334455.local',
          ip: '192.168.1.101',
          port: 80,
          family: 'led-node',
          model: 'lg-led-v1',
          firmware: '1.0.0',
          lastSeenMs: 12345000,
          ageMs: 4000,
          status: 'online',
        },
        {
          id: 'lg-aabbccddeeff',
          name: 'Stage Right',
          hostname: 'lg-aabbccddeeff.local',
          ip: '192.168.1.102',
          port: 80,
          family: 'led-node',
          model: 'lg-led-v1',
          firmware: '1.0.0',
          lastSeenMs: 12320000,
          ageMs: 29000,
          status: 'stale',
        },
      ],
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockResponse,
      clone() {
        return this;
      },
    });

    const res = await getFleetNodes('192.168.1.50');
    expect(res.self.id).toBe('lg-84f703c29110');
    expect(res.nodes.length).toBe(2);
    expect(res.nodes[0].id).toBe('lg-001122334455');
    expect(res.nodes[0].status).toBe('online');
    expect(res.nodes[1].id).toBe('lg-aabbccddeeff');
    expect(res.nodes[1].status).toBe('stale');
    expect(global.fetch).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/fleet/nodes',
      expect.objectContaining({
        signal: expect.any(AbortSignal),
      }),
    );
  });

  it('handles empty fleet list', async () => {
    const mockResponse: FleetNodesResponse = {
      self: {
        id: 'lg-84f703c29110',
        name: 'Isolated Node',
        hostname: 'lg-84f703c29110',
        family: 'led-node',
        model: 'lg-led-v1',
        firmware: '1.0.0',
        hardwareRevision: 'A',
      },
      nodes: [],
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockResponse,
      clone() {
        return this;
      },
    });

    const res = await getFleetNodes('192.168.1.50');
    expect(res.nodes.length).toBe(0);
  });

  it('propagates error when node returns non-200', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      ok: false,
      status: 403,
      statusText: 'Forbidden',
      json: async () => ({ error: 'unauthorized' }),
      clone() {
        return this;
      },
    });

    await expect(getFleetNodes('192.168.1.50')).rejects.toThrow();
  });
});
