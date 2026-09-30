export type DiscoverySource = 'mdns' | 'sync' | 'mqtt' | 'manual';

export type NodeConnectionStatus =
  | 'offline'
  | 'discovering'
  | 'connecting'
  | 'online'
  | 'degraded';

export type NodePresence = 'online' | 'stale' | 'offline';

export interface NodeChild {
  id: string;
  parentId: string;
  kind: 'zone' | 'output' | 'sensor';
  name: string;
}

export interface NodeRecord {
  id: string;

  deviceName: string;
  alias?: string;

  address?: string;
  hostname?: string;

  family: string;
  model: string;
  firmware?: string;

  lastSeen: number;
  discoverySource: DiscoverySource;
  presence: NodePresence;
  connection?: NodeConnectionStatus;
  children?: NodeChild[];

  /** @deprecated backward-compat alias mapping to alias || deviceName */
  name?: string;
}

export interface SavedNodeInfo {
  ip: string;
  name: string;
  lastSeen?: number;
  id?: string;
  family?: string;
  model?: string;
  firmware?: string;
  presence?: NodePresence;
}

/**
 * Canonical display name helper:
 * Display: alias || deviceName || id
 */
export function getNodeDisplayName(node: { alias?: string; deviceName?: string; name?: string; id: string }): string {
  return (node.alias && node.alias.trim()) ||
         (node.deviceName && node.deviceName.trim()) ||
         (node.name && node.name.trim()) ||
         node.id;
}
