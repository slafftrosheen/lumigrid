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

export const NODE_META_KEY = 'lg_nodes_meta';
export const NODE_REGISTRY_KEY = 'lg_nodes_v3';

function storage(scope: 'local' | 'session' = 'local'): Storage | null {
  // Accessing the storage property itself can throw when browser persistence is
  // disabled. Keep both that lookup and storage operations inside this boundary.
  try {
    if (typeof window !== 'undefined') {
      const browserStore = scope === 'session' ? window.sessionStorage : window.localStorage;
      if (browserStore) return browserStore;
    }
    return scope === 'session'
      ? (typeof sessionStorage === 'undefined' ? null : sessionStorage)
      : (typeof localStorage === 'undefined' ? null : localStorage);
  } catch {
    return null;
  }
}

export function readString(key: string, fallback = ''): string {
  try { return storage()?.getItem(key) ?? fallback; } catch { return fallback; }
}

export function writeString(key: string, value: string): void {
  try { storage()?.setItem(key, value); } catch {}
}

export function removeValue(key: string): void {
  try { storage()?.removeItem(key); } catch {}
}

/** Session credentials must remain scoped to the tab, never persistent storage. */
export function readSessionString(key: string, fallback = ''): string {
  try { return storage('session')?.getItem(key) ?? fallback; } catch { return fallback; }
}

export function writeSessionString(key: string, value: string): void {
  try { storage('session')?.setItem(key, value); } catch {}
}

export function removeSessionValue(key: string): void {
  try { storage('session')?.removeItem(key); } catch {}
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

// ── Session Management (Node-authoritative per LG-029) ───────────────────────

import { nodeAuthStore } from './nodeAuth';

/**
 * Atomically lock/clear all local dashboard sessions.
 */
export function lockDashboardSession(): void {
  removeValue(API_TOKEN_KEY);
  nodeAuthStore.clearAllSessions();
}

// ── Fleet Node & Topology Model (Control Plane v3) ───────────────────────────
// Moved to src/lib/fleet per LG-031 Requirement 0.4.
// Re-exported here for temporary backward compatibility.

export type {
  DiscoverySource,
  NodeConnectionStatus,
  NodePresence,
  NodeChild,
  NodeRecord,
  SavedNodeInfo,
} from '../fleet/types';
export { getNodeDisplayName } from '../fleet/types';

export {
  NODE_REGISTRY_CHANGED_EVENT,
  readNodeMetadataMap,
  writeNodeMetadataMap,
  normalizeNodeAddress,
  readNodeRegistry,
  writeNodeRegistry,
  upsertNodeRecord,
  setNodeLocalAlias,
  recordDiscoveredNode,
  getSavedNodes,
  addSavedNode,
  renameSavedNode,
  removeSavedNode,
  updateNodeLastSeen,
} from '../fleet/registry';

