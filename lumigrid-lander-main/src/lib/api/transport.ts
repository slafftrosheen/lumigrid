import { nodeAuthStore } from '../auth/nodeAuth';

export interface ApiFetchOptions extends RequestInit {
  apiKey?: string;
}

export function authHeaders(
  ip: string,
  headers?: HeadersInit,
  apiKey?: string
): Headers {
  const result = new Headers(headers);

  // 1. Explicit automation API Key (X-API-Key) takes priority if specified
  if (apiKey) {
    result.set('X-API-Key', apiKey);
    return result;
  }

  // 2. Per-node browser session token (Authorization: Bearer <session-token>)
  if (!result.has('Authorization') && !result.has('X-API-Key')) {
    const session = nodeAuthStore.getSession(ip);
    if (session && session.sessionToken) {
      result.set('Authorization', `Bearer ${session.sessionToken}`);
    }
  }

  return result;
}

export class TransportBlockedError extends Error {
  constructor(public nodeIp: string) {
    super('Direct local-node access is blocked by this browser context. Open the dashboard directly from a LumiGrid node.');
    this.name = 'TransportBlockedError';
  }
}

export function isTransportBlockedError(error: unknown): boolean {
  return error instanceof TransportBlockedError ||
    (error instanceof Error && error.name === 'TransportBlockedError');
}

export function isBrowserHttpsContext(): boolean {
  return typeof location !== 'undefined' && location.protocol === 'https:';
}

/**
 * Return canonical HTTP base URL for a LumiGrid node.
 * Current firmware serves plain HTTP. Does not guess HTTPS from dashboard origin.
 */
export function getNodeHttpBase(address: string): string {
  const clean = address.trim().replace(/^https?:\/\//i, '').replace(/\/+$/, '');
  return `http://${clean}`;
}

/**
 * Return canonical WebSocket URL for a LumiGrid node.
 * Current firmware serves plain WS. Does not guess WSS from dashboard origin.
 */
export function getNodeWsUrl(address: string, path = '/ws'): string {
  const clean = address.trim()
    .replace(/^wss?:\/\//i, '')
    .replace(/^https?:\/\//i, '')
    .replace(/\/+$/, '');
  const cleanPath = path.startsWith('/') ? path : `/${path}`;
  return `ws://${clean}${cleanPath}`;
}

async function consumeNodeResponse<T>(
  ip: string,
  path: string,
  options: ApiFetchOptions,
  timeoutMs: number,
  consume: (response: Response, signal: AbortSignal) => Promise<T>,
): Promise<T> {
  const base = getNodeHttpBase(ip);
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  const abort = () => controller.abort(options.signal?.reason);
  options.signal?.addEventListener('abort', abort, { once: true });
  if (options.signal?.aborted) abort();

  const reqHeaders = authHeaders(ip, options.headers, options.apiKey);
  const session = nodeAuthStore.getSession(ip);

  try {
    const response = await fetch(`${base}${path}`, {
      ...options,
      headers: reqHeaders,
      signal: controller.signal,
    }).catch((err: unknown) => {
      if (isBrowserHttpsContext() && err instanceof TypeError && !controller.signal.aborted) {
        throw new TransportBlockedError(ip);
      }
      throw err;
    });

    // On 401: clear only the affected node's browser session
    if (response.status === 401 && session && nodeAuthStore.getSession(ip) === session &&
        reqHeaders.get('Authorization') === `Bearer ${session.sessionToken}`) {
      nodeAuthStore.clearSession(ip);
    }

    // The JSON caller owns the controller until the body is fully consumed.
    return await consume(response, controller.signal);
  } finally {
    clearTimeout(timer);
    options.signal?.removeEventListener('abort', abort);
  }
}

/** Raw callers receive headers and retain responsibility for consuming the body. */
export function apiFetch(
  ip: string,
  path: string,
  options: ApiFetchOptions = {},
  timeoutMs = 8000,
): Promise<Response> {
  return consumeNodeResponse(ip, path, options, timeoutMs, async response => response);
}

export class ApiError extends Error {
  constructor(
    public status: number,
    public endpoint: string,
    public nodeIp: string,
    public serverMessage: string,
  ) {
    const text = serverMessage ? ` ${serverMessage}` : '';
    super(`[${nodeIp}] ${endpoint} → HTTP ${status}${text}`);
    this.name = 'ApiError';
  }
}

export async function apiJson<T>(
  ip: string,
  path: string,
  options: ApiFetchOptions = {},
  timeoutMs = 8000,
): Promise<T> {
  const session = nodeAuthStore.getSession(ip);
  // Explicit automation credentials are independent of the dashboard session.
  const ownsSession = !options.apiKey && !new Headers(options.headers).has('X-API-Key');
  const assertCurrent = (signal: AbortSignal) => {
    if (signal.aborted || (ownsSession && nodeAuthStore.getSession(ip) !== session)) {
      throw new DOMException('Node session changed or request was cancelled', 'AbortError');
    }
  };
  try {
    return await consumeNodeResponse(ip, path, options, timeoutMs, async (response, signal) => {
      if (!response.ok) {
        let detail = response.statusText;
        try {
          const body = await response.json() as { error?: string };
          if (body?.error) {
            detail = response.statusText ? `${response.statusText}: ${body.error}` : body.error;
          }
        } catch {
          // Keep HTTP status text when the node returns a non-JSON error body.
        }
        throw new ApiError(response.status, path, ip, detail);
      }
      const value = await response.json() as T;
      assertCurrent(signal);
      return value;
    });
  } catch (error: unknown) {
    if (error instanceof ApiError || isTransportBlockedError(error) ||
        (error instanceof Error && error.name === 'AbortError')) throw error;
    const message = error instanceof Error ? error.message : String(error);
    throw new Error(`[${ip}] ${path} → ${message}`);
  }
}
