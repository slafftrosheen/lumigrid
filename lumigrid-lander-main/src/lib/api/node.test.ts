import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import {
  getPresets,
  savePreset,
  activatePreset,
  getSignageStatus,
  getEffects,
  getSequence,
  saveSequence,
  getHealth,
  getAuditLog,
  getFactoryStatus,
  postFactoryEffect,
  postFactoryAbort,
  postFactoryAutoLoop,
  type ScenePreset,
  type Sequence,
} from './node';

describe('canonical dashboard API contracts (/api/v1/*)', () => {
  const originalFetch = globalThis.fetch;

  beforeEach(() => {
    vi.restoreAllMocks();
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.stubGlobal('fetch', originalFetch);
  });

  it('fetches presets from /api/v1/presets', async () => {
    const mockPresets = [{ index: 0, name: 'Chill', globalBrightness: 100, transitionMs: 500, zones: [] }];
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify(mockPresets), { status: 200 }));

    const res = await getPresets('192.168.1.50');
    expect(res).toEqual(mockPresets);

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/presets',
      expect.objectContaining({
        headers: expect.any(Headers),
        signal: expect.any(AbortSignal),
      }),
    );
  });

  it('saves a preset via PUT /api/v1/presets', async () => {
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ saved: true }), { status: 200 }));

    const preset: Partial<ScenePreset> & { index: number } = { index: 1, name: 'Party' };
    const res = await savePreset('192.168.1.50', preset);
    expect(res).toEqual({ saved: true });

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/presets',
      expect.objectContaining({
        method: 'PUT',
        body: JSON.stringify(preset),
      }),
    );
  });

  it('requests preset activation via POST /api/v1/activate', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(
        JSON.stringify({
          accepted: true,
          preset: 2,
          generation: 41,
        }),
        {
          status: 202,
          headers: {
            'Content-Type': 'application/json',
          },
        },
      ),
    );

    const res = await activatePreset('192.168.1.50', 2);

    expect(res).toEqual({
      accepted: true,
      preset: 2,
      generation: 41,
    });

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/activate',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({
          preset: 2,
        }),
      }),
    );
  });

  it('propagates rejected preset activation', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(
        JSON.stringify({
          error: 'scene_activation_rejected',
        }),
        {
          status: 409,
          statusText: 'Conflict',
          headers: {
            'Content-Type': 'application/json',
          },
        },
      ),
    );

    await expect(
      activatePreset('192.168.1.50', 7),
    ).rejects.toThrow(/scene_activation_rejected/);
  });

  it('reads scene request/application telemetry from /api/v1/status', async () => {
    const mockStatus = {
      activePreset: 2,
      requestedPreset: 2,
      requestedGeneration: 41,
      appliedGeneration: 41,
      sceneApplyPending: false,
      transitioning: false,
      outputFault: false,
    };

    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify(mockStatus), { status: 200 }),
    );

    const status = await getSignageStatus('192.168.1.50');
    expect(status).toEqual(mockStatus);

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/status',
      expect.objectContaining({
        headers: expect.any(Headers),
        signal: expect.any(AbortSignal),
      }),
    );
  });

  it('loads effects catalog from /api/v1/effects', async () => {
    const mockEffects = [{ id: 'rainbow', label: 'Rainbow', description: '', hasParameter: false }];
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify(mockEffects), { status: 200 }));

    const res = await getEffects('192.168.1.50');
    expect(res).toEqual(mockEffects);
  });

  it('gets and saves timeline sequence via /api/v1/sequence', async () => {
    const mockSeq: Sequence = { bpm: 128, enabled: true, tracks: [] };
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify(mockSeq), { status: 200 }));

    const getRes = await getSequence('node.local');
    expect(getRes).toEqual(mockSeq);

    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ saved: true }), { status: 200 }));
    const saveRes = await saveSequence('node.local', mockSeq);
    expect(saveRes).toEqual({ saved: true });

    expect(vi.mocked(fetch)).toHaveBeenLastCalledWith(
      'http://node.local/api/v1/sequence',
      expect.objectContaining({
        method: 'PUT',
        body: JSON.stringify(mockSeq),
      }),
    );
  });

  it('retrieves health and audit telemetry', async () => {
    const mockHealth = { schema_version: 1, system: { uptime_s: 120 } };
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify(mockHealth), { status: 200 }));

    const health = await getHealth('10.0.0.1');
    expect(health).toEqual(mockHealth);

    const mockAudit = [{ timestamp_s: 100, role: 3, action: 'boot', detail: 'clean' }];
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify(mockAudit), { status: 200 }));

    const audit = await getAuditLog('10.0.0.1');
    expect(audit).toEqual(mockAudit);
  });

  it('controls factory effects and blackout via factory endpoints', async () => {
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ effect: 'burn_in', brightness: 64 }), { status: 200 }));
    const status = await getFactoryStatus('10.0.0.1');
    expect(status.effect).toBe('burn_in');

    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ accepted: true }), { status: 200 }));
    await postFactoryEffect('10.0.0.1', 'pixel_chase', 128, 5000);
    expect(vi.mocked(fetch)).toHaveBeenLastCalledWith(
      'http://10.0.0.1/api/v1/factory/effect',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({ effect: 'pixel_chase', brightness: 128, durationMs: 5000 }),
      }),
    );

    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ accepted: true, action: 'blackout' }), { status: 200 }));
    const abortRes = await postFactoryAbort('10.0.0.1');
    expect(abortRes).toEqual({ accepted: true, action: 'blackout' });

    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ accepted: true }), { status: 200 }));
    await postFactoryAutoLoop('10.0.0.1', true);
    expect(vi.mocked(fetch)).toHaveBeenLastCalledWith(
      'http://10.0.0.1/api/v1/factory/auto-loop',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({ enabled: true }),
      }),
    );
  });
});
