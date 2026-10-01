import { describe, it, expect } from 'vitest';
import {
  MAX_CUE_LIST_NAME_BYTES,
  MAX_CUE_LABEL_BYTES,
  MAX_CUES_PER_LIST,
  createDefaultCue,
  createDefaultCueList,
  cueListsEqual,
  validateCueList,
  hasBlockingIssues,
  exportCueList,
  importCueList,
  toPutBody,
  utf8ByteLength,
  duplicateCueList,
  type CueListDocument,
} from './document';

function makeDoc(overrides: Partial<CueListDocument> = {}): CueListDocument {
  return {
    schema: 2,
    index: 2,
    checksum: 123456,
    name: 'Opening',
    mode: 'single_shot',
    endAction: 'hold_last',
    cues: [
      { sceneIndex: 0, holdMs: 3000, fadeMs: 1000, curve: 'easeInOut', label: 'Warm' },
      { sceneIndex: 1, holdMs: 8000, fadeMs: 500, curve: 'linear', label: 'Gold' },
    ],
    ...overrides,
  };
}

describe('cue document model (LG-034 §81 A..G)', () => {
  it('A. detects dirty documents structurally', () => {
    const a = makeDoc();
    const b = makeDoc();
    expect(cueListsEqual(a, b)).toBe(true);

    const edited = makeDoc({ name: 'Opening v2' });
    expect(cueListsEqual(a, edited)).toBe(false);

    const reordered = makeDoc({
      cues: [...a.cues].reverse(),
    });
    expect(cueListsEqual(a, reordered)).toBe(false);

    const timing = makeDoc({
      cues: [{ ...a.cues[0], holdMs: 3001 }, a.cues[1]],
    });
    expect(cueListsEqual(a, timing)).toBe(false);
  });

  it('B. excludes checksum from dirty comparison', () => {
    const a = makeDoc();
    const b = makeDoc({ checksum: 999999 });
    expect(cueListsEqual(a, b)).toBe(true);
  });

  it('C. enforces the name byte limit with UTF-8', () => {
    const issues = validateCueList(makeDoc({ name: 'x'.repeat(MAX_CUE_LIST_NAME_BYTES + 1) }), () => true);
    expect(issues.nameTooLong).toBe(true);
    expect(hasBlockingIssues(issues)).toBe(true);

    const multibyte = 'ä'.repeat(16); // 32 bytes > 31
    expect(utf8ByteLength(multibyte)).toBe(32);
    const issues2 = validateCueList(makeDoc({ name: multibyte }), () => true);
    expect(issues2.nameTooLong).toBe(true);

    const ok = validateCueList(makeDoc(), () => true);
    expect(ok.nameTooLong).toBe(false);
  });

  it('D. enforces the label byte limit with UTF-8', () => {
    const doc = makeDoc({
      cues: [{ ...createDefaultCue(0), label: 'é'.repeat(12) }], // 24 bytes > 23
    });
    const issues = validateCueList(doc, () => true);
    expect(issues.overlongLabels).toEqual([0]);
    expect(hasBlockingIssues(issues)).toBe(true);

    const okDoc = makeDoc({
      cues: [{ ...createDefaultCue(0), label: 'é'.repeat(11) }], // 22 bytes
    });
    const ok = validateCueList(okDoc, () => true);
    expect(ok.overlongLabels).toEqual([]);
  });

  it('E. blocks save on missing scene references', () => {
    const doc = makeDoc({
      cues: [{ ...createDefaultCue(17) }],
    });
    const issues = validateCueList(doc, (idx) => idx < 16);
    expect(issues.missingScenes).toEqual([{ cueIndex: 0, sceneIndex: 17 }]);
    expect(hasBlockingIssues(issues)).toBe(true);
  });

  it('F. validates imports strictly', () => {
    const doc = makeDoc();
    const exported = exportCueList(doc);

    const result = importCueList(exported, 5);
    expect(result.ok).toBe(true);
    if (result.ok) {
      expect(result.doc.index).toBe(5);
      expect(result.doc.name).toBe('Opening');
      expect(result.doc.mode).toBe('single_shot');
      expect(result.doc.cues).toHaveLength(2);
    }

    expect(importCueList({ ...exported, type: 'wrong-type' }, 0).ok).toBe(false);
    expect(importCueList({ ...exported, name: '' }, 0).ok).toBe(false);
    expect(importCueList({ ...exported, mode: 'weird' }, 0).ok).toBe(false);
    expect(importCueList({ ...exported, cues: [] }, 0).ok).toBe(false);
    expect(
      importCueList({ ...exported, cues: Array.from({ length: MAX_CUES_PER_LIST + 1 }, () => createDefaultCue(0)) }, 0).ok
    ).toBe(false);
    expect(
      importCueList({ ...exported, cues: [{ sceneIndex: 99, holdMs: 1, fadeMs: 1, curve: 'linear', label: '' }] }, 0).ok
    ).toBe(false);
    // Numeric endAction compatibility (§32)
    const numeric = importCueList({ ...exported, endAction: 2 }, 0);
    expect(numeric.ok).toBe(true);
    if (numeric.ok) expect(numeric.doc.endAction).toBe('restore_fallback');
  });

  it('G. exports contain no runtime state, checksums, or node identity', () => {
    const doc = makeDoc();
    const exported = exportCueList(doc) as unknown as Record<string, unknown>;
    expect(exported.type).toBe('lumigrid-cue-list');
    expect(exported).not.toHaveProperty('checksum');
    expect(exported).not.toHaveProperty('index');
    expect(exported).not.toHaveProperty('nodeId');
    expect(exported).not.toHaveProperty('generation');
    expect(exported).not.toHaveProperty('auth');
  });

  it('PUT body serializes explicit end action plus numeric compatibility', () => {
    const body = toPutBody(makeDoc({ endAction: 'restore_fallback' }), 123456);
    expect(body.endAction).toBe('restore_fallback');
    expect(body.endActionValue).toBe(2);
    expect(body.expectedChecksum).toBe(123456);
    expect(body.cues).toHaveLength(2);
  });

  it('duplicate creates an independent draft in a new slot', () => {
    const dup = duplicateCueList(makeDoc(), 4);
    expect(dup.index).toBe(4);
    expect(dup.checksum).toBeNull();
    expect(dup.name).toContain('copy');
    expect(dup.cues).toHaveLength(2);
    expect(dup.cues[0]).not.toBe(makeDoc().cues[0]);
  });
});
