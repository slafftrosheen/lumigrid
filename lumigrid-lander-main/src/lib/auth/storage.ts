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

/** Return the currently selected node without exposing storage to routes. */
export function getSelectedNodeIp(fallback = ''): string {
  return readString(NODE_IP_KEY, fallback).trim();
}

/** Persist the selected node in one place for all dashboard surfaces. */
export function setSelectedNodeIp(nodeIp: string): void {
  const value = nodeIp.trim();
  if (value) writeString(NODE_IP_KEY, value);
  else removeValue(NODE_IP_KEY);
}

// ── API Credential & Session Management ───────────────────────────────────────

export function getApiToken(): string {
  return readString(API_TOKEN_KEY, '').trim();
}

export function setApiToken(token: string): void {
  const val = token.trim();
  if (val) writeString(API_TOKEN_KEY, val);
  else removeValue(API_TOKEN_KEY);
}

export function clearApiToken(): void {
  removeValue(API_TOKEN_KEY);
}

/**
 * Atomically lock the local dashboard session:
 * Clears dashboard auth timestamp, dashboard role, and active API token.
 */
export function lockDashboardSession(): void {
  removeValue(AUTH_TIMESTAMP_KEY);
  removeValue(AUTH_ROLE_KEY);
  removeValue(API_TOKEN_KEY);
}

/**
 * Atomically unlock the local dashboard session with a specific role and corresponding API token:
 * Admin -> 'lumigrid_admin'
 * Operator -> 'lumigrid_op'
 * Installer -> 'lumigrid_install'
 */
export function unlockDashboardSession(role: 'admin' | 'operator' | 'installer'): void {
  writeString(AUTH_TIMESTAMP_KEY, String(Date.now()));
  writeString(AUTH_ROLE_KEY, role);
  if (role === 'admin') {
    setApiToken('lumigrid_admin');
  } else if (role === 'installer') {
    setApiToken('lumigrid_install');
  } else {
    setApiToken('lumigrid_op');
  }
}

// ── Fleet Node Metadata ────────────────────────────────────────────────────────

export const NODE_META_KEY = 'lg_nodes_meta';

export interface SavedNodeInfo {
  ip: string;
  name: string;
  lastSeen?: number;
}

export function readNodeMetadataMap(): Record<string, { name?: string; lastSeen?: number }> {
  const raw = readString(NODE_META_KEY, '{}');
  try {
    const parsed = JSON.parse(raw);
    return typeof parsed === 'object' && parsed !== null ? parsed : {};
  } catch {
    return {};
  }
}

export function writeNodeMetadataMap(map: Record<string, { name?: string; lastSeen?: number }>): void {
  writeString(NODE_META_KEY, JSON.stringify(map));
}

export function getSavedNodes(): SavedNodeInfo[] {
  const ips = readStringList(NODE_LIST_KEY);
  const metaMap = readNodeMetadataMap();
  return ips.map(ip => ({
    ip,
    name: metaMap[ip]?.name || ip,
    lastSeen: metaMap[ip]?.lastSeen,
  }));
}

export function addSavedNode(ip: string, name?: string): void {
  const cleanIp = ip.trim();
  if (!cleanIp) return;
  const ips = readStringList(NODE_LIST_KEY);
  if (!ips.includes(cleanIp)) {
    writeStringList(NODE_LIST_KEY, [...ips, cleanIp]);
  }
  const metaMap = readNodeMetadataMap();
  metaMap[cleanIp] = {
    ...metaMap[cleanIp],
    name: name?.trim() || metaMap[cleanIp]?.name || cleanIp,
    lastSeen: Date.now(),
  };
  writeNodeMetadataMap(metaMap);
}

export function renameSavedNode(ip: string, newName: string): void {
  const cleanIp = ip.trim();
  const cleanName = newName.trim();
  if (!cleanIp || !cleanName) return;
  const metaMap = readNodeMetadataMap();
  metaMap[cleanIp] = {
    ...metaMap[cleanIp],
    name: cleanName,
  };
  writeNodeMetadataMap(metaMap);
}

export function removeSavedNode(ip: string): void {
  const cleanIp = ip.trim();
  const ips = readStringList(NODE_LIST_KEY).filter(i => i !== cleanIp);
  writeStringList(NODE_LIST_KEY, ips);

  const metaMap = readNodeMetadataMap();
  delete metaMap[cleanIp];
  writeNodeMetadataMap(metaMap);

  if (getSelectedNodeIp() === cleanIp) {
    setSelectedNodeIp(ips.length > 0 ? ips[0] : '');
  }
}

export function updateNodeLastSeen(ip: string): void {
  const cleanIp = ip.trim();
  if (!cleanIp) return;
  const metaMap = readNodeMetadataMap();
  metaMap[cleanIp] = {
    ...metaMap[cleanIp],
    lastSeen: Date.now(),
  };
  writeNodeMetadataMap(metaMap);
}

