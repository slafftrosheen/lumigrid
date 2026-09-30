import { writable, derived, get } from 'svelte/store';
import type { NodeRecord, DiscoverySource, NodePresence } from './types';
import { getNodeDisplayName } from './types';
import {
  readNodeRegistry,
  writeNodeRegistry,
  upsertNodeRecord,
  setNodeLocalAlias,
  recordDiscoveredNode,
  removeSavedNode,
  addSavedNode,
  getSelectedNodeIp,
  setSelectedNodeIp,
  NODE_REGISTRY_CHANGED_EVENT,
} from './registry';

const initialRegistry = readNodeRegistry();

/** Primary store containing all known fleet nodes indexed by immutable node ID */
export const fleetRegistry = writable<Record<string, NodeRecord>>(initialRegistry);

// Synchronize store when registry changes in localStorage or other tabs
if (typeof window !== 'undefined') {
  window.addEventListener(NODE_REGISTRY_CHANGED_EVENT, () => {
    fleetRegistry.set(readNodeRegistry());
  });

  window.addEventListener('storage', (e) => {
    if (e.key === 'lg_nodes_v3') {
      fleetRegistry.set(readNodeRegistry());
    }
  });
}

/** Array of all fleet nodes sorted by presence (online first), then display name */
export const fleetNodes = derived(fleetRegistry, ($registry) => {
  const list = Object.values($registry);
  return list.sort((a, b) => {
    const aOnline = a.presence === 'online' ? 0 : 1;
    const bOnline = b.presence === 'online' ? 0 : 1;
    if (aOnline !== bOnline) return aOnline - bOnline;

    const nameA = getNodeDisplayName(a).toLowerCase();
    const nameB = getNodeDisplayName(b).toLowerCase();
    return nameA.localeCompare(nameB);
  });
});

/** Array of only online fleet nodes */
export const onlineNodes = derived(fleetNodes, ($nodes) => {
  return $nodes.filter((n) => n.presence === 'online');
});

/** Currently selected node IP/address store */
export const selectedNodeIp = writable<string>(getSelectedNodeIp());

selectedNodeIp.subscribe((val) => {
  setSelectedNodeIp(val);
});

export const fleetStore = {
  subscribe: fleetRegistry.subscribe,

  /** Reload from local storage */
  refresh(): void {
    fleetRegistry.set(readNodeRegistry());
  },

  /** Update or register a node */
  upsert(record: Partial<NodeRecord> & { id: string }): NodeRecord {
    const updated = upsertNodeRecord(record);
    fleetRegistry.set(readNodeRegistry());
    return updated;
  },

  /** Record a discovered node from mDNS or sync */
  recordDiscovered(
    node: { id: string; address?: string; deviceName?: string; name?: string; family?: string; model?: string; hostname?: string; firmware?: string },
    source: DiscoverySource = 'sync'
  ): NodeRecord {
    const updated = recordDiscoveredNode(node, source);
    fleetRegistry.set(readNodeRegistry());
    return updated;
  },

  /** Set local browser alias for a node */
  setAlias(nodeId: string, alias: string): NodeRecord | null {
    const updated = setNodeLocalAlias(nodeId, alias);
    fleetRegistry.set(readNodeRegistry());
    return updated;
  },

  /** Add manual node route */
  addManualNode(address: string, name?: string): void {
    addSavedNode(address, name);
    fleetRegistry.set(readNodeRegistry());
  },

  /** Remove node from registry */
  removeNode(addressOrId: string): void {
    removeSavedNode(addressOrId);
    fleetRegistry.set(readNodeRegistry());
  },

  /** Select active node */
  selectNode(address: string): void {
    selectedNodeIp.set(address.trim());
  },
};
