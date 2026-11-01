import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import { apiFetch, apiJson, ApiError } from './transport';
import { nodeAuthStore } from '../auth/nodeAuth';

describe('dashboard API transport', () => {
  const originalFetch = globalThis.fetch;

  beforeEach(() => {
    vi.restoreAllMocks();
    vi.stubGlobal('fetch', vi.fn());
    nodeAuthStore.clearAllSessions();
  });

  afterEach(() => {
    vi.stubGlobal('fetch', originalFetch);
    nodeAuthStore.clearAllSessions();
  });

  it('transmits Bearer session token for authenticated node', async () => {
    nodeAuthStore.setSession({
      address: '192.168.1.10',
      nodeId: 'node-10',
      role: 'admin',
      sessionToken: 'test-session-token-xyz',
      expiresAt: Date.now() + 3600000,
    });

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
    expect((options?.headers as Headers).get('Authorization')).toBe('Bearer test-session-token-xyz');
  });

  it('transmits X-API-Key when explicit apiKey option is provided', async () => {
    vi.mocked(fetch).mockResolvedValue(new Response(JSON.stringify({ ok: true }), { status: 200 }));

    await apiFetch('192.168.1.10', '/api/v1/health', { apiKey: 'automation-secret-key-123' });

    const options = vi.mocked(fetch).mock.calls[0]?.[1];
    expect((options?.headers as Headers).get('X-API-Key')).toBe('automation-secret-key-123');
    expect((options?.headers as Headers).get('Authorization')).toBeNull();
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

  it('handles 401 Unauthorized responses and clears only affected node session', async () => {
    nodeAuthStore.setSession({
      address: '192.168.1.50',
      nodeId: 'node-50',
      role: 'admin',
      sessionToken: 'node50-token',
      expiresAt: Date.now() + 3600000,
    });
    nodeAuthStore.setSession({
      address: '192.168.1.60',
      nodeId: 'node-60',
      role: 'admin',
      sessionToken: 'node60-token',
      expiresAt: Date.now() + 3600000,
    });

    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ error: 'invalid token' }), {
        status: 401,
        statusText: 'Unauthorized',
      }),
    );

    await expect(apiJson('192.168.1.50', '/api/v1/config'))
      .rejects.toThrow('HTTP 401 Unauthorized: invalid token');

    // 192.168.1.50 cleared, but 192.168.1.60 preserved
    expect(nodeAuthStore.hasValidSession('192.168.1.50')).toBe(false);
    expect(nodeAuthStore.hasValidSession('192.168.1.60')).toBe(true);
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

  it('does not let an old 401 clear a replacement session on the same address', async () => {
    const setSession = (token: string) => nodeAuthStore.setSession({
      address: 'node.local', role: 'admin', sessionToken: token, expiresAt: Date.now() + 60000,
    });
    setSession('old');
    let resolve!: (response: Response) => void;
    vi.mocked(fetch).mockReturnValue(new Promise(done => { resolve = done; }));
    const pending = apiJson('node.local', '/api/v1/config');
    setSession('new');
    resolve(new Response('{"error":"expired session"}', { status: 401 }));
    await expect(pending).rejects.toThrow('HTTP 401');
    expect(nodeAuthStore.getSession('node.local')?.sessionToken).toBe('new');
  });

  it('discards a successful response body completed under an obsolete session', async () => {
    const setSession = (token: string) => nodeAuthStore.setSession({
      address: 'node.local', role: 'admin', sessionToken: token, expiresAt: Date.now() + 60000,
    });
    setSession('old');
    let resolve!: (body: unknown) => void;
    const response = new Response();
    vi.spyOn(response, 'json').mockReturnValue(new Promise(done => { resolve = done; }));
    vi.mocked(fetch).mockResolvedValue(response);
    const pending = apiJson('node.local', '/api/v1/config');
    await Promise.resolve();
    await Promise.resolve();
    setSession('new');
    resolve({ saved: true });
    await expect(pending).rejects.toMatchObject({ name: 'AbortError' });
  });

  it('honors a caller cancellation instead of replacing its signal', async () => {
    const controller = new AbortController();
    vi.mocked(fetch).mockImplementation(async (_url, options) => {
      expect(options?.signal?.aborted).toBe(true);
      throw new DOMException('Cancelled', 'AbortError');
    });
    controller.abort();
    await expect(apiJson('node.local', '/api/v1/health', { signal: controller.signal }))
      .rejects.toMatchObject({ name: 'AbortError' });
  });

  it.each(['timeout', 'caller abort'] as const)('cancels a stalled JSON body on %s after headers arrived', async (cause) => {
    vi.useFakeTimers();
    const controller = new AbortController();
    let bodyStarted!: () => void;
    const started = new Promise<void>(resolve => { bodyStarted = resolve; });
    let fetchSignal!: AbortSignal;
    vi.mocked(fetch).mockImplementation(async (_url, options) => {
      fetchSignal = options!.signal!;
      const stream = new ReadableStream<Uint8Array>({
        start(body) {
          body.enqueue(new TextEncoder().encode('{"partial":'));
          fetchSignal.addEventListener('abort', () => {
            body.error(new DOMException('Body cancelled', 'AbortError'));
          }, { once: true });
        },
        pull() { bodyStarted(); },
      });
      return new Response(stream, { status: 200 });
    });
    try {
      const pending = apiJson('node.local', '/api/v1/health', { signal: controller.signal }, 100);
      const rejected = expect(pending).rejects.toMatchObject({ name: 'AbortError' });
      await started;
      await Promise.resolve();
      if (cause === 'timeout') await vi.advanceTimersByTimeAsync(101);
      else controller.abort();
      await rejected;
      expect(fetchSignal.aborted).toBe(true);
    } finally {
      vi.useRealTimers();
    }
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
