import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import { apiFetch, apiJson, DEV_API_TOKEN } from './transport';

describe('dashboard API transport', () => {
  const originalFetch = globalThis.fetch;

  beforeEach(() => {
    vi.restoreAllMocks();
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.stubGlobal('fetch', originalFetch);
  });

  it('uses the development admin credential by default', async () => {
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ ok: true }), { status: 200 }));

    await apiFetch('192.168.1.10', '/api/v1/health');

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      'http://192.168.1.10/api/v1/health',
      expect.objectContaining({
        headers: expect.any(Headers),
        signal: expect.any(AbortSignal),
      }),
    );

    const options = vi.mocked(fetch).mock.calls[0]?.[1];
    expect((options?.headers as Headers).get('Authorization')).toBe(`Bearer ${DEV_API_TOKEN}`);
  });

  it('decodes successful JSON responses', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ status: 'ready', brightness: 42 }), { status: 200 }),
    );

    await expect(apiJson<{ status: string; brightness: number }>('node.local', '/api/v1/health'))
      .resolves.toEqual({ status: 'ready', brightness: 42 });
  });

  it('surfaces structured API errors with node and endpoint context', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ error: 'operator token required' }), {
        status: 403,
        statusText: 'Forbidden',
      }),
    );

    await expect(apiJson('node.local', '/api/v1/activate', { method: 'POST' }))
      .rejects.toThrow('[node.local] /api/v1/activate → HTTP 403 Forbidden: operator token required');
  });
});
