import { apiJson } from './transport';

export interface DeviceProtocolConfig {
  artnetEnabled: boolean;
  sacnEnabled: boolean;
  universeStart: number;
  liveStreamLayer: number;
  mergeMode?: number | string;
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

  // Optional telemetry / wire metadata
  port?: string;
  kind?: 'addressable' | 'pwm' | string;
  hardwareId?: number;
  gpio?: number;
  configured?: boolean;
  addressable?: {
    type: 'WS2812' | 'SK6812' | string;
    pixels: number;
    pixelOrder: string | number;
  };
}

export interface ChannelGroup {
  name: string;
  channelMask: number;
  role: number | string;
  enabled: boolean;
  index?: number;
}

/** Internal normalized model used across UI stores and forms. */
export interface DeviceConfig {
  deviceName: string;
  activePresetIndex: number;
  fallbackPresetIndex: number;
  watchdogTimeoutS: number;
  maxPowerMw: number;
  gammaX10: number;
  softStartEnabled: boolean;
  softStartMs: number;
  timezone?: string;
  mqttEnabled: boolean;
  mqttBroker: string;
  mqttPort: number;
  syncEnabled: boolean;
  syncRole: number;
  syncToken?: number;
  serialNumber?: string;
  hardwareRevision?: string;
  siteLabel?: string;
  groupLabel?: string;
  protocol: DeviceProtocolConfig;
  auth?: DeviceAuthConfig;
  channels?: ChannelConfig[];
  groups?: ChannelGroup[];
  zones?: DeviceZoneConfig[];
  /** Hardware output pin/channel definitions (must be preserved on every save). */
  physicalOutputs?: PhysicalOutput[];
  /** Logical fixture → physical output mappings (must be preserved on every save). */
  fixtures?: FixtureConfig[];

  // Aliases for optional compatibility
  activePreset?: number;
  fallbackPreset?: number;
  gamma?: number;
  softStart?: boolean;
}

export interface ChannelConfigWire extends Partial<ChannelConfig> {
  name: string;
}

/** Wire format as received from or sent to firmware /api/v1/config. */
export interface DeviceConfigWire {
  deviceName: string;
  serialNumber?: string;
  hardwareRevision?: string;
  siteLabel?: string;
  groupLabel?: string;
  activePreset?: number;
  activePresetIndex?: number;
  fallbackPreset?: number;
  fallbackPresetIndex?: number;
  watchdogTimeoutS?: number;
  maxPowerMw?: number;
  gamma?: number;
  gammaX10?: number;
  softStart?: boolean;
  softStartEnabled?: boolean;
  softStartMs?: number;
  timezone?: string;
  syncEnabled?: boolean;
  syncRole?: number;
  syncToken?: number;
  mqttEnabled?: boolean;
  mqttBroker?: string;
  mqttPort?: number;
  protocol?: DeviceProtocolConfig;
  auth?: DeviceAuthConfig;
  channels?: ChannelConfigWire[];
  groups?: ChannelGroup[];
  zones?: DeviceZoneConfig[];
  physicalOutputs?: PhysicalOutput[];
  fixtures?: FixtureConfig[];
}

const PIXEL_ORDER_MAP: Record<string, number> = {
  RGB: 0,
  GRB: 1,
  RGBW: 2,
  GRBW: 3,
};

const PIXEL_ORDER_NAMES = ['RGB', 'GRB', 'RGBW', 'GRBW'];

const ZONE_ROLE_NAMES = [
  'primary',
  'secondary',
  'accent',
  'halo',
  'backdrop',
  'ambient',
  'custom',
  'custom2',
];

/** Normalizes raw wire format into strongly-typed internal DeviceConfig. */
export function normalizeDeviceConfig(wire: DeviceConfigWire): DeviceConfig {
  const activePresetIndex = wire.activePresetIndex ?? wire.activePreset ?? 0;
  const fallbackPresetIndex = wire.fallbackPresetIndex ?? wire.fallbackPreset ?? 0;
  const gammaX10 =
    wire.gammaX10 ??
    (wire.gamma !== undefined ? Math.round(wire.gamma * 10) : 22);
  const softStartEnabled = wire.softStartEnabled ?? wire.softStart ?? true;
  const softStartMs = wire.softStartMs ?? 500;

  // Normalize channels
  const channels: ChannelConfig[] = (wire.channels ?? []).map((ch, idx) => {
    let addressableType = ch.addressableType ?? 0;
    let pixelCount = ch.pixelCount ?? 0;
    let pixelOrder = ch.pixelOrder ?? 1;

    if (ch.addressable) {
      if (typeof ch.addressable.type === 'string') {
        addressableType = ch.addressable.type.toUpperCase() === 'SK6812' ? 1 : 0;
      }
      if (typeof ch.addressable.pixels === 'number') {
        pixelCount = ch.addressable.pixels;
      }
      if (typeof ch.addressable.pixelOrder === 'string') {
        pixelOrder = PIXEL_ORDER_MAP[ch.addressable.pixelOrder.toUpperCase()] ?? 1;
      } else if (typeof ch.addressable.pixelOrder === 'number') {
        pixelOrder = ch.addressable.pixelOrder;
      }
    }

    return {
      ...ch,
      addressableType,
      pixelCount,
      pixelOrder,
      enabled: Boolean(ch.enabled),
      maxBrightness: ch.maxBrightness ?? 255,
      gammaCorrection: ch.gammaCorrection ?? true,
      maxMilliwatts: ch.maxMilliwatts ?? 0,
      dmxUniverse: ch.dmxUniverse ?? 0,
      dmxStartChannel: ch.dmxStartChannel ?? 0,
      dmxMode: ch.dmxMode ?? 0,
    };
  });

  return {
    deviceName: wire.deviceName || 'lumigrid',
    serialNumber: wire.serialNumber ?? '',
    hardwareRevision: wire.hardwareRevision ?? 'v1.0',
    siteLabel: wire.siteLabel ?? '',
    groupLabel: wire.groupLabel ?? '',
    activePresetIndex,
    fallbackPresetIndex,
    watchdogTimeoutS: wire.watchdogTimeoutS ?? 60,
    maxPowerMw: wire.maxPowerMw ?? 0,
    gammaX10,
    softStartEnabled,
    softStartMs,
    timezone: wire.timezone ?? 'UTC0',
    mqttEnabled: Boolean(wire.mqttEnabled),
    mqttBroker: wire.mqttBroker ?? '',
    mqttPort: wire.mqttPort ?? 1883,
    syncEnabled: Boolean(wire.syncEnabled),
    syncRole: wire.syncRole ?? 0,
    syncToken: wire.syncToken,
    protocol: wire.protocol ?? {
      artnetEnabled: false,
      sacnEnabled: false,
      universeStart: 1,
      liveStreamLayer: 2,
    },
    auth: wire.auth,
    channels: channels.length > 0 ? channels : undefined,
    groups: wire.groups,
    zones: wire.zones,
    physicalOutputs: wire.physicalOutputs,
    fixtures: wire.fixtures,
    // Provide aliases for convenience
    activePreset: activePresetIndex,
    fallbackPreset: fallbackPresetIndex,
    gamma: Number((gammaX10 / 10).toFixed(1)),
    softStart: softStartEnabled,
  };
}

/** Serializes internal DeviceConfig into canonical public wire contract for PUT /api/v1/config. */
export function serializeDeviceConfig(config: DeviceConfig): Record<string, unknown> {
  const activePreset = config.activePresetIndex ?? config.activePreset ?? 0;
  const fallbackPreset = config.fallbackPresetIndex ?? config.fallbackPreset ?? 0;
  const gamma =
    config.gamma !== undefined
      ? config.gamma
      : (config.gammaX10 !== undefined ? config.gammaX10 / 10 : 2.2);
  const softStart = config.softStart !== undefined ? config.softStart : config.softStartEnabled;

  const payload: Record<string, unknown> = {
    deviceName: config.deviceName,
    activePreset,
    fallbackPreset,
    watchdogTimeoutS: config.watchdogTimeoutS,
    maxPowerMw: config.maxPowerMw,
    gamma: Number(gamma.toFixed(1)),
    softStart: Boolean(softStart),
    softStartMs: config.softStartMs,
  };

  if (config.serialNumber !== undefined) payload.serialNumber = config.serialNumber;
  if (config.hardwareRevision !== undefined) payload.hardwareRevision = config.hardwareRevision;
  if (config.siteLabel !== undefined) payload.siteLabel = config.siteLabel;
  if (config.groupLabel !== undefined) payload.groupLabel = config.groupLabel;
  if (config.timezone !== undefined) payload.timezone = config.timezone;
  if (config.syncEnabled !== undefined) payload.syncEnabled = config.syncEnabled;
  if (config.syncRole !== undefined) payload.syncRole = config.syncRole;
  if (config.syncToken !== undefined && config.syncToken !== null && config.syncToken !== 0) {
    payload.syncToken = config.syncToken;
  }
  if (config.mqttEnabled !== undefined) payload.mqttEnabled = config.mqttEnabled;
  if (config.mqttBroker !== undefined) payload.mqttBroker = config.mqttBroker;
  if (config.mqttPort !== undefined) payload.mqttPort = config.mqttPort;
  if (config.protocol !== undefined) payload.protocol = config.protocol;

  if (config.auth) {
    const authCopy: DeviceAuthConfig = {};
    if (config.auth.adminToken?.trim()) authCopy.adminToken = config.auth.adminToken.trim();
    if (config.auth.installerToken?.trim()) authCopy.installerToken = config.auth.installerToken.trim();
    if (config.auth.operatorToken?.trim()) authCopy.operatorToken = config.auth.operatorToken.trim();
    if (Object.keys(authCopy).length > 0) {
      payload.auth = authCopy;
    }
  }

  if (config.channels) {
    payload.channels = config.channels.map((ch, idx) => {
      const channelPayload: Record<string, unknown> = {
        name: ch.name ?? '',
        enabled: Boolean(ch.enabled),
        maxBrightness: ch.maxBrightness ?? 255,
        gammaCorrection: ch.gammaCorrection ?? true,
        maxMilliwatts: ch.maxMilliwatts ?? 0,
        dmxUniverse: ch.dmxUniverse ?? 0,
        dmxStartChannel: ch.dmxStartChannel ?? 0,
        dmxMode: ch.dmxMode ?? 0,
      };

      // Addressable parameters for A1–A8 (indices 0–7)
      if (idx < 8) {
        const typeStr =
          ch.addressable?.type || (ch.addressableType === 1 ? 'SK6812' : 'WS2812');
        const pixels = ch.addressable?.pixels ?? ch.pixelCount ?? 0;
        let pixelOrder = ch.addressable?.pixelOrder;
        if (pixelOrder === undefined || typeof pixelOrder === 'number') {
          pixelOrder = PIXEL_ORDER_NAMES[ch.pixelOrder ?? 1] ?? 'GRB';
        }
        channelPayload.addressable = {
          type: typeStr,
          pixels,
          pixelOrder,
        };
      }

      return channelPayload;
    });
  }

  if (config.groups) {
    payload.groups = config.groups.map((g) => {
      let roleStr = 'primary';
      if (typeof g.role === 'string') {
        roleStr = g.role;
      } else if (typeof g.role === 'number') {
        roleStr = ZONE_ROLE_NAMES[g.role] ?? 'primary';
      }
      return {
        name: g.name ?? '',
        channelMask: g.channelMask ?? 0,
        role: roleStr,
        enabled: Boolean(g.enabled),
      };
    });
  }

  // Preserve legacy/diagnostic fields if present
  if (config.physicalOutputs) payload.physicalOutputs = config.physicalOutputs;
  if (config.fixtures) payload.fixtures = config.fixtures;
  if (config.zones) payload.zones = config.zones;

  return payload;
}

export async function getDeviceConfig(ip: string): Promise<DeviceConfig> {
  const wire = await apiJson<DeviceConfigWire>(ip, '/api/v1/config');
  return normalizeDeviceConfig(wire);
}

export function saveDeviceConfig(ip: string, config: DeviceConfig): Promise<{ saved: true }> {
  const payload = serializeDeviceConfig(config);
  return apiJson<{ saved: true }>(ip, '/api/v1/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
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

export type NetworkMode = 'ap_only' | 'sta_only' | 'sta_with_ap_fallback';

export type NetworkApplyState = 'idle' | 'applying' | 'waiting_for_network' | 'rolling_back' | 'failed';

export interface NetworkConfigWire {
  mode: NetworkMode;
  ssid: string;
  passwordSet: boolean;
  apName?: string;
  apChannel?: number;
}

export interface NetworkConfigUpdate {
  mode?: NetworkMode;
  ssid?: string;
  password?: string;
  clearPassword?: boolean;
  apName?: string;
  apChannel?: number;
}

export interface NetworkRuntimeStatus {
  state: string; // 'connected' | 'connecting' | 'fallback_ap' | 'ap_active' | 'idle' | 'failed'
  mode: NetworkMode;
  staConnected: boolean;
  staSsid: string;
  staRssi: number;
  staIp: string;
  apActive: boolean;
  apIp: string;
  retryCount: number;
  applyState: NetworkApplyState;
  pendingCommandId: number | null;
  lastApplyError: string | null;
  lastDisconnectReason: number;
  persistedMatchesRuntime: boolean;
}

export function getNetworkConfig(ip: string): Promise<NetworkConfigWire> {
  return apiJson<NetworkConfigWire>(ip, '/api/v1/network/config');
}

export function saveNetworkConfig(
  ip: string,
  update: NetworkConfigUpdate
): Promise<{ accepted: boolean; commandId?: number; applyPending: boolean }> {
  return apiJson<{ accepted: boolean; commandId?: number; applyPending: boolean }>(ip, '/api/v1/network/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(update),
  });
}

export function getNetworkStatus(ip: string): Promise<NetworkRuntimeStatus> {
  return apiJson<NetworkRuntimeStatus>(ip, '/api/v1/network/status');
}


