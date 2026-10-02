/**
 * LumiGrid Firmware / OTA API client.
 *
 * Canonical OTA endpoint: POST /api/v1/ota
 *
 * The legacy /update endpoint is NOT used here. If compatibility shims are needed
 * for old firmware, they belong in src/lib/api/legacy.ts only.
 *
 * OTA URL ALWAYS uses getNodeHttpBase() from transport.ts.
 * Never derive scheme from location.protocol — the node is plain HTTP.
 */

import { nodeAuthStore } from '../auth/nodeAuth';
import { apiJson, getNodeHttpBase } from './transport';

// ── Job types ─────────────────────────────────────────────────────────────────

export type JobType = 'ota' | 'restore' | 'burn_in' | 'config_migrate';
export type JobState = 'queued' | 'preparing' | 'running' | 'verifying' | 'completed' | 'failed' | 'cancelled';

export interface OperationJob {
  jobId: number;
  type: JobType | string;
  state: JobState | string;
  progress: number;
  startedMs: number;
  error: string | null;
}

// ── OTA response types ────────────────────────────────────────────────────────

/**
 * Response from POST /api/v1/ota.
 * targetVersion comes from firmware metadata in the uploaded image — do NOT
 * guess version from the filename.
 */
export interface OtaUploadResponse {
  accepted: boolean;
  commandId: number;
  jobId: number;
  targetVersion: string;
  projectName: string;
  state: 'pending_reboot';
}

/**
 * Response from GET /api/v1/ota/status.
 * state == 'confirmed' AND runningVersion == targetVersion is the only success.
 * state == 'rolled_back' means the update failed — do NOT show success.
 */
export interface OtaStatus {
  state:
    | 'idle'
    | 'pending_reboot'
    | 'pending_verify'
    | 'confirmed'
    | 'rolled_back'
    | 'failed';
  runningVersion: string;
  previousVersion?: string;
  targetVersion?: string;
  rolledBack: boolean;
  reason?: string;
}

// ── Single canonical OTA uploader ─────────────────────────────────────────────

/**
 * Upload a firmware image via POST /api/v1/ota.
 *
 * This is the ONE canonical OTA upload path. The legacy /update endpoint
 * is never called from normal dashboard code.
 *
 * URL scheme ALWAYS uses getNodeHttpBase(ip) — never location.protocol.
 * Progress 0-90% is upload progress; the remaining 10% is reserved for
 * the firmware's internal validation phase.
 */
export function uploadFirmware(
  ip: string,
  binary: Blob | ArrayBuffer,
  onProgress?: (pct: number) => void,
): Promise<OtaUploadResponse> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    const session = nodeAuthStore.getSession(ip);

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) {
        onProgress(Math.round((e.loaded / e.total) * 90));
      }
    };

    xhr.onload = () => {
      if (xhr.status === 401) {
        nodeAuthStore.clearSession(ip);
        reject(new Error(`[${ip}] /api/v1/ota → HTTP 401 Authentication required`));
        return;
      }
      if (xhr.status >= 200 && xhr.status < 300) {
        try {
          const res = JSON.parse(xhr.responseText) as OtaUploadResponse;
          resolve(res);
        } catch {
          // If firmware returns non-JSON (old firmware compat), synthesise minimal response
          resolve({
            accepted: true,
            commandId: 0,
            jobId: 0,
            targetVersion: '',
            projectName: '',
            state: 'pending_reboot',
          });
        }
      } else {
        reject(new Error(`[${ip}] /api/v1/ota → HTTP ${xhr.status}: ${xhr.statusText}`));
      }
    };

    xhr.onerror = () => reject(new Error(`[${ip}] /api/v1/ota → Network error during OTA upload`));
    xhr.ontimeout = () => reject(new Error(`[${ip}] /api/v1/ota → Upload timed out`));
    xhr.timeout = 120000;

    // ALWAYS use getNodeHttpBase — never infer HTTPS from dashboard origin
    const base = getNodeHttpBase(ip);
    xhr.open('POST', `${base}/api/v1/ota`, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    if (session?.sessionToken) {
      xhr.setRequestHeader('Authorization', `Bearer ${session.sessionToken}`);
    }

    xhr.send(binary);
  });
}

// ── OTA status ────────────────────────────────────────────────────────────────

export function getOtaStatus(ip: string): Promise<OtaStatus> {
  return apiJson<OtaStatus>(ip, '/api/v1/ota/status');
}

// ── Job queries ────────────────────────────────────────────────────────────────

/**
 * Queries the current long-running operation job on the node.
 */
export async function getCurrentJob(ip: string): Promise<OperationJob | null> {
  const res = await apiJson<{ job: OperationJob | null }>(ip, '/api/v1/jobs/current');
  return res.job;
}

// ── OTA result helpers ────────────────────────────────────────────────────────

/**
 * Returns true only when OTA is definitively confirmed successful.
 * confirmed state AND running version matches target — no other condition is success.
 */
export function isOtaSuccess(status: OtaStatus): boolean {
  return (
    status.state === 'confirmed' &&
    Boolean(status.targetVersion) &&
    status.runningVersion === status.targetVersion
  );
}

/**
 * Returns true when a rollback has occurred.
 * Old version after rollback is NEVER considered success.
 */
export function isOtaRolledBack(status: OtaStatus): boolean {
  return status.state === 'rolled_back' || status.rolledBack;
}
