import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import { runPreflightCheck, executeStagedDeployment, type DeploymentPlan } from './deployment';
import * as nodeApi from './node';
import * as firmwareApi from './firmware';

describe('Staged Fleet Deployment', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  const mockHealthyCapabilities = {
    schema: 1,
    node: { family: 'led-node', model: 'lg-led-v1', firmware: '0.8.0', hardwareRevision: 'A' },
    limits: { pixelLanes: 8, pwmChannels: 8, zones: 8, layersPerZone: 3, presets: 32, timelineSteps: 16 },
    features: { addressable: true, pwm: true, artnet: true, sacn: true, sync: true, mqtt: true, factory: true, ota: true, signedOta: true, backup: true },
    protocols: { httpApi: 1, websocket: 2, sync: 1 },
  };

  const mockHealthyHealth = {
    schema_version: 1,
    system: { uptime_s: 100, free_heap: 50000, min_free_heap: 40000, reset_reason: 1, fps: 60 },
    network: { wifi_rssi: -50 },
    nvs: { used: 10, free: 90 },
    config: { valid: true, error_count: 0, first_error: '', persistence_state: 'saved', primary_valid: true, backup_valid: true },
    startup: { state: 'ok', stage: 'app', failing_stage: '', failing_error: 0 },
    output: { state: 'running', fault: false, fault_events: 0, failed_commits: 0, last_error: 0, estop_latched: false, runtime_estop: false, control_source: 1, live_objects: 1, current_power_mw: 500, throttling_events: 0, active_preset: 0, watchdog_fallback: false },
    dmx: { sources_active: 0, universes_active: 0, sources_dropped: 0, packets_accepted: 0, packets_rejected_seq: 0, packets_ignored_unpatched: 0, submission_failures: 0, configured_universes: [] },
    sync: { packet_loss: 0 },
  };

  it('runPreflightCheck succeeds for healthy node', async () => {
    vi.spyOn(nodeApi, 'getCapabilities').mockResolvedValue(mockHealthyCapabilities as any);
    vi.spyOn(nodeApi, 'getHealth').mockResolvedValue(mockHealthyHealth as any);

    const check = await runPreflightCheck('lg-01', '192.168.1.51');
    expect(check.ok).toBe(true);
    expect(check.errors).toEqual([]);
    expect(check.family).toBe('led-node');
    expect(check.outputFault).toBe(false);
  });

  it('runPreflightCheck flags output fault and low heap', async () => {
    vi.spyOn(nodeApi, 'getCapabilities').mockResolvedValue(mockHealthyCapabilities as any);
    vi.spyOn(nodeApi, 'getHealth').mockResolvedValue({
      ...mockHealthyHealth,
      system: { ...mockHealthyHealth.system, free_heap: 10000 },
      output: { ...mockHealthyHealth.output, fault: true },
    } as any);

    const check = await runPreflightCheck('lg-02', '192.168.1.52');
    expect(check.ok).toBe(false);
    expect(check.errors.length).toBeGreaterThanOrEqual(2);
    expect(check.errors.some(e => e.includes('Insufficient free heap'))).toBe(true);
    expect(check.errors.some(e => e.includes('hardware output fault'))).toBe(true);
  });

  it('executeStagedDeployment halts when preflight fails', async () => {
    vi.spyOn(nodeApi, 'getCapabilities').mockResolvedValue(mockHealthyCapabilities as any);
    vi.spyOn(nodeApi, 'getHealth').mockResolvedValue({
      ...mockHealthyHealth,
      output: { ...mockHealthyHealth.output, fault: true },
    } as any);

    const postOtaSpy = vi.spyOn(firmwareApi, 'postOta');

    const plan: DeploymentPlan = {
      id: 'dep-1',
      targetVersion: '0.8.0',
      nodeIds: ['lg-01', 'lg-02'],
      canaryCount: 1,
      concurrency: 2,
      state: 'preflight',
    };

    const nodeMap = new Map([
      ['lg-01', '192.168.1.51'],
      ['lg-02', '192.168.1.52'],
    ]);

    const result = await executeStagedDeployment(plan, nodeMap, new Blob(['bin']));
    expect(result.success).toBe(false);
    expect(plan.state).toBe('failed');
    expect(postOtaSpy).not.toHaveBeenCalled();
  });

  it('executeStagedDeployment runs canary gate then remaining batch concurrency', async () => {
    vi.spyOn(nodeApi, 'getCapabilities').mockResolvedValue(mockHealthyCapabilities as any);
    vi.spyOn(nodeApi, 'getHealth').mockResolvedValue(mockHealthyHealth as any);
    const postOtaSpy = vi.spyOn(firmwareApi, 'postOta').mockResolvedValue({
      status: 'success',
      accepted: true,
      commandId: 10,
      jobId: 1,
    });

    const progressStates: string[] = [];

    const plan: DeploymentPlan = {
      id: 'dep-2',
      targetVersion: '0.8.0',
      nodeIds: ['lg-canary', 'lg-02', 'lg-03'],
      canaryCount: 1,
      concurrency: 2,
      state: 'preflight',
    };

    const nodeMap = new Map([
      ['lg-canary', '192.168.1.100'],
      ['lg-02', '192.168.1.102'],
      ['lg-03', '192.168.1.103'],
    ]);

    const result = await executeStagedDeployment(
      plan,
      nodeMap,
      new Blob(['bin']),
      p => progressStates.push(p.state),
      { verifyDelayMs: 10, verifyTimeoutMs: 200 }
    );

    expect(result.success).toBe(true);
    expect(result.completed).toEqual(['lg-canary', 'lg-02', 'lg-03']);
    expect(postOtaSpy).toHaveBeenCalledTimes(3);
    expect(progressStates).toContain('preflight');
    expect(progressStates).toContain('canary');
    expect(progressStates).toContain('verifying');
    expect(progressStates).toContain('deploying');
    expect(progressStates).toContain('completed');
  });
});
