import { describe, expect, it } from 'vitest';
import { BUF_SIZE, decodePreviewFrame } from './nodeStore';

describe('bounded preview wire format', () => {
  it('decodes schema 2 and preserves both ends of every sampled lane', () => {
    const frame = new Uint8Array(4 + BUF_SIZE);
    frame.set([2, 8, 64, 0]);
    frame[4] = 17;
    frame[frame.length - 1] = 231;
    const result = decodePreviewFrame(frame)!;
    expect(result.length).toBe(BUF_SIZE);
    expect(result[0]).toBe(17);
    expect(result[result.length - 1]).toBe(231);
  });

  it('rejects invalid headers, truncated frames and oversized samples', () => {
    for (const header of [[2, 9, 64, 0], [2, 8, 65, 0], [2, 8, 0, 0], [2, 8, 64, 1]]) {
      expect(decodePreviewFrame(new Uint8Array(header))).toBeNull();
    }
    expect(decodePreviewFrame(new Uint8Array([2, 8, 64, 0]))).toBeNull();
    expect(decodePreviewFrame(new Uint8Array([3, 8, 64, 0]))).toBeNull();
  });

  it('resamples the historical 144-pixel frame without truncating the final lane', () => {
    const frame = new Uint8Array(1 + 8 * 144 * 3);
    frame[0] = 1;
    frame[frame.length - 1] = 91;
    const result = decodePreviewFrame(frame)!;
    expect(result[result.length - 1]).toBe(91);
  });
});
