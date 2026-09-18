import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import { getChannels, testChannel, type HardwareChannel } from './channels';

describe('channels API contracts (/api/v1/channels and /api/v1/channel/test)', () => {
  const originalFetch = globalThis.fetch;

  beforeEach(() => {
    vi.restoreAllMocks();
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.stubGlobal('fetch', originalFetch);
  });

  it('fetches all 16 hardware channels from /api/v1/channels', async () => {
    const mockChannels: HardwareChannel[] = [
      {
        index: 0,
        port: 'A1',
        kind: 'addressable',
        hardwareId: 0,
        gpio: 16,
        configured: true,
        enabled: true,
        name: 'Front Logo',
        maxBrightness: 255,
        gammaCorrection: true,
        maxMilliwatts: 0,
        dmxUniverse: 1,
        dmxStartChannel: 1,
        dmxMode: 0,
        addressable: { type: 'WS2812', pixels: 120, pixelOrder: 'GRB' },
      },
      {
        index: 8,
        port: 'P1',
        kind: 'pwm',
        hardwareId: 0,
        configured: false,
        enabled: false,
        name: '',
        maxBrightness: 255,
        gammaCorrection: true,
        maxMilliwatts: 0,
        dmxUniverse: 1,
        dmxStartChannel: 1,
        dmxMode: 0,
      },
    ];

    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ channels: mockChannels }), { status: 200 }),
    );

    const channels = await getChannels('192.168.1.50');
    expect(channels).toHaveLength(2);
    expect(channels[0].port).toBe('A1');
    expect(channels[0].kind).toBe('addressable');
    expect(channels[1].port).toBe('P1');
    expect(channels[1].kind).toBe('pwm');

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/channels',
      expect.objectContaining({
        headers: expect.any(Headers),
        signal: expect.any(AbortSignal),
      }),
    );
  });

  it('invokes channel test/identify via POST /api/v1/channel/test', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ accepted: true, commandId: 42, channel: 2, mode: 'identify', durationMs: 3000 }), {
        status: 200,
      }),
    );

    const res = await testChannel('192.168.1.50', 2, 'identify', 3000);
    expect(res.accepted).toBe(true);
    expect(res.commandId).toBe(42);

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/channel/test',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({ channel: 2, mode: 'identify', durationMs: 3000 }),
      }),
    );
  });
});
