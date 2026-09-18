import { apiJson } from './transport';

export interface DeviceProtocolConfig {
  artnetEnabled: boolean;
  sacnEnabled: boolean;
  universeStart: number;
  liveStreamLayer: number;
  mergeMode?: number;
  streamTimeoutMs?: number;
  zoneStartChannel?: number;
}

export interface DeviceAuthConfig {
  adminToken?: string;
  installerToken?: string;
  operatorToken?: string;
}

export interface DeviceZoneConfig {
  name: string;
  role: string;
}

/** Mirrors firmware PhysicalOutput — a single hardware output pin/channel. */
export interface PhysicalOutput {
  /** 0=RMT, 1=PWM, 2=SPI */
  type: number;
  /** RMT lane 0-7, PWM channel 0-7, etc. */
  hardwareId: number;
  gpioPin: number;
  enabled: boolean;
}

/** Mirrors firmware FixtureConfig — a logical fixture mapped to physical outputs. */
export interface FixtureConfig {
  /** 0=WS2812, 1=SK6812, 2=PWM_Single, 3=PWM_RGB, 4=PWM_RGBW */
  profile: number;
  /** 0=RGB, 1=GRB, 2=RGBW, 3=GRBW */
  pixelOrder: number;
  /** Indices into physical_outputs (1-4 entries depending on profile) */
  physicalOutputIndex: number[];
  zoneId: number;
  numPixels: number;
  maxBrightness: number;
  whiteBalance: [number, number, number];
  gammaCorrection: boolean;
  maxMilliwatts: number;
  dmxUniverse: number;
  dmxStartChannel: number;
  dmxMode: number;
}

export interface ChannelConfig {
  name: string;
  enabled: boolean;
  addressableType: number;
  pixelOrder: number;
  pixelCount: number;
  maxBrightness: number;
  gammaCorrection: boolean;
  maxMilliwatts: number;
  dmxUniverse: number;
  dmxStartChannel: number;
  dmxMode: number;
}

export interface ChannelGroup {
  name: string;
  channelMask: number;
  role: number;
  enabled: boolean;
}

export interface DeviceConfig {
  deviceName: string;
  activePresetIndex: number;
  fallbackPresetIndex: number;
  watchdogTimeoutS: number;
  maxPowerMw: number;
  gammaX10: number;
  softStartEnabled: boolean;
  softStartMs: number;
  mqttEnabled: boolean;
  mqttBroker: string;
  mqttPort: number;
  syncEnabled: boolean;
  syncRole: number;
  protocol: DeviceProtocolConfig;
  auth?: DeviceAuthConfig;
  channels?: ChannelConfig[];
  groups?: ChannelGroup[];
  zones?: DeviceZoneConfig[];
  /** Hardware output pin/channel definitions (must be preserved on every save). */
  physicalOutputs?: PhysicalOutput[];
  /** Logical fixture → physical output mappings (must be preserved on every save). */
  fixtures?: FixtureConfig[];
}


export function getDeviceConfig(ip: string): Promise<DeviceConfig> {
  return apiJson<DeviceConfig>(ip, '/api/v1/config');
}

export function saveDeviceConfig(ip: string, config: DeviceConfig): Promise<{ saved: true }> {
  const sanitized = { ...config };
  if (sanitized.auth) {
    const authCopy: DeviceAuthConfig = {};
    if (sanitized.auth.adminToken?.trim()) authCopy.adminToken = sanitized.auth.adminToken.trim();
    if (sanitized.auth.installerToken?.trim()) authCopy.installerToken = sanitized.auth.installerToken.trim();
    if (sanitized.auth.operatorToken?.trim()) authCopy.operatorToken = sanitized.auth.operatorToken.trim();
    if (Object.keys(authCopy).length > 0) {
      sanitized.auth = authCopy;
    } else {
      delete sanitized.auth;
    }
  }
  return apiJson<{ saved: true }>(ip, '/api/v1/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(sanitized),
  });
}

export function getBackup(ip: string): Promise<Record<string, unknown>> {
  return apiJson<Record<string, unknown>>(ip, '/api/v1/backup');
}

export function restoreBackup(ip: string, payload: Record<string, unknown>): Promise<{ restored: true }> {
  return apiJson<{ restored: true }>(ip, '/api/v1/restore', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
}
