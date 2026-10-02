import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  getRecoveryStatus,
  describeRecoveryCause,
  downloadBackup,
  saveBackupFile,
  validateRestorePackage,
  restorePackage,
  type RecoveryStatus,
} from './recovery';
import { parseBackupHeader } from '../recovery/backup';
import * as transport from './transport';

describe('recovery & backup/restore contracts (LG-036.1)', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  const healthyStatus: RecoveryStatus = {
    safeMode: false,
    configValid: true,
    commissioningState: 'none',
    startupState: 'running',
    failingStage: '',
    failingError: 0,
    recoveryNetworkActive: false,
    outputsForcedOff: false,
    otaState: 'idle',
    restoreState: 'none',
  };

  describe('Recovery Status & Causes (Requirements J, K, L, M, N, O)', () => {
    it('K. surfaces config-invalid cause visibly and human-readably', () => {
      const status: RecoveryStatus = {
        ...healthyStatus,
        safeMode: true,
        configValid: false,
        failingStage: 'config_load',
        failingError: 4001,
      };
      const desc = describeRecoveryCause(status);
      expect(desc).toContain('Configuration invalid');
    });

    it('L. surfaces interrupted restore cause visibly', () => {
      const status: RecoveryStatus = {
        ...healthyStatus,
        safeMode: true,
        restoreState: 'interrupted',
      };
      const desc = describeRecoveryCause(status);
      expect(desc).toContain('Restore interrupted');
      expect(desc).toContain('safe mode');
    });

    it('surfaces OTA verification failure cause', () => {
      const status: RecoveryStatus = {
        ...healthyStatus,
        safeMode: true,
        otaState: 'failed',
        failingError: 501,
      };
      const desc = describeRecoveryCause(status);
      expect(desc).toContain('OTA candidate verification failed');
    });

    it('M. outputsForcedOff is exposed accurately in RecoveryStatus', async () => {
      vi.spyOn(transport, 'apiJson').mockResolvedValue({
        safeMode: true,
        outputsForcedOff: true,
        configValid: false,
        commissioningState: 'physical_recovery',
      });

      const res = await getRecoveryStatus('192.168.1.1');
      expect(res.safeMode).toBe(true);
      expect(res.outputsForcedOff).toBe(true);
    });
  });

  describe('Backup Contract (Requirements P, Q, R)', () => {
    it('P & Q: backup uses apiFetch and returns raw blob without JSON.parse or reserialization', async () => {
      const rawNdjson = '{"type":"header","schema":4}\n{"type":"config","deviceName":"test"}\n';
      const mockBlob = new Blob([rawNdjson], { type: 'application/x-lumigrid-backup' });

      let calledPath = '';
      vi.spyOn(transport, 'apiFetch').mockImplementation(async (_ip, path) => {
        calledPath = path;
        return new Response(mockBlob, {
          status: 200,
          headers: {
            'Content-Type': 'application/x-lumigrid-backup',
            'Content-Disposition': 'attachment; filename="lumigrid-lg-node-01-backup.lgbak"',
          },
        });
      });

      const res = await downloadBackup('192.168.1.50');
      expect(calledPath).toBe('/api/v1/backup');
      expect(res.filename).toBe('lumigrid-lg-node-01-backup.lgbak');
      const text = await res.blob.text();
      expect(text).toBe(rawNdjson);
    });

    it('R: fallback backup filename uses immutable node ID, never IP address', async () => {
      const rawNdjson = '{"type":"header","schema":4}\n';
      const mockBlob = new Blob([rawNdjson], { type: 'application/x-lumigrid-backup' });

      vi.spyOn(transport, 'apiFetch').mockResolvedValue(
        new Response(mockBlob, {
          status: 200,
          headers: { 'Content-Type': 'application/x-lumigrid-backup' },
        })
      );

      // Trigger saveBackupFile with node ID
      let createdAnchor: any = null;
      const mockDoc = {
        createElement: vi.fn((tag: string) => {
          const el = {
            href: '',
            download: '',
            click: vi.fn(),
          };
          if (tag === 'a') createdAnchor = el;
          return el as any;
        }),
        body: {
          appendChild: vi.fn(),
          removeChild: vi.fn(),
        },
      };
      vi.stubGlobal('document', mockDoc);
      vi.stubGlobal('URL', {
        createObjectURL: vi.fn().mockReturnValue('blob:test'),
        revokeObjectURL: vi.fn(),
      });

      await saveBackupFile('192.168.1.50', 'immutable-node-999');

      expect(createdAnchor).toBeDefined();
      expect(createdAnchor.download).toBe('lumigrid-immutable-node-999-backup.lgbak');
      expect(createdAnchor.download).not.toContain('192.168.1.50');
    });
  });

  describe('Restore Contract (Requirements S, T, U, V)', () => {
    it('S: preflights restore file via POST /api/v1/restore/validate', async () => {
      const file = new Blob(['{"type":"header","schema":4}'], { type: 'application/x-lumigrid-backup' });

      let calledPath = '';
      let sentHeaders: any = null;
      vi.spyOn(transport, 'apiFetch').mockImplementation(async (_ip, path, init) => {
        calledPath = path;
        sentHeaders = init?.headers;
        return new Response(
          JSON.stringify({
            valid: true,
            schema: 4,
            source: { nodeId: 'node-1', firmwareVersion: '1.0.0' },
            packageChecksum: 0xABCD,
            plan: { deviceConfig: true, scenes: 3, sceneMotions: 0, timeline: false, calendar: false, cueLists: 1 },
            preserved: { authentication: true, networkCredentials: true, nodeIdentity: true, syncToken: true },
            warnings: [],
          }),
          { status: 200, headers: { 'Content-Type': 'application/json' } }
        );
      });

      const res = await validateRestorePackage('192.168.1.50', file);
      expect(calledPath).toBe('/api/v1/restore/validate');
      expect(sentHeaders['Content-Type']).toBe('application/x-lumigrid-backup');
      expect(res.valid).toBe(true);
      expect(res.plan.scenes).toBe(3);
      expect(res.preserved.authentication).toBe(true);
    });

    it('V: restore sends raw file bytes with Content-Type: application/x-lumigrid-backup', async () => {
      const file = new Blob(['raw-backup-bytes'], { type: 'application/x-lumigrid-backup' });

      let calledPath = '';
      let sentBody: any = null;
      let sentHeaders: any = null;
      vi.spyOn(transport, 'apiFetch').mockImplementation(async (_ip, path, init) => {
        calledPath = path;
        sentBody = init?.body;
        sentHeaders = init?.headers;
        return new Response(
          JSON.stringify({ accepted: true, commandId: 42, jobId: 9 }),
          { status: 200, headers: { 'Content-Type': 'application/json' } }
        );
      });

      const res = await restorePackage('192.168.1.50', file);
      expect(calledPath).toBe('/api/v1/restore');
      expect(sentHeaders['Content-Type']).toBe('application/x-lumigrid-backup');
      expect(sentBody).toBe(file);
      expect(res.accepted).toBe(true);
    });
  });

  describe('Backup Header Parser (Requirement 25 & 34)', () => {
    it('parses valid Schema 4 NDJSON header for display', async () => {
      const ndjson =
        '{"type":"header","schema":4,"source":{"nodeId":"node-abc","family":"led-node","model":"lg-led-v1","firmwareVersion":"1.0.0","hardwareRevision":"v1.0"},"policy":{"authIncluded":false,"networkSecretsIncluded":false,"syncSecretIncluded":false}}\n{"type":"config"}\n';
      const file = new Blob([ndjson]);

      const result = await parseBackupHeader(file);
      expect(result.ok).toBe(true);
      expect(result.header?.schema).toBe(4);
      expect(result.header?.source.nodeId).toBe('node-abc');
      expect(result.header?.policy.authIncluded).toBe(false);
      expect(result.isLegacyV3).toBe(false);
    });

    it('detects legacy v3 JSON backup', async () => {
      const legacyJson = '{"version":3,"config":{"deviceName":"old-node"},"scenes":[]}';
      const file = new Blob([legacyJson]);

      const result = await parseBackupHeader(file);
      expect(result.ok).toBe(true);
      expect(result.isLegacyV3).toBe(true);
    });
  });
});
