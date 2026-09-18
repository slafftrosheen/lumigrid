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
  };
}

export const capabilities = writable<CapabilitiesManifest | null>(null);
export const caps = writable<Caps | null>(null);
export const capsError = writable<string | null>(null);

export async function loadCaps(ipOrUrl: string = ''): Promise<void> {
  try {
    let manifest: CapabilitiesManifest;
    if (ipOrUrl.startsWith('http://') || ipOrUrl.startsWith('https://') || ipOrUrl.startsWith('/')) {
      const response = await fetch(ipOrUrl, { mode: 'cors' });
      if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
      manifest = await response.json();
    } else {
      manifest = await getCapabilities(ipOrUrl);
    }

    capabilities.set(manifest);
    // Project into legacy Caps structure for UI backward compatibility
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
      },
    });
    capsError.set(null);
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    capabilities.set(null);
    caps.set(null);
    capsError.set(`Capabilities unavailable: ${message}`);
  }
}
