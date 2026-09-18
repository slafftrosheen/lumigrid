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

export const NODE_META_KEY = 'lg_nodes_meta';
export const NODE_REGISTRY_KEY = 'lg_nodes_v3';

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

// ── Fleet Node & Topology Model (Control Plane v3) ───────────────────────────

export type DiscoverySource = 'mdns' | 'sync' | 'mqtt' | 'manual';

export type NodeConnectionStatus =
  | 'offline'
  | 'discovering'
  | 'connecting'
  | 'online'
  | 'degraded';

export interface NodeChild {
  id: string;
  parentId: string;
  kind: 'zone' | 'output' | 'sensor';
  name: string;
}

export interface NodeRecord {
  id: string; // Immutable node ID: "lg-84f703c29110"
  name: string;
  address?: string; // Transport route IP / hostname
  family: string;
  model: string;
  lastSeen: number;
  discoverySource: DiscoverySource;
  connection: NodeConnectionStatus;
  children?: NodeChild[];
}

export interface SavedNodeInfo {
  ip: string;
  name: string;
  lastSeen?: number;
  id?: string;
}

const DISCOVERY_PRIORITY: Record<DiscoverySource, number> = {
  mdns: 4,
  sync: 3,
  mqtt: 2,
  manual: 1,
};

export function readNodeRegistry(): Record<string, NodeRecord> {
  const raw = readString(NODE_REGISTRY_KEY, '');
  if (raw) {
    try {
      const parsed = JSON.parse(raw);
      if (typeof parsed === 'object' && parsed !== null) {
        return parsed as Record<string, NodeRecord>;
      }
    } catch {}
  }

  // Automatic one-time migration from legacy storage
  const legacyIps = readStringList(NODE_LIST_KEY);
  const legacyMeta = readNodeMetadataMap();
  const migrated: Record<string, NodeRecord> = {};

  for (const ip of legacyIps) {
    const id = `node-${ip.replace(/\./g, '-')}`;
    migrated[id] = {
      id,
      name: legacyMeta[ip]?.name || ip,
      address: ip,
      family: 'led-node',
      model: 'lg-led-v1',
      lastSeen: legacyMeta[ip]?.lastSeen || Date.now(),
      discoverySource: 'manual',
      connection: 'offline',
    };
  }

  if (Object.keys(migrated).length > 0) {
    writeString(NODE_REGISTRY_KEY, JSON.stringify(migrated));
  }

  return migrated;
}

export function writeNodeRegistry(registry: Record<string, NodeRecord>): void {
  writeString(NODE_REGISTRY_KEY, JSON.stringify(registry));
}

export function upsertNodeRecord(record: Partial<NodeRecord> & { id: string }): NodeRecord {
  const registry = readNodeRegistry();
  const existing = registry[record.id];

  // Precedence rule: do not let a lower-priority discovery source override higher-priority data
  let effectiveSource = record.discoverySource ?? existing?.discoverySource ?? 'manual';
  if (existing && record.discoverySource) {
    const oldPri = DISCOVERY_PRIORITY[existing.discoverySource] ?? 0;
    const newPri = DISCOVERY_PRIORITY[record.discoverySource] ?? 0;
    if (oldPri > newPri) {
      effectiveSource = existing.discoverySource;
    }
  }

  // If a manual entry with the same address exists, merge into canonical ID
  if (record.address) {
    for (const [key, val] of Object.entries(registry)) {
      if (key !== record.id && val.address === record.address) {
        delete registry[key];
      }
    }
  }

  const updated: NodeRecord = {
    id: record.id,
    name: record.name ?? existing?.name ?? record.address ?? record.id,
    address: record.address ?? existing?.address,
    family: record.family ?? existing?.family ?? 'led-node',
    model: record.model ?? existing?.model ?? 'lg-led-v1',
    lastSeen: record.lastSeen ?? Date.now(),
    discoverySource: effectiveSource,
    connection: record.connection ?? existing?.connection ?? 'offline',
    children: record.children ?? existing?.children ?? [],
  };

  registry[record.id] = updated;
  writeNodeRegistry(registry);

  // Synchronize legacy keys for backwards compatibility
  syncLegacyFromRegistry(registry);

  return updated;
}

export function recordDiscoveredNode(
  node: { id: string; address?: string; name?: string; family?: string; model?: string },
  source: DiscoverySource = 'sync'
): NodeRecord {
  return upsertNodeRecord({
    id: node.id,
    address: node.address,
    name: node.name,
    family: node.family,
    model: node.model,
    discoverySource: source,
    lastSeen: Date.now(),
  });
}

function syncLegacyFromRegistry(registry: Record<string, NodeRecord>): void {
  const ips: string[] = [];
  const meta: Record<string, { name?: string; lastSeen?: number }> = {};

  for (const rec of Object.values(registry)) {
    if (rec.address) {
      ips.push(rec.address);
      meta[rec.address] = { name: rec.name, lastSeen: rec.lastSeen };
    }
  }

  writeStringList(NODE_LIST_KEY, ips);
  writeString(NODE_META_KEY, JSON.stringify(meta));
}

// ── Backwards-Compatible Saved Node API ───────────────────────────────────────

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
  const registry = readNodeRegistry();
  const records = Object.values(registry);
  if (records.length > 0) {
    return records
      .filter(r => Boolean(r.address))
      .map(r => ({
        ip: r.address!,
        name: r.name,
        lastSeen: r.lastSeen,
        id: r.id,
      }));
  }

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

  const id = `node-${cleanIp.replace(/\./g, '-')}`;
  upsertNodeRecord({
    id,
    name: name?.trim() || cleanIp,
    address: cleanIp,
    discoverySource: 'manual',
    lastSeen: Date.now(),
  });
}

export function renameSavedNode(ip: string, newName: string): void {
  const cleanIp = ip.trim();
  const cleanName = newName.trim();
  if (!cleanIp || !cleanName) return;

  const registry = readNodeRegistry();
  for (const rec of Object.values(registry)) {
    if (rec.address === cleanIp) {
      rec.name = cleanName;
      writeNodeRegistry(registry);
      syncLegacyFromRegistry(registry);
      return;
    }
  }

  addSavedNode(cleanIp, cleanName);
}

export function removeSavedNode(ip: string): void {
  const cleanIp = ip.trim();
  const registry = readNodeRegistry();

  for (const [key, rec] of Object.entries(registry)) {
    if (rec.address === cleanIp) {
      delete registry[key];
    }
  }
  writeNodeRegistry(registry);
  syncLegacyFromRegistry(registry);

  if (getSelectedNodeIp() === cleanIp) {
    const remaining = Object.values(registry).find(r => Boolean(r.address));
    setSelectedNodeIp(remaining?.address || '');
  }
}

export function updateNodeLastSeen(ip: string): void {
  const cleanIp = ip.trim();
  if (!cleanIp) return;

  const registry = readNodeRegistry();
  let found = false;
  for (const rec of Object.values(registry)) {
    if (rec.address === cleanIp) {
      rec.lastSeen = Date.now();
      found = true;
    }
  }
  if (found) {
    writeNodeRegistry(registry);
  }
}
