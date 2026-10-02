/**
 * LumiGrid Recovery Package v4 parser.
 *
 * Parses bounded NDJSON (.lgbak) client-side for preflight display ONLY.
 * This does NOT reinterpret the package as a new canonical domain model.
 * The firmware remains the restore authority — do NOT write parsed data back.
 *
 * Never JSON.stringify this and re-upload it. Upload the original file bytes.
 */

// ── Package header type ────────────────────────────────────────────────────────

export interface RecoveryPackageHeader {
  type: 'header';
  schema: 4;
  source: {
    nodeId: string;
    family: string;
    model: string;
    firmwareVersion: string;
    hardwareRevision: string;
  };
  policy: {
    authIncluded: boolean;
    networkSecretsIncluded: boolean;
    syncSecretIncluded: boolean;
  };
}

export interface RecoveryPackageParseResult {
  ok: boolean;
  error?: string;
  header?: RecoveryPackageHeader;
  /** Number of NDJSON lines parsed successfully. */
  lineCount: number;
  /** Whether this looks like a legacy v3 JSON backup (pre-LG-036). */
  isLegacyV3: boolean;
}

/**
 * Parses the first few lines of a .lgbak NDJSON file to extract the header
 * for preflight display purposes only.
 *
 * Limits parsing to the first 8 KB to remain bounded. This is display-only;
 * the firmware validates and applies the full package authoritatively.
 */
export async function parseBackupHeader(file: Blob): Promise<RecoveryPackageParseResult> {
  const MAX_BYTES = 8192;
  const slice = file.slice(0, MAX_BYTES);

  let text: string;
  try {
    text = await slice.text();
  } catch (e) {
    return { ok: false, error: `Failed to read file: ${e}`, lineCount: 0, isLegacyV3: false };
  }

  // Detect legacy v3 JSON backup (single JSON object, not NDJSON)
  const trimmed = text.trimStart();
  if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
    // Could be a legacy JSON backup — try to parse as object
    try {
      const parsed = JSON.parse(text) as unknown;
      if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
        return {
          ok: true,
          lineCount: 1,
          isLegacyV3: true,
          // No header for legacy format — firmware preflight will validate
        };
      }
    } catch {
      // Not valid JSON either
    }
  }

  // Parse NDJSON: one JSON object per line
  const lines = text.split('\n').filter((l) => l.trim().length > 0);
  if (lines.length === 0) {
    return { ok: false, error: 'File appears to be empty', lineCount: 0, isLegacyV3: false };
  }

  // First line must be the header record
  let header: RecoveryPackageHeader | undefined;
  try {
    const firstRecord = JSON.parse(lines[0]) as { type?: string; schema?: number };
    if (firstRecord.type !== 'header') {
      return {
        ok: false,
        error: `Unexpected first record type: "${firstRecord.type}" (expected "header")`,
        lineCount: lines.length,
        isLegacyV3: false,
      };
    }
    if (firstRecord.schema !== 4) {
      return {
        ok: false,
        error: `Unsupported backup schema: ${firstRecord.schema} (expected 4)`,
        lineCount: lines.length,
        isLegacyV3: false,
      };
    }
    header = firstRecord as unknown as RecoveryPackageHeader;
  } catch (e) {
    return {
      ok: false,
      error: `Failed to parse backup header: ${e}`,
      lineCount: lines.length,
      isLegacyV3: false,
    };
  }

  return {
    ok: true,
    header,
    lineCount: lines.length,
    isLegacyV3: false,
  };
}
