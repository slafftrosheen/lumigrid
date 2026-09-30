import { writable } from 'svelte/store';
import { getCapabilities, type CapabilitiesManifest } from '../api/node';

export interface Caps {
  device: string;
  maker: string;
  fw: string;
  features: {
    zones: boolean;
    scenes: boolean;
    schedule: boolean;
    sync: boolean;
    pixels: boolean;
    white_pwm: boolean;
    blend_control: boolean;
    web_app: boolean;
    failsafe_resume: boolean;
    sacn?: boolean;
    artnet?: boolean;
    ota?: boolean;
    factory?: boolean;
    scene_motion?: boolean;
    cue_lists?: boolean;
    transition_engine_v2?: boolean;
  };
}

export const capabilities = writable<CapabilitiesManifest | null>(null);
export const caps = writable<Caps | null>(null);
export const capsError = writable<string | null>(null);

/** Per-node cache keyed by nodeId:firmwareVersion */
const capsCache = new Map<string, CapabilitiesManifest>();
/** Maps nodeId to latest cached firmwareVersion to detect invalidation */
const nodeFirmwareMap = new Map<string, string>();

export function clearCapabilitiesCache(): void {
  capsCache.clear();
  nodeFirmwareMap.clear();
}

export function invalidateCapabilitiesCache(nodeId?: string): void {
  if (nodeId) {
    const fw = nodeFirmwareMap.get(nodeId);
    if (fw) {
      capsCache.delete(`${nodeId}:${fw}`);
    }
    nodeFirmwareMap.delete(nodeId);
  } else {
    clearCapabilitiesCache();
  }
}

export function getCachedCapabilities(nodeId: string, firmwareVersion: string): CapabilitiesManifest | null {
  return capsCache.get(`${nodeId}:${firmwareVersion}`) ?? null;
}

function projectCaps(manifest: CapabilitiesManifest): void {
  capabilities.set(manifest);
  caps.set({
    device: manifest.node.model,
    maker: manifest.node.family,
    fw: manifest.node.firmware,
    features: {
      zones: manifest.limits.zones > 0,
      scenes: manifest.limits.presets > 0,
      schedule: manifest.limits.timelineSteps > 0,
      sync: manifest.features.sync,
      pixels: manifest.features.addressable,
      white_pwm: manifest.features.pwm,
      blend_control: true,
      web_app: true,
      failsafe_resume: true,
      sacn: manifest.features.sacn,
      artnet: manifest.features.artnet,
      ota: manifest.features.ota,
      factory: manifest.features.factory,
      scene_motion: manifest.features.sceneMotion ?? false,
      cue_lists: manifest.features.cueLists ?? false,
      transition_engine_v2: manifest.features.transitionEngineV2 ?? false,
    },
  });
  capsError.set(null);
}

export async function loadCaps(ipOrUrl: string = '', nodeId?: string, currentFwVersion?: string): Promise<void> {
  const targetId = nodeId || ipOrUrl;

  // Check cache if nodeId and currentFwVersion provided
  if (targetId && currentFwVersion) {
    const cached = capsCache.get(`${targetId}:${currentFwVersion}`);
    if (cached) {
      projectCaps(cached);
      return;
    }
  }

  try {
    let manifest: CapabilitiesManifest;
    if (ipOrUrl.startsWith('http://') || ipOrUrl.startsWith('https://') || ipOrUrl.startsWith('/')) {
      const response = await fetch(ipOrUrl, { mode: 'cors' });
      if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
      manifest = await response.json();
    } else {
      manifest = await getCapabilities(ipOrUrl);
    }

    const effectiveId = targetId || manifest.node.model;
    const fw = manifest.node.firmware;

    // Check if firmware changed for this node, invalidating previous cache
    const prevFw = nodeFirmwareMap.get(effectiveId);
    if (prevFw && prevFw !== fw) {
      capsCache.delete(`${effectiveId}:${prevFw}`);
    }

    nodeFirmwareMap.set(effectiveId, fw);
    capsCache.set(`${effectiveId}:${fw}`, manifest);

    projectCaps(manifest);
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    capabilities.set(null);
    caps.set(null);
    capsError.set(`Capabilities unavailable: ${message}`);
  }
}
