import type {
  DiscoverySource,
  NodeConnectionStatus,
  NodePresence,
  NodeRecord,
  SavedNodeInfo
} from './types';
import { getNodeDisplayName } from './types';
import {
  NODE_LIST_KEY,
  NODE_META_KEY,
  NODE_REGISTRY_KEY,
  readString,
  writeString,
  readStringList,
  writeStringList,
  getSelectedNodeIp,
  setSelectedNodeIp,
} from '../auth/storage';

// Preserve existing fleet imports while keeping browser persistence in its
// shared boundary. No storage access occurs during module initialization.
export {
  NODE_IP_KEY,
  NODE_LIST_KEY,
  NODE_META_KEY,
  NODE_REGISTRY_KEY,
  readString,
  writeString,
  removeValue,
  readStringList,
  writeStringList,
  getSelectedNodeIp,
  setSelectedNodeIp,
} from '../auth/storage';

export const NODE_REGISTRY_CHANGED_EVENT = 'lumigrid:nodes-changed';

function notifyNodeRegistryChanged(): void {
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new Event(NODE_REGISTRY_CHANGED_EVENT));
  }
}

const DISCOVERY_PRIORITY: Record<DiscoverySource, number> = {
  mdns: 4,
  sync: 3,
  mqtt: 2,
  manual: 1,
};

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

export function normalizeNodeAddress(raw: string): string | null {
  const stripped = raw.trim()
    .replace(/^https?:\/\//i, '')
    .replace(/\/+$/, '');

  if (!stripped || /[/?#\s]/.test(stripped)) return null;

  try {
    const parsed = new URL(`http://${stripped}`);
    if (!parsed.hostname || parsed.username || parsed.password) return null;
    return parsed.host;
  } catch {
    return null;
  }
}

export function readNodeRegistry(): Record<string, NodeRecord> {
  const raw = readString(NODE_REGISTRY_KEY, '');
  if (raw) {
    try {
      const parsed = JSON.parse(raw);
      if (typeof parsed === 'object' && parsed !== null) {
        const records = parsed as Record<string, any>;
        // Migration of schema if older records have `name` but not `deviceName`
        for (const [id, r] of Object.entries(records)) {
          if (!r.deviceName && r.name) {
            r.deviceName = r.name;
          }
          if (!r.presence) {
            r.presence = (Date.now() - (r.lastSeen || 0) < 15000) ? 'online' : 'offline';
          }
        }
        return records as Record<string, NodeRecord>;
      }
    } catch {}
  }

  // Automatic one-time migration from legacy storage
  const legacyIps = readStringList(NODE_LIST_KEY);
  const legacyMeta = readNodeMetadataMap();
  const migrated: Record<string, NodeRecord> = {};

  for (const ip of legacyIps) {
    const id = `node-${ip.replace(/\./g, '-')}`;
    const devName = legacyMeta[ip]?.name || ip;
    migrated[id] = {
      id,
      deviceName: devName,
      name: devName,
      address: ip,
      family: 'led-node',
      model: 'lg-led-v1',
      lastSeen: legacyMeta[ip]?.lastSeen || Date.now(),
      discoverySource: 'manual',
      presence: 'offline',
      connection: 'offline',
    };
  }

  if (Object.keys(migrated).length > 0) {
    writeString(NODE_REGISTRY_KEY, JSON.stringify(migrated));
  }

  return migrated;
}

export function writeNodeRegistry(
  registry: Record<string, NodeRecord>,
  notify = true,
): void {
  writeString(NODE_REGISTRY_KEY, JSON.stringify(registry));
  if (notify) notifyNodeRegistryChanged();
}

function syncLegacyFromRegistry(registry: Record<string, NodeRecord>): void {
  const ips: string[] = [];
  const meta: Record<string, { name?: string; lastSeen?: number }> = {};

  for (const rec of Object.values(registry)) {
    if (rec.address) {
      ips.push(rec.address);
      const displayName = getNodeDisplayName(rec);
      meta[rec.address] = { name: displayName, lastSeen: rec.lastSeen };
    }
  }

  writeStringList(NODE_LIST_KEY, ips);
  writeString(NODE_META_KEY, JSON.stringify(meta));
}

/**
 * Upsert a node record.
 * Invariant: mDNS rediscovery may update deviceName, but it must NEVER overwrite alias.
 */
export function upsertNodeRecord(record: Partial<NodeRecord> & { id: string }): NodeRecord {
  const registry = readNodeRegistry();
  const existing = registry[record.id];

  let effectiveSource = record.discoverySource ?? existing?.discoverySource ?? 'manual';
  if (existing && record.discoverySource) {
    const oldPri = DISCOVERY_PRIORITY[existing.discoverySource] ?? 0;
    const newPri = DISCOVERY_PRIORITY[record.discoverySource] ?? 0;
    if (oldPri > newPri) {
      effectiveSource = existing.discoverySource;
    }
  }

  // If a manual entry with the same address exists under a temporary id, merge into canonical ID
  if (record.address) {
    for (const [key, val] of Object.entries(registry)) {
      if (key !== record.id && val.address === record.address) {
        delete registry[key];
      }
    }
  }

  const devName = record.deviceName ?? record.name ?? existing?.deviceName ?? existing?.name ?? record.address ?? record.id;
  // Local alias survives mDNS deviceName refresh
  const alias = record.alias !== undefined ? (record.alias.trim() || undefined) : existing?.alias;

  const presence: NodePresence = record.presence ?? existing?.presence ?? 'online';

  const updated: NodeRecord = {
    id: record.id,
    deviceName: devName,
    alias,
    name: alias || devName, // backward compat
    address: record.address ?? existing?.address,
    hostname: record.hostname ?? existing?.hostname,
    family: record.family ?? existing?.family ?? 'led-node',
    model: record.model ?? existing?.model ?? 'lg-led-v1',
    firmware: record.firmware ?? existing?.firmware,
    presence,
    lastSeen: record.lastSeen ?? Date.now(),
    discoverySource: effectiveSource,
    connection: record.connection ?? existing?.connection ?? 'offline',
    children: record.children ?? existing?.children ?? [],
  };

  registry[record.id] = updated;
  writeNodeRegistry(registry);
  syncLegacyFromRegistry(registry);

  return updated;
}

export function setNodeLocalAlias(nodeId: string, alias: string): NodeRecord | null {
  const registry = readNodeRegistry();
  const existing = registry[nodeId];
  if (!existing) return null;

  const cleanAlias = alias.trim();
  existing.alias = cleanAlias || undefined;
  existing.name = existing.alias || existing.deviceName || existing.id;

  writeNodeRegistry(registry);
  syncLegacyFromRegistry(registry);
  return existing;
}

export function recordDiscoveredNode(
  node: { id: string; address?: string; deviceName?: string; name?: string; family?: string; model?: string; hostname?: string; firmware?: string },
  source: DiscoverySource = 'sync'
): NodeRecord {
  return upsertNodeRecord({
    id: node.id,
    address: node.address,
    deviceName: node.deviceName ?? node.name,
    hostname: node.hostname,
    family: node.family,
    model: node.model,
    firmware: node.firmware,
    discoverySource: source,
    lastSeen: Date.now(),
    presence: 'online',
  });
}

export function getSavedNodes(): SavedNodeInfo[] {
  const registry = readNodeRegistry();
  const records = Object.values(registry);
  if (records.length > 0) {
    return records
      .filter(r => Boolean(r.address))
      .map(r => ({
        ip: r.address!,
        name: getNodeDisplayName(r),
        lastSeen: r.lastSeen,
        id: r.id,
        family: r.family,
        model: r.model,
        firmware: r.firmware,
        presence: r.presence,
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
  const cleanIp = normalizeNodeAddress(ip);
  if (!cleanIp) return;

  const id = `node-${cleanIp.replace(/\./g, '-')}`;
  upsertNodeRecord({
    id,
    deviceName: name?.trim() || cleanIp,
    address: cleanIp,
    discoverySource: 'manual',
    lastSeen: Date.now(),
  });
}

/** Set local alias or device name for a saved node address */
export function renameSavedNode(ip: string, newAlias: string): void {
  const cleanIp = ip.trim();
  const cleanAlias = newAlias.trim();
  if (!cleanIp) return;

  const registry = readNodeRegistry();
  for (const rec of Object.values(registry)) {
    if (rec.address === cleanIp) {
      setNodeLocalAlias(rec.id, cleanAlias);
      return;
    }
  }

  addSavedNode(cleanIp, cleanAlias);
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
      rec.presence = 'online';
      found = true;
    }
  }
  if (found) {
    writeNodeRegistry(registry, false);
  }
}
