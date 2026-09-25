import { apiJson } from './transport';

export interface DiscoveredFleetNode {
  id: string;
  name: string;
  hostname: string;
  ip: string;
  port: number;
  family: string;
  model: string;
  firmware: string;
  lastSeenMs: number;
  ageMs: number;
  status: 'online' | 'stale' | 'offline';
}

export interface NodeSelfIdentity {
  id: string;
  name: string;
  hostname: string;
  family: string;
  model: string;
  firmware: string;
  hardwareRevision: string;
}

export interface FleetNodesResponse {
  self: NodeSelfIdentity;
  nodes: DiscoveredFleetNode[];
}

/**
 * Fetch fleet nodes discovered via mDNS by the connected LumiGrid node.
 * Bounded to 32 nodes max on the firmware side.
 */
export async function getFleetNodes(
  ip: string,
  apiKey?: string,
  timeoutMs = 5000,
): Promise<FleetNodesResponse> {
  return await apiJson<FleetNodesResponse>(ip, '/api/v1/fleet/nodes', { apiKey }, timeoutMs);
}
