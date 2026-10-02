/**
 * LumiGrid Recovery API client.
 *
 * Covers:
 *   GET  /api/v1/recovery/status
 *   GET  /api/v1/backup             → streaming application/x-lumigrid-backup (.lgbak)
 *   POST /api/v1/restore/validate   → preflight before restore
 *   POST /api/v1/restore            → two-stage restore (requires preflight first)
 */

import { apiFetch, apiJson } from './transport';

// ── Recovery status types ────────────────────────────────────────────────────

export type CommissioningState =
  | 'none'
  | 'factory_first_boot'
  | 'physical_recovery';

export type OtaState =
  | 'idle'
  | 'pending_reboot'
  | 'pending_verify'
  | 'confirmed'
  | 'rolled_back'
  | 'failed';

export interface RecoveryStatus {
  safeMode: boolean;
  configValid: boolean;
  commissioningState: CommissioningState;
  startupState: string;
  failingStage: string;
  failingError: number;
  recoveryNetworkActive: boolean;
  outputsForcedOff: boolean;
  otaState: OtaState | string;
  restoreState: string;
}

export function getRecoveryStatus(ip: string): Promise<RecoveryStatus> {
  return apiJson<RecoveryStatus>(ip, '/api/v1/recovery/status');
}

// ── Human-readable recovery cause ────────────────────────────────────────────

/**
 * Returns a human-readable description of the recovery cause from RecoveryStatus.
 * Do not expose numeric error codes directly in UI.
 */
export function describeRecoveryCause(status: RecoveryStatus): string {
  if (status.restoreState === 'interrupted') {
    return 'Restore interrupted — node is in safe mode to protect data integrity.';
  }
  if (!status.configValid) {
    return 'Configuration invalid — startup halted at configuration validation.';
  }
  if (status.otaState === 'failed') {
    return `OTA candidate verification failed${status.failingError ? ` (error ${status.failingError})` : ''}.`;
  }
  if (status.failingStage) {
    return `Startup subsystem failed: ${status.failingStage}${status.failingError ? ` (error ${status.failingError})` : ''}.`;
  }
  if (status.safeMode) {
    return 'Node is in safe mode.';
  }
  return '';
}

// ── Backup ─────────────────────────────────────────────────────────────────────

/**
 * Downloads backup as a Blob containing raw streaming NDJSON (.lgbak).
 *
 * Uses apiFetch (not apiJson) — backup is NOT JSON, it is
 * application/x-lumigrid-backup streaming NDJSON.
 *
 * Do NOT JSON.stringify this blob or reserialize it before saving/uploading.
 */
export async function downloadBackup(ip: string): Promise<{ blob: Blob; filename: string }> {
  const response = await apiFetch(ip, '/api/v1/backup');

  if (!response.ok) {
    throw new Error(`[${ip}] /api/v1/backup → HTTP ${response.status}: ${response.statusText}`);
  }

  // Read filename from Content-Disposition if available; fall back to immutable node ID.
  let filename = '';
  const disposition = response.headers.get('Content-Disposition');
  if (disposition) {
    const match = /filename[^;=\n]*=(?:(["'])([^"']*)\1|([^;\s]*))/i.exec(disposition);
    if (match) {
      filename = match[2] ?? match[3] ?? '';
    }
  }

  const blob = await response.blob();

  // Fallback filename uses node ID from URL, not IP address.
  if (!filename) {
    // Try to extract a node ID from the transport layer (best-effort).
    filename = `lumigrid-backup.lgbak`;
  }

  return { blob, filename };
}

/**
 * Downloads backup and triggers browser save dialog.
 *
 * @param ip Node IP address.
 * @param nodeId Immutable node ID used for filename fallback. Never uses IP.
 */
export async function saveBackupFile(ip: string, nodeId: string): Promise<void> {
  const response = await apiFetch(ip, '/api/v1/backup');

  if (!response.ok) {
    throw new Error(`[${ip}] /api/v1/backup → HTTP ${response.status}: ${response.statusText}`);
  }

  let filename = '';
  const disposition = response.headers.get('Content-Disposition');
  if (disposition) {
    const match = /filename[^;=\n]*=(?:(["'])([^"']*)\1|([^;\s]*))/i.exec(disposition);
    if (match) {
      filename = match[2] ?? match[3] ?? '';
    }
  }

  if (!filename) {
    // Immutable node ID in filename — never the IP address.
    filename = `lumigrid-${nodeId}-backup.lgbak`;
  }

  const blob = await response.blob();
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

// ── Restore validation response ───────────────────────────────────────────────

export interface RestoreValidationResponse {
  valid: boolean;
  schema: number;

  source: {
    nodeId: string;
    firmwareVersion: string;
  };

  packageChecksum: number;

  plan: {
    deviceConfig: boolean;
    scenes: number;
    sceneMotions: number;
    timeline: boolean;
    calendar: boolean;
    cueLists: number;
  };

  preserved: {
    authentication: boolean;
    networkCredentials: boolean;
    nodeIdentity: boolean;
    syncToken: boolean;
  };

  warnings: string[];
}

/**
 * Preflights a .lgbak file via POST /api/v1/restore/validate.
 * Always call this before restorePackage(). There must be no direct path
 * from file selection to restore.
 *
 * Sends raw file bytes with Content-Type: application/x-lumigrid-backup.
 */
export async function validateRestorePackage(
  ip: string,
  file: Blob,
): Promise<RestoreValidationResponse> {
  const response = await apiFetch(ip, '/api/v1/restore/validate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-lumigrid-backup' },
    body: file,
  });

  if (!response.ok) {
    let detail = response.statusText;
    try {
      const body = await response.clone().json() as { error?: string };
      if (body?.error) detail = body.error;
    } catch { /* keep status text */ }
    throw new Error(`[${ip}] /api/v1/restore/validate → HTTP ${response.status}: ${detail}`);
  }

  return response.json() as Promise<RestoreValidationResponse>;
}

export interface RestoreResponse {
  accepted: boolean;
  commandId?: number;
  jobId?: number;
}

/**
 * Executes restore via POST /api/v1/restore.
 * Must only be called after validateRestorePackage() has returned valid: true
 * and the operator has confirmed.
 *
 * Sends raw file bytes with Content-Type: application/x-lumigrid-backup.
 * The node will reboot after restore. Expect network disconnection.
 */
export async function restorePackage(
  ip: string,
  file: Blob,
): Promise<RestoreResponse> {
  const response = await apiFetch(ip, '/api/v1/restore', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-lumigrid-backup' },
    body: file,
  });

  if (!response.ok) {
    let detail = response.statusText;
    try {
      const body = await response.clone().json() as { error?: string };
      if (body?.error) detail = body.error;
    } catch { /* keep status text */ }
    throw new Error(`[${ip}] /api/v1/restore → HTTP ${response.status}: ${detail}`);
  }

  return response.json() as Promise<RestoreResponse>;
}
