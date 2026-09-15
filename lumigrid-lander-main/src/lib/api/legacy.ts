import type { ApiNode, ChannelConfig, NodeInfo, NodeState } from './node';

export function getInfoLegacy(ip: string, fetcher: <T>(ip: string, path: string) => Promise<T>): Promise<NodeInfo> {
  return fetcher<NodeInfo>(ip, '/json/info');
}

export function getStateLegacy(ip: string, fetcher: <T>(ip: string, path: string) => Promise<T>): Promise<NodeState> {
  return fetcher<NodeState>(ip, '/json/state');
}

export function getNodesLegacy(ip: string, fetcher: <T>(ip: string, path: string) => Promise<T>): Promise<{ nodes: ApiNode[] }> {
  return fetcher<{ nodes: ApiNode[] }>(ip, '/json/nodes');
}

export async function getChannelConfigLegacy(
  ip: string,
  fetcher: <T>(ip: string, path: string) => Promise<T>,
): Promise<ChannelConfig | null> {
  try {
    const cfg = await fetcher<{ um?: { Lumigrid_PCA9685?: ChannelConfig } }>(ip, '/json/cfg');
    return cfg?.um?.Lumigrid_PCA9685 ?? null;
  } catch {
    return null;
  }
}

export async function getScheduleLegacy(
  ip: string,
  fetcher: (ip: string, path: string) => Promise<Response>,
): Promise<unknown[] | null> {
  try {
    const res = await fetcher(ip, '/animations.json');
    if (!res.ok) return null;
    const j = await res.json() as { animations?: unknown[] };
    return j.animations ?? [];
  } catch {
    return null;
  }
}
