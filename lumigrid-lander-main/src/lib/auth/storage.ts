/**
 * Browser-only dashboard storage helpers.
 *
 * Keep localStorage access behind these functions so dashboard routes do not
 * accidentally touch browser globals during module evaluation. The firmware
 * remains the authorization authority; these values are only client state.
 */

export const NODE_IP_KEY = 'lg_node_ip';
export const NODE_LIST_KEY = 'lg_nodes';
export const API_TOKEN_KEY = 'lg_api_token';
export const AUTH_TIMESTAMP_KEY = 'lg_auth_ts';
export const AUTH_ROLE_KEY = 'lg_auth_role';

function storage(): Storage | null {
  return typeof localStorage === 'undefined' ? null : localStorage;
}

export function readString(key: string, fallback = ''): string {
  return storage()?.getItem(key) ?? fallback;
}

export function writeString(key: string, value: string): void {
  try { storage()?.setItem(key, value); } catch {}
}

export function removeValue(key: string): void {
  try { storage()?.removeItem(key); } catch {}
}

export function readStringList(key: string): string[] {
  const raw = readString(key, '[]');
  try {
    const parsed: unknown = JSON.parse(raw);
    return Array.isArray(parsed) && parsed.every(item => typeof item === 'string')
      ? parsed
      : [];
  } catch {
    return [];
  }
}

export function writeStringList(key: string, values: string[]): void {
  writeString(key, JSON.stringify(values));
}
