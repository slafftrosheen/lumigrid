import { API_TOKEN_KEY, readString, removeValue, writeString } from '../auth/storage';

/** Development credential matching the firmware's default ConfigStore value. */
export const DEV_API_TOKEN = 'lumigrid_admin';

export function getApiToken(): string {
  return readString(API_TOKEN_KEY, DEV_API_TOKEN);
}

export function setApiToken(token: string): void {
  const value = token.trim();
  if (value) writeString(API_TOKEN_KEY, value);
  else removeValue(API_TOKEN_KEY);
}

export function clearApiToken(): void {
  removeValue(API_TOKEN_KEY);
}

export function authHeaders(headers?: HeadersInit): Headers {
  const result = new Headers(headers);
  result.set('Authorization', `Bearer ${getApiToken()}`);
  return result;
}

export async function apiFetch(
  ip: string,
  path: string,
  options: RequestInit = {},
  timeoutMs = 8000,
): Promise<Response> {
  // Derive http/https from the page's own protocol so that Mixed Content errors
  // don't block requests when the dashboard is served over HTTPS (e.g. Vercel).
  const scheme =
    typeof location !== 'undefined' && location.protocol === 'https:' ? 'https' : 'http';
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    return await fetch(`${scheme}://${ip}${path}`, {
      ...options,
      headers: authHeaders(options.headers),
      signal: controller.signal,
    });
  } finally {
    clearTimeout(timer);
  }
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
  options: RequestInit = {},
  timeoutMs = 8000,
): Promise<T> {
  let response: Response;
  try {
    response = await apiFetch(ip, path, options, timeoutMs);
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    throw new Error(`[${ip}] ${path} → ${message}`);
  }

  if (!response.ok) {
    let detail = response.statusText;
    try {
      const body = await response.clone().json() as { error?: string };
      if (body?.error) {
        detail = response.statusText ? `${response.statusText}: ${body.error}` : body.error;
      }
    } catch {
      // Keep HTTP status text when the node returns a non-JSON error body.
    }
    throw new ApiError(response.status, path, ip, detail);
  }

  return await response.json() as T;
}
