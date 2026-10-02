import { describe, it, expect } from 'vitest';
import * as fs from 'fs';
import * as path from 'path';

describe('LG-036.1 Static Code Invariants (§61)', () => {
  const srcDir = path.resolve(__dirname, '../..'); // lumigrid-lander-main/src

  function getFiles(dir: string, fileList: string[] = []): string[] {
    const files = fs.readdirSync(dir);
    for (const file of files) {
      const fullPath = path.join(dir, file);
      if (fs.statSync(fullPath).isDirectory()) {
        getFiles(fullPath, fileList);
      } else if (file.endsWith('.ts') || file.endsWith('.svelte')) {
        fileList.push(fullPath);
      }
    }
    return fileList;
  }

  function stripComments(src: string): string {
    return src.replace(/\/\*[\s\S]*?\*\/|\/\/.*/g, '');
  }

  const allSourceFiles = getFiles(srcDir);

  it('no active frontend code references the legacy /update endpoint', () => {
    // Only legacy.ts and test files asserting /update rejection are exempt
    const offenders: string[] = [];

    for (const filePath of allSourceFiles) {
      const relPath = path.relative(srcDir, filePath).replace(/\\/g, '/');
      if (relPath.includes('legacy.ts') || relPath.endsWith('.test.ts')) {
        continue;
      }

      const content = stripComments(fs.readFileSync(filePath, 'utf-8'));
      if (content.includes("'/update'") || content.includes('"/update"') || content.includes('`/update`')) {
        offenders.push(relPath);
      }
    }

    expect(offenders).toEqual([]);
  });

  it('no OTA code derives protocol/scheme from location.protocol', () => {
    const offenders: string[] = [];

    for (const filePath of allSourceFiles) {
      const relPath = path.relative(srcDir, filePath).replace(/\\/g, '/');
      if (relPath.includes('transport.ts') || relPath.endsWith('.test.ts')) continue;

      const content = stripComments(fs.readFileSync(filePath, 'utf-8'));
      if (content.includes('location.protocol') && (content.includes('ota') || content.includes('firmware'))) {
        offenders.push(relPath);
      }
    }

    expect(offenders).toEqual([]);
  });

  it('system RestorePanel preflights before restoring', () => {
    const restorePanelPath = path.join(srcDir, 'lib/components/system/RestorePanel.svelte');
    expect(fs.existsSync(restorePanelPath)).toBe(true);

    const content = fs.readFileSync(restorePanelPath, 'utf-8');
    expect(content).toContain('validateRestorePackage');
    expect(content).toContain('restorePackage');
    // Ensure raw file content is passed, not JSON.parse
    expect(content).not.toContain('JSON.parse(content)');
  });

  it('system components do not use legacy api/node barrel for config/recovery/firmware', () => {
    const systemDir = path.join(srcDir, 'lib/components/system');
    const systemFiles = fs.readdirSync(systemDir).filter(f => f.endsWith('.svelte'));

    const barrelRegex = /import\s*\{[^}]*\b(getDeviceConfig|saveDeviceConfig|getBackup|restoreBackup|uploadFirmware|postOta)\b[^}]*\}\s*from\s*['"]\$lib\/api\/node['"]/;

    const offenders: string[] = [];
    for (const f of systemFiles) {
      const content = fs.readFileSync(path.join(systemDir, f), 'utf-8');
      if (barrelRegex.test(content)) {
        offenders.push(f);
      }
    }

    expect(offenders).toEqual([]);
  });
});
