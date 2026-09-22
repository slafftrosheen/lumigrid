/**
 * Staged Fleet OTA Deployment orchestrator.
 * Workflow:
 *   Firmware package -> preflight -> canary -> verify -> concurrency-limited fleet deployment
 */

import { getHealth, getCapabilities } from './node';
import { postOta, type OtaCommandResult } from './firmware';

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
  expectedFamily = 'led-node'
): Promise<NodePreflightCheck> {
  const errors: string[] = [];
  let family = '';
  let model = '';
  let freeHeap = 0;
  let outputFault = false;

  try {
    const caps = await getCapabilities(ip).catch(() => null);
    if (caps?.node?.family) {
      family = caps.node.family;
      model = caps.node.model;
      if (expectedFamily && family !== expectedFamily) {
        errors.push(`Device family '${family}' does not match expected '${expectedFamily}'`);
      }
    }

    const health = await getHealth(ip);
    freeHeap = health.system.free_heap;
    if (health.system.free_heap < 20000) {
      errors.push(`Insufficient free heap for OTA (${health.system.free_heap} bytes)`);
    }
    if (health.output.fault) {
      outputFault = true;
      errors.push('Device is reporting an active hardware output fault');
    }
  } catch (e) {
    errors.push(`Preflight unreachable or error: ${e instanceof Error ? e.message : String(e)}`);
  }

  return {
    nodeId,
    ip,
    ok: errors.length === 0,
    errors,
    family,
    model,
    freeHeap,
    outputFault,
  };
}

export async function executeStagedDeployment(
  plan: DeploymentPlan,
  nodeAddressMap: Map<string, string>, // nodeId -> ip
  binary: Blob | ArrayBuffer,
  onProgress?: (progress: DeploymentProgress) => void,
  options?: {
    verifyDelayMs?: number;
    verifyTimeoutMs?: number;
  }
): Promise<{ success: boolean; completed: string[]; failed: string[] }> {
  const completed: string[] = [];
  const failed: string[] = [];
  const verifyDelayMs = options?.verifyDelayMs ?? 1000;
  const verifyTimeoutMs = options?.verifyTimeoutMs ?? 30000;

  const notify = (state: DeploymentPlan['state'], active: string[] = [], canary?: string, err?: string) => {
    plan.state = state;
    if (onProgress) {
      onProgress({
        planId: plan.id,
        state,
        canaryNodeId: canary,
        completedNodeIds: [...completed],
        failedNodeIds: [...failed],
        activeNodeIds: [...active],
        error: err,
      });
    }
  };

  // 1. Preflight phase across all selected nodes
  notify('preflight');
  for (const nodeId of plan.nodeIds) {
    const ip = nodeAddressMap.get(nodeId);
    if (!ip) {
      failed.push(nodeId);
      notify('failed', [], undefined, `Missing IP address for node ${nodeId}`);
      return { success: false, completed, failed };
    }

    const preflight = await runPreflightCheck(nodeId, ip);
    if (!preflight.ok) {
      failed.push(nodeId);
      notify('failed', [], undefined, `Preflight failed on ${nodeId}: ${preflight.errors.join('; ')}`);
      return { success: false, completed, failed };
    }
  }

  if (plan.nodeIds.length === 0) {
    notify('completed');
    return { success: true, completed, failed };
  }

  // 2. Canary Phase: flash 1st node and verify
  const canaryId = plan.nodeIds[0];
  const canaryIp = nodeAddressMap.get(canaryId)!;
  notify('canary', [canaryId], canaryId);

  try {
    await postOta(canaryIp, binary);
  } catch (e) {
    failed.push(canaryId);
    notify('failed', [], canaryId, `Canary OTA upload failed on ${canaryId}: ${e}`);
    return { success: false, completed, failed };
  }

  // Verify canary node after reboot
  notify('verifying', [canaryId], canaryId);
  const canaryVerified = await pollVerification(canaryIp, plan.targetVersion, verifyDelayMs, verifyTimeoutMs);
  if (!canaryVerified) {
    failed.push(canaryId);
    notify('failed', [], canaryId, `Canary validation failed on ${canaryId}`);
    return { success: false, completed, failed };
  }
  completed.push(canaryId);

  // 3. Deployment Phase: remaining nodes with concurrency limit (default 2)
  const remainingIds = plan.nodeIds.slice(1);
  if (remainingIds.length > 0) {
    notify('deploying');
    const concurrency = Math.max(1, plan.concurrency || 2);

    for (let i = 0; i < remainingIds.length; i += concurrency) {
      const batch = remainingIds.slice(i, i + concurrency);
      notify('deploying', batch);

      const batchResults = await Promise.allSettled(
        batch.map(async (nodeId) => {
          const ip = nodeAddressMap.get(nodeId)!;
          await postOta(ip, binary);
          const ok = await pollVerification(ip, plan.targetVersion, verifyDelayMs, verifyTimeoutMs);
          if (!ok) throw new Error(`Verification failed on ${nodeId}`);
          return nodeId;
        })
      );

      for (let j = 0; j < batch.length; j++) {
        const nodeId = batch[j];
        const res = batchResults[j];
        if (res.status === 'fulfilled') {
          completed.push(nodeId);
        } else {
          failed.push(nodeId);
        }
      }
    }
  }

  const success = failed.length === 0;
  notify(success ? 'completed' : 'failed');
  return { success, completed, failed };
}

async function pollVerification(
  ip: string,
  targetVersion: string,
  delayMs: number,
  timeoutMs: number
): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, delayMs));
    try {
      const caps = await getCapabilities(ip);
      if (caps && (!targetVersion || caps.node.firmware === targetVersion)) {
        const health = await getHealth(ip);
        if (!health.output.fault) {
          return true;
        }
      }
    } catch {
      // Node is rebooting, retry until deadline
    }
  }
  return false;
}
