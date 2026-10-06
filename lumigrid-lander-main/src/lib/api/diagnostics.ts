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

/** LG-038.3 §3/§58/§59: real 8-bit heap truth. Primary UI metric is free8. */
export interface HealthMemory {
  free8: number;
  largest8: number;
  minimum_free: number;
  health: 'HEALTHY' | 'LOW_RUNTIME_MEMORY' | 'HEAP_FRAGMENTED' | 'CRITICAL' | string;
  low_runtime_memory: boolean;
  heap_fragmented: boolean;
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
  memory?: HealthMemory;
}

/** LG-038.3 §4: /api/v1/diagnostics/memory response. */
export interface MemoryDiagnosticsTask {
  name: string;
  stackBytes: number;
  minimumFreeStack: number;
}

export interface MemoryDiagnosticsResponse {
  free8: number;
  largest8: number;
  minimumFree: number;
  freeInternal?: number;
  largestInternal?: number;
  freeDma?: number;
  largestDma?: number;
  fragmentationRatio?: number;
  health: string;
  allocationFailures?: {
    apiAllocationFailures: number;
    previewAllocationFailures: number;
    rmtAllocationFailures: number;
    factoryTaskStartFailures: number;
    networkTaskStartFailures: number;
  };
  services?: Record<string, boolean>;
  tasks: MemoryDiagnosticsTask[];
}

export function getMemoryDiagnostics(ip: string): Promise<MemoryDiagnosticsResponse> {
  return apiJson<MemoryDiagnosticsResponse>(ip, '/api/v1/diagnostics/memory');
}

export interface AuditEvent {
  timestamp_s: number;
  role: number;
  action: string;
  detail: string;
}

export interface ActionableIssue {
  id: string;
  severity: 'critical' | 'warning' | 'info';
  category: string;
  title: string;
  description: string;
  remedy?: string;
}

export function evaluateActionableIssues(health: HealthSnapshot | null): ActionableIssue[] {
  if (!health) return [];
  const issues: ActionableIssue[] = [];

  // 1. E-Stop latched or runtime E-stop
  if (health.output.estop_latched || health.output.runtime_estop) {
    issues.push({
      id: 'estop_latched',
      severity: 'critical',
      category: 'safety',
      title: 'Emergency Stop Active',
      description: 'Physical or software E-stop is latched. All LED outputs and PWM channels are strictly inhibited.',
      remedy: 'Clear the emergency stop command via UI or reset hardware E-stop line to restore illumination.',
    });
  }

  // 2. Hardware commit failures or output fault
  if (health.output.failed_commits > 0 || health.output.fault) {
    issues.push({
      id: 'output_fault',
      severity: 'critical',
      category: 'hardware',
      title: `Hardware Commit Failures (${health.output.failed_commits})`,
      description: `OutputController encountered ${health.output.failed_commits} hardware commit failures. Last error code: ${health.output.last_error}.`,
      remedy: 'Check wiring, level shifters, power supply rails, and verify channel pin configuration.',
    });
  }

  // 3. Configuration Integrity
  if (!health.config.valid || health.config.error_count > 0 || !health.config.primary_valid) {
    issues.push({
      id: 'config_integrity',
      severity: 'critical',
      category: 'config',
      title: 'Configuration Integrity Fault',
      description: `Config errors: ${health.config.error_count}. Primary NVS slot: ${health.config.primary_valid ? 'Valid' : 'Corrupt'}. Backup slot: ${health.config.backup_valid ? 'Valid' : 'Corrupt'}. Persistence: ${health.config.persistence_state}.`,
      remedy: 'Restore node configuration from a known-good backup or re-save default settings.',
    });
  }

  // 4. Low Memory Headroom (LG-038.3 §45: judge free total AND largest block)
  {
    const mem8 = health.memory;
    const freeForEval = mem8 ? mem8.free8 : health.system.free_heap;
    const largestForEval = mem8 ? mem8.largest8 : health.system.free_heap;
    const minForEval = mem8 ? mem8.minimum_free : health.system.min_free_heap;
    const low = mem8 ? mem8.low_runtime_memory : minForEval < 30000;
    const fragmented = mem8 ? mem8.heap_fragmented : false;
    if (low || fragmented) {
      issues.push({
        id: 'low_heap',
        severity: minForEval < 15000 ? 'critical' : 'warning',
        category: 'memory',
        title: fragmented && !low
          ? `Fragmented Heap (${Math.round(freeForEval / 1024)} KB free, largest block ${Math.round(largestForEval / 1024)} KB)`
          : `Low Free Heap Memory (${Math.round(freeForEval / 1024)} KB, min ${Math.round(minForEval / 1024)} KB)`,
        description: fragmented
          ? 'Free 8-bit memory is split into small blocks. Large allocations (topology, restore, OTA) can fail even with free heap remaining.'
          : 'System free memory is critically low, increasing risk of task allocation failure.',
        remedy: 'Reduce addressable topology, limit concurrent WebSocket clients, or restart node.',
      });
    }
  }

  // 5. Weak Wi-Fi RSSI
  if (health.network.wifi_rssi < -82 && health.network.wifi_rssi !== 0) {
    issues.push({
      id: 'weak_wifi',
      severity: 'warning',
      category: 'network',
      title: `Degraded Wi-Fi Signal (${health.network.wifi_rssi} dBm)`,
      description: 'High packet latency and packet loss may occur during live Art-Net/sACN streaming or sync.',
      remedy: 'Reposition the Wi-Fi access point closer or attach an external 2.4 GHz antenna.',
    });
  }

  // 6. Sync Drift & Packet Loss
  if (health.sync.packet_loss > 0) {
    issues.push({
      id: 'sync_loss',
      severity: 'warning',
      category: 'sync',
      title: `Clock Sync Loss (${health.sync.packet_loss} packets lost)`,
      description: 'Node sync packets dropped on the local network. Multi-node timeline cues may drift.',
      remedy: 'Ensure network multicast/broadcast is enabled on the switch without IGMP snooping drops.',
    });
  }

  // 7. DMX Dropped Sources or Submission Failures
  if (health.dmx.submission_failures > 0 || health.dmx.sources_dropped > 0) {
    issues.push({
      id: 'dmx_failures',
      severity: 'warning',
      category: 'dmx',
      title: 'DMX Ingestion Drops Detected',
      description: `${health.dmx.sources_dropped} sources dropped, ${health.dmx.submission_failures} submission failures. Rejected seq: ${health.dmx.packets_rejected_seq}.`,
      remedy: 'Verify DMX console packet rate (keep <= 44 Hz per universe) and check network congestion.',
    });
  }

  // 8. Power Throttling
  if (health.output.throttling_events > 0) {
    issues.push({
      id: 'power_throttled',
      severity: 'warning',
      category: 'power',
      title: `Power Budget Throttling (${health.output.current_power_mw} mW)`,
      description: `Node exceeded power limits ${health.output.throttling_events} times and throttled global brightness.`,
      remedy: 'Increase power supply capacity or lower peak scene brightness to prevent supply rail sag.',
    });
  }

  return issues;
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

export interface OutputDiagnosticsAddressable {
  activeLanes: number;
  maxPixels: number;
  lastCommitUs: number;
  maxCommitUs: number;
  failedLane: number | null;
}

export interface OutputDiagnosticsPwm {
  available: boolean;
  address: number;
  i2cHz: number;
  pwmHz: number;
  oeEnabled: boolean;
  commitFailures: number;
}

export interface OutputDiagnosticsFrame {
  targetPeriodUs: number;
  lastUs: number;
  maxUs: number;
  deadlineMisses: number;
}

export interface OutputDiagnosticsResponse {
  addressable: OutputDiagnosticsAddressable;
  pwm: OutputDiagnosticsPwm;
  frame: OutputDiagnosticsFrame;
}

export function getOutputDiagnostics(ip: string): Promise<OutputDiagnosticsResponse> {
  return apiJson<OutputDiagnosticsResponse>(ip, '/api/v1/diagnostics/output');
}
