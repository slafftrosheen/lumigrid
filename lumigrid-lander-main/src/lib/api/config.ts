import { apiJson } from './transport';

export interface DeviceProtocolConfig {
  artnetEnabled: boolean;
  sacnEnabled: boolean;
  universeStart: number;
  liveStreamLayer: number;
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
  zones?: DeviceZoneConfig[];
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
