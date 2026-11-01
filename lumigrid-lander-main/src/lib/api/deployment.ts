/**
 * Staged Fleet OTA Deployment orchestrator.
 * Workflow:
 *   Firmware package -> preflight -> canary -> verify -> concurrency-limited fleet deployment
 *
 * Implements LG-036.1 contracts:
 * - Uses uploadFirmware canonical endpoint (POST /api/v1/ota)
 * - Identifies nodes strictly by immutable NodeRecord.id
 * - Canary rollback immediately halts entire deployment
 * - Confirmed targetVersion check (rollback is NEVER success)
 */

import { getHealth, getCapabilities } from './node';
import { uploadFirmware, getVerifiedOtaStatus, bindOtaTarget, isOtaSlotReady, isOtaTargetStatus, isOtaSuccess, isOtaRolledBack, waitForOtaDelay, OtaIdentityError, type OtaStatus, type OtaVerificationTarget } from './firmware';
import { nodeAuthStore } from '../auth/nodeAuth';

export interface DeploymentPlan {
  id: string;
  targetVersion: string;
  nodeIds: string[];
  canaryCount: number;
  concurrency: number;
  state: 'preflight' | 'canary' | 'deploying' | 'verifying' | 'completed' | 'failed';
}

export interface NodePreflightCheck {
  nodeId: string;
  ip: string;
  ok: boolean;
  errors: string[];
  family?: string;
  model?: string;
  freeHeap?: number;
  outputFault?: boolean;
  status?: OtaStatus;
}

export interface DeploymentProgress {
  planId: string;
  state: DeploymentPlan['state'];
  canaryNodeId?: string;
  completedNodeIds: string[];
  failedNodeIds: string[];
  activeNodeIds: string[];
  error?: string;
}

export async function runPreflightCheck(
  nodeId: string,
  ip: string,
  expectedFamily = 'led-node',
  options: { imageBytes?: number; signal?: AbortSignal } = {},
): Promise<NodePreflightCheck> {
  const errors: string[] = [];
  let family = '', model = '', freeHeap = 0, outputFault = false;
  let status: OtaStatus | undefined;
  try {
    if (options.signal?.aborted) throw new DOMException('Rollout cancelled', 'AbortError');
    const caps = await getCapabilities(ip);
    family = caps.node.family;
    model = caps.node.model;
    if (expectedFamily && family !== expectedFamily) errors.push(`Device family '${family}' does not match '${expectedFamily}'`);
    if (caps.features?.ota !== true) errors.push('The node has not advertised firmware update support');
    status = await getVerifiedOtaStatus(ip, nodeId, options.signal);
    if (!isOtaSlotReady(status)) errors.push('Firmware slot capacity or target partition is unavailable');
    if (options.imageBytes !== undefined && (!Number.isSafeInteger(options.imageBytes) || options.imageBytes <= 0 ||
        options.imageBytes > (status.maxFirmwareBytes ?? 0))) errors.push('The selected image is empty or exceeds the firmware slot');
    if (status.state === 'pending_reboot' || status.state === 'pending_verify') errors.push('An earlier update is still awaiting boot verification');
    const health = await getHealth(ip);
    freeHeap = health.system.free_heap;
    if (!Number.isFinite(freeHeap) || freeHeap < 20000) errors.push(`Insufficient free heap for OTA (${freeHeap} bytes)`);
    outputFault = health.output.fault !== false;
    if (outputFault) errors.push('Device is reporting an active hardware output fault');
    if (options.signal?.aborted) throw new DOMException('Rollout cancelled', 'AbortError');
  } catch (error) {
    errors.push(`Preflight unavailable: ${error instanceof Error ? error.message : String(error)}`);
  }
  return { nodeId, ip, ok: errors.length === 0, errors, family, model, freeHeap, outputFault, status };
}

export async function executeStagedDeployment(
  plan: DeploymentPlan,
  nodeAddressMap: Map<string, string>,
  binary: Blob | ArrayBuffer,
  onProgress?: (progress: DeploymentProgress) => void,
  options: { verifyDelayMs?: number; verifyTimeoutMs?: number; signal?: AbortSignal } = {},
): Promise<{ success: boolean; completed: string[]; failed: string[] }> {
  const completed: string[] = [], failed: string[] = [];
  const nodeIds = [...plan.nodeIds];
  const addresses = new Map(nodeAddressMap);
  const sessions = new Map(nodeIds.map(id => [id, nodeAuthStore.getSession(addresses.get(id) ?? '')]));
  const imageBytes = binary instanceof ArrayBuffer ? binary.byteLength : binary.size;
  const concurrency = plan.concurrency;
  const canaryCount = plan.canaryCount;
  const signal = options.signal;
  let candidateVersion = plan.targetVersion.trim();
  const delayMs = options.verifyDelayMs ?? 1000;
  const timeoutMs = options.verifyTimeoutMs ?? 90000;

  const notify = (state: DeploymentPlan['state'], active: string[] = [], canary?: string, error?: string) => {
    plan.state = state;
    onProgress?.({ planId: plan.id, state, canaryNodeId: canary, completedNodeIds: [...completed],
      failedNodeIds: [...failed], activeNodeIds: [...active], error });
  };
  const assertOwner = (id: string) => {
    if (signal?.aborted || nodeAuthStore.getSession(addresses.get(id) ?? '') !== sessions.get(id)) {
      throw new DOMException('Rollout stopped because its page or credentials changed. An accepted update may still finish.', 'AbortError');
    }
  };
  const preflight = async (id: string) => {
    assertOwner(id);
    const ip = addresses.get(id);
    if (!ip) throw new Error(`Missing IP address for node ${id}`);
    const check = await runPreflightCheck(id, ip, 'led-node', { imageBytes, signal });
    assertOwner(id);
    if (!check.ok || !check.status) throw new Error(`Preflight failed on ${id}: ${check.errors.join('; ')}`);
    return check.status;
  };
  const updateOne = async (id: string, onUploaded?: () => void) => {
    // Recheck identity/capacity just before each write; initial fleet preflight can age.
    const before = await preflight(id);
    assertOwner(id);
    const ip = addresses.get(id)!;
    const receipt = await uploadFirmware(ip, binary, undefined, {
      signal, expectedNodeId: before.targetBinding ? id : undefined,
    });
    assertOwner(id);
    const target = bindOtaTarget(receipt, before, id);
    if (candidateVersion && target.targetVersion !== candidateVersion) throw new Error(`Accepted candidate version differs on ${id}`);
    candidateVersion = target.targetVersion;
    onUploaded?.();
    if (!await pollVerification(ip, target, delayMs, timeoutMs, () => assertOwner(id), signal)) {
      throw new Error(`Update verification failed or remains unconfirmed on ${id}`);
    }
    assertOwner(id);
    return id;
  };

  notify('preflight');
  if (new Set(nodeIds).size !== nodeIds.length || !Number.isInteger(concurrency) || concurrency < 1 ||
      !Number.isInteger(canaryCount) || canaryCount < 1 || (nodeIds.length > 0 && canaryCount > nodeIds.length) ||
      !Number.isFinite(delayMs) || delayMs < 0 || !Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    notify('failed', [], undefined, 'Invalid rollout targets or staging limits');
    return { success: false, completed, failed };
  }
  for (const id of nodeIds) {
    try { await preflight(id); }
    catch (error) {
      failed.push(id); notify('failed', [], undefined, error instanceof Error ? error.message : String(error));
      return { success: false, completed, failed };
    }
  }
  for (const id of nodeIds.slice(0, canaryCount)) {
    notify('canary', [id], id);
    try {
      await updateOne(id, () => notify('verifying', [id], id)); completed.push(id);
    } catch (error) {
      failed.push(id); notify('failed', [], id, error instanceof Error ? error.message : String(error));
      return { success: false, completed, failed };
    }
  }
  const remaining = nodeIds.slice(canaryCount);
  for (let i = 0; i < remaining.length; i += concurrency) {
    const batch = remaining.slice(i, i + concurrency);
    notify('deploying', batch);
    const results = await Promise.allSettled(batch.map(id => updateOne(id)));
    for (let j = 0; j < batch.length; j++) {
      if (results[j].status === 'fulfilled') completed.push(batch[j]);
      else failed.push(batch[j]);
    }
    if (failed.length) {
      const failure = results.find(result => result.status === 'rejected') as PromiseRejectedResult;
      notify('failed', [], undefined, failure.reason instanceof Error ? failure.reason.message : String(failure.reason));
      return { success: false, completed, failed };
    }
  }
  notify('completed');
  return { success: true, completed, failed };
}

async function pollVerification(
  ip: string, target: OtaVerificationTarget, delayMs: number, timeoutMs: number,
  assertOwner: () => void, parentSignal?: AbortSignal,
): Promise<boolean> {
  const controller = new AbortController();
  const abort = () => controller.abort();
  parentSignal?.addEventListener('abort', abort, { once: true });
  if (parentSignal?.aborted) controller.abort();
  const deadline = setTimeout(abort, timeoutMs);
  try {
    while (!controller.signal.aborted) {
      assertOwner();
      await waitForOtaDelay(delayMs, controller.signal);
      try {
        const status = await getVerifiedOtaStatus(ip, target.nodeId, controller.signal);
        assertOwner();
        if (controller.signal.aborted) return false;
        if (!isOtaTargetStatus(status, target)) continue;
        if (isOtaRolledBack(status) || status.state === 'failed') return false;
        if (isOtaSuccess(status, target)) {
          const health = await getHealth(ip);
          assertOwner();
          return !controller.signal.aborted && health.output.fault === false;
        }
      } catch (error) {
        if (error instanceof OtaIdentityError || controller.signal.aborted) return false;
        assertOwner();
        // Rebooting nodes can be temporarily unreachable; missing status is never success.
      }
    }
  } catch (error) {
    if (!(error instanceof Error && error.name === 'AbortError')) throw error;
  } finally {
    clearTimeout(deadline); parentSignal?.removeEventListener('abort', abort);
  }
  return false;
}
