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
  nodeId?: string;
  targetPartition?: string;
  state: 'pending_reboot';
}

/**
 * Response from GET /api/v1/ota/status.
 * Confirmation must match the accepted node, version and partition when
 * checking a specific upload; version alone cannot distinguish same-version installs.
 * state == 'rolled_back' means the update failed — do NOT show success.
 */
export interface OtaStatus {
  nodeId?: string;
  targetBinding?: boolean;
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
  maxFirmwareBytes?: number;
  runningPartition?: string;
  nextPartition?: string;
  targetPartition?: string;
  partitionLayout?: number;
  imageBytes?: number;
  verificationStartedMs?: number;
  verificationDeadlineMs?: number;
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
  options: { signal?: AbortSignal; expectedNodeId?: string } = {},
): Promise<OtaUploadResponse> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    const session = nodeAuthStore.getSession(ip);
    const signal = options.signal;
    let settled = false;
    const cleanup = () => {
      signal?.removeEventListener('abort', abort);
      xhr.upload.onprogress = null;
      xhr.onload = xhr.onerror = xhr.ontimeout = xhr.onabort = null;
    };
    const finish = (value?: OtaUploadResponse, error?: Error) => {
      if (settled) return;
      settled = true;
      cleanup();
      if (error) reject(error);
      else resolve(value!);
    };
    const abort = () => {
      finish(undefined, new DOMException('Firmware request cancelled; an accepted update may still finish on the node.', 'AbortError'));
      xhr.abort();
    };
    if (signal?.aborted) { finish(undefined, new DOMException('Firmware request cancelled', 'AbortError')); return; }
    signal?.addEventListener('abort', abort, { once: true });

    xhr.upload.onprogress = (e) => {
      if (!settled && nodeAuthStore.getSession(ip) === session && e.lengthComputable && e.total > 0 && onProgress) {
        onProgress(Math.min(90, Math.max(0, Math.round((e.loaded / e.total) * 90))));
      }
    };

    xhr.onload = () => {
      if (nodeAuthStore.getSession(ip) !== session) {
        finish(undefined, new DOMException('Credentials changed before upload confirmation', 'AbortError'));
        return;
      }
      if (xhr.status === 401) {
        nodeAuthStore.clearSession(ip);
        finish(undefined, new Error(`[${ip}] /api/v1/ota → HTTP 401 Authentication required`));
        return;
      }
      if (xhr.status >= 200 && xhr.status < 300) {
        try {
          const res = JSON.parse(xhr.responseText) as Partial<OtaUploadResponse> | null;
          if (!res || res.accepted !== true || res.state !== 'pending_reboot' ||
              typeof res.targetVersion !== 'string' || !res.targetVersion.trim() ||
              !Number.isInteger(res.commandId) || res.commandId! <= 0 ||
              !Number.isInteger(res.jobId) || res.jobId! <= 0) {
            throw new Error('Incomplete update receipt');
          }
          if (options.expectedNodeId && res.nodeId !== options.expectedNodeId) {
            throw new Error('Update receipt does not identify the selected node');
          }
          finish(res as OtaUploadResponse);
        } catch {
          finish(undefined, new OtaUnconfirmedError('The node returned no valid update receipt. Check its status before retrying; the update may still finish.'));
        }
      } else {
        finish(undefined, new Error(`[${ip}] /api/v1/ota → HTTP ${xhr.status}: ${xhr.statusText}`));
      }
    };

    xhr.onerror = () => finish(undefined, new OtaUnconfirmedError(`[${ip}] Upload connection lost. The update outcome is unconfirmed.`));
    xhr.ontimeout = () => finish(undefined, new OtaUnconfirmedError(`[${ip}] Upload timed out. The update outcome is unconfirmed.`));
    xhr.onabort = () => finish(undefined, new DOMException('Firmware request cancelled', 'AbortError'));
    xhr.timeout = 120000;

    // ALWAYS use getNodeHttpBase — never infer HTTPS from dashboard origin
    try {
      const base = getNodeHttpBase(ip);
      xhr.open('POST', `${base}/api/v1/ota`, true);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      if (session?.sessionToken) xhr.setRequestHeader('Authorization', `Bearer ${session.sessionToken}`);
      if (options.expectedNodeId) xhr.setRequestHeader('X-LumiGrid-Node-Id', options.expectedNodeId);
      xhr.send(binary);
    } catch (error) {
      finish(undefined, error instanceof Error ? error : new Error(String(error)));
    }
  });
}

// ── OTA status ────────────────────────────────────────────────────────────────

export function getOtaStatus(ip: string, signal?: AbortSignal): Promise<OtaStatus> {
  return apiJson<OtaStatus>(ip, '/api/v1/ota/status', { signal, cache: 'no-store' });
}

export class OtaUnconfirmedError extends Error {
  constructor(message: string) { super(message); this.name = 'OtaUnconfirmedError'; }
}

export class OtaIdentityError extends Error {
  constructor() { super('This address no longer identifies the selected node. Update confirmation stopped.'); this.name = 'OtaIdentityError'; }
}

export interface OtaVerificationTarget {
  nodeId: string;
  targetVersion: string;
  targetPartition: string;
}

/** Require complete slot metadata before starting a write. */
export function isOtaSlotReady(status: OtaStatus): boolean {
  return Number.isSafeInteger(status.maxFirmwareBytes) && status.maxFirmwareBytes! > 0 &&
    typeof status.runningPartition === 'string' && Boolean(status.runningPartition.trim()) &&
    typeof status.nextPartition === 'string' && Boolean(status.nextPartition.trim()) &&
    status.runningPartition !== 'unknown' && status.nextPartition !== 'unknown' &&
    status.runningPartition !== status.nextPartition;
}

/** Preserve the accepted candidate identity, including same-version updates. */
export function bindOtaTarget(receipt: OtaUploadResponse, before: OtaStatus, nodeId: string): OtaVerificationTarget {
  const partition = receipt.targetPartition ?? before.nextPartition;
  if (!nodeId || (receipt.nodeId !== undefined && receipt.nodeId !== nodeId)) throw new OtaIdentityError();
  if (receipt.accepted !== true || receipt.state !== 'pending_reboot' || !receipt.targetVersion ||
      !isOtaSlotReady(before) || !partition || partition === 'unknown' || partition === before.runningPartition) {
    throw new OtaUnconfirmedError('The accepted candidate cannot be identified. Check the node before retrying.');
  }
  return { nodeId, targetVersion: receipt.targetVersion, targetPartition: partition };
}

/** Older nodes expose identity separately; never treat an unavailable identity as a match. */
export async function getVerifiedOtaStatus(ip: string, nodeId: string, signal?: AbortSignal): Promise<OtaStatus> {
  const [identity, status] = await Promise.all([
    apiJson<{ id: string }>(ip, '/api/v1/identity', { signal, cache: 'no-store' }),
    getOtaStatus(ip, signal),
  ]);
  if (identity.id !== nodeId || (status.nodeId !== undefined && status.nodeId !== nodeId)) throw new OtaIdentityError();
  return { ...status, nodeId };
}

export function isOtaTargetStatus(status: OtaStatus, target: OtaVerificationTarget): boolean {
  return status.nodeId === target.nodeId && status.targetVersion === target.targetVersion &&
    status.targetPartition === target.targetPartition;
}

export function waitForOtaDelay(ms: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const abort = () => { clearTimeout(timer); signal?.removeEventListener('abort', abort); reject(new DOMException('Update confirmation cancelled', 'AbortError')); };
    const timer = setTimeout(() => { signal?.removeEventListener('abort', abort); resolve(); }, ms);
    if (signal?.aborted) abort();
    else signal?.addEventListener('abort', abort, { once: true });
  });
}

export interface StoragePartition { label: string; type: string; address: number; size: number }
export interface StorageDiagnostics {
  physicalFlashBytes: number;
  configuredFlashBytes: number;
  partitionLayoutVersion: number;
  health: string;
  imageBytes: number;
  running: StoragePartition | null;
  nextOta: StoragePartition | null;
  partitions: StoragePartition[];
}
export function getStorageDiagnostics(ip: string): Promise<StorageDiagnostics> {
  return apiJson<StorageDiagnostics>(ip, '/api/v1/diagnostics/storage');
}
export function firmwareFitsSlot(imageBytes: number, slotBytes: number): boolean {
  return Number.isSafeInteger(imageBytes) && Number.isSafeInteger(slotBytes) && imageBytes > 0 && slotBytes > 0 && imageBytes <= slotBytes;
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
export function isOtaSuccess(status: OtaStatus, target?: OtaVerificationTarget): boolean {
  return (
    status.state === 'confirmed' && !status.rolledBack &&
    Boolean(status.targetVersion) &&
    status.runningVersion === status.targetVersion &&
    (!target || (isOtaTargetStatus(status, target) && status.runningPartition === target.targetPartition))
  );
}

/**
 * Returns true when a rollback has occurred.
 * Old version after rollback is NEVER considered success.
 */
export function isOtaRolledBack(status: OtaStatus): boolean {
  return status.state === 'rolled_back' || status.rolledBack;
}
