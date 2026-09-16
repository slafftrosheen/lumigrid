import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import { apiFetch, apiJson, ApiError, DEV_API_TOKEN } from './transport';

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

  it('surfaces structured ApiError with status 403 Forbidden', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ error: 'operator token required' }), {
        status: 403,
        statusText: 'Forbidden',
      }),
    );

    try {
      await apiJson('node.local', '/api/v1/activate', { method: 'POST' });
      expect.unreachable('Should have thrown ApiError');
    } catch (e: any) {
      expect(e).toBeInstanceOf(ApiError);
      expect(e.status).toBe(403);
      expect(e.endpoint).toBe('/api/v1/activate');
      expect(e.nodeIp).toBe('node.local');
      expect(e.message).toContain('HTTP 403 Forbidden: operator token required');
    }
  });

  it('handles 401 Unauthorized responses', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ error: 'invalid token' }), {
        status: 401,
        statusText: 'Unauthorized',
      }),
    );

    await expect(apiJson('192.168.1.50', '/api/v1/config'))
      .rejects.toThrow('HTTP 401 Unauthorized: invalid token');
  });

  it('handles 404 Not Found responses', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ error: 'preset not found' }), {
        status: 404,
        statusText: 'Not Found',
      }),
    );

    await expect(apiJson('192.168.1.50', '/api/v1/presets/99'))
      .rejects.toThrow('HTTP 404 Not Found: preset not found');
  });

  it('handles 422 Unprocessable Entity responses', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ error: 'invalid parameters' }), {
        status: 422,
        statusText: 'Unprocessable Entity',
      }),
    );

    await expect(apiJson('192.168.1.50', '/api/v1/sequence', { method: 'PUT' }))
      .rejects.toThrow('HTTP 422 Unprocessable Entity: invalid parameters');
  });

  it('handles 500 Internal Server Error without JSON error body', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response('NVS storage error', {
        status: 500,
        statusText: 'Internal Server Error',
      }),
    );

    await expect(apiJson('192.168.1.50', '/api/v1/backup'))
      .rejects.toThrow('HTTP 500 Internal Server Error');
  });

  it('rejects when response body is not valid JSON', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response('<html><body>Gateway Timeout</body></html>', { status: 200 }),
    );

    await expect(apiJson('192.168.1.50', '/api/v1/health'))
      .rejects.toThrow();
  });
});
