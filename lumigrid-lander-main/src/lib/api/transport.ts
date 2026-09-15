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
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    return await fetch(`http://${ip}${path}`, {
      ...options,
      headers: authHeaders(options.headers),
      signal: controller.signal,
    });
  } finally {
    clearTimeout(timer);
  }
}

export async function apiJson<T>(
  ip: string,
  path: string,
  options: RequestInit = {},
  timeoutMs = 8000,
): Promise<T> {
  try {
    const response = await apiFetch(ip, path, options, timeoutMs);
    if (!response.ok) {
      let detail = '';
      try {
        const body = await response.clone().json() as { error?: string };
        if (body?.error) detail = `: ${body.error}`;
      } catch {
        // Keep HTTP status when the node returns a non-JSON error body.
      }
      throw new Error(`HTTP ${response.status} ${response.statusText}${detail}`);
    }
    return await response.json() as T;
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    throw new Error(`[${ip}] ${path} → ${message}`);
  }
}
