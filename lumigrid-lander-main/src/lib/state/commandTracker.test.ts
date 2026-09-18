import { describe, expect, it, beforeEach, afterEach, vi } from 'vitest';
import { CommandTracker, type NodeStatusSnapshot } from './commandTracker';

describe('CommandTracker', () => {
  let tracker: CommandTracker;

  beforeEach(() => {
    vi.useFakeTimers();
    tracker = new CommandTracker();
  });

  afterEach(() => {
    tracker.clear();
    vi.useRealTimers();
  });

  it('registers a command with default state "accepted"', () => {
    const cmd = tracker.register({
      id: 101,
      domain: 'scene',
      nodeId: 'lg-aabbcc',
      generation: 42,
    });
    expect(cmd.state).toBe('accepted');
    expect(cmd.id).toBe(101);
    expect(tracker.get(101)).toBe(cmd);
  });

  it('observes Schema 2 appliedGeneration and marks scene command as applied', async () => {
    tracker.register({
      id: 200,
      domain: 'scene',
      nodeId: 'lg-test',
      generation: 15,
    });

    const waitPromise = tracker.waitFor(200, 3000);

    // Snapshot before generation reached
    tracker.observe({
      schema: 2,
      seq: 1,
      state: {
        scene: {
          appliedGeneration: 14,
          activePreset: 1,
        },
      },
    });
    expect(tracker.get(200)?.state).toBe('accepted');

    // Snapshot when generation reached
    tracker.observe({
      schema: 2,
      seq: 2,
      state: {
        scene: {
          appliedGeneration: 15,
          activePreset: 2,
        },
      },
    });

    const result = await waitPromise;
    expect(result.state).toBe('applied');
    expect(tracker.get(200)?.state).toBe('applied');
  });

  it('observes legacy/top-level appliedGeneration and resolves', async () => {
    tracker.register({
      id: 201,
      domain: 'scene',
      generation: 5,
    });

    const waitPromise = tracker.waitFor(201, 1000);

    tracker.observe({
      appliedGeneration: 5,
    });

    const result = await waitPromise;
    expect(result.state).toBe('applied');
  });

  it('times out if appliedGeneration is never received', async () => {
    tracker.register({
      id: 202,
      domain: 'scene',
      generation: 99,
    });

    const waitPromise = tracker.waitFor(202, 1000);

    vi.advanceTimersByTime(1001);

    await expect(waitPromise).rejects.toThrow('Timeout waiting for command 202');
  });

  it('resolves immediately if command was already applied', async () => {
    tracker.register({
      id: 203,
      domain: 'scene',
      generation: 10,
    });

    tracker.observe({
      appliedGeneration: 12,
    });

    const result = await tracker.waitFor(203, 1000);
    expect(result.state).toBe('applied');
  });

  it('handles job completion for ota domain', async () => {
    tracker.register({
      id: 77,
      domain: 'ota',
    });

    const waitPromise = tracker.waitFor(77, 2000);

    tracker.observe({
      schema: 2,
      seq: 10,
      state: {
        job: {
          jobId: 77,
          state: 'completed',
          error: null,
        },
      },
    });

    const result = await waitPromise;
    expect(result.state).toBe('applied');
  });

  it('rejects job on failure', async () => {
    tracker.register({
      id: 78,
      domain: 'ota',
    });

    const waitPromise = tracker.waitFor(78, 2000);

    tracker.observe({
      schema: 2,
      seq: 11,
      state: {
        job: {
          jobId: 78,
          state: 'failed',
          error: 'Write verification error',
        },
      },
    });

    await expect(waitPromise).rejects.toThrow('Write verification error');
    expect(tracker.get(78)?.state).toBe('failed');
  });
});
