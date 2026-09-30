/**
 * CommandTracker: Tracks in-flight control-plane commands and resolves them
 * when authoritative device state confirms execution (via WebSocket state path).
 */

export type CommandDomain =
  | 'scene'
  | 'timeline'
  | 'schedule'
  | 'config'
  | 'factory'
  | 'ota'
  | 'restore'
  | 'sync'
  | 'sceneMotion'
  | 'cue';

export type CommandState =
  | 'accepted'
  | 'applying'
  | 'applied'
  | 'rejected'
  | 'failed'
  | 'superseded';

export interface PendingCommand {
  id: number;
  commandId?: number;
  jobId?: number;
  domain: CommandDomain;
  nodeId?: string;
  nodeIp?: string;
  acceptedAt: number;
  state: CommandState;
  generation?: number;
  error?: string;
  resolve?: (cmd: PendingCommand) => void;
  reject?: (err: Error) => void;
  timer?: ReturnType<typeof setTimeout>;
}

export interface NodeStatusSnapshot {
  schema?: number;
  seq?: number;
  streamSeq?: number;
  nodeId?: string;
  appliedGeneration?: number;
  activePreset?: number;
  scene?: {
    appliedGeneration?: number;
    activePreset?: number;
    requestedGeneration?: number;
    applyPending?: boolean;
  };
  job?: {
    jobId?: number;
    type?: string;
    state?: string;
    progress?: number;
    startedMs?: number;
    error?: string | null;
  } | null;
  state?: {
    scene?: {
      appliedGeneration?: number;
      activePreset?: number;
      requestedGeneration?: number;
      sceneApplyPending?: boolean;
    };
    job?: {
      jobId?: number;
      state?: string;
      error?: string | null;
    } | null;
  };
  status?: {
    appliedGeneration?: number;
    activePreset?: number;
  };
}

export class CommandTracker {
  private pending = new Map<number, PendingCommand>();

  /**
   * Registers a pending command.
   */
  register(command: Omit<PendingCommand, 'state' | 'acceptedAt'> & { state?: CommandState; acceptedAt?: number }): PendingCommand {
    const fullCmd: PendingCommand = {
      ...command,
      state: command.state ?? 'accepted',
      acceptedAt: command.acceptedAt ?? Date.now(),
    };
    this.pending.set(fullCmd.id, fullCmd);
    return fullCmd;
  }

  get(id: number): PendingCommand | undefined {
    return this.pending.get(id);
  }

  has(id: number): boolean {
    return this.pending.has(id);
  }

  getAll(): PendingCommand[] {
    return Array.from(this.pending.values());
  }

  /**
   * Observes a live snapshot received from WebSocket and reconciles pending commands.
   */
  observe(snapshot: NodeStatusSnapshot): void {
    if (!snapshot) return;

    // Resolve applied generation from Schema 3 (snapshot.scene.appliedGeneration)
    // or Schema 2 (snapshot.state.scene.appliedGeneration) or legacy top-level
    const appliedGen =
      snapshot.scene?.appliedGeneration ??
      snapshot.state?.scene?.appliedGeneration ??
      snapshot.appliedGeneration ??
      snapshot.status?.appliedGeneration;

    const job = snapshot.job !== undefined ? snapshot.job : snapshot.state?.job;

    for (const cmd of this.pending.values()) {
      if (cmd.state === 'applied' || cmd.state === 'failed' || cmd.state === 'rejected' || cmd.state === 'superseded') {
        continue;
      }

      // 1. Scene domain commands: check applied generation
      if (cmd.domain === 'scene' && cmd.generation !== undefined && appliedGen !== undefined) {
        if (appliedGen >= cmd.generation) {
          this.markApplied(cmd);
        }
      }

      // 2. Job-based commands (OTA / Restore / Factory burn-in)
      // Requirement 29: Command matching must use actual authoritative IDs.
      if ((cmd.domain === 'ota' || cmd.domain === 'restore' || cmd.domain === 'factory') && job) {
        const matchesJob =
          (cmd.jobId !== undefined && cmd.jobId === job.jobId) ||
          (cmd.jobId === undefined && (cmd.commandId === job.jobId || cmd.id === job.jobId));

        if (matchesJob) {
          if (job.state === 'completed') {
            this.markApplied(cmd);
          } else if (job.state === 'failed') {
            this.markFailed(cmd, job.error || 'Job failed on device');
          }
        }
      }
    }
  }

  private markApplied(cmd: PendingCommand): void {
    cmd.state = 'applied';
    if (cmd.timer) clearTimeout(cmd.timer);
    if (cmd.resolve) {
      cmd.resolve(cmd);
    }
  }

  private markFailed(cmd: PendingCommand, error: string): void {
    cmd.state = 'failed';
    cmd.error = error;
    if (cmd.timer) clearTimeout(cmd.timer);
    if (cmd.reject) {
      cmd.reject(new Error(error));
    }
  }

  /**
   * Returns a promise that resolves when the command is observed as 'applied',
   * or rejects if 'failed' or if timeout occurs.
   */
  waitFor(commandId: number, timeoutMs = 5000): Promise<PendingCommand> {
    const cmd = this.pending.get(commandId);
    if (!cmd) {
      return Promise.reject(new Error(`Command ${commandId} not found in tracker`));
    }

    if (cmd.state === 'applied') {
      return Promise.resolve(cmd);
    }
    if (cmd.state === 'failed' || cmd.state === 'rejected') {
      return Promise.reject(new Error(cmd.error || `Command ${commandId} ${cmd.state}`));
    }

    return new Promise<PendingCommand>((resolve, reject) => {
      cmd.resolve = resolve;
      cmd.reject = reject;
      cmd.timer = setTimeout(() => {
        if (cmd.state !== 'applied') {
          cmd.state = 'failed';
          cmd.error = `Timeout waiting for command ${commandId} after ${timeoutMs}ms`;
          this.pending.delete(commandId);
          reject(new Error(cmd.error));
        }
      }, timeoutMs);
    });
  }

  clear(): void {
    for (const cmd of this.pending.values()) {
      if (cmd.timer) clearTimeout(cmd.timer);
    }
    this.pending.clear();
  }
}

export const commandTracker = new CommandTracker();
