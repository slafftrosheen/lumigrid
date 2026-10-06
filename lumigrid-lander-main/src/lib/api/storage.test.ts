import { describe, it, expect } from 'vitest';
import { firmwareFitsSlot } from './firmware';
describe('OTA capacity selection', () => {
  it('accepts an image up to the actual slot limit and rejects overflow/empty', () => {
    expect(firmwareFitsSlot(1300000, 0x250000)).toBe(true);
    expect(firmwareFitsSlot(0x250000, 0x250000)).toBe(true);
    expect(firmwareFitsSlot(0x250001, 0x250000)).toBe(false);
    expect(firmwareFitsSlot(0, 0x250000)).toBe(false);
    expect(firmwareFitsSlot(1300000, 0)).toBe(false);
  });
});
