import { nodeAuthStore } from '../auth/nodeAuth';
import { apiJson } from './transport';

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

export interface OtaCommandResult {
  status: string;
  accepted: boolean;
  commandId: number;
  jobId: number;
  message?: string;
}

/**
 * Queries the current long-running operation job on the node.
 */
export async function getCurrentJob(ip: string): Promise<OperationJob | null> {
  const res = await apiJson<{ job: OperationJob | null }>(ip, '/api/v1/jobs/current');
  return res.job;
}

/**
 * Initiates an OTA job on the node via the control-plane endpoint.
 * Uses the authoritative node session token.
 */
export async function postOta(
  ip: string,
  binary: Blob | ArrayBuffer,
  onProgress?: (pct: number) => void
): Promise<OtaCommandResult> {
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
          const res = JSON.parse(xhr.responseText) as OtaCommandResult;
          resolve(res);
        } catch {
          resolve({
            status: 'success',
            accepted: true,
            commandId: 0,
            jobId: 0,
            message: 'OTA uploaded successfully',
          });
        }
      } else {
        reject(new Error(`HTTP ${xhr.status}: ${xhr.statusText}`));
      }
    };

    xhr.onerror = () => reject(new Error('Network error during OTA upload'));
    xhr.ontimeout = () => reject(new Error('OTA upload timed out'));
    xhr.timeout = 120000;

    const scheme =
      typeof location !== 'undefined' && location.protocol === 'https:' ? 'https' : 'http';
    xhr.open('POST', `${scheme}://${ip}/api/v1/ota`, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    if (session?.sessionToken) {
      xhr.setRequestHeader('Authorization', `Bearer ${session.sessionToken}`);
    }

    xhr.send(binary);
  });
}

export function uploadFirmware(
  ip: string,
  binary: File | Blob,
  onProgress?: (pct: number) => void,
): Promise<{ ok: boolean; message?: string }> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    const session = nodeAuthStore.getSession(ip);

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) {
        onProgress(Math.round((e.loaded / e.total) * 100));
      }
    };

    xhr.onload = () => {
      if (xhr.status === 401) {
        nodeAuthStore.clearSession(ip);
        reject(new Error(`[${ip}] /update → HTTP 401 Authentication required`));
        return;
      }
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve({ ok: true });
      } else {
        reject(new Error(`HTTP ${xhr.status}: ${xhr.statusText}`));
      }
    };

    xhr.onerror = () => reject(new Error('Network error during firmware upload'));
    xhr.ontimeout = () => reject(new Error('Firmware upload timed out'));
    xhr.timeout = 120000;

    const scheme =
      typeof location !== 'undefined' && location.protocol === 'https:' ? 'https' : 'http';
    xhr.open('POST', `${scheme}://${ip}/update`, true);
    if (session?.sessionToken) {
      xhr.setRequestHeader('Authorization', `Bearer ${session.sessionToken}`);
    }

    const form = new FormData();
    form.append('update', binary, 'firmware.bin');
    xhr.send(form);
  });
}
