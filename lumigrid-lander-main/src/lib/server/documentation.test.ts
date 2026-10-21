import { describe, expect, it } from 'vitest';
import { documentHref, guides, loadGuide } from './documentation';
import { publicContent } from '$lib/publicContent';

describe('published documentation', () => {
  it('resolves guide links and history links with anchors', () => {
    expect(documentHref('GETTING_STARTED', 'FLASH_LAYOUT.md#ota')).toBe('/docs/FLASH_LAYOUT#ota');
    expect(documentHref('history/2026-10-21/ACCESS', '../../README.md')).toBe('/docs/README');
    expect(documentHref('README', 'history/2026-10-21/ACCESS.md')).toBe('/docs/history/2026-10-21/ACCESS');
  });

  it('does not publish unsafe schemes or invent missing local file URLs', () => {
    expect(documentHref('README', 'javascript:alert(1)')).toBeNull();
    expect(documentHref('README', '//example.com')).toBeNull();
    expect(documentHref('README', '../../../private.md')).toBeNull();
    expect(documentHref('README', 'MISSING.md')).toBeNull();
  });

  it('renders an actual guide with functioning section anchors', async () => {
    const guide = await loadGuide('FLASH_LAYOUT');
    expect(guide.content).toContain('id="ota"');
    expect(guide.content).toContain('0x250000');
    expect(guide.historical).toBe(false);
  });

  it('rejects unknown and prototype property slugs', async () => {
    await expect(loadGuide('not-a-guide')).rejects.toMatchObject({ status: 404 });
    await expect(loadGuide('__proto__')).rejects.toMatchObject({ status: 404 });
  });

  it('labels preserved historical documents', async () => {
    expect((await loadGuide('history/2026-10-21/LG-P0-BOOT-001_REPORT')).historical).toBe(true);
  });

  it('all translated feature cards link to published guides', () => {
    for (const copy of Object.values(publicContent)) {
      for (const [, , slug] of copy.features) expect(Object.hasOwn(guides, slug)).toBe(true);
    }
  });
});
