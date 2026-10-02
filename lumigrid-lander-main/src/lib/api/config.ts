import { apiJson } from './transport';

// ── Hardware limits (firmware-authoritative, never hard-code in UI) ─────────

export interface DeviceConfigLimits {
  maxPowerMw: number;
  maxPixelsPerChannel: number;
  maxGroups: number;
  maxChannels: number;
  maxDmxUniverse: number;
}

// ── Sub-config types ──────────────────────────────────────────────────────────

export interface DeviceProtocolConfig {
  artnetEnabled: boolean;
  sacnEnabled: boolean;
  universeStart: number;
  liveStreamLayer: number;
  mergeMode?: number | string;
  streamTimeoutMs?: number;
  zoneStartChannel?: number;
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

// ── Impact of a config apply (from firmware, not inferred in UI) ──────────────

export interface ConfigApplyImpact {
  hotApply: boolean;
  outputRestart: boolean;
  dmxRestart: boolean;
  syncRestart: boolean;
  schedulerRestart: boolean;
  mqttRestart: boolean;
  rebootRequired: boolean;
}

export interface ConfigValidationResponse {
  valid: boolean;
  currentChecksum: number;
  impact: ConfigApplyImpact;
  warnings: string[];
}

// ── Config save response ──────────────────────────────────────────────────────

export interface SaveDeviceConfigResponse {
  accepted: boolean;
  saved: boolean;
  commandId: number;
  checksum: number;
  apply: string;
}

// ── Conflict error ────────────────────────────────────────────────────────────

/**
 * Thrown when PUT /api/v1/config returns 409 config_conflict.
 * The UI must surface this explicitly — do NOT translate to a generic "Save failed".
 */
export class ConfigConflictError extends Error {
  constructor(public readonly currentChecksum: number) {
    super(`config_conflict: configuration changed on the node since you opened it (currentChecksum=${currentChecksum})`);
    this.name = 'ConfigConflictError';
  }
}

// ── DeviceConfig (internal normalized model) ──────────────────────────────────

/**
 * Internal normalized model used across UI stores and forms.
 *
 * schema, checksum, limits, syncTokenSet:  read-only metadata from firmware.
 * pendingSyncToken:  write-only; supply to set a new token (never displayed).
 * clearSyncToken:    write-only; supply true to remove the existing token.
 * syncToken field is deliberately absent — GET never returns the secret.
 */
export interface DeviceConfig {
  /** Config schema version from firmware. Survives normalization. */
  schema: number;
  /** CRC32 checksum of the persisted config blob. Survives normalization. */
  checksum: number;
  /** Firmware-authoritative hardware limits. Never hard-code these in UI. */
  limits: DeviceConfigLimits;

  deviceName: string;
  activePresetIndex: number;
  fallbackPresetIndex: number;
  watchdogTimeoutS: number;
  /** Use config.limits.maxPowerMw for the allowed maximum. */
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
  /** True if a sync token is currently set on the node. Never the token value. */
  syncTokenSet: boolean;
  /** Write-only: supply to set a new sync token. Never read back. */
  pendingSyncToken?: number;
  /** Write-only: supply true to explicitly clear the existing sync token. */
  clearSyncToken?: boolean;
  serialNumber?: string;
  hardwareRevision?: string;
  siteLabel?: string;
  groupLabel?: string;
  protocol: DeviceProtocolConfig;
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

// ── Wire format ───────────────────────────────────────────────────────────────

export interface ChannelConfigWire extends Partial<ChannelConfig> {
  name: string;
}

/** Wire format as received from firmware GET /api/v1/config. */
export interface DeviceConfigWire {
  /** Config schema version. */
  schema?: number;
  /** CRC32 checksum of the persisted config. */
  checksum?: number;
  /** Hardware limits provided by firmware. */
  limits?: Partial<DeviceConfigLimits>;

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
  /** Firmware returns a boolean indicating whether a token is set, never the token value. */
  syncTokenSet?: boolean;
  mqttEnabled?: boolean;
  mqttBroker?: string;
  mqttPort?: number;
  protocol?: DeviceProtocolConfig;
  channels?: ChannelConfigWire[];
  groups?: ChannelGroup[];
  zones?: DeviceZoneConfig[];
  physicalOutputs?: PhysicalOutput[];
  fixtures?: FixtureConfig[];
}

// ── Normalization ─────────────────────────────────────────────────────────────

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

/**
 * Normalizes raw wire format into strongly-typed internal DeviceConfig.
 *
 * Preserves: schema, checksum, limits, syncTokenSet.
 * Does NOT discard firmware-authoritative metadata.
 */
export function normalizeDeviceConfig(wire: DeviceConfigWire): DeviceConfig {
  const activePresetIndex = wire.activePresetIndex ?? wire.activePreset ?? 0;
  const fallbackPresetIndex = wire.fallbackPresetIndex ?? wire.fallbackPreset ?? 0;
  const gammaX10 =
    wire.gammaX10 ??
    (wire.gamma !== undefined ? Math.round(wire.gamma * 10) : 22);
  const softStartEnabled = wire.softStartEnabled ?? wire.softStart ?? true;
  const softStartMs = wire.softStartMs ?? 500;

  // Firmware-authoritative limits — preserve exactly as received.
  const limits: DeviceConfigLimits = {
    maxPowerMw: wire.limits?.maxPowerMw ?? 0,
    maxPixelsPerChannel: wire.limits?.maxPixelsPerChannel ?? 0,
    maxGroups: wire.limits?.maxGroups ?? 0,
    maxChannels: wire.limits?.maxChannels ?? 0,
    maxDmxUniverse: wire.limits?.maxDmxUniverse ?? 0,
  };

  // Normalize channels
  const channels: ChannelConfig[] = (wire.channels ?? []).map((ch) => {
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
    // Firmware-authoritative metadata — MUST survive normalization
    schema: wire.schema ?? 1,
    checksum: wire.checksum ?? 0,
    limits,
    syncTokenSet: wire.syncTokenSet ?? false,

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
    protocol: wire.protocol ?? {
      artnetEnabled: false,
      sacnEnabled: false,
      universeStart: 1,
      liveStreamLayer: 2,
    },
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

// ── Serialization ─────────────────────────────────────────────────────────────

/**
 * Serializes editable DeviceConfig content for PUT /api/v1/config.
 *
 * Does NOT include: checksum, limits, syncTokenSet.
 * These are not ordinary configuration fields.
 * The save wrapper adds expectedChecksum separately.
 *
 * Sync token write-only handling:
 *   pendingSyncToken present  → include as syncToken in payload to set a new one
 *   clearSyncToken true       → include clearSyncToken: true to erase
 *   neither                   → omit sync token entirely (preserves existing)
 */
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

  // Write-only sync token handling — never echoed from GET
  if (config.clearSyncToken === true) {
    payload.clearSyncToken = true;
  } else if (config.pendingSyncToken !== undefined && config.pendingSyncToken !== 0) {
    // Send as syncToken key so firmware recognizes it
    payload.syncToken = config.pendingSyncToken;
  }
  // If neither clearSyncToken nor pendingSyncToken: omit entirely (firmware preserves existing)

  if (config.mqttEnabled !== undefined) payload.mqttEnabled = config.mqttEnabled;
  if (config.mqttBroker !== undefined) payload.mqttBroker = config.mqttBroker;
  if (config.mqttPort !== undefined) payload.mqttPort = config.mqttPort;
  if (config.protocol !== undefined) payload.protocol = config.protocol;

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

  // Preserve hardware pin/fixture mappings
  if (config.physicalOutputs) payload.physicalOutputs = config.physicalOutputs;
  if (config.fixtures) payload.fixtures = config.fixtures;
  if (config.zones) payload.zones = config.zones;

  return payload;
}

// ── API functions ─────────────────────────────────────────────────────────────

export async function getDeviceConfig(ip: string): Promise<DeviceConfig> {
  const wire = await apiJson<DeviceConfigWire>(ip, '/api/v1/config');
  return normalizeDeviceConfig(wire);
}

/**
 * Validates a candidate config against POST /api/v1/config/validate.
 * No write occurs. Returns the impact plan from firmware.
 *
 * @param expectedChecksum The checksum from the currently loaded config.
 */
export async function validateDeviceConfig(
  ip: string,
  config: DeviceConfig,
  expectedChecksum: number,
): Promise<ConfigValidationResponse> {
  const configPayload = serializeDeviceConfig(config);
  const body = { ...configPayload, expectedChecksum };
  return apiJson<ConfigValidationResponse>(ip, '/api/v1/config/validate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
}

/**
 * Saves config via PUT /api/v1/config with expectedChecksum for conflict detection.
 *
 * Throws ConfigConflictError on 409 config_conflict — the caller must surface
 * explicit UI for this case. Do NOT translate to a generic "Save failed".
 */
export async function saveDeviceConfig(
  ip: string,
  config: DeviceConfig,
  expectedChecksum: number,
): Promise<SaveDeviceConfigResponse> {
  const configPayload = serializeDeviceConfig(config);
  const body = { ...configPayload, expectedChecksum };

  let response: Response;
  try {
    const { apiFetch } = await import('./transport');
    response = await apiFetch(ip, '/api/v1/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    throw new Error(`[${ip}] /api/v1/config → ${msg}`);
  }

  if (response.status === 409) {
    let currentChecksum = 0;
    try {
      const errBody = await response.clone().json() as { currentChecksum?: number };
      currentChecksum = errBody.currentChecksum ?? 0;
    } catch { /* fallback to 0 */ }
    throw new ConfigConflictError(currentChecksum);
  }

  if (!response.ok) {
    let detail = response.statusText;
    try {
      const errBody = await response.clone().json() as { error?: string };
      if (errBody?.error) detail = errBody.error;
    } catch { /* keep status text */ }
    throw new Error(`[${ip}] /api/v1/config → HTTP ${response.status}: ${detail}`);
  }

  return response.json() as Promise<SaveDeviceConfigResponse>;
}

// ── Network config (stays here — logically part of device config) ─────────────

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
