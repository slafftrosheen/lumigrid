import { apiJson, apiFetch } from './transport';

export interface HealthSystem {
  uptime_s: number;
  free_heap: number;
  min_free_heap: number;
  reset_reason: number;
  fps: number;
}

export interface HealthNetwork {
  wifi_rssi: number;
}

export interface HealthNvs {
  used: number;
  free: number;
}

export interface HealthConfig {
  valid: boolean;
  error_count: number;
  first_error: string;
  persistence_state: string;
  primary_valid: boolean;
  backup_valid: boolean;
}

export interface HealthStartup {
  state: string;
  stage: string;
  failing_stage: string;
  failing_error: number;
}

export interface HealthOutput {
  state: string;
  fault: boolean;
  fault_events: number;
  failed_commits: number;
  last_error: number;
  estop_latched: boolean;
  runtime_estop: boolean;
  control_source: number;
  live_objects: number;
  current_power_mw: number;
  throttling_events: number;
  active_preset: number;
  watchdog_fallback: boolean;
}

export interface HealthDmx {
  sources_active: number;
  universes_active: number;
  sources_dropped: number;
  packets_accepted: number;
  packets_rejected_seq: number;
  packets_ignored_unpatched: number;
  submission_failures: number;
  configured_universes: number[];
}

export interface HealthSync {
  packet_loss: number;
}

export interface HealthSnapshot {
  schema_version: number;
  system: HealthSystem;
  network: HealthNetwork;
  nvs: HealthNvs;
  config: HealthConfig;
  startup: HealthStartup;
  output: HealthOutput;
  dmx: HealthDmx;
  sync: HealthSync;
}

export interface AuditEvent {
  timestamp_s: number;
  role: number;
  action: string;
  detail: string;
}

export function getHealth(ip: string): Promise<HealthSnapshot> {
  return apiJson<HealthSnapshot>(ip, '/api/v1/health');
}

export function getLogs(ip: string): Promise<string> {
  return apiFetch(ip, '/api/v1/logs').then(async res => {
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.text();
  });
}

export function getAuditLog(ip: string): Promise<AuditEvent[]> {
  return apiJson<AuditEvent[]>(ip, '/api/v1/audit');
}
